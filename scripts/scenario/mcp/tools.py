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
screen name is a type error rather than a silent no-op.

The two tools that change the game (``cs2_launch`` and ``cs2_inject``) say so
in their description and require an explicit ``confirm`` — the MCP client is
an agent acting for a user, and "start the game" / "inject a DLL" are not
things an agent should do by accident.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Mapping, Required, Sequence, TypedDict

from .. import screens
from ..control import Control
from ..protocol import AimObjective, ButtonName, InputBackend, WaitExpr
from ..scenarios import SCENARIOS
from ..screens import Screen
from ..session import CS2_TASK, SessionError
from .schema import schema_for
from .session import Session

__all__ = [
    "TOOLS",
    "Tool",
    "tool_definitions",
]


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
    """``cs2_press`` / ``cs2_release``: one button."""

    key: Required[ButtonName | str]
    hold: float
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

    values: dict[str, int | float | list[int] | str]


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
            timeout=float(args.get("timeout", 120.0)),
            load_budget=float(args.get("load_budget", 60.0)),
            join_team=int(args.get("join_team", 2)),
            verify_movable=bool(args.get("verify_movable", False)),
            pause_bots=bool(args.get("pause_bots", True)),
        )
        report = state.as_dict()
        report["steps"] = [entry["action"] for entry in machine.log]
        return report

    return session.run(action)


def _tool_exec(args: Mapping[str, Any], session: Session) -> Any:
    """Queue one console line."""
    line = str(args["line"])
    return session.call(lambda control: control.exec(line))


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
    request: dict[str, Any] = {}
    if fields:
        request["get"] = [str(name) for name in fields]
    return session.call(lambda control: control.request("settings", request))


def _tool_settings_set(args: Mapping[str, Any], session: Session) -> Any:
    """Write the feature settings block."""
    values = dict(args["values"])
    return session.call(lambda control: control.request("settings", {"set": values}))


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
            "Needs the game to be running."
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
            "engine's console command. Needs the game to be running."
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
        shape=PressArgs,
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
            "local player to exist. Needs the game to be running."
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
            "('esp_box': 1, 'movement_thirdperson_distance': 64). The whole "
            "request is validated before anything is written, so a typo in one "
            "key leaves the block untouched. A value that does not change is "
            "not published, and the answer lists the keys that actually "
            "changed. Use cs2_settings_get to learn the names. Needs the game "
            "to be running."
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
)


def tool_definitions(problems: list[str] | None = None) -> list[dict[str, Any]]:
    """The ``tools/list`` payload: every tool with its generated schema."""
    return [tool.definition(problems) for tool in TOOLS]
