"""Scenario: aim and move the local player, asserting the state changes.

Exercises the runner-side input helpers (`look`, `press`) end to end: it reads
the view angles and origin from `state`, applies a `setang` aim and a held
`+forward`, and asserts the resulting angles/origin. Bots are frozen first so
they cannot kill the player mid-test (a dead player ignores input).
"""

from __future__ import annotations

from typing import Any

from ..session import Game, SessionError

# A yaw where +forward is along world +X (Source angle convention), so the
# expected movement axis is unambiguous.
YAW = 90.0
PITCH = -15.0
HOLD_S = 1.0
MIN_MOVE_UNITS = 40.0


def _ensure_alive(game: Game, timeout: float = 30.0) -> bool:
    game.exec("sv_cheats 1")
    game.exec("bot_stop 1")  # freeze bots: they would otherwise end the round
    if game.wait_for(local_alive=True, timeout=timeout)["matched"]:
        return True
    game.exec("mp_restartgame 1")
    return game.wait_for(local_alive=True, timeout=timeout)["matched"]


def run(game: Game, *, yaw: float = YAW, pitch: float = PITCH,
        hold: float = HOLD_S) -> dict[str, Any]:
    """Aim to ``yaw``/``pitch`` and walk forward; assert both took effect."""
    if not _ensure_alive(game):
        raise SessionError("no alive local player to drive")

    applied = game.look(yaw=yaw, pitch=pitch)
    angles = game.local()["eye_angles"]
    if abs(angles["yaw"] - applied["yaw"]) > 1.5 or abs(angles["pitch"] - applied["pitch"]) > 1.5:
        raise SessionError(f"look did not apply: wanted {applied}, got {angles}")

    before = game.local()["origin"]
    game.press("w", hold=hold)
    after = game.local()["origin"]
    moved = ((after["x"] - before["x"]) ** 2 + (after["y"] - before["y"]) ** 2) ** 0.5
    if moved < MIN_MOVE_UNITS:
        raise SessionError(f"press('w') moved only {moved:.1f} units")

    game.exec("bot_stop 0")  # restore
    return {
        "yaw": angles["yaw"],
        "pitch": angles["pitch"],
        "moved_units": round(moved, 1),
        "origin": after,
    }
