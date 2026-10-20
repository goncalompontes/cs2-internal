"""Unit tests for the state machine's *pure* logic (`scripts/scenario/screens.py`).

The live ``screens`` scenario exercises the machine against a running game; this
file exercises the parts that can be wrong without one:

* the **decision table** — the order the deduction applies the rules in,
* the **capability resolution** — what a screen allows and why it refuses,
* the **transition graph** — connectivity, and the action vocabulary,
* the **actions** — that the right one fires for a screen and that an action
  which does not apply never touches the game.

The actions are driven against a **fake channel** (a stand-in for the control
pipe), so no CS2 and no payload are needed:

    python -m unittest discover -s scripts/scenario/tests
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from dataclasses import dataclass

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import screens  # noqa: E402
from scenario.protocol import ControlTransport  # noqa: E402
from scenario.screens import Can, Machine, Screen  # noqa: E402
from scenario.session import Game, SessionError  # noqa: E402
from scenario.tests.test_screens import blank, snapshot  # noqa: E402


@dataclass(frozen=True)
class Frame:
    """One moment of the fake game: the snapshot and the event tail."""

    raw: dict
    events: tuple[str, ...] = ()


class FakeGame:
    """A stand-in for the control channel.

    It answers the calls the machine makes (``state``, ``events``, ``exec``,
    ``wait_for``, ``ping``) and records what it was asked to do, so a test can
    assert both *that* an action fired and *what* it fired.

    The fake advances to the next **frame** when the machine does something that
    changes the world (a console line, a wait) — so a scripted flow reads like
    the real one: the reconnect opens the next frame.
    """

    def __init__(self, *frames: Frame) -> None:
        self.frames: list[Frame] = list(frames) or [Frame(snapshot())]
        self.execs: list[str] = []
        self.waits: list[dict] = []
        self.polls = 0
        self._index = 0

    # --- the channel ------------------------------------------------------
    @property
    def frame(self) -> Frame:
        return self.frames[min(self._index, len(self.frames) - 1)]

    def state(self) -> dict:
        self.polls += 1
        return self.frame.raw

    def events(self, limit: int = 64, subscribe=None) -> dict:
        return {
            "count": len(self.frame.events),
            "subscribe": {},
            "events": [{"name": name, "seq": index, "args": {}}
                       for index, name in enumerate(self.frame.events)],
        }

    def exec(self, line: str) -> dict:
        self.execs.append(line)
        self._index += 1
        return {"queued": True}

    def wait_for(self, *, timeout: float = 30.0, event: str | None = None,
                 event_args=None, **predicates) -> dict:
        self.waits.append({"timeout": timeout, "event": event, **predicates})
        self._index += 1
        return {"matched": True, "snapshot": None}

    def ping(self) -> dict:
        return {"pong": True, "pid": 1, "uptime_ms": 60_000, "exec_ready": True,
                "events_ready": True, "events_subscribed": 0,
                "events_dispatch_calls": 0, "input_ready": True,
                "input_hooked": True, "input_backend": "console",
                "state_ready": True}


def machine_for(*frames: Frame) -> tuple[Machine, FakeGame]:
    """A machine bound to a fake channel that plays the given frames."""
    game = FakeGame(*frames)
    return Machine(game, debug=False), game


class TestTransitionGraph(unittest.TestCase):
    """The graph of screens — the machine's shape, without a game."""

    def test_every_screen_is_connected_to_the_main_menu(self) -> None:
        """No screen is an orphan.

        The graph is a *drive* graph, not a state automaton: the machine reaches
        some screens without a documented edge (a load in flight, a disconnect),
        so the invariant is connectivity in either direction.
        """
        neighbours: dict[Screen, set[Screen]] = {}
        for edge in screens.TRANSITIONS:
            neighbours.setdefault(edge.frm, set()).add(edge.to)
            neighbours.setdefault(edge.to, set()).add(edge.frm)
        seen = {Screen.NOT_IN_GAME}
        frontier = [Screen.NOT_IN_GAME]
        while frontier:
            current = frontier.pop()
            for target in neighbours.get(current, ()):
                if target not in seen:
                    seen.add(target)
                    frontier.append(target)
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                self.assertIn(screen, seen, f"{screen} is an orphan")

    def test_no_screen_is_a_dead_end(self) -> None:
        """Every screen has a way out (or is a documented terminal)."""
        sources = {edge.frm for edge in screens.TRANSITIONS}
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                self.assertIn(screen, sources, f"{screen} has no outgoing edge")

    def test_every_action_is_implemented(self) -> None:
        """The table may only name actions the machine can run."""
        for action in screens.KNOWN_ACTIONS:
            with self.subTest(action=action):
                self.assertTrue(hasattr(Machine, f"_do_{action}"),
                                f"Machine has no _do_{action}")
        for edge in screens.TRANSITIONS:
            with self.subTest(edge=f"{edge.frm}->{edge.to}"):
                self.assertIn(edge.action, screens.KNOWN_ACTIONS)

    def test_the_edges_are_unique(self) -> None:
        """No two edges leave a screen for the same target with the same action."""
        seen: set[tuple[Screen, Screen, str]] = set()
        for edge in screens.TRANSITIONS:
            key = (edge.frm, edge.to, edge.action)
            self.assertNotIn(key, seen)
            seen.add(key)


