"""The overlay menu's shared settings block (`wire::Settings`, ADR-0021).

The payload publishes the segment (``%LOCALAPPDATA%\\cs2-internal\\cs2-overlay.shm``)
and the Vulkan layer's in-game menu edits it; the payload applies **changes** — which
is how the movement features (`auto_strafe`, `bhop`) are owned by the menu
(``docs/control-harness.md`` §"Input backends"). A scenario can use the very same
channel to make those features deterministic for a test and put the operator's
settings back afterwards, instead of racing the menu.

Only the header + `Settings` are touched here; the layout is the one in
``overlay/include/cs2/overlay/wire.hpp`` (version 4):

    magic u32 | version u32 | screen_width u32 | screen_height u32
    | sequence u32 | settings_sequence u32 | Settings (16 x u8) | Model …
"""

from __future__ import annotations

import mmap
import os
import pathlib
import struct
from dataclasses import dataclass, replace

#: Where the payload maps the segment (`CS2_OVERLAY_SHM` overrides it).
DEFAULT_SHM = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "cs2-internal" / "cs2-overlay.shm"
#: `wire::k_version` — a mismatch means the payload/layer pair is stale.
K_VERSION = 4
_SETTINGS_SEQUENCE_OFFSET = 20
_SETTINGS_OFFSET = 24
_SETTINGS_SIZE = 16


@dataclass(frozen=True)
class Settings:
    """The 16 settings bytes, named where they matter."""

    raw: tuple[int, ...]

    @property
    def auto_strafe(self) -> bool:
        return bool(self.raw[10])

    @property
    def bhop(self) -> bool:
        return bool(self.raw[11])

    def with_movement(self, *, auto_strafe: bool | None = None,
                      bhop: bool | None = None) -> "Settings":
        values = list(self.raw)
        if auto_strafe is not None:
            values[10] = 1 if auto_strafe else 0
        if bhop is not None:
            values[11] = 1 if bhop else 0
        return replace(self, raw=tuple(values))


class SettingsError(RuntimeError):
    """The segment is missing, a different version, or mid-write."""


def _open(path: pathlib.Path) -> mmap.mmap:
    if not path.exists():
        raise SettingsError(f"shared segment not found: {path}")
    with open(path, "r+b") as handle:
        return mmap.mmap(handle.fileno(), 0)


def read(path: pathlib.Path = DEFAULT_SHM) -> Settings:
    """Read the settings block (raising on a version mismatch)."""
    mm = _open(path)
    try:
        version = struct.unpack_from("<I", mm, 4)[0]
        if version != K_VERSION:
            raise SettingsError(f"segment version {version} != {K_VERSION} (stale pair)")
        for _ in range(3):
            first = struct.unpack_from("<I", mm, _SETTINGS_SEQUENCE_OFFSET)[0]
            if first % 2:
                continue  # mid-write
            raw = tuple(mm[_SETTINGS_OFFSET:_SETTINGS_OFFSET + _SETTINGS_SIZE])
            if struct.unpack_from("<I", mm, _SETTINGS_SEQUENCE_OFFSET)[0] == first:
                return Settings(raw=raw)
        raise SettingsError("settings block kept changing while reading")
    finally:
        mm.close()


def write(settings: Settings, path: pathlib.Path = DEFAULT_SHM) -> Settings:
    """Write the block under the section's seqlock — exactly what the menu does."""
    mm = _open(path)
    try:
        version = struct.unpack_from("<I", mm, 4)[0]
        if version != K_VERSION:
            raise SettingsError(f"segment version {version} != {K_VERSION} (stale pair)")
        sequence = struct.unpack_from("<I", mm, _SETTINGS_SEQUENCE_OFFSET)[0]
        struct.pack_into("<I", mm, _SETTINGS_SEQUENCE_OFFSET, (sequence + 1) & 0xFFFFFFFF)
        mm[_SETTINGS_OFFSET:_SETTINGS_OFFSET + _SETTINGS_SIZE] = bytes(settings.raw)
        struct.pack_into("<I", mm, _SETTINGS_SEQUENCE_OFFSET, (sequence + 2) & 0xFFFFFFFF)
        return settings
    finally:
        mm.close()
