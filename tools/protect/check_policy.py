#!/usr/bin/env python3
"""Check that every `protect` entry of the policy matched a function.

An LLVM pass sees one translation unit at a time, so it cannot know whether
a policy entry matched *somewhere* in the build. The plugin therefore writes
one coverage file per module; this script reads them after the build and
fails it when an entry matched nothing. A typo must not silently protect
nothing.

Usage: check_policy.py [--policy FILE] [--coverage DIR] [--output FILE]

  --policy    the policy file (default: $CS2_PROTECT_CONFIG)
  --coverage  the coverage directory (default: $CS2_PROTECT_COVERAGE, else
              <temp>/cs2-protect-coverage/<sha256 of the policy path>)
  --output    a stamp file to write when the check passes

The coverage directory default is the same one the plugin computes, so the
two agree without the build having to pass anything around.

Exit code 0 when every entry matched (or when there is no policy at all),
1 when an entry matched nothing or the policy cannot be read.
"""
import hashlib
import json
import os
import sys
from pathlib import Path


def policy_patterns(policy: dict) -> list[str]:
    """The glob of every `protect` entry of the policy."""
    patterns: list[str] = []
    for entry in policy.get("protect", []):
        if isinstance(entry, str):
            patterns.append(entry)
        elif isinstance(entry, dict) and isinstance(entry.get("symbol"), str):
            patterns.append(entry["symbol"])
        else:
            raise ValueError(f"a 'protect' entry must be a string or an object: {entry!r}")
    return patterns


def default_coverage_dir(policy_path: str) -> Path:
    """The directory the plugin writes the coverage to."""
    digest = hashlib.sha256(policy_path.encode()).hexdigest().upper()[:16]
    root = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    return Path(root) / "cs2-protect-coverage" / digest


def read_coverage(coverage_dir: Path, digest: str) -> tuple[set[str], bool]:
    """The patterns that matched, over every coverage file of this policy.

    The second value says whether any coverage file belongs to this policy at
    all: when none does, the coverage is stale (the policy changed after the
    last compile) and the check cannot say anything.
    """
    matched: set[str] = set()
    current = False
    if not coverage_dir.is_dir():
        return matched, current
    for path in sorted(coverage_dir.glob("*.cov")):
        entries: dict[str, str] = {}
        policy_digest = ""
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            key, _, value = line.partition(" ")
            if key == "policy-sha256":
                policy_digest = value.strip().upper()
            elif key == "entry":
                state, _, pattern = value.partition(" ")
                entries[pattern.strip()] = state.strip()
        if policy_digest != digest:
            # The coverage of another policy; it says nothing about this one.
            continue
        current = True
        for pattern, state in entries.items():
            if state == "matched":
                matched.add(pattern)
    return matched, current


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
        policy = json.loads(policy_file.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"check_policy: cannot read the policy '{policy_path}': {error}", file=sys.stderr)
        return 1
    try:
        patterns = policy_patterns(policy)
    except ValueError as error:
        print(f"check_policy: {policy_path}: {error}", file=sys.stderr)
        return 1

    if coverage_dir is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE")
        coverage_dir = Path(env) if env else default_coverage_dir(policy_path)

    digest = hashlib.sha256(policy_file.read_bytes()).hexdigest().upper()
    matched, current = read_coverage(coverage_dir, digest)
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
    unmatched = [pattern for pattern in patterns if pattern not in matched]
    if unmatched:
        noun = "entry" if len(unmatched) == 1 else "entries"
        print(
            f"check_policy: {policy_path}: {len(unmatched)} policy {noun} matched no function:",
            file=sys.stderr,
        )
        for pattern in unmatched:
            print(f"  {pattern}", file=sys.stderr)
        return 1

    print(f"check_policy: {len(patterns)} policy entries matched")
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(f"{len(patterns)} entries matched\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
