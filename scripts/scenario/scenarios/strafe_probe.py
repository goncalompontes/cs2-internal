"""Scenario: do the silent sub-tick view angles steer **movement**?

Auto-strafe needs two channels per tick: the analog wish direction (validated —
`move(forward=1.0)` = 1.01x the engine's own walk) and a **yaw change inside the
tick**. The payload can write view angles sub-tick (`look(..., via="usercmd")` fills
every `input_history` entry's `view_angles`) — but that is only proven to steer
*shots* (`subtick-shot`). Per the community,
`sv_subtick_movement_view_angles` (on by default) is what feeds those angles into
movement, so this scenario measures it directly:

| test | camera (`setang`) | write | expected direction if… |
|---|---|---|---|
| `console` | yaw 0 | engine `+forward` | 0 (reference walk) |
| `usercmd` | yaw 0 | `move(1.0)` | 0 |
| `usercmd+angle` | yaw 0 | `move(1.0)` **+ silent angles at yaw 90** | **90** if the angles steer movement, else 0 |
| `usercmd+spin` | yaw 0 | `move(1.0)` **+ silent angles spinning** | a curved path if they do |

The measurement is the **direction of the displacement** (`atan2(dy, dx)`), so a
sideways walk is unmistakable: the camera never moves (`setang` stays at 0) and the
walk is measured from one pinned spot with the teleport landed and zero velocity
(`awaiting.prepare`). Run it with `--set subtick_angles=0` to compare against
`sv_subtick_movement_view_angles 0`.
"""

from __future__ import annotations

import math
import time
from typing import Any

from .. import awaiting
from ..control import ControlError
from ..session import Game, SessionError

YAW = 0.0        # camera yaw for the whole test (Source: forward = (cos, sin))
ANGLE_OFFSET = 90.0   # the silent-angle offset whose effect we are testing
HOLD = 1.0
SPIN_RATE = 180.0     # deg/s while the spin case walks
SPIN_UPDATE = 0.05    # how often the runner re-writes the spinning angle
MIN_MOVE = 60.0


def _dir_yaw(before: dict[str, float], after: dict[str, float]) -> float | None:
    """Yaw of the displacement vector, in degrees (`None` when it did not move)."""
    dx = after["x"] - before["x"]
    dy = after["y"] - before["y"]
    if math.hypot(dx, dy) < 1.0:
        return None
    return round(math.degrees(math.atan2(dy, dx)) % 360.0, 1)


def _clear_angles(game: Game) -> None:
    """Stop writing the sub-tick angles (`look` with `clear`)."""
    game.control.request("look", {"clear": True})


def _reset_camera(game: Game, yaw: float) -> dict[str, Any]:
    """Put the camera back on the reference yaw and read it back.

    A command-`viewangles` write moves the *input view* as well and it **persists**
    across cases (measured 2026-10-15), so every case must start from a verified
    camera — otherwise a later case's "silent" reading is just the previous case's
    turn inherited.
    """
    game.look(yaw=yaw, pitch=0.0)  # engine `setang`, blocking
    camera = (awaiting.local(game).get("eye_angles") or {})
    eye = round(camera.get("yaw", 0.0), 1)
    delta = abs((eye - yaw + 180.0) % 360.0 - 180.0)
    return {"eye_yaw": eye, "on_reference": delta < 2.0}


def _walk(game: Game, home: dict[str, float], *, hold: float,
          spin: float | None = None, console: bool = False,
          command: bool = False) -> dict[str, Any]:
    """One measured walk from the pinned spot; returns displacement + direction."""
    result: dict[str, Any] = {"moved_units": 0.0, "dir_yaw": None}
    try:
        awaiting.require(game, "move")
        quiet = awaiting.prepare(game, home)
        before = awaiting.origin(game)
        eye_before = (awaiting.local(game).get("eye_angles") or {}).get("yaw")
        if console:
            game.press("w", hold=hold)
        else:
            game.move(forward=1.0)
            if spin is not None:
                start = time.monotonic()
                while time.monotonic() - start < hold:
                    angle = YAW + (time.monotonic() - start) * spin
                    game.look(yaw=angle, pitch=0.0, via="usercmd", command=command)
                    time.sleep(SPIN_UPDATE)
            else:
                time.sleep(hold)
            game.move(clear=True)
        after = awaiting.origin(game)
        status = game.input()
        camera = (awaiting.local(game).get("eye_angles") or {})
        moved = awaiting.displacement(before, after) if (before and after) else 0.0
        result.update({
            "moved_units": round(moved, 1),
            "dir_yaw": _dir_yaw(before, after) if (before and after) else None,
            "eye_yaw": round(camera.get("yaw", 0.0), 1),
            "eye_yaw_before": round(eye_before, 1) if eye_before is not None else None,
            "quiet": quiet,
            "angle_writes": status.get("angle_writes"),
            "command_angle_writes": status.get("command_angle_writes"),
            "command_angles": status.get("command_angles"),
            "origin": {"before": before, "after": after},
        })
    except (SessionError, ControlError) as error:
        result["error"] = str(error)[:160]
    finally:
        try:
            game.move(clear=True)
        except (SessionError, ControlError):
            pass
    return result


