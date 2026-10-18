#!/usr/bin/env python3
"""Render the payload's multi-target damage heatmap to a PNG (``docs/ragebot.md``).

Talks to a running payload's control channel, asks the ``heatmap`` command for
every enemy's hitbox silhouettes — and their sampled lag records — projected into
one shared angular chart, and rasterises the result with the pure-stdlib renderer
in ``scenario/heatmap.py`` (no numpy / PIL / matplotlib: just zlib for the PNG).

Two images are written: the **damage** field (max damage per pixel across every
player and record) and the **record** ages (live poses warm, older records cold),
so the lag-record spread is visible.

Usage:
  python scripts/heatmap-view.py                                  # damage + records
  python scripts/heatmap-view.py --history 12 --fov 60 -o artifacts/hm.png
  python scripts/heatmap-view.py --mode damage --no-sample        # live poses only
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from scenario.control import Control  # noqa: E402
from scenario.heatmap import render  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--history", type=int, default=8,
                        help="lag records per player to include (0 = live poses only)")
    parser.add_argument("--fov", type=float, default=90.0,
                        help="horizontal field of view covered by the chart (degrees)")
    parser.add_argument("--size", type=int, default=800, help="output image size (pixels)")
    parser.add_argument("--target", type=int, default=None,
                        help="centre the chart on this entity index")
    parser.add_argument("--out", "-o", default="artifacts/heatmap.png",
                        help="output PNG path (default: artifacts/heatmap.png)")
    parser.add_argument("--records-out", default=None,
                        help="record-age PNG path (default: <out>_records.png)")
    parser.add_argument("--mode", choices=("damage", "record", "both", "occluders", "penetration"),
                        default="both",
                        help="which image(s) to write (occluders = the world solids only; "
                             "penetration = the engine-model wallbang damage)")
    parser.add_argument("--fit", action="store_true",
                        help="crop to the cells' bounding box instead of the chart square")
    parser.add_argument("--occluders", action="store_true",
                        help="overlay the projected world solids on a damage render")
    parser.add_argument("--walls", action="store_true",
                        help="enumerate the world solids (engine query) and attenuate by penetration")
    parser.add_argument("--walls-call", action="store_true",
                        help="actually invoke the engine query (otherwise diagnostics only)")
    parser.add_argument("--pen", choices=("simple", "engine"), default=None,
                        help="penetration model for the occluded cells (docs/re/penetration.md): "
                             "'engine' = the real per-surface damage loss")
    parser.add_argument("--pen-mod", type=float, default=None,
                        help="per-surface penetration modifier for the engine model (default 1.0)")
    parser.add_argument("--dmg-mod", type=float, default=None,
                        help="per-surface damage modifier for the engine model (default 1.0)")
    parser.add_argument("--max-pen", type=int, default=None,
                        help="penetration budget (max walls crossed, default 4)")
    parser.add_argument("--no-sample", dest="sample", action="store_false",
                        help="disable the payload's continuous record sampling")
    parser.add_argument("--clear", action="store_true",
                        help="clear the record store before reading")
    parser.add_argument("--settle", type=float, default=1.0,
                        help="seconds to let records accrue after enabling sampling")
    parser.add_argument("--json", default=None, help="also write the raw heatmap JSON here")
    parser.add_argument("--pipe", default=None)
    args = parser.parse_args(argv)

    control = Control(args.pipe).connect(timeout=20.0)
    try:
        # Prime: set the sampling flag (and optionally clear), then let the render
        # thread accrue a window of records before reading the chart.
        control.heatmap(sample=args.sample, clear=args.clear)
        if args.sample:
            time.sleep(args.settle)
        if args.walls:
            # The world enumeration runs on the render thread; enable it, give it a
            # frame or two, then read the cached solids.
            control.heatmap(walls=True, walls_call=args.walls_call)
            time.sleep(0.6)
        result = control.heatmap(history=args.history, fov=args.fov, target=args.target,
                                 walls=args.walls, walls_call=args.walls_call,
                                 pen=args.pen, pen_mod=args.pen_mod,
                                 dmg_mod=args.dmg_mod, max_pen=args.max_pen)
    finally:
        control.close()

    if not result.get("valid"):
        print(f"heatmap unavailable: {result}", file=sys.stderr)
        return 1

    if args.json:
        import json
        path = pathlib.Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(result))
        print(f"[heatmap-view] wrote {args.json}")

    out = pathlib.Path(args.out)
    if args.mode in ("damage", "both"):
        stats = render(result, out, size=args.size, mode="damage", fit=args.fit,
                       occluders=args.occluders)
        print(f"[heatmap-view] damage  {out}  cells={stats['cells']} "
              f"pixels={stats['pixels']} value=[{stats.get('min', 0):.1f},{stats.get('max', 0):.1f}] "
              f"occluders={stats.get('occluders')}")
    if args.mode == "occluders":
        stats = render(result, out, size=args.size, mode="occluders", fit=args.fit)
        print(f"[heatmap-view] occluders {out}  occluders={stats.get('occluders')} "
              f"cells={stats['cells']}")
    if args.mode == "penetration":
        stats = render(result, out, size=args.size, mode="penetration", fit=args.fit)
        print(f"[heatmap-view] penetration {out}  cells={stats['cells']} "
              f"pixels={stats['pixels']} value=[{stats.get('min', 0):.1f},{stats.get('max', 0):.1f}] "
              f"occluders={stats.get('occluders')}")
    if args.mode in ("record", "both"):
        rec = pathlib.Path(args.records_out) if args.records_out else \
            out.with_name(f"{out.stem}_records{out.suffix}")
        stats = render(result, rec, size=args.size, mode="record", fit=args.fit)
        print(f"[heatmap-view] records {rec}  age=[{stats.get('min', 0):.0f},{stats.get('max', 0):.0f}]")

    targets = result.get("targets") or []
    print(f"[heatmap-view] enemies={len(targets)} cells={result.get('cell_count')} "
          f"extent={result.get('extent'):.3f} weapon={result.get('weapon')}")
    walls = result.get("walls") or {}
    if walls.get("enabled"):
        print(f"[heatmap-view] walls: called={walls.get('called')} ok={walls.get('ok')} "
              f"faulted={walls.get('faulted')} records={walls.get('records')} "
              f"shapes={walls.get('shapes')} hulls={walls.get('hulls')} "
              f"occluders={walls.get('occluders')} pen={walls.get('penetration_model')} "
              f"types={walls.get('type_counts')}")
        print(f"[heatmap-view] walls ptrs: iface={walls.get('iface')} world={walls.get('world')} "
              f"filter={walls.get('filter')} filter_vtable={walls.get('filter_vtable')}")
    for target in targets:
        print(f"  idx {target['index']:>3} {target.get('name', ''):<20} "
              f"hp {target.get('health'):>3} dist {target.get('distance'):.0f} "
              f"records {target.get('records')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
