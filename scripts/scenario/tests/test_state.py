"""Unit tests for the observation's accessors and the capability table.

Two things the state machine is judged by:

* the **accessors** — what the observation says about the game (``valid``,
  ``in_game``, ``buy_time``, ``round_over``, the local player, ...). They
  were never asserted, so a wrong answer (a screen that reads "live" while
  the player is dead) stayed invisible.
* the **content** of the capability table. The existing test only proves that
  ``can()`` agrees with the table — a *wrong but consistent* table passes
  that. The table is the promise the scenarios rely on ("the game is live, so
  I may move"), so it is spelled out here.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import screens  # noqa: E402
from scenario.screens import Can, GameState, Screen  # noqa: E402
from scenario.tests.test_screens import (  # noqa: E402
    FIXTURES,
    deduce,
    snapshot,
)

#: What every screen must allow — the table's *content*, not its shape.
#:
#: The read-only capabilities (``STATE``, ``SCORE``) are in every row: reading
#: the game is never unsafe. Everything else says what the screen *permits*;
#: the runtime facts on top of it (the buy window, the C4, the defuser) are
#: checked by :meth:`GameState.can`, not here.
EXPECTED: dict[Screen, frozenset[Can]] = {
    Screen.NOT_READY: frozenset({Can.STATE, Can.SCORE}),
    Screen.NOT_IN_GAME: frozenset({Can.STATE, Can.SCORE, Can.RECONNECT}),
    Screen.LOADING: frozenset({Can.STATE, Can.SCORE}),
    Screen.DISCONNECTED: frozenset({Can.STATE, Can.SCORE, Can.RECONNECT}),
    Screen.TEAM_SELECT: frozenset({Can.STATE, Can.SCORE}),
    Screen.SPECTATOR: frozenset({Can.STATE, Can.SCORE, Can.LOOK}),
    Screen.WARMUP: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE,
                              Can.JUMP, Can.ATTACK, Can.RELOAD, Can.DROP,
                              Can.INSPECT, Can.SWITCH_WEAPON, Can.GRENADE,
                              Can.TELEPORT}),
    # A dead player ignores every input: only the reads are left.
    Screen.DEAD: frozenset({Can.STATE, Can.SCORE}),
    Screen.FREEZE: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.BUY,
                              Can.TELEPORT}),
    Screen.HALF_TIME: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.TELEPORT}),
    Screen.OVERTIME: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE,
                                Can.JUMP, Can.ATTACK, Can.RELOAD, Can.DROP,
                                Can.INSPECT, Can.SWITCH_WEAPON, Can.GRENADE,
                                Can.TELEPORT}),
    Screen.PAUSED: frozenset({Can.STATE, Can.SCORE, Can.LOOK}),
    Screen.ROUND_END: frozenset({Can.STATE, Can.SCORE}),
    # A defusing player is pinned to the bomb: no dropping, no switching.
    Screen.DEFUSING: frozenset({Can.STATE, Can.SCORE, Can.LOOK}),
    # A planted round is still played — but the bomb is down, so nobody may
    # plant it again.
    Screen.PLANTED: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE,
                                Can.JUMP, Can.ATTACK, Can.RELOAD, Can.DROP,
                                Can.INSPECT, Can.SWITCH_WEAPON, Can.GRENADE,
                                Can.DEFUSE, Can.TELEPORT}),
    Screen.BUY_TIME: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE,
                                Can.JUMP, Can.ATTACK, Can.RELOAD, Can.DROP,
                                Can.INSPECT, Can.SWITCH_WEAPON, Can.GRENADE,
                                Can.BUY, Can.PLANT, Can.TELEPORT}),
    Screen.LIVE: frozenset({Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP,
                            Can.ATTACK, Can.RELOAD, Can.DROP, Can.INSPECT,
                            Can.SWITCH_WEAPON, Can.GRENADE, Can.PLANT,
                            Can.TELEPORT}),
}


class TestAccessors(unittest.TestCase):
    """What the observation says about the game."""

    def test_valid_reads_the_snapshot(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).valid)
        self.assertFalse(deduce(Screen.NOT_READY).valid)

    def test_in_game_is_the_level_rule(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).in_game)
        self.assertFalse(deduce(Screen.NOT_IN_GAME).in_game)

    def test_in_map_is_the_payloads_own_flag(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).in_map)

    def test_the_rules_are_reported_as_readable(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).rules_loaded)

    def test_the_level_block_is_handed_out(self) -> None:
        level = deduce(Screen.LIVE).level
        self.assertTrue(level["up"])
        self.assertEqual(level["identities"], 726)

    def test_the_rules_block_is_handed_out(self) -> None:
        rules = deduce(Screen.LIVE).rules
        self.assertEqual(rules["game_phase"], 2)
        self.assertFalse(rules["bomb_planted"])

    def test_the_local_player_is_handed_out(self) -> None:
        local = deduce(Screen.LIVE).local
        if local is None:
            self.fail("the live fixture has no local player")
        self.assertEqual(local["weapon"], "weapon_ak47")

    def test_without_a_local_player_there_is_none(self) -> None:
        self.assertIsNone(deduce(Screen.NOT_READY).local)

    def test_the_team_is_the_local_players_team(self) -> None:
        self.assertEqual(deduce(Screen.LIVE).team, 2)
        self.assertEqual(deduce(Screen.TEAM_SELECT).team, 0)

    def test_alive_is_the_local_players_state(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).alive)
        self.assertFalse(deduce(Screen.DEAD).alive)

    def test_the_life_state_is_reported(self) -> None:
        self.assertEqual(deduce(Screen.LIVE).life_state, 0)
        self.assertEqual(deduce(Screen.DEAD).life_state, 2)

    def test_the_ground_state_is_reported(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).on_ground)

    def test_the_origin_is_the_local_players_position(self) -> None:
        origin = deduce(Screen.LIVE).origin
        if origin is None:
            self.fail("the live fixture has no origin")
        self.assertAlmostEqual(origin["x"], 1.0)

    def test_the_player_count_is_the_snapshot_length(self) -> None:
        self.assertEqual(deduce(Screen.LIVE).players, 2)

    def test_a_real_map_is_a_level_with_a_player_in_it(self) -> None:
        self.assertTrue(deduce(Screen.LIVE).in_real_map)
        self.assertFalse(deduce(Screen.NOT_IN_GAME).in_real_map)

    def test_the_buy_window_is_reported(self) -> None:
        self.assertFalse(deduce(Screen.LIVE).buy_time)
        self.assertTrue(deduce(Screen.BUY_TIME).buy_time)

    def test_a_dead_player_has_no_buy_window(self) -> None:
        """The window is a property of a living player."""
        dead = screens.deduce(snapshot(local={"alive": False},
                                       rules={"t_cant_buy": False}))
        self.assertFalse(dead.buy_time)

    def test_the_round_over_is_reported(self) -> None:
        self.assertFalse(deduce(Screen.LIVE).round_over)
        self.assertTrue(deduce(Screen.ROUND_END).round_over)

    def test_the_round_over_also_comes_from_the_events(self) -> None:
        """A build whose rules cannot be read still reports the round end."""
        state = screens.deduce(snapshot(), ["round_start", "round_end"])
        self.assertTrue(state.round_over)

    def test_the_bomb_facts_are_reported(self) -> None:
        planted = deduce(Screen.PLANTED)
        self.assertTrue(planted.bomb_planted)
        self.assertTrue(planted.defuser)
        self.assertFalse(planted.defusing)

    def test_the_match_facts_are_reported(self) -> None:
        self.assertEqual(deduce(Screen.LIVE).game_phase, 2)
        self.assertFalse(deduce(Screen.LIVE).overtime)
        self.assertFalse(deduce(Screen.LIVE).paused)

    def test_the_summary_names_the_screen_and_the_facts(self) -> None:
        summary = deduce(Screen.LIVE).summary()
        self.assertIn("screen=live", summary)
        self.assertIn("team=2", summary)
        self.assertIn("players=2", summary)

    def test_the_summary_mentions_the_notes(self) -> None:
        self.assertIn("notes=", deduce(Screen.NOT_IN_GAME).summary())

    def test_the_dict_view_is_json_friendly(self) -> None:
        view = deduce(Screen.LIVE).as_dict()
        self.assertEqual(view["screen"], "live")
        self.assertTrue(view["valid"])
        self.assertTrue(view["in_game"])
        self.assertEqual(view["players"], 2)
        self.assertIn("capabilities", view)
        self.assertIn("move", view["capabilities"])

    def test_the_dict_view_reports_every_capability(self) -> None:
        view = deduce(Screen.LIVE).as_dict()
        for capability in Can:
            self.assertIn(str(capability), view["capabilities"])

    def test_the_dict_view_serialises(self) -> None:
        """The view goes into a scenario's JSON result."""
        import json

        json.dumps(deduce(Screen.LIVE).as_dict())


