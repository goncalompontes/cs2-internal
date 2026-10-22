"""Scenario: join a map and set up a ready-to-test bot match.

Loads the level, applies the practice convars (no warmup/freeze, 60-minute
rounds, respawns, infinite ammo and money), joins a team, adds ``per_team`` bots
to each side and freezes them (``bot_stop 1``). The work lives in
:func:`scenario.awaiting.ensure_bot_match` so scenarios (and ad-hoc callers) can
reuse it.

Usage:
  python scripts/run-scenario.py bot-match
  python scripts/run-scenario.py bot-match --set map_name=de_inferno --set per_team=3
"""

from __future__ import annotations

from typing import Any

from .. import awaiting
from ..session import Game


def run(game: Game, *, map_name: str = "de_dust2", per_team: int = 5,
        respawn: bool = True, bot_stop: bool = True) -> dict[str, Any]:
    """Set up a bot match on ``map_name`` with ``per_team`` bots per side."""
    return awaiting.ensure_bot_match(game, map_name=map_name, per_team=per_team,
                                     respawn=respawn, bot_stop=bot_stop)
