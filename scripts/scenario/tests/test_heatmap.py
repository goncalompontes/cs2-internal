"""Unit tests for the heatmap rasteriser (`scripts/scenario/heatmap.py`).

The payload answers with the hitbox silhouettes of every player as convex
polygons in one shared angular chart; this module fills them into an image and
colourmaps the damage — with a tiny standard-library PNG writer (no numpy, no
PIL). The render is pure arithmetic, so it can be checked pixel by pixel
without a game:

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import heatmap  # noqa: E402


def read_png(path: pathlib.Path) -> tuple[int, int, list[bytes]]:
    """The image's size and its scanlines (the test's own tiny PNG reader).

    The writer emits filter type 0 on every row, so a scanline is the raw
    RGB bytes.
    """
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("not a PNG")
    offset = 8
    width = height = 0
    compressed = bytearray()
    while offset + 8 <= len(data):
        (length,) = struct.unpack(">I", data[offset:offset + 4])
        tag = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if tag == b"IHDR":
            width, height = struct.unpack(">II", payload[:8])
        elif tag == b"IDAT":
            compressed.extend(payload)
        offset += 12 + length
    raw = zlib.decompress(bytes(compressed))
    stride = width * 3
    rows = [bytes(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
            for y in range(height)]
    return width, height, rows


def pixel(rows: list[bytes], x: int, y: int) -> tuple[int, int, int]:
    """One pixel of a decoded image."""
    row = rows[y]
    return (row[x * 3], row[x * 3 + 1], row[x * 3 + 2])


def cell(value: float, u0: float, u1: float, v0: float = -0.25,
         v1: float = 0.25, **extra: Any) -> dict[str, Any]:
    """One silhouette cell, as the payload writes it."""
    body: dict[str, Any] = {
        "poly": [[u0, v0], [u1, v0], [u1, v1], [u0, v1]],
        "value": value,
        "base_value": value,
        "target": 1,
        "hitgroup": 1,
        "record": -1,
        "age": 0.0,
    }
    body.update(extra)
    return body


class TestImage(unittest.TestCase):
    """The image buffer and its PNG writer."""

    def test_the_buffer_starts_at_the_background(self) -> None:
        image = heatmap.Image(2, 2, (1, 2, 3))
        self.assertEqual(bytes(image.rgb), bytes((1, 2, 3)) * 4)

    def test_a_blend_mixes_the_colour(self) -> None:
        image = heatmap.Image(1, 1, (0, 0, 0))
        image.blend(0, 0, (100, 100, 100), 0.5)
        self.assertEqual(bytes(image.rgb), bytes((50, 50, 50)))

    def test_a_full_blend_replaces_the_colour(self) -> None:
        image = heatmap.Image(1, 1, (0, 0, 0))
        image.blend(0, 0, (7, 8, 9), 1.0)
        self.assertEqual(bytes(image.rgb), bytes((7, 8, 9)))

    def test_a_transparent_blend_changes_nothing(self) -> None:
        image = heatmap.Image(1, 1, (0, 0, 0))
        image.blend(0, 0, (7, 8, 9), 0.0)
        self.assertEqual(bytes(image.rgb), bytes((0, 0, 0)))

    def test_a_blend_outside_the_image_is_ignored(self) -> None:
        image = heatmap.Image(1, 1, (0, 0, 0))
        image.blend(-1, 0, (7, 8, 9), 1.0)
        image.blend(0, 5, (7, 8, 9), 1.0)
        self.assertEqual(bytes(image.rgb), bytes((0, 0, 0)))

    def test_the_png_carries_the_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "nested" / "image.png"
            image = heatmap.Image(3, 2, (4, 5, 6))
            image.blend(1, 1, (200, 100, 50), 1.0)
            image.write_png(path)
            width, height, rows = read_png(path)
            self.assertEqual((width, height), (3, 2))
            self.assertEqual(pixel(rows, 1, 1), (200, 100, 50))
            self.assertEqual(pixel(rows, 0, 0), (4, 5, 6))


class TestColourMap(unittest.TestCase):
    """The heat gradient."""

    def test_the_cold_end_is_the_first_stop(self) -> None:
        self.assertEqual(heatmap._color(0.0), (12, 18, 48))

    def test_the_hot_end_is_the_last_stop(self) -> None:
        self.assertEqual(heatmap._color(1.0), (222, 56, 40))

    def test_the_gradient_is_clamped(self) -> None:
        self.assertEqual(heatmap._color(-5.0), heatmap._color(0.0))
        self.assertEqual(heatmap._color(5.0), heatmap._color(1.0))

    def test_the_middle_is_between_the_stops(self) -> None:
        middle = heatmap._color(0.5)
        self.assertNotEqual(middle, heatmap._color(0.0))
        self.assertNotEqual(middle, heatmap._color(1.0))

    def test_the_attenuation_colour_runs_from_blue_to_red(self) -> None:
        """Blue = the shot reaches the hitbox; red = the walls ate it."""
        self.assertEqual(heatmap._attenuation_color(1.0), (70, 150, 255))
        self.assertEqual(heatmap._attenuation_color(0.0), (235, 70, 60))


class TestExtent(unittest.TestCase):
    """How big the chart is."""

    def test_the_payloads_extent_wins(self) -> None:
        self.assertEqual(heatmap._extent({"extent": 3.0}), 3.0)

    def test_the_cells_bounds_are_the_fallback(self) -> None:
        payload = {"cells": [{"poly": [[-1.0, -2.0], [3.0, 1.0]]}]}
        self.assertEqual(heatmap._extent(payload), 3.0)

    def test_an_empty_chart_is_one_unit(self) -> None:
        self.assertEqual(heatmap._extent({}), 1.0)


class TestFill(unittest.TestCase):
    """The scanline fill."""

    def test_a_square_fills_its_pixels(self) -> None:
        buffer = [-1.0] * 16
        heatmap._fill(buffer, 4, [(1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)],
                      5.0, max)
        self.assertEqual(buffer[1 * 4 + 2], 5.0)
        self.assertEqual(buffer[2 * 4 + 1], 5.0)
        self.assertEqual(buffer[0], -1.0)

    def test_a_polygon_with_fewer_than_three_points_is_ignored(self) -> None:
        buffer = [-1.0] * 4
        heatmap._fill(buffer, 2, [(0.0, 0.0), (1.0, 1.0)], 5.0, max)
        self.assertEqual(buffer, [-1.0] * 4)


class TestRender(unittest.TestCase):
    """The render itself — the pixels it writes."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = pathlib.Path(self.directory.name) / "heatmap.png"

    def payload(self, cells: list[dict[str, Any]], **extra: Any) -> dict[str, Any]:
        body: dict[str, Any] = {
            "extent": 2.0,
            "view_yaw": 0.0,
            "view_pitch": 0.0,
            "cells": cells,
        }
        body.update(extra)
        return body

    def test_the_damage_is_colourmapped_onto_the_cells(self) -> None:
        """The cold cell is the first stop, the hot one the last."""
        payload = self.payload([cell(0.0, -1.5, -1.0), cell(1.0, 1.0, 1.5)])
        stats = heatmap.render(payload, self.path, size=64)
        _, _, rows = read_png(self.path)
        self.assertEqual(pixel(rows, 52, 32), heatmap._color(0.0))
        self.assertEqual(pixel(rows, 12, 32), heatmap._color(1.0))

    def test_the_background_is_left_alone(self) -> None:
        payload = self.payload([cell(1.0, -0.5, 0.5)])
        heatmap.render(payload, self.path, size=64)
        _, _, rows = read_png(self.path)
        self.assertEqual(pixel(rows, 0, 0), (8, 10, 14))

    def test_the_stats_report_what_was_drawn(self) -> None:
        payload = self.payload([cell(0.0, -1.5, -1.0), cell(1.0, 1.0, 1.5)])
        stats = heatmap.render(payload, self.path, size=64)
        self.assertEqual(stats["cells"], 2)
        self.assertEqual(stats["mode"], "damage")
        self.assertEqual(stats["min"], 0.0)
        self.assertEqual(stats["max"], 1.0)
        self.assertGreater(stats["pixels"], 0)

    def test_an_empty_chart_writes_the_background(self) -> None:
        payload = self.payload([])
        stats = heatmap.render(payload, self.path, size=32)
        self.assertEqual(stats["pixels"], 0)
        _, _, rows = read_png(self.path)
        self.assertEqual(pixel(rows, 0, 0), (8, 10, 14))

    def test_the_record_mode_uses_the_age(self) -> None:
        """The record render shows the age of the pose, not the damage."""
        payload = self.payload([cell(0.0, -0.5, 0.5, age=4.0, record=3)])
        stats = heatmap.render(payload, self.path, size=64, mode="record")
        self.assertEqual(stats["mode"], "record")
        self.assertEqual(stats["max"], 4.0)

    def test_the_occluder_polygons_are_drawn(self) -> None:
        """The world solids are overlaid on the target outlines."""
        walls = {"occluder_polygons": [[[-0.2, -0.2], [0.2, -0.2], [0.2, 0.2],
                                        [-0.2, 0.2]]]}
        payload = self.payload([cell(1.0, -0.5, 0.5)], walls=walls)
        stats = heatmap.render(payload, self.path, size=64, occluders=True)
        self.assertEqual(stats["occluders"], 1)
        _, _, rows = read_png(self.path)
        self.assertNotEqual(pixel(rows, 32, 32), (8, 10, 14))

    def test_the_penetration_mode_outlines_by_attenuation(self) -> None:
        """A cell the walls halved is outlined in red, an untouched one in blue.

        The chart is 2 units wide over 64 pixels, so a cell's vertical edge
        sits at a known pixel: the halved cell's at x=40, the untouched
        cell's at x=24.
        """
        cells = [cell(0.0, -0.5, 0.0, base_value=10.0),
                 cell(10.0, 0.0, 0.5, base_value=10.0)]
        payload = self.payload(cells)
        heatmap.render(payload, self.path, size=64, mode="penetration")
        _, _, rows = read_png(self.path)
        # The outline is translucent, so the test asks for the *hue*: the
        # halved cell's edge is red-ish, the untouched cell's edge is blue-ish.
        # The row is off the crosshair (which is drawn through the centre).
        halved = pixel(rows, 40, 30)
        untouched = pixel(rows, 24, 30)
        self.assertGreater(halved[0], halved[2])
        self.assertGreater(untouched[2], untouched[0])

    def test_the_fit_crops_to_the_cells(self) -> None:
        """``fit`` makes a distant target readable."""
        payload = self.payload([cell(1.0, 1.0, 1.2)])
        stats = heatmap.render(payload, self.path, size=64, fit=True)
        self.assertLess(stats["extent"], 2.0)

    def test_a_focus_restricts_the_frame_to_one_target(self) -> None:
        cells = [cell(1.0, -0.5, 0.0, target=1), cell(1.0, 1.0, 1.5, target=2)]
        payload = self.payload(cells)
        stats = heatmap.render(payload, self.path, size=64, fit=True, focus=2)
        self.assertLess(stats["extent"], 2.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
