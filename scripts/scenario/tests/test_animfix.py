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
from scenario.tests.test_control import FakePipe, answer, sent  # noqa: E402
from scenario.tests.test_screens import snapshot  # noqa: E402


def connected(chunks: list[bytes]) -> tuple[Control, FakePipe]:
    control = Control("fake-pipe")
    pipe = FakePipe(chunks)
    control._pipe = pipe
    return control, pipe


class TestAnimfixCommand(unittest.TestCase):
    """The ``animfix`` command's wire shape."""

    def test_the_command_is_a_boolean_toggle(self) -> None:
        control, pipe = connected([answer(1, {"animfix": True})])
        result = control.animfix(True)
        self.assertTrue(result.get("animfix"))
        request = sent(pipe)[0]
        self.assertEqual(request["cmd"], "animfix")
        self.assertEqual(request["args"], {"on": True})

    def test_the_command_can_be_switched_off(self) -> None:
        control, pipe = connected([answer(1, {"animfix": False})])
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
        control, _ = connected([answer(1, block)])
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


class UptimeGame(FakeGame):
    """A payload that has been up for a while, with the animfix **on**.

    The counters behave the way the real ones do:

    * ``animfix_client_ticks`` climbs on every client tick, whether or not
      the animfix is on — it is the driver's own counter, and the animfix is
      off by default, so it carries the payload's whole uptime;
    * ``animfix_enabled_client_ticks`` climbs only while the animfix is on;
    * ``animfix_corrected_ticks`` climbs only while the animfix is on, and
      only on the ticks on which the correction won.
    """

    def __init__(self, *, uptime_ticks: int = 3608, ticks_per_read: int = 8,
                 corrected_per_read: int = 8) -> None:
        super().__init__({
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
        }, climbing=True)
        self.client_ticks = uptime_ticks
        self.enabled_client_ticks = 0
        self.corrected_ticks = 0
        self.ticks_per_read = ticks_per_read
        self.corrected_per_read = corrected_per_read

    def _status(self) -> InputStatus:
        status = super()._status()
        self.client_ticks += self.ticks_per_read
        self.enabled_client_ticks += self.ticks_per_read
        self.corrected_ticks += self.corrected_per_read
        status["animfix_client_ticks"] = self.client_ticks
        status["animfix_enabled_client_ticks"] = self.enabled_client_ticks
        status["animfix_corrected_ticks"] = self.corrected_ticks
        return status


class TestAnimfixDutyCycle(unittest.TestCase):
    """The **duty cycle** the scenario reports.

    The payload's counters are cumulative, and the two of them have
    **different windows**: the correction counter only moves while the
    animfix is on, and the driver's own client-tick counter climbs since the
    payload started (the animfix is off by default). Dividing the two
    absolute counters therefore reports a duty cycle of 0 % for a feature
    that wins every tick — the quotient is diluted by the payload's uptime.
    """

    def test_the_duty_cycle_is_measured_over_the_window_not_the_uptime(self) -> None:
        """A payload that has been up for an hour still reads 100 %."""
        game = UptimeGame(uptime_ticks=3608)
        result = animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertAlmostEqual(float(result["duty_cycle"]), 1.0)
        # The window's deltas are what is reported.
        self.assertEqual(result["corrected_ticks_during_window"], 8)
        self.assertEqual(result["client_ticks_during_window"], 8)

    def test_a_duty_cycle_below_the_floor_is_a_failure(self) -> None:
        """The correction wins on a quarter of the ticks: that is a failure."""
        game = UptimeGame(uptime_ticks=3608, ticks_per_read=8, corrected_per_read=2)
        with self.assertRaises(Exception):
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        # The feature is switched off again even when the run fails.
        self.assertEqual(game.writes[-1], ("animfix", False))

    def test_a_payload_without_client_ticks_does_not_fail_the_run(self) -> None:
        """No client ticks means the duty cycle is not measurable, not zero."""
        game = FakeGame({
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
        })
        result = animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertIsNone(result["duty_cycle"])


class TestAnimfixCodecDiagnosis(unittest.TestCase):
    """The **diagnosis** the codec counters settle.

    The failure this exists for: a scenario that says "nothing was captured"
    and stops there. That is a *symptom* — the operator cannot tell "the
    codec never ran" (the seam is wrong) from "the codec ran for somebody
    else" (the comparison is wrong) from "the local controller could not be
    resolved" (the payload's own state is stale). Each of the three needs a
    different fix, so the scenario must name which one it was.
    """

    @staticmethod
    def _block(**counters: Any) -> dict[str, Any]:
        block: dict[str, Any] = {
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
            "animfix_codec_calls": 0,
            "animfix_codec_other_controller": 0,
            "animfix_codec_not_local": 0,
            "animfix_codec_local_calls": 0,
        }
        block.update(counters)
        return block

    def _run(self, block: dict[str, Any]) -> str:
        game = FakeGame(block, climbing=False)
        with self.assertRaises(Exception) as raised:
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        return str(raised.exception)

    def test_no_codec_call_is_h1(self) -> None:
        """Not one call: the hooked codec never runs."""
        message = self._run(self._block())
        self.assertIn("H1", message)
        self.assertIn("never ran", message)

    def test_calls_for_other_controllers_are_h2(self) -> None:
        """The codec runs, but never for the local player."""
        message = self._run(self._block(animfix_codec_calls=12,
                                        animfix_codec_other_controller=12))
        self.assertIn("H2", message)
        self.assertIn("never for the local controller", message)

    def test_an_unresolvable_local_controller_is_h2(self) -> None:
        """The comparison could not be made: a different fix again."""
        message = self._run(self._block(animfix_codec_calls=5,
                                        animfix_codec_not_local=5))
        self.assertIn("H2", message)
        self.assertIn("could not be resolved", message)

    def test_a_healthy_payload_is_not_diagnosed_as_broken(self) -> None:
        """The codec ran for the local player: the diagnosis says so."""
        diagnosis = animfix_scenario.diagnose_codec(9, 8, 0, 1)
        self.assertIn("local controller", diagnosis)
        self.assertNotIn("H1", diagnosis)
        self.assertNotIn("H2", diagnosis)

    def test_the_diagnosis_agrees_with_the_payloads_own(self) -> None:
        """The scenario and the payload must not tell different stories."""
        self.assertIn("never ran", animfix_scenario.diagnose_codec(0, 0, 0, 0))
        self.assertIn("never for the local controller",
                      animfix_scenario.diagnose_codec(3, 3, 0, 0))
        self.assertIn("could not be resolved",
                      animfix_scenario.diagnose_codec(3, 0, 3, 0))
        self.assertIn("local controller", animfix_scenario.diagnose_codec(3, 2, 0, 1))

    def test_the_counters_are_reported(self) -> None:
        """The numbers are in the result: the operator reads them."""
        game = FakeGame(self._block(animfix_codec_calls=7,
                                    animfix_codec_other_controller=7), climbing=False)
        with self.assertRaises(Exception):
            animfix_scenario.run(cast(Game, game), watch=0.0, setup=False)
        self.assertEqual(game.writes[-1], ("animfix", False))


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
