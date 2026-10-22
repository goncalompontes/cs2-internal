#!/usr/bin/env python3
"""Check that every `protect` entry of the policy matched a function.

An LLVM pass sees one translation unit at a time, so it cannot know whether a
policy entry matched *somewhere* in the build. The plugin therefore writes
one coverage file per module; this script reads them after the build and
fails it when an entry matched nothing. A typo must not silently protect
nothing.

The plugin and this script must agree on the policy file, so both reject the
same things: a duplicated key, an unknown key, a malformed entry. The plugin
reads the file with the LLVM SDK's JSON parser (which keeps the *first* of
two equal keys); Python's `json` keeps the *last*. A duplicated key is
therefore an error on both sides, with the same message.

The script has a second mode, `--seed`: it prints the seed of the build (the
environment's `CS2_PROTECT_SEED`, else the policy's `seed`) and fails when
there is none. The wiring runs it at **configure** time, so that a protected
build without a seed fails before it compiles anything. The rule is the
plugin's, and the script is the only place that reads the policy for it: two
readers of the policy are two rules that drift.

Usage: check_policy.py [--policy FILE] [--coverage DIR] [--source-root DIR] [--output FILE]
                        [--seed]

  --policy      the policy file (default: $CS2_PROTECT_CONFIG)
  --coverage    the coverage directory (default: $CS2_PROTECT_COVERAGE, else
                <temp>/cs2-protect-coverage/<sha256 of the policy path>)
  --source-root the tree that is being built (default: $CS2_PROTECT_SOURCE_ROOT);
                the coverage of another tree is not counted
  --output      a stamp file to write when the check passes
  --seed        print the seed of the build and exit: the exit code is 1 when
                the build has no seed at all, and 2 when the seed or the
                policy is malformed

The coverage directory default is the same one the plugin computes, so the
two agree without the build having to pass anything around.

Exit code 0 when every entry matched (or when there is no policy at all),
1 when an entry matched nothing, an entry matched a function that cannot be
protected, or the policy cannot be read.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any, NamedTuple

# The coverage format is shared with the payload check: the plugin writes it
# and the two readers have to agree on it.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from protect_checks import (  # noqa: E402
    Coverage,
    cross_state_priority,
    default_coverage_dir,
    read_coverage as read_coverage_files,
)

#: The keys the policy file may carry.
POLICY_KEYS = (
    "protect",
    "cross_module",
    "deny",
    "seed",
    "strings",
    "strings_deny",
    "strings_min_length",
)

#: The keys a `protect` entry object may carry.
ENTRY_KEYS = ("symbol", "module", "level")

#: The priority of the states a policy entry can be in.
#:
#: The worst state wins, so that a failure is never masked by a match in
#: another module: an entry whose selected function did not survive to the
#: transform phase failed in that module, whatever it did in another one.
STATE_PRIORITY = {
    "unmatched": 0,
    "blocked": 1,
    "matched": 2,
    "unimplemented": 3,
    "vanished": 4,
}


#: The exit code of the `--seed` mode when the build has no seed at all.
EXIT_NO_SEED = 1

#: The exit code of the `--seed` mode when the seed or the policy is
#: malformed. It is not the exit code of the absence of a seed, because the
#: two failures are told apart by the wiring: a malformed policy is the
#: plugin's error to report, and it must not be hidden behind a seed error.
EXIT_INVALID = 2


def state_priority(state: str) -> int:
    """The priority of the state of a policy entry."""
    return STATE_PRIORITY.get(state.split(":", 1)[0], 0)


def no_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """The JSON object hook that rejects a duplicated key.

    The plugin rejects it too, with the same message: both sides must fail
    for the same reason, or the build fails with a diagnostic that names a
    pattern the plugin never looked at.
    """
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"the policy file has a duplicate key '{key}'")
        result[key] = value
    return result


def check_policy_keys(policy: dict[str, Any]) -> None:
    """Reject the top-level keys the plugin rejects."""
    for key in sorted(policy):
        if key not in POLICY_KEYS:
            raise ValueError(
                f"unknown key in the policy file: '{key}' "
                "(expected 'protect', 'cross_module', 'deny', 'seed', 'strings', 'strings_deny' or "
                "'strings_min_length')"
            )


def check_policy_values(policy: dict[str, Any]) -> None:
    """Reject the `deny`, `cross_module`, `strings` and `seed` shapes the plugin rejects."""
    deny = policy.get("deny", [])
    if not isinstance(deny, list):
        raise ValueError("'deny' must be an array")
    for entry in deny:
        if not isinstance(entry, str):
            raise ValueError("a 'deny' entry must be a string")
    cross = policy.get("cross_module", [])
    if not isinstance(cross, list):
        raise ValueError("'cross_module' must be an array")
    for entry in cross:
        if not isinstance(entry, str):
            raise ValueError("a 'cross_module' entry must be a string")
    seed = policy.get("seed")
    if seed is not None and not isinstance(seed, str):
        raise ValueError("'seed' must be a hexadecimal string")
    for key in ("strings", "strings_deny"):
        entries = policy.get(key, [])
        if not isinstance(entries, list):
            raise ValueError(f"'{key}' must be an array")
        for entry in entries:
            if not isinstance(entry, str):
                raise ValueError(f"a '{key}' entry must be a string")
    minimum = policy.get("strings_min_length")
    if minimum is not None and (not isinstance(minimum, int) or isinstance(minimum, bool)):
        raise ValueError("'strings_min_length' must be a number")
    if isinstance(minimum, int) and minimum < 0:
        raise ValueError("'strings_min_length' must not be negative")


def policy_patterns(policy: dict[str, Any]) -> list[str]:
    """The glob of every `protect` entry of the policy."""
    entries = policy.get("protect", [])
    if not isinstance(entries, list):
        raise ValueError("'protect' must be an array")
    patterns: list[str] = []
    for entry in entries:
        if isinstance(entry, str):
            patterns.append(entry)
            continue
        if not isinstance(entry, dict):
            raise ValueError("a 'protect' entry must be a string or an object")
        symbol = entry.get("symbol")
        if not isinstance(symbol, str):
            raise ValueError("a 'protect' object needs a 'symbol' string")
        module = entry.get("module")
        if module is not None and not isinstance(module, str):
            raise ValueError("the 'module' of a 'protect' entry must be a string")
        level = entry.get("level")
        if level is not None and not isinstance(level, str):
            raise ValueError("the 'level' of a 'protect' entry must be a string")
        if level is not None and level not in ("call", "virtualized"):
            raise ValueError(
                f"unknown protection level: '{level}' (expected 'call' or 'virtualized')"
            )
        for key in sorted(entry):
            if key not in ENTRY_KEYS:
                raise ValueError(f"unknown key in a 'protect' entry: '{key}'")
        patterns.append(symbol)
    return patterns


class PolicyStates(NamedTuple):
    """What the coverage files of a policy say about its entries."""

    #: The state of every `protect` entry, over every module.
    entries: dict[str, str]
    #: The state of every `cross_module` entry, over every module.
    cross: dict[str, str]
    #: Whether any coverage file belongs to this policy at all.
    current: bool


def read_coverage(coverage_dir: Path, policy_path: Path,
                  source_root: Path | None = None) -> PolicyStates:
    """The state of every policy entry, over every coverage file of this policy.

    The state of an entry is "matched" (it named a function that is
    protected), "blocked" (it named a function that cannot be protected),
    "unimplemented" (it named a function whose protection level is not
    implemented yet), "vanished" (the selected function did not survive to
    the transform phase) or "unmatched" (it named nothing in that module).

    The second value says whether any coverage file belongs to this policy at
    all: when none does, the coverage is stale (the policy changed after the
    last compile) and the check cannot say anything.

    The `cross_module` entries have their own priority: the symbol has to
    be routed *everywhere* it is called, so a call that could not be routed
    outranks a routing.

    A coverage file that does not describe *this* build is not evidence about
    it: a file that was edited by hand (its content digest does not match),
    a file of an earlier state of the source (the digest of the source does
    not match) and the coverage of another tree (another worktree that
    shares the policy) are skipped, and the reason is reported. Without
    that, a file left behind by another build would fail the build of a good
    one.
    """
    entries: dict[str, str] = {}
    cross: dict[str, str] = {}
    current = False
    if not coverage_dir.is_dir():
        return PolicyStates(entries, cross, current)
    digest = hashlib.sha256(policy_path.read_bytes()).hexdigest().upper()
    files, skipped, malformed = read_coverage_files(coverage_dir, policy_path, source_root)
    for name, reason in malformed:
        print(f"check_policy: the coverage '{name}' is malformed: {reason}", file=sys.stderr)
    for name, reason, rewritten in skipped:
        if rewritten > 0:
            print(f"check_policy: the coverage '{name}' is skipped: {reason}", file=sys.stderr)
    for entry in files:
        if entry.policy != digest:
            # The coverage of another policy; it says nothing about this one.
            continue
        current = True
        for pattern, state in entry.states().items():
            if pattern not in entries or state_priority(state) > state_priority(entries[pattern]):
                entries[pattern] = state
        for pattern, state in entry.cross_states().items():
            if pattern not in cross or (
                cross_state_priority(state) > cross_state_priority(cross[pattern])
            ):
                cross[pattern] = state
    return PolicyStates(entries, cross, current)


def prune_stale_coverage(coverage_dir: Path, policy_path: Path) -> int:
    """Remove the coverage files that do not describe this build.

    The directory is keyed by the policy path, so an edited policy leaves the
    files of the previous version behind. They say nothing about the current
    policy, and they are the only thing that makes the directory grow
    without a bound. The files of the current policy are left alone: the
    check may run again (a rebuild that compiles nothing) and has to find
    its evidence.

    A file of the current policy that does not describe the current build --
    an older plugin wrote it, or it was edited by hand -- is removed too:
    it is not evidence about anything, and leaving it would let it be
    counted by a later check.

    The pruning runs *after* the build, so it cannot race with a compile
    that is writing its coverage.
    """
    removed = 0
    if not coverage_dir.is_dir():
        return removed
    digest = hashlib.sha256(policy_path.read_bytes()).hexdigest().upper()
    for path in coverage_dir.glob("*.cov"):
        policy_digest = ""
        try:
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                key, _, value = line.partition(" ")
                if key == "policy-sha256":
                    policy_digest = value.strip().upper()
                    break
        except OSError:
            continue
        stale = policy_digest != digest
        if not stale:
            entry = Coverage(path)
            stale = not entry.intact or not entry.source_is_current()
        if stale:
            try:
                path.unlink()
                removed += 1
            except OSError:
                pass
    return removed


def read_the_policy(policy_path: str) -> tuple[dict[str, Any] | None, str]:
    """The policy of the build, and the reason it cannot be read.

    The plugin and this script must reject the same things: a duplicated
    key, an unknown key, a malformed entry. The plugin reads the file with
    the LLVM SDK's JSON parser (which keeps the *first* of two equal keys);
    Python's `json` keeps the *last*. A duplicated key is therefore an error
    on both sides, with the same message.

    This is the **only** reader of the policy in the Python tools: the seed
    of the build and the check of the entries read the same file with the
    same rules, and two readers are two rules that drift.
    """
    policy_file = Path(policy_path)
    try:
        # "utf-8-sig" accepts the byte order mark that some Windows editors
        # write; the plugin accepts and skips it too.
        raw = json.loads(
            policy_file.read_text(encoding="utf-8-sig"), object_pairs_hook=no_duplicate_keys
        )
    except (OSError, ValueError) as error:
        return None, f"{policy_path}: {error}"
    if not isinstance(raw, dict):
        return None, f"{policy_path}: the policy file must contain a JSON object"
    try:
        check_policy_keys(raw)
        policy_patterns(raw)
        check_policy_values(raw)
    except ValueError as error:
        return None, f"{policy_path}: {error}"
    return raw, ""


def is_hexadecimal(text: str) -> bool:
    """Whether the text is a hexadecimal seed, the way the plugin reads it.

    The plugin reads a seed as a hexadecimal string with an optional `0x`
    prefix (`src/config.cpp`: `parse_seed`), and it rejects everything else.
    The probe has to accept exactly what the plugin accepts: a seed the
    plugin rejects has to fail the **configure** step, where the message can
    name it, and not the compile step.
    """
    digits = text
    if len(digits) > 2 and digits[0] == "0" and digits[1] in "xX":
        digits = digits[2:]
    if not digits:
        return False
    return all(c in "0123456789abcdefABCDEF" for c in digits)


def the_seed(policy_path: str) -> int:
    """The seed of the build, and the reason there is none.

    The rule is the plugin's (`src/config.cpp`): the **environment** wins
    over the policy, and a seed is a **hexadecimal string**.

    The probe accepts exactly what the plugin accepts, and it reports the
    plugin's own errors: a policy the plugin rejects is the plugin's error to
    report, and the probe must not hide it behind a seed error. That is why
    the two failures have two exit codes.
    """
    seed = os.environ.get("CS2_PROTECT_SEED", "").strip()
    if seed:
        if not is_hexadecimal(seed):
            print(f"CS2_PROTECT_SEED: the seed is not hexadecimal: '{seed}'", file=sys.stderr)
            return EXIT_INVALID
        sys.stdout.write(seed)
        return 0

    no_seed = (
        "the build has no seed: set CS2_PROTECT_SEED to a 64-bit hexadecimal value, or put "
        'a "seed" in the policy file'
    )
    if not policy_path:
        print(no_seed, file=sys.stderr)
        return EXIT_NO_SEED

    raw, reason = read_the_policy(policy_path)
    if raw is None:
        # The policy is malformed. The plugin reports it with the same
        # message, and the probe must not hide it behind a seed error.
        print(reason, file=sys.stderr)
        return EXIT_INVALID

    policy_seed = raw.get("seed")
    if policy_seed is None:
        print(no_seed, file=sys.stderr)
        return EXIT_NO_SEED
    if not isinstance(policy_seed, str) or not is_hexadecimal(policy_seed):
        print(f"CS2_PROTECT_CONFIG: {policy_path}: the seed is not hexadecimal: '{policy_seed}'",
              file=sys.stderr)
        return EXIT_INVALID
    sys.stdout.write(policy_seed)
    return 0


def main(argv: list[str]) -> int:
    # `set VAR=value && something` in cmd.exe leaves a trailing space in the
    # value; the plugin trims it as well, so the two agree on the path.
    policy_path = os.environ.get("CS2_PROTECT_CONFIG", "").strip()
    coverage_dir: Path | None = None
    source_root: Path | None = None
    output: Path | None = None
    seed_only = False
    args = list(argv)
    while args:
        arg = args.pop(0)
        if arg == "--policy" and args:
            policy_path = args.pop(0).strip()
        elif arg == "--coverage" and args:
            coverage_dir = Path(args.pop(0))
        elif arg == "--source-root" and args:
            source_root = Path(args.pop(0))
        elif arg == "--output" and args:
            output = Path(args.pop(0))
        elif arg == "--seed":
            seed_only = True
        else:
            print(f"check_policy: unknown argument '{arg}'", file=sys.stderr)
            return 2

    if seed_only:
        # The seed of the build: the wiring runs it at configure time, and
        # the coverage of the build does not exist yet.
        return the_seed(policy_path)

    if not policy_path:
        # No policy: the pass had nothing to select, so there is nothing to
        # check.
        return 0

    policy_file = Path(policy_path)
    raw, reason = read_the_policy(policy_path)
    if raw is None:
        print(f"check_policy: {reason}", file=sys.stderr)
        return 1
    patterns = policy_patterns(raw)

    if coverage_dir is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE", "").strip()
        coverage_dir = Path(env) if env else default_coverage_dir(policy_path)
    if source_root is None:
        env = os.environ.get("CS2_PROTECT_SOURCE_ROOT", "").strip()
        source_root = Path(env) if env else None

    digest = hashlib.sha256(policy_file.read_bytes()).hexdigest().upper()
    states = read_coverage(coverage_dir, policy_file, source_root)
    if not states.current:
        print(
            f"check_policy: {policy_path}: no coverage of this policy in "
            f"'{coverage_dir}'.\n"
            "              The policy changed after the last compile (or the "
            "payload was not built with the pass enabled). Re-run the Meson "
            "configure step and the build:\n"
            "                meson setup --reconfigure <build dir> payload "
            "-Dprotect=true -Dprotect_plugin=<path>",
            file=sys.stderr,
        )
        return 1

    matched = {pattern for pattern, state in states.entries.items() if state == "matched"}
    blocked = {
        pattern: state.split(":", 1)[1]
        for pattern, state in states.entries.items()
        if state.startswith("blocked:")
    }
    unimplemented = {
        pattern: state.split(":", 1)[1]
        for pattern, state in states.entries.items()
        if state.startswith("unimplemented:")
    }
    vanished = {pattern for pattern, state in states.entries.items() if state.startswith("vanished")}
    failed = False

    # A selected function that did not survive to the transform phase: the
    # protection would be silently lost, so the build has to fail.
    if vanished:
        noun = "entry" if len(vanished) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(vanished)} policy {noun} named a selected "
            "function that did not survive to the transform phase:",
            file=sys.stderr,
        )
        for pattern in sorted(vanished):
            print(f"  {pattern}", file=sys.stderr)
        failed = True

    # A level that is not implemented yet: the entry protects nothing, and a
    # build that looks protected and is not is the failure mode this project
    # exists to avoid.
    if unimplemented:
        noun = "entry" if len(unimplemented) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(unimplemented)} policy {noun} named a function "
            "whose protection level is not implemented:",
            file=sys.stderr,
        )
        for pattern, level in sorted(unimplemented.items()):
            print(f"  {pattern} ({level})", file=sys.stderr)
        failed = True

    # An entry that named a function that cannot be protected protects
    # nothing: the build has to fail, and it has to say why.
    blocked_only = [
        pattern
        for pattern in patterns
        if pattern not in matched and pattern in blocked and pattern not in vanished
    ]
    if blocked_only:
        noun = "entry" if len(blocked_only) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(blocked_only)} policy {noun} matched a function "
            "that cannot be protected:",
            file=sys.stderr,
        )
        for pattern in blocked_only:
            print(f"  {pattern} ({blocked[pattern]})", file=sys.stderr)
        failed = True

    unmatched = [
        pattern
        for pattern in patterns
        if pattern not in matched
        and pattern not in blocked
        and pattern not in vanished
        and pattern not in unimplemented
    ]
    if unmatched:
        noun = "entry" if len(unmatched) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(unmatched)} policy {noun} matched no function:",
            file=sys.stderr,
        )
        for pattern in unmatched:
            print(f"  {pattern}", file=sys.stderr)
        failed = True

    # A `cross_module` entry has to be routed *everywhere* it is called: the
    # worst state wins, and an entry that is not "matched" protects nothing.
    # The rule is the one the `protect` entries follow, with the priority
    # inverted: for a `protect` entry a match in one module is enough, for a
    # `cross_module` entry every call has to be routed.
    cross_patterns = raw.get("cross_module", [])
    cross_blocked = {
        pattern: state.split(":", 1)[1]
        for pattern, state in states.cross.items()
        if state.startswith("blocked:")
    }
    cross_unmatched = [
        pattern for pattern in cross_patterns if states.cross.get(pattern, "unmatched") == "unmatched"
    ]
    cross_refused = [
        pattern
        for pattern in cross_patterns
        if pattern in cross_blocked and states.cross.get(pattern) != "matched"
    ]
    if cross_refused:
        noun = "entry" if len(cross_refused) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(cross_refused)} cross_module {noun} named a "
            "declaration whose call could not be routed:",
            file=sys.stderr,
        )
        for pattern in sorted(cross_refused):
            print(f"  {pattern} ({cross_blocked[pattern]})", file=sys.stderr)
        failed = True
    if cross_unmatched:
        noun = "entry" if len(cross_unmatched) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(cross_unmatched)} cross_module {noun} matched no "
            "declaration:",
            file=sys.stderr,
        )
        for pattern in sorted(cross_unmatched):
            print(f"  {pattern}", file=sys.stderr)
        failed = True

    if failed:
        return 1

    prune_stale_coverage(coverage_dir, policy_file)
    print(f"check_policy: {len(patterns)} policy entries matched")
    if len(cross_patterns) > 0:
        matched_cross = len(cross_patterns) - len(cross_refused) - len(cross_unmatched)
        print(f"check_policy: {matched_cross} cross_module entries routed every call")
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(f"{len(patterns)} entries matched\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
