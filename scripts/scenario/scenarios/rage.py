"""Scenario: prove the **ragebot keybind** fires end-to-end (``docs/ragebot.md``).

Unlike ``aim-shot`` (which calls the one-shot ``aim`` command), this drives the
hold-key ragebot the way the overlay menu does: it enables the feature on the
payload, then lets the payload's own per-frame solve + per-tick silent write +
attack do the work. It asserts:

* the payload's ``rage`` status shows solves climbing (the render-thread solve
  runs) and ``writes``/``shots`` climbing (the per-tick ``Input::apply`` writes
  the silent sub-tick angle and presses attack);
* the bot's **health drops** (the shot follows the silent angle);
* the **camera does not move** (the aim is silent).

The harness cannot synthesize a physical key press into the game session, so the
scenario uses ``rage(force=True)`` to bypass the key check; the physical-key path
is exercised by the operator via the overlay menu's *Ragebot* tab.
"""

from __future__ import annotations

import math
import time
from typing import Any

from .. import awaiting
from ..protocol import PlayerSnapshotDict
from ..session import Game, SessionError

HOLD_SECONDS = 1.2
CAMERA_TOLERANCE = 2.0


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


def run(game: Game, *, hold: float = HOLD_SECONDS, map_name: str = "de_dust2",
        setup: bool = True, objective: str = "hitchance",
        fire: bool = True) -> dict[str, Any]:
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
        game.exec("bot_place")  # a bot directly in front of the camera
        # Guarantee a real gun with ammo, so the shot can damage the bot.
        game.exec("give weapon_ak47")
        game.exec("slot1")
        time.sleep(0.3)
    else:
        game.exec("weapon_accuracy_nospread 1")

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    awaiting.require(game, "attack")

    local = awaiting.local(game)
    target = _closest_enemy(game, local)
    if target is None:
        raise SessionError(f"no enemy bot to shoot at: {local}")

    camera_before = local.get("eye_angles")
    health_before = target.get("health")

    # Enable the payload-native ragebot. `force` bypasses the key check; `fire`
    # presses attack (this is the path the menu's Ragebot tab drives).
    game.rage(on=True, force=True, fire=fire, objective=objective)

    # Let the render-thread solve + per-tick write run.
    time.sleep(hold)
    rage_status = game.rage()
    # Stop before reading health, so no further shots land during the checks.
    game.rage(force=False, on=False)
    time.sleep(0.2)

    camera_after = awaiting.local(game).get("eye_angles")
    health_after = next(
        (p.get("health") for p in game.players() if p.get("name") == target.get("name")), None)

    if setup:
        game.exec("bot_stop 0")

    diagnostics = {
        "rage": rage_status,
        "health": [health_before, health_after],
        "camera_before": camera_before,
        "camera_after": camera_after,
        "target": target.get("name"),
    }

    solves = int(rage_status.get("solves", 0))
    writes = int(rage_status.get("writes", 0))
    shots = int(rage_status.get("shots", 0))
    if solves == 0:
        raise SessionError(f"ragebot never solved: {diagnostics}")
    if writes == 0:
        raise SessionError(f"ragebot never wrote the silent angle: {diagnostics}")
    if fire and shots == 0:
        raise SessionError(f"ragebot never fired: {diagnostics}")

    hit = health_before is not None and (health_after is None or health_after < health_before)
    if not hit:
        raise SessionError(f"ragebot shot missed: {diagnostics}")
    if camera_before is None or camera_after is None or \
            abs(camera_after["yaw"] - camera_before["yaw"]) > CAMERA_TOLERANCE:
        raise SessionError(f"ragebot aim was not silent: {camera_before} -> {camera_after}")

    return {
        "rage": rage_status,
        "hit": True,
        "target": target.get("name"),
        "target_health": [health_before, health_after],
        "camera_yaw": camera_before["yaw"],
        "solves": solves,
        "writes": writes,
        "shots": shots,
    }
