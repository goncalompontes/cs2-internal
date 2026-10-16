#!/usr/bin/env python3
"""Extract one player's studio hitboxes and render them in 3D (no dependencies).

The payload's ``aim`` command already reports each hitbox's bone-local endpoints,
its bone transform and (from this build) the **world** capsule endpoints. This
tool pulls those for a single live player, rebases them on the player's origin,
dumps the raw JSON, and rasterises the capsules into a PNG montage (four
elevations) with a tiny pure-Python renderer — so the hitbox *coordinates* can be
judged independently of the in-game overlay's drawing.

Usage:
  python scripts/hitbox-view.py                     # nearest live enemy, all views
  python scripts/hitbox-view.py --index 12
  python scripts/hitbox-view.py --out hit.png --json dump.json
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from scenario.control import Control  # noqa: E402

# Hitgroup -> display colour (RGB). Mirrors the payload's own hitgroup ids.
GROUP_COLORS: dict[int, tuple[int, int, int]] = {
    1: (235, 70, 70),     # head
    2: (90, 210, 130),    # chest
    3: (90, 170, 235),    # stomach / pelvis
    4: (230, 200, 80),    # left arm
    5: (235, 150, 60),    # right arm
    6: (170, 130, 240),   # left leg
    7: (140, 100, 220),   # right leg
    8: (200, 200, 200),   # neck
}


# --- tiny PNG writer + rasteriser ----------------------------------------
class Image:
    def __init__(self, width: int, height: int) -> None:
        self.w = width
        self.h = height
        self.rgb = bytearray(width * height * 3)
        self.zbuf = [1e30] * (width * height)

    def clear(self, color: tuple[int, int, int]) -> None:
        self.rgb = bytearray(bytes(color) * (self.w * self.h))
        self.zbuf = [1e30] * (self.w * self.h)

    def pixel(self, x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.rgb[i] = color[0]
            self.rgb[i + 1] = color[1]
            self.rgb[i + 2] = color[2]

    def write_png(self, path: pathlib.Path) -> None:
        raw = bytearray()
        stride = self.w * 3
        for y in range(self.h):
            raw.append(0)  # filter: none
            raw += self.rgb[y * stride:(y + 1) * stride]

        def chunk(tag: bytes, data: bytes) -> bytes:
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        png += chunk(b"IEND", b"")
        path.write_bytes(png)


# --- 3D maths -------------------------------------------------------------
def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / length, a[1] / length, a[2] / length)


class View:
    """A pinhole camera rendering into a sub-rectangle of the image."""

    def __init__(self, image: Image, x0: int, y0: int, w: int, h: int,
                 eye, target, fov_deg: float = 45.0) -> None:
        self.img = image
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.eye = eye
        forward = norm(sub(target, eye))
        self.f = forward
        right = norm(cross(forward, (0.0, 0.0, 1.0)))
        self.r = right
        self.u = cross(right, forward)
        self.focal = (h * 0.5) / math.tan(math.radians(fov_deg) * 0.5)

    def project(self, p):
        v = sub(p, self.eye)
        depth = dot(v, self.f)
        if depth <= 0.05:
            return None
        x = dot(v, self.r)
        y = dot(v, self.u)
        return (self.x0 + self.w * 0.5 + x * self.focal / depth,
                self.y0 + self.h * 0.5 - y * self.focal / depth,
                depth)

    def line(self, a, b, color, steps: int = 96) -> None:
        pa, pb = self.project(a), self.project(b)
        if pa is None or pb is None:
            return
        for i in range(steps + 1):
            t = i / steps
            self.img.pixel(int(round(pa[0] + (pb[0] - pa[0]) * t)),
                           int(round(pa[1] + (pb[1] - pa[1]) * t)), color)

    def sphere(self, center, radius: float, color) -> None:
        """A shaded, z-buffered sphere."""
        p = self.project(center)
        if p is None:
            return
        cx, cy, depth = p
        sr = radius * self.focal / depth
        if sr < 0.6:
            return
        inv = 1.0 / self.focal
        for py in range(max(self.y0, int(cy - sr)), min(self.y0 + self.h, int(cy + sr) + 1)):
            for px in range(max(self.x0, int(cx - sr)), min(self.x0 + self.w, int(cx + sr) + 1)):
                dx = (px - cx) * depth * inv
                dy = (cy - py) * depth * inv
                r2 = dx * dx + dy * dy
                if r2 > radius * radius:
                    continue
                dz = math.sqrt(radius * radius - r2)
                z = depth - dz
                idx = py * self.img.w + px
                if z >= self.img.zbuf[idx]:
                    continue
                nx, ny, nz = dx / radius, dy / radius, dz / radius
                shade = 0.28 + 0.62 * nz + 0.20 * ny
                shade = 0.0 if shade < 0.0 else (1.0 if shade > 1.0 else shade)
                self.img.zbuf[idx] = z
                self.img.pixel(px, py, (int(color[0] * shade), int(color[1] * shade),
                                        int(color[2] * shade)))

    def capsule(self, a, b, radius: float, color) -> None:
        axis = sub(b, a)
        length = math.sqrt(dot(axis, axis))
        steps = max(2, int(length / max(radius * 0.5, 0.5)) + 1)
        for i in range(steps + 1):
            t = i / steps
            self.sphere(add(a, mul(axis, t)), radius, color)


def render(payload: dict, out: pathlib.Path) -> None:
    origin = payload["origin"]
    boxes = payload["hitboxes"]
    yaw = math.radians(payload.get("eye_angles", {}).get("yaw", 0.0))
    fwd = (math.cos(yaw), math.sin(yaw), 0.0)
    right = (-math.sin(yaw), math.cos(yaw), 0.0)

    # Rebase everything on the player's origin (feet).
    def rel(p):
        return (p[0] - origin["x"], p[1] - origin["y"], p[2] - origin["z"])

    center = (0.0, 0.0, 36.0)
    dist = 150.0
    views = [
        ("front", add(center, mul(fwd, dist))),
        ("back", add(center, mul(fwd, -dist))),
        ("left", add(center, mul(right, dist))),
        ("right", add(center, mul(right, -dist))),
    ]
    width, height = 560, 560
    image = Image(width * 2, height * 2)
    image.clear((16, 16, 20))

    for index, (name, eye) in enumerate(views):
        vx = (index % 2) * width
        vy = (index // 2) * height
        view = View(image, vx, vy, width, height, eye, center)

        # Ground grid + origin frame (drawn first, no depth).
        for g in range(-80, 81, 20):
            view.line((g, -80, 0.0), (g, 80, 0.0), (40, 40, 48), steps=2)
            view.line((-80, g, 0.0), (80, g, 0.0), (40, 40, 48), steps=2)
        view.line((0, 0, 0), (0, 0, 60), (70, 70, 110))       # z axis
        view.line((0, 0, 0), (24, 0, 0), (140, 60, 60))       # x axis
        view.line((0, 0, 0), (0, 24, 0), (60, 140, 60))       # y axis
        # Heading marker: where the player is looking.
        view.line((0, 0, 60), add((0, 0, 60), mul(fwd, 25)), (240, 240, 120))

        for box in boxes:
            color = GROUP_COLORS.get(int(box["group"]), (200, 200, 200))
            a = rel((box["a_x"], box["a_y"], box["a_z"]))
            b = rel((box["b_x"], box["b_y"], box["b_z"]))
            view.capsule(a, b, max(box["radius"], 0.4), color)

        # Bone origins (small dark markers) — the raw skeleton the boxes ride.
        for box in boxes:
            view.sphere(rel((box["bone_x"], box["bone_y"], box["bone_z"])), 0.9, (255, 255, 255))

    out.parent.mkdir(parents=True, exist_ok=True)
    image.write_png(out)
    print(f"[hitbox-view] wrote {out} ({out.stat().st_size} bytes)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--index", type=int, default=None, help="target entity index")
    parser.add_argument("--out", default="artifacts/hitboxes.png",
                        help="output PNG path (default: artifacts/hitboxes.png)")
    parser.add_argument("--json", default=None, help="also write the raw hitbox JSON here")
    parser.add_argument("--pipe", default=None)
    args = parser.parse_args(argv)

    control = Control(args.pipe).connect(timeout=20.0)
    try:
        state = control.state()
        players = state.get("players") or []
        local = state.get("local") or {}
        team = local.get("team")
        enemies = [p for p in players if p.get("team") in (2, 3)
                   and p.get("team") != team and p.get("alive")]
        if args.index is not None:
            enemies = [p for p in players if p.get("index") == args.index]
        if not enemies:
            print("no live enemy to dump", file=sys.stderr)
            return 1
        target = enemies[0]
        result = control.aim(target=target["index"])
    finally:
        control.close()

    boxes = result.get("hitboxes") or []
    payload = {
        "index": target["index"],
        "name": target.get("name"),
        "origin": target["origin"],
        "eye_origin": target.get("eye_origin"),
        "eye_angles": target.get("eye_angles"),
        "hitcount": result.get("hitcount"),
        "hitboxes": boxes,
    }
    if args.json:
        pathlib.Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(args.json).write_text(json.dumps(payload, indent=2))
        print(f"[hitbox-view] wrote {args.json}")

    origin = payload["origin"]
    print(f"target {payload['name']} (idx {payload['index']}) "
          f"boxes={len(boxes)} origin=({origin['x']:.1f},{origin['y']:.1f},{origin['z']:.1f})")
    print(f"{'name':<13}{'bone':>5}{'mesh':>5}{'grp':>4}{'rad':>7}   "
          f"A(rel) -> B(rel)")
    for box in boxes:
        a = (box["a_x"] - origin["x"], box["a_y"] - origin["y"], box["a_z"] - origin["z"])
        b = (box["b_x"] - origin["x"], box["b_y"] - origin["y"], box["b_z"] - origin["z"])
        print(f"{box['name']:<13}{box['bone']:>5}{box['mesh']:>5}{box['group']:>4}"
              f"{box['radius']:>7.2f}   "
              f"({a[0]:7.2f},{a[1]:7.2f},{a[2]:7.2f}) -> ({b[0]:7.2f},{b[1]:7.2f},{b[2]:7.2f})")

    render(payload, pathlib.Path(args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
