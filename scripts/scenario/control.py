"""Client for the payload's control channel (ADR-0020).

The payload inside ``cs2.exe`` (Windows session 1) owns the named-pipe *server*;
the scenario runner is the *client*. Requests and responses are newline-delimited
JSON, one object per line::

    {"id": 1, "cmd": "exec", "args": {"line": "sv_cheats 1"}}
    {"id": 1, "ok": true, "result": {"queued": true}}

Commands: ``ping``, ``state``, ``exec``, ``wait_for``, ``quit``
(see ``docs/control-harness.md``).
"""

from __future__ import annotations

import itertools
import json
import os
import time
from typing import Any

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
        """Liveness + readiness: ``{pong, pid, exec_ready, state_ready}``."""
        return self.request("ping")

    def state(self) -> dict[str, Any]:
        """The current snapshot: ``{valid, in_map, local, players}``."""
        return self.request("state")

    def exec(self, line: str) -> dict[str, Any]:
        """Queue a (``;``-separated) console line in the engine's Cbuf."""
        return self.request("exec", {"line": line})

    def wait_for(self, *, timeout: float = 30.0, **predicates: Any) -> dict[str, Any]:
        """Poll until ``predicates`` hold; returns ``{matched, snapshot}``.

        ``predicates`` is the structured expression described in
        ``docs/control-harness.md`` (``local_alive=True``, ``players_gte=3``,
        ``team="T"``, ``in_map=True``, ``all=[...]``, ...). With no predicate it
        waits for the local player to exist.
        """
        expr: dict[str, Any] = dict(predicates) if predicates else {"in_map": True}
        return self.wait_for_expr(expr, timeout=timeout)

    def wait_for_expr(self, expr: dict[str, Any], timeout: float = 30.0) -> dict[str, Any]:
        """Like :meth:`wait_for` but takes the expression object directly."""
        return self.request(
            "wait_for", {"expr": expr, "timeout_ms": int(timeout * 1000)}
        )

    # --- input (engine button commands + setang) --------------------------
    def press(self, key: str, hold: float = 0.0) -> dict[str, Any]:
        """Hold a key/button; with ``hold`` seconds, release it again.

        ``key`` is a friendly name (``"w"``, ``"space"``, ``"mouse1"``, ...) or
        a raw button command name (``"forward"``). With ``hold=0`` the button
        stays held until :meth:`release` (or the game clears it).
        """
        button = BUTTONS.get(key.lower(), key)
        self.exec(f"+{button}")
        if hold > 0:
            time.sleep(hold)
            self.exec(f"-{button}")
        return {"button": button, "held": hold <= 0}

    def release(self, key: str) -> dict[str, Any]:
        """Release a button previously held with :meth:`press`."""
        button = BUTTONS.get(key.lower(), key)
        return self.exec(f"-{button}")

    def look(self, *, yaw: float | None = None, pitch: float | None = None,
             roll: float | None = None, timeout: float = 5.0) -> dict[str, Any]:
        """Aim the view via the engine ``setang`` command (needs ``sv_cheats 1``).

        Omitted components keep their current value (read from ``state``). Blocks
        until the engine has applied the angles (``setang`` is queued in the Cbuf
        and takes effect on the next frame). Returns the applied angles.
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
        self.exec(f"setang {target['pitch']:.3f} {target['yaw']:.3f} {target['roll']:.3f}")
        return self.wait_look(yaw=target["yaw"], pitch=target["pitch"],
                              roll=target["roll"], timeout=timeout)

    def wait_look(self, *, yaw: float | None = None, pitch: float | None = None,
                  roll: float | None = None, tolerance: float = 1.5,
                  timeout: float = 5.0) -> dict[str, Any]:
        """Wait until the local eye angles reach the given values."""
        deadline = time.monotonic() + timeout
        while True:
            local = self.state().get("local")
            if local is not None:
                angles = local["eye_angles"]
                wanted = (("yaw", yaw), ("pitch", pitch), ("roll", roll))
                if all(want is None or abs(angles[name] - want) <= tolerance
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
