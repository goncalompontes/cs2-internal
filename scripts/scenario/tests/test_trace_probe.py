"""The trace probe's typed answer (``scripts/scenario/protocol.py``).

The probe asks the engine its own question — ``TraceLineIntervals``, the call the
real bullet makes — for a ray, and publishes **both** answers side by side plus the
difference. The wire types are the client's half of that contract; this test pins
them against the payload's own JSON keys, so a renamed field is a failure here
rather than a surprise at runtime.

    python -m unittest discover -s scripts/scenario/tests
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from typing import Any, get_args, get_type_hints

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import protocol  # noqa: E402
from scenario.protocol import (  # noqa: E402
    TraceDifferenceDict,
    TraceIntervalDict,
    TraceOccluderDict,
    TraceProbeResult,
    TraceRayDict,
    TraceSolidKind,
    TraceSpanDict,
    TraceVerdict,
)

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]


def trace_probe_answer() -> TraceProbeResult:
    """A ``trace_probe`` answer as the payload writes it.

    The literal is typed as the wire type, so a renamed or retyped field is a
    **static** failure here (``scripts/typecheck.py`` runs mypy over this
    package) and not only a runtime one.
    """
    interval: TraceIntervalDict = {
        "fraction": 0.25,
        "distance": 750.0,
        "enter_exit": 1,
        "surface": "unmapped",
        "raw": "00" * 56,
    }
    occluder: TraceOccluderDict = {
        "index": 12,
        "kind": "hull",
        "cluster": -1,
        "entry": 748.0,
        "exit": 800.0,
    }
    span: TraceSpanDict = {
        "entry": 750.0,
        "exit": 800.0,
        "enter_record": 0,
        "exit_record": 1,
    }
    difference: TraceDifferenceDict = {
        "verdict": "engine_only",
        "engine_only": [span],
        "ours_only": [],
    }
    ray: TraceRayDict = {
        "start": {"x": 1.0, "y": 2.0, "z": 3.0},
        "direction": {"x": 1.0, "y": 0.0, "z": 0.0},
        "end": {"x": 3001.0, "y": 2.0, "z": 3.0},
        "depth": 3000.0,
        "engine": {
            "ok": True,
            "faulted": False,
            "interval_count": 2,
            "intervals_overflow": False,
            "intervals": [interval],
        },
        "ours": [occluder],
        "difference": difference,
    }
    return {
        "eye": {"x": 1.0, "y": 2.0, "z": 3.0},
        "yaw": 90.0,
        "pitch": -2.0,
        "spread": 0.0,
        "count": 1,
        "depth": 3000.0,
        "max_rays": 8,
        "max_intervals": 64,
        "engine": {
            "ok": True,
            "faulted": False,
            "iface": "0x7ff600000000",
            "filter": "0x7ff600001000",
            "filter_mask": 0x1C300B,
            "filter_group": 3,
            "filter_flags": 15,
            "occluders": 812,
            "occluder_hits": 1,
        },
        "ran": True,
        "timeout": False,
        "rays": [ray],
    }


class TestTraceProbeTypes(unittest.TestCase):
    def test_the_verdicts_are_the_four_the_payload_publishes(self) -> None:
        """The difference's one-line verdict has exactly four values."""
        self.assertEqual(
            set(get_args(TraceVerdict)),
            {"agrees", "engine_only", "ours_only", "both"},
        )

    def test_the_solid_kinds_are_the_solver_ones(self) -> None:
        """The probe reports the kinds `cs2::aim::SolidKind` carries."""
        self.assertEqual(
            set(get_args(TraceSolidKind)),
            {"hull", "sphere", "capsule", "mesh"},
        )

    def test_the_answer_carries_both_answers_and_the_difference(self) -> None:
        """The answer is the engine's, ours, and the difference between them."""
        answer = trace_probe_answer()
        self.assertTrue(answer["engine"]["ok"])
        self.assertFalse(answer["engine"]["faulted"])
        self.assertEqual(answer["rays"][0]["difference"]["verdict"], "engine_only")
        self.assertEqual(len(answer["rays"][0]["engine"]["intervals"]), 1)
        self.assertEqual(len(answer["rays"][0]["ours"]), 1)

    def test_the_types_are_exported(self) -> None:
        """Every new type is in `__all__` — the package's public surface."""
        for name in (
            "TraceDifferenceDict",
            "TraceIntervalDict",
            "TraceOccluderDict",
            "TraceProbeEngineDict",
            "TraceProbeResult",
            "TraceRayDict",
            "TraceRayEngineDict",
            "TraceSolidKind",
            "TraceSpanDict",
            "TraceVerdict",
        ):
            with self.subTest(name=name):
                self.assertIn(name, protocol.__all__)
                self.assertTrue(hasattr(protocol, name))

    def test_the_interval_record_publishes_its_raw_bytes(self) -> None:
        """The record's unmapped fields are published as raw hex.

        The probe's whole point is the evidence: a later RE pass maps the rest of
        the 0x38-byte record from the probe's own output.
        """
        hints = get_type_hints(TraceIntervalDict)
        self.assertIn("raw", hints)
        self.assertEqual(hints["raw"], str)
        answer = trace_probe_answer()
        raw = answer["rays"][0]["engine"]["intervals"][0]["raw"]
        self.assertEqual(len(raw), 56 * 2)

    def test_the_payload_dispatches_the_command(self) -> None:
        """The payload's own dispatch knows the command the client sends.

        The command name is the contract: the client asks for ``trace_probe``
        and the payload's command switch must answer it.
        """
        service = REPO_ROOT / "payload" / "control" / "src" / "service.cpp"
        self.assertTrue(service.is_file(), "the payload's service source is there")
        source = service.read_text(encoding="utf-8")
        self.assertIn('command == "trace_probe"', source)
        self.assertIn("do_trace_probe(", source)


if __name__ == "__main__":
    unittest.main()
