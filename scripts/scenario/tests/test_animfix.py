"""Unit tests for the **local animfix**'s control surface and its scenario.

Two things are pinned here:

* the **wire shape** of the ``animfix`` command — ``{"cmd": "animfix",
  "args": {"on": true}}`` — and the fact that it answers with the input-status
  block, so the operator can read the animfix's own counters and the
  server-vs-predicted aim yaw out of the same block every other command uses;
* the **scenario's** promise: it is observation-only apart from that one
  command. A scenario that wrote anything else (``exec``, a settings change,
  a movement command) would change the game it is supposed to measure, so the
  writes are recorded and asserted.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import json
import pathlib
import sys
import unittest
from typing import Any, cast

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario.control import Control  # noqa: E402
from scenario.protocol import AnimfixResult, InputStatus  # noqa: E402
from scenario.scenarios import animfix as animfix_scenario  # noqa: E402
from scenario.session import Game  # noqa: E402
from scenario.tests.test_control import FakePipe  # noqa: E402
from scenario.tests.test_screens import snapshot  # noqa: E402


def connected(chunks: list[bytes]) -> tuple[Control, FakePipe]:
    control = Control("fake-pipe")
    pipe = FakePipe(chunks)
    control._pipe = pipe
    return control, pipe


def answer(result: dict[str, Any], request_id: int = 1) -> bytes:
    return (json.dumps({"id": request_id, "ok": True, "result": result}) + "\n").encode("utf-8")


def sent(pipe: FakePipe) -> list[dict[str, Any]]:
    text = bytes(pipe.written).decode("utf-8")
    return [json.loads(line) for line in text.splitlines() if line]


class TestAnimfixCommand(unittest.TestCase):
    """The ``animfix`` command's wire shape."""

    def test_the_command_is_a_boolean_toggle(self) -> None:
        control, pipe = connected([answer({"animfix": True})])
        result = control.animfix(True)
        self.assertTrue(result.get("animfix"))
        request = sent(pipe)[0]
        self.assertEqual(request["cmd"], "animfix")
        self.assertEqual(request["args"], {"on": True})

    def test_the_command_can_be_switched_off(self) -> None:
        control, pipe = connected([answer({"animfix": False})])
        control.animfix(False)
        self.assertEqual(sent(pipe)[0]["args"], {"on": False})

    def test_the_answer_carries_the_animfix_counters(self) -> None:
        """The status block is where the operator reads the evidence."""
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_acted": True,
            "animfix_captures": 12,
            "animfix_applies": 48,
            "animfix_skips": 1,
            "animfix_server_aim_yaw": 91.5,
            "animfix_predicted_aim_yaw": 88.0,
            "animfix_aim_yaw_delta": 3.5,
        }
        control, _ = connected([answer(block)])
        result: AnimfixResult = control.animfix(True)
        self.assertEqual(result["animfix_captures"], 12)
        self.assertEqual(result["animfix_applies"], 48)
        self.assertEqual(result["animfix_skips"], 1)
        self.assertAlmostEqual(cast(float, result["animfix_aim_yaw_delta"]), 3.5)
        self.assertAlmostEqual(cast(float, result["animfix_server_aim_yaw"]), 91.5)
        self.assertAlmostEqual(cast(float, result["animfix_predicted_aim_yaw"]), 88.0)


class FakeGame:
    """A ``Game`` that records what the scenario asked for.

    Only the surface the animfix scenario uses is here: ``input()``,
    ``animfix()``, ``state()`` / ``events()`` (the screen readout) and the
    ``awaiting`` helpers the scenario calls. The counters climb on every read,
    the way they do in a live round.
    """

    def __init__(self, block: dict[str, Any], *, climbing: bool = True) -> None:
        self.block = block
        self.climbing = climbing
        self.writes: list[tuple[str, Any]] = []
        self.requests: list[str] = []
        self.reads = 0
        self.captures = int(block.get("animfix_captures") or 0)
        self.applies = int(block.get("animfix_applies") or 0)

    def _status(self) -> InputStatus:
        self.reads += 1
        if self.climbing:
            self.captures += 5
            self.applies += 20
        status = dict(self.block)
        status["animfix_captures"] = self.captures
        status["animfix_applies"] = self.applies
        return status  # type: ignore[return-value]

    def input(self, via: Any = None) -> InputStatus:
        self.requests.append("input")
        return self._status()

    def animfix(self, on: bool = True) -> InputStatus:
        self.writes.append(("animfix", on))
        return self._status()

    def state(self) -> Any:
        return snapshot()

    def events(self, limit: int = 6) -> Any:
        return {"count": 0, "events": []}

    def exec(self, line: str) -> Any:
        self.writes.append(("exec", line))
        raise AssertionError("the animfix scenario must not drive the game")

    def request(self, cmd: str, args: Any = None) -> Any:
        self.writes.append((cmd, args))
        raise AssertionError("the animfix scenario must not use the raw escape hatch")


