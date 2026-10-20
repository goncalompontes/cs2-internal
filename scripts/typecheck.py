#!/usr/bin/env python3
"""The type gate: run mypy over the scenario package and fail on an error.

"Fully type safe" is only worth something if it is **enforceable**, and a
checker nobody runs is a comment. This script is the gate:

    python scripts/typecheck.py            # the whole package
    python scripts/typecheck.py scripts/scenario/control.py

It runs ``mypy --config-file pyproject.toml`` over the package and exits with
the checker's own status, so a type error fails the build instead of being
noticed later. The configuration lives in ``pyproject.toml`` (``[tool.mypy]``)
— this script only decides *what* to check and *how to fail*.

Exit codes:

* ``0``   — clean: the package type-checks.
* ``1``   — **the gate failed**: mypy reported errors (its own diagnostics are
  printed verbatim).
* ``77``  — **skipped**: mypy is not installed. 77 is the automake "skip"
  code, so ``meson test`` reports the suite as skipped rather than failed —
  a machine without the dev dependencies must not break the build, and a
  machine *with* them must not skip the gate. Install the checker with
  ``pip install -r requirements-dev.txt``.

The package stays standard library at runtime: mypy is a *dev* dependency.
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path

#: The repository root (``scripts/typecheck.py`` -> ``scripts`` -> the root).
REPO_ROOT = Path(__file__).resolve().parents[1]

#: The configuration file (the mypy section lives in it).
CONFIG = REPO_ROOT / "pyproject.toml"

#: What is checked by default: the whole scenario package.
PACKAGE = "scripts/scenario"

#: The automake "skip" code — the gate could not run, it did not fail.
SKIP = 77


def mypy_command(targets: list[str]) -> list[str]:
    """The command line the gate runs."""
    return [sys.executable, "-m", "mypy", "--config-file", str(CONFIG), *targets]


def checker_available() -> bool:
    """Whether mypy is importable in this interpreter."""
    return importlib.util.find_spec("mypy") is not None


def main(argv: list[str] | None = None) -> int:
    """Run the gate; return the exit code."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("targets", nargs="*",
                        help=f"what to check (default: {PACKAGE})")
    arguments = parser.parse_args(argv)

    if not CONFIG.is_file():
        print(f"typecheck: the configuration is missing: {CONFIG}", file=sys.stderr)
        return 1

    if not checker_available():
        print("typecheck: mypy is not installed — the gate is skipped "
              "(pip install -r requirements-dev.txt)", file=sys.stderr)
        return SKIP

    targets = arguments.targets or [PACKAGE]
    command = mypy_command(targets)
    print("typecheck: " + " ".join(command), file=sys.stderr)
    # The checker's own diagnostics are the report; its exit code is the
    # verdict. `cwd` is the root, so the configuration's relative paths
    # (the package it checks) resolve the way they read.
    completed = subprocess.run(command, cwd=REPO_ROOT, check=False)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
