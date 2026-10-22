"""The tool catalogue: the scenario/control API as MCP tools.

One tool per **API area** — not one per command — so the catalogue stays
readable and an agent can find "the thing that moves the player" without
reading nineteen descriptions.  Every tool is a thin wrapper over the typed
library in :mod:`scenario` (the state machine, the control channel, the
scenario registry, the session launcher).

The important part is the **input schema**: it is *generated* from the typed
argument models below (see :mod:`scenario.mcp.schema`), so the type safety
reaches the tool boundary.  A tool whose arguments do not match the schema is
rejected by the server **before** the request reaches the game, and the
argument models are built from the same ``Literal`` aliases the payload's
grammar uses (:mod:`scenario.protocol`), so a typo in a button name or a
screen name is refused at the boundary rather than reaching the game.

The button names are the clearest case: ``cs2_press`` advertises an **enum**
of every name the console and the button mask know (the runner's aliases and
the engine's own actions), so ``"spcae"`` is refused before it can become the
console command ``+spcae`` — a command the engine answers with an error
nobody sees, i.e. a silent no-op.

The two tools that change the game (``cs2_launch`` and ``cs2_inject``) say so
in their description and require an explicit ``confirm`` — the MCP client is
an agent acting for a user, and "start the game" / "inject a DLL" are not
things an agent should do by accident.  ``cs2_exec`` is the escape hatch for
the console, and the commands that would end the game are refused there (see
:data:`DESTRUCTIVE_CONSOLE_COMMANDS`).
"""

from __future__ import annotations

from dataclasses import dataclass, field
import os
import pathlib
import subprocess
import sys
from typing import Any, Callable, Literal, Mapping, Required, Sequence, TypedDict

from .. import screens
from ..control import Control
from ..protocol import (
    AimObjective,
    ButtonName,
    InputBackend,
    SettingsValue,
    UsercmdButtonName,
    WaitExpr,
)
from ..scenarios import SCENARIOS
from ..screens import Screen
from ..session import CS2_TASK, SessionError
from .schema import schema_for
from .session import Session

__all__ = [
    "DESTRUCTIVE_CONSOLE_COMMANDS",
    "TOOLS",
    "Tool",
    "tool_definitions",
]


#: The console commands ``cs2_exec`` refuses: they end the game (or the
#: session), which is exactly what the server promises never to do.  The
#: engine's own ``quit`` is one of them.
DESTRUCTIVE_CONSOLE_COMMANDS: frozenset[str] = frozenset({"quit", "exit"})


# --- the argument models ---------------------------------------------------
# These are the *typed* shapes of the tool arguments.  They are built from the
# library's own aliases (`ButtonName`, `AimObjective`, `InputBackend`,
# `WaitExpr`, the `Screen` enum), so the generated schema and the runtime
# contract cannot drift apart.
class NoArgs(TypedDict):
    """No arguments at all."""


class ScreenArgs(TypedDict, total=False):
    """``cs2_screen``: how much history the deduction may look at."""

    events: int


class EnsureArgs(TypedDict, total=False):
    """``cs2_ensure``: drive the game into a screen."""

    target: Required[Screen]
    map_name: str
    timeout: float
    load_budget: float
    join_team: int
    verify_movable: bool
    pause_bots: bool


class ExecArgs(TypedDict):
    """``cs2_exec``: one console line."""

    line: str


class LookArgs(TypedDict, total=False):
    """``cs2_look``: the view angles."""

    yaw: float | None
    pitch: float | None
    roll: float | None
    timeout: float
    via: InputBackend | None
    command: bool
    raw: bool
    step_yaw: float | None


class MoveArgs(TypedDict, total=False):
    """``cs2_move``: the analog move axes."""

    forward: float
    side: float
    up: float
    clear: bool
    quantize: bool
    step: bool
    raw_entry: bool


class PressArgs(TypedDict, total=False):
    """``cs2_press``: one button (held for ``hold`` seconds, then released)."""

    #: The button name: the runner's aliases (``w``, ``space``, ``mouse1``,
    #: ...) and the engine's own actions (``forward``, ``jump``, ...).  The
    #: mask-only names (``moveleft``, ``moveright``, ``speed``, ``zoom``)
    #: need ``via="usercmd"``; the console path refuses them.
    key: Required[ButtonName | UsercmdButtonName]
    hold: float
    via: InputBackend | None