class TestAnimfixScenario(unittest.TestCase):
    """The scenario's contract: observation-only, and it reports the evidence."""

    def test_the_scenario_switches_the_animfix_on_and_off(self) -> None:
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_status": "ok",
            "animfix_acted": True,
            "animfix_captures": 5,
            "animfix_applies": 20,
            "animfix_skips": 0,
            "animfix_data_valid": True,
            "animfix_server_aim_yaw": 10.0,
            "animfix_predicted_aim_yaw": 7.0,
            "animfix_aim_yaw_delta": 3.0,
        }
        game = FakeGame(block)
        result = animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        # The feature is off by default: the scenario switches it on to
        # observe it, and off again so the round is left as it was found.
        self.assertEqual(game.writes, [("animfix", True), ("animfix", False)])
        self.assertIn("verdict", result)
        self.assertNotIn("skipped", result)

    def test_the_scenario_reports_the_divergence(self) -> None:
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_status": "ok",
            "animfix_acted": True,
            "animfix_captures": 5,
            "animfix_applies": 20,
            "animfix_skips": 0,
            "animfix_data_valid": True,
            "animfix_server_aim_yaw": 10.0,
            "animfix_predicted_aim_yaw": 7.0,
            "animfix_aim_yaw_delta": 3.0,
        }
        game = FakeGame(block)
        result = animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertIsNotNone(result["server_aim_yaw"])
        self.assertIsNotNone(result["predicted_aim_yaw"])
        self.assertIsNotNone(result["aim_yaw_delta"])
        self.assertAlmostEqual(float(result["server_aim_yaw"]), 10.0)
        self.assertAlmostEqual(float(result["predicted_aim_yaw"]), 7.0)
        self.assertAlmostEqual(float(result["aim_yaw_delta"]), 3.0)
        self.assertTrue(result["data_valid"])

    def test_a_payload_that_never_acts_is_a_failure(self) -> None:
        """The animfix on but doing nothing is a failure, not a pass."""
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_status": "ok",
            "animfix_acted": False,
            "animfix_captures": 0,
            "animfix_applies": 0,
            "animfix_skips": 0,
            "animfix_data_valid": False,
            "animfix_server_aim_yaw": None,
            "animfix_predicted_aim_yaw": None,
            "animfix_aim_yaw_delta": None,
        }
        game = FakeGame(block, climbing=False)
        with self.assertRaises(Exception):
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        # The feature is switched off again even when the run fails.
        self.assertEqual(game.writes[-1], ("animfix", False))

    def test_a_failure_inside_the_watch_loop_still_switches_the_feature_off(self) -> None:
        """A pipe hiccup must not leave the animfix on for the rest of the round."""
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_status": "ok",
            "animfix_acted": True,
            "animfix_captures": 5,
            "animfix_applies": 20,
            "animfix_skips": 0,
            "animfix_data_valid": True,
            "animfix_server_aim_yaw": 10.0,
            "animfix_predicted_aim_yaw": 7.0,
            "animfix_aim_yaw_delta": 3.0,
        }
        game = FakeGame(block)
        calls = {"n": 0}

        def flaky(via: Any = None) -> InputStatus:
            calls["n"] += 1
            if calls["n"] > 1:
                raise RuntimeError("the pipe hiccuped")
            return game._status()

        game.input = flaky  # type: ignore[method-assign]
        with self.assertRaises(RuntimeError):
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertEqual(game.writes[-1], ("animfix", False))

    def test_a_failure_on_the_first_read_still_switches_the_feature_off(self) -> None:
        """The switch-on is inside the `try`: the first read may fail too."""
        block = {
            "animfix": True,
            "animfix_ready": True,
            "animfix_status": "ok",
            "animfix_acted": True,
            "animfix_captures": 5,
            "animfix_applies": 20,
            "animfix_skips": 0,
            "animfix_data_valid": True,
            "animfix_server_aim_yaw": 10.0,
            "animfix_predicted_aim_yaw": 7.0,
            "animfix_aim_yaw_delta": 3.0,
        }
        game = FakeGame(block)

        def explode(via: Any = None) -> InputStatus:
            raise RuntimeError("the pipe hiccuped on the first read")

        game.input = explode  # type: ignore[method-assign]
        with self.assertRaises(RuntimeError):
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertEqual(game.writes, [("animfix", True), ("animfix", False)])

    def test_hooks_that_did_not_resolve_are_reported_not_failed(self) -> None:
        """No signatures in this build: the scenario says so instead of failing."""
        block = {
            "animfix": True,
            "animfix_ready": False,
            "animfix_status": "the calibration dump has no 'animfix_anim_tick' signature",
            "animfix_acted": False,
            "animfix_captures": 0,
            "animfix_applies": 0,
            "animfix_skips": 0,
            "animfix_data_valid": False,
            "animfix_server_aim_yaw": None,
            "animfix_predicted_aim_yaw": None,
            "animfix_aim_yaw_delta": None,
        }
        game = FakeGame(block)
        result = animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertTrue(result.get("skipped"))
        self.assertIn("not installed", result["verdict"])
        # The payload's own explanation is carried through, not swallowed.
        self.assertIn("calibration dump", result["animfix_status"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
