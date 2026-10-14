"""Scenario session: launch CS2, inject the payload, connect to the control
channel, and tear down (ADR-0020).

Launching goes through the ``CS2Launch`` scheduled task, which starts the game in
the interactive session (session 1) where the GPU lives. The runner itself may be
in session 0; same-user named pipes cross sessions, so the channel still works.
See ``AGENTS.md`` ("Launching the game on Windows").
"""

from __future__ import annotations

import contextlib
import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any, Iterator

from .control import DEFAULT_PIPE, Control, ControlError

REPO_ROOT = Path(__file__).resolve().parents[2]
CS2_TASK = "CS2Launch"
PLAYER_PROCESS = "cs2.exe"
CLIENT_MODULE = "client.dll"


class SessionError(RuntimeError):
    """A session setup/teardown step failed."""


# --- tool locations -------------------------------------------------------
def payload_dll() -> Path:
    return REPO_ROOT / "build-payload" / "payload.dll"


def injector_exe() -> Path:
    return REPO_ROOT / "build-injector" / "injector.exe"


def dumper_exe() -> Path:
    name = "cs2-dumper.exe" if os.name == "nt" else "cs2-dumper"
    return REPO_ROOT / "external" / "dumper" / "target" / "release" / name


# --- process discovery ----------------------------------------------------
def _run(argv: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv, capture_output=True, text=True)


def process_manifest(process: str = PLAYER_PROCESS) -> dict[str, Any] | None:
    """Return the dumper's manifest (``{process, pid, modules}``) or ``None``."""
    binary = dumper_exe()
    if not binary.is_file():
        raise SessionError(f"dumper not built: {binary}")
    result = _run([str(binary), "--process", process, "--json"])
    if result.returncode != 0:
        return None
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise SessionError(f"bad dumper manifest: {error}") from error


def wait_for_process(timeout: float = 180.0, interval: float = 1.0) -> dict[str, Any]:
    """Wait until ``cs2.exe`` is running; return its manifest."""
    deadline = time.monotonic() + timeout
    while True:
        manifest = process_manifest()
        if manifest is not None:
            return manifest
        if time.monotonic() >= deadline:
            raise SessionError(f"{PLAYER_PROCESS} did not start within {timeout:.0f}s")
        time.sleep(interval)


def wait_for_module(timeout: float = 180.0, interval: float = 1.0) -> dict[str, Any]:
    """Wait until ``client.dll`` (the point the control channel works) is loaded."""
    deadline = time.monotonic() + timeout
    while True:
        manifest = process_manifest()
        if manifest is not None:
            names = {str(m.get("name", "")).lower() for m in manifest.get("modules", [])}
            if CLIENT_MODULE in names:
                return manifest
        if time.monotonic() >= deadline:
            raise SessionError(f"{CLIENT_MODULE} not loaded within {timeout:.0f}s")
        time.sleep(interval)


# --- actions --------------------------------------------------------------
def kill_cs2(wait: bool = True, timeout: float = 20.0) -> None:
    """Stop any running ``cs2.exe`` and wait for it to actually exit.

    Waiting matters: launching while a previous instance is still tearing down
    races the process list, and injecting into a terminating process fails with
    ``CreateRemoteThread`` ACCESS_DENIED.
    """
    if os.name != "nt":
        return
    _run(["taskkill", "/IM", PLAYER_PROCESS, "/F"])
    if not wait:
        return
    deadline = time.monotonic() + timeout
    while process_manifest() is not None:
        if time.monotonic() >= deadline:
            raise SessionError(f"{PLAYER_PROCESS} did not exit within {timeout:.0f}s")
        time.sleep(0.5)


def launch_cs2() -> None:
    """Start the game through the interactive-session scheduled task."""
    if os.name != "nt":
        raise SessionError("launching is Windows-only; use the CS2Launch task")
    result = _run(["schtasks", "/run", "/tn", CS2_TASK])
    if result.returncode != 0:
        raise SessionError(
            f"failed to run the {CS2_TASK} task: {result.stderr.strip() or result.stdout.strip()}"
        )


