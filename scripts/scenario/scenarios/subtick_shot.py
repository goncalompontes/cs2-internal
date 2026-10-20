"""Scenario: prove the *shot* follows the sub-tick (silent) view angles.

``subtick-aim`` only proves the write lands and the camera does not follow; it
never proves the shot uses the silent angle. This scenario closes that gap:

1. reach a live round and freeze a bot directly in front of the player
   (``bot_place``), so the camera points at it;
2. **turn the camera away** from the bot (engine ``setang``) by ``yaw_offset``;
3. **silently aim back** at the bot's body (``look(..., via="usercmd")`` — the
   payload writes the sub-tick ``view_angles``; the camera must *not* follow);
4. fire, and check the bot's **health** dropped (a direct state read — the
   `player_hurt` event cannot be waited on *after* the shot).

A health drop with the camera still off means the shot used the sub-tick angle,
not the camera.
"""

from __future__ import annotations

import math
import time
from typing import Any, Mapping

from .. import awaiting
from ..protocol import PlayerSnapshotDict
from ..session import Game, SessionError

YAW_OFFSET = 45.0
HOLD_SECONDS = 0.8
CAMERA_TOLERANCE = 2.0


def _aim_angles(eye: Mapping[str, Any], target: Mapping[str, Any]) -> tuple[float, float]:
    """Source-convention ``(pitch, yaw)`` from ``eye`` to ``target``."""
    dx = target["x"] - eye["x"]
    dy = target["y"] - eye["y"]
    dz = target["z"] - eye["z"]
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(-dz, math.hypot(dx, dy)))
    return pitch, yaw


def _closest_enemy(game: Game, local: dict[str, Any]) -> PlayerSnapshotDict | None:
    eye = local.get("eye_origin")
    if eye is None:
        return None
    best: PlayerSnapshotDict | None = None
    best_d = math.inf
    for player in game.players():
        if player.get("is_local") or not player.get("alive"):
            continue
        if player.get("team") == local.get("team"):
            continue
        origin = player.get("origin")
        if origin is None:
            continue
        distance = math.dist((eye["x"], eye["y"], eye["z"]),
                             (origin["x"], origin["y"], origin["z"]))
        if distance < best_d:
            best_d = distance
            best = player
    return best


def run(game: Game, *, yaw_offset: float = YAW_OFFSET, hold: float = HOLD_SECONDS,
        map_name: str = "de_dust2") -> dict[str, Any]:
    """Silently aim at a bot while the camera looks away; assert the shot hits."""
    awaiting.ensure_live_round(game, map_name=map_name)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    # The screen machine decides whether firing is meaningful at all.
    awaiting.require(game, "attack")

    # Deterministic shot: a frozen bot in front, no spread.
    game.exec("sv_cheats 1")
    game.exec("bot_stop 1")
    game.exec("weapon_accuracy_nospread 1")

    local = awaiting.local(game)
    if _closest_enemy(game, local) is None:  # no enemy bot yet -> add one
        # Friendly fire is off in a casual match, so the bot must be an enemy.
        game.exec("bot_add_t" if local.get("team") == 3 else "bot_add_ct")
        if not game.wait_for(players_gte=2, timeout=25.0).get("matched"):
            raise SessionError("could not add a bot to shoot at")
        game.exec("bot_stop 1")

    game.exec("bot_place")  # put a bot directly in front of the camera

    local = awaiting.local(game)
    target = _closest_enemy(game, local)
    if target is None or local.get("eye_origin") is None:
        raise SessionError(f"no enemy bot to shoot at: {local}")

    eye = local["eye_origin"]
    # Aim at the body centre: the midpoint between the feet (`origin`) and the
    # eyes (`eye_origin`) — a reliable hitbox without head-bone data.
    feet = target["origin"]
    target_eye = target.get("eye_origin") or feet
    aim_point = {
        "x": feet["x"] + (target_eye["x"] - feet["x"]) / 2.0,
        "y": feet["y"] + (target_eye["y"] - feet["y"]) / 2.0,
        "z": feet["z"] + (target_eye["z"] - feet["z"]) / 2.0,
    }
    pitch, yaw = _aim_angles(eye, aim_point)

    # 1. Camera looks away from the bot.
    camera = game.look(yaw=yaw + yaw_offset, pitch=pitch)
    camera_before = awaiting.local(game).get("eye_angles")

    # 2. Silent aim back at the bot (sub-tick angles; the camera must not follow).
    game.look(yaw=yaw, pitch=pitch, via="usercmd")

    # 3. Fire; verify the hit by the target's *health*, not a `wait_for(event=…)`
    #    started after the shot (that can never see the event it just caused).
    state = awaiting.round_state(game)  # what the test depends on
    health_before = target.get("health")
    game.press("attack")
    time.sleep(hold)
    game.release("attack")
    time.sleep(0.2)

    after = game.input()
    camera_after = awaiting.local(game).get("eye_angles")
    health_after = next(
        (p.get("health") for p in game.players() if p.get("name") == target.get("name")), None)

    game.control.request("look", {"clear": True})
    game.exec("bot_stop 0")

    hit = health_before is not None and (health_after is None or health_after < health_before)
    if not hit:
        raise SessionError(
            f"silent shot missed: target health {health_before} -> {health_after} "
            f"(angle_writes={after.get('angle_writes')}, "
            f"camera={camera_before} -> {camera_after}, state={state})"
        )
    if camera_before is None or camera_after is None or \
            abs(camera_after["yaw"] - camera_before["yaw"]) > CAMERA_TOLERANCE:
        raise SessionError(f"angle write was not silent: {camera_before} -> {camera_after}")

    return {
        "aim_yaw": yaw,
        "camera_yaw": camera_before["yaw"],
        "silent_yaw": yaw,
        "hit": True,
        "target": target.get("name"),
        "target_health": [health_before, health_after],
        "angle_writes": after.get("angle_writes"),
        "state": state,
    }