class ReleaseArgs(TypedDict, total=False):
    """``cs2_release``: one button that was held."""

    key: Required[ButtonName | UsercmdButtonName]
    via: InputBackend | None


class AimArgs(TypedDict, total=False):
    """``cs2_aim``: the ragebot's solve."""

    target: int | None
    objective: AimObjective | None
    sigma: float | None
    damage: float | None
    range_modifier: float | None
    penetration: float | None
    headshot: float | None
    write: bool | None
    seed: int | None
    sequence: int | None
    spread: float | None
    inaccuracy: float | None
    pellets: int | None
    cone: float | None


class RageArgs(TypedDict, total=False):
    """``cs2_rage``: the hold-key ragebot's configuration."""

    on: bool | None
    key: int | None
    fire: bool | None
    force: bool | None
    objective: AimObjective | None
    damage: float | None
    headshot: float | None
    range_modifier: float | None
    hitbox: int | None
    fov: float | None
    min_damage: float | None
    min_hitchance: float | None
    backtrack: bool | None


class EventsArgs(TypedDict, total=False):
    """``cs2_events``: the captured game events."""

    limit: int
    subscribe: list[str] | None
    since: int | None


class WaitForArgs(TypedDict, total=False):
    """``cs2_wait_for``: the structured predicate wait."""

    timeout: float
    event: str | None
    event_args: dict[str, Any] | None
    expr: WaitExpr | None


class SettingsGetArgs(TypedDict, total=False):
    """``cs2_settings_get``: read the feature settings block."""

    fields: list[str]


class SettingsSetArgs(TypedDict):
    """``cs2_settings_set``: write the feature settings block."""

    #: The value kinds the payload's ``write_field`` accepts: a **flag**
    #: takes a bool (``{"esp_box": true}``) or a number, a number field a
    #: number, a colour an array of numbers and a text a string.
    values: dict[str, SettingsValue]


class RunScenarioArgs(TypedDict, total=False):
    """``cs2_run_scenario``: run one of the built-in scenarios."""

    name: Required[str]
    args: dict[str, Any]


class LaunchArgs(TypedDict, total=False):
    """``cs2_launch``: start the game (destructive)."""

    confirm: Required[bool]


class InjectArgs(TypedDict, total=False):
    """``cs2_inject``: inject the payload (destructive)."""

    confirm: Required[bool]
    pid: int | None


class BuildArgs(TypedDict, total=False):
    """``cs2_build``: compile one of the Windows targets (or all of them)."""

    target: Literal["host", "payload", "injector", "all"]
    timeout: float


class TestArgs(TypedDict, total=False):
    """``cs2_test``: run one of the suites (or all of them)."""

    suite: Literal["host", "payload", "python", "all"]
    timeout: float


class OffsetsArgs(TypedDict, total=False):
    """``cs2_offsets``: refresh (or check) the offline offset dump."""

    check: bool


class PreflightArgs(TypedDict, total=False):
    """``cs2_preflight``: inspect the host for the build and live-run inputs."""

    timeout: float


class CalibrateArgs(TypedDict, total=False):
    """``cs2_calibrate``: resolve the interfaces and the schema from the game."""

    check: bool
    timeout: float


#: The aim arguments the ``aim`` command takes (the rest are ignored).
_AIM_KEYS = tuple(AimArgs.__annotations__)
#: The rage arguments the ``rage`` command takes.
_RAGE_KEYS = tuple(RageArgs.__annotations__)


# --- the tool --------------------------------------------------------------
@dataclass(frozen=True)
class Tool:
    """One MCP tool: a name, a description, a typed argument model and a body."""

    name: str
    description: str
    shape: type
    handler: Callable[[Mapping[str, Any], Session], Any]
    #: Properties whose schema is an enum of *runtime* values (the scenario
    #: registry).  The enum is generated from the registry, so it cannot drift
    #: away from the names that actually exist.
    enums: Mapping[str, Sequence[str]] = field(default_factory=dict)

    def input_schema(self, problems: list[str] | None = None) -> dict[str, Any]:
        """The generated JSON Schema of the arguments."""
        schema = schema_for(self.shape, problems=problems)
        for key, values in self.enums.items():
            if key in schema.get("properties", {}):
                schema["properties"][key] = {"type": "string", "enum": list(values)}
        return schema

    def definition(self, problems: list[str] | None = None) -> dict[str, Any]:
        """The ``tools/list`` entry of this tool."""
        return {
            "name": self.name,
            "description": self.description,
            "inputSchema": self.input_schema(problems),
        }


