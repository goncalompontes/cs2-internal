#!/usr/bin/env python3
"""Visual verification of the occlusion layer (ADR-0022 Tier 2).

Talks to a running payload, freezes the bots so the view is stable, aims at the
nearest enemy, then reads the heatmap **without** and **with** the world-solid
enumeration (``walls``) and renders three images per view:

  * ``walls_<view>_base.png``      damage, no occluders (the plain silhouettes)
  * ``walls_<view>_walls.png``     damage with the projected solids overlaid
  * ``walls_<view>_occluders.png`` only the projected world solids — the world
                                   geometry as the engine reports it, in the same
                                   angular chart as the hitboxes

The occluder silhouettes are the payload's ``walls.occluder_polygons`` (chart
space), drawn by ``scenario.heatmap.render(mode="occluders")``. The solids that may
actually attenuate are decided per cell by depth in the payload
(``OccluderCell::ray_span``), so the render can show the whole world (floor, walls)
while the damage stays correct.

Usage:
  python scripts/heatmap-walls.py                 # level / down / up views
  python scripts/heatmap-walls.py --fov 110
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
import time
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from scenario.control import Control  # noqa: E402
from scenario.heatmap import render  # noqa: E402


def _yaw_to(eye: dict[str, float], point: dict[str, float]) -> float:
    return math.degrees(math.atan2(point["y"] - eye["y"], point["x"] - eye["x"]))


def _pitch_to(eye: dict[str, float], point: dict[str, float]) -> float:
    dx = point["x"] - eye["x"]
    dy = point["y"] - eye["y"]
    dz = point["z"] - eye["z"]
    return -math.degrees(math.atan2(dz, math.hypot(dx, dy)))


def _aim_nearest(control: Control) -> dict[str, Any] | None:
    state = control.state()
    local = state.get("local")
    if not local:
        return None
    enemies = [p for p in state.get("players", [])
               if not p.get("is_local") and p.get("alive")
               and p.get("team") != local.get("team")]
    if not enemies:
        return None
    enemy = min(enemies, key=lambda p: p.get("distance", 1e9))
    aim = enemy.get("eye_origin") or enemy["origin"]
    control.look(yaw=_yaw_to(local["eye_origin"], aim),
                 pitch=_pitch_to(local["eye_origin"], aim))
    return enemy


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fov", type=float, default=110.0)
    parser.add_argument("--history", type=int, default=0,
                        help="lag records per player (0 = live poses only)")
    parser.add_argument("--out-dir", default="artifacts")
    parser.add_argument("--pipe", default=None)
    args = parser.parse_args(argv)

    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    control = Control(args.pipe).connect(timeout=20.0)
    try:
        control.exec("sv_cheats 1")
        control.exec("bot_stop 1")  # freeze the bots for a stable comparison
        control.heatmap(sample=False)
        time.sleep(0.4)
        enemy = _aim_nearest(control)
        print(f"[walls] aiming at {enemy.get('name') if enemy else '(none)'}")
        time.sleep(0.4)

        # Enable the world enumeration once; the per-frame query site then keeps the
        # hull cache warm, so later reads are cheap.
        control.heatmap(walls=True, walls_call=True)
        time.sleep(0.8)

        views = [("level", 0.0), ("down", -40.0), ("up", 35.0)]
        summary: dict[str, Any] = {}
        for name, pitch_delta in views:
            if enemy is not None:
                local = control.state().get("local") or {}
                aim = enemy.get("eye_origin") or enemy["origin"]
                control.look(yaw=_yaw_to(local["eye_origin"], aim),
                             pitch=_pitch_to(local["eye_origin"], aim) + pitch_delta)
            time.sleep(0.5)

            walls = control.heatmap(history=args.history, fov=args.fov, walls=True,
                                    walls_call=True, pen="engine")
            base = control.heatmap(history=args.history, fov=args.fov, walls=False)

            stem = out_dir / f"walls_{name}"
            # base: no world solids.  cells: world solids attenuate, no overlay.
            # walls: both.  occluders: the solids alone.  pen: the engine penetration
            # model (per-surface loss) with the attenuation outline.
            render(base, stem.with_name(stem.name + "_base.png"), size=800, mode="damage")
            render(walls, stem.with_name(stem.name + "_cells.png"), size=800, mode="damage")
            render(walls, stem.with_name(stem.name + "_walls.png"), size=800,
                   mode="damage", occluders=True)
            render(walls, stem.with_name(stem.name + "_occluders.png"), size=800,
                   mode="occluders")
            render(walls, stem.with_name(stem.name + "_pen.png"), size=800,
                   mode="penetration")
            (out_dir / f"walls_{name}.json").write_text(json.dumps(walls))

            w = walls.get("walls") or {}
            summary[name] = {
                "pitch_delta": pitch_delta,
                "eye": walls.get("eye"),
                "targets": len(walls.get("targets") or []),
                "cells": walls.get("cell_count"),
                "occluders": w.get("occluders"),
                "hulls": w.get("hulls"),
                "records": w.get("records"),
                "shapes": w.get("shapes"),
                "penetration_model": w.get("penetration_model"),
            }
            print(f"[walls] {name:5s} pitch{'+' if pitch_delta >= 0 else ''}{pitch_delta:+.0f} "
                  f"cells={walls.get('cell_count')} occluders={w.get('occluders')} "
                  f"hulls={w.get('hulls')} records={w.get('records')}")
    finally:
        control.close()

    (out_dir / "walls_summary.json").write_text(json.dumps(summary, indent=2, default=str))
    print(f"[walls] wrote {out_dir}/walls_*.png and walls_summary.json")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
