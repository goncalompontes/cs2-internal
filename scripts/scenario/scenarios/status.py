"""Scenario: report the current local/round state (diagnostic, no input).

Prints what an input test depends on: whether the local player is alive and on a
team, whether they are on the ground and holding a weapon, and the tail of the
game-event stream (``round_start`` / ``round_freeze_end`` / ``round_end`` /
``player_death``). Use it to check that a test really ran during a live round
rather than warmup, freeze or the team-select screen.

Run against an already-running game::

    python scripts/run-scenario.py status --no-launch --no-inject --keep-running
"""

from __future__ import annotations

from typing import Any

from .. import awaiting
from ..session import Game


def run(game: Game, *, events: int = 12) -> dict[str, Any]:
    """Return the current state + recent events (no input)."""
    return awaiting.round_state(game, events=events)
