"""Scenario session: launch CS2, inject the payload, connect to the control
channel, and tear down (ADR-0020).

Launching goes through the ``CS2Launch`` scheduled task, which starts the game in
the interactive session (session 1) where the GPU lives. The runner itself may be
in session 0; the payload's AF_UNIX socket is filesystem-local, so the channel still works.
See ``AGENTS.md`` ("Launching the game on Windows").
"""

from __future__ import annotations

import contextlib
import json
import os
import subprocess
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any, Iterator, Mapping, Sequence

from .control import Control, ControlError
from .protocol import (
    AimResult,
    EventsResult,
    ExecResult,
    HeatmapResult,
    InputBackend,
    InputStatus,
    LookResult,
    MoveResult,
    NoclipResult,
    PingResult,
    PlayerSnapshotDict,
    PressResult,
    RageResult,
    ReleaseResult,
    SettingsValue,
    StateSnapshotDict,
    WaitResult,
)

if TYPE_CHECKING:  # pragma: no cover - the import cycle (screens -> session)
    from .screens import Can, GameState, Machine, Screen

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
        manifest: dict[str, Any] = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise SessionError(f"bad dumper manifest: {error}") from error
    return manifest


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
    """A connected control session (thin wrapper over :class:`Control`).

    The wrappers are typed with the wire types of :mod:`.protocol`: the payload
    answers with a *validated* snapshot (``StateSnapshotDict``) and a validated
    ping, so the old ``dict[str, Any]`` annotations were wrong — a ``TypedDict``
    is not a ``dict[str, Any]`` to a type checker.

    The class satisfies the ``ControlTransport`` protocol of :mod:`.protocol`
    (the channel the state machine and the readiness layer program against).
    A runtime ``isinstance`` check only proves that the *attributes* are there,
    so the real check is **static**: ``tests/test_protocol.py`` assigns a
    ``Game`` to a ``ControlTransport``-typed variable, and the mypy gate
    (``scripts/typecheck.py``) compares the two signatures.
    """

    def __init__(self, control: Control) -> None:
        self.control = control

    def request(self, cmd: str, args: Mapping[str, Any] | None = None) -> Any:
        """One raw wire call — the escape hatch below the typed helpers."""
        return self.control.request(cmd, args)

    def ping(self) -> PingResult:
        return self.control.ping()

    def state(self) -> StateSnapshotDict:
        return self.control.state()

    def exec(self, line: str) -> ExecResult:
        return self.control.exec(line)

    def wait_for(self, *, timeout: float = 30.0, event: str | None = None,
                 event_args: Mapping[str, Any] | None = None,
                 **predicates: Any) -> WaitResult:
        return self.control.wait_for(timeout=timeout, event=event, event_args=event_args,
                                     **predicates)

    def events(self, limit: int = 64,
               subscribe: str | list[str] | None = None) -> EventsResult:
        """Recent game events: ``{count, events: [{name, seq, args}, ...]}``."""
        return self.control.events(limit=limit, subscribe=subscribe)

    def press(self, key: str, hold: float = 0.0,
              via: InputBackend | None = None) -> PressResult:
        return self.control.press(key, hold=hold, via=via)

    def release(self, key: str, via: InputBackend | None = None) -> ReleaseResult:
        return self.control.release(key, via=via)

    def input(self, via: InputBackend | None = None) -> InputStatus:
        """Select/query the input backend (``"console"`` / ``"usercmd"``)."""
        return self.control.input(via=via)

    def buttons(self, *, press: Any = None, release: Any = None,
                reset: bool = False) -> InputStatus:
        """Raw ``usercmd`` button overrides (names or a mask)."""
        return self.control.buttons(press=press, release=release, reset=reset)

    def bhop(self, on: bool = True) -> InputStatus:
        """Enable the payload's auto-jump."""
        return self.control.bhop(on)

    def animfix(self, on: bool = True) -> InputStatus:
        """Toggle the payload's **local animfix** (``docs/re/animfix.md``)."""
        return self.control.animfix(on)

    def strafe(self, on: bool = True, *, wishspeed: float | None = None,
               subtick: int | None = None) -> InputStatus:
        """Toggle the payload's auto-strafe (airborne only, silent)."""
        return self.control.strafe(on, wishspeed=wishspeed, subtick=subtick)

    def look(self, *, yaw: float | None = None, pitch: float | None = None,
             roll: float | None = None, timeout: float = 5.0,
             via: InputBackend | None = None, command: bool = False,
             raw: bool = False, step_yaw: float | None = None) -> LookResult:
        return self.control.look(yaw=yaw, pitch=pitch, roll=roll, timeout=timeout,
                                  via=via, command=command, raw=raw,
                                  step_yaw=step_yaw)

    def aim(self, **kwargs: Any) -> AimResult:
        """Ragebot diagnostic solve (``docs/ragebot.md``)."""
        return self.control.aim(**kwargs)

    def rage(self, **kwargs: Any) -> RageResult:
        """Configure the hold-key ragebot (``docs/ragebot.md``)."""
        return self.control.rage(**kwargs)

    def heatmap(self, **kwargs: Any) -> HeatmapResult:
        """The multi-target damage heatmap (``docs/ragebot.md``)."""
        return self.control.heatmap(**kwargs)

    def move(self, *, forward: float = 0.0, side: float = 0.0, up: float = 0.0,
             clear: bool = False, quantize: bool = False, step: bool = False,
             raw_entry: bool = False) -> MoveResult:
        """Set the ``usercmd`` analog move (`forward`/`side`), or `clear` it.

        ``quantize``/``step``/``raw_entry`` opt into the community ingredients of
        the movement write — see :meth:`Control.move`.
        """
        return self.control.move(forward=forward, side=side, up=up, clear=clear,
                                 quantize=quantize, step=step, raw_entry=raw_entry)

    def noclip(self, enabled: bool = True) -> NoclipResult:
        return self.control.noclip(enabled)

    def teleport(self, x: float, y: float, z: float | None = None) -> ExecResult:
        return self.control.teleport(x, y, z)

    def settings_get(self, fields: Sequence[str] | None = None) -> dict[str, Any]:
        """Read the feature settings block (the flat wire names)."""
        return self.control.settings_get(fields)

    def settings_set(self, values: Mapping[str, SettingsValue]) -> dict[str, Any]:
        """Write the feature settings block (a flag takes a bool or a number)."""
        return self.control.settings_set(values)

    def players(self) -> list[PlayerSnapshotDict]:
        """The snapshot's player blocks (the wire shape, not a plain dict)."""
        return self.state().get("players", [])

    def local(self) -> PlayerSnapshotDict | None:
        """The local player block (``None`` when there is none)."""
        return self.state().get("local")

    # --- screen state machine (see `.screens`) ----------------------------
    # The scenarios gate on these rather than re-deriving "is it safe to send
    # input?" from raw fields: `screen()` says which screen the game is in,
    # `require()` asserts a capability with the *reason*, and `ensure()` drives
    # the documented transitions to reach a screen.
    def state_machine(self, *, map_name: str = "de_dust2",
                      debug: bool = True) -> "Machine":
        """A :class:`~.screens.Machine` bound to this session."""
        from .screens import Machine

        return Machine(self, map_name=map_name, debug=debug)

    def screen(self) -> "Screen":
        """The current :class:`~.screens.Screen`."""
        from .awaiting import screen

        return screen(self)

    def game_state(self) -> "GameState":
        """The deduced :class:`~.screens.GameState` (screen + capabilities)."""
        from .awaiting import state

        return state(self)

    def require(self, capability: "Can | str") -> "GameState":
        """Fail (with a reason) unless ``capability`` is available now."""
        from .awaiting import require

        return require(self, capability)

    def ensure_screen(self, target: "Screen", *, map_name: str = "de_dust2",
                      **kwargs: Any) -> "GameState":
        """Drive the machine into ``target`` (see `.screens.Machine.ensure`)."""
        from .awaiting import ensure

        return ensure(self, target, map_name=map_name, **kwargs)

    # --- readiness / measurement (see `.awaiting`) ------------------------
    # Thin delegators so scenarios can write `game.ensure_live_round()`; the
    # logic lives in `.awaiting` (imported lazily to avoid an import cycle).
    def ensure_map(self, map_name: str = "de_dust2", timeout: float = 180.0) -> None:
        from .awaiting import ensure_map

        return ensure_map(self, map_name=map_name, timeout=timeout)

    def ensure_live_round(self, **kwargs: Any) -> "GameState":
        from .awaiting import ensure_live_round

        return ensure_live_round(self, **kwargs)

    def ensure_bot_match(self, **kwargs: Any) -> dict[str, Any]:
        """Join a map and set up a bot match (see `.awaiting.ensure_bot_match`)."""
        from .awaiting import ensure_bot_match

        return ensure_bot_match(self, **kwargs)

    def wait_airborne(self, timeout: float = 5.0) -> bool:
        from .awaiting import wait_airborne

        return wait_airborne(self, timeout=timeout)

    def wait_on_ground(self, timeout: float = 5.0) -> bool:
        from .awaiting import wait_on_ground

        return wait_on_ground(self, timeout=timeout)

    def measure_move(self, **kwargs: Any) -> dict[str, Any]:
        from .awaiting import measure_move

        return measure_move(self, **kwargs)


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
    control = Control(pipe_name)
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
