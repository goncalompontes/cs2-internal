"""Scenario: join a map and set up a ready-to-test bot match.

The order is the contract (2026-10-21): load the level → **join the team**
(as early as the screen allows) → the playground convars → ``mp_restartgame 1``
→ wait for the round to go live → add the bots → ``bot_stop 1`` → **wait for
the round to be live again** → return. The convars stay *before* the restart
(otherwise the round's freezetime/roundtime/respawn are the old ones) and the
bots stay *after* the round went live.

The scenario does not return while the round is still in a transition: the
result says which screen the game is in, what the round looks like
(``round``), how long the final wait took (``waited_s``) and how the local
player got onto a team (``team_joined``: ``"joined"`` — the scenario's own
``jointeam`` — or ``"auto"``, the server assigned it).

The work lives in :func:`scenario.awaiting.ensure_bot_match` so scenarios
(and ad-hoc callers) can reuse it.

Usage:
  python scripts/run-scenario.py bot-match
  python scripts/run-scenario.py bot-match --set map_name=de_inferno --set per_team=3
"""

from __future__ import annotations

from typing import Any

from .. import awaiting
from ..session import Game


def run(game: Game, *, map_name: str = "de_dust2", per_team: int = 5,
        respawn: bool = True, bot_stop: bool = True,
        join_timeout: float = 30.0,
        final_timeout: float = awaiting.DEFAULT_FINAL_TIMEOUT) -> dict[str, Any]:
    """Set up a bot match on ``map_name`` with ``per_team`` bots per side.

    ``join_timeout`` is the budget for the team join and ``final_timeout`` the
    budget for the final wait (the round must be live again before the
    scenario returns); both raise :class:`~scenario.session.SessionError` when
    they expire.
    """
    return awaiting.ensure_bot_match(game, map_name=map_name, per_team=per_team,
                                     respawn=respawn, bot_stop=bot_stop,
                                     join_timeout=join_timeout,
                                     final_timeout=final_timeout)
