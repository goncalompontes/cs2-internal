"""The trace probe's typed answer (``scripts/scenario/protocol.py``).

The probe asks the engine its own question — ``TraceLineIntervals``, the call the
real bullet makes — for a ray, and publishes **both** answers side by side plus the
difference. The wire types are the client's half of that contract; this test pins
them against the payload's own JSON keys — it reads the payload's C++ and checks
that every key the protocol declares is one the payload actually writes — so a
renamed field is a failure here rather than a surprise at runtime.

    python -m unittest discover -s scripts/scenario/tests
"""

from __future__ import annotations

import pathlib
import re
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
PAYLOAD_SERVICE = REPO_ROOT / "payload" / "control" / "src" / "service.cpp"
PAYLOAD_QUERY_HEADER = REPO_ROOT / "payload" / "control" / "include" / "cs2" / "control" / "occlusion_query.hpp"


def payload_service_source() -> str:
    """The payload's control service — the half that writes the answer."""
    assert PAYLOAD_SERVICE.is_file(), "the payload's service source is there"
    return PAYLOAD_SERVICE.read_text(encoding="utf-8")


def payload_trace_probe_keys() -> set[str]:
    """The JSON keys the payload's ``trace_probe`` command writes.

    The keys are the ones set inside ``do_trace_probe`` — the command's own
    publication, not the rest of the service.
    """
    source = payload_service_source()
    marker = "void Service::do_trace_probe"
    assert marker in source, "the payload has the trace_probe command"
    region = source.split(marker, 1)[1]
    return set(re.findall(r'\.set\("([^"]+)"', region))


def payload_verdict_names() -> set[str]:
    """The verdict names the payload's ``to_string`` can produce."""
    source = PAYLOAD_QUERY_HEADER.read_text(encoding="utf-8")
    marker = "auto to_string(TraceVerdict verdict)"
    assert marker in source, "the payload names the verdicts"
    region = source.split(marker, 1)[1]
    # The function's own body: the next closing brace at the start of a line.
    region = region.split("\n}\n", 1)[0]
    return set(re.findall(r'return "([a-z_]+)";', region))


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
        "engine_answer_usable": True,
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
            "overrun": False,
            "interval_count": 2,
            "intervals_overflow": False,
            "answer_missing": False,
            "surface_mapped": False,
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
            "overrun": False,
            "rays_ok": 1,
            "rays_faulted": 0,
            "rays_overrun": 0,
            "iface": "0x7ff600000000",
            "filter": "0x7ff600001000",
            "filter_mask": 0x1C300B,
            "filter_group": 3,
            "filter_flags": 15,
            "occluders": 812,
            "occluder_hits": 1,
            "occluder_cap": 256,
            "occluders_truncated": False,
            "world_cached": True,
            "refused": False,
            "refusal_reason": "",
            "target_pad": 8.0,
        },
        "ran": True,
        "timeout": False,
        "game_thread": "answered",
        "rays": [ray],
    }


class TestTraceProbeTypes(unittest.TestCase):
    def test_the_verdicts_are_the_five_the_payload_publishes(self) -> None:
        """The difference's one-line verdict has exactly the payload's values."""
        self.assertEqual(
            set(get_args(TraceVerdict)),
            {"agrees", "engine_only", "ours_only", "both", "unknown"},
        )

    def test_the_verdicts_are_the_payloads_own_names(self) -> None:
        """The client's verdict names are the payload's ``to_string`` names.

        A verdict the client does not know (or one the payload cannot produce) is a
        wire break — and it is silent, because both halves are typed.
        """
        self.assertEqual(set(get_args(TraceVerdict)), payload_verdict_names())

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
        source = payload_service_source()
        self.assertIn('command == "trace_probe"', source)
        self.assertIn("do_trace_probe(", source)

    def test_the_protocol_keys_are_the_payloads_own_json_keys(self) -> None:
        """Every key the protocol declares is a key the payload writes.

        This is the check the docstring promises: it reads the payload's C++ (the
        same pattern as the other protocol tests), so a **renamed** key in the
        payload is a failure here rather than a silent wire break. Both halves are
        typed, so nothing else would notice.
        """
        written = payload_trace_probe_keys()
        self.assertTrue(written, "the payload's command writes keys")
        for name, typed in (
            ("TraceProbeResult", TraceProbeResult),
            ("TraceRayDict", TraceRayDict),
            ("TraceRayEngineDict", protocol.TraceRayEngineDict),
            ("TraceDifferenceDict", TraceDifferenceDict),
            ("TraceIntervalDict", TraceIntervalDict),
            ("TraceOccluderDict", TraceOccluderDict),
            ("TraceSpanDict", TraceSpanDict),
            ("TraceProbeEngineDict", protocol.TraceProbeEngineDict),
        ):
            for key in get_type_hints(typed):
                with self.subTest(type=name, key=key):
                    self.assertIn(key, written, f"the payload writes {name}.{key}")

    def test_the_dropped_engine_answer_is_part_of_the_contract(self) -> None:
        """The answer says when the engine's answer could not be read.

        A probe whose engine call faulted (or whose container did not hold the
        buffer it handed the engine) must not report "agrees" — the difference is
        ``unknown`` and the answer says so.
        """
        answer = trace_probe_answer()
        ray = answer["rays"][0]
        self.assertIn("answer_missing", ray["engine"])
        self.assertIn("engine_answer_usable", ray["difference"])
        self.assertFalse(ray["engine"]["answer_missing"])
        self.assertTrue(ray["difference"]["engine_answer_usable"])

    def test_the_cold_cache_refusal_is_part_of_the_contract(self) -> None:
        """The probe refuses to answer while the world's cache is cold.

        A refusal is **not** a fault: the probe does not ask the engine
        anything when our own answer would be empty anyway, so the answer has to
        say *why* it is missing — otherwise the operator cannot tell "the probe
        refused" from "the engine call faulted".
        """
        answer = trace_probe_answer()
        engine = answer["engine"]
        self.assertIn("refused", engine)
        self.assertIn("refusal_reason", engine)
        self.assertFalse(engine["refused"])
        self.assertEqual(engine["refusal_reason"], "")
        # The payload names the cold cache as the reason, and publishes both
        # keys.
        header = PAYLOAD_QUERY_HEADER.read_text(encoding="utf-8")
        self.assertIn("world_cache_cold", header)
        source = payload_service_source()
        self.assertIn('"refused"', source)
        self.assertIn('"refusal_reason"', source)


if __name__ == "__main__":
    unittest.main()
