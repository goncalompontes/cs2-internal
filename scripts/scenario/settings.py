"""The movement features' **live** settings (the overlay menu's block).

The block is owned by the overlay menu (ADR-0021) and the payload applies its
changes; the control channel reads and writes **the same block** through that
owner (``settings_get`` / ``settings_set``), so a scenario can see — and drive
— exactly what the operator's menu shows, in either render mode (the in-game
menu and the Vulkan layer's).

**Why this module used to read the shared segment, and why it no longer does.**
It parsed the segment directly: the wire header, the settings seqlock and the
16-byte block of the flags. The wire has since grown to **version 13** — the
block is 441 bytes, it starts at offset 28 (the checksum sits at 24) and it
carries a checksum of its own — so the old reader raised ``segment version 13
!= 4 (stale pair)`` on **every** call. The movement scenarios treated that as
"the feature is off" and reported themselves as *skipped* while the payload was
acting: the 2026-10-20 measurement watched 748 strafe writes in a window the
``autostrafe`` scenario called "auto-strafe is OFF in the overlay menu". The
control channel is the one reader that cannot go stale — it asks the block's
owner — so the scenarios use it.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol, Sequence


class _SettingsReader(Protocol):
    """The part of the session this module needs (the control channel)."""

    def settings_get(self, fields: Sequence[str] | None = None) -> dict[str, Any]:
        ...

    def settings_set(self, values: dict[str, Any]) -> dict[str, Any]:
        ...


@dataclass(frozen=True)
class Settings:
    """The movement features' state, as the menu's block has it."""

    auto_strafe: bool = False
    bhop: bool = False

    def with_movement(self, *, auto_strafe: bool | None = None,
                      bhop: bool | None = None) -> "Settings":
        """A copy with the movement flags replaced (the others untouched)."""
        return Settings(
            auto_strafe=self.auto_strafe if auto_strafe is None else auto_strafe,
            bhop=self.bhop if bhop is None else bhop,
        )


class SettingsError(RuntimeError):
    """The live block could not be read."""


def read(game: _SettingsReader) -> Settings:
    """Read the movement features' state from the **live** block.

    The read goes through the control channel's ``settings`` command, so it
    answers with the block the menu owns and the payload applies — not a copy
    of an older layout. Raises :class:`SettingsError` when the channel does
    not answer (no payload, or the block is not published yet).
    """
    try:
        answer = game.settings_get(["auto_strafe", "bhop"])
    except Exception as error:  # noqa: BLE001 - any transport failure is one answer
        raise SettingsError(f"cannot read the live settings block: {error}") from error
    values = answer.get("values") if isinstance(answer, dict) else None
    if not isinstance(values, dict):
        raise SettingsError(f"the settings command did not answer with values: {answer!r}")
    return Settings(auto_strafe=bool(values.get("auto_strafe", 0)),
                    bhop=bool(values.get("bhop", 0)))


def write(game: _SettingsReader, settings: Settings) -> Settings:
    """Set the movement features' state in the **live** block.

    The write is a field-wise merge through the block's owner (the menu), so it
    cannot race with the menu's own edits the way a whole-block replacement
    would. The settings the operator did not name are left alone.
    """
    try:
        game.settings_set({"auto_strafe": settings.auto_strafe, "bhop": settings.bhop})
    except Exception as error:  # noqa: BLE001 - any transport failure is one answer
        raise SettingsError(f"cannot write the live settings block: {error}") from error
    return settings