class TestCapabilityContent(unittest.TestCase):
    """The table says what the game allows — not merely that it agrees with itself."""

    def test_every_screen_has_a_row(self) -> None:
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                self.assertIn(screen, screens.CAPABILITIES)

    def test_the_rows_say_what_the_game_allows(self) -> None:
        """The content of every row, spelled out."""
        for screen, expected in EXPECTED.items():
            with self.subTest(screen=str(screen)):
                self.assertEqual(frozenset(screens.CAPABILITIES[screen]), expected)

    def test_the_table_has_no_row_for_an_unknown_screen(self) -> None:
        self.assertEqual(len(screens.CAPABILITIES), len(Screen))

    def test_a_live_round_may_move_and_shoot(self) -> None:
        live = deduce(Screen.LIVE)
        self.assertTrue(live.can(Can.MOVE))
        self.assertTrue(live.can(Can.ATTACK))
        self.assertTrue(live.can(Can.JUMP))

    def test_a_dead_player_may_only_read(self) -> None:
        dead = deduce(Screen.DEAD)
        self.assertEqual({cap for cap in Can if dead.can(cap)},
                         {Can.STATE, Can.SCORE})

    def test_a_planted_round_may_be_defused_but_not_planted(self) -> None:
        planted = deduce(Screen.PLANTED)
        self.assertTrue(planted.can(Can.DEFUSE))
        self.assertFalse(planted.can(Can.PLANT))
        self.assertTrue(planted.can(Can.MOVE))

    def test_the_buy_window_is_only_where_it_is_open(self) -> None:
        """``BUY`` is in the freeze and in the buy window — nowhere else."""
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                if screen in (Screen.FREEZE, Screen.BUY_TIME):
                    self.assertIn(Can.BUY, screens.CAPABILITIES[screen])
                else:
                    self.assertNotIn(Can.BUY, screens.CAPABILITIES[screen])

    def test_only_a_disconnected_or_idle_client_may_reconnect(self) -> None:
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                expected = screen in (Screen.DISCONNECTED, Screen.NOT_IN_GAME)
                self.assertEqual(Can.RECONNECT in screens.CAPABILITIES[screen],
                                 expected)

    def test_the_read_only_capabilities_are_in_every_row(self) -> None:
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                for capability in screens.READ_ONLY:
                    self.assertIn(capability, screens.CAPABILITIES[screen])


class TestStateHelpers(unittest.TestCase):
    """The small helpers around the observation."""

    def test_deduce_is_pure(self) -> None:
        """The same inputs always give the same observation."""
        raw = snapshot()
        first = screens.deduce(raw, ["round_start"])
        second = screens.deduce(raw, ["round_start"])
        self.assertIs(first.screen, second.screen)
        self.assertEqual(first.as_dict()["screen"], second.as_dict()["screen"])

    def test_the_notes_explain_the_deduction(self) -> None:
        """Every observation says why it says what it says."""
        for screen in Screen:
            with self.subTest(screen=str(screen)):
                state = deduce(screen)
                self.assertTrue(state.notes, f"{screen} has no note")

    def test_the_fixtures_cover_every_screen(self) -> None:
        self.assertEqual(set(FIXTURES), set(Screen))

    def test_the_observation_is_immutable(self) -> None:
        """The observation is a value: it cannot be edited under the machine."""
        state: GameState = deduce(Screen.LIVE)
        with self.assertRaises(Exception):
            state.screen = Screen.DEAD  # type: ignore[misc]


if __name__ == "__main__":
    unittest.main(verbosity=2)
