#!/usr/bin/env python3
"""Render the C++ SDK headers from the calibration JSON dumps.

Cross-platform (Linux and native Windows); the Meson builds invoke this
directly so no POSIX shell is required. Mirrors the historical gen-sdk.sh.

Usage: gen-sdk.py [--live] [DUMP_DIR] [OUT_DIR] [STAMP]

  DUMP_DIR   directory holding interfaces.json / schema.json / offsets.json
             (default: <repo>/calibration)
  OUT_DIR    where the headers are written (default: <repo>/payload/sdk)
  STAMP      optional file to touch on success (used by the Meson target)

Any subset of the dumps may be missing; missing inputs simply produce empty
sections. The dumper is built (release) on demand if no binary is present.

Where the dumper binary is depends on cargo: ``CARGO_TARGET_DIR`` moves the
whole target directory (which is how a worktree keeps cargo's output out of
itself), and the release binary is then at ``<CARGO_TARGET_DIR>/release/``.
Without it the binary is in the dumper crate's own target directory.
"""
import os
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path

#: The repository root (the script lives in ``scripts/``).
ROOT = Path(__file__).resolve().parent.parent


def dumper_exe_name() -> str:
    """The dumper binary's file name on this platform."""
    return "cs2-dumper.exe" if os.name == "nt" else "cs2-dumper"


def dumper_binary(root: Path, env: Mapping[str, str] | None = None) -> Path:
    """Where cargo put (or will put) the dumper binary.

    Cargo honours ``CARGO_TARGET_DIR``: when it is set the whole target
    directory moves there, and the release binary is at
    ``<CARGO_TARGET_DIR>/release/<exe>``.  When it is not set the binary is in
    the dumper crate's own target directory,
    ``external/dumper/target/release/<exe>``.

    The path is resolved in a function - not inline in ``main`` - so it can be
    unit tested without a cargo build.  ``env`` is the environment to read
    (the process environment by default); a relative ``CARGO_TARGET_DIR`` is
    relative to the working directory, which is cargo's own rule.
    """
    environment: Mapping[str, str] = os.environ if env is None else env
    target_dir = environment.get("CARGO_TARGET_DIR")
    if target_dir:
        return Path(target_dir) / "release" / dumper_exe_name()
    return root / "external" / "dumper" / "target" / "release" / dumper_exe_name()


def main(argv: list[str]) -> int:
    root = ROOT

    args = list(argv)
    live = False
    if args and args[0] == "--live":
        live = True
        args = args[1:]

    dump_dir = Path(args[0]) if len(args) > 0 else root / "calibration"
    out_dir = Path(args[1]) if len(args) > 1 else root / "payload" / "sdk"
    stamp = args[2] if len(args) > 2 and args[2] else None

    if live:
        # calibrate-live.py is cross-platform (the dumper has a native Windows
        # live-memory backend), so run it directly on Linux and Windows alike.
        subprocess.run(
            [sys.executable, str(root / "scripts" / "calibrate-live.py"), str(dump_dir)],
            check=False,
        )

    dumper_args = ["--gen", str(out_dir)]
    found = 0
    for name in ("interfaces", "schema", "offsets"):
        path = dump_dir / f"{name}.json"
        if path.is_file():
            dumper_args += [f"--{name}-json", str(path)]
            found += 1

    binary = dumper_binary(root)
    if not binary.is_file():
        print("gen-sdk: building cs2-dumper (release)", file=sys.stderr)
        subprocess.run(
            [
                "cargo",
                "build",
                "--release",
                "--manifest-path",
                str(root / "external" / "dumper" / "Cargo.toml"),
            ],
            check=True,
        )

    if found == 0:
        print(
            f"gen-sdk: no dumps in {dump_dir} - writing empty SDK headers",
            file=sys.stderr,
        )

    subprocess.run([str(binary), *dumper_args], check=True)

    if stamp:
        stamp_path = Path(stamp)
        stamp_path.parent.mkdir(parents=True, exist_ok=True)
        stamp_path.touch()

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
