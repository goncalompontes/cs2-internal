"""Unit tests for the live settings block (`scripts/scenario/settings.py`).

The block is owned by the overlay menu and read through the **control channel**
(`settings_get` / `settings_set`) — the same block the menu edits and the
payload applies. The reader is a thin adapter over that channel, so it is
testable without a game: the channel is a stub.

The module used to parse the shared segment directly (the wire header, the
settings seqlock and a 16-byte block). The wire grew to **version 13** — the
block is 441 bytes at offset 28 and carries its own checksum — so that reader
raised ``segment version 13 != 4`` on every call, and the movement scenarios
reported the features as OFF while the payload was acting (2026-10-20: 748
strafe writes in a window the scenario called "skipped"). The channel cannot go
stale that way, and these tests pin that the reader asks the channel for the
**live** block rather than for a copy of an old layout.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from pathlib import Path
from typing import Any, Sequence

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import settings  # noqa: E402
from scenario.settings import Settings, SettingsError  # noqa: E402


class StubChannel:
    """The control channel's settings command, answered from a dictionary."""

    def __init__(self, values: dict[str, Any] | None = None) -> None:
        self.values = dict(values or {})
        self.requested: list[Sequence[str] | None] = []
        self.written: list[dict[str, Any]] = []
        self.raises: Exception | None = None

    def settings_get(self, fields: Sequence[str] | None = None) -> dict[str, Any]:
        self.requested.append(fields)
        if self.raises is not None:
            raise self.raises
        return {"values": {key: self.values.get(key, 0) for key in (fields or self.values)}}

    def settings_set(self, values: dict[str, Any]) -> dict[str, Any]:
        self.written.append(dict(values))
        if self.raises is not None:
            raise self.raises
        self.values.update(values)
        return {"changed": sorted(values), "values": dict(values)}


class TestRead(unittest.TestCase):
    """Reading the live block."""

    def test_the_block_is_read(self) -> None:
        channel = StubChannel({"auto_strafe": 1, "bhop": 1})
        read = settings.read(channel)
        self.assertTrue(read.auto_strafe)
        self.assertTrue(read.bhop)

    def test_the_flags_default_to_off(self) -> None:
        read = settings.read(StubChannel({}))
        self.assertFalse(read.auto_strafe)
        self.assertFalse(read.bhop)

    def test_only_the_movement_flags_are_asked_for(self) -> None:
        """The reader asks for the two flags it needs — and no more."""
        channel = StubChannel({"auto_strafe": 1, "bhop": 0, "esp_box": 1})
        settings.read(channel)
        self.assertEqual(channel.requested, [["auto_strafe", "bhop"]])

    def test_a_transport_failure_is_an_error(self) -> None:
        channel = StubChannel({})
        channel.raises = OSError("the pipe is gone")
        with self.assertRaises(SettingsError) as raised:
            settings.read(channel)
        self.assertIn("cannot read", str(raised.exception))

    def test_an_answer_without_values_is_an_error(self) -> None:
        """A payload that answers with something else must not be read as 'off'."""
        class Empty:
            def settings_get(self, fields: Sequence[str] | None = None) -> dict[str, Any]:
                return {}

            def settings_set(self, values: dict[str, Any]) -> dict[str, Any]:
                return {}

        with self.assertRaises(SettingsError) as raised:
            settings.read(Empty())
        self.assertIn("did not answer with values", str(raised.exception))


class TestWrite(unittest.TestCase):
    """Writing the live block."""

    def test_the_block_is_written(self) -> None:
        channel = StubChannel({})
        written = settings.write(channel, Settings(auto_strafe=True, bhop=True))
        self.assertTrue(written.auto_strafe)
        self.assertTrue(written.bhop)
        self.assertEqual(channel.written, [{"auto_strafe": True, "bhop": True}])

    def test_a_transport_failure_is_an_error(self) -> None:
        channel = StubChannel({})
        channel.raises = OSError("the pipe is gone")
        with self.assertRaises(SettingsError) as raised:
            settings.write(channel, Settings(auto_strafe=True))
        self.assertIn("cannot write", str(raised.exception))


class TestSettings(unittest.TestCase):
    """The value object."""

    def test_the_movement_flags_are_named(self) -> None:
        value = Settings(auto_strafe=True, bhop=True)
        self.assertTrue(value.auto_strafe)
        self.assertTrue(value.bhop)

    def test_the_flags_can_be_changed(self) -> None:
        """``with_movement`` is how a scenario sets a feature and puts it back."""
        value = Settings()
        changed = value.with_movement(auto_strafe=True, bhop=True)
        self.assertTrue(changed.auto_strafe)
        self.assertTrue(changed.bhop)
        # The original is untouched (the value is immutable).
        self.assertFalse(value.auto_strafe)
        self.assertFalse(value.bhop)

    def test_the_other_flags_are_kept(self) -> None:
        """Only the two movement flags are addressed; the rest is the menu's."""
        value = Settings(auto_strafe=True)
        changed = value.with_movement(bhop=True)
        self.assertTrue(changed.auto_strafe)
        self.assertTrue(changed.bhop)

    def test_a_flag_can_be_cleared_again(self) -> None:
        value = Settings(auto_strafe=True)
        self.assertFalse(value.with_movement(auto_strafe=False).auto_strafe)


if __name__ == "__main__":
    unittest.main()
