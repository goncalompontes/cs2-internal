"""Unit tests for the screen state machine (`scripts/scenario/screens.py`).

The deduction and the capability table are the parts that can be wrong *without*
the game running, and they are what every scenario now depends on — so they are
tested here with synthetic payload snapshots, no CS2 required:

    python -m unittest discover -s scripts/scenario/tests
    # or
    python scripts/scenario/tests/test_screens.py
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from dataclasses import dataclass
from typing import Any, Iterable, Mapping, cast

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import screens  # noqa: E402
from scenario.protocol import StateSnapshotDict, validate_snapshot  # noqa: E402
from scenario.screens import Can, Screen  # noqa: E402


def player(**overrides: Any) -> dict[str, Any]:
    """One player block, as the payload writes it.

    Every key the payload writes is present: the snapshot contract says the
    block is never partial, so a fixture that omitted one would be a fixture
    the payload could never produce.
    """
    raw: dict[str, Any] = {
        "index": 0,
        "name": "player",
        "weapon": "weapon_ak47",
        "team": 2,
        "connected": 0,
        "health": 100,
        "max_health": 100,
        "armor": 0,
        "life_state": 0,
        "on_ground": True,
        "flags": 1,
        "clip": 30,
        "reserve": 90,
        "ping": 10,
        "money": 800,
        "flash": 0.0,
        "alive": True,
        "is_local": True,
        "helmet": False,
        "defuser": False,
        "scoped": False,
        "defusing": False,
        "in_buy_zone": True,
        "has_bomb": False,
        "origin": {"x": 1.0, "y": 2.0, "z": 3.0},
        "eye_origin": {"x": 1.0, "y": 2.5, "z": 3.0},
        "velocity": {"x": 0.0, "y": 0.0, "z": 0.0},
        "eye_angles": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0},
        "distance": 0.0,
        "simulation_time": 30.0,
        "tick_base": 1000,
    }
    raw.update(overrides)
    return raw


def snapshot(**overrides: Any) -> StateSnapshotDict:
    """A payload `state` snapshot, live and playable unless overridden.

    The default is a **plain live round**: the buy window is *closed* (the
    window being open is its own screen now — `Screen.BUY_TIME`), the bomb is
    not down and the match is in the first half.

    The fixture is a **complete** wire snapshot — every key the payload writes
    is present — and it is *validated* on the way out, so a fixture can never
    drift away from the contract the payload answers with.
    """
    raw: dict[str, Any] = {
        "valid": True,
        "in_map": True,
        "local_pawn": 0x1234,
        "level": {
            "up": True,
            "proxy": "0x1",
            "rules_global": True,
            "identities": 726,
            "local_connected": 0,
        },
        "local": player(),
        "rules": {
            "loaded": True,
            "match_started": True,
            "warmup": False,
            "freeze": False,
            "game_restart": False,
            "bomb_planted": False,
            "bomb_dropped": False,
            # The buy window is closed in a plain live round.
            "t_cant_buy": True,
            "ct_cant_buy": True,
            "game_phase": 2,
            "round_time": 115,
            "freeze_time": 0,
            "round_win_status": 0,
            "round_win_reason": 0,
            "total_rounds_played": 0,
            "match_start_time": 30.0,
            "round_start_time": 12.0,
            "restart_round_time": 0.0,
            "game_start_time": 0.0,
            "warmup_period_start": 0.0,
            "time_until_next_phase": 0.0,
            # The two fields the payload does not send yet (see
            # `screens.PENDING_PAYLOAD_FIELDS`).
            "overtime_playing": 0,
            "game_paused": False,
        },
        "players": [player(index=1, team=2, is_local=False),
                    player(index=2, team=3, is_local=False)],
    }
    for key, value in overrides.items():
        if key in raw and isinstance(raw[key], dict) and isinstance(value, dict):
            raw[key] = {**raw[key], **value}
        else:
            raw[key] = value
    return validate_snapshot(raw)


@dataclass(frozen=True)
class Fixture:
    """One representative observation of a screen.

    ``loading`` is the machine's own knowledge ("a map I asked for is still
    coming") and ``events`` the tail of the game's event stream — the two
    inputs the deduction takes besides the snapshot.
    """

    raw: StateSnapshotDict
    loading: bool = False
    events: tuple[str, ...] = ()


def blank() -> StateSnapshotDict:
    """A snapshot with nothing readable in it.

    The engine booting after an injection, or the level teardown: no local
    player, no readable rules, nothing to deduce from.
    """
    return snapshot(valid=False, in_map=False, local=None, players=[],
                    rules={"loaded": False, "match_started": False})


def deduce_raw(raw: Mapping[str, Any], events: Iterable[str] = (),
               *, loading: bool = False) -> screens.GameState:
    """Deduce a screen from a raw snapshot.

    The deduction takes the *validated* wire shape; this is the one place
    where a deliberately damaged snapshot (a missing probe) enters it.
    """
    return screens.deduce(cast(StateSnapshotDict, raw), events, loading=loading)


def drop(raw: StateSnapshotDict, block: str, key: str) -> None:
    """Remove a key from one block of a snapshot, in place.

    The *pending* fields (the ones the payload does not send yet) are not part
    of the wire contract, so they cannot be removed through the typed shape —
    at runtime the block is a plain dict.
    """
    cast(dict[str, Any], raw)[block].pop(key, None)


#: One representative fixture per screen (the deduction table, as data).
FIXTURES: dict[Screen, Fixture] = {
    Screen.NOT_READY: Fixture(blank()),
    # The main menu: `valid`/`in_map` true, a placeholder local at (0,0,0) with no
    # team, and — measured live 2026-10-15 — the `game_rules` global *readable*
    # (a default, unstarted object). The level probe is what says "main screen".
    Screen.NOT_IN_GAME: Fixture(snapshot(
        in_game=False, level={"up": False},
        rules={"loaded": True, "warmup": True, "freeze": True,
               "match_started": False},
        local={"team": 0, "alive": False, "life_state": 2, "weapon": "",
               "origin": {"x": 0.0, "y": 0.0, "z": 0.0}})),
    # A map load in flight: the teardown cleared the local state, so the only
    # thing that says "loading" is the machine's own knowledge.
    Screen.LOADING: Fixture(blank(), loading=True),
    # The client lost the server: the event is *after* the last round start.
    Screen.DISCONNECTED: Fixture(snapshot(),
                                 events=("round_start", "round_freeze_end",
                                         "client_disconnect")),
    Screen.TEAM_SELECT: Fixture(snapshot(local={"team": 0, "alive": False})),
    Screen.SPECTATOR: Fixture(snapshot(local={"team": 1, "alive": False})),
    Screen.WARMUP: Fixture(snapshot(rules={"warmup": True, "match_started": False})),
    Screen.DEAD: Fixture(snapshot(local={"alive": False, "life_state": 2})),
    # The buy window is open during the freeze — that is what makes the row's
    # `buy` capability true here.
    Screen.FREEZE: Fixture(snapshot(rules={"freeze": True, "t_cant_buy": False,
                                           "ct_cant_buy": False})),
    # `m_gamePhase == 4` is the halftime; the round winner of the last round is
    # still set (the match fact outranks the stale round fact).
    Screen.HALF_TIME: Fixture(snapshot(rules={"game_phase": 4,
                                               "round_win_status": 2})),
    Screen.OVERTIME: Fixture(snapshot(rules={"overtime_playing": 1})),
    Screen.PAUSED: Fixture(snapshot(rules={"game_paused": True})),
    Screen.ROUND_END: Fixture(snapshot(rules={"round_win_status": 2,
                                              "round_win_reason": 9})),
    # The local player is defusing: the bomb is down *and* the defuse runs.
    Screen.DEFUSING: Fixture(snapshot(local={"defusing": True},
                                      rules={"bomb_planted": True})),
    # A CT with a defuser and the bomb down.
    Screen.PLANTED: Fixture(snapshot(local={"team": 3, "defuser": True},
                                     rules={"bomb_planted": True})),
    # The round runs and the buy window is still open.
    Screen.BUY_TIME: Fixture(snapshot(rules={"t_cant_buy": False,
                                             "ct_cant_buy": False})),
    Screen.LIVE: Fixture(snapshot()),
}


def deduce(screen: Screen) -> screens.GameState:
    """Deduce the screen of a fixture (the inputs the deduction takes)."""
    fixture = FIXTURES[screen]
    return screens.deduce(fixture.raw, fixture.events, loading=fixture.loading)


class TestDeduction(unittest.TestCase):
    def test_main_screen_rule(self) -> None:
        """The main screen is `in_game == false` even with readable rules.

        Measured live (2026-10-15): at the main menu the `game_rules` global is
        readable (a default unstarted object, `warmup = freeze = true`), a
        `C_CSGameRulesProxy` entity exists and the world has hundreds of entities —
        so none of those is the rule. The local player is what differs: the menu
        placeholder is at `(0,0,0)` with no team and no weapon.
        """
        menu = FIXTURES[Screen.NOT_IN_GAME]
        state = screens.deduce(menu.raw, menu.events)
        self.assertIs(state.screen, Screen.NOT_IN_GAME)
        self.assertFalse(state.in_game)
        self.assertTrue(state.rules_loaded)  # the fact is still reported
        self.assertIn("game_rules global readable", " ".join(state.notes))

    def test_main_screen_with_a_running_match_but_no_team(self) -> None:
        """Team-select in a live match is *not* the main screen.

        The local has no team and may sit at `(0,0,0)`, but the match is running —
        that is the corroboration the rule uses.
        """
        select = snapshot(local={"team": 0, "alive": False,
                                 "origin": {"x": 0.0, "y": 0.0, "z": 0.0}},
                          rules={"match_started": True, "game_start_time": 30.0})
        self.assertIs(screens.deduce(select).screen, Screen.TEAM_SELECT)
        self.assertTrue(screens.deduce(select).in_game)

    def test_event_history_is_evidence_of_a_level(self) -> None:
        """Any game event means a level ran, even with menu-looking fields."""
        raw = snapshot(local={"team": 0, "alive": False,
                              "origin": {"x": 0.0, "y": 0.0, "z": 0.0}},
                       rules={"match_started": False, "game_start_time": 0.0})
        self.assertIs(screens.deduce(raw, ["round_start"]).screen, Screen.TEAM_SELECT)
        self.assertIs(screens.deduce(raw, []).screen, Screen.NOT_IN_GAME)

    def test_fallback_without_the_payload_probe(self) -> None:
        """Without the level block the local player still decides."""
        raw = dict(FIXTURES[Screen.NOT_IN_GAME].raw)
        del raw["level"]
        self.assertIs(deduce_raw(raw).screen, Screen.NOT_IN_GAME)
        raw = dict(FIXTURES[Screen.LIVE].raw)
        del raw["level"]
        self.assertIs(deduce_raw(raw).screen, Screen.LIVE)

    def test_each_screen_is_recognised(self) -> None:
        for expected in Screen:
            with self.subTest(screen=str(expected)):
                self.assertIs(deduce(expected).screen, expected)

    def test_round_end_from_events_only(self) -> None:
        """The event fallback works when the rules cannot be read."""
        state = screens.deduce(snapshot(), ["round_start", "round_freeze_end", "round_end"])
        self.assertIs(state.screen, Screen.ROUND_END)
        self.assertEqual(state.rules.get("round_win_status"), 0)

    def test_fresh_round_beats_an_old_round_end(self) -> None:
        state = screens.deduce(snapshot(), ["round_end", "round_start", "round_freeze_end"])
        self.assertIs(state.screen, Screen.LIVE)

    def test_dead_beats_freeze_and_round_end(self) -> None:
        """A dead player ignores input whatever the phase, so report DEAD."""
        raw = snapshot(local={"alive": False, "life_state": 2},
                       rules={"freeze": True, "round_win_status": 2})
        self.assertIs(screens.deduce(raw).screen, Screen.DEAD)

    # --- the new screens --------------------------------------------------
    def test_loading_needs_the_machines_own_knowledge(self) -> None:
        """`LOADING` is the machine's knowledge, not a payload flag.

        The payload has no "loading" flag: the map command clears the local state
        and the level comes up with the local not spawned yet, so the snapshot
        alone cannot tell a load from the main menu. Without the hint the same
        snapshot is `NOT_READY`.
        """
        fixture = FIXTURES[Screen.LOADING]
        self.assertIs(screens.deduce(fixture.raw).screen, Screen.NOT_READY)
        self.assertIs(screens.deduce(fixture.raw, loading=True).screen,
                      Screen.LOADING)

    def test_loading_is_not_the_main_screen(self) -> None:
        """A load in flight is not "the client is at the main menu".

        The machine must not read the main screen while it waits for the map it
        asked for — it would issue another `map` command and restart the load
        (observed live 2026-10-15).
        """
        menu = FIXTURES[Screen.NOT_IN_GAME].raw
        self.assertIs(screens.deduce(menu, loading=True).screen, Screen.LOADING)

    def test_disconnected_needs_an_event_after_the_round_start(self) -> None:
        """A stale disconnect (from an earlier round) does not pin the screen."""
        stale = screens.deduce(snapshot(),
                               ["client_disconnect", "round_start", "round_freeze_end"])
        self.assertIs(stale.screen, Screen.LIVE)
        fresh = screens.deduce(snapshot(),
                               ["round_start", "round_freeze_end", "client_disconnect"])
        self.assertIs(fresh.screen, Screen.DISCONNECTED)

    def test_the_buy_window_is_its_own_screen(self) -> None:
        """An open buy window in a running round is `BUY_TIME`, not `LIVE`."""
        raw = snapshot(rules={"t_cant_buy": False, "ct_cant_buy": False})
        self.assertIs(screens.deduce(raw).screen, Screen.BUY_TIME)
        closed = snapshot(rules={"t_cant_buy": True, "ct_cant_buy": True})
        self.assertIs(screens.deduce(closed).screen, Screen.LIVE)

    def test_the_buy_window_is_team_specific(self) -> None:
        """The T window is closed but the CT window is open — each team its own."""
        ct = snapshot(rules={"t_cant_buy": True, "ct_cant_buy": False},
                      local={"team": 3})
        self.assertIs(screens.deduce(ct).screen, Screen.BUY_TIME)
        t = snapshot(rules={"t_cant_buy": True, "ct_cant_buy": False},
                     local={"team": 2})
        self.assertIs(screens.deduce(t).screen, Screen.LIVE)

    def test_the_bomb_down_is_a_planted_round(self) -> None:
        raw = snapshot(rules={"bomb_planted": True})
        state = screens.deduce(raw)
        self.assertIs(state.screen, Screen.PLANTED)
        self.assertTrue(state.bomb_planted)

    def test_defusing_is_more_specific_than_planted(self) -> None:
        raw = snapshot(local={"defusing": True}, rules={"bomb_planted": True})
        state = screens.deduce(raw)
        self.assertIs(state.screen, Screen.DEFUSING)
        self.assertTrue(state.defusing)

    def test_the_halftime_is_the_game_phase_four(self) -> None:
        """`m_gamePhase == 4` — confirmed against the shipped client.dll."""
        state = screens.deduce(snapshot(rules={"game_phase": 4}))
        self.assertIs(state.screen, Screen.HALF_TIME)

    def test_the_overtime_screen_needs_the_payload_field(self) -> None:
        """`OVERTIME` reads `overtime_playing` — a field the payload lacks today."""
        raw = snapshot(rules={"overtime_playing": 1})
        self.assertIs(screens.deduce(raw).screen, Screen.OVERTIME)
        # Without the field the screen is never returned (the rules block has no
        # such key, so the test is false) — the screen is dark, not faked.
        without = snapshot()
        drop(without, "rules", "overtime_playing")
        self.assertIs(screens.deduce(without).screen, Screen.LIVE)
        self.assertIn("overtime_playing", screens.PENDING_PAYLOAD_FIELDS)

    def test_the_paused_screen_needs_the_payload_field(self) -> None:
        """`PAUSED` reads `game_paused` — a field the payload lacks today."""
        raw = snapshot(rules={"game_paused": True})
        self.assertIs(screens.deduce(raw).screen, Screen.PAUSED)
        without = snapshot()
        drop(without, "rules", "game_paused")
        self.assertIs(screens.deduce(without).screen, Screen.LIVE)
        self.assertIn("game_paused", screens.PENDING_PAYLOAD_FIELDS)


class TestDecisionOrder(unittest.TestCase):
    """The order the deduction applies the rules in — it *is* the model."""

    def test_the_enum_order_is_the_decision_order(self) -> None:
        """The `Screen` enum is declared in the deduction order."""
        self.assertEqual(
            [str(screen) for screen in Screen],
            ["loading", "not-ready", "not-in-game", "disconnected", "team-select",
             "spectator", "warmup", "dead", "freeze", "half-time", "overtime",
             "paused", "round-end", "defusing", "planted", "buy-time", "live"])

    def test_a_dead_player_beats_the_planted_bomb(self) -> None:
        """DEAD (the player fact) outranks PLANTED (the round fact).

        A dead player ignores every input, so "the bomb is down" is not what a
        scenario needs to know first — and the bomb is still reported as a fact.
        """
        raw = snapshot(local={"alive": False, "life_state": 2},
                       rules={"bomb_planted": True})
        state = screens.deduce(raw)
        self.assertIs(state.screen, Screen.DEAD)
        self.assertTrue(state.bomb_planted)  # the fact survives

    def test_the_halftime_beats_a_stale_round_winner(self) -> None:
        """The match phase outranks the round phase.

        `m_iRoundWinStatus` is not reset between the halves, so at the halftime
        the last round's winner is still set.
        """
        raw = snapshot(rules={"game_phase": 4, "round_win_status": 2})
        self.assertIs(screens.deduce(raw).screen, Screen.HALF_TIME)

    def test_the_round_end_beats_the_planted_bomb(self) -> None:
        """The bomb exploded — the round is over, not "planted"."""
        raw = snapshot(rules={"bomb_planted": True, "round_win_status": 2})
        self.assertIs(screens.deduce(raw).screen, Screen.ROUND_END)

    def test_the_freeze_beats_the_buy_window(self) -> None:
        """The freeze is its own screen, even with the buy window open."""
        raw = snapshot(rules={"freeze": True, "t_cant_buy": False,
                              "ct_cant_buy": False})
        self.assertIs(screens.deduce(raw).screen, Screen.FREEZE)

    def test_the_warmup_beats_the_buy_window(self) -> None:
        raw = snapshot(rules={"warmup": True, "t_cant_buy": False,
                              "ct_cant_buy": False})
        self.assertIs(screens.deduce(raw).screen, Screen.WARMUP)


class TestCapabilities(unittest.TestCase):
    def test_table_matches_the_deduction(self) -> None:
        """`GameState.can` must agree with the `CAPABILITIES` table.

        Three capabilities carry a runtime fact on top of the row: `buy` needs
        the buy window, `plant` needs the C4 (and a bomb that is not down) and
        `defuse` needs a planted bomb and a defuser.
        """
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                state = deduce(screen)
                self.assertIs(state.screen, screen)
                for cap in Can:
                    with self.subTest(capability=str(cap)):
                        expected = cap in screens.CAPABILITIES[screen]
                        if cap is Can.BUY:
                            expected = expected and state.buy_time
                        elif cap is Can.PLANT:
                            expected = expected and state.can_plant
                        elif cap is Can.DEFUSE:
                            expected = expected and state.can_defuse
                        self.assertEqual(state.can(cap), expected, msg=state.why(cap))

    def test_dead_and_freeze_refuse_input(self) -> None:
        dead = deduce(Screen.DEAD)
        self.assertFalse(dead.can(Can.MOVE))
        self.assertFalse(dead.can(Can.ATTACK))
        self.assertFalse(dead.can(Can.LOOK))
        freeze = deduce(Screen.FREEZE)
        self.assertFalse(freeze.can(Can.MOVE))
        self.assertIn("freeze", freeze.why(Can.MOVE))

    def test_buy_needs_the_window(self) -> None:
        """The buy window gates the capability (and is a screen of its own)."""
        open_window = deduce(Screen.BUY_TIME)
        self.assertTrue(open_window.can(Can.BUY))
        # T buy window closed, CT still open: the T player cannot buy.
        closed = screens.deduce(snapshot(rules={"t_cant_buy": True,
                                                "ct_cant_buy": True}))
        self.assertFalse(closed.can(Can.BUY))
        self.assertIn("does not allow", closed.why(Can.BUY))
        # ... and the same snapshot as a CT player still can.
        ct = screens.deduce(snapshot(rules={"t_cant_buy": True,
                                             "ct_cant_buy": False},
                                     local={"team": 3}))
        self.assertTrue(ct.can(Can.BUY))
        # A closed window *inside* a screen that permits buying is refused with
        # the reason that names the flag.
        freeze = screens.deduce(snapshot(rules={"freeze": True,
                                                "t_cant_buy": True,
                                                "ct_cant_buy": True}))
        self.assertFalse(freeze.can(Can.BUY))
        self.assertIn("t_cant_buy", freeze.why(Can.BUY))

    def test_require_raises_with_the_reason(self) -> None:
        state = deduce(Screen.TEAM_SELECT)
        with self.assertRaises(Exception) as raised:
            state.require(Can.MOVE)
        self.assertIn("team-select", str(raised.exception))

    # --- the content of the table -----------------------------------------
    def test_the_table_says_what_the_game_allows(self) -> None:
        """The table is not merely self-consistent — it says the truth.

        This is the assertion the scenario relies on: a scenario that says
        "the game is live, so I may move and shoot" must be right, and one that
        says "the bomb is planted" must not be told it may plant again.
        """
        live = deduce(Screen.LIVE)
        self.assertTrue(live.can(Can.MOVE))
        self.assertTrue(live.can(Can.ATTACK))
        self.assertTrue(live.can(Can.JUMP))
        self.assertTrue(live.can(Can.RELOAD))
        self.assertTrue(live.can(Can.DROP))
        self.assertTrue(live.can(Can.INSPECT))
        self.assertTrue(live.can(Can.SWITCH_WEAPON))
        self.assertTrue(live.can(Can.GRENADE))

        dead = deduce(Screen.DEAD)
        self.assertEqual({cap for cap in Can if dead.can(cap)},
                         {Can.STATE, Can.SCORE})

        planted = deduce(Screen.PLANTED)
        # The players move in a planted round — the round is not over.
        self.assertTrue(planted.can(Can.MOVE))
        # A CT with a defuser may defuse ...
        self.assertTrue(planted.can(Can.DEFUSE))
        # ... but the bomb is already down, so nobody may plant it again, and
        # the buy window closed when the bomb went down.
        self.assertFalse(planted.can(Can.PLANT))
        self.assertFalse(planted.can(Can.BUY))

    def test_planting_needs_the_c4(self) -> None:
        """`PLANT` needs the bomb in hand and a bomb that is not down."""
        without = screens.deduce(snapshot(local={"has_bomb": False}))
        self.assertFalse(without.can(Can.PLANT))
        self.assertIn("does not carry the C4", without.why(Can.PLANT))
        with_c4 = screens.deduce(snapshot(local={"has_bomb": True}))
        self.assertTrue(with_c4.can(Can.PLANT))
        # The bomb is down: the planted screen does not allow planting again.
        planted = screens.deduce(snapshot(local={"has_bomb": True},
                                          rules={"bomb_planted": True}))
        self.assertFalse(planted.can_plant)
        self.assertFalse(planted.can(Can.PLANT))

    def test_defusing_needs_a_defuser(self) -> None:
        """`DEFUSE` needs a planted bomb, a defuser and no running defuse."""
        no_defuser = screens.deduce(snapshot(local={"team": 3, "defuser": False},
                                             rules={"bomb_planted": True}))
        self.assertFalse(no_defuser.can(Can.DEFUSE))
        self.assertIn("no defuser", no_defuser.why(Can.DEFUSE))
        defusing = screens.deduce(snapshot(local={"team": 3, "defuser": True,
                                                   "defusing": True},
                                            rules={"bomb_planted": True}))
        self.assertFalse(defusing.can_defuse)

    def test_score_and_state_are_readable_everywhere(self) -> None:
        """The read-only capabilities are available in every screen."""
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                state = deduce(screen)
                self.assertTrue(state.can(Can.STATE))
                self.assertTrue(state.can(Can.SCORE))
                for cap in screens.READ_ONLY:
                    self.assertIn(cap, screens.CAPABILITIES[screen])

    def test_reconnect_is_only_offered_where_it_makes_sense(self) -> None:
        self.assertTrue(deduce(Screen.DISCONNECTED).can(Can.RECONNECT))
        self.assertTrue(deduce(Screen.NOT_IN_GAME).can(Can.RECONNECT))
        self.assertFalse(deduce(Screen.LIVE).can(Can.RECONNECT))
        self.assertFalse(deduce(Screen.NOT_READY).can(Can.RECONNECT))


class TestTransitions(unittest.TestCase):
    #: Every action `Machine.ensure` implements; the table may only name these.
    KNOWN_ACTIONS = {"wait", "load_map", "join_team", "end_warmup", "restart_round",
                     "wait_freeze_end", "wait_round_start", "wait_respawn",
                     "wait_load", "wait_bomb_planted", "wait_defuse",
                     "wait_defused", "wait_buy_time", "wait_buy_end",
                     "wait_halftime", "wait_overtime", "wait_pause",
                     "reconnect", "resume_match"}

    def test_transition_actions_exist(self) -> None:
        for edge in screens.TRANSITIONS:
            with self.subTest(edge=f"{edge.frm}->{edge.to}"):
                self.assertIn(edge.action, self.KNOWN_ACTIONS)

    def test_every_screen_is_reachable_or_terminal(self) -> None:
        """Every screen is either a transition source or reachable as a target."""
        sources = {edge.frm for edge in screens.TRANSITIONS}
        targets = {edge.to for edge in screens.TRANSITIONS}
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                self.assertTrue(screen in sources or screen in targets,
                                f"{screen} is unreachable")

    def test_ensure_targets_are_consistent(self) -> None:
        self.assertIn(Screen.LIVE, screens.TARGETS)
        for target in screens.TARGETS:
            self.assertIn(target, Screen)


if __name__ == "__main__":
    unittest.main(verbosity=2)