class TestDecisionTable(unittest.TestCase):
    """The deduction itself — the pure half of the machine."""

    def test_the_deduction_is_a_pure_function_of_its_inputs(self) -> None:
        """The same snapshot and events always give the same screen."""
        raw = snapshot(rules={"bomb_planted": True})
        first = screens.deduce(raw, ["round_start"])
        second = screens.deduce(raw, ["round_start"])
        self.assertIs(first.screen, second.screen)
        self.assertEqual(first.as_dict()["screen"], second.as_dict()["screen"])

    def test_the_loading_hint_is_the_only_way_to_see_a_load(self) -> None:
        """`LOADING` needs the machine's knowledge — the payload has no flag."""
        self.assertIs(screens.deduce(blank()).screen, Screen.NOT_READY)
        self.assertIs(screens.deduce(blank(), loading=True).screen, Screen.LOADING)

    def test_the_capability_resolution_explains_itself(self) -> None:
        """`why()` names the reason — the machine's error messages rely on it."""
        state = screens.deduce(snapshot(local={"alive": False, "life_state": 2}))
        self.assertTrue(state.why(Can.MOVE).startswith("blocked:"))
        self.assertIn("dead", state.why(Can.MOVE))
        self.assertTrue(state.why(Can.SCORE).startswith("ok:"))

    def test_require_names_the_screen_and_the_reason(self) -> None:
        state = screens.deduce(snapshot(local={"team": 0, "alive": False}))
        with self.assertRaises(SessionError) as raised:
            state.require(Can.MOVE)
        message = str(raised.exception)
        self.assertIn("team-select", message)
        self.assertIn("move", message)


class TestMachineActions(unittest.TestCase):
    """The actions — driven against a fake channel."""

    def test_the_reconnect_action_only_fires_when_disconnected(self) -> None:
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        self.assertFalse(machine._do_reconnect())
        self.assertEqual(game.execs, [])

        machine, game = machine_for(
            Frame(snapshot(), ("round_start", "client_disconnect")))
        machine.observe()
        self.assertTrue(machine._do_reconnect())
        self.assertEqual(game.execs, ["reconnect"])

    def test_the_resume_action_only_fires_when_paused(self) -> None:
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        self.assertFalse(machine._do_resume_match())
        self.assertEqual(game.execs, [])

        machine, game = machine_for(Frame(snapshot(rules={"game_paused": True})))
        machine.observe()
        self.assertTrue(machine._do_resume_match())
        self.assertEqual(game.execs, ["unpausematch"])

    def test_the_join_team_action_only_fires_without_a_team(self) -> None:
        machine, game = machine_for(Frame(snapshot(local={"team": 2})))
        machine.observe()
        self.assertFalse(machine._do_join_team(2))
        self.assertEqual(game.execs, [])

        machine, game = machine_for(Frame(snapshot(local={"team": 0})))
        machine.observe()
        self.assertTrue(machine._do_join_team(2))
        self.assertIn("jointeam 2", game.execs)

    def test_the_end_warmup_action_only_fires_in_the_warmup(self) -> None:
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        self.assertFalse(machine._do_end_warmup())
        self.assertEqual(game.execs, [])

        machine, game = machine_for(Frame(snapshot(rules={"warmup": True})))
        machine.observe()
        self.assertTrue(machine._do_end_warmup())
        self.assertIn("mp_warmup_end", game.execs)

    def test_the_load_map_action_only_fires_without_a_level(self) -> None:
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        self.assertFalse(machine._do_load_map(timeout=1.0, load_budget=1.0))
        self.assertEqual(game.execs, [])

    def test_the_waiting_actions_never_touch_the_game(self) -> None:
        """A wait is a wait: no console line, no input."""
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        machine._do_wait_load(timeout=0.1)
        machine._do_wait_bomb_planted(timeout=0.1)
        machine._do_wait_defuse(timeout=0.1)
        machine._do_wait_defused(timeout=0.1)
        machine._do_wait_buy_time(timeout=0.1)
        machine._do_wait_buy_end(timeout=0.1)
        machine._do_wait_halftime(timeout=0.1)
        machine._do_wait_overtime(timeout=0.1)
        machine._do_wait_pause(timeout=0.1)
        self.assertEqual(game.execs, [])

    def test_the_bomb_planted_wait_uses_the_game_event(self) -> None:
        machine, game = machine_for(Frame(snapshot()))
        machine.observe()
        self.assertTrue(machine._do_wait_bomb_planted(timeout=1.0))
        self.assertEqual(game.waits[-1]["event"], "bomb_planted")

    def test_the_round_start_wait_covers_the_post_plant_restart(self) -> None:
        """The same action serves the round end and the post-plant restart."""
        machine, game = machine_for(Frame(snapshot(rules={"bomb_planted": True})))
        machine.observe()
        self.assertTrue(machine._do_wait_round_start(timeout=1.0))
        self.assertEqual(game.waits[-1]["event"], "round_start")


