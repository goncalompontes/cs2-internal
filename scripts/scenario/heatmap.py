"""Rasterise the payload's multi-target damage heatmap (``docs/ragebot.md``).

The payload's ``heatmap`` command returns the hitbox silhouettes of every player
— and every sampled lag record of every player — as convex polygons in one shared
angular chart, each tagged with its player, hitgroup and record. This module fills
them into an image and colourmaps the damage (or the record age), with a tiny
pure-stdlib PNG writer: no numpy / PIL / matplotlib.

The chart is the **absolute world yaw/pitch** of a direction from the eye (radians).
``u`` is yaw about world +Z, ``v`` is pitch above the XY plane. The render is centred
on the camera direction (``view_yaw``/``view_pitch``) and screen-right is *decreasing*
yaw (Source yaw increases to the left), so it matches the in-game view; the chart data
itself is absolute and does not move with the camera.

Usage (as a library):
    from scenario.heatmap import render
    stats = render(payload, Path("artifacts/heatmap.png"), mode="damage")
"""

from __future__ import annotations

import math
import pathlib
import struct
import zlib
from typing import Any, Callable, Mapping

# A compact 5-stop heat gradient (dark blue -> cyan -> green -> yellow -> red).
_STOPS: list[tuple[float, tuple[int, int, int]]] = [
    (0.00, (12, 18, 48)),
    (0.25, (24, 132, 168)),
    (0.50, (52, 190, 96)),
    (0.75, (232, 205, 64)),
    (1.00, (222, 56, 40)),
]


class Image:
    """A trivial RGB image with a zlib-backed PNG writer."""

    def __init__(self, width: int, height: int, background: tuple[int, int, int]) -> None:
        self.w = width
        self.h = height
        self.rgb = bytearray(bytes(background) * (width * height))

    def blend(self, x: int, y: int, color: tuple[int, int, int], alpha: float) -> None:
        if 0 <= x < self.w and 0 <= y < self.h and alpha > 0.0:
            i = (y * self.w + x) * 3
            a = min(1.0, alpha)
            self.rgb[i] = int(self.rgb[i] * (1.0 - a) + color[0] * a)
            self.rgb[i + 1] = int(self.rgb[i + 1] * (1.0 - a) + color[1] * a)
            self.rgb[i + 2] = int(self.rgb[i + 2] * (1.0 - a) + color[2] * a)

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


def _color(t: float) -> tuple[int, int, int]:
    """Map ``t`` in [0, 1] to an RGB along the heat gradient."""
    t = max(0.0, min(1.0, t))
    for i in range(len(_STOPS) - 1):
        t0, c0 = _STOPS[i]
        t1, c1 = _STOPS[i + 1]
        if t <= t1:
            f = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
            return (int(c0[0] + (c1[0] - c0[0]) * f),
                    int(c0[1] + (c1[1] - c0[1]) * f),
                    int(c0[2] + (c1[2] - c0[2]) * f))
    return _STOPS[-1][1]


def _attenuation_color(fraction: float) -> tuple[int, int, int]:
    """Map a penetration fraction (1 = no wall, 0 = fully blocked) to an RGB.

    Blue = the shot reaches the hitbox unobstructed, red = the walls removed the
    damage. Used to outline each cell in the ``penetration`` render mode.
    """
    t = max(0.0, min(1.0, fraction))
    cold = (70, 150, 255)
    warm = (235, 70, 60)
    return (int(cold[0] + (warm[0] - cold[0]) * (1.0 - t)),
            int(cold[1] + (warm[1] - cold[1]) * (1.0 - t)),
            int(cold[2] + (warm[2] - cold[2]) * (1.0 - t)))


def _extent(payload: Mapping[str, Any]) -> float:
    extent = float(payload.get("extent", 0.0) or 0.0)
    if extent > 0.0:
        return extent
    # Fall back to the cells' own bounds (e.g. a chart the payload did not clip).
    lo = hi = 0.0
    for cell in payload.get("cells", []):
        for u, v in cell.get("poly", []):
            lo = min(lo, u, v)
            hi = max(hi, u, v)
    return max(abs(lo), abs(hi)) or 1.0


