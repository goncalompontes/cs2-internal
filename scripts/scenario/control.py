"""Client for the payload's control channel (ADR-0020).

The payload inside ``cs2.exe`` (Windows session 1) owns the named-pipe *server*;
the scenario runner is the *client*. Requests and responses are newline-delimited
JSON, one object per line::

    {"id": 1, "cmd": "exec", "args": {"line": "sv_cheats 1"}}
    {"id": 1, "ok": true, "result": {"queued": true}}

Commands: ``ping``, ``state``, ``exec``, ``events``, ``wait_for``, ``input``,
``press``, ``release``, ``bhop``, ``quit`` (see ``docs/control-harness.md``).
"""

from __future__ import annotations

import itertools
import json
import os
import time
from typing import Any


def _angle_delta(a: float, b: float) -> float:
    """Shortest absolute difference between two angles, in degrees."""
    delta = abs((a - b + 180.0) % 360.0 - 180.0)
    return delta

DEFAULT_PIPE = r"\\.\pipe\cs2-internal-control"

# Input is driven through the engine's own console commands (via `exec`), not
# synthetic OS input: the runner may be in session 0 while the game is in
# session 1, so SendInput from the runner would never reach it. Holding a
# `+<button>` command until the matching `-<button>` is the engine's own button
# state, so this is exact and build-independent (no offsets, no hooks).
#   buttons: https://developer.valvesoftware.com/wiki/Button_commands
BUTTONS = {
    # movement
    "w": "forward",
    "s": "back",
    "a": "moveleft",
    "d": "moveright",
    "space": "jump",
    "jump": "jump",
    "ctrl": "duck",
    "crouch": "duck",
    "shift": "speed",
    "walk": "speed",
    # action
    "mouse1": "attack",
    "attack": "attack",
    "mouse2": "attack2",
    "attack2": "attack2",
    "r": "reload",
    "reload": "reload",
    "e": "use",
    "use": "use",
    # view (turn/pitch; `look` uses `setang` for absolute angles)
    "left": "left",
    "right": "right",
    "up": "lookup",
    "down": "lookdown",
}


class ControlError(RuntimeError):
    """The channel is unusable, or the payload rejected a request."""


