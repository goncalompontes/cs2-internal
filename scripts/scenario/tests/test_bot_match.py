"""Unit tests for the bot-match setup (``awaiting.ensure_bot_match``).

The scenario's contract (2026-10-21) is an **order** and a **final wait**::

    load the map → join the team → the convars → mp_restartgame 1 →
    wait for the round to go live → add the bots → bot_stop 1 →
    wait for the round to be live again → return

The tests drive it against a **fake game** that models the world (a screen, a
team, a growing player list) and records every console line *and* every
observation in one timeline — so the order can be asserted, and a world that
never goes live can be made to fail:

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import inspect
import pathlib
import sys
import time
import unittest
from typing import Any, cast

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import awaiting, screens  # noqa: E402
from scenario.protocol import (  # noqa: E402
    EventsResult,
    ExecResult,
    PingResult,
    PlayerSnapshotDict,
    StateSnapshotDict,
    WaitResult,
)
from scenario.session import Game, SessionError  # noqa: E402
from scenario.tests.test_screens import player, snapshot  # noqa: E402


class BotMatchGame:
    """A fake game that models the bot-match flow.

    The world has a **screen**, and the console lines move it:

    ==============  =========================================================
    phase           what the machine sees
    ==============  =========================================================
    ``select``      the level is up, the local player has no team (the
                    team-select screen)
    ``warmup``      the local player has a team, the round has not started
    ``live``        the round is running, the buy window is closed
    ``buy``         the round is running, the buy window is still open
    ``stuck``       the buy window is open and the world does **not**
                    simulate — the player cannot move
    ==============  =========================================================

    ``jointeam`` gives the player a team and ``mp_restartgame 1`` starts the
    round. Every console line and every observation lands in one **timeline**,
    so a test can assert the order of the setup.
    """

    def __init__(self, *, restart_phase: str = "live",
                 bot_phase: str | None = None, team: int = 0) -> None:
        self.phase = "select" if team == 0 else "warmup"
        self.restart_phase = restart_phase
        self.bot_phase = bot_phase
        self.team = team
        #: Whether the world is broken — a broken world ignores the restarts.
        self.broken = False
        self.player_count = 1
        self.origin: dict[str, float] = {"x": 1.0, "y": 2.0, "z": 3.0}
        #: Every console line, in order.
        self.execs: list[str] = []
        #: ``("exec", line)`` and ``("state", screen)`` entries, in order.
        self.timeline: list[tuple[str, str]] = []
        self.waits: list[dict[str, Any]] = []
        #: Whether the world simulates: a player that does not move is the
        #: machine's liveness gate failing.
        self.simulating = True

    # --- the timeline -----------------------------------------------------
    def index_of(self, kind: str, value: str) -> int:
        """Where ``value`` happened in the timeline (the first one)."""
        for position, (entry_kind, entry) in enumerate(self.timeline):
            if entry_kind == kind and entry == value:
                return position
        raise AssertionError(f"{value!r} never happened ({kind})")

    # --- the world --------------------------------------------------------
    def _snapshot(self) -> StateSnapshotDict:
        if self.phase == "select":
            return snapshot(local={"team": 0, "alive": False,
                                   "origin": dict(self.origin)},
                            rules={"warmup": False, "freeze": False})
        if self.phase == "warmup":
            return snapshot(local={"team": self.team, "alive": True,
                                   "origin": dict(self.origin)},
                            rules={"warmup": True, "freeze": False})
        buy_open = self.phase in ("buy", "stuck")
        return snapshot(local={"team": self.team, "alive": True,
                               "origin": dict(self.origin)},
                        rules={"warmup": False, "freeze": False,
                               "t_cant_buy": not buy_open,
                               "ct_cant_buy": not buy_open})

    def _react(self, line: str) -> None:
        if line.startswith("jointeam"):
            self.team = 2
            self.phase = "warmup"
        elif line == "mp_restartgame 1":
            if not self.broken:
                self.phase = self.restart_phase
        elif line == "bot_kick":
            self.player_count = 1
            if self.bot_phase is not None:
                # The bots' arrival broke the round: the world stays in the
                # buy window and never simulates again.
                self.broken = True
                self.phase = self.bot_phase
                self.simulating = False
        elif line.startswith("bot_add"):
            self.player_count += 1
        elif line == "+forward" and self.simulating:
            self.origin = {"x": self.origin["x"] + 100.0,
                           "y": self.origin["y"], "z": self.origin["z"]}

    # --- the channel ------------------------------------------------------
    def state(self) -> StateSnapshotDict:
        raw = self._snapshot()
        self.timeline.append(("state", str(screens.deduce(raw, ()).screen)))
        return raw

    def events(self, limit: int = 64,
               subscribe: str | list[str] | None = None) -> EventsResult:
        return {"count": 0, "subscribe": {}, "events": []}

    def exec(self, line: str) -> ExecResult:
        self.execs.append(line)
        self.timeline.append(("exec", line))
        self._react(line)
        return {"queued": True}

    def wait_for(self, *, timeout: float = 30.0, event: str | None = None,
                 event_args: dict[str, Any] | None = None,
                 **predicates: Any) -> WaitResult:
        self.waits.append({"timeout": timeout, "event": event, **predicates})
        return {"matched": True, "snapshot": None}

    def ping(self) -> PingResult:
        return {"pong": True, "pid": 1, "uptime_ms": 60_000, "exec_ready": True,
                "events_ready": True, "events_subscribed": 0,
                "events_dispatch_calls": 0, "input_ready": True,
                "input_hooked": True, "input_backend": "console",
                "state_ready": True}

    def players(self) -> list[PlayerSnapshotDict]:
        return [cast(PlayerSnapshotDict,
                     player(index=index + 1, name=f"bot{index}", team=2,
                            is_local=False))
                for index in range(self.player_count)]


def as_game(fake: BotMatchGame) -> Game:
    """The fake in the shape the helpers expect (the façade's contract)."""
    return cast(Game, fake)


def run_setup(fake: BotMatchGame, **kwargs: Any) -> dict[str, Any]:
    """The bot-match setup against the fake, with short budgets.

    The knobs the **pre-fix** signature does not have are dropped, so the
    suite can also be run against the old code: it then fails on the
    *behaviour* (the order, the final wait, the result) instead of on an
    unknown keyword.
    """
    options: dict[str, Any] = {"per_team": 1, "verify_movable": False,
                               "timeout": 30.0, "final_timeout": 2.0}
    options.update(kwargs)
    accepted = inspect.signature(awaiting.ensure_bot_match).parameters
    return awaiting.ensure_bot_match(
        as_game(fake),
        **{name: value for name, value in options.items() if name in accepted})


class TestTheOrder(unittest.TestCase):
    """The order is the contract: the team first, the bots last."""

    def test_the_team_is_joined_before_the_convars_and_the_restart(self) -> None:
        fake = BotMatchGame()
        run_setup(fake)
        join = fake.index_of("exec", "jointeam 2")
        convar = fake.index_of("exec", "mp_roundtime 60")
        restart = fake.index_of("exec", "mp_restartgame 1")
        self.assertLess(join, convar, "the team is joined after the convars")
        self.assertLess(join, restart, "the team is joined after the restart")

    def test_the_bots_are_added_after_the_round_went_live(self) -> None:
        fake = BotMatchGame()
        run_setup(fake)
        live = fake.index_of("state", "live")
        bot = fake.index_of("exec", "bot_add_t")
        self.assertLess(live, bot, "the bots are added before the round is live")

    def test_the_bots_are_added_before_the_final_wait(self) -> None:
        """The final wait is the scenario's **last** act."""
        fake = BotMatchGame()
        run_setup(fake)
        bot = fake.index_of("exec", "bot_add_t")
        stop = fake.index_of("exec", "bot_stop 1")
        self.assertLess(bot, stop)
        # Nothing but the observation of the result happens after the bots.
        after = [entry for entry in fake.timeline[stop + 1:]
                 if entry[0] == "exec"]
        self.assertEqual(after, [])


class TestTheFinalWait(unittest.TestCase):
    """The scenario must not return before the round is live again."""

    def test_a_round_that_never_goes_live_raises(self) -> None:
        """A world stuck in the buy window (and not simulating) is a failure."""
        fake = BotMatchGame(bot_phase="stuck")
        with self.assertRaises(SessionError) as raised:
            run_setup(fake, verify_movable=True, final_timeout=2.0)
        self.assertIn("did not go live", str(raised.exception))

    def test_the_wait_is_bounded(self) -> None:
        """The wait has a timeout — the scenario never hangs."""
        fake = BotMatchGame(bot_phase="stuck")
        started = time.monotonic()
        with self.assertRaises(SessionError) as raised:
            run_setup(fake, verify_movable=True, final_timeout=1.0)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 20.0, "the final wait is not bounded")
        self.assertIn("within 1s", str(raised.exception))

    def test_a_live_round_returns(self) -> None:
        """The happy path: the round is live, so the scenario returns."""
        fake = BotMatchGame()
        result = run_setup(fake)
        self.assertEqual(result["screen"], "live")


