"""Scenario: build and render the multi-target damage heatmap (``docs/ragebot.md``).

Sets up a moving bot match, faces the nearest enemy, and asks the payload's
``heatmap`` command for **every** enemy's hitbox silhouettes — together with their
sampled lag records — in one shared angular chart. The pure-stdlib renderer in
``scenario.heatmap`` then rasterises the damage field (and the record ages) to PNG
so the model can be inspected out of game.

Run:
  python scripts/run-scenario.py heatmap --set out=artifacts/heatmap.png --keep-running
"""

from __future__ import annotations

import math
import time
from typing import Any, Mapping

from .. import awaiting
from ..heatmap import render
from ..protocol import PlayerSnapshotDict
from ..session import REPO_ROOT, Game, SessionError


def _yaw_to(eye: Mapping[str, Any], point: Mapping[str, Any]) -> float:
    return math.degrees(math.atan2(point["y"] - eye["y"], point["x"] - eye["x"]))


def _pitch_to(eye: Mapping[str, Any], point: Mapping[str, Any]) -> float:
    dx = point["x"] - eye["x"]
    dy = point["y"] - eye["y"]
    dz = point["z"] - eye["z"]
    return -math.degrees(math.atan2(dz, math.hypot(dx, dy)))


def _nearest_enemy(game: Game) -> tuple[PlayerSnapshotDict | None,
                                       PlayerSnapshotDict | None]:
    """The nearest living enemy and the local player (the wire shapes)."""
    local = game.local()
    if local is None:
        return None, None
    enemies = [p for p in game.players()
               if not p.get("is_local") and p.get("alive")
               and p.get("team") != local.get("team")]
    if not enemies:
        return None, local
    return min(enemies, key=lambda p: p.get("distance", 1e9)), local


def run(game: Game, *, map_name: str = "de_dust2", per_team: int = 3, history: int = 8,
        fov: float = 90.0, size: int = 800, settle: float = 1.5, map_timeout: float = 300.0,
        attempts: int = 4, walls: bool = True, distance: float = 400.0, pen: str = "engine",
        out: str = "artifacts/heatmap.png") -> dict[str, Any]:
    game.exec("sv_cheats 1")
    # Keep the bots moving so the lag records actually differ frame to frame.
    awaiting.ensure_bot_match(game, map_name=map_name, per_team=per_team, bot_stop=False,
                              map_timeout=map_timeout)
    # Invulnerable: the bots can keep moving (so records differ) without killing us.
    game.exec("god")

    # Clear the record store and ensure continuous sampling; the render thread then
    # accrues a window of records while we stand next to an enemy.
    game.heatmap(sample=True, clear=True)

    result = None
    enemy = None
    for _ in range(max(1, attempts)):
        enemy, local = _nearest_enemy(game)
        if enemy is None or local is None:
            time.sleep(0.5)
            continue
        # Stand next to the enemy so its silhouette is close and large in the chart
        # (at spawn distances a body is only a couple of degrees across), then face
        # it. The bots are moving, so a look can miss — retry with the new nearest.
        origin = enemy["origin"]
        game.teleport(origin["x"] + distance, origin["y"], origin["z"])
        time.sleep(0.4)
        enemy, local = _nearest_enemy(game)
        if enemy is None or local is None:
            continue
        aim = enemy.get("eye_origin") or enemy["origin"]
        game.look(yaw=_yaw_to(local["eye_origin"], aim),
                  pitch=_pitch_to(local["eye_origin"], aim))
        time.sleep(settle)
        candidate = game.heatmap(history=history, fov=fov, walls=walls, walls_call=walls,
                                 pen=pen)
        if candidate.get("valid") and candidate.get("cell_count", 0) > 0:
            result = candidate
            break
    if result is None or enemy is None:
        raise SessionError(f"the heatmap produced no silhouettes after {attempts} attempts")

    path = REPO_ROOT / out
    focus = enemy.get("index")
    # With `walls` we want every enemy in frame (so occluded ones are visible), so
    # render the whole chart rather than cropping to the faced target.
    stats = render(result, path, size=size, mode="damage", fit=not walls, focus=focus)
    records_path = path.with_name(f"{path.stem}_records{path.suffix}")
    stats_records = render(result, records_path, size=size, mode="record", fit=not walls,
                           focus=focus)
    # The engine-penetration damage view: the damage the enemies would take through the
    # walls, with the solids and each cell's attenuation drawn (docs/re/penetration.md).
    pen_path = path.with_name(f"{path.stem}_pen{path.suffix}")
    stats_pen = render(result, pen_path, size=size, mode="penetration", fit=not walls, focus=focus)

    return {
        "map": map_name,
        "image": str(path),
        "image_records": str(records_path),
        "image_penetration": str(pen_path),
        "enemy": enemy.get("name"),
        "cells": result.get("cell_count"),
        "targets": result.get("targets"),
        "weapon": result.get("weapon"),
        "extent": result.get("extent"),
        "penetration_model": (result.get("walls") or {}).get("penetration_model"),
        "stats": stats,
        "stats_records": stats_records,
        "stats_penetration": stats_pen,
    }
