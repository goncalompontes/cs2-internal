"""Live ragebot perf / enumeration checks.

One entry point for the measurements this work needed, so a later session can reproduce
them instead of re-deriving the ad-hoc probes:

  spot        print the current position/angles and the payload DLL identity
  save-spot   record the current position/angles as the comparison spot
  restore     teleport to the recorded spot (setpos/setang) and report the drift
  baseline    per-tick stage timings for the damage and hitchcone objectives, with the
              per-tick deltas of the payload's cumulative counters
  place       bring bots into view (``bot_place``) so the solve has real geometry
  enum        the payload's own enumeration self-check: ``enum=both`` runs the single-AABB
              and the per-target pyramid queries on the same tick and must report
              ``enum_outside_box == 0`` (the pyramid is a subset of the box volume)
  occlusion   build the wall-aware heatmap and report how many cells are attenuated

The comparison spot lives in ``artifacts/perf-spot.json`` so "before" and "after" runs are
measured at the identical world position (the bots themselves respawn differently, so the
cell count can change between game restarts — the host benchmark over a saved cell set is
the controlled measurement, see ``docs/ragebot-perf.md`` §3.9).

Examples:
    python scripts/perf-live.py save-spot
    python scripts/perf-live.py place
    python scripts/perf-live.py enum
    python scripts/perf-live.py baseline --out artifacts/perf-after.json --seconds 8
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from scenario.control import Control  # noqa: E402
from scenario.session import Game  # noqa: E402

SPOT_PATH = "artifacts/perf-spot.json"
STAGES = ("total_us", "solve_us", "solve_argmax_us", "solve_occlude_us", "solve_project_us",
          "backtrack_us", "project_us", "query_us", "sample_us", "build_us")


def connect() -> tuple[Control, Game]:
    control = Control()
    control.connect()
    return control, Game(control)


def cmd_spot(game: Game) -> int:
    local = game.local() or {}
    dll = os.path.join("build-payload", "payload.dll")
    print(json.dumps({
        "origin": local.get("origin"),
        "eye_angles": local.get("eye_angles"),
        "weapon": local.get("weapon"),
        "team": local.get("team"),
        "payload": {
            "mtime": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(os.path.getmtime(dll))),
            "size": os.path.getsize(dll),
        },
    }, indent=1))
    return 0


def cmd_save_spot(game: Game) -> int:
    local = game.local() or {}
    spot = {"origin": local.get("origin"), "angles": local.get("eye_angles"),
            "weapon": local.get("weapon"), "team": local.get("team")}
    os.makedirs(os.path.dirname(SPOT_PATH) or ".", exist_ok=True)
    with open(SPOT_PATH, "w", encoding="utf-8") as handle:
        json.dump(spot, handle, indent=1)
    print(json.dumps(spot, indent=1))
    return 0


def restore(game: Game) -> bool:
    """Teleport to the saved spot. Returns True when the drift is within 8 units."""
    if not os.path.exists(SPOT_PATH):
        print(f"no saved spot at {SPOT_PATH}")
        return False
    with open(SPOT_PATH, encoding="utf-8") as handle:
        spot = json.load(handle)
    origin = spot["origin"]
    angles = spot["angles"]
    before = (game.local() or {}).get("origin")
    game.exec(f"setpos {origin['x']:.3f} {origin['y']:.3f} {origin['z']:.3f}")
    game.exec(f"setang {angles['pitch']:.3f} {angles['yaw']:.3f}")
    time.sleep(1.5)
    after = (game.local() or {}).get("origin")
    print(f"before: {before}")
    print(f"after:  {after}")
    if not after:
        return False
    drift = max(abs(after[k] - origin[k]) for k in ("x", "y", "z"))
    print(f"drift:  {drift:.2f} units")
    return drift <= 8.0


def cmd_place(game: Game) -> int:
    game.exec("sv_cheats 1")
    game.exec("bot_stop 1")
    game.exec("weapon_accuracy_nospread 1")
    game.exec("give weapon_ak47")
    game.exec("slot1")
    time.sleep(0.3)
    for _ in range(4):
        game.exec("bot_place")
        time.sleep(0.25)
    game.exec("bot_stop 1")
    time.sleep(0.5)
    local = game.local() or {}
    enemies = [p for p in game.players()
               if not p.get("is_local") and p.get("alive") and p.get("team") != local.get("team")]
    print(f"local: {local.get('origin')} weapon: {local.get('weapon')}")
    for player in enemies:
        print(f"  {player['index']:2d} {player['name'][:16]:16s} "
              f"dist {player.get('distance', 0):7.1f}")
    return 0


def _snapshot(game: Game) -> tuple[int, dict[str, float], dict]:
    perf = game.rage()["perf"]
    frames = int(perf["frames"])
    return frames, {k: float(perf[k]) * frames for k in STAGES}, perf


def cmd_baseline(game: Game, out: str, seconds: float, apply: bool) -> int:
    if apply:
        restore(game)
    runs = []
    # The objective is a **name**, not a number: the payload's `rage` command
    # maps a name to the profile's byte (`cs2/control/rage_objective.hpp`).
    # The sweep used to pass the numbers 0 and 1, which the payload ignored —
    # so it measured the same objective twice and never measured the
    # Monte-Carlo one at all.
    for objective in ("damage", "hitchance", "expected", "monte_carlo"):
        game.rage(on=True, force=True, fire=False, objective=objective)
        time.sleep(1.5)  # settle the cumulative averages onto this objective
        f0, a0, _ = _snapshot(game)
        time.sleep(seconds)
        f1, a1, perf = _snapshot(game)
        frames = max(f1 - f0, 1)
        runs.append({
            "objective": objective,
            "frames": frames,
            "us_per_tick": {k: round((a1[k] - a0[k]) / frames, 1) for k in STAGES},
            "counters": {k: perf[k] for k in ("cells", "hitboxes", "occluders", "poses",
                                              "candidates", "solve_calls")},
        })
    game.rage(on=False, force=False)

    local = game.local() or {}
    dll = os.path.join("build-payload", "payload.dll")
    record = {
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "spot": {"origin": local.get("origin"), "eye_angles": local.get("eye_angles"),
                 "weapon": local.get("weapon"), "team": local.get("team")},
        "payload": {"mtime": time.strftime("%Y-%m-%dT%H:%M:%S",
                                           time.localtime(os.path.getmtime(dll))),
                    "size": os.path.getsize(dll)},
        "runs": runs,
    }
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=1)
    print(json.dumps(record, indent=1))
    return 0


def cmd_enum(game: Game) -> int:
    print("spot:", (game.local() or {}).get("origin"))
    worst = 0
    for _ in range(6):
        walls = (game.heatmap(walls=True, walls_call=True, enum="both").get("walls") or {})
        row = {k: walls.get(k) for k in ("world_box_kept", "enum_box_kept", "enum_pyramid_kept",
                                         "enum_outside_box", "occluders")}
        print(row)
        worst = max(worst, int(row.get("enum_outside_box") or 0))
        time.sleep(0.4)
    print(f"pyramid hulls outside the box volume: {worst} (must be 0)")
    return 0 if worst == 0 else 1


def cmd_occlusion(game: Game) -> int:
    heat = game.heatmap(walls=True, walls_call=True)
    cells = heat.get("cells") or []
    attenuated = sum(1 for c in cells
                     if float(c.get("value", 0.0)) < float(c.get("base_value", 0.0)) - 0.05)
    blocked = sum(1 for c in cells if float(c.get("value", 0.0)) <= 0.05)
    walls = heat.get("walls") or {}
    print("spot:", (game.local() or {}).get("origin"))
    print("world hulls kept:", walls.get("world_box_kept"), "occluders:", walls.get("occluders"))
    print(f"cells: {len(cells)}  attenuated: {attenuated}  blocked: {blocked}")
    return 0 if cells else 1


def cmd_compare(game: Game) -> int:
    """Diff the AABB and pyramid enumerations on one scene.

    Both are exact; the pyramid is just tighter. The authoritative assertion is that the
    **used occluder silhouettes** are identical: those are the solids `build_heatmap`
    decided actually stand between the eye and a hitbox, so a difference would mean the
    tighter volume dropped a wall that matters.

    The per-cell values are reported too, but compared loosely: the payload re-reads the
    live poses on every command, so two calls are never the same tick (running the same
    mode twice jitters by the same amount).
    """
    def measure(mode: str) -> dict:
        # Ask for this mode, then wait for the game thread to re-enumerate and settle.
        game.heatmap(walls=True, walls_call=True, history=0, enum=mode)
        time.sleep(0.6)
        return game.heatmap(walls=True, walls_call=True, history=0, enum=mode)

    def used_occluders(payload: dict) -> list[tuple]:
        polygons = (payload.get("walls") or {}).get("occluder_polygons") or []
        return sorted(tuple((round(p[0], 3), round(p[1], 3)) for p in poly) for poly in polygons)

    def values(payload: dict) -> list[float]:
        return sorted(round(float(c.get("value", 0.0)), 3) for c in (payload.get("cells") or []))

    def report(name: str, payload: dict) -> None:
        cells = payload.get("cells") or []
        attenuated = sum(1 for c in cells
                         if float(c.get("value", 0.0)) < float(c.get("base_value", 0.0)) - 0.05)
        blocked = sum(1 for c in cells if float(c.get("value", 0.0)) <= 0.05)
        walls = payload.get("walls") or {}
        print(f"{name:8s} box_kept={walls.get('world_box_kept')} "
              f"occluders={walls.get('occluders')} cells={len(cells)} "
              f"attenuated={attenuated} blocked={blocked}")

    print("spot:", (game.local() or {}).get("origin"))
    box = measure("box")
    pyramid = measure("pyramid")
    report("AABB", box)
    report("pyramid", pyramid)

    box_occluders = used_occluders(box)
    pyramid_occluders = used_occluders(pyramid)
    missing = [p for p in box_occluders if p not in pyramid_occluders]
    extra = [p for p in pyramid_occluders if p not in box_occluders]
    print(f"used occluder silhouettes: AABB {len(box_occluders)}, "
          f"pyramid {len(pyramid_occluders)}")
    print(f"  only in AABB:    {len(missing)}")
    print(f"  only in pyramid: {len(extra)}")

    left, right = values(box), values(pyramid)
    if len(left) == len(right) and left:
        deltas = [abs(a - b) for a, b in zip(left, right)]
        print(f"cell values: max delta {max(deltas):.3f}, mean {sum(deltas) / len(deltas):.3f} "
              "(same-mode runs jitter by a comparable amount)")
    return 0 if not missing and not extra else 1


def cmd_watch(game: Game, seconds: float, interval: float, threshold_us: float) -> int:
    """Sample the live rage stage costs and report the worst ticks.

    The payload's counters are cumulative, so the rows are deltas per sample window; the
    payload's own `last_us` is the single most recent rage tick and is what catches a
    pathological frame.
    """
    print(f"watching for {seconds:.0f}s (threshold {threshold_us:.0f} us/tick)")
    prev = None
    worst = None
    end = time.time() + seconds
    while time.time() < end:
        perf = game.rage()["perf"]
        frames = int(perf["frames"])
        acc = {k: float(perf[k]) * frames for k in STAGES}
        last = float(perf.get("last_us", 0.0))
        row = {"t": time.strftime("%H:%M:%S"), "last_us": last,
               "cells": perf.get("cells"), "hitboxes": perf.get("hitboxes"),
               "occluders": perf.get("occluders"), "poses": perf.get("poses")}
        if prev is not None:
            df = max(frames - prev[0], 1)
            row["stages"] = {k: round((acc[k] - prev[1][k]) / df, 1) for k in STAGES}
        if last >= threshold_us:
            print("SPIKE", json.dumps(row))
        if worst is None or last > worst["last_us"]:
            worst = row
        prev = (frames, acc)
        time.sleep(interval)
    print("worst tick seen:", json.dumps(worst))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("spot", "save-spot", "restore", "place",
                                            "baseline", "enum", "occlusion", "compare",
                                            "watch"))
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--threshold", type=float, default=20000.0,
                        help="report a tick slower than this many microseconds")
    parser.add_argument("--out", default="artifacts/perf-baseline.json")
    parser.add_argument("--apply", action="store_true",
                        help="restore the saved spot before measuring")
    args = parser.parse_args()

    control, game = connect()
    try:
        if args.command == "spot":
            return cmd_spot(game)
        if args.command == "save-spot":
            return cmd_save_spot(game)
        if args.command == "restore":
            return 0 if restore(game) else 1
        if args.command == "place":
            return cmd_place(game)
        if args.command == "baseline":
            return cmd_baseline(game, args.out, args.seconds, args.apply)
        if args.command == "enum":
            return cmd_enum(game)
        if args.command == "compare":
            return cmd_compare(game)
        if args.command == "watch":
            return cmd_watch(game, args.seconds, args.interval, args.threshold)
        return cmd_occlusion(game)
    finally:
        control.close()


if __name__ == "__main__":
    raise SystemExit(main())