class TestMachineDrive(unittest.TestCase):
    """`Machine.ensure` — the drive, against a fake channel."""

    def test_ensure_returns_the_target_screen(self) -> None:
        machine, _ = machine_for(Frame(snapshot()))
        state = machine.ensure(Screen.LIVE, timeout=2.0, pause_bots=False)
        self.assertIs(state.screen, Screen.LIVE)

    def test_ensure_accepts_the_buy_time_screen_for_a_live_target(self) -> None:
        """A running round with an open buy window *is* live.

        The window may never close (``mp_buytime 9999`` in the playground
        convars), so demanding the exact `LIVE` screen would time out there.
        """
        machine, _ = machine_for(Frame(snapshot(rules={"t_cant_buy": False,
                                                       "ct_cant_buy": False})))
        state = machine.ensure(Screen.LIVE, timeout=2.0, pause_bots=False)
        self.assertIs(state.screen, Screen.BUY_TIME)

    def test_ensure_reconnects_when_the_server_is_gone(self) -> None:
        machine, game = machine_for(
            Frame(snapshot(), ("round_start", "client_disconnect")),
            Frame(snapshot()))
        state = machine.ensure(Screen.LIVE, timeout=2.0, pause_bots=False)
        self.assertIn("reconnect", game.execs)
        self.assertIs(state.screen, Screen.LIVE)

    def test_the_machine_does_not_re_issue_the_map_during_a_load(self) -> None:
        """Re-issuing ``map`` during a load restarts it (the 2026-10-15 livelock).

        The load is the machine's own knowledge: while the screen is LOADING the
        machine waits for the level — it does not read the main screen and ask
        for another map.
        """
        machine, game = machine_for(Frame(blank()))
        machine._loading = True
        machine.observe()
        self.assertIs(machine.state.screen, Screen.LOADING)
        # The action of the LOADING screen waits; it never asks for a map.
        machine._do_wait_load(timeout=0.2)
        self.assertEqual(game.execs, [])

    def test_the_machine_reports_the_loading_screen_while_a_map_is_in_flight(self) -> None:
        """The machine knows it asked for a map — the payload does not say so."""
        machine, _ = machine_for(Frame(blank()))
        machine._loading = True
        state = machine.observe()
        self.assertIs(state.screen, Screen.LOADING)
        self.assertTrue(machine._loading)

    def test_the_machine_forgets_the_load_when_the_level_is_up(self) -> None:
        machine, _ = machine_for(Frame(snapshot()))
        machine._loading = True
        state = machine.observe()
        self.assertIs(state.screen, Screen.LIVE)
        self.assertFalse(machine._loading)


class TestTransport(unittest.TestCase):
    """The façade the higher layers program against."""

    def test_the_game_facade_satisfies_the_transport_protocol(self) -> None:
        """`Game` is what `screens.Machine` and `awaiting` program against."""
        game = Game(control=FakeGame())  # type: ignore[arg-type]
        self.assertIsInstance(game, ControlTransport)

    def test_the_facade_keeps_the_import_cycle_broken(self) -> None:
        """`session` must not import `screens` at module level."""
        import scenario.session as session

        self.assertNotIn("screens", vars(session))


if __name__ == "__main__":
    unittest.main(verbosity=2)
