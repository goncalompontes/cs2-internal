"""Unit tests for the readiness layer (`scripts/scenario/awaiting.py`).

The helpers here are what every scenario calls before it touches the game:
"is the player alive?", "does the console actually move it?", "put the player
on that spot and wait until it stopped". They had **no** tests, so a wrong
answer showed up as a scenario that quietly measured nothing.

They are driven against a **fake game** — a small object that answers the
calls the helpers make and records what they did — so no CS2 and no payload
are needed:

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from typing import Any, Callable, cast

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import awaiting  # noqa: E402
from scenario.protocol import (  # noqa: E402
    EventsResult,
    ExecResult,
    LookResult,
    PingResult,
    PressResult,
    StateSnapshotDict,
    Vec3Dict,
    WaitResult,
)
from scenario.session import Game, SessionError  # noqa: E402
from scenario.tests.test_screens import snapshot  # noqa: E402


class FakeGame:
    """A game that answers the readiness layer's calls from a script.

    The snapshot is mutable, and the console commands and the key presses can
    change it (that is how "the player walks" is simulated), so the helpers
    see the same kind of world they see live: a moving player, a growing
    player list.
    """

    def __init__(self, raw: StateSnapshotDict | None = None) -> None:
        self.raw: StateSnapshotDict = raw if raw is not None else snapshot()
        self.execs: list[str] = []
        self.looks: list[dict[str, Any]] = []
        self.presses: list[dict[str, Any]] = []
        self.teleports: list[tuple[float, float, float | None]] = []
        self.waits: list[dict[str, Any]] = []
        self.events_seen: list[int] = []
        #: What a console line does to the world (the "physics").
        self.on_exec: Callable[[str], None] | None = None
        #: What a held key does to the world.
        self.on_press: Callable[[str], None] | None = None
        #: How many players the world has (``bot_add`` grows it).
        self.player_count = len(self.raw.get("players") or [])
        #: Whether a teleport actually moves the player.
        self.teleport_lands = True
        #: Whether a ``wait_for`` matches.
        self.wait_matches = True

    # --- the channel ------------------------------------------------------
    def state(self) -> StateSnapshotDict:
        return self.raw

    def events(self, limit: int = 64, subscribe: str | list[str] | None = None) -> EventsResult:
        self.events_seen.append(limit)
        return {"count": 0, "subscribe": {}, "events": []}

    def exec(self, line: str) -> ExecResult:
        self.execs.append(line)
        if self.on_exec is not None:
            self.on_exec(line)
        return {"queued": True}

    def wait_for(self, *, timeout: float = 30.0, event: str | None = None,
                 event_args: dict[str, Any] | None = None,
                 **predicates: Any) -> WaitResult:
        self.waits.append({"timeout": timeout, "event": event, **predicates})
        return {"matched": self.wait_matches, "snapshot": None}

    def ping(self) -> PingResult:
        return {"pong": True, "pid": 1, "uptime_ms": 60_000, "exec_ready": True,
                "events_ready": True, "events_subscribed": 0,
                "events_dispatch_calls": 0, "input_ready": True,
                "input_hooked": True, "input_backend": "console",
                "state_ready": True}

    def look(self, **kwargs: Any) -> LookResult:
        self.looks.append(kwargs)
        return {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}

    def press(self, key: str, hold: float = 0.0, via: str | None = None) -> PressResult:
        self.presses.append({"key": key, "hold": hold, "via": via})
        if self.on_press is not None:
            self.on_press(key)
        return {"button": key, "held": hold <= 0}

    def teleport(self, x: float, y: float, z: float | None = None) -> ExecResult:
        self.teleports.append((x, y, z))
        if self.teleport_lands:
            self.raw = snapshot(local={"origin": {"x": x, "y": y,
                                                 "z": z if z is not None else 0.0}})
        return {"queued": True}

    def players(self) -> list[dict[str, Any]]:
        return [{"team": 2}] * self.player_count


def as_game(fake: FakeGame) -> Game:
    """The fake in the shape the helpers expect (the façade's contract)."""
    return cast(Game, fake)


class TestStateReaders(unittest.TestCase):
    """The pure readers — what the snapshot says."""

    def test_the_local_player_is_read_by_name(self) -> None:
        fake = FakeGame()
        self.assertEqual(awaiting.local(as_game(fake))["name"], "player")

    def test_a_missing_local_player_is_an_empty_mapping(self) -> None:
        fake = FakeGame(snapshot(local=None))
        self.assertEqual(awaiting.local(as_game(fake)), {})

    def test_the_origin_is_the_local_players_origin(self) -> None:
        fake = FakeGame()
        origin = awaiting.origin(as_game(fake))
        self.assertIsNotNone(origin)
        self.assertAlmostEqual(origin["x"], 1.0)  # type: ignore[index]

    def test_on_ground_defaults_to_true_when_unknown(self) -> None:
        """An unknown ground state is not a reason to refuse the input."""
        fake = FakeGame(snapshot(local=None))
        self.assertTrue(awaiting.on_ground(as_game(fake)))

    def test_alive_is_false_without_a_local_player(self) -> None:
        fake = FakeGame(snapshot(local=None))
        self.assertFalse(awaiting.alive(as_game(fake)))

    def test_in_map_is_the_payloads_own_flag(self) -> None:
        self.assertTrue(awaiting.in_map(as_game(FakeGame())))
        self.assertFalse(awaiting.in_map(as_game(FakeGame(snapshot(in_map=False)))))

    def test_a_real_map_has_a_position_or_a_team(self) -> None:
        """The menu exposes a local at ``(0,0,0)`` with no team."""
        menu = FakeGame(snapshot(local={"team": 0, "alive": False,
                                        "origin": {"x": 0.0, "y": 0.0, "z": 0.0}}))
        self.assertFalse(awaiting.in_real_map(as_game(menu)))
        self.assertTrue(awaiting.in_real_map(as_game(FakeGame())))

    def test_playable_needs_a_live_player_on_a_team(self) -> None:
        self.assertTrue(awaiting.playable(as_game(FakeGame())))
        dead = FakeGame(snapshot(local={"alive": False}))
        self.assertFalse(awaiting.playable(as_game(dead)))
        select = FakeGame(snapshot(local={"team": 0}))
        self.assertFalse(awaiting.playable(as_game(select)))

    def test_the_displacement_ignores_the_height(self) -> None:
        a: dict[str, float] = {"x": 0.0, "y": 0.0, "z": 0.0}
        b: dict[str, float] = {"x": 3.0, "y": 4.0, "z": 100.0}
        self.assertAlmostEqual(awaiting.displacement(a, b), 5.0)

    def test_the_speed_is_the_horizontal_velocity(self) -> None:
        fake = FakeGame(snapshot(local={"velocity": {"x": 3.0, "y": 4.0, "z": 9.0}}))
        self.assertAlmostEqual(awaiting.speed_2d(as_game(fake)), 5.0)


class TestWaits(unittest.TestCase):
    """The wait helpers — they poll, so they must also be able to give up."""

    def test_wait_on_ground_returns_when_the_player_is_grounded(self) -> None:
        self.assertTrue(awaiting.wait_on_ground(as_game(FakeGame()), timeout=0.2))

    def test_wait_airborne_gives_up_on_a_grounded_player(self) -> None:
        self.assertFalse(awaiting.wait_airborne(as_game(FakeGame()), timeout=0.2))

    def test_wait_alive_gives_up_on_a_dead_player(self) -> None:
        dead = FakeGame(snapshot(local={"alive": False}))
        self.assertFalse(awaiting.wait_alive(as_game(dead), timeout=0.2))

    def test_wait_playable_uses_the_payloads_predicate(self) -> None:
        fake = FakeGame()
        self.assertTrue(awaiting.wait_playable(as_game(fake), timeout=0.2))
        self.assertEqual(fake.waits[-1]["playable"], True)

    def test_wait_round_started_waits_for_both_events(self) -> None:
        """A live round needs the start *and* the end of the freeze."""
        fake = FakeGame()
        self.assertTrue(awaiting.wait_round_started(as_game(fake), timeout=0.2))
        self.assertEqual([wait["event"] for wait in fake.waits],
                         ["round_start", "round_freeze_end"])


class TestIsMovable(unittest.TestCase):
    """The liveness gate: does the console actually move the player?"""

    def walk_to(self, fake: FakeGame, distance: float) -> None:
        """Make the fake's player walk ``distance`` units on ``+forward``."""
        def handle(line: str) -> None:
            if line == "+forward":
                fake.raw = snapshot(local={"origin": {"x": distance, "y": 0.0, "z": 0.0}})
        fake.on_exec = handle

    def test_a_walk_is_a_movement(self) -> None:
        fake = FakeGame()
        self.walk_to(fake, 100.0)
        self.assertTrue(awaiting.is_movable(as_game(fake), hold=0.0))

    def test_a_player_that_does_not_move_is_not_movable(self) -> None:
        fake = FakeGame()
        self.assertFalse(awaiting.is_movable(as_game(fake), hold=0.0))

    def test_a_short_walk_is_below_the_threshold(self) -> None:
        fake = FakeGame()
        self.walk_to(fake, 10.0)
        self.assertFalse(awaiting.is_movable(as_game(fake), hold=0.0))

    def test_the_threshold_is_the_callers(self) -> None:
        fake = FakeGame()
        self.walk_to(fake, 10.0)
        self.assertTrue(awaiting.is_movable(as_game(fake), hold=0.0, min_units=5.0))

    def test_without_a_local_player_there_is_no_movement(self) -> None:
        fake = FakeGame(snapshot(local=None))
        self.assertFalse(awaiting.is_movable(as_game(fake), hold=0.0))

    def test_the_console_key_is_held_and_released(self) -> None:
        fake = FakeGame()
        self.walk_to(fake, 100.0)
        awaiting.is_movable(as_game(fake), hold=0.0)
        self.assertEqual(fake.execs, ["+forward", "-forward"])


class TestRecenter(unittest.TestCase):
    """The teleport that has to *land* before the measurement starts."""

    def test_a_teleport_is_applied_and_waits_for_the_move(self) -> None:
        fake = FakeGame()
        result = awaiting.recenter(as_game(fake), {"x": 50.0, "y": 25.0})
        self.assertTrue(result["applied"])
        self.assertEqual(fake.teleports, [(50.0, 25.0, None)])
        self.assertIsNotNone(result["origin"])

    def test_without_a_spot_nothing_is_teleported(self) -> None:
        fake = FakeGame()
        result = awaiting.recenter(as_game(fake), None)
        self.assertFalse(result["applied"])
        self.assertEqual(fake.teleports, [])

    def test_a_player_already_on_the_spot_is_not_moved(self) -> None:
        fake = FakeGame(snapshot(local={"origin": {"x": 10.0, "y": 10.0, "z": 0.0}}))
        result = awaiting.recenter(as_game(fake), {"x": 10.0, "y": 10.0})
        self.assertTrue(result["applied"])
        self.assertEqual(result["distance"], 0.0)
        self.assertEqual(fake.teleports, [])

    def test_the_height_is_teleported_too(self) -> None:
        fake = FakeGame()
        awaiting.recenter(as_game(fake), {"x": 11.0, "y": 12.0, "z": 3.0})
        self.assertEqual(fake.teleports, [(11.0, 12.0, 3.0)])

    def test_a_move_that_does_not_land_is_visible_in_the_distance(self) -> None:
        """A teleport the engine ignores must not look like a success.

        The success criterion is "the position stopped changing" — the engine
        may resolve a ``setpos`` to a nearby standing spot — so a teleport
        that never happens is reported as applied, but the *distance* is what
        tells the caller that it did not.
        """
        fake = FakeGame()
        fake.teleport_lands = False
        result = awaiting.recenter(as_game(fake), {"x": 500.0, "y": 500.0},
                                   timeout=0.2)
        self.assertGreater(result["distance"], 100.0)


class TestSettle(unittest.TestCase):
    """The wait for the player to stand still."""

    def test_a_still_player_is_settled(self) -> None:
        fake = FakeGame(snapshot(local={"velocity": {"x": 0.0, "y": 0.0, "z": 0.0}}))
        self.assertTrue(awaiting.settle(as_game(fake), timeout=0.2))

    def test_a_moving_player_is_not_settled(self) -> None:
        fake = FakeGame(snapshot(local={"velocity": {"x": 100.0, "y": 0.0, "z": 0.0}}))
        self.assertFalse(awaiting.settle(as_game(fake), timeout=0.2))

    def test_a_player_in_the_air_is_not_settled(self) -> None:
        """A falling player ruins the horizontal reading."""
        fake = FakeGame(snapshot(local={"on_ground": False}))
        self.assertFalse(awaiting.settle(as_game(fake), timeout=0.2))

    def test_the_ground_can_be_dropped_from_the_requirement(self) -> None:
        fake = FakeGame(snapshot(local={"on_ground": False}))
        self.assertTrue(awaiting.settle(as_game(fake), timeout=0.2,
                                         require_ground=False))

    def test_the_speed_threshold_is_the_callers(self) -> None:
        fake = FakeGame(snapshot(local={"velocity": {"x": 20.0, "y": 0.0, "z": 0.0}}))
        self.assertFalse(awaiting.settle(as_game(fake), timeout=0.2, max_speed=5.0))
        self.assertTrue(awaiting.settle(as_game(fake), timeout=0.2, max_speed=50.0))


class TestPrepare(unittest.TestCase):
    """The pair the measurements start from."""

    def test_prepare_reports_both_halves(self) -> None:
        fake = FakeGame()
        result = awaiting.prepare(as_game(fake), {"x": 20.0, "y": 0.0})
        self.assertTrue(result["applied"])
        self.assertTrue(result["settled"])
        self.assertIn("speed", result)
        self.assertIn("origin", result)

    def test_prepare_without_a_spot_only_settles(self) -> None:
        fake = FakeGame()
        result = awaiting.prepare(as_game(fake))
        self.assertEqual(fake.teleports, [])
        self.assertTrue(result["settled"])


class TestMeasureMove(unittest.TestCase):
    """The displacement a held key produces."""

    def test_the_displacement_is_measured(self) -> None:
        fake = FakeGame()

        def walk(key: str) -> None:
            fake.raw = snapshot(local={"origin": {"x": 60.0, "y": 0.0, "z": 0.0}})

        fake.on_press = walk
        result = awaiting.measure_move(as_game(fake), hold=0.0)
        self.assertAlmostEqual(result["moved"], 59.0)  # from (1,2,3) to (60,0,0)
        self.assertIsNotNone(result["before"])
        self.assertIsNotNone(result["after"])

    def test_the_view_is_set_first_when_a_yaw_is_given(self) -> None:
        fake = FakeGame()
        awaiting.measure_move(as_game(fake), yaw=90.0, hold=0.0)
        self.assertEqual(fake.looks[-1]["yaw"], 90.0)

    def test_a_key_that_moves_nothing_measures_zero(self) -> None:
        fake = FakeGame()
        result = awaiting.measure_move(as_game(fake), hold=0.0)
        self.assertEqual(result["moved"], 0.0)


class TestClearHeading(unittest.TestCase):
    """The clearance sweep — it finds the yaw the player can actually walk."""

    def test_the_openest_heading_wins(self) -> None:
        fake = FakeGame()

        def walk(key: str) -> None:
            # Only the first heading is open; the others are blocked.
            yaw = fake.looks[-1].get("yaw") if fake.looks else None
            distance = 100.0 if yaw == 0.0 else 5.0
            fake.raw = snapshot(local={"origin": {"x": distance, "y": 0.0, "z": 0.0}})

        fake.on_press = walk
        result = awaiting.pick_clear_heading(as_game(fake), headings=4, hold=0.0)
        self.assertEqual(result["yaw"], 0.0)
        self.assertEqual(result["readings"], 4)
        self.assertEqual(len(result["all"]), 4)

    def test_the_best_heading_is_left_applied(self) -> None:
        fake = FakeGame()
        awaiting.pick_clear_heading(as_game(fake), headings=2, hold=0.0)
        self.assertEqual(fake.looks[-1]["yaw"], 0.0)

    def test_a_dead_player_has_no_measurements(self) -> None:
        """A dead player ignores ``setang`` — the sweep must say so."""
        fake = FakeGame(snapshot(local={"alive": False, "life_state": 2}))
        result = awaiting.pick_clear_heading(as_game(fake), headings=2, hold=0.0)
        self.assertEqual(result["readings"], 0)
        self.assertTrue(all("error" in reading for reading in result["all"]))
        self.assertIn("died", result["all"][0]["error"])


class TestBots(unittest.TestCase):
    """The bot-match setup."""

    def test_add_bots_kicks_and_adds_to_both_sides(self) -> None:
        fake = FakeGame()
        fake.player_count = 1

        def handle(line: str) -> None:
            if line == "bot_kick":
                fake.player_count = 1
            elif line.startswith("bot_add"):
                fake.player_count += 1

        fake.on_exec = handle
        count = awaiting.add_bots(as_game(fake), per_team=2, timeout=0.1)
        self.assertEqual(count, 5)
        self.assertIn("bot_kick", fake.execs)
        self.assertIn("bot_add_t", fake.execs)
        self.assertIn("bot_add_ct", fake.execs)

    def test_an_add_that_never_lands_is_an_error(self) -> None:
        """A bot that does not join must not be counted as joined."""
        fake = FakeGame()
        fake.player_count = 0
        fake.wait_matches = False
        with self.assertRaises(SessionError) as raised:
            awaiting.add_bots(as_game(fake), per_team=1, attempts=1, timeout=0.1)
        self.assertIn("bot_add", str(raised.exception))

    def test_the_playground_convars_are_applied_in_order(self) -> None:
        fake = FakeGame()
        applied = awaiting.apply_convars(as_game(fake), ["sv_cheats 1", "mp_buytime 9999"])
        self.assertEqual(fake.execs, ["sv_cheats 1", "mp_buytime 9999"])
        self.assertEqual(applied, ["sv_cheats 1", "mp_buytime 9999"])

    def test_the_respawn_convars_are_optional(self) -> None:
        self.assertIn("mp_respawn_on_death_t 1", awaiting.RESPAWN_CONVARS)
        self.assertIn("mp_buytime 9999", awaiting.PLAYGROUND_CONVARS)


class TestBotMatch(unittest.TestCase):
    """The whole setup, driven against a fake game."""

    def test_the_setup_reports_what_it_did(self) -> None:
        fake = FakeGame()
        fake.player_count = 1

        def handle(line: str) -> None:
            if line == "bot_kick":
                fake.player_count = 1
            elif line.startswith("bot_add"):
                fake.player_count += 1

        fake.on_exec = handle
        result = awaiting.ensure_bot_match(as_game(fake), per_team=1,
                                           verify_movable=False, timeout=2.0)
        self.assertEqual(result["map"], "de_dust2")
        self.assertIn("convars", result)
        self.assertIn("screen", result)
        self.assertIn("sv_cheats 1", fake.execs)
        self.assertIn("bot_stop 1", fake.execs)

    def test_the_respawn_convars_are_only_applied_when_asked(self) -> None:
        fake = FakeGame()
        fake.player_count = 1
        result = awaiting.ensure_bot_match(as_game(fake), per_team=0, respawn=False,
                                           bot_stop=False, verify_movable=False,
                                           timeout=2.0)
        self.assertNotIn("mp_respawn_on_death_t 1", result["convars"])


class TestRoundState(unittest.TestCase):
    """The diagnostic snapshot."""

    def test_the_round_state_names_the_screen_and_the_blocks(self) -> None:
        fake = FakeGame()
        state = awaiting.round_state(as_game(fake))
        self.assertEqual(state["screen"], "live")
        self.assertIn("rules", state)
        self.assertIn("blocked", state)
        self.assertIn("recent_events", state)


class TestVec3(unittest.TestCase):
    """The origin helper the helpers share."""

    def test_an_origin_is_read_by_its_axes(self) -> None:
        origin: Vec3Dict = {"x": 1.0, "y": 2.0, "z": 3.0}
        self.assertEqual(origin["y"], 2.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
