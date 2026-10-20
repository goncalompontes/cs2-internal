"""Smoke scenario: start an offline bot match and assert the game state.

This is the milestone-1 acceptance scenario from ``docs/control-harness.md``:
launch + inject + connect (the session), then drive the console through the
control channel's ``exec`` and observe the result with ``wait_for``/``state``.

Bots are added one at a time and each add is confirmed with ``wait_for`` before
the next: firing several ``bot_add_*`` in one frame races the game's own bot
processing and one add can silently not stick.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

MAX_ADD_ATTEMPTS = 3
ADD_TIMEOUT = 25.0


def _wait_players_at_most(game: Game, count: int, timeout: float = 20.0) -> int:
    """Poll ``state`` until the player count is <= ``count``; returns the count."""
    deadline = time.monotonic() + timeout
    players = len(game.players())
    while players > count and time.monotonic() < deadline:
        time.sleep(0.5)
        players = len(game.players())
    return players


def _add_bot(game: Game, command: str, expected_players: int) -> bool:
    """Run ``command`` until the player count reaches ``expected_players``."""
    for _ in range(MAX_ADD_ATTEMPTS):
        game.exec(command)
        if game.wait_for(players_gte=expected_players, timeout=ADD_TIMEOUT)["matched"]:
            return True
    return False


def run(game: Game, *, map_name: str = "de_dust2", bots: int = 2,
        map_timeout: float = 180.0) -> dict[str, Any]:
    """Seed a bot match on ``map_name`` and assert at least ``bots + 1`` players."""
    # Cheats on, then a casual offline match (`awaiting.ensure_map`).
    game.exec("sv_cheats 1")
    awaiting.ensure_map(game, map_name=map_name, timeout=map_timeout)

    game.exec("bot_kick")  # clear whatever the previous match left behind
    added = max(_wait_players_at_most(game, 1), 1)
    for index in range(bots):
        added += 1
        command = "bot_add_t" if index % 2 == 0 else "bot_add_ct"
        if not _add_bot(game, command, added):
            raise SessionError(f"{command!r} did not raise the player count to {added}")

    players = game.players()
    if len(players) < bots + 1:
        raise SessionError(f"expected >= {bots + 1} players, saw {len(players)}")

    local = game.local()
    teams: dict[int, int] = {}
    for player in players:
        team = player["team"]
        teams[team] = teams.get(team, 0) + 1
    return {
        "map": map_name,
        "players": len(players),
        "teams": teams,
        "local_name": local.get("name") if local else None,
        "local_alive": bool(local and local.get("alive")),
    }
