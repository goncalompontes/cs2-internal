"""Scenario: exercise the ragebot aim solve live (``docs/ragebot.md``).

Brings up a live round with an enemy bot, faces it, and asserts the payload's
``aim`` command returns a direction that points at the enemy's head — the core of
the damage-field solve, running against the real game.

The solve uses the collision-AABB proxy for hitboxes (head = the top 8 units, body
below) and no occlusion yet, so the expected direction is the enemy's *head* (the
best cell, because of the headshot multiplier).
"""

from __future__ import annotations

import math
import time
from typing import Any, Mapping

from .. import awaiting
from ..protocol import PlayerSnapshotDict
from ..session import Game, SessionError

# The proxy head box sits at z in [64, 72] above the feet; the damage optimum is
# its near (lower) edge, so aim the expectation at ~64 units up.
HEAD_UNITS = 64.0
YAW_TOLERANCE = 4.0
PITCH_TOLERANCE = 6.0


def _yaw_to(eye: Mapping[str, Any], point: Mapping[str, Any]) -> float:
    return math.degrees(math.atan2(point["y"] - eye["y"], point["x"] - eye["x"]))


def _pitch_to(eye: Mapping[str, Any], point: Mapping[str, Any]) -> float:
    dx = point["x"] - eye["x"]
    dy = point["y"] - eye["y"]
    dz = point["z"] - eye["z"]
    return -math.degrees(math.atan2(dz, math.hypot(dx, dy)))


def _delta(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def _wait_players_at_most(game: Game, count: int, timeout: float = 20.0) -> int:
    deadline = time.monotonic() + timeout
    players = len(game.players())
    while players > count and time.monotonic() < deadline:
        time.sleep(0.5)
        players = len(game.players())
    return players


def _add_bot(game: Game, command: str, expected: int, timeout: float = 25.0) -> None:
    for _ in range(3):
        game.exec(command)
        if game.wait_for(players_gte=expected, timeout=timeout)["matched"]:
            return
    raise SessionError(f"{command!r} did not raise the player count to {expected}")


def _find_enemy(game: Game) -> tuple[PlayerSnapshotDict | None,
                                    PlayerSnapshotDict | None]:
    """The first living enemy and the local player (the wire shapes)."""
    local = game.local()
    if local is None:
        return None, None
    for player in game.players():
        if (player.get("is_local") or not player.get("alive")
                or player.get("team") == local.get("team")):
            continue
        return player, local
    return None, local


def run(game: Game, *, map_name: str = "de_dust2", bots: int = 2,
        map_timeout: float = 240.0) -> dict[str, Any]:
    game.exec("sv_cheats 1")
    awaiting.ensure_live_round(game, map_name=map_name, timeout=map_timeout)

    game.exec("bot_kick")
    added = max(_wait_players_at_most(game, 1), 1)  # the local player
    for index in range(max(bots, 1)):
        added += 1
        _add_bot(game, "bot_add_t" if index % 2 == 0 else "bot_add_ct", added)
    game.exec("bot_stop 1")  # freeze the bots so the test is deterministic

    enemy = None
    local = None
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        enemy, local = _find_enemy(game)
        if enemy is not None:
            break
        time.sleep(0.5)
    if enemy is None or local is None:
        raise SessionError(f"no live enemy bot; players={game.players()}")

    # Face the enemy (yaw), level pitch, so it is in front of the eye.
    game.look(yaw=_yaw_to(local["eye_origin"], enemy["origin"]), pitch=0.0)
    time.sleep(0.3)

    result = game.aim()
    if not result.get("valid"):
        raise SessionError(f"aim returned invalid: {result}")

    # The expected direction to the enemy's head, from the *current* eye.
    local = game.local()
    if local is None:
        raise SessionError("the local player vanished")
    eye = local["eye_origin"]
    head = {"x": enemy["origin"]["x"], "y": enemy["origin"]["y"],
            "z": enemy["origin"]["z"] + HEAD_UNITS}
    expected_yaw = _yaw_to(eye, head)
    expected_pitch = _pitch_to(eye, head)

    yaw_err = _delta(result["yaw"], expected_yaw)
    pitch_err = abs(result["pitch"] - expected_pitch)
    if yaw_err > YAW_TOLERANCE or pitch_err > PITCH_TOLERANCE:
        raise SessionError(
            f"aim off target: got yaw={result['yaw']:.2f} pitch={result['pitch']:.2f}; "
            f"expected yaw={expected_yaw:.2f} pitch={expected_pitch:.2f} "
            f"(yaw_err={yaw_err:.2f}, pitch_err={pitch_err:.2f})"
        )

    return {
        "map": map_name,
        "enemy": enemy.get("name"),
        "distance": enemy.get("distance"),
        "aim": result,
        "expected_yaw": expected_yaw,
        "expected_pitch": expected_pitch,
        "yaw_err": yaw_err,
        "pitch_err": pitch_err,
        "hitchance": game.aim(objective="hitchance"),
        "expected": game.aim(objective="expected", sigma=0.02),
    }
