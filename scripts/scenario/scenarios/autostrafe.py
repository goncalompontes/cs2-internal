"""Scenario: verify the payload's auto-strafe **as it is used** — observation-only.

The movement features are payload features owned by the overlay menu's `Movement`
tab (`scripts/scenario/settings.py` reads that block; ``docs/control-harness.md``
§"Input backends"). This scenario therefore **drives nothing**: it reads the settings
the operator has enabled, watches the payload's own state while the feature runs in a
live round, and reports the evidence:

* the payload reports the strafe acting while airborne (`strafe_airborne`,
  `strafe_writes` climbing, `strafe_delta` non-zero),
* the horizontal speed grows across the hops (air-strafe acceleration),
* the pawn's eye angles stay put (the silent channel — the camera does not move).

It fails only when the feature is enabled and does **not** act, or when it acts
non-silently; a disabled feature is reported as skipped, not failed (enable it in the
menu's `Movement` tab, or with `CS2_PAYLOAD_MOVEMENT`).

Read-only apart from what a live round needs: `ensure_live_round` is a no-op when the
round is already live, and no `strafe`/`bhop`/`move`/settings write is ever sent.
"""

from __future__ import annotations

import math
import time
from typing import Any

from .. import awaiting, settings
from ..session import Game, SessionError

WATCH = 6.0     # how long to observe the feature doing its thing
SAMPLE = 0.05
MIN_GAIN = 15.0  # units/s of speed gained across the window before we call it acting


def _speed(game: Game) -> float:
    velocity = (awaiting.local(game).get("velocity") or {})
    return math.hypot(velocity.get("x", 0.0), velocity.get("y", 0.0))


