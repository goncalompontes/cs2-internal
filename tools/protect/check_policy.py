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

Usage: check_policy.py [--policy FILE] [--coverage DIR] [--output FILE]

  --policy    the policy file (default: $CS2_PROTECT_CONFIG)
  --coverage  the coverage directory (default: $CS2_PROTECT_COVERAGE, else
              <temp>/cs2-protect-coverage/<sha256 of the policy path>)
  --output    a stamp file to write when the check passes

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
from typing import Any

#: The keys the policy file may carry.
POLICY_KEYS = ("protect", "deny", "seed")

#: The keys a `protect` entry object may carry.
ENTRY_KEYS = ("symbol", "module", "level")


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
                "(expected 'protect', 'deny' or 'seed')"
            )


def check_policy_values(policy: dict[str, Any]) -> None:
    """Reject the `deny` and `seed` shapes the plugin rejects."""
    deny = policy.get("deny", [])
    if not isinstance(deny, list):
        raise ValueError("'deny' must be an array")
    for entry in deny:
        if not isinstance(entry, str):
            raise ValueError("a 'deny' entry must be a string")
    seed = policy.get("seed")
    if seed is not None and not isinstance(seed, str):
        raise ValueError("'seed' must be a hexadecimal string")


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


def default_coverage_dir(policy_path: str) -> Path:
    """The directory the plugin writes the coverage to."""
    digest = hashlib.sha256(policy_path.encode()).hexdigest().upper()[:16]
    root = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    return Path(root) / "cs2-protect-coverage" / digest


def read_coverage(coverage_dir: Path, digest: str) -> tuple[dict[str, str], bool]:
    """The state of every policy entry, over every coverage file of this policy.

    The state of an entry is "matched" (it named a function that is
    protected), "blocked" (it named a function that cannot be protected) or
    "unmatched" (it named nothing in that module). A blocked entry is not a
    matched one: it protects nothing.

    The second value says whether any coverage file belongs to this policy at
    all: when none does, the coverage is stale (the policy changed after the
    last compile) and the check cannot say anything.
    """
    states: dict[str, str] = {}
    current = False
    if not coverage_dir.is_dir():
        return states, current
    for path in sorted(coverage_dir.glob("*.cov")):
        entries: dict[str, str] = {}
        policy_digest = ""
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            key, _, value = line.partition(" ")
            if key == "policy-sha256":
                policy_digest = value.strip().upper()
            elif key == "entry":
                state, _, rest = value.partition(" ")
                if state == "blocked":
                    reason, _, pattern = rest.partition(" ")
                    entries[pattern.strip()] = f"blocked:{reason.strip()}"
                else:
                    entries[rest.strip()] = state.strip()
        if policy_digest != digest:
            # The coverage of another policy; it says nothing about this one.
            continue
        current = True
        for pattern, state in entries.items():
            if state == "matched":
                states[pattern] = "matched"
            elif state.startswith("blocked:") and states.get(pattern) != "matched":
                states[pattern] = state
            elif pattern not in states:
                states[pattern] = "unmatched"
    return states, current


def prune_stale_coverage(coverage_dir: Path, digest: str) -> int:
    """Remove the coverage files of the *other* policies.

    The directory is keyed by the policy path, so an edited policy leaves the
    files of the previous version behind. They say nothing about the current
    policy, and they are the only thing that makes the directory grow
    without a bound. The files of the current policy are left alone: the
    check may run again (a rebuild that compiles nothing) and has to find
    its evidence.

    The pruning runs *after* the build, so it cannot race with a compile
    that is writing its coverage.
    """
    removed = 0
    if not coverage_dir.is_dir():
        return removed
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
        if policy_digest != digest:
            try:
                path.unlink()
                removed += 1
            except OSError:
                pass
    return removed


def main(argv: list[str]) -> int:
    # `set VAR=value && something` in cmd.exe leaves a trailing space in the
    # value; the plugin trims it as well, so the two agree on the path.
    policy_path = os.environ.get("CS2_PROTECT_CONFIG", "").strip()
    coverage_dir: Path | None = None
    output: Path | None = None
    args = list(argv)
    while args:
        arg = args.pop(0)
        if arg == "--policy" and args:
            policy_path = args.pop(0).strip()
        elif arg == "--coverage" and args:
            coverage_dir = Path(args.pop(0))
        elif arg == "--output" and args:
            output = Path(args.pop(0))
        else:
            print(f"check_policy: unknown argument '{arg}'", file=sys.stderr)
            return 2

    if not policy_path:
        # No policy: the pass had nothing to select, so there is nothing to
        # check.
        return 0

    policy_file = Path(policy_path)
    try:
        # "utf-8-sig" accepts the byte order mark that some Windows editors
        # write; the plugin accepts and skips it too.
        raw = json.loads(
            policy_file.read_text(encoding="utf-8-sig"), object_pairs_hook=no_duplicate_keys
        )
    except (OSError, ValueError) as error:
        print(f"check_policy: {policy_path}: {error}", file=sys.stderr)
        return 1
    if not isinstance(raw, dict):
        print(f"check_policy: {policy_path}: the policy file must contain a JSON object",
              file=sys.stderr)
        return 1
    try:
        check_policy_keys(raw)
        patterns = policy_patterns(raw)
        check_policy_values(raw)
    except ValueError as error:
        print(f"check_policy: {policy_path}: {error}", file=sys.stderr)
        return 1

    if coverage_dir is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE", "").strip()
        coverage_dir = Path(env) if env else default_coverage_dir(policy_path)

    digest = hashlib.sha256(policy_file.read_bytes()).hexdigest().upper()
    states, current = read_coverage(coverage_dir, digest)
    if not current:
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

    matched = {pattern for pattern, state in states.items() if state == "matched"}
    blocked = {
        pattern: state.split(":", 1)[1]
        for pattern, state in states.items()
        if state.startswith("blocked:")
    }
    failed = False

    # An entry that named a function that cannot be protected protects
    # nothing: the build has to fail, and it has to say why.
    blocked_only = [pattern for pattern in patterns if pattern not in matched and pattern in blocked]
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

    unmatched = [pattern for pattern in patterns if pattern not in matched and pattern not in blocked]
    if unmatched:
        noun = "entry" if len(unmatched) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(unmatched)} policy {noun} matched no function:",
            file=sys.stderr,
        )
        for pattern in unmatched:
            print(f"  {pattern}", file=sys.stderr)
        failed = True

    if failed:
        return 1

    prune_stale_coverage(coverage_dir, digest)
    print(f"check_policy: {len(patterns)} policy entries matched")
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(f"{len(patterns)} entries matched\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
