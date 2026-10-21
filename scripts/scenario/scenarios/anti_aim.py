"""Scenario: observe the **anti-aim** in a live round — observation-only.

The anti-aim writes the fake (desync) angles into the **command's own**
``viewangles``; the camera reads a **different** field (the live view angles,
``CCSGOInput + 0x688``), so the camera cannot follow the fake angles
(``docs/re/angle-split.md`` §5.2). This scenario checks exactly that, plus the
**movement re-base** — the analog that keeps the operator's W/A/S_D
camera-relative while the command carries the fake angles.

It fails only when the anti-aim is enabled, its gate is open and it does
**not** act, or when the camera *did* follow the fake angles. A disabled
anti-aim, or one whose ``move_crc`` gate is closed, is reported as **skipped**
with the reason — the gate is an open blocker
(``docs/re/angle-split.md`` §5.4), not a failure.

Read-only apart from what a live round needs: ``ensure_live_round`` is a no-op
when the round is already live, and no ``antiaim``/``strafe``/``move``/settings
write is ever sent — the scenario only *reads* the payload's own state.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

WATCH = 4.0     # how long to observe the anti-aim doing its thing
SAMPLE = 0.05


def _delta(first: float, second: float) -> float:
    """The signed difference ``first - second`` in degrees, wrapped to ±180."""
    return (first - second + 180.0) % 360.0 - 180.0


def run(game: Game, *, watch: float = WATCH, map_name: str = "de_dust2") -> dict[str, Any]:
    """Watch the payload's anti-aim in a live round and report the evidence.

    **Nothing is driven**: the scenario reads the payload's own ``input`` status
    while the feature runs and reports what it saw.
    """
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    status = game.input()
    enabled = bool(status.get("anti_aim"))
    gate = bool(status.get("anti_aim_rebase"))
    writes_before = int(status.get("anti_aim_writes") or 0)
    applied_before = int(status.get("anti_aim_rebase_applied") or 0)

    fake_yaws: list[float] = []
    camera_yaws: list[float] = []
    forwards: list[float] = []
    lefts: list[float] = []
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
        time.sleep(SAMPLE)

    status = game.input()
    writes_after = int(status.get("anti_aim_writes") or 0)
    applied_after = int(status.get("anti_aim_rebase_applied") or 0)

    # The **split**: the camera's own yaw and the fake yaw must differ — the
    # camera reads the live view angles, the anti-aim writes the command's, so
    # a camera that followed the fake angles would report the same number twice.
    split_gap = None
    if fake_yaws and camera_yaws:
        gaps = [abs(_delta(fake, camera)) for fake, camera in zip(fake_yaws, camera_yaws)]
        split_gap = round(min(gaps), 1)

    result: dict[str, Any] = {
        "screen": str(awaiting.screen(game)),
        "anti_aim": enabled,
        "anti_aim_gate": gate,
        "anti_aim_reason": status.get("anti_aim_reason"),
        "anti_aim_acted_samples": acting_samples,
        "anti_aim_writes_during_window": writes_after - writes_before,
        "anti_aim_rebase_applied_during_window": applied_after - applied_before,
        "anti_aim_fake_yaw": round(fake_yaws[-1], 1) if fake_yaws else None,
        "anti_aim_camera_yaw": round(camera_yaws[-1], 1) if camera_yaws else None,
        # The gap between the fake yaw and the camera's own yaw: the split.
        "anti_aim_split_gap_deg": split_gap,
        "anti_aim_forward": round(forwards[-1], 3) if forwards else None,
        "anti_aim_left": round(lefts[-1], 3) if lefts else None,
        "anti_aim_yaw_offset": status.get("anti_aim_yaw_offset"),
        "anti_aim_pitch": status.get("anti_aim_pitch"),
        "anti_aim_jitter": status.get("anti_aim_jitter"),
        "anti_aim_side": status.get("anti_aim_side"),
    }

    if not enabled:
        result["verdict"] = ("the anti-aim is OFF (enable it with the `antiaim` "
                             "control command) — nothing to observe")
        result["skipped"] = True
        return result

    if not gate:
        # The `move_crc` blocker: the command's CRC is never written by the
        # client and the engine's CRC routine over the move values was not
        # found, so the movement re-base — and with it the whole anti-aim — is
        # gated off (`docs/re/angle-split.md` §5.4). The payload reports the
        # gate rather than silently breaking the operator's movement.
        result["verdict"] = ("the anti-aim is configured but its `move_crc` gate is "
                             "CLOSED: the movement re-base is disabled, so the anti-aim "
                             "writes nothing (the command's CRC is not recomputed — "
                             "docs/re/angle-split.md §5.4)")
        result["skipped"] = True
        return result

    if acting_samples == 0 or writes_after == writes_before:
        raise SessionError(
            "the anti-aim is enabled and its gate is open, but the payload never "
            f"acted: acting samples={acting_samples}, "
            f"anti_aim_writes +{writes_after - writes_before} ({result})"
        )

    if split_gap is not None and split_gap < 1.0:
        raise SessionError(
            "the camera followed the fake angles (the fake yaw and the camera's own "
            f"yaw differ by only {split_gap} deg): the split is broken ({result})"
        )

    result["verdict"] = (
        f"the anti-aim acted in {acting_samples} samples "
        f"({writes_after - writes_before} writes, {applied_after - applied_before} "
        f"re-bases); the fake yaw and the camera's own yaw differ by {split_gap} deg "
        "(the camera did not follow)"
    )
    return result
