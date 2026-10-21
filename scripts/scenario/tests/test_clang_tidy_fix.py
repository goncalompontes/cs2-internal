"""Unit tests for the clang-tidy sweep's **check mode**.

``scripts/clang-tidy-fix.py`` applies clang-tidy's fixes one group at a time.
The sweep itself needs clang-tidy and a build tree, but the decision the
check mode turns into an exit code -- "does a selected group report a
finding?" -- is pure, and it is what makes the sweep's result *checkable*: a
change that introduces a finding under a gated group fails the gate.

These tests are **independent of the build**: they drive the sweep's own
functions and its command line, so they fail even on a machine without
clang-tidy.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import importlib.util
import pathlib
import unittest
from collections import Counter
from typing import Any

#: The repository root (``scripts/scenario/tests`` -> ``scripts`` -> root).
ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The sweep (its name is not a module name, so it is loaded by path).
SWEEP = ROOT / "scripts" / "clang-tidy-fix.py"


def _load_sweep() -> Any:
    """The sweep module, loaded from its path.

    The module is loaded dynamically (its file name is not importable), so
    its functions are reached through ``Any``: the checker cannot see them.
    """
    spec = importlib.util.spec_from_file_location("clang_tidy_fix", SWEEP)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SWEEP_MODULE = _load_sweep()


class TestGatedGroups(unittest.TestCase):
    """The gate itself: which groups the tree must be clean under."""

    def test_every_gated_group_exists(self) -> None:
        groups = SWEEP_MODULE.GROUPS
        for name in SWEEP_MODULE.GATED_GROUPS:
            self.assertIn(name, groups, f"gated group '{name}' is not a known group")

    def test_the_gate_is_not_empty(self) -> None:
        self.assertTrue(
            SWEEP_MODULE.GATED_GROUPS,
            "an empty gate would let everything through",
        )

    def test_the_gate_has_no_duplicates(self) -> None:
        gated = list(SWEEP_MODULE.GATED_GROUPS)
        self.assertEqual(len(gated), len(set(gated)))


class TestGatedFindings(unittest.TestCase):
    """The decision the check mode turns into an exit code."""

    def test_a_clean_group_passes(self) -> None:
        findings: dict[str, Counter[str]] = {"performance": Counter()}
        self.assertEqual(SWEEP_MODULE.gated_findings(["performance"], findings), [])

    def test_a_group_with_a_finding_fails(self) -> None:
        findings = {"performance": Counter({"performance-for-range-copy": 2})}
        self.assertEqual(
            SWEEP_MODULE.gated_findings(["performance"], findings), ["performance"]
        )

    def test_only_the_selected_groups_are_considered(self) -> None:
        findings = {
            "performance": Counter(),
            "nodiscard": Counter({"modernize-use-nodiscard": 1}),
        }
        self.assertEqual(SWEEP_MODULE.gated_findings(["performance"], findings), [])
        self.assertEqual(
            SWEEP_MODULE.gated_findings(["performance", "nodiscard"], findings), ["nodiscard"]
        )

    def test_an_unselected_group_is_ignored(self) -> None:
        findings = {"const": Counter({"misc-const-correctness": 3})}
        self.assertEqual(SWEEP_MODULE.gated_findings(["performance"], findings), [])

    def test_a_missing_group_is_clean(self) -> None:
        self.assertEqual(SWEEP_MODULE.gated_findings(["performance"], {}), [])


class TestCommandLine(unittest.TestCase):
    """The command line: the gate can be inspected and replaced."""

    def test_list_gate_prints_the_gated_groups(self) -> None:
        import contextlib
        import io

        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = SWEEP_MODULE.main(["--list-gate"])
        self.assertEqual(code, 0)
        printed = buffer.getvalue()
        for name in SWEEP_MODULE.GATED_GROUPS:
            self.assertIn(name, printed)

    def test_an_unknown_gated_group_is_rejected(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            SWEEP_MODULE.main(["--gate", "no-such-group", "--list-gate"])
        self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
