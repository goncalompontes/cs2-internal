"""Scenario: prove the ragebot solve *hits* (docs/ragebot.md).

The aim solve is live-validated in `aim-probe` (the direction is right); this
closes the loop — it lets the **solver** choose the silent sub-tick angle and
then fires, asserting the enemy's health drops:

1. reach a live round, freeze a bot directly in front of the camera (`bot_place`);
2. run `aim(write=True)` — the payload solves the damage field and writes the
   chosen direction to the sub-tick `view_angles` (the camera does not follow);
3. fire, and check the bot's **health** dropped.

A health drop with the camera unchanged means the shot used the solver's angle.
"""

from __future__ import annotations

import math
import time
from typing import Any, Mapping

from .. import awaiting
from ..protocol import PlayerSnapshotDict
from ..session import Game, SessionError

HOLD_SECONDS = 0.8
CAMERA_TOLERANCE = 2.0


def _aim_angles(eye: Mapping[str, Any], target: Mapping[str, Any]) -> tuple[float, float]:
    """Source-convention ``(pitch, yaw)`` from ``eye`` to ``target``."""
    dx = target["x"] - eye["x"]
    dy = target["y"] - eye["y"]
    dz = target["z"] - eye["z"]
    return (math.degrees(math.atan2(-dz, math.hypot(dx, dy))),
            math.degrees(math.atan2(dy, dx)))


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


def _facing_enemy(game: Game, local: dict[str, Any]) -> PlayerSnapshotDict | None:
    """The live enemy closest to the camera's forward — the one being looked at."""
    eye = local.get("eye_origin")
    angles = local.get("eye_angles")
    if eye is None or not angles:
        return None
    yaw = math.radians(angles.get("yaw", 0.0))
    pitch = math.radians(angles.get("pitch", 0.0))
    fwd = (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch))
    best: PlayerSnapshotDict | None = None
    best_angle = 180.0
    for player in game.players():
        if player.get("is_local") or not player.get("alive"):
            continue
        if player.get("team") == local.get("team"):
            continue
        origin = player.get("origin")
        if origin is None:
            continue
        d = (origin["x"] - eye["x"], origin["y"] - eye["y"], origin["z"] + 64.0 - eye["z"])
        norm = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
        if norm <= 0.0:
            continue
        cosine = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / norm
        angle = math.degrees(math.acos(max(-1.0, min(1.0, cosine))))
        if angle < best_angle:
            best_angle = angle
            best = player
    return best


def run(game: Game, *, hold: float = HOLD_SECONDS, map_name: str = "de_dust2",
        setup: bool = True, objective: str | None = None,
        headshot: float | None = None) -> dict[str, Any]:
    """Let the solver aim (silent) and fire; assert the enemy's health drops.

    ``setup=False`` skips the map/bot/placement steps and uses the caller's
    position (the player is expected to be looking at a live enemy already).
    """
    if setup:
        awaiting.ensure_live_round(game, map_name=map_name)
        game.exec("sv_cheats 1")
        game.exec("bot_stop 1")
        game.exec("weapon_accuracy_nospread 1")

        local = awaiting.local(game)
        if _closest_enemy(game, local) is None:
            game.exec("bot_add_t" if local.get("team") == 3 else "bot_add_ct")
            if not game.wait_for(players_gte=2, timeout=25.0).get("matched"):
                raise SessionError("could not add a bot to shoot at")
            game.exec("bot_stop 1")

        game.exec("bot_place")  # put a bot directly in front of the camera

        local = awaiting.local(game)
        target = _closest_enemy(game, local)
        if target is None or local.get("eye_origin") is None:
            raise SessionError(f"no enemy bot to shoot at: {local}")
        feet = target["origin"]
        target_eye = target.get("eye_origin") or feet
        body = {"x": feet["x"] + (target_eye["x"] - feet["x"]) / 2.0,
                "y": feet["y"] + (target_eye["y"] - feet["y"]) / 2.0,
                "z": feet["z"] + (target_eye["z"] - feet["z"]) / 2.0}
        pitch, yaw = _aim_angles(local["eye_origin"], body)
        game.look(yaw=yaw, pitch=pitch)
    else:
        # The caller has the game set up (positioned, bots static); just make the
        # shot deterministic. Do **not** touch `bot_stop` — the caller owns it.
        game.exec("weapon_accuracy_nospread 1")

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    awaiting.require(game, "attack")

    local = awaiting.local(game)
    target = _facing_enemy(game, local)
    if target is None or local.get("eye_origin") is None:
        raise SessionError(f"no enemy bot to shoot at: {local}")
    camera_before = local.get("eye_angles")
    clip_before = local.get("clip")

    solve = game.aim(write=True, objective=objective, target=target.get("index"),
                     headshot=headshot)
    if not solve.get("valid") or not solve.get("wrote_view"):
        raise SessionError(f"solve did not write the view: {solve}")

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
    if setup:
        game.exec("bot_stop 0")  # restore only what this scenario froze

    # Diagnostics: where the solver aimed vs. where the enemy actually is. The
    # enemy's own eye origin is the head reference (the eye sits in the head).
    eye = local["eye_origin"]
    feet = target["origin"]
    head = target.get("eye_origin") or {"x": feet["x"], "y": feet["y"], "z": feet["z"] + 64.0}
    enemy_yaw = math.degrees(math.atan2(head["y"] - eye["y"], head["x"] - eye["x"]))
    enemy_pitch = math.degrees(math.atan2(-(head["z"] - eye["z"]),
                                          math.hypot(head["x"] - eye["x"], head["y"] - eye["y"])))
    diagnostics = {
        "solve": solve,
        "camera_before": camera_before,
        "camera_after": camera_after,
        "eye": eye,
        "enemy_origin": feet,
        "enemy_head": head,
        "enemy_distance": target.get("distance"),
        "enemy_yaw": enemy_yaw,
        "enemy_pitch": enemy_pitch,
        "angle_writes": after.get("angle_writes"),
        "clip": [clip_before, awaiting.local(game).get("clip")],
        "weapon": local.get("weapon"),
    }

    hit = health_before is not None and (health_after is None or health_after < health_before)
    if not hit:
        raise SessionError(
            f"ragebot shot missed: target health {health_before} -> {health_after}; "
            f"diagnostics={diagnostics}"
        )
    if camera_before is None or camera_after is None or \
            abs(camera_after["yaw"] - camera_before["yaw"]) > CAMERA_TOLERANCE:
        raise SessionError(f"solve write was not silent: {camera_before} -> {camera_after}")

    return {
        "solve": solve,
        "hit": True,
        "target": target.get("name"),
        "target_health": [health_before, health_after],
        "camera_yaw": camera_before["yaw"],
        "enemy_yaw": enemy_yaw,
        "enemy_pitch": enemy_pitch,
        "angle_writes": after.get("angle_writes"),
    }
