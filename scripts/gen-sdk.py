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

Where the dumper binary is follows cargo: ``CARGO_TARGET_DIR`` moves the
whole target directory (which is how a worktree keeps cargo's output out of
itself), and the release binary is then in ``<CARGO_TARGET_DIR>/release/``.
A build for a **target triple** puts it in
``<CARGO_TARGET_DIR>/<triple>/release/`` instead.  Without
``CARGO_TARGET_DIR`` the binary is in the dumper crate's own target
directory.
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


def dumper_target_dir(root: Path, env: Mapping[str, str] | None = None) -> Path:
    """The cargo target directory that holds the dumper's artifacts.

    ``CARGO_TARGET_DIR`` moves it; without it the dumper crate's own
    ``target/`` directory is the one cargo uses.  A relative
    ``CARGO_TARGET_DIR`` stays relative: cargo resolves it against the
    working directory, and so does everything here.
    """
    environment: Mapping[str, str] = os.environ if env is None else env
    target_dir = environment.get("CARGO_TARGET_DIR")
    if target_dir:
        return Path(target_dir)
    return root / "external" / "dumper" / "target"


def dumper_candidates(root: Path, env: Mapping[str, str] | None = None) -> list[Path]:
    """The paths cargo puts the release dumper at, in the order to try them.

    Cargo's layout: the release binary of a crate is in
    ``<target-dir>/release/``.  A build **for a target triple** (``--target``,
    ``CARGO_BUILD_TARGET``) puts it in ``<target-dir>/<triple>/release/``
    instead, and that is the only place cargo will put it in that build -
    so with a triple in the environment it is the only candidate.
    """
    environment: Mapping[str, str] = os.environ if env is None else env
    base = dumper_target_dir(root, environment)
    exe = dumper_exe_name()
    triple = environment.get("CARGO_BUILD_TARGET")
    if triple:
        return [base / triple / "release" / exe]
    return [base / "release" / exe]


def find_dumper(root: Path, env: Mapping[str, str] | None = None) -> Path | None:
    """The dumper binary, if cargo has put one where it belongs.

    The candidates of :func:`dumper_candidates` are tried first.  A triple
    cargo took from its own configuration file (``build.target``) is not in
    the environment, so the target directory is searched for one as well:
    the binary is in ``<target-dir>/<triple>/release/``.
    """
    for candidate in dumper_candidates(root, env):
        if candidate.is_file():
            return candidate
    base = dumper_target_dir(root, env)
    for candidate in sorted(base.glob(f"*/release/{dumper_exe_name()}")):
        if candidate.is_file():
            return candidate
    return None


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

    # The dumper is resolved **before** the live branch: the live calibration
    # runs the same binary, so it has to be the one cargo built - the live
    # script resolves it on its own otherwise, and looks in the wrong place.
    binary = find_dumper(root)
    if binary is None:
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
        binary = find_dumper(root)
        if binary is None:
            # A sentence, not a stack trace: the binary cargo was asked to
            # build is not where it was expected.
            searched = ", ".join(str(path) for path in dumper_candidates(root))
            print(
                f"gen-sdk: the dumper is not where cargo puts it ({searched})",
                file=sys.stderr,
            )
            return 1

    if live:
        # calibrate-live.py is cross-platform (the dumper has a native Windows
        # live-memory backend), so run it directly on Linux and Windows alike.
        # The binary is passed explicitly, and the child's exit code is
        # checked: a failed calibration must fail the build.  Its dumps are
        # missing, so the SDK would be empty - a green build against an empty
        # SDK is worse than a red one.
        completed = subprocess.run(
            [
                sys.executable,
                str(root / "scripts" / "calibrate-live.py"),
                str(dump_dir),
                "--bin",
                str(binary),
            ],
            check=False,
        )
        if completed.returncode != 0:
            print(
                f"gen-sdk: the live calibration failed "
                f"(exit {completed.returncode})",
                file=sys.stderr,
            )
            return 1

    dumper_args = ["--gen", str(out_dir)]
    found = 0
    for name in ("interfaces", "schema", "offsets"):
        path = dump_dir / f"{name}.json"
        if path.is_file():
            dumper_args += [f"--{name}-json", str(path)]
            found += 1

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