def inject(dll: Path | None = None, process: str = PLAYER_PROCESS, pid: int | None = None,
           load_missing: bool = True) -> None:
    """Manual-map ``payload.dll`` into the running game.

    ``pid`` targets a specific instance (preferred, so a stale process cannot be
    picked by name). ``load_missing`` passes the injector's ``--load-missing`` (a
    remote ``LoadLibraryW`` for import modules the target has not loaded yet);
    the game boots without a few system DLLs the payload may import, so it
    defaults on.
    """
    injector = injector_exe()
    if not injector.is_file():
        raise SessionError(f"injector not built: {injector}")
    image = dll or payload_dll()
    if not image.is_file():
        raise SessionError(f"payload not built: {image}")
    target = str(pid) if pid is not None else process
    argv = [str(injector), target, str(image)]
    if load_missing:
        argv.append("--load-missing")
    result = _run(argv)
    if result.returncode != 0:
        raise SessionError(
            f"injection failed: {result.stderr.strip() or result.stdout.strip()}"
        )


# --- high-level session ---------------------------------------------------
class Game:
    """A connected control session (thin wrapper over :class:`Control`)."""

    def __init__(self, control: Control) -> None:
        self.control = control

    def ping(self) -> dict[str, Any]:
        return self.control.ping()

    def state(self) -> dict[str, Any]:
        return self.control.state()

    def exec(self, line: str) -> dict[str, Any]:
        return self.control.exec(line)

    def wait_for(self, *, timeout: float = 30.0, **predicates: Any) -> dict[str, Any]:
        return self.control.wait_for(timeout=timeout, **predicates)

    def press(self, key: str, hold: float = 0.0) -> dict[str, Any]:
        return self.control.press(key, hold=hold)

    def release(self, key: str) -> dict[str, Any]:
        return self.control.release(key)

    def look(self, *, yaw: float | None = None, pitch: float | None = None,
             roll: float | None = None) -> dict[str, Any]:
        return self.control.look(yaw=yaw, pitch=pitch, roll=roll)

    def noclip(self, enabled: bool = True) -> dict[str, Any]:
        return self.control.noclip(enabled)

    def teleport(self, x: float, y: float, z: float | None = None) -> dict[str, Any]:
        return self.control.teleport(x, y, z)

    def players(self) -> list[dict[str, Any]]:
        return self.state().get("players", [])

    def local(self) -> dict[str, Any] | None:
        return self.state().get("local")


@contextlib.contextmanager
def cs2_session(
    *,
    launch: bool = True,
    inject_payload: bool = True,
    wait_for_client: bool = True,
    connect_timeout: float = 60.0,
    pipe_name: str | None = None,
    keep_running: bool = False,
    game_timeout: float = 180.0,
) -> Iterator[Game]:
    """Set up a control session and tear it down afterwards.

    Steps: (optionally) kill a stale instance, launch via ``CS2Launch``, wait for
    ``client.dll``, inject the payload, connect to the pipe and ``ping``.
    """
    if launch:
        kill_cs2()
        launch_cs2()
    manifest = wait_for_process(timeout=game_timeout)
    if wait_for_client:
        manifest = wait_for_module(timeout=game_timeout)
    if inject_payload:
        inject(pid=manifest.get("pid"))
    control = Control(pipe_name or DEFAULT_PIPE)
    control.connect(timeout=connect_timeout)
    try:
        ping = control.ping()
        if not ping.get("pong"):
            raise SessionError(f"unexpected ping response: {ping}")
        yield Game(control)
    finally:
        if keep_running:
            # Leave the payload's channel up so the next client can reconnect
            # (the service accepts a new client after a disconnect); only the
            # socket is closed here.
            control.close()
        else:
            try:
                control.quit()
            except ControlError:
                control.close()
            kill_cs2()
