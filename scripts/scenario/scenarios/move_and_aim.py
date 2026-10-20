"""Scenario: aim and move the local player, asserting the state changes.

Exercises the runner-side input helpers (`look`, `press`) end to end: it reads
the view angles and origin from `state`, applies a `setang` aim and a held
`+forward`, and asserts the resulting angles/origin. Bots are frozen first so
they cannot kill the player mid-test (a dead player ignores input).
"""

from __future__ import annotations

from typing import Any

from .. import awaiting
from ..session import Game, SessionError

# A yaw where +forward is along world +X (Source angle convention), so the
# expected movement axis is unambiguous.
YAW = 90.0
PITCH = -15.0
HOLD_S = 1.0
MIN_MOVE_UNITS = 40.0


def run(game: Game, *, yaw: float = YAW, pitch: float = PITCH,
        hold: float = HOLD_S, map_name: str = "de_dust2") -> dict[str, Any]:
    """Aim to ``yaw``/``pitch`` and walk forward; assert both took effect."""
    awaiting.ensure_live_round(game, map_name=map_name)

    applied = game.look(yaw=yaw, pitch=pitch)
    local = game.local()
    if local is None:
        raise SessionError("no local player")
    angles = local["eye_angles"]
    if abs(angles["yaw"] - applied["yaw"]) > 1.5 or abs(angles["pitch"] - applied["pitch"]) > 1.5:
        raise SessionError(f"look did not apply: wanted {applied}, got {angles}")

    result = awaiting.measure_move(game, key="w", hold=hold, yaw=yaw, pitch=pitch)
    if result["moved"] < MIN_MOVE_UNITS:
        raise SessionError(f"press('w') moved only {result['moved']:.1f} units")

    game.exec("bot_stop 0")  # restore
    return {
        "yaw": angles["yaw"],
        "pitch": angles["pitch"],
        "moved_units": result["moved"],
        "origin": result["after"],
    }
