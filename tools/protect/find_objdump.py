#!/usr/bin/env python3
"""The llvm-objdump of the LLVM SDK the plugin was built against.

The payload check disassembles the image it verifies, and the disassembler
has to be the one of the SDK the plugin was built with: the pass reasons
about the IR that *that* LLVM turns into machine code, and another LLVM may
print the instructions differently enough for the check to miss a dispatch.

The path is printed on stdout, and nothing else, so that a caller can
capture it:

    python tools/protect/find_objdump.py

The rules, in order:

  1. ``--sdk DIR`` -- the SDK directory, whose ``bin`` holds the tool;
  2. ``$CS2_PROTECT_OBJDUMP`` -- the tool itself (an explicit override);
  3. ``$CS2_LLVM_SDK`` -- the SDK directory (the same variable
     ``scripts/build-protect-plugin.py`` uses);
  4. the extracted release tarball under ``F:\\llvm-sdk`` (the default of
     the plugin build script).

The exit code is 0 when the tool was found and 1 when it was not; the
reason is printed on stderr.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

#: The extracted LLVM release tarball (the default of the plugin build).
DEFAULT_SDK = Path(r"F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc")

#: The name of the tool, with and without the extension.
TOOL_NAMES = ("llvm-objdump.exe", "llvm-objdump")


def tool_in(sdk: Path) -> Path | None:
    """The objdump of an SDK directory, or None when it has none."""
    for name in TOOL_NAMES:
        candidate = sdk / "bin" / name
        if candidate.is_file():
            return candidate
    return None


def find_objdump(sdk: Path | None) -> Path | None:
    """The objdump to use, or None when there is none."""
    if sdk is not None:
        found = tool_in(sdk)
        if found is not None:
            return found
    override = os.environ.get("CS2_PROTECT_OBJDUMP", "").strip()
    if override:
        candidate = Path(override)
        if candidate.is_file():
            return candidate
    env_sdk = os.environ.get("CS2_LLVM_SDK", "").strip()
    if env_sdk:
        found = tool_in(Path(env_sdk))
        if found is not None:
            return found
    return tool_in(DEFAULT_SDK)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sdk", type=Path, default=None,
                        help="the LLVM SDK directory (default: $CS2_LLVM_SDK)")
    args = parser.parse_args(argv)

    found = find_objdump(args.sdk)
    if found is None:
        print(
            "find_objdump: the llvm-objdump of the LLVM SDK was not found.\n"
            "              set $CS2_LLVM_SDK to the extracted SDK, or "
            "$CS2_PROTECT_OBJDUMP\n"
            "              to the tool itself (the protected build needs it: it "
            "checks the image it\n"
            "              produces).",
            file=sys.stderr,
        )
        return 1
    print(found.resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