def run(game: Game, *, map_name: str = "de_dust2", hold: float = HOLD,
        offset: float = ANGLE_OFFSET, spin: float = SPIN_RATE,
        subtick_angles: int = 1, x: float | None = None, y: float | None = None,
        z: float | None = None) -> dict[str, Any]:
    """Measure whether silent sub-tick angles change the walk direction."""
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)
    if not game.input("usercmd").get("hooked"):
        raise SessionError("usercmd backend not hooked")
    awaiting.require(game, "move")

    # Stable environment: no bots to cycle the round under the measurement.
    game.exec("bot_kick")
    time.sleep(1.0)
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)
    game.exec(f"sv_subtick_movement_view_angles {1 if subtick_angles else 0}")

    if x is not None and y is not None:
        home = {"x": float(x), "y": float(y), "z": z}
        awaiting.recenter(game, home)
    else:
        home = awaiting.origin(game)
    game.look(yaw=YAW, pitch=0.0)  # the camera stays here for every case
    print(f"[strafe-probe] spot {home}, camera yaw {YAW}, "
          f"sv_subtick_movement_view_angles={1 if subtick_angles else 0}", flush=True)

    cases: dict[str, Any] = {}
    try:
        # Each case: reset+verify the camera, apply its channel, then walk. The
        # reset matters — a command-`viewangles` write moves the input view and it
        # persists, so without it a later "silent" case just inherits the turn.
        _clear_angles(game)
        _reset_camera(game, YAW)
        cases["console"] = _walk(game, home, hold=hold, console=True)
        _clear_angles(game)
        _reset_camera(game, YAW)
        cases["usercmd"] = _walk(game, home, hold=hold)

        # (a) The silent `input_history` angles, 90 degrees off the camera.
        _reset_camera(game, YAW)
        game.look(yaw=YAW + offset, pitch=0.0, via="usercmd")
        cases["usercmd+angle"] = _walk(game, home, hold=hold)

        # (b) The **raw** per-sub-tick angle history (`CCSGOInput + 0xbd0`) — a
        # silent candidate, so it runs before any command-`viewangles` write.
        _clear_angles(game)
        _reset_camera(game, YAW)
        game.look(yaw=YAW + offset, pitch=0.0, via="usercmd", raw=True)
        cases["raw+angle"] = _walk(game, home, hold=hold)

        # (c) A `CSubtickMoveStep` carrying only a yaw delta (`when = 1`) — the
        # other silent candidate.
        _clear_angles(game)
        _reset_camera(game, YAW)
        game.look(step_yaw=offset, via="usercmd")
        cases["step+yaw"] = _walk(game, home, hold=hold)

        # (d) The command's **own** `viewangles` (`base + 0x40`) — steerer, but the
        # eye angles follow it (the contrast case).
        _clear_angles(game)
        _reset_camera(game, YAW)
        game.look(yaw=YAW + offset, pitch=0.0, via="usercmd", command=True)
        cases["command+angle"] = _walk(game, home, hold=hold)

        # (e) A rotating command yaw (what an auto-strafe does per tick).
        cases["command+spin"] = _walk(game, home, hold=hold, spin=spin,
                                      command=True)
    finally:
        _clear_angles(game)
        game.move(clear=True)
        game.exec("sv_subtick_movement_view_angles 1")

    straight = (cases.get("usercmd") or {}).get("dir_yaw")

    def _turn(name: str) -> float | None:
        case = cases.get(name) or {}
        if case.get("dir_yaw") is None or straight is None:
            return None
        return round(abs((case["dir_yaw"] - straight + 180.0) % 360.0 - 180.0), 1)

    def _silent(name: str) -> bool | None:
        """Turned the walk *without* turning the pawn's eye angles.

        Both readings must be clean: the camera had to be on the reference yaw
        *before* the walk (else the turn is inherited) and the walk must have
        turned while the eye angles stayed there.
        """
        case = cases.get(name) or {}
        turn = _turn(name)
        eye = case.get("eye_yaw")
        eye_before = case.get("eye_yaw_before")
        if turn is None or eye is None or eye_before is None:
            return None
        started_clean = abs((eye_before - YAW + 180.0) % 360.0 - 180.0) < 5.0
        stayed = abs((eye - YAW + 180.0) % 360.0 - 180.0) < 10.0
        return bool(started_clean and turn > 20.0 and stayed)

    turns = {name: _turn(name) for name in
             ("usercmd+angle", "command+angle", "raw+angle", "step+yaw")}
    silent = {name: _silent(name) for name in turns}
    steerers = [name for name, value in turns.items() if value is not None and value > 20.0]
    silent_steerers = [name for name, value in silent.items() if value is True]
    return {
        "screen": str(awaiting.screen(game)),
        "spot": home,
        "camera_yaw": YAW,
        "angle_offset": offset,
        "subtick_movement_view_angles": 1 if subtick_angles else 0,
        "cases": cases,
        "moved_units": {name: case.get("moved_units") for name, case in cases.items()},
        "dir_yaw": {name: case.get("dir_yaw") for name, case in cases.items()},
        "eye_yaw": {name: case.get("eye_yaw") for name, case in cases.items()},
        "turn_vs_straight": turns,
        "silent_steering": silent,
        "steerers": steerers,
        "silent_steerers": silent_steerers,
        "verdict": (
            f"silent movement-yaw channel found: {silent_steerers} turns the walk "
            f"({turns}) without moving the eye angles"
            if silent_steerers else
            f"no silent channel yet — steerers {steerers} all move the eye angles "
            f"(command+angle eye_yaw={cases.get('command+angle', {}).get('eye_yaw')})"
            if steerers else
            "no channel steered the walk (inconclusive)"
        ),
    }