# --- the bodies ------------------------------------------------------------
def _optional(args: Mapping[str, Any], key: str) -> Any:
    """The value of an optional argument (``None`` when it was not given)."""
    value = args.get(key)
    return None if value is None else value


def _pick(args: Mapping[str, Any], keys: Sequence[str]) -> dict[str, Any]:
    """The given keys that the caller actually supplied."""
    return {key: args[key] for key in keys if key in args}


def _tool_ping(args: Mapping[str, Any], session: Session) -> Any:
    """Liveness: the payload's readiness flags and the command-surface version."""
    return session.call(lambda control: control.ping())


def _tool_state(args: Mapping[str, Any], session: Session) -> Any:
    """The typed snapshot (the local player, the players, the rules)."""
    return session.call(lambda control: control.state())


def _tool_screen(args: Mapping[str, Any], session: Session) -> Any:
    """The deduced screen, the capabilities and *why* each refusal happened."""
    events = int(args.get("events", 6))

    def action(game: Any) -> Any:
        state = screens.observe(game, events=events)
        report = state.as_dict()
        # The refusals are the point of the tool: an agent that may not move
        # needs to know *why*, not just that it may not.
        report["refused"] = {
            name: info["reason"]
            for name, info in report["capabilities"].items()
            if not info["allowed"]
        }
        return report

    return session.run(action)


def _tool_ensure(args: Mapping[str, Any], session: Session) -> Any:
    """Drive the game into the requested screen."""
    target = Screen(str(args["target"]))
    budget = float(args.get("timeout", 120.0))

    def action(game: Any) -> Any:
        machine = screens.Machine(
            game,
            map_name=str(args.get("map_name", "de_dust2")),
            # The machine's debug log goes to stdout — and stdout is the
            # protocol channel, so it must be off.
            debug=False,
        )
        state = machine.ensure(
            target,
            timeout=budget,
            load_budget=float(args.get("load_budget", 60.0)),
            join_team=int(args.get("join_team", 2)),
            verify_movable=bool(args.get("verify_movable", False)),
            pause_bots=bool(args.get("pause_bots", True)),
        )
        report = state.as_dict()
        report["steps"] = [entry["action"] for entry in machine.log]
        return report

    # The call's own deadline is the tool's timeout plus the margin the
    # transitions need: the call must not be cut short while the machine is
    # still driving the game into the screen.
    return session.run(action, timeout=budget + 60.0)


def _tool_exec(args: Mapping[str, Any], session: Session) -> Any:
    """Queue one console line."""
    line = str(args["line"])
    _refuse_destructive(line)
    return session.call(lambda control: control.exec(line))


def _refuse_destructive(line: str) -> None:
    """Refuse a console line that would end the game.

    The server promises that it never kills the game (``cs2_launch`` and
    ``cs2_inject`` are the only tools that change it), and the engine's own
    ``quit`` is a console command: without this gate ``cs2_exec`` would be
    the third way to end the game, and the promise would be a lie.  A console
    line may carry several commands (``a; b``), so every word of it is
    checked.
    """
    for part in line.replace(";", " ").split():
        command = part.lstrip("+").lower()
        if command in DESTRUCTIVE_CONSOLE_COMMANDS:
            raise SessionError(
                f"cs2_exec refuses {command!r}: it would end the game, and this "
                "server never kills the game"
            )


def _tool_look(args: Mapping[str, Any], session: Session) -> Any:
    """Aim the view."""
    def action(control: Control) -> Any:
        return control.look(
            yaw=_optional(args, "yaw"),
            pitch=_optional(args, "pitch"),
            roll=_optional(args, "roll"),
            timeout=float(args.get("timeout", 5.0)),
            via=_optional(args, "via"),
            command=bool(args.get("command", False)),
            raw=bool(args.get("raw", False)),
            step_yaw=_optional(args, "step_yaw"),
        )

    return session.call(action)


