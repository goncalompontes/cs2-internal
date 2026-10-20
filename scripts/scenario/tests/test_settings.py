"""Unit tests for the shared settings block (`scripts/scenario/settings.py`).

The overlay's menu edits the settings block in a shared-memory segment and the
payload applies the *changes*; a scenario can drive the very same channel to
make a feature deterministic — and put the operator's settings back afterwards.

The block is a seqlock: the writer bumps a sequence number to an odd value,
writes the bytes, and bumps it to an even value again. A reader that sees an
odd sequence is looking at a half-written block and must retry. That is the
whole contract of this module, and it is testable without a game: the segment
is a file.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import struct
import sys
import tempfile
import unittest
from typing import Any
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import settings  # noqa: E402
from scenario.settings import Settings, SettingsError  # noqa: E402

#: The size of the settings block (16 bytes, one per feature flag).
BLOCK_SIZE = 16


def segment(path: pathlib.Path, *, version: int = settings.K_VERSION,
            block: bytes = bytes(BLOCK_SIZE),
            sequence: int = 2) -> pathlib.Path:
    """Write a segment the way the payload publishes it.

    The layout is the one in ``overlay/include/cs2/overlay/wire.hpp``: the
    magic, the version, the two screen dimensions, the sequence, the settings
    sequence and the settings block itself.
    """
    header = bytearray(64)
    struct.pack_into("<I", header, 0, 0x43533200)      # the magic
    struct.pack_into("<I", header, 4, version)          # wire::k_version
    struct.pack_into("<I", header, 8, 1920)             # screen width
    struct.pack_into("<I", header, 12, 1080)            # screen height
    struct.pack_into("<I", header, 20, sequence)        # the settings seqlock
    header[settings._SETTINGS_OFFSET:
           settings._SETTINGS_OFFSET + BLOCK_SIZE] = block
    path.write_bytes(bytes(header))
    return path


def block_with(auto_strafe: bool = False, bhop: bool = False) -> bytes:
    """A settings block with the two movement flags set."""
    raw = bytearray(BLOCK_SIZE)
    raw[10] = 1 if auto_strafe else 0
    raw[11] = 1 if bhop else 0
    return bytes(raw)


class FlippingStruct:
    """A ``struct`` whose first read of the settings sequence finds a writer.

    It simulates the seqlock race: the reader sees an *odd* sequence (the
    writer is half-way through), and the block settles before the reader's
    second attempt.
    """

    def __init__(self, real: Any, offset: int) -> None:
        self._real = real
        self._offset = offset
        self.flipped = False

    def unpack_from(self, fmt: str, buffer: Any, offset: int = 0) -> Any:
        value = self._real.unpack_from(fmt, buffer, offset)
        if offset == self._offset and not self.flipped:
            self.flipped = True
            self._real.pack_into("<I", buffer, offset, 4)
        return value

    def pack_into(self, fmt: str, buffer: Any, offset: int, value: Any) -> Any:
        return self._real.pack_into(fmt, buffer, offset, value)


class TestRead(unittest.TestCase):
    """Reading the block."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = pathlib.Path(self.directory.name) / "cs2-overlay.shm"

    def test_the_block_is_read(self) -> None:
        segment(self.path, block=block_with(auto_strafe=True, bhop=True))
        read = settings.read(self.path)
        self.assertTrue(read.auto_strafe)
        self.assertTrue(read.bhop)

    def test_the_flags_default_to_off(self) -> None:
        segment(self.path)
        read = settings.read(self.path)
        self.assertFalse(read.auto_strafe)
        self.assertFalse(read.bhop)

    def test_a_missing_segment_is_an_error(self) -> None:
        with self.assertRaises(SettingsError) as raised:
            settings.read(self.path)
        self.assertIn("shared segment not found", str(raised.exception))

    def test_a_version_mismatch_is_an_error(self) -> None:
        """A stale payload/layer pair must not be read as a valid block."""
        segment(self.path, version=settings.K_VERSION + 1)
        with self.assertRaises(SettingsError) as raised:
            settings.read(self.path)
        self.assertIn("stale pair", str(raised.exception))

    def test_a_block_that_keeps_changing_is_an_error(self) -> None:
        """An odd sequence means "a writer is half-way through"."""
        segment(self.path, sequence=3)
        with self.assertRaises(SettingsError) as raised:
            settings.read(self.path)
        self.assertIn("kept changing", str(raised.exception))

    def test_a_block_that_settles_is_read(self) -> None:
        """The retry: the writer finished while the reader was looking."""
        segment(self.path, sequence=3, block=block_with(auto_strafe=True))
        with mock.patch.object(settings, "struct",
                               FlippingStruct(struct, settings._SETTINGS_SEQUENCE_OFFSET)):
            read = settings.read(self.path)
        self.assertTrue(read.auto_strafe)


class TestWrite(unittest.TestCase):
    """Writing the block — what the menu does."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = pathlib.Path(self.directory.name) / "cs2-overlay.shm"

    def test_the_block_is_written_and_read_back(self) -> None:
        segment(self.path)
        written = settings.write(Settings(raw=tuple(block_with(auto_strafe=True))),
                                  self.path)
        self.assertTrue(written.auto_strafe)
        self.assertTrue(settings.read(self.path).auto_strafe)

    def test_the_sequence_is_left_even(self) -> None:
        """The seqlock must be closed when the writer is done."""
        segment(self.path, sequence=8)
        settings.write(Settings(raw=tuple(block_with(bhop=True))), self.path)
        sequence = struct.unpack_from("<I", self.path.read_bytes(),
                                      settings._SETTINGS_SEQUENCE_OFFSET)[0]
        self.assertEqual(sequence % 2, 0)
        self.assertEqual(sequence, 10)

    def test_a_version_mismatch_is_refused(self) -> None:
        segment(self.path, version=settings.K_VERSION + 1)
        with self.assertRaises(SettingsError):
            settings.write(Settings(raw=tuple(bytes(BLOCK_SIZE))), self.path)


class TestSettings(unittest.TestCase):
    """The value object."""

    def test_the_movement_flags_are_named(self) -> None:
        value = Settings(raw=tuple(block_with(auto_strafe=True, bhop=True)))
        self.assertTrue(value.auto_strafe)
        self.assertTrue(value.bhop)

    def test_the_flags_can_be_changed(self) -> None:
        """``with_movement`` is how a scenario sets a feature and puts it back."""
        value = Settings(raw=tuple(bytes(BLOCK_SIZE)))
        changed = value.with_movement(auto_strafe=True, bhop=True)
        self.assertTrue(changed.auto_strafe)
        self.assertTrue(changed.bhop)
        # The original is untouched (the value is immutable).
        self.assertFalse(value.auto_strafe)
        self.assertFalse(value.bhop)

    def test_the_other_bytes_are_kept(self) -> None:
        """Only the two movement flags are addressed; the rest is the menu's."""
        raw = bytearray(BLOCK_SIZE)
        raw[0] = 7
        changed = Settings(raw=tuple(raw)).with_movement(bhop=True)
        self.assertEqual(changed.raw[0], 7)
        self.assertEqual(changed.raw[11], 1)

    def test_a_flag_can_be_cleared_again(self) -> None:
        value = Settings(raw=tuple(block_with(auto_strafe=True)))
        self.assertFalse(value.with_movement(auto_strafe=False).auto_strafe)


if __name__ == "__main__":
    unittest.main(verbosity=2)
