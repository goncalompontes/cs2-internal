#!/usr/bin/env python3
"""Run one scenario test suite — the entry point ``meson test`` uses.

The scenario library has its own suite (``scripts/scenario/tests``), and it is
pure logic: no game, no payload, no network. It belongs to the same
``meson test`` run as the C++ host tests, one test per suite — the same shape
the C++ side uses.

    python scripts/scenario/tests/run_suite.py test_control

The exit code is the verdict:

* ``0``  — the suite passed.
* ``1``  — the suite failed (or the suite does not exist — that is a wiring
  error, not an environment problem).
* ``77`` — the suite **could not run** here (a missing optional dependency).
  77 is the automake "skip" code, so Meson reports the test as SKIP rather
  than FAIL: a machine without the optional packages must not break the
  build, and a machine *with* them must not silently skip the suite.
"""

from __future__ import annotations

import importlib
import sys
import unittest
from pathlib import Path

#: The package root (``scripts/``), so ``import scenario`` works from any
#: working directory — Meson runs the tests from the build directory.
ROOT = Path(__file__).resolve().parents[2]

#: The automake "skip" code: the suite could not run, it did not fail.
SKIP = 77


def main(argv: list[str]) -> int:
    """Run the named suite; return the exit code."""
    if len(argv) != 1:
        print("usage: run_suite.py <suite>", file=sys.stderr)
        return 2
    name = argv[0]
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))

    path = ROOT / "scenario" / "tests" / f"{name}.py"
    if not path.is_file():
        # A name that does not exist is a wiring error: it must be loud.
        print(f"scenario tests: no such suite: {name}", file=sys.stderr)
        return 1

    try:
        module = importlib.import_module(f"scenario.tests.{name}")
    except ModuleNotFoundError as error:
        # The suite is there, but a package it needs is not installed. That
        # is an environment, not a defect: skip it and say why.
        print(f"scenario tests: {name} cannot run here ({error})", file=sys.stderr)
        return SKIP
    except ImportError as error:
        # The import itself is broken (not a missing package): that is a
        # defect in the suite, and it must fail.
        print(f"scenario tests: {name} does not import ({error})", file=sys.stderr)
        return 1

    suite = unittest.defaultTestLoader.loadTestsFromModule(module)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
