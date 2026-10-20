#!/usr/bin/env python3
"""Check that the LLVM pass plugin did not change under the build.

Ninja cannot attach an arbitrary file to a compile edge, so a plugin that
changed after the build was configured would silently leave stale objects
behind: the compile command still names the same DLL, but the DLL now
behaves differently. This script turns that into a loud failure -- it
compares the plugin with the digest that Meson recorded at configure time
and refuses the build when they differ.

Usage: verify_plugin.py --plugin FILE --expected-sha256 HEX --output FILE

  --plugin           the plugin DLL the payload build loads
  --expected-sha256  the digest Meson computed at configure time
  --output           the stamp file to write on success

Exit code 0 when the plugin is unchanged (the digest is then written to
--output), 1 when it changed, 2 on a usage error.
"""
import argparse
import hashlib
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)

    if not args.plugin.is_file():
        print(f"verify_plugin: the plugin '{args.plugin}' does not exist", file=sys.stderr)
        return 2

    actual = hashlib.sha256(args.plugin.read_bytes()).hexdigest()
    expected = args.expected_sha256.strip().lower()
    if actual != expected:
        print(
            "verify_plugin: the plugin changed after the build was configured "
            f"({args.plugin}: sha256 {actual}, expected {expected}).\n"
            "              Re-run the Meson configure step so that the build picks "
            "the new plugin up:\n"
            "                meson setup --reconfigure <build dir> payload "
            "-Dprotect=true -Dprotect_plugin=<path>",
            file=sys.stderr,
        )
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(f"{actual}\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
