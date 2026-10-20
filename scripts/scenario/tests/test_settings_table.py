"""Unit tests for the settings name table's generator.

``scripts/gen-settings-table.py`` parses ``wire.hpp`` and renders the
name -> offset table the control channel's ``settings`` command addresses the
feature settings by.  The table is **checked in** and the build verifies it
(``meson.build`` runs the generator in ``--check`` mode), so the generator is
part of the contract: a parse that silently produces a shorter table would
make every setting after the gap unknown.

These tests are **independent of the build**: they drive the generator's own
functions and its command line, so they fail even in a run that regenerates
the file first (the bug the Meson test could not catch).

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest
from typing import Any

#: The repository root (``scripts/scenario/tests`` -> ``scripts`` -> root).
ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The generator (its name is not a module name, so it is loaded by path).
GENERATOR = ROOT / "scripts" / "gen-settings-table.py"

#: The two files the generator works on.
WIRE = ROOT / "overlay" / "include" / "cs2" / "overlay" / "wire.hpp"
TABLE = ROOT / "overlay" / "include" / "cs2" / "overlay" / "settings_table.hpp"


def _load_generator() -> Any:
    """The generator module, loaded from its path.

    The module is loaded dynamically (its file name is not importable), so
    its functions are reached through ``Any``: the checker cannot see them.
    """
    spec = importlib.util.spec_from_file_location("gen_settings_table", GENERATOR)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GENERATOR_MODULE = _load_generator()


#: A minimal wire header: the fields the generator understands.
WIRE_SAMPLE = """\
#pragma once
#include <cstdint>

namespace cs2::overlay::wire {

struct Settings {
  // --- ESP ---
  uint8_t esp_box;
  uint8_t esp_box_color[4];
  // --- movement ---
  float movement_thirdperson_distance;
  uint32_t movement_sequence;
  char overlay_title[16];
};

}  // namespace cs2::overlay::wire
"""


class TestParsing(unittest.TestCase):
    """The parse: the packed layout is the source of the offsets."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def test_the_fields_are_parsed_with_their_offsets(self) -> None:
        fields = self.generator.parse_settings(WIRE_SAMPLE)
        names = [field[0] for field in fields]
        self.assertEqual(names, ["esp_box", "esp_box_color",
                                 "movement_thirdperson_distance",
                                 "movement_sequence", "overlay_title"])

    def test_the_offsets_follow_the_packed_layout(self) -> None:
        """The struct is packed: a field's offset is the sum of the sizes."""
        fields = {field[0]: field for field in
                  self.generator.parse_settings(WIRE_SAMPLE)}
        self.assertEqual(fields["esp_box"][3], 0)             # a byte
        self.assertEqual(fields["esp_box_color"][3], 1)       # four bytes
        self.assertEqual(fields["movement_thirdperson_distance"][3], 5)
        self.assertEqual(fields["movement_sequence"][3], 9)
        self.assertEqual(fields["overlay_title"][3], 13)

    def test_the_sections_are_read(self) -> None:
        fields = {field[0]: field for field in
                  self.generator.parse_settings(WIRE_SAMPLE)}
        self.assertEqual(fields["esp_box"][1], "ESP")
        self.assertEqual(fields["movement_sequence"][1], "movement")

    def test_a_parse_failure_is_loud(self) -> None:
        """A field the generator does not know is an error, not a guess."""
        broken = WIRE_SAMPLE.replace("uint8_t esp_box;", "double esp_box;")
        with self.assertRaises(self.generator.ParseError):
            self.generator.parse_settings(broken)

    def test_a_missing_struct_is_loud(self) -> None:
        with self.assertRaises(self.generator.ParseError):
            self.generator.parse_settings("struct Other { uint8_t a; };")


class TestRendering(unittest.TestCase):
    """The render: the header the build compiles."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def test_the_table_carries_every_field(self) -> None:
        text = self.generator.render(self.generator.parse_settings(WIRE_SAMPLE))
        for name in ("esp_box", "esp_box_color", "movement_thirdperson_distance",
                     "overlay_title"):
            self.assertIn(f'"{name}"', text)
        self.assertIn("SETTING_TABLE", text)
        self.assertIn("find_setting", text)

    def test_the_render_is_deterministic(self) -> None:
        fields = self.generator.parse_settings(WIRE_SAMPLE)
        self.assertEqual(self.generator.render(fields),
                         self.generator.render(fields))


class TestCommandLine(unittest.TestCase):
    """The command line: the ``--check`` mode the build and the test use."""

    def _run(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(GENERATOR), *args],
                              capture_output=True, text=True, timeout=120)

    def test_the_check_passes_on_a_fresh_table(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            wire = pathlib.Path(directory) / "wire.hpp"
            table = pathlib.Path(directory) / "settings_table.hpp"
            wire.write_text(WIRE_SAMPLE, encoding="utf-8")
            self.assertEqual(self._run(str(wire), str(table)).returncode, 0)
            completed = self._run("--check", str(wire), str(table))
            self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_the_check_fails_on_a_stale_table(self) -> None:
        """The test that the Meson one could not be: a stale table must fail.

        The table is compared against a **copy**, so the check does not depend
        on what the build did to the file in the source tree.
        """
        with tempfile.TemporaryDirectory() as directory:
            wire = pathlib.Path(directory) / "wire.hpp"
            table = pathlib.Path(directory) / "settings_table.hpp"
            wire.write_text(WIRE_SAMPLE, encoding="utf-8")
            self.assertEqual(self._run(str(wire), str(table)).returncode, 0)
            # A hand edit: the table no longer matches the header.
            table.write_text(table.read_text(encoding="utf-8")
                             .replace("esp_box", "esp_boxX"), encoding="utf-8")
            completed = self._run("--check", str(wire), str(table))
            self.assertEqual(completed.returncode, 1)
            self.assertIn("out of date", completed.stderr)

    def test_a_missing_table_fails_the_check(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            wire = pathlib.Path(directory) / "wire.hpp"
            wire.write_text(WIRE_SAMPLE, encoding="utf-8")
            completed = self._run("--check", str(wire),
                                  str(pathlib.Path(directory) / "nope.hpp"))
            self.assertEqual(completed.returncode, 1)

    def test_a_parse_failure_exits_with_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            wire = pathlib.Path(directory) / "wire.hpp"
            table = pathlib.Path(directory) / "settings_table.hpp"
            wire.write_text("struct Other { uint8_t a; };", encoding="utf-8")
            completed = self._run(str(wire), str(table))
            self.assertEqual(completed.returncode, 2)
            self.assertIn("gen-settings-table", completed.stderr)


class TestCheckedInTable(unittest.TestCase):
    """The table that is in the tree must match the header it came from."""

    def test_the_checked_in_table_is_up_to_date(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(GENERATOR), "--check", str(WIRE), str(TABLE)],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_a_rewrite_keeps_the_line_ending(self) -> None:
        """A CRLF checkout must not turn into a "modified" file."""
        generator = GENERATOR_MODULE
        with tempfile.TemporaryDirectory() as directory:
            table = pathlib.Path(directory) / "settings_table.hpp"
            table.write_bytes(b"old\r\ncontent\r\n")
            self.assertEqual(generator.line_ending(table), "\r\n")
            table.write_bytes(b"old\ncontent\n")
            self.assertEqual(generator.line_ending(table), "\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