def _fill(buffer: list[float], size: int, poly: list[tuple[float, float]], value: float,
          reduce: Callable[[float, float], float]) -> None:
    """Scanline-fill a convex polygon into ``buffer`` (row-major), reducing values."""
    n = len(poly)
    if n < 3:
        return
    ys = [p[1] for p in poly]
    y0 = max(0, int(math.floor(min(ys) - 0.5)))
    y1 = min(size - 1, int(math.ceil(max(ys) - 0.5)))
    for y in range(y0, y1 + 1):
        yc = y + 0.5
        xs: list[float] = []
        for i in range(n):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % n]
            if (ay <= yc < by) or (by <= yc < ay):
                xs.append(ax + (yc - ay) / (by - ay) * (bx - ax))
        xs.sort()
        base = y * size
        for k in range(0, len(xs) - 1, 2):
            x0 = max(0, int(math.ceil(xs[k] - 0.5)))
            x1 = min(size - 1, int(math.floor(xs[k + 1] - 0.5)))
            for x in range(x0, x1 + 1):
                idx = base + x
                buffer[idx] = reduce(buffer[idx], value)


def _fill_poly_color(image: Image, poly: list[tuple[float, float]],
                     color: tuple[int, int, int], alpha: float) -> None:
    """Scanline-fill a convex polygon with one translucent colour (no buffer)."""
    n = len(poly)
    if n < 3:
        return
    ys = [p[1] for p in poly]
    y0 = max(0, int(math.floor(min(ys) - 0.5)))
    y1 = min(image.h - 1, int(math.ceil(max(ys) - 0.5)))
    for y in range(y0, y1 + 1):
        yc = y + 0.5
        xs: list[float] = []
        for i in range(n):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % n]
            if (ay <= yc < by) or (by <= yc < ay):
                xs.append(ax + (yc - ay) / (by - ay) * (bx - ax))
        xs.sort()
        for k in range(0, len(xs) - 1, 2):
            x0 = max(0, int(math.ceil(xs[k] - 0.5)))
            x1 = min(image.w - 1, int(math.floor(xs[k + 1] - 0.5)))
            for x in range(x0, x1 + 1):
                image.blend(x, y, color, alpha)


def _line(image: Image, x0: float, y0: float, x1: float, y1: float,
          color: tuple[int, int, int], alpha: float) -> None:
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(steps + 1):
        f = i / steps if steps else 0.0
        image.blend(int(round(x0 + (x1 - x0) * f)), int(round(y0 + (y1 - y0) * f)), color, alpha)


