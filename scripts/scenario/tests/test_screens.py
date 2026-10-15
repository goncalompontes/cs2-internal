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

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import screens  # noqa: E402
from scenario.screens import Can, Screen  # noqa: E402


def snapshot(**overrides: object) -> dict:
    """A payload `state` snapshot, live and playable unless overridden."""
    raw: dict = {
        "valid": True,
        # `in_game` = the payload's level probe (the level's rules-proxy entity).
        "in_game": True,
        "in_map": True,
        "local_pawn": 0x1234,
        "level": {"up": True, "proxy": "0x1", "rules_global": True},
        "local": {
            "team": 2,
            "alive": True,
            "life_state": 0,
            "on_ground": True,
            "origin": {"x": 1.0, "y": 2.0, "z": 3.0},
            "weapon": "weapon_ak47",
        },
        "rules": {
            "loaded": True,
            "match_started": True,
            "warmup": False,
            "freeze": False,
            "round_win_status": 0,
            "round_win_reason": 0,
            "game_phase": 2,
            "round_time": 115,
            "freeze_time": 0,
            "t_cant_buy": False,
            "ct_cant_buy": False,
        },
        "players": [{"team": 2}, {"team": 3}],
    }
    for key, value in overrides.items():
        if key in raw and isinstance(raw[key], dict) and isinstance(value, dict):
            raw[key] = {**raw[key], **value}
        else:
            raw[key] = value
    return raw


#: One representative snapshot per screen (the deduction table, as data).
FIXTURES: dict[Screen, dict] = {
    Screen.NOT_READY: snapshot(valid=False, in_game=False, in_map=False, local={},
                               rules={}, players=[]),
    # The main menu: `valid`/`in_map` true, a placeholder local at (0,0,0) with no
    # team, and — measured live 2026-10-15 — the `game_rules` global *readable*
    # (a default, unstarted object). The level probe is what says "main screen".
    Screen.NOT_IN_GAME: snapshot(in_game=False, level={"up": False},
                                 rules={"loaded": True, "warmup": True, "freeze": True,
                                        "match_started": False},
                                 local={"team": 0, "alive": False, "life_state": 2,
                                        "weapon": "", "origin": {"x": 0.0, "y": 0.0,
                                                                 "z": 0.0}}),
    Screen.TEAM_SELECT: snapshot(local={"team": 0, "alive": False}),
    Screen.SPECTATOR: snapshot(local={"team": 1, "alive": False}),
    Screen.WARMUP: snapshot(rules={"warmup": True, "match_started": False}),
    Screen.FREEZE: snapshot(rules={"freeze": True}),
    Screen.LIVE: snapshot(),
    Screen.DEAD: snapshot(local={"alive": False, "life_state": 2}),
    Screen.ROUND_END: snapshot(rules={"round_win_status": 2, "round_win_reason": 9}),
}


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
        state = screens.deduce(menu)
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
        raw = FIXTURES[Screen.NOT_IN_GAME]
        del raw["level"]
        self.assertIs(screens.deduce(raw).screen, Screen.NOT_IN_GAME)
        raw = FIXTURES[Screen.LIVE]
        del raw["level"]
        self.assertIs(screens.deduce(raw).screen, Screen.LIVE)

    def test_each_screen_is_recognised(self) -> None:
        for expected, raw in FIXTURES.items():
            with self.subTest(screen=str(expected)):
                self.assertIs(screens.deduce(raw).screen, expected)

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


class TestCapabilities(unittest.TestCase):
    def test_table_matches_the_deduction(self) -> None:
        """`GameState.can` must agree with the `CAPABILITIES` table."""
        for screen, raw in FIXTURES.items():
            state = screens.deduce(raw)
            self.assertIs(state.screen, screen)
            for cap in Can:
                with self.subTest(screen=str(screen), capability=str(cap)):
                    expected = cap in screens.CAPABILITIES[screen]
                    # `buy` additionally needs the buy window, which the fixtures
                    # leave open on purpose.
                    self.assertEqual(state.can(cap), expected, msg=state.why(cap))

    def test_dead_and_freeze_refuse_input(self) -> None:
        dead = screens.deduce(FIXTURES[Screen.DEAD])
        self.assertFalse(dead.can(Can.MOVE))
        self.assertFalse(dead.can(Can.ATTACK))
        self.assertFalse(dead.can(Can.LOOK))
        freeze = screens.deduce(FIXTURES[Screen.FREEZE])
        self.assertFalse(freeze.can(Can.MOVE))
        self.assertIn("freeze", freeze.why(Can.MOVE))

    def test_buy_needs_the_window(self) -> None:
        live = screens.deduce(snapshot())
        self.assertTrue(live.can(Can.BUY))
        # T buy window closed, CT still open: the T player cannot buy.
        closed = screens.deduce(snapshot(rules={"t_cant_buy": True}))
        self.assertFalse(closed.can(Can.BUY))
        self.assertIn("t_cant_buy", closed.why(Can.BUY))
        # ... and the same snapshot as a CT player still can.
        ct = screens.deduce(snapshot(rules={"t_cant_buy": True},
                                     local={"team": 3}))
        self.assertTrue(ct.can(Can.BUY))

    def test_require_raises_with_the_reason(self) -> None:
        state = screens.deduce(FIXTURES[Screen.TEAM_SELECT])
        with self.assertRaises(Exception) as raised:
            state.require(Can.MOVE)
        self.assertIn("team-select", str(raised.exception))


class TestTransitions(unittest.TestCase):
    #: Every action `Machine.ensure` implements; the table may only name these.
    KNOWN_ACTIONS = {"wait", "load_map", "join_team", "end_warmup", "restart_round",
                     "wait_freeze_end", "wait_round_start", "wait_respawn"}

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