def _tool_move(args: Mapping[str, Any], session: Session) -> Any:
    """Set the analog move axes."""
    def action(control: Control) -> Any:
        return control.move(
            forward=float(args.get("forward", 0.0)),
            side=float(args.get("side", 0.0)),
            up=float(args.get("up", 0.0)),
            clear=bool(args.get("clear", False)),
            quantize=bool(args.get("quantize", False)),
            step=bool(args.get("step", False)),
            raw_entry=bool(args.get("raw_entry", False)),
        )

    return session.call(action)


def _tool_press(args: Mapping[str, Any], session: Session) -> Any:
    """Hold a button (and release it again when ``hold`` is given)."""
    def action(control: Control) -> Any:
        return control.press(
            str(args["key"]),
            hold=float(args.get("hold", 0.0)),
            via=_optional(args, "via"),
        )

    return session.call(action)


def _tool_release(args: Mapping[str, Any], session: Session) -> Any:
    """Release a button that was held."""
    def action(control: Control) -> Any:
        return control.release(str(args["key"]), via=_optional(args, "via"))

    return session.call(action)


def _tool_aim(args: Mapping[str, Any], session: Session) -> Any:
    """The ragebot's solve (the damage field, the aim, the hitboxes)."""
    return session.call(lambda control: control.aim(**_pick(args, _AIM_KEYS)))


def _tool_rage(args: Mapping[str, Any], session: Session) -> Any:
    """Configure the hold-key ragebot."""
    return session.call(lambda control: control.rage(**_pick(args, _RAGE_KEYS)))


def _tool_events(args: Mapping[str, Any], session: Session) -> Any:
    """The captured game events (with the ``since`` cursor)."""
    request: dict[str, Any] = {"limit": int(args.get("limit", 64))}
    subscribe = args.get("subscribe")
    if subscribe is not None:
        request["subscribe"] = list(subscribe)
    since = args.get("since")
    if since is not None:
        request["since"] = int(since)
    return session.call(lambda control: control.request("events", request))


def _tool_wait_for(args: Mapping[str, Any], session: Session) -> Any:
    """Wait for the structured predicate (and optionally a game event)."""
    timeout = float(args.get("timeout", 30.0))
    event = _optional(args, "event")
    event_args = _optional(args, "event_args")
    expr = args.get("expr")

    def action(control: Control) -> Any:
        if expr:
            # The expression is the predicate part; `not` is a Python keyword,
            # so it can only travel as an object — hence the merge.
            return control.wait_for(timeout=timeout, event=event,
                                    event_args=event_args, **dict(expr))
        return control.wait_for(timeout=timeout, event=event, event_args=event_args)

    return session.call(action)


def _tool_settings_get(args: Mapping[str, Any], session: Session) -> Any:
    """Read the feature settings block."""
    fields = args.get("fields") or []
    return session.call(lambda control: control.settings_get([str(name) for name in fields]))


def _tool_settings_set(args: Mapping[str, Any], session: Session) -> Any:
    """Write the feature settings block."""
    values = dict(args["values"])
    return session.call(lambda control: control.settings_set(values))


def _tool_list_scenarios(args: Mapping[str, Any], session: Session) -> Any:
    """The built-in scenarios."""
    entries = []
    for name in sorted(SCENARIOS):
        doc = (SCENARIOS[name].__doc__ or "").strip().splitlines()
        entries.append({"name": name, "summary": doc[0] if doc else ""})
    return {"scenarios": entries}


def _tool_run_scenario(args: Mapping[str, Any], session: Session) -> Any:
    """Run one of the built-in scenarios."""
    name = str(args["name"])
    run = SCENARIOS.get(name)
    if run is None:
        raise SessionError(f"unknown scenario {name!r} (known: {sorted(SCENARIOS)})")
    kwargs = dict(args.get("args") or {})

    def action(game: Any) -> Any:
        return run(game, **kwargs)

    return session.run(action)


