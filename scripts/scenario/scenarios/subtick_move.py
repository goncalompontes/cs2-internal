"""Scenario: synthesise analog movement through the payload (raw input axes).

Exercises the ``usercmd`` movement seam: the payload writes the raw analog move
axes on ``CCSGOInput`` (``+0x270``), which ``create_move_input`` copies into
``CBaseUserCmdPB::forwardmove`` each tick — the same source the console
``+forward`` writes (``docs/re/subtick.md`` §"The command build").

It aims at a fixed yaw so ``forward`` is along world +X, holds ``move(forward=…)``
for ``hold`` seconds, and asserts the local player actually moved. This is the
movement counterpart to ``subtick-shot``: a failure means the command-side /
raw-input write is not reaching the simulation.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

YAW = 90.0  # +forward is along world +X (Source convention)
PITCH = 0.0
HOLD_SECONDS = 1.0
# A full-speed walk covers ~200 units in a second (measured live 2026-10-15:
# `move(forward=1.0)` = 204 units, the console `+forward` = 203).
MIN_MOVE_UNITS = 100.0
# The engine's own unit: a console `+forward` writes `forwardmove = 1.0` (450 is
# `cl_forwardspeed`, applied downstream — writing 450 does not move the player).
FORWARD_SPEED = 1.0


def run(game: Game, *, hold: float = HOLD_SECONDS, forward: float = FORWARD_SPEED,
        map_name: str = "de_dust2") -> dict[str, Any]:
    """Hold the raw ``usercmd`` forward axis; assert the player moves."""
    awaiting.ensure_live_round(game, map_name=map_name)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    # The screen machine decides whether movement input is meaningful at all
    # (main menu / team-select / warmup / freeze / dead all swallow it).
    awaiting.require(game, "move")

    game.look(yaw=YAW, pitch=PITCH)  # unambiguous movement axis
    before = awaiting.origin(game)
    state = awaiting.round_state(game)  # what the test depends on

    game.move(forward=forward)
    time.sleep(hold)
    after_origin = awaiting.origin(game)
    game.move(clear=True)

    after = game.input()
    moved = awaiting.displacement(before, after_origin) if (before and after_origin) else 0.0

    if moved < MIN_MOVE_UNITS:
        raise SessionError(
            f"usercmd move did not move the player ({moved:.1f} units): the raw "
            f"axis write is not reaching the simulation "
            f"(raw_writes={after.get('raw_writes')}, "
            f"move_writes={after.get('move_writes')}, state={state})"
        )

    return {
        "before": before,
        "after": after_origin,
        "moved_units": round(moved, 1),
        "raw_writes": after.get("raw_writes"),
        "move_writes": after.get("move_writes"),
        "state": state,
    }
