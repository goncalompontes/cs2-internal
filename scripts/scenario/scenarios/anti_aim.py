"""Scenario: observe the **anti-aim** in a live round â€” observation-only.

The anti-aim writes the fake (desync) angles into the **command's own**
``viewangles``; the camera reads a **different** field (the live view angles,
``CCSGOInput + 0x688``), so the camera cannot follow the fake angles
(``docs/re/angle-split.md`` Â§5.2). This scenario checks exactly that, plus the
**movement re-base** â€” the analog that keeps the operator's W/A/S_D
camera-relative while the command carries the fake angles.

It fails only when the anti-aim is enabled, its movement re-base is on and it
does **not** act, or when the camera *did* follow the fake angles. A disabled
anti-aim, or one whose movement re-base is switched off, is reported as
**skipped** with the reason.

**The split check.** The fake yaw is *derived* from the camera's yaw (the
configured offset), so the gap between the two says nothing on its own â€” it is
the configured offset by construction. What it says is whether the gap
**collapsed**: a camera that followed the fake angles would report the fake yaw
as its own and the gap would go to zero. The check is therefore skipped when
the configured offset (plus the jitter) is ~0 â€” there is nothing to collapse.

Read-only apart from what a live round needs: ``ensure_live_round`` is a no-op
when the round is already live, and no ``antiaim``/``strafe``/``move``/settings
write is ever sent â€” the scenario only *reads* the payload's own state.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

WATCH = 4.0     # how long to observe the anti-aim doing its thing
SAMPLE = 0.05


def _delta(first: float, second: float) -> float:
    """The signed difference ``first - second`` in degrees, wrapped to Â±180."""
    return (first - second + 180.0) % 360.0 - 180.0


def run(game: Game, *, watch: float = WATCH, map_name: str = "de_dust2") -> dict[str, Any]:
    """Watch the payload's anti-aim in a live round and report the evidence.

    **Nothing is driven**: the scenario reads the payload's own ``input`` status
    while the feature runs and reports what it saw.
    """
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    status = game.input()
    enabled = bool(status.get("anti_aim"))
    move_rebase = bool(status.get("anti_aim_move_rebase"))
    writes_before = int(status.get("anti_aim_writes") or 0)
    applied_before = int(status.get("anti_aim_rebase_applied") or 0)
    offset = abs(float(status.get("anti_aim_yaw_offset") or 0.0))
    jitter = abs(float(status.get("anti_aim_jitter") or 0.0))
    #: What the gap between the fake yaw and the camera's own yaw should be:
    #: the configured offset (the fake yaw is derived from the camera's yaw).
    configured_gap = offset + jitter

    fake_yaws: list[float] = []
    camera_yaws: list[float] = []
    forwards: list[float] = []
    lefts: list[float] = []
    errors: list[float] = []
    factors: list[float] = []
    acting_samples = 0

    deadline = time.monotonic() + watch
    while time.monotonic() < deadline:
        live = game.input()
        if live.get("anti_aim_acted"):
            acting_samples += 1
            fake_yaws.append(float(live.get("anti_aim_fake_yaw") or 0.0))
            camera_yaws.append(float(live.get("anti_aim_camera_yaw") or 0.0))
            forwards.append(float(live.get("anti_aim_forward") or 0.0))
            lefts.append(float(live.get("anti_aim_left") or 0.0))
            errors.append(float(live.get("anti_aim_direction_error") or 0.0))
            factors.append(float(live.get("anti_aim_wish_factor") or 0.0))
        time.sleep(SAMPLE)

    status = game.input()
    writes_after = int(status.get("anti_aim_writes") or 0)
    applied_after = int(status.get("anti_aim_rebase_applied") or 0)

    # The **split**: the gap between the fake yaw and the camera's own yaw.
    gaps = [abs(_delta(fake, camera)) for fake, camera in zip(fake_yaws, camera_yaws)]
    split_gap = round(min(gaps), 1) if gaps else None

    result: dict[str, Any] = {
        "screen": str(awaiting.screen(game)),
        "anti_aim": enabled,
        "anti_aim_move_rebase": move_rebase,
        "anti_aim_reason": status.get("anti_aim_reason"),
        "anti_aim_acted_samples": acting_samples,
        "anti_aim_writes_during_window": writes_after - writes_before,
        "anti_aim_rebase_applied_during_window": applied_after - applied_before,
        "anti_aim_fake_yaw": round(fake_yaws[-1], 1) if fake_yaws else None,
        "anti_aim_camera_yaw": round(camera_yaws[-1], 1) if camera_yaws else None,
        # The gap between the fake yaw and the camera's own yaw: the split.
        "anti_aim_split_gap_deg": split_gap,
        "anti_aim_configured_gap_deg": round(configured_gap, 1),
        # The **delivered** axes and what they cost: the direction error
        # against the camera's own wish and the walk-speed factor.
        "anti_aim_forward": round(forwards[-1], 3) if forwards else None,
        "anti_aim_left": round(lefts[-1], 3) if lefts else None,
        "anti_aim_direction_error": round(errors[-1], 2) if errors else None,
        "anti_aim_wish_factor": round(factors[-1], 3) if factors else None,
        "anti_aim_saturated": bool(status.get("anti_aim_saturated")),
        "anti_aim_yaw_offset": status.get("anti_aim_yaw_offset"),
        "anti_aim_pitch": status.get("anti_aim_pitch"),
        "anti_aim_jitter": status.get("anti_aim_jitter"),
        "anti_aim_side": status.get("anti_aim_side"),
    }

    if not enabled:
        result["verdict"] = ("the anti-aim is OFF (enable it with the `antiaim` "
                             "control command) â€” nothing to observe")
        result["skipped"] = True
        return result

    if not move_rebase:
        # The movement re-base is switched off: the fake angles are in place
        # and the movement's wish basis follows them, so the operator's walk
        # turns with the desync. That is a diagnostic, not a mode to play in.
        result["verdict"] = ("the anti-aim is configured but its movement re-base is "
                             "OFF: the fake angles rotate the walk basis â€” the "
                             "operator's controls would follow the desync")
        result["skipped"] = True
        return result

    if acting_samples == 0 or writes_after == writes_before:
        raise SessionError(
            "the anti-aim is enabled and its re-base is on, but the payload never "
            f"acted: acting samples={acting_samples}, "
            f"anti_aim_writes +{writes_after - writes_before} ({result})"
        )

    # The **split**: the gap must not collapse. With a configured offset of ~0
    # the fake yaw *is* the camera's yaw by construction, so there is nothing to
    # check â€” the check is skipped rather than failed.
    if configured_gap <= 1.0:
        result["split_check"] = ("skipped: the configured offset is ~0, so the fake "
                                 "yaw equals the camera's yaw by construction")
    elif split_gap is not None and split_gap < configured_gap * 0.5:
        raise SessionError(
            "the camera followed the fake angles: the gap between the fake yaw and "
            f"the camera's own yaw collapsed to {split_gap} deg (the configured "
            f"offset is {round(configured_gap, 1)} deg) â€” the split is broken "
            f"({result})"
        )
    else:
        result["split_check"] = (f"the gap held at {split_gap} deg (the configured "
                                  f"offset is {round(configured_gap, 1)} deg)")

    result["verdict"] = (
        f"the anti-aim acted in {acting_samples} samples "
        f"({writes_after - writes_before} writes, {applied_after - applied_before} "
        f"re-bases); the camera's own yaw and the fake yaw differ by {split_gap} deg "
        f"(the camera did not follow); the delivered walk direction is "
        f"{round(errors[-1], 2) if errors else 0} deg off the camera's and the walk "
        f"speed is at {round(factors[-1], 3) if factors else 1} of normal"
    )
    if bool(status.get("anti_aim_saturated")):
        result["verdict"] += (" â€” the re-base SATURATED (the fake pitch tilts the "
                              "walk's basis): the direction or the speed is given up")
    return result