def _tool_launch(args: Mapping[str, Any], session: Session) -> Any:
    """Start the game through the ``CS2Launch`` scheduled task."""
    if not args.get("confirm"):
        raise SessionError(
            "cs2_launch starts the game — pass confirm: true to acknowledge it"
        )
    from ..session import launch_cs2

    launch_cs2()
    # The old channel (if any) belongs to the previous instance.
    session.reset()
    return {"launched": True, "task": CS2_TASK}


def _tool_inject(args: Mapping[str, Any], session: Session) -> Any:
    """Inject the payload into the running game."""
    if not args.get("confirm"):
        raise SessionError(
            "cs2_inject injects the payload into the running game — pass "
            "confirm: true to acknowledge it"
        )
    from ..session import inject, process_manifest

    pid = args.get("pid")
    if pid is None:
        manifest = process_manifest()
        if manifest is None:
            raise SessionError(
                "cs2.exe is not running (the dumper manifest is empty) — start "
                "the game with cs2_launch first"
            )
        pid = int(manifest.get("pid") or 0)
        if not pid:
            raise SessionError("the dumper manifest has no pid for cs2.exe")
    inject(pid=int(pid))
    # The payload opens the pipe when it starts: the cached handle is stale.
    session.reset()
    return {"injected": True, "pid": int(pid)}


# --- the build/test tools --------------------------------------------------
# The one-time scripts are gone; these run the same commands behind the MCP.
_REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]


def _tail(text: str, limit: int = 6000) -> str:
    """The end of a command's output (the part a failure is in)."""
    return text if len(text) <= limit else "..." + text[-limit:]


