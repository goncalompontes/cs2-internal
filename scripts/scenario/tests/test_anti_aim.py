"""Unit tests for the anti-aim's scenario (`scripts/scenario/scenarios/anti_aim.py`).

The scenario's **split check** is the part that can be wrong without the game:
the fake yaw is *derived* from the camera's yaw (the configured offset), so the
gap between the two says nothing on its own \u2014 what it says is whether the gap
**collapsed** (a camera that followed the fake angles would report the fake yaw
as its own). The check must therefore

* **skip** when the configured offset is ~0 (the fake yaw *is* the camera's yaw
  by construction \u2014 there is nothing to collapse), and
* **fail** when the camera really did follow the fake angles.

Both halves are driven here with a stub session \u2014 no CS2 required:

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from typing import Any, cast
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario.scenarios import anti_aim  # noqa: E402
from scenario.session import Game, SessionError  # noqa: E402


def status(**overrides: Any) -> dict[str, Any]:
    """The payload's `input` status, as the anti-aim leaves it."""
    raw: dict[str, Any] = {
        "anti_aim": True,
        "anti_aim_move_rebase": True,
        "anti_aim_acted": True,
        "anti_aim_reason": 1,
        "anti_aim_writes": 100,
        "anti_aim_rebase_applied": 100,
        "anti_aim_rebase_skipped": 0,
        # The camera looks at 100 degrees; the anti-aim's fake yaw is 30 degrees
        # to the side of it.
        "anti_aim_camera_yaw": 100.0,
        "anti_aim_fake_yaw": 130.0,
        "anti_aim_forward": 1.0,
        "anti_aim_left": -1.0,
        "anti_aim_direction_error": 15.0,
        "anti_aim_wish_factor": 1.0,
        "anti_aim_saturated": False,
        "anti_aim_yaw_offset": 30.0,
        "anti_aim_pitch": 0.0,
        "anti_aim_jitter": 0.0,
        "anti_aim_side": 0,
    }
    raw.update(overrides)
    return raw


class StubGame:
    """A session that answers the scenario's only question: the input status.

    The write counter **climbs** on every read, like the payload's does while
    the anti-aim acts \u2014 the scenario reads it twice and compares.
    """

    def __init__(self, live: dict[str, Any]) -> None:
        self._live = live
        self._calls = 0

    def input(self, via: Any = None) -> dict[str, Any]:
        self._calls += 1
        answer = dict(self._live)
        if answer.get("anti_aim_acted"):
            answer["anti_aim_writes"] = int(answer.get("anti_aim_writes") or 0) + self._calls
        return answer


def run(live: dict[str, Any]) -> dict[str, Any]:
    """Run the scenario against a stub session (no game, no network)."""
    game = cast(Game, StubGame(live))
    with mock.patch("scenario.awaiting.ensure_live_round"), \
            mock.patch("scenario.awaiting.screen", return_value="live"):
        return anti_aim.run(game, watch=0.01)


class TestAntiAimScenario(unittest.TestCase):
    """The anti-aim scenario's verdicts."""

    def test_a_healthy_anti_aim_passes(self) -> None:
        result = run(status())
        self.assertFalse(result.get("skipped", False))
        self.assertIn("the camera did not follow", str(result.get("verdict")))
        self.assertIn("30.0", str(result.get("anti_aim_split_gap_deg")))

    def test_the_default_configuration_is_skipped_not_failed(self) -> None:
        """The spec's own example command: offset 0, jitter 0.

        The fake yaw is then the camera's yaw **by construction**, so the gap
        says nothing \u2014 the check must be skipped, not failed.
        """
        live = status(anti_aim_yaw_offset=0.0, anti_aim_jitter=0.0,
                      anti_aim_fake_yaw=100.0, anti_aim_camera_yaw=100.0)
        result = run(live)
        self.assertIn("skipped", str(result.get("split_check")))
        self.assertNotIn("the split is broken", str(result.get("verdict")))

    def test_a_camera_that_follows_the_fake_angles_fails(self) -> None:
        """The failure the check exists for: the camera's yaw *is* the fake yaw."""
        live = status(anti_aim_fake_yaw=100.0, anti_aim_camera_yaw=100.0)
        with self.assertRaises(SessionError) as raised:
            run(live)
        self.assertIn("the camera followed the fake angles", str(raised.exception))

    def test_a_disabled_anti_aim_is_skipped(self) -> None:
        result = run(status(anti_aim=False))
        self.assertTrue(result.get("skipped", False))

    def test_an_anti_aim_without_its_rebase_is_skipped(self) -> None:
        result = run(status(anti_aim_move_rebase=False))
        self.assertTrue(result.get("skipped", False))
        self.assertIn("movement re-base is OFF", str(result.get("verdict")))

    def test_an_anti_aim_that_never_acted_fails(self) -> None:
        with self.assertRaises(SessionError) as raised:
            run(status(anti_aim_acted=False))
        self.assertIn("never acted", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
