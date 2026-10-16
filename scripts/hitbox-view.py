#!/usr/bin/env python3
"""Extract one player's studio hitboxes and render them as real 3D capsules.

Pulls the live hitbox set for a single player through the payload's ``aim``
command (bone-local endpoints + the world endpoints + the bone transform +
radius), rebases everything on the player's origin, dumps the raw JSON, and
rasterises each hitbox as an **actual capsule mesh** (a cylinder with two
hemispherical caps) with a small pure-Python rasteriser: per-vertex normals,
Gouraud shading, a depth buffer. No dependencies — just zlib for the PNG.

``--style solid`` (default) fills the meshes; ``--style wire`` draws the mesh as
a wireframe (cap/rim rings + longitudinal lines, back-facing edges dimmed). The
point is to validate the *geometry* out of process: if these capsules look
right, the coordinates are right and any in-game mismatch is the overlay's draw.

Usage:
  python scripts/hitbox-view.py                                  # nearest live enemy
  python scripts/hitbox-view.py --style wire -o artifacts/wire.png
  python scripts/hitbox-view.py --index 12
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

# Hitgroup -> display colour (RGB).
GROUP_COLORS: dict[int, tuple[int, int, int]] = {
    1: (235, 74, 74),     # head
    2: (86, 205, 128),    # chest
    3: (88, 170, 235),    # stomach / pelvis
    4: (232, 200, 84),    # left arm
    5: (236, 150, 62),    # right arm
    6: (168, 128, 240),   # left leg
    7: (138, 100, 220),   # right leg
    8: (205, 205, 205),   # neck
}

SEGMENTS = 20     # around the axis
CAP_RINGS = 4     # hemisphere subdivisions


# --- tiny PNG writer ------------------------------------------------------
class Image:
    def __init__(self, width: int, height: int) -> None:
        self.w = width
        self.h = height
        self.rgb = bytearray(width * height * 3)
        self.zbuf = [1e30] * (width * height)

    def clear(self, color: tuple[int, int, int]) -> None:
        self.rgb = bytearray(bytes(color) * (self.w * self.h))
        self.zbuf = [1e30] * (self.w * self.h)

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
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(png)


# --- vector maths ---------------------------------------------------------
def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    length = math.sqrt(vdot(a, a))
    return (a[0] / length, a[1] / length, a[2] / length) if length > 1e-12 else (0.0, 0.0, 1.0)


# --- capsule mesh ---------------------------------------------------------
def capsule_rings(a, b, radius: float):
    """The capsule's rings, bottom pole -> top pole; each a list of ``(pos, n)``.

    A capsule is the Minkowski sum of the segment ``a``-``b`` and a sphere of
    ``radius``: a cylinder of that radius with two hemispherical caps — exactly
    the shape the engine stores per hitbox (``m_vMinBounds`` / ``m_vMaxBounds``
    endpoints + ``m_flShapeRadius``).
    """
    if math.sqrt(vdot(vsub(b, a), vsub(b, a))) < 1e-4:
        b = vadd(a, (0.0, 0.0, 1.0))  # degenerate: a unit-length capsule
    n = vnorm(vsub(b, a))
    ref = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = vnorm(vcross(n, ref))
    v = vcross(n, u)

    def ring(center, ring_radius, normal_from):
        points = []
        for i in range(SEGMENTS):
            theta = 2.0 * math.pi * i / SEGMENTS
            radial = vadd(vmul(u, math.cos(theta)), vmul(v, math.sin(theta)))
            position = vadd(center, vmul(radial, ring_radius))
            points.append((position, vnorm(vsub(position, normal_from))))
        return points

    rings = [ring(vadd(a, vmul(n, -radius)), 0.0, a)]
    for k in range(1, CAP_RINGS + 1):
        phi = (math.pi / 2.0) * k / CAP_RINGS
        rings.append(ring(vadd(a, vmul(n, -radius * math.cos(phi))), radius * math.sin(phi), a))
    for k in range(CAP_RINGS, 0, -1):
        phi = (math.pi / 2.0) * k / CAP_RINGS
        rings.append(ring(vadd(b, vmul(n, radius * math.cos(phi))), radius * math.sin(phi), b))
    rings.append(ring(vadd(b, vmul(n, radius)), 0.0, b))
    return rings


def capsule_mesh(a, b, radius: float):
    """Triangles ``(p0, p1, p2, n0, n1, n2)`` for a closed capsule mesh."""
    rings = capsule_rings(a, b, radius)
    triangles = []
    for i in range(len(rings) - 1):
        lo, hi = rings[i], rings[i + 1]
        for j in range(SEGMENTS):
            k = (j + 1) % SEGMENTS
            a0, n0 = lo[j]
            a1, n1 = lo[k]
            b0, n2 = hi[j]
            b1, n3 = hi[k]
            triangles.append((a0, b0, b1, n0, n2, n3))
            triangles.append((a0, b1, a1, n0, n3, n1))
    return triangles


# --- rasteriser -----------------------------------------------------------
class Camera:
    def __init__(self, image: Image, x0: int, y0: int, w: int, h: int,
                 eye, target, fov_deg: float = 45.0) -> None:
        self.img = image
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.eye = eye
        self.f = vnorm(vsub(target, eye))
        self.r = vnorm(vcross(self.f, (0.0, 0.0, 1.0)))
        self.u = vcross(self.r, self.f)
        self.focal = (h * 0.5) / math.tan(math.radians(fov_deg) * 0.5)
        # Light, in camera space: upper-left, slightly toward the eye.
        self.light = vnorm(vadd(vadd(vmul(self.r, -0.40), vmul(self.u, 0.50)),
                                vmul(self.f, -0.77)))

    def to_camera(self, p):
        d = vsub(p, self.eye)
        return (vdot(d, self.r), vdot(d, self.u), vdot(d, self.f))

    def project(self, p):
        cx, cy, depth = self.to_camera(p)
        if depth <= 0.05:
            return None
        return (self.x0 + self.w * 0.5 + cx * self.focal / depth,
                self.y0 + self.h * 0.5 - cy * self.focal / depth,
                depth)

    def line(self, a, b, color, steps: int = 0) -> None:
        pa, pb = self.project(a), self.project(b)
        if pa is None or pb is None:
            return
        if steps <= 0:
            steps = max(2, min(64, int(math.hypot(pb[0] - pa[0], pb[1] - pa[1]))))
        for i in range(steps + 1):
            t = i / steps
            x = int(round(pa[0] + (pb[0] - pa[0]) * t))
            y = int(round(pa[1] + (pb[1] - pa[1]) * t))
            if self.x0 <= x < self.x0 + self.w and self.y0 <= y < self.y0 + self.h:
                idx = (y * self.img.w + x) * 3
                self.img.rgb[idx:idx + 3] = bytes(color)

    def triangle(self, p0, p1, p2, n0, n1, n2, color) -> None:
        """Gouraud-shaded, z-buffered triangle."""
        c0, c1, c2 = self.project(p0), self.project(p1), self.project(p2)
        if c0 is None or c1 is None or c2 is None:
            return
        area = (c1[0] - c0[0]) * (c2[1] - c0[1]) - (c1[1] - c0[1]) * (c2[0] - c0[0])
        if area == 0.0:
            return
        inv_area = 1.0 / area
        w0, w1, w2 = 1.0 / c0[2], 1.0 / c1[2], 1.0 / c2[2]
        min_x = max(self.x0, int(min(c0[0], c1[0], c2[0])))
        max_x = min(self.x0 + self.w - 1, int(max(c0[0], c1[0], c2[0])) + 1)
        min_y = max(self.y0, int(min(c0[1], c1[1], c2[1])))
        max_y = min(self.y0 + self.h - 1, int(max(c0[1], c1[1], c2[1])) + 1)
        img, zbuf, light = self.img, self.img.zbuf, self.light
        base = vmul(color, 1.0 / 255.0)
        for py in range(min_y, max_y + 1):
            dy0 = py - c0[1]
            row = py * img.w
            for px in range(min_x, max_x + 1):
                dx0 = px - c0[0]
                l1 = (dx0 * (c2[1] - c0[1]) - dy0 * (c2[0] - c0[0])) * inv_area
                l2 = (dy0 * (c1[0] - c0[0]) - dx0 * (c1[1] - c0[1])) * inv_area
                l0 = 1.0 - l1 - l2
                if l0 < 0.0 or l1 < 0.0 or l2 < 0.0:
                    continue
                depth = 1.0 / (l0 * w0 + l1 * w1 + l2 * w2)
                index = row + px
                if depth >= zbuf[index]:
                    continue
                nx = l0 * n0[0] + l1 * n1[0] + l2 * n2[0]
                ny = l0 * n0[1] + l1 * n1[1] + l2 * n2[1]
                nz = l0 * n0[2] + l1 * n1[2] + l2 * n2[2]
                mag = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                nx, ny, nz = nx / mag, ny / mag, nz / mag
                if nx * self.f[0] + ny * self.f[1] + nz * self.f[2] > 0.0:
                    nx, ny, nz = -nx, -ny, -nz  # two-sided: flip toward the eye
                lambert = nx * light[0] + ny * light[1] + nz * light[2]
                if lambert < 0.0:
                    lambert = 0.0
                shade = 0.30 + 0.70 * lambert
                zbuf[index] = depth
                idx = index * 3
                img.rgb[idx] = int(base[0] * shade * 255.0)
                img.rgb[idx + 1] = int(base[1] * shade * 255.0)
                img.rgb[idx + 2] = int(base[2] * shade * 255.0)

    def capsule(self, a, b, radius, color) -> None:
        for p0, p1, p2, n0, n1, n2 in capsule_mesh(a, b, radius):
            self.triangle(p0, p1, p2, n0, n1, n2, color)

    def wire_capsule(self, a, b, radius, color) -> None:
        """The capsule mesh as a wireframe: the cap/rim rings plus longitudinal
        lines, with back-facing edges dimmed and lit by the same light."""
        rings = capsule_rings(a, b, radius)

        def edge(p, np_, q, nq):
            facing = vdot(np_, self.f) + vdot(nq, self.f)
            lambert = max(0.0, (vdot(np_, self.light) + vdot(nq, self.light)) * 0.5)
            shade = (0.34 + 0.66 * lambert) * (0.42 if facing > 0.0 else 1.0)
            self.line(p, q, (int(color[0] * shade), int(color[1] * shade),
                              int(color[2] * shade)))

        for ring_pts in rings:
            for j in range(SEGMENTS):
                k = (j + 1) % SEGMENTS
                edge(ring_pts[j][0], ring_pts[j][1], ring_pts[k][0], ring_pts[k][1])
        for i in range(len(rings) - 1):
            for j in range(0, SEGMENTS, 2):
                edge(rings[i][j][0], rings[i][j][1], rings[i + 1][j][0], rings[i + 1][j][1])


# --- scene ----------------------------------------------------------------
def render(payload: dict, out: pathlib.Path, style: str = "solid") -> None:
    origin = payload["origin"]
    boxes = payload["hitboxes"]
    yaw = math.radians(payload.get("eye_angles", {}).get("yaw", 0.0))
    fwd = (math.cos(yaw), math.sin(yaw), 0.0)
    right = (-math.sin(yaw), math.cos(yaw), 0.0)

    def rel(p):
        return (p[0] - origin["x"], p[1] - origin["y"], p[2] - origin["z"])

    center = (0.0, 0.0, 36.0)
    dist = 145.0
    views = [
        ("front", vadd(center, vmul(fwd, dist))),
        ("back", vadd(center, vmul(fwd, -dist))),
        ("left", vadd(center, vmul(right, dist))),
        ("right", vadd(center, vmul(right, -dist))),
    ]
    width, height = 560, 560
    image = Image(width * 2, height * 2)
    image.clear((15, 15, 19))

    draw = "wire_capsule" if style == "wire" else "capsule"
    for index, (_name, eye) in enumerate(views):
        camera = Camera(image, (index % 2) * width, (index // 2) * height, width, height,
                        eye, center)
        for g in range(-80, 81, 20):
            camera.line((g, -80, 0.0), (g, 80, 0.0), (44, 44, 52), steps=2)
            camera.line((-80, g, 0.0), (80, g, 0.0), (44, 44, 52), steps=2)
        camera.line((0, 0, 0), (0, 0, 62), (70, 70, 110))       # z axis
        camera.line((0, 0, 0), (24, 0, 0), (140, 60, 60))       # x axis
        camera.line((0, 0, 0), (0, 24, 0), (60, 140, 60))       # y axis
        camera.line((0, 0, 62), vadd((0, 0, 62), vmul(fwd, 25)), (235, 235, 120))

        for box in boxes:
            color = GROUP_COLORS.get(int(box["group"]), (200, 200, 200))
            getattr(camera, draw)(rel((box["a_x"], box["a_y"], box["a_z"])),
                                  rel((box["b_x"], box["b_y"], box["b_z"])),
                                  max(float(box["radius"]), 0.5), color)
        for box in boxes:  # bone origins
            bone = rel((box["bone_x"], box["bone_y"], box["bone_z"]))
            getattr(camera, draw)(bone, bone, 0.9, (255, 255, 255))

    image.write_png(out)
    print(f"[hitbox-view] wrote {out} ({out.stat().st_size} bytes)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--index", type=int, default=None, help="target entity index")
    parser.add_argument("--out", "-o", default="artifacts/hitboxes.png",
                        help="output PNG path (default: artifacts/hitboxes.png)")
    parser.add_argument("--style", choices=("solid", "wire"), default="solid",
                        help="solid meshes (default) or a wireframe")
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
        path = pathlib.Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2))
        print(f"[hitbox-view] wrote {args.json}")

    origin = payload["origin"]
    print(f"target {payload['name']} (idx {payload['index']}) "
          f"boxes={len(boxes)} origin=({origin['x']:.1f},{origin['y']:.1f},{origin['z']:.1f})"
          f" style={args.style}")
    print(f"{'name':<13}{'bone':>5}{'grp':>4}{'rad':>6}   A(rel) -> B(rel)")
    for box in boxes:
        a = (box["a_x"] - origin["x"], box["a_y"] - origin["y"], box["a_z"] - origin["z"])
        b = (box["b_x"] - origin["x"], box["b_y"] - origin["y"], box["b_z"] - origin["z"])
        print(f"{box['name']:<13}{box['bone']:>5}{box['group']:>4}{box['radius']:>6.2f}   "
              f"({a[0]:7.2f},{a[1]:7.2f},{a[2]:7.2f}) -> ({b[0]:7.2f},{b[1]:7.2f},{b[2]:7.2f})")

    render(payload, pathlib.Path(args.out), args.style)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