def _run(command: Sequence[str] | str, timeout: float) -> dict[str, Any]:
    """Run one command in the repository and return a bounded result.

    A ``str`` is handed to the platform as-is (the shell form ``_shell`` emits); a
    sequence is quoted by the platform's own argument rules.
    """
    try:
        completed = subprocess.run(
            command if isinstance(command, str) else list(command),
            cwd=_REPO_ROOT, capture_output=True, text=True,
            timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired:
        return {"ok": False, "returncode": None, "timeout": True,
                "stdout": "", "stderr": f"timed out after {timeout:.0f}s"}
    except OSError as error:
        return {"ok": False, "returncode": None, "timeout": False,
                "stdout": "", "stderr": str(error)}
    return {"ok": completed.returncode == 0, "returncode": completed.returncode,
            "timeout": False, "stdout": _tail(completed.stdout),
            "stderr": _tail(completed.stderr)}


def _vcvars() -> pathlib.Path | None:
    """The Visual Studio vcvars64.bat, when this is Windows and it exists."""
    if os.name != "nt":
        return None
    vswhere = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    if vswhere.is_file():
        try:
            installation = subprocess.run(
                [str(vswhere), "-latest", "-property", "installationPath"],
                capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            installation = ""
        if installation:
            script = pathlib.Path(installation) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
            if script.is_file():
                return script
    return None


def _shell(command: str) -> str | list[str]:
    """A shell command with the MSVC environment, where the platform has one.

    Windows returns **one string**, not ``["cmd", "/c", …]``: the vcvars prefix is
    itself quoted (``call "C:\\Program Files\\…\\vcvars64.bat"``), and an argument
    list makes Python escape those quotes (``\\"…\\"``), so cmd then looks for a
    program literally named `"C:\\Program Files\\…"` — the build tools failed with
    ``'\\"C:\\Program Files\\…\\vcvars64.bat\\"' is not recognized`` before meson ever
    started. With ``/s`` cmd strips the outer quotes and runs the rest as written.
    """
    if os.name == "nt":
        vcvars = _vcvars()
        prefix = f'call "{vcvars}" >nul && ' if vcvars is not None else ""
        return f'cmd /d /s /c "{prefix}{command}"'
    return ["sh", "-c", command]


def _tool_build(args: Mapping[str, Any], session: Session) -> Any:
    """Compile the requested target(s); never needs the game."""
    target = str(args.get("target", "all"))
    timeout = float(args.get("timeout", 1800.0))
    targets = ["host", "payload", "injector"] if target == "all" else [target]
    results: dict[str, Any] = {}
    for name in targets:
        directory = {"host": "build", "payload": "build-payload",
                     "injector": "build-injector"}[name]
        results[name] = _run(_shell(f"meson compile -C {directory}"), timeout)
        if not results[name]["ok"]:
            break
    return {"ok": all(result["ok"] for result in results.values()), "targets": results}


def _tool_test(args: Mapping[str, Any], session: Session) -> Any:
    """Run the host, payload or Python suite (or all of them)."""
    suite = str(args.get("suite", "all"))
    timeout = float(args.get("timeout", 1800.0))
    suites = ["host", "payload", "python"] if suite == "all" else [suite]
    commands = {
        "host": _shell("meson test -C build --print-errorlogs"),
        "payload": _shell("meson test -C build-payload --print-errorlogs"),
        "python": [sys.executable, "-m", "unittest", "discover",
                   "-s", "scripts/scenario/tests", "-t", "scripts"],
    }
    results: dict[str, Any] = {}
    for name in suites:
        results[name] = _run(commands[name], timeout)
        if not results[name]["ok"]:
            break
    return {"ok": all(result["ok"] for result in results.values()), "suites": results}


def _tool_offsets(args: Mapping[str, Any], session: Session) -> Any:
    """Refresh the offsets from the module on disk (or compare, writing nothing)."""
    command = [sys.executable, "scripts/resolve-offsets-offline.py"]
    if bool(args.get("check", False)):
        command.append("--check")
    return _run(command, 600.0)


def _tool_preflight(args: Mapping[str, Any], session: Session) -> Any:
    """Inspect the host for the toolchain, the launch task and the payload."""
    timeout = float(args.get("timeout", 300.0))
    return _run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", "scripts/preflight-windows.ps1"], timeout)


def _tool_calibrate(args: Mapping[str, Any], session: Session) -> Any:
    """Resolve the live interfaces and schema (the game must be running)."""
    timeout = float(args.get("timeout", 600.0))
    return _run([sys.executable, "scripts/calibrate-live.py"], timeout)


# --- the catalogue ---------------------------------------------------------
TOOLS: tuple[Tool, ...] = (
    Tool(
        name="cs2_ping",
        description=(
            "Liveness and readiness of the payload inside the running game: the "
            "pong flag, the game's pid, how long the payload has been up and "
            "which subsystems are ready (exec, events, input, state). Also the "
            "command-surface version, so a client can tell whether the payload "
            "knows a command it wants to use. Needs the game to be running."
        ),
        shape=NoArgs,
        handler=_tool_ping,
    ),
    Tool(
        name="cs2_state",
        description=(
            "The full typed snapshot of the game: the local player, every "
            "player (name, weapon, team, health, position, eye angles) and the "
            "round rules (freeze, warmup, bomb, buy window, game phase). This "
            "is the raw truth; cs2_screen reduces it to a screen and its "
            "capabilities. Needs the game to be running."
        ),
        shape=NoArgs,
        handler=_tool_state,
    ),
    Tool(
        name="cs2_screen",
        description=(
            "The screen the game is in (live, freeze, dead, team-select, "
            "loading, planted, buy-time, ...), the capabilities that screen "
            "allows, and — for every refused capability — the reason it is "
            "refused ('blocked: dead does not allow move'). Read this before "
            "acting: it says whether an input would be ignored right now. "
            "Needs the game to be running."
        ),
        shape=ScreenArgs,
        handler=_tool_screen,
    ),
    Tool(
        name="cs2_ensure",
        description=(
            "Drive the game into a screen (live, freeze, team-select, ...) "
            "through the documented transitions: it loads a map, joins a team, "
            "ends the warmup and waits for the round. This CHANGES the game "
            "state and can take minutes; the timeout bounds the wait. Use it "
            "when the screen is not the one a test needs. Needs the game to be "
            "running."
        ),
        shape=EnsureArgs,
        handler=_tool_ensure,
    ),
    Tool(
        name="cs2_exec",
        description=(
            "Queue one console line in the engine (e.g. 'sv_cheats 1', "
            "'jointeam 2', 'mp_restartgame 1'). The line is queued, not run "
            "synchronously — the effect shows up in the next state read. This "
            "is the escape hatch for anything the typed tools do not cover. "
            "The commands that would end the game ('quit', 'exit') are "
            "refused: this server never kills the game. Needs the game to be "
            "running."
        ),
        shape=ExecArgs,
        handler=_tool_exec,
    ),
    Tool(
        name="cs2_look",
        description=(
            "Aim the view. By default the engine's 'setang' console command is "
            "used and the call waits until the eye angles reach the target; "
            "via='usercmd' writes the silent sub-tick view angles instead "
            "(the camera may not follow). Omitted components keep their "
            "current value. Needs the game to be running."
        ),
        shape=LookArgs,
        handler=_tool_look,
    ),
    Tool(
        name="cs2_move",
        description=(
            "Set the analog move axes (forward/side/up, -1..1). The default "
            "backend writes the raw analog axes and the command's move floats; "
            "clear=true releases them, quantize/step/raw_entry opt into the "
            "community's movement recipe. Needs the game to be running."
        ),
        shape=MoveArgs,
        handler=_tool_move,
    ),
    Tool(
        name="cs2_press",
        description=(
            "Hold a button ('w', 'space', 'mouse1', 'jump', ...). With hold>0 "
            "the button is released again after that many seconds; with hold=0 "
            "it stays held until cs2_release. via='usercmd' writes the "
            "command's button mask through the payload's hook instead of the "
            "engine's console command. The key must be one of the known "
            "button names (the runner's aliases and the engine's own "
            "actions); a name the engine has no action for is refused rather "
            "than sent as an unknown console command. Needs the game to be "
            "running."
        ),
        shape=PressArgs,
        handler=_tool_press,
    ),
    Tool(
        name="cs2_release",
        description=(
            "Release a button that cs2_press (or a console '+button') held. "
            "Needs the game to be running."
        ),
        shape=ReleaseArgs,
        handler=_tool_release,
    ),
    Tool(
        name="cs2_aim",
        description=(
            "The ragebot's solve: the damage field around the nearest enemy, "
            "the aim (yaw/pitch), the hit chance and the hitboxes. "
            "objective picks what is maximised ('damage', 'hitchance', "
            "'expected', 'pellets'); write=true applies the solve to the "
            "silent sub-tick view angles. Needs the game to be running."
        ),
        shape=AimArgs,
        handler=_tool_aim,
    ),
    Tool(
        name="cs2_rage",
        description=(
            "Configure the hold-key ragebot: which virtual key triggers it, "
            "whether it fires, the objective, the hitbox filter, the fov and "
            "the damage/hit-chance gates. The result reports the ragebot's "
            "state (enabled, active, target, solves, writes, shots). Needs "
            "the game to be running."
        ),
        shape=RageArgs,
        handler=_tool_rage,
    ),
    Tool(
        name="cs2_events",
        description=(
            "The game events the payload captured (round_start, player_death, "
            "bomb_planted, item_purchase, ...), oldest first. 'since' filters "
            "to the events after a sequence number, so a poll loop can use the "
            "last_seq of the previous answer as its cursor. 'subscribe' asks "
            "the payload to watch an event it does not watch yet. Needs the "
            "game to be running."
        ),
        shape=EventsArgs,
        handler=_tool_events,
    ),
    Tool(
        name="cs2_wait_for",
        description=(
            "Wait until a predicate holds (and optionally until a game event "
            "fires). The predicate is the payload's own grammar: in_map, "
            "local_alive, on_ground, playable (bools), players_gte, "
            "alive_gte, local_hp_gte (ints), team ('T'/'CT'), and the "
            "combinators all/any/not. With no predicate it waits for the "
            "local player to exist. The keys are checked here down to four "
            "levels of nesting; a key deeper than that is refused by the "
            "payload (which reports it as an error), not by this tool. "
            "Needs the game to be running."
        ),
        shape=WaitForArgs,
        handler=_tool_wait_for,
    ),
    Tool(
        name="cs2_settings_get",
        description=(
            "Read the feature settings block (the ESP switches, the third "
            "person camera, the chams, the movement features). With no fields "
            "every setting is returned — that is the cheapest way to learn the "
            "field names; with 'fields' only those are returned. The names are "
            "the flat wire names ('esp_box', 'movement_thirdperson_distance')."
        ),
        shape=SettingsGetArgs,
        handler=_tool_settings_get,
    ),
    Tool(
        name="cs2_settings_set",
        description=(
            "Write the feature settings block: a name -> value object "
            "('esp_box': 1, 'esp_box': true, 'movement_thirdperson_distance': "
            "64). A switch takes a boolean (true/false) or a number, a "
            "number field a number, a colour a list of four numbers and a "
            "text a string. The whole request is validated before anything "
            "is written, so a typo in one key leaves the block untouched. A "
            "value that does not change is not published, and the answer "
            "lists the keys that actually changed. Use cs2_settings_get to "
            "learn the names. Needs the game to be running."
        ),
        shape=SettingsSetArgs,
        handler=_tool_settings_set,
    ),
    Tool(
        name="cs2_list_scenarios",
        description=(
            "The built-in scenarios (the same ones 'run-scenario.py' runs) with "
            "a one-line summary of each."
        ),
        shape=NoArgs,
        handler=_tool_list_scenarios,
    ),
    Tool(
        name="cs2_run_scenario",
        description=(
            "Run one of the built-in scenarios by name. A scenario drives the "
            "game (it may load a map, join a team, add bots, kill the player) "
            "and returns its report as JSON; 'args' are the scenario's own "
            "keyword arguments. This is the long way round — prefer the "
            "focused tools unless the scenario is exactly what you need."
        ),
        shape=RunScenarioArgs,
        handler=_tool_run_scenario,
        enums={"name": sorted(SCENARIOS)},
    ),
    Tool(
        name="cs2_launch",
        description=(
            "DESTRUCTIVE: start the game. The game is started through the "
            "'CS2Launch' scheduled task, because the agent runs in session 0 "
            "without a GPU and cs2.exe would die there. The tool never kills "
            "anything. Pass confirm: true to acknowledge that the game is "
            "started."
        ),
        shape=LaunchArgs,
        handler=_tool_launch,
    ),
    Tool(
        name="cs2_inject",
        description=(
            "DESTRUCTIVE: manual-map the payload into the running game (the "
            "control channel only exists after the injection). The pid of the "
            "running cs2.exe is found automatically when it is not given. "
            "Pass confirm: true to acknowledge that a DLL is injected into the "
            "game."
        ),
        shape=InjectArgs,
        handler=_tool_inject,
    ),
    Tool(
        name="cs2_build",
        description=(
            "Compile the tree: the host build and tests' code ('host'), the "
            "injected payload ('payload'), the injector ('injector') or all "
            "three. Runs under the MSVC environment. Does not need the game."
        ),
        shape=BuildArgs,
        handler=_tool_build,
    ),
    Tool(
        name="cs2_test",
        description=(
            "Run a suite: the C++ host tests ('host'), the payload's clang-cl "
            "tests ('payload'), the scenario/Python suite ('python') or all "
            "three. Returns the command's tail, not its whole log."
        ),
        shape=TestArgs,
        handler=_tool_test,
    ),
    Tool(
        name="cs2_offsets",
        description=(
            "Resolve the curated signatures from the module on disk and write "
            "calibration/offsets.json (or check: true to compare and write "
            "nothing). No game is needed; the interfaces and the schema still "
            "need cs2_calibrate."
        ),
        shape=OffsetsArgs,
        handler=_tool_offsets,
    ),
    Tool(
        name="cs2_preflight",
        description=(
            "Check the machine for the build and live-run inputs: the MSVC "
            "toolchain, the build directories, the launch task, the payload's "
            "imports and the calibration files. Reports PASS/WARN/FAIL."
        ),
        shape=PreflightArgs,
        handler=_tool_preflight,
    ),
    Tool(
        name="cs2_calibrate",
        description=(
            "Resolve the live interfaces and the schema from the running game "
            "and write the calibration dumps. Needs the game to be running and "
            "the payload to be injected; cs2_offsets covers the offline half."
        ),
        shape=CalibrateArgs,
        handler=_tool_calibrate,
    ),
)


def tool_definitions(problems: list[str] | None = None) -> list[dict[str, Any]]:
    """The ``tools/list`` payload: every tool with its generated schema."""
    return [tool.definition(problems) for tool in TOOLS]
