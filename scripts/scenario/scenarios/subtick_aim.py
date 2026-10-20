"""Scenario: write the sub-tick view angles through the payload (silent aim).

Exercises the payload's `view_angles` write (`docs/re/subtick.md`): it reaches a
live round, selects the `usercmd` backend, and aims via `look(..., via="usercmd")`
— which writes the newest `input_history` entry's `view_angles` (the silent-aim
seam). It asserts the write lands every tick and that the **camera does not
follow** (the defining property of a silent angle write).

The full "the shot follows the silent angles" test needs a target in front and is
a follow-up (see `docs/control-harness.md` §"Next actions").
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

YAW_OFFSET = 90.0
MIN_ANGLE_WRITES = 10
CAMERA_TOLERANCE = 2.0


def run(game: Game, *, yaw_offset: float = YAW_OFFSET,
        map_name: str = "de_dust2") -> dict[str, Any]:
    """Write a silent view-angle offset; assert it lands and stays silent."""
    awaiting.ensure_live_round(game, map_name=map_name)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    awaiting.require(game, "look")  # the machine: can view angles be written here?

    local = game.local()
    camera = local["eye_angles"] if local else None
    if camera is None:
        raise SessionError("no local player")
    target_yaw = camera["yaw"] + yaw_offset
    game.look(yaw=target_yaw, pitch=camera["pitch"], roll=camera["roll"], via="usercmd")

    time.sleep(0.6)
    after = game.input()
    if after.get("angle_writes", 0) < MIN_ANGLE_WRITES:
        raise SessionError(f"sub-tick view angles not written: {after}")

    local = game.local()
    moved = local["eye_angles"] if local else None
    if moved is None or abs(moved["yaw"] - camera["yaw"]) > CAMERA_TOLERANCE:
        raise SessionError(f"angle write was not silent: camera {camera} -> {moved}")

    game.control.request("look", {"clear": True})
    return {
        "camera_yaw": camera["yaw"],
        "silent_yaw": target_yaw,
        "camera_unchanged": True,
        "angle_writes": after["angle_writes"],
    }
