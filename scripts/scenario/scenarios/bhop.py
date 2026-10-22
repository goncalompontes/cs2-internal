"""Scenario: verify the payload's tick-perfect **sub-tick bunnyhop**.

The bhop is a payload feature owned by the overlay menu's `Movement` tab
(`scripts/scenario/settings.py`, ADR-0021). It only assists while the player
actually holds jump, so this scenario holds jump like a player would (through the
command's own mask — the run the feature exists for) and then reports what the
payload's **record-based** path did, from its own diagnostics
(`docs/re/prediction.md` §"RE findings", `docs/re/research-bhop.md`):

* `bhop_writes` / `bhop_presses` climb while jump is held;
* `bhop_a` (placement A, past the recorded contact) is placed at all — that is
  what proves the phase: `bhop_age` = `build - landed_tick` = 1;
* `bhop_a` dominates `bhop_b` (the beta catch-up, which loses speed to the
  ground-slice clamp);
* the player keeps hopping (on-ground transitions) instead of sticking to the
  floor.

`bhop_age` itself legitimately spans the whole airtime between two landings
(`landed_tick` only moves when the engine records a new contact), so the phase is
judged from the `bhop_a` / `bhop_b` decision counters, not from sampling `bhop_age`.

It fails when the feature is enabled and jump is held but nothing is written, or
when no hop cycle happens. A feature that is **off** in the menu is reported as
skipped, not failed (enable it in the menu's `Movement` tab, or seed
`CS2_PAYLOAD_MOVEMENT=bhop`).
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting, settings
from ..session import Game, SessionError

WATCH = 5.0   # how long to hold jump and observe
SAMPLE = 0.02


def run(game: Game, *, watch: float = WATCH, map_name: str = "de_dust2") -> dict[str, Any]:
    """Hold jump in a live round and report what the payload bhop did."""
    awaiting.ensure_live_round(game, map_name=map_name)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")

    try:
        menu = settings.read()
    except settings.SettingsError as error:
        print(f"[bhop] cannot read the menu settings: {error}", flush=True)
        menu = None
    enabled = bool(menu and menu.bhop)
    print(f"[bhop] menu bhop={enabled} (settings block read-only)", flush=True)
    if not enabled:
        return {
            "menu_bhop": False,
            "skipped": True,
            "verdict": ("bhop is OFF in the overlay menu (Movement tab) — nothing to "
                        "observe; enable it there (or seed CS2_PAYLOAD_MOVEMENT=bhop) "
                        "and re-run"),
        }

    before = game.input()
    writes_before = before.get("bhop_writes") or 0
    presses_before = before.get("bhop_presses") or 0
    a_before = before.get("bhop_a") or 0
    b_before = before.get("bhop_b") or 0

    # "Holding space": the assist's trigger is the player's own held jump.
    game.buttons(reset=True)
    game.buttons(press=["jump"])

    ages: dict[int, int] = {}
    modes: dict[int, int] = {}
    whens: list[float] = []
    landings: set[int] = set()
    ticks: set[int] = set()
    hops = 0
    last = awaiting.on_ground(game)
    deadline = time.monotonic() + watch
    while time.monotonic() < deadline:
        live = game.input()
        age = live.get("bhop_age")
        mode = live.get("bhop_mode")
        if isinstance(age, (int, float)):
            ages[int(age)] = ages.get(int(age), 0) + 1
        if isinstance(mode, (int, float)) and mode:
            modes[int(mode)] = modes.get(int(mode), 0) + 1
            if isinstance(live.get("bhop_when"), (int, float)):
                whens.append(float(live["bhop_when"]))
        landed = live.get("bhop_landed_tick")
        if isinstance(landed, (int, float)) and landed:
            landings.add(int(landed))
        build = live.get("bhop_build")
        if isinstance(build, (int, float)) and build:
            ticks.add(int(build))
        grounded = awaiting.on_ground(game)
        if grounded and not last:
            hops += 1
        last = grounded
        time.sleep(SAMPLE)

    after = game.input()
    game.buttons(reset=True)
    writes = (after.get("bhop_writes") or 0) - writes_before
    presses = (after.get("bhop_presses") or 0) - presses_before
    a_placed = (after.get("bhop_a") or 0) - a_before
    b_placed = (after.get("bhop_b") or 0) - b_before
    result: dict[str, Any] = {
        "menu_bhop": enabled,
        "bhop_writes": writes,
        "bhop_presses": presses,
        "bhop_placement_a": a_placed,
        "bhop_placement_b": b_placed,
        "hop_cycles": hops,
        "landings_seen": sorted(landings)[:8],
        "build_ticks_seen": len(ticks),
        "age_samples": dict(sorted(ages.items())),
        "mode_samples": dict(sorted(modes.items())),
        "landed_tick": after.get("bhop_landed_tick"),
        "landed_frac": after.get("bhop_landed_frac"),
        "build": after.get("bhop_build"),
        "tick_base": after.get("bhop_tick_base"),
        "client_tick": after.get("bhop_client_tick"),
        "holding_ticks": after.get("bhop_holding_ticks"),
        "when_range": [round(min(whens), 4), round(max(whens), 4)] if whens else None,
    }

    if writes == 0:
        result["verdict"] = ("bhop is enabled and jump was held, but the assist never "
                             "wrote a pair")
        raise SessionError(f"payload bhop never acted: {result}")

    result["phase_ok"] = a_placed > 0
    result["verdict"] = (
        f"bhop wrote {writes} pairs ({presses} presses) over {hops} hop cycles; "
        f"placement A {a_placed} / B {b_placed}, "
        f"when {result['when_range']} "
        + ("(phase on: A was placed)" if a_placed > 0 else
           "(PHASE OFF: A never placed — only the beta catch-up fired; compare "
           "bhop_tick_base / bhop_client_tick)")
    )
    if hops == 0:
        raise SessionError(f"bhop wrote pairs but the player never landed/hop: {result}")
    return result
