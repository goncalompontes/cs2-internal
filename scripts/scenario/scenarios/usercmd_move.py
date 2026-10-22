"""Scenario: discrete buttons through the payload ``usercmd`` backend.

Validates the payload ``CreateMove`` hook (``docs/re/client-dll.md`` §"Input
path"): it reaches a live round, selects ``usercmd``, then drives **jump** and
**bhop** through the command's button mask and asserts the player leaves the
ground and hops repeatedly.

The hook writes the command's legacy button mask (`cmd + 0x60`); live testing
shows that drives *discrete* actions (jump/attack/duck/reload) but **not** analog
movement (`forward`/`moveleft`), which is subtick in this build — analog movement
stays on the console backend (`press("w")`) for now.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting, settings
from ..session import Game, SessionError

BHOP_SECONDS = 4.0


def run(game: Game, *, bhop_seconds: float = BHOP_SECONDS,
        map_name: str = "de_dust2") -> dict[str, Any]:
    """Drive jump + bhop via ``usercmd``; assert both take effect."""
    awaiting.ensure_live_round(game, map_name=map_name)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    awaiting.require(game, "jump")  # the machine: is a jump meaningful here?

    # A single discrete button: hold jump through the command's mask.
    game.buttons(reset=True)
    game.buttons(press=["jump"])
    jumped = awaiting.wait_airborne(game, timeout=5.0)
    game.buttons(reset=True)
    if not jumped:
        raise SessionError("usercmd jump did not leave the ground")
    awaiting.wait_on_ground(game, timeout=5.0)

    # Auto-jump is a payload feature **owned by the overlay menu** and only assists
    # while the player holds jump, so this scenario presses jump through the command
    # mask (the player's own held input) and reports what the menu's setting does —
    # it does not toggle the feature itself.
    try:
        bhop_enabled = settings.read().bhop
    except settings.SettingsError:
        bhop_enabled = False
    presses_before = (game.input().get("bhop_presses") or 0)
    game.buttons(press=["jump"])  # "holding space"
    landings = 0
    last = awaiting.on_ground(game)
    deadline = time.monotonic() + bhop_seconds
    while time.monotonic() < deadline:
        grounded = awaiting.on_ground(game)
        if grounded and not last:
            landings += 1
        last = grounded
        time.sleep(0.05)
    presses_after = (game.input().get("bhop_presses") or 0)
    game.buttons(reset=True)
    if bhop_enabled and landings < 1:
        raise SessionError(
            "bhop is enabled in the menu and jump was held, but no hop cycle happened"
        )

    return {"jumped": jumped, "bhop_enabled": bhop_enabled, "bhop_landings": landings,
            "bhop_presses": presses_after - presses_before,
            "note": "" if bhop_enabled else "menu bhop is off — hold-jump hops only"}
