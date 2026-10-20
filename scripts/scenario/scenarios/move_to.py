"""Scenario: walk the local player to a saved world coordinate via sub-tick move.

Exercises the payload's ``usercmd`` analog move (``move(...)`` ->
``CCSGOInput + 0x270`` / ``CBaseUserCmdPB::forwardmove``) — **not** the console
``+forward``.

The command's move axes are in the *view* frame (``forwardmove`` along the view
yaw, ``leftmove`` to the view left), so each tick it takes the world-space
direction to the target and rotates it into the view frame using the player's
current yaw. That makes the walk independent of where the player is looking — no
``setang`` is issued.

Logs position / distance / move values / payload write counters every ``log``
seconds so a stalled walk is visible.

Usage::

    python scripts/run-scenario.py move-to --no-launch --no-inject --keep-running \
        --set x=-980 --set y=-754 --set tol=5
"""

from __future__ import annotations

import math
import struct
import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError


def _float_of(word: str) -> float:
    """Decode a hex qword from the payload's probe as a little-endian float."""
    packed = struct.pack("<I", int(word, 16) & 0xFFFFFFFF)
    (value,) = struct.unpack("<f", packed)
    return float(value)


def run(game: Game, *, x: float, y: float, z: float | None = None,
        tol: float = 5.0, timeout: float = 90.0, speed: float = 1.0,
        interval: float = 0.05, log: float = 0.5) -> dict[str, Any]:
    """Walk to ``(x, y)`` with the sub-tick move; return once within ``tol``."""
    awaiting.require(game, "move")
    game.input("usercmd")  # select the sub-tick backend

    target = {"x": float(x), "y": float(y)}
    start = time.monotonic()
    deadline = start + timeout
    next_log = start
    try:
        while True:
            local = game.local()
            if local is None:
                raise SessionError(f"no local player: {awaiting.round_state(game)}")
            origin = local.get("origin")
            angles = local.get("eye_angles")
            if origin is None or angles is None:
                raise SessionError(f"no local player: {awaiting.round_state(game)}")

            dx = target["x"] - origin["x"]
            dy = target["y"] - origin["y"]
            distance = math.hypot(dx, dy)
            if distance <= tol:
                return {
                    "reached": True,
                    "distance": round(distance, 2),
                    "target": target,
                    "origin": origin,
                    "yaw": angles["yaw"],
                    "seconds": round(time.monotonic() - start, 1),
                }
            if time.monotonic() > deadline:
                raise SessionError(
                    f"did not reach ({x:.1f}, {y:.1f}) within {timeout:.0f}s — "
                    f"{distance:.1f} units away at {origin}"
                )

            # World direction -> view frame (Source: forward=(cos,sin),
            # left=(-sin,cos), yaw in degrees, 0=+X, 90=+Y).
            yaw = math.radians(angles["yaw"])
            ux, uy = dx / distance, dy / distance
            forward = speed * (ux * math.cos(yaw) + uy * math.sin(yaw))
            side = speed * (-ux * math.sin(yaw) + uy * math.cos(yaw))
            game.move(forward=forward, side=side)

            now = time.monotonic()
            if now >= next_log:
                inp = game.input()
                base = inp.get("base_words", [])
                cmd_fwd = _float_of(base[11]) if len(base) > 11 else float("nan")
                print(
                    f"[{now - start:5.1f}s] dist={distance:8.1f} "
                    f"pos=({origin['x']:8.1f},{origin['y']:8.1f}) yaw={angles['yaw']:6.1f} "
                    f"move fwd={forward:7.1f} side={side:7.1f} | "
                    f"raw_writes={inp.get('raw_writes')} move_writes={inp.get('move_writes')} "
                    f"cmd+0x58={cmd_fwd:.1f}",
                    flush=True,
                )
                next_log = now + log

            time.sleep(interval)
    finally:
        game.move(clear=True)