class TestTheResult(unittest.TestCase):
    """What the scenario says about what it did."""

    def test_the_result_reports_the_live_screen_and_the_round(self) -> None:
        fake = BotMatchGame()
        result = run_setup(fake)
        self.assertEqual(result["screen"], "live")
        self.assertEqual(result["round"]["screen"], "live")
        self.assertIn("recent_events", result["round"])
        self.assertIn("rules", result["round"])

    def test_the_result_reports_how_long_the_final_wait_took(self) -> None:
        fake = BotMatchGame()
        result = run_setup(fake)
        self.assertIn("waited_s", result)
        self.assertGreaterEqual(result["waited_s"], 0.0)

    def test_the_result_says_the_team_was_joined(self) -> None:
        fake = BotMatchGame()
        result = run_setup(fake)
        self.assertEqual(result["team_joined"], "joined")
        self.assertIn("jointeam 2", fake.execs)

    def test_the_result_says_auto_when_the_team_was_already_there(self) -> None:
        """The server assigns the team on its own — the result must say so."""
        fake = BotMatchGame(team=2)
        result = run_setup(fake)
        self.assertEqual(result["team_joined"], "auto")
        self.assertNotIn("jointeam 2", fake.execs)

    def test_the_old_keys_keep_their_meaning(self) -> None:
        fake = BotMatchGame()
        result = run_setup(fake)
        self.assertEqual(result["map"], "de_dust2")
        self.assertEqual(result["players"], 3)  # the local player + 2 bots
        self.assertEqual(result["teams"], {2: 3})
        self.assertIn("mp_buytime 9999", result["convars"])
        self.assertEqual(result["local_alive"], True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