def run(game: Game, *, watch: float = WATCH, min_gain: float = MIN_GAIN,
        wait_airborne: float = 90.0, map_name: str = "de_dust2") -> dict[str, Any]:
    """Observe the payload's auto-strafe in a live round while the player jumps.

    **Nothing is driven**: the scenario waits (up to ``wait_airborne`` seconds) for the
    operator to leave the ground in-game, then watches the payload's own state for
    ``watch`` seconds and reports the evidence.
    """
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    try:
        operator_settings = settings.read(game)
    except settings.SettingsError as error:
        # A block that could not be read is **not** "the feature is off": the
        # old reader failed exactly this way ("segment version 13 != 4") and the
        # scenarios reported themselves as skipped while the payload was
        # acting. A read failure is an error, not a disabled feature.
        raise SessionError(f"cannot read the live settings block: {error}") from error

    enabled = bool(operator_settings and operator_settings.auto_strafe)
    bhop_enabled = bool(operator_settings and operator_settings.bhop)
    print(f"[autostrafe] menu: auto_strafe={enabled} bhop={bhop_enabled} "
          f"(settings block read-only; nothing is written)", flush=True)
    if enabled:
        print(f"[autostrafe] waiting up to {wait_airborne:.0f}s for the player to leave "
              f"the ground — jump in-game to exercise the auto-strafe", flush=True)

    status = game.input()
    writes_before = status.get("strafe_writes") or 0
    presses_before = status.get("bhop_presses") or 0
    ignored_before = status.get("strafe_ignored") or 0
    eye_before = (awaiting.local(game).get("eye_angles") or {}).get("yaw")
    speeds: list[float] = []
    airborne_samples = 0
    acting_samples = 0
    grounded_samples = 0
    deltas: list[float] = []

    # Phase 1: wait for the first airborne sample (the operator's own jump).
    waited = time.monotonic() + (wait_airborne if enabled else 0.0)
    while time.monotonic() < waited:
        local = awaiting.local(game)
        if not local.get("on_ground", True):
            break
        time.sleep(0.2)
    else:
        if enabled:
            print("[autostrafe] never left the ground — reporting what was seen", flush=True)

    # Phase 2: observe the feature while the player is in the air.
    deadline = time.monotonic() + watch
    while time.monotonic() < deadline:
        local = awaiting.local(game)
        velocity = local.get("velocity") or {}
        speeds.append(math.hypot(velocity.get("x", 0.0), velocity.get("y", 0.0)))
        if not local.get("on_ground", True):
            airborne_samples += 1
            live = game.input()
            if live.get("strafe_airborne"):
                acting_samples += 1
                deltas.append(float(live.get("strafe_delta") or 0.0))
        else:
            grounded_samples += 1
        time.sleep(SAMPLE)

    status = game.input()
    writes_after = status.get("strafe_writes") or 0
    presses_after = status.get("bhop_presses") or 0
    ignored_after = status.get("strafe_ignored") or 0
    eye_after = (awaiting.local(game).get("eye_angles") or {}).get("yaw")
    eye_drift = None
    if eye_before is not None and eye_after is not None:
        eye_drift = round(abs((eye_after - eye_before + 180.0) % 360.0 - 180.0), 1)
    gain = round((max(speeds) - min(speeds)) if speeds else 0.0, 1)
    observed_writes = writes_after - writes_before

    result: dict[str, Any] = {
        "screen": str(awaiting.screen(game)),
        "menu_auto_strafe": enabled,
        "menu_bhop": bhop_enabled,
        "airborne_samples": airborne_samples,
        "grounded_samples": grounded_samples,
        "strafe_airborne_samples": acting_samples,
        "strafe_writes_during_window": observed_writes,
        "bhop_presses_during_window": presses_after - presses_before,
        "strafe_delta_range": [round(min(deltas), 1), round(max(deltas), 1)] if deltas else None,
        "max_speed": round(max(speeds), 1) if speeds else 0.0,
        "speed_gain": gain,
        "eye_yaw_before": eye_before,
        "eye_yaw_after": eye_after,
        "eye_drift": eye_drift,
        # The payload's own **outcome** and **gate verdict**: the speed gained
        # over the last airtime (u/s) and whether the engine applied the
        # requested basis rotation. The rotation is applied only when the
        # server honours the sub-tick view angles
        # (`sv_subtick_movement_view_angles`); when it does not, the feature
        # writes every tick and gains nothing — the operator has to be able to
        # see that (`docs/re/strafing.md` §"The gate").
        "strafe_gain": round(float(status.get("strafe_gain") or 0.0), 1),
        "strafe_steered": bool(status.get("strafe_steered")),
        "strafe_ignored_during_window": ignored_after - ignored_before,
        "strafe_subticks": status.get("strafe_subticks"),
    }

    if not enabled:
        result["verdict"] = ("auto-strafe is OFF in the overlay menu (Movement tab) — "
                             "nothing to observe; enable it there and re-run")
        result["skipped"] = True
        return result

    if airborne_samples == 0:
        result["verdict"] = ("auto-strafe is enabled but the player never left the "
                             "ground during the window — nothing to observe (jump in-game)")
        result["skipped"] = True
        return result

    if acting_samples == 0 or observed_writes == 0:
        raise SessionError(
            "auto-strafe is enabled and the player was airborne, but the payload never "
            f"acted: airborne samples={airborne_samples}, acting={acting_samples}, "
            f"strafe_writes +{observed_writes} ({result})"
        )

    result["bhop_note"] = (
        f"bhop assist queued {presses_after - presses_before} sub-tick jump presses"
        if bhop_enabled else "menu bhop is off"
    )
    if eye_drift is not None and eye_drift >= 5.0:
        raise SessionError(f"auto-strafe moved the eye angles (drift {eye_drift}): {result}")

    result["verdict"] = (
        f"auto-strafe acted in {acting_samples}/{airborne_samples} airborne samples "
        f"({observed_writes} step writes), speed {round(min(speeds), 0)}->"
        f"{round(max(speeds), 0)} units/s, eye drift {eye_drift} deg (silent)"
    )
    # The **gate**: the silent encoding writes the wish as a sub-tick yaw
    # delta, and the engine only applies it when the server honours the
    # sub-tick view angles (`sv_subtick_movement_view_angles`). When the
    # server ignores them the feature writes every tick, the velocity never
    # turns, and nothing accelerates — the symptom the operator reported. The
    # payload measures it (`strafe_steered` / `strafe_ignored`), so the report
    # says it outright instead of leaving the operator to guess.
    #
    # The counters are the signal (one noisy tick flips the last verdict), and
    # the **camera-frame analog encoding has no rotation at all** — the gate does
    # not apply there, so no gate verdict is reported for it.
    followed = int(status.get("strafe_steered_ticks") or 0)
    ignored = int(result["strafe_ignored_during_window"])
    judged = followed + ignored
    result["strafe_judged_ticks"] = judged
    result["strafe_followed_ticks"] = followed
    if int(result["strafe_subticks"] or 0) > 0 and judged > 0 and ignored * 2 > judged:
        result["verdict"] += (
            f" — BUT the rotation was not applied ({ignored} of {judged} judged ticks"
            " did not turn the velocity): the server may be ignoring the sub-tick"
            " view angles (sv_subtick_movement_view_angles), and the silent"
            " encoding cannot steer without them"
            " (docs/re/strafing.md §\"The gate\")"
        )
    elif result["strafe_gain"] != 0.0:
        result["verdict"] += f" (last airtime gained {result['strafe_gain']:+.1f} u/s)"
    return result