class Control:
    """A connection to the payload's control channel."""

    def __init__(self, pipe_name: str | None = None) -> None:
        self.pipe_name = pipe_name or os.environ.get("CS2_CONTROL_PIPE") or DEFAULT_PIPE
        self._pipe: Any = None
        self._buffer = bytearray()
        self._ids = itertools.count(1)

    # --- connection -------------------------------------------------------
    def connect(self, timeout: float = 30.0, interval: float = 0.25) -> "Control":
        """Open the pipe, retrying until ``timeout`` elapses."""
        deadline = time.monotonic() + timeout
        last_error: OSError | None = None
        while True:
            try:
                self._pipe = open(self.pipe_name, "r+b", buffering=0)
                self._buffer.clear()
                return self
            except OSError as error:
                last_error = error
                if time.monotonic() >= deadline:
                    raise ControlError(
                        f"cannot connect to {self.pipe_name!r}: {last_error}"
                    ) from error
                time.sleep(interval)

    def close(self) -> None:
        if self._pipe is not None:
            try:
                self._pipe.close()
            finally:
                self._pipe = None

    def __enter__(self) -> "Control":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    # --- framing ----------------------------------------------------------
    def _read_line(self) -> bytes:
        while True:
            newline = self._buffer.find(b"\n")
            if newline != -1:
                line = bytes(self._buffer[:newline])
                del self._buffer[: newline + 1]
                return line
            chunk = self._pipe.read(4096)
            if not chunk:
                raise ControlError("control channel closed")
            self._buffer.extend(chunk)

    def _write_all(self, payload: bytes) -> None:
        view = memoryview(payload)
        while view:
            written = self._pipe.write(view)
            if not written:
                raise ControlError("control channel closed while writing")
            view = view[written:]

    def request(self, cmd: str, args: dict[str, Any] | None = None) -> Any:
        """Send one request and return its ``result`` (or raise ControlError)."""
        if self._pipe is None:
            raise ControlError("not connected")
        request_id = next(self._ids)
        message: dict[str, Any] = {"id": request_id, "cmd": cmd}
        if args is not None:
            message["args"] = args
        self._write_all((json.dumps(message) + "\n").encode("utf-8"))
        while True:
            line = self._read_line()
            if not line:
                continue
            try:
                response = json.loads(line)
            except json.JSONDecodeError:
                continue  # ignore non-JSON noise
            if response.get("id") != request_id:
                continue  # a stale response for an earlier request
            if not response.get("ok"):
                raise ControlError(response.get("error", f"{cmd} failed"))
            return response.get("result")

    # --- commands ---------------------------------------------------------
    def ping(self) -> dict[str, Any]:
        """Liveness + readiness: ``{pong, pid, exec_ready, state_ready, events_ready}``."""
        return self.request("ping")

    def state(self) -> dict[str, Any]:
        """The current snapshot: ``{valid, in_map, local, players}``."""
        return self.request("state")

    def exec(self, line: str) -> dict[str, Any]:
        """Queue a (``;``-separated) console line in the engine's Cbuf."""
        return self.request("exec", {"line": line})

    def events(self, limit: int = 64, subscribe: str | list[str] | None = None) -> dict[str, Any]:
        """Recent game events captured by the payload listener.

        Returns ``{count, events: [{"name": ..., "seq": ...}, ...]}``, oldest
        first. ``subscribe`` registers one or more event names on demand (for
        events the payload was not already watching); names the game does not
        know are ignored by the manager.
        """
        args: dict[str, Any] = {"limit": limit}
        if subscribe is not None:
            args["subscribe"] = subscribe
        return self.request("events", args)

    def wait_for(self, *, timeout: float = 30.0, event: str | None = None,
                 event_args: dict[str, Any] | None = None,
                 **predicates: Any) -> dict[str, Any]:
        """Wait until ``predicates`` hold and (if given) ``event`` has fired.

        ``predicates`` is the structured expression described in
        ``docs/control-harness.md`` (``local_alive=True``, ``players_gte=3``,
        ``team="T"``, ``in_map=True``, ``all=[...]``, ...). With no predicate
        and no ``event`` it waits for the local player to exist.

        ``event`` matches a game event fired *after* this call started; the
        payload watches it on demand. ``event_args`` additionally requires the
        event to carry those field values, e.g.
        ``wait_for(event="player_death", event_args={"headshot": True})``.
        """
        args: dict[str, Any] = {"timeout_ms": int(timeout * 1000)}
        if predicates:
            args["expr"] = dict(predicates)
        elif event is None:
            args["expr"] = {"in_map": True}
        if event is not None:
            args["event"] = event
        if event_args:
            args["event_args"] = event_args
        return self.request("wait_for", args)

    def wait_for_expr(self, expr: dict[str, Any], timeout: float = 30.0) -> dict[str, Any]:
        """Like :meth:`wait_for` but takes the expression object directly."""
        return self.request(
            "wait_for", {"expr": expr, "timeout_ms": int(timeout * 1000)}
        )

    # --- input (engine button commands + setang) --------------------------
    def input(self, via: str | None = None) -> dict[str, Any]:
        """Select/query the input backend (``"console"`` or ``"usercmd"``).

        Returns the backend + hook status: ``{backend, ready, hooked, slot,
        buttons_set, buttons_clear, bhop, calls, writes}``.
        """
        args: dict[str, Any] = {}
        if via is not None:
            args["via"] = via
        return self.request("input", args)

    def press(self, key: str, hold: float = 0.0, via: str | None = None) -> dict[str, Any]:
        """Hold a key/button; with ``hold`` seconds, release it again.

        ``via="usercmd"`` writes the command's button mask through the payload's
        `CreateMove` hook (``docs/re/client-dll.md`` §"Input path"); otherwise
        the engine's own ``+<button>`` console command is used. ``key`` is a
        friendly name (``"w"``, ``"space"``, ``"mouse1"``, ...).
        """
        if via == "usercmd":
            name = key.lower()
            self.request("press", {"buttons": [name]})
            if hold > 0:
                time.sleep(hold)
                self.request("release", {"buttons": [name]})
            return {"button": name, "held": hold <= 0, "via": "usercmd"}
        button = BUTTONS.get(key.lower(), key)
        self.exec(f"+{button}")
        if hold > 0:
            time.sleep(hold)
            self.exec(f"-{button}")
        return {"button": button, "held": hold <= 0}

    def release(self, key: str, via: str | None = None) -> dict[str, Any]:
        """Release a button previously held with :meth:`press`."""
        if via == "usercmd":
            return self.request("release", {"buttons": [key.lower()]})
        button = BUTTONS.get(key.lower(), key)
        return self.exec(f"-{button}")

    def move(self, *, forward: float = 0.0, side: float = 0.0, up: float = 0.0,
             clear: bool = False, quantize: bool = False, step: bool = False,
             raw_entry: bool = False) -> dict[str, Any]:
        """Set the ``usercmd`` analog move (raw input axes + command + buttons).

        The payload writes the raw analog axes on ``CCSGOInput`` (``+0x270``) —
        the source ``create_move_input`` copies from — plus the command's move
        floats/buttons, before and after the original ``CreateMove``
        (``docs/re/subtick.md`` §"The command build"). Measure it with the
        ``subtick-move`` scenario.

        Three opt-in ingredients of the community recipe (all default off, so the
        historic write is unchanged), exercised by ``movement-probe``:

        ``quantize``
            snap the axes to the ``sv_quantize_movement_input`` valid set
            ``{-1, 0, +1}`` (the engine's own units — measured live 2026-10-15) so a
            server that rounds ``forwardmove``/``leftmove`` to integers still moves
            the player (UC 729264).
        ``step``
            also write the residual (``requested - quantized``) on the sub-tick
            analog channel (``CSubtickMoveStep``), which the engine adds on top of
            the base move (UC 761983).
        ``raw_entry``
            also write the per-controller input entry (``CCSGOInput + 0xb58``)
            that ``FUN_180b5f7b0`` copies into ``+0x270`` inside slot 5 — the
            documented "next probe" for the raw-input seam.
        """
        if clear:
            args: dict[str, Any] = {"clear": True}
        else:
            args = {"forward": forward, "side": side, "up": up}
            if quantize:
                args["quantize"] = True
            if step:
                args["step"] = True
            if raw_entry:
                args["raw_entry"] = True
        return self.request("move", args)

    def buttons(self, *, press: Any = None, release: Any = None,
                reset: bool = False) -> dict[str, Any]:
        """Raw ``usercmd`` button overrides (a name, array of names, or mask)."""
        if reset:
            self.request("press", {"reset": True})
        if release is not None:
            self.request("release", {"buttons": release})
        if press is not None:
            self.request("press", {"buttons": press})
        return self.input()

    def bhop(self, on: bool = True) -> dict[str, Any]:
        """Enable the payload's auto-jump (force ``IN_JUMP`` while grounded)."""
        return self.request("bhop", {"on": bool(on)})

    def strafe(self, on: bool = True, *, wishspeed: float | None = None,
               subtick: int | None = None) -> dict[str, Any]:
        """Toggle the payload's **auto-strafe** (airborne only, silent).

        While airborne and above ~25 units/s the payload takes the goal heading from
        the held keys in the view frame (``W``/nothing = the camera's forward, ``S``
        = behind; a held ``A``/``D`` stands it down — your own air control wins) and
        pushes the air-acceleration wish perpendicular to the *current* velocity on
        that side, re-deriving it every tick. Nothing writes a view angle, so the
        camera and the pawn's eye angles never move. On the ground it writes nothing.

        ``subtick`` (**default 8**, validated live at **+85.1 median / +104.9 max u/s
        per hop**, scaling with the slice count) keeps the analog on the valid set
        ``(1, 0)`` and rotates the movement basis with ``N`` ``yaw_delta`` steps (each
        set from a per-slice simulation, so every slice lands on the optimal angle —
        needs ``sv_subtick_movement_view_angles``); ``0`` puts the direction in the
        camera-frame analog instead (exact only when ``sv_quantize_movement_input`` is
        off, and the ``{-1,0,1}`` snap loses the perpendicular angle — measured nearly
        flat). ``wishspeed`` (default ~250 = the engine's clamped speed for a full
        analog) is the ``AirAccelerate`` budget. Returns the backend status
        (``strafe``, ``strafe_airborne``, ``strafe_speed``, ``strafe_wish``,
        ``strafe_rot``, ``strafe_side``, ``strafe_steps``, ``strafe_reason``,
        ``strafe_writes``).
        """
        args: dict[str, Any] = {"on": bool(on)}
        if wishspeed is not None:
            args["wishspeed"] = wishspeed
        if subtick is not None:
            args["subtick"] = subtick
        return self.request("strafe", args)

    def aim(self, *, target: int | None = None, objective: str | None = None,
            sigma: float | None = None, damage: float | None = None,
            range_modifier: float | None = None, penetration: float | None = None,
            headshot: float | None = None, write: bool | None = None,
            seed: int | None = None, sequence: int | None = None,
            spread: float | None = None, inaccuracy: float | None = None,
            pellets: int | None = None, cone: float | None = None) -> dict[str, Any]:
        """Ragebot solve (``docs/ragebot.md``).

        Solves the damage field for the nearest enemy (or the one at entity index
        ``target``) and returns the aim: ``{valid, yaw, pitch, value, cells,
        aim_u, aim_v, wrote_view}``. ``objective`` is ``"damage"`` (default),
        ``"hitchance"``, ``"expected"`` (uses ``sigma``) or ``"pellets"`` (the
        deterministic pellet-sum; the RNG seed is ``seed`` or ``sequence`` →
        ``MD5_PseudoRandom``, and ``spread``/``inaccuracy``/``pellets`` describe the
        cone — ``spread`` defaults to the weapon's real ``m_flSpread``).
        ``write=True`` applies the solve to the **silent** sub-tick view angles.
        """
        args: dict[str, Any] = {}
        if target is not None:
            args["target"] = target
        if objective is not None:
            args["objective"] = objective
        if sigma is not None:
            args["sigma"] = sigma
        if damage is not None:
            args["damage"] = damage
        if range_modifier is not None:
            args["range_modifier"] = range_modifier
        if penetration is not None:
            args["penetration"] = penetration
        if headshot is not None:
            args["headshot"] = headshot
        if seed is not None:
            args["seed"] = seed
        if sequence is not None:
            args["sequence"] = sequence
        if spread is not None:
            args["spread"] = spread
        if inaccuracy is not None:
            args["inaccuracy"] = inaccuracy
        if pellets is not None:
            args["pellets"] = pellets
        if cone is not None:
            args["cone"] = cone
        if write is not None:
            args["write"] = write
        return self.request("aim", args)

    def rage(self, *, on: bool | None = None, key: int | None = None,
             fire: bool | None = None, force: bool | None = None,
             objective: str | None = None, damage: float | None = None,
             headshot: float | None = None,
             range_modifier: float | None = None, hitbox: int | None = None,
             fov: float | None = None, min_damage: float | None = None,
             min_hitchance: float | None = None) -> dict[str, Any]:
        """Configure the hold-key ragebot (``docs/ragebot.md``).

        ``on`` enables the feature; ``key`` is the Win32 virtual-key code held to
        trigger it; ``fire`` presses attack while aiming; ``force`` bypasses the
        physical key check (the harness cannot synthesize one). ``objective`` is
        ``"damage"``/``"hitchance"``/``"expected"``; ``hitbox`` filters the solve
        (0 auto/head-first, 1 head only, 2 body only); ``fov`` limits the target cone
        (degrees); ``min_damage``/``min_hitchance`` gate the aim and fire. Returns
        ``{enabled, key, fire, force, active, target, hitbox, objective, fov,
        min_damage, min_hitchance, occluders, solves, writes, shots}``.
        """
        args: dict[str, Any] = {}
        if on is not None:
            args["on"] = on
        if key is not None:
            args["key"] = key
        if fire is not None:
            args["fire"] = fire
        if force is not None:
            args["force"] = force
        if objective is not None:
            args["objective"] = objective
        if damage is not None:
            args["damage"] = damage
        if headshot is not None:
            args["headshot"] = headshot
        if range_modifier is not None:
            args["range_modifier"] = range_modifier
        if hitbox is not None:
            args["hitbox"] = hitbox
        if fov is not None:
            args["fov"] = fov
        if min_damage is not None:
            args["min_damage"] = min_damage
        if min_hitchance is not None:
            args["min_hitchance"] = min_hitchance
        return self.request("rage", args)

    def heatmap(self, *, target: int | None = None, fov: float | None = None,
                history: int | None = None, sample: bool | None = None,
                clear: bool | None = None, all: bool | None = None,
                walls: bool | None = None, walls_call: bool | None = None,
                max_unlag: float | None = None, interp: float | None = None,
                damage: float | None = None,
                range_modifier: float | None = None, penetration: float | None = None,
                headshot: float | None = None, pen: str | None = None,
                pen_mod: float | None = None, dmg_mod: float | None = None,
                max_pen: int | None = None, material: int | None = None,
                grate: bool | None = None) -> dict[str, Any]:
        """The multi-target damage heatmap (``docs/ragebot.md``).

        Projects every enemy's hitboxes — and their sampled lag records — into one
        shared angular chart and returns the tagged silhouette cells, so the Python
        side can render an image. ``target`` centres the chart on an enemy's entity
        index; ``fov`` is the horizontal field of view covered by the chart (degrees);
        ``history`` is how many lag records per player to include; ``sample`` toggles
        the payload's continuous record sampling; ``clear`` empties the record store;
        ``all`` includes teammates as well as enemies; ``max_unlag``/``interp`` set
        the lag-comp window (`sv_maxunlag` / the interp seconds, default 0.2 /
        0.03125).

        ``pen`` selects the penetration model that values the occluded hitboxes
        (``docs/re/penetration.md``): ``"simple"`` (default) is the first-pass
        ``prod(1 - t*mod/m_flPenetration)``; ``"engine"`` is the engine's real
        per-surface loss with the per-surface modifiers (``pen_mod`` penetration
        modifier, ``dmg_mod`` damage modifier), the ``max_pen`` 4-wall budget and the
        ~90-unit thickness cap. Returns ``{valid, eye, basis, fov, extent, weapon,
        tick_base, rewind_tick, window_ticks, targets, cells, ...}``.
        """
        args: dict[str, Any] = {}
        if target is not None:
            args["target"] = target
        if fov is not None:
            args["fov"] = fov
        if history is not None:
            args["history"] = history
        if sample is not None:
            args["sample"] = sample
        if clear is not None:
            args["clear"] = clear
        if all is not None:
            args["all"] = all
        if walls is not None:
            args["walls"] = walls
        if walls_call is not None:
            args["walls_call"] = walls_call
        if max_unlag is not None:
            args["max_unlag"] = max_unlag
        if interp is not None:
            args["interp"] = interp
        if damage is not None:
            args["damage"] = damage
        if range_modifier is not None:
            args["range_modifier"] = range_modifier
        if penetration is not None:
            args["penetration"] = penetration
        if headshot is not None:
            args["headshot"] = headshot
        if pen is not None:
            args["pen"] = pen
        if pen_mod is not None:
            args["pen_mod"] = pen_mod
        if dmg_mod is not None:
            args["dmg_mod"] = dmg_mod
        if max_pen is not None:
            args["max_pen"] = max_pen
        if material is not None:
            args["material"] = material
        if grate is not None:
            args["grate"] = grate
        return self.request("heatmap", args)

    def look(self, *, yaw: float | None = None, pitch: float | None = None,
             roll: float | None = None, timeout: float = 5.0,
             via: str | None = None, command: bool = False, raw: bool = False,
             step_yaw: float | None = None) -> dict[str, Any]:
        """Aim the view.

        Default: the engine ``setang`` command (needs ``sv_cheats 1``), blocking
        until the eye angles reach the target. ``via="usercmd"`` instead writes the
        *sub-tick* ``view_angles`` through the payload (``docs/re/subtick.md``) —
        the silent-aim seam; it does not block (the camera may not follow).

        ``command=True`` additionally writes the command's **own** ``viewangles``
        (``base + 0x40``): measured live 2026-10-15, that is the channel movement's
        wishdir uses (the ``input_history`` entries do not steer movement) — but the
        pawn's eye angles follow it, so it is not silent.

        The *silent* movement-yaw candidates are ``raw=True`` (the raw per-sub-tick
        angle history ``CCSGOInput + 0xbd0``) and ``step_yaw=<degrees>`` (one
        ``CSubtickMoveStep`` carrying only a yaw delta) — see ``docs/re/subtick.md``
        §"The movement yaw channel". Omitted components keep their current value.
        """
        current = self.state().get("local")
        if current is None:
            raise ControlError("look requires an in-game local player")
        angles = current["eye_angles"]
        target = {
            "pitch": angles["pitch"] if pitch is None else pitch,
            "yaw": angles["yaw"] if yaw is None else yaw,
            "roll": angles["roll"] if roll is None else roll,
        }
        if via == "usercmd":
            request = dict(target)
            if command:
                request["command"] = True
            if raw:
                request["raw"] = True
            if step_yaw is not None:
                request["step_yaw"] = step_yaw
            self.request("look", request)
            return target
        self.exec(f"setang {target['pitch']:.3f} {target['yaw']:.3f} {target['roll']:.3f}")
        return self.wait_look(yaw=target["yaw"], pitch=target["pitch"],
                              roll=target["roll"], timeout=timeout)

    def wait_look(self, *, yaw: float | None = None, pitch: float | None = None,
                  roll: float | None = None, tolerance: float = 1.5,
                  timeout: float = 5.0) -> dict[str, Any]:
        """Wait until the local eye angles reach the given values.

        The comparison shortens to the **shortest angle** (``(-180, 180]``), so a
        target of ``180`` matches a reported ``-180`` — without that, any heading
        at or past due-south (180/225/270/315) can never converge and the wait
        times out even though the view is correct (observed 2026-10-15: the
        clearance sweep "failed" exactly those four headings).
        """
        deadline = time.monotonic() + timeout
        while True:
            local = self.state().get("local")
            if local is not None:
                angles = local["eye_angles"]
                wanted = (("yaw", yaw), ("pitch", pitch), ("roll", roll))
                if all(want is None or _angle_delta(angles[name], want) <= tolerance
                       for name, want in wanted):
                    return angles
            if time.monotonic() >= deadline:
                raise ControlError(
                    f"view angles did not reach yaw={yaw} pitch={pitch} within {timeout:.1f}s"
                )
            time.sleep(0.1)

    def noclip(self, enabled: bool = True) -> dict[str, Any]:
        """Toggle noclip (needs ``sv_cheats 1``) — handy for positioning."""
        self.exec("noclip 1" if enabled else "noclip 0")
        return {"noclip": enabled}

    def teleport(self, x: float, y: float, z: float | None = None) -> dict[str, Any]:
        """Teleport the local player (``setpos``; needs ``sv_cheats 1``)."""
        line = f"setpos {x:.3f} {y:.3f}" + ("" if z is None else f" {z:.3f}")
        return self.exec(line)

    def quit(self) -> None:
        """Ask the payload to stop the channel, then close the socket."""
        try:
            self.request("quit")
        finally:
            self.close()