def render(payload: Mapping[str, Any], path: pathlib.Path, *, size: int = 800,
           mode: str = "damage", outlines: bool = True, fit: bool = False,
           focus: int | None = None, occluders: bool = False,
           background: tuple[int, int, int] = (8, 10, 14)) -> dict[str, Any]:
    """Render a heatmap payload to ``path``.

    ``mode``: ``"damage"`` (max damage per pixel), ``"record"`` (the record age
    — live poses are 0, older records colder) or ``"occluders"`` (draw only the
    projected world solids returned in ``walls.occluder_polygons``, over the target
    outlines). ``"penetration"`` renders the damage the payload computed with the
    engine's real penetration model, overlays the solids, and outlines each cell by
    how much the walls attenuated it (blue = untouched, red = mostly removed). With
    ``fit`` the image crops to the cells' own bounding box (so a distant target is
    readable) instead of the whole chart square; ``focus`` restricts that box to one
    player's cells (and its record trail). ``occluders`` overlays the world solids on
    a damage/record render. Returns a stats dict.

    The payload is read, never written, so a mapping is enough — the wire-typed
    :class:`~scenario.protocol.HeatmapResult` is one.
    """
    extent = _extent(payload)
    cells = payload.get("cells", [])
    occluder_polys = (payload.get("walls") or {}).get("occluder_polygons", []) or []
    penetration = mode == "penetration"

    centre_u = float(payload.get("view_yaw", 0.0) or 0.0)
    centre_v = float(payload.get("view_pitch", 0.0) or 0.0)
    if fit:
        framed = [c for c in cells if focus is None or c.get("target") == focus] or cells
        us = [u for cell in framed for u, _ in cell.get("poly", [])]
        vs = [v for cell in framed for _, v in cell.get("poly", [])]
        if us:
            centre_u = (min(us) + max(us)) * 0.5
            centre_v = (min(vs) + max(vs)) * 0.5
            half = max(max(us) - min(us), max(vs) - min(vs)) * 0.5
            extent = half * 1.2 if half > 0.0 else extent
    scale = size / (2.0 * extent)

    def to_px(u: float, v: float) -> tuple[float, float]:
        # Absolute yaw/pitch -> pixels, centred on the camera direction. Screen-right is
        # *decreasing* yaw (Source yaw increases to the left). Unwrap yaw about the
        # centre so a window that straddles the +-pi seam renders continuously.
        du = u - centre_u
        while du > math.pi:
            du -= 2.0 * math.pi
        while du < -math.pi:
            du += 2.0 * math.pi
        return (size * 0.5 - du * scale, size * 0.5 - (v - centre_v) * scale)

    if mode == "record":
        buffer = [1.0e9] * (size * size)
        reduce: Callable[[float, float], float] = min
        key = "age"
    else:
        buffer = [-1.0] * (size * size)
        reduce = max
        key = "value"

    if mode != "occluders":
        for cell in cells:
            poly = [to_px(u, v) for u, v in cell.get("poly", [])]
            _fill(buffer, size, poly, float(cell.get(key, 0.0)), reduce)

    # Normalise the buffer to [0, 1] for the colourmap.
    present = [b for b in buffer if (b >= 0.0 if mode != "record" else b < 1.0e8)]

    image = Image(size, size, background)
    lo = hi = 0.0
    if present:
        lo, hi = min(present), max(present)
        span = (hi - lo) or 1.0
        for y in range(size):
            row = y * size
            for x in range(size):
                sample = buffer[row + x]
                if mode == "record":
                    if sample >= 1.0e8:
                        continue
                    t = (sample - lo) / span
                else:
                    if sample < 0.0:
                        continue
                    t = (sample - lo) / span
                color = _color(t)
                i = (row + x) * 3
                image.rgb[i] = color[0]
                image.rgb[i + 1] = color[1]
                image.rgb[i + 2] = color[2]

    # The projected world solids (occluders), under the target outlines.
    if occluders or mode == "occluders" or penetration:
        only = mode == "occluders"
        fill_alpha = 0.55 if only else (0.16 if penetration else 0.10)
        line_alpha = 0.95 if only else (0.45 if penetration else 0.30)
        for poly in [[to_px(u, v) for u, v in p] for p in occluder_polys]:
            _fill_poly_color(image, poly, (206, 118, 52), fill_alpha)
            for i in range(len(poly)):
                start = poly[i]
                end = poly[(i + 1) % len(poly)]
                _line(image, start[0], start[1], end[0], end[1],
                      (250, 170, 90), line_alpha)

    if not present and mode != "occluders":
        image.write_png(path)
        return {"cells": len(cells), "pixels": 0, "extent": extent, "mode": mode,
                "occluders": len(occluder_polys)}

    if outlines:
        for cell in cells:
            poly = [to_px(u, v) for u, v in cell.get("poly", [])]
            if penetration:
                # The attenuation the walls applied to this cell: the damage that
                # survived over the damage it would have dealt unobstructed.
                base_value = float(cell.get("base_value") or 0.0)
                cell_value = float(cell.get("value") or 0.0)
                fraction = cell_value / base_value if base_value > 0.0 else 1.0
                color = _attenuation_color(fraction)
                alpha = 0.55
            else:
                is_record = int(cell.get("record", -1)) >= 0
                color = (210, 90, 90) if is_record else (215, 215, 225)
                alpha = 0.16 if is_record else 0.22
            for i in range(len(poly)):
                start = poly[i]
                end = poly[(i + 1) % len(poly)]
                _line(image, start[0], start[1], end[0], end[1], color, alpha)

    # A crosshair at the view centre (the camera's forward / aim centre).
    cx, cy = to_px(centre_u, centre_v)
    _line(image, cx - 12, cy, cx + 12, cy, (120, 220, 255), 0.7)
    _line(image, cx, cy - 12, cx, cy + 12, (120, 220, 255), 0.7)
    image.write_png(path)

    return {"cells": len(cells), "pixels": len(present), "extent": extent, "mode": mode,
            "min": lo, "max": hi, "occluders": len(occluder_polys)}
