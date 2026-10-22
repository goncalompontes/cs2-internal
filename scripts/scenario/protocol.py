"""The wire contract of the payload's control channel (typed).

The payload inside ``cs2.exe`` answers the scenario runner over a JSON-RPC socket
with plain JSON, and nothing in that JSON is checked at runtime: a renamed
field is a ``KeyError`` three frames away, and a mistyped argument is an error
raised by the *other* side of the pipe. This module is the single home for the
wire types — one ``TypedDict`` per command's answer, the ``Literal``s for the
constrained strings, the recursive predicate expression of ``wait_for`` and the
``ControlTransport`` protocol the higher layers program against.

Two rules shape it:

* **The payload is the ground truth.** Every shape here mirrors
  ``payload/control/src/service.cpp`` (the ``do_*`` handlers) and
  ``payload/control/src/snapshot_json.cpp``; each type's docstring names the
  handler it mirrors, so a drift is a one-line fix.
* **Static types describe one build; the validator covers the rest.** The
  payload is a *separately built binary*, so a newer one may answer with a field
  this client does not know and an older one may omit a field it needs.
  :func:`validate_snapshot` and :func:`validate_ping` check the required keys at
  runtime and raise :class:`ProtocolError` — loudly and early, instead of three
  frames later. Extra keys are **accepted**: a newer payload may add fields, and
  that is not an error.

The module imports nothing from the rest of the package (the MCP server and the
tests use it on their own) and nothing outside the standard library.
"""

from __future__ import annotations

import types
from typing import (
    Any,
    Literal,
    Mapping,
    NotRequired,
    Protocol,
    Required,
    TypedDict,
    TypeGuard,
    Union,
    Unpack,
    cast,
    get_args,
    get_origin,
    get_type_hints,
    runtime_checkable,
)

__all__ = [
    "AimObjective",
    "AimResult",
    "AngleDict",
    "AnimfixResult",
    "BasisDict",
    "BhopResult",
    "ButtonName",
    "CONSOLE_BUTTONS",
    "CompoundDict",
    "ControlError",
    "ControlTransport",
    "EventsResult",
    "ExecResult",
    "GameEventDict",
    "HeatmapCellDict",
    "HeatmapEnum",
    "HeatmapPen",
    "HeatmapResult",
    "HeatmapTargetDict",
    "HeatmapWeaponDict",
    "HitboxDict",
    "InputBackend",
    "InputStatus",
    "LevelSnapshotDict",
    "LookResult",
    "MoveResult",
    "MoveSampleDict",
    "NoclipResult",
    "PingResult",
    "PlayerSnapshotDict",
    "PressResult",
    "ProtocolError",
    "RageMode",
    "RagePerfDict",
    "RageResult",
    "ReleaseResult",
    "RulesSnapshotDict",
    "SettingsValue",
    "StateSnapshotDict",
    "StepProbeDict",
    "StrafeResult",
    "TeamName",
    "TraceDifferenceDict",
    "TraceIntervalDict",
    "TraceOccluderDict",
    "TraceProbeEngineDict",
    "TraceProbeResult",
    "TraceRayDict",
    "TraceRayEngineDict",
    "TraceSolidKind",
    "TraceSpanDict",
    "TraceVerdict",
    "UsercmdButtonName",
    "Vec3Dict",
    "WallsDict",
    "WaitExpr",
    "WaitResult",
    "validate_ping",
    "validate_snapshot",
]


# --- errors ----------------------------------------------------------------
class ControlError(RuntimeError):
    """The channel is unusable, or the payload rejected a request.

    It lives here (and not in ``control.py``) so that the wire types can raise
    it without importing the transport — the validator is part of the contract,
    not of the client.
    """


class ProtocolError(ControlError):
    """The payload answered with a shape this client does not understand.

    A static type describes *one* build's expectations; the payload is a
    separately built binary. This is the runtime half of that gap: the client
    fails at the boundary, naming the offending key, instead of three frames
    later with a ``KeyError`` (or, worse, silently reading a wrong value).
    """


# --- the constrained strings ------------------------------------------------
#: The input backends the payload knows (``Service::do_input``).
InputBackend = Literal["console", "usercmd"]

#: The ragebot solve objectives (``Service::do_aim``).
#:
#: ``monte_carlo`` is the engine's own spread kernel, sampled
#: (``docs/ragebot.md`` §13).  It is in the literal because the literal
#: **is** the MCP tools' advertised schema: ``cs2_aim`` and ``cs2_rage``
#: validate their arguments against it before the request is sent, so an
#: objective missing here is an objective the tools refuse.
AimObjective = Literal["damage", "hitchance", "expected", "pellets", "monte_carlo"]

#: The penetration models the heatmap can be valued with (``Service::do_heatmap``).
HeatmapPen = Literal["simple", "engine"]

#: The occluder enumeration modes (a diagnostic; ``Service::do_heatmap``).
HeatmapEnum = Literal["pyramid", "box", "both"]

#: The ragebot's trigger modes (``Service::do_rage``: hold / always / toggle).
#: The payload's ``rage`` command takes them as a name or as 0/1/2; the
#: runner's ``rage()`` helper does not expose the argument (yet).
RageMode = Literal["hold", "always", "toggle"]

#: The team names the ``wait_for`` predicate accepts (``Predicate::from_json``:
#: ``"T"``/``"t"``/``2`` and ``"CT"``/``"ct"``/``3``; the server also reads the
#: *strings* ``"2"``/``"3"``, which the type does not spell out).
TeamName = Literal["T", "CT", "t", "ct", 2, 3]

#: The engine's own input actions in CS2 — the names a console command can
#: carry (the command is the action name with a ``+``/``-`` in front).
#:
#: CS2's actions are ``attack``, ``attack2``, ``back``, ``duck``, ``forward``,
#: ``jump``, ``klook``, ``left``, ``movedown``, ``moveup``, ``reload``,
#: ``right``, ``sprint``, ``strafe``, ``turndown``, ``turnleft``,
#: ``turnright``, ``turnup`` and ``use``. There is **no** ``+moveleft``,
#: ``+moveright``, ``+speed`` or ``+zoom``: the strings do not exist in
#: ``client.dll``.
CONSOLE_BUTTONS: frozenset[str] = frozenset({
    "attack", "attack2", "back", "duck", "forward", "jump", "klook", "left",
    "movedown", "moveup", "reload", "right", "sprint", "strafe", "turndown",
    "turnleft", "turnright", "turnup", "use",
})

#: The button names the **console** path understands: the engine's own input
#: actions and the runner's friendly aliases, which the ``BUTTONS`` table maps
#: onto those actions. Every name here is expressible as a console command.
#:
#: The set is deliberately the *union* of two families, because the console
#: path accepts both: the runner's aliases (``w``, ``space``, ``mouse1``, ...)
#: and the engine's own action names (``forward``, ``jump``, ``turnup``, ...).
#: The usercmd-only names are **not** here — they have no console command (see
#: :data:`UsercmdButtonName`).
#:
#: ``turnup`` / ``turndown`` are the engine's **console** commands the runner's
#: ``BUTTONS`` table maps ``up`` / ``down`` to (the console path drives
#: ``+turnup`` / ``+turndown`` — CS2 has no ``+lookup`` / ``+lookdown`` — and
#: the pitch actions set no button bit, so the mask has no name for them).
#: ``sprint`` is in both: the engine's ``sprint`` action carries the
#: ``IN_SPEED`` bit.
ButtonName = Literal[
    # the engine's own actions
    "attack", "attack2", "back", "duck", "forward", "jump", "klook", "left",
    "movedown", "moveup", "reload", "right", "sprint", "strafe", "turndown",
    "turnleft", "turnright", "turnup", "use",
    # the runner's friendly aliases (the ``BUTTONS`` table maps them onto the
    # engine's actions)
    "mouse1", "mouse2", "space", "w", "s", "a", "d", "ctrl", "crouch", "shift",
    "r", "e", "up", "down",
]

#: The names only the **usercmd** path understands.
#:
#: The payload's button mask spells the lateral buttons ``moveleft`` /
#: ``moveright`` and the sprint key ``speed``, and it has a ``zoom`` bit (the
#: engine's scope state) that no client action produces. CS2's console has
#: **no** ``+moveleft``, ``+moveright``, ``+speed`` or ``+zoom`` — the strings
#: do not exist in ``client.dll`` — so these names are only usable with
#: ``via="usercmd"``; the console path rejects them instead of issuing an
#: unknown console command.
UsercmdButtonName = Literal["moveleft", "moveright", "speed", "zoom"]

#: The value kinds the ``settings`` command accepts (``Service::do_settings``
#: → ``write_field`` in ``payload/control/src/settings_access.cpp``):
#:
#: * a **flag** takes a *bool* (``true`` / ``false``) **or** a number;
#: * a number field takes a number;
#: * a colour takes an array of numbers (one per channel);
#: * a text takes a string.
#:
#: The bool is not decoration: the payload's ``write_field`` reads a flag's
#: ``true``/``false`` as 1/0, so a client that may only send numbers cannot
#: express "switch this off" in the way the wire format documents it.
SettingsValue = bool | int | float | list[int] | str


# --- the snapshot (payload/control/src/snapshot_json.cpp) --------------------
class Vec3Dict(TypedDict):
    """A world vector (``to_json(const game::Vec3&)``)."""

    x: float
    y: float
    z: float


class AngleDict(TypedDict):
    """The engine's own view angles (``to_json(const game::Angle&)``)."""

    pitch: float
    yaw: float
    roll: float


class PlayerSnapshotDict(TypedDict):
    """One player (``player_to_json`` in ``snapshot_json.cpp``).

    Every key is always present: the payload writes the whole block, so a
    missing key means a payload/client skew, not a quiet player.
    """

    index: int
    name: str
    weapon: str
    team: int
    connected: int
    health: int
    max_health: int
    armor: int
    life_state: int
    on_ground: bool
    flags: int
    clip: int
    reserve: int
    ping: int
    money: int
    flash: float
    alive: bool
    is_local: bool
    helmet: bool
    defuser: bool
    scoped: bool
    defusing: bool
    in_buy_zone: bool
    has_bomb: bool
    origin: Vec3Dict
    eye_origin: Vec3Dict
    velocity: Vec3Dict
    eye_angles: AngleDict
    distance: float
    simulation_time: float
    tick_base: int


class RulesSnapshotDict(TypedDict):
    """The round rules (``to_json(const game::RulesSnapshot&)``).

    The last two fields are the **pending** ones: the client knows their
    names and reads them, but the payload does not send them yet (see
    ``screens.PENDING_PAYLOAD_FIELDS``). They are optional for the same
    reason ``PingResult.protocol`` is: a payload that predates a field omits
    it, and the client must cope with both builds.
    """

    loaded: bool
    match_started: bool
    freeze: bool
    warmup: bool
    game_restart: bool
    bomb_planted: bool
    bomb_dropped: bool
    t_cant_buy: bool
    ct_cant_buy: bool
    game_phase: int
    round_time: int
    freeze_time: int
    round_win_status: int
    round_win_reason: int
    total_rounds_played: int
    match_start_time: float
    round_start_time: float
    restart_round_time: float
    game_start_time: float
    warmup_period_start: float
    time_until_next_phase: float
    # Sent by the current payload; optional for an older one.
    #: ``C_CSGameRules::m_nOvertimePlaying``.
    overtime_playing: NotRequired[int]
    #: ``CGameRules::m_bGamePaused``.
    game_paused: NotRequired[bool]


class LevelSnapshotDict(TypedDict):
    """The level probe (``to_json(const game::LevelSnapshot&)``).

    ``up`` is the *main-screen* signal (the level's rules-proxy entity exists —
    true at the main menu too, so it is a fact and not the rule; the rule is
    derived in ``screens.py``).
    """

    up: bool
    proxy: str
    rules_global: bool
    identities: int
    local_connected: int


class StateSnapshotDict(TypedDict):
    """The whole ``state`` answer (``to_json(const game::Snapshot&)``).

    ``local`` is ``None`` when there is no local player (the main menu); the
    payload writes a JSON ``null`` there, never a missing key.
    """

    valid: bool
    in_map: bool
    local_pawn: float
    level: LevelSnapshotDict
    rules: RulesSnapshotDict
    local: PlayerSnapshotDict | None
    players: list[PlayerSnapshotDict]
    # The payload's state machine: the screen and the capabilities in force.
    # Optional so a payload that predates the state machine still parses.
    screen: NotRequired[str]
    capabilities: NotRequired[dict[str, bool]]


# --- the small, closed answers ---------------------------------------------
class GameEventDict(TypedDict):
    """One captured game event (``EventLog::recent``).

    Every argument value is a **string** on the wire: the payload captures the
    game's own formatted arguments, so a JSON scalar matches by its text.
    """

    name: str
    seq: int
    args: dict[str, str]


class PingResult(TypedDict):
    """The liveness + readiness answer (``Service::ping_result``)."""

    pong: bool
    pid: int
    uptime_ms: int
    exec_ready: bool
    events_ready: bool
    events_subscribed: int
    events_dispatch_calls: int
    input_ready: bool
    input_hooked: bool
    input_backend: str
    state_ready: bool
    #: The command-surface version. The payload answers it only once it knows
    #: the version, so it is optional here (an older payload omits it).
    protocol: NotRequired[int]


class ExecResult(TypedDict):
    """The ``exec`` answer (``Service::do_exec``): the line was queued."""

    queued: bool


class EventsResult(TypedDict):
    """The ``events`` answer (``Service::do_events``)."""

    count: int
    subscribe: dict[str, list[str]]
    events: list[GameEventDict]
    #: The "since" cursor (the sequence number the next poll may pass). The
    #: payload answers it only once it keeps the cursor, so it is optional.
    last_seq: NotRequired[int]


class WaitResult(TypedDict):
    """The ``wait_for`` answer (``Service::do_wait_for``).

    ``snapshot`` is ``None`` when the wait timed out before a readable state
    existed (the payload writes a JSON ``null``).
    """

    matched: bool
    snapshot: StateSnapshotDict | None


class NoclipResult(TypedDict):
    """The runner's own answer to ``noclip`` (the console line was queued)."""

    noclip: bool


# --- the wide diagnostic answers --------------------------------------------
class InputStatus(TypedDict, total=False):
    """The input backend's status block (``Service::input_status``).

    Seven commands answer with it (``input``, ``press``, ``release``, ``move``,
    ``look``, ``bhop``, ``strafe``) and the **anti-aim** (``antiaim``) does too;
    it is ~110 keys wide and it grows with every new probe, so it is
    ``total=False``: the client reads the fields it cares about instead of the
    type demanding the whole block.
    """

    backend: str
    ready: bool
    hooked: bool
    slot: int
    slot5: int
    calls5: int
    buttons_set: int
    buttons_clear: int
    bhop: bool
    predict: bool
    move: bool
    move_quantize: bool
    move_step: bool
    move_raw_entry: bool
    move_forward: float
    move_side: float
    move_up: float
    angles: bool
    angle_pitch: float
    angle_yaw: float
    angle_roll: float
    command_angles: bool
    command_angle_yaw: float
    command_angle_writes: int
    raw_angles: bool
    raw_angle_writes: int
    step_yaw: float
    step_yaw_enabled: bool
    strafe: bool
    strafe_airborne: bool
    strafe_speed: float
    strafe_delta: float
    strafe_goal: float
    strafe_wish: float
    strafe_rot: float
    strafe_side: int
    strafe_steps: int
    strafe_reason: int
    strafe_clamped: bool
    strafe_wishspeed: float
    strafe_subticks: int
    strafe_mode: int
    strafe_ignore_keys: bool
    strafe_intent_f: float
    strafe_intent_l: float
    strafe_side_locked: bool
    strafe_side_last: int
    strafe_deadzone_deg: float
    strafe_writes: int
    strafe_raw_writes: int
    # The **anti-aim**: what it wrote (the fake yaw and pitch), the camera's
    # own angles (the proof that the camera did not follow them), the
    # **delivered** analog axes (a member of the engine's discrete set — what
    # the server acts on) with the direction error and the walk-speed factor
    # they cost, and the `move_crc` caveat.
    anti_aim: bool
    anti_aim_move_rebase: bool
    anti_aim_acted: bool
    anti_aim_reason: int
    anti_aim_fake_yaw: float
    anti_aim_fake_pitch: float
    anti_aim_camera_yaw: float
    anti_aim_camera_pitch: float
    anti_aim_forward: float
    anti_aim_left: float
    anti_aim_direction_error: float
    anti_aim_wish_factor: float
    anti_aim_saturated: bool
    anti_aim_basis_cos: float
    anti_aim_yaw_offset: float
    anti_aim_pitch: float
    anti_aim_jitter: float
    anti_aim_side: int
    anti_aim_writes: int
    anti_aim_rebase_applied: int
    anti_aim_rebase_skipped: int
    anti_aim_crc_caveat: str
    # The auto-strafe's **outcome** (the speed gained over the last airtime) and
    # its **gate verdict** (did the engine apply the requested basis rotation?
    # `docs/re/strafing.md` §"The gate").
    strafe_gain: float
    strafe_steered: bool
    strafe_steered_ticks: int
    strafe_ignored: int
    bhop_presses: int
    bhop_writes: int
    bhop_landed_tick: int
    bhop_landed_frac: float
    bhop_build: int
    bhop_age: int
    bhop_tick_base: int
    bhop_client_tick: int
    bhop_cmd_number: int
    bhop_mode: int
    bhop_when: float
    bhop_a: int
    bhop_b: int
    bhop_skipped: int
    bhop_holding_ticks: int
    bhop_air_ticks: int
    input_local_pawn: int
    predict_ready: bool
    predict_runs: int
    predicted_landing_tick: int
    predicted_landing_frac: float
    predicted_landing_delta: float
    calls: int
    writes: int
    angle_writes: int
    move_writes: int
    raw_writes: int
    entry_writes: int
    step_writes: int
    last_cmd: str
    cmd_words: list[str]
    base: str
    base_words: list[str]
    base_has_bits: str
    base_move: MoveSampleDict
    raw_move: MoveSampleDict
    entry_move: MoveSampleDict
    steps: int
    step_dump: list[StepProbeDict]
    # The local animfix (`docs/re/animfix.md`): whether it is on, whether it
    # acted, its counters, and the **server's** aim yaw against the **client's
    # predicted** one - the divergence the animfix removes. The aim-yaw keys
    # are ``None`` until the animfix has observed both (``animfix_data_valid``),
    # so "no data" is not "no divergence". ``animfix_status`` says why the
    # animfix is not ready (a stale calibration dump, an unhookable seam).
    animfix: bool
    animfix_ready: bool
    animfix_status: str
    animfix_acted: bool
    animfix_captures: int
    animfix_applies: int
    animfix_skips: int
    animfix_skip_no_capture: int
    animfix_skip_stale: int
    animfix_skip_controller: int
    animfix_skip_write_failed: int
    animfix_anim_ticks: int
    animfix_client_ticks: int
    # The client anim ticks that ran while the animfix was **on**: the
    # denominator of the duty cycle. The counters above are cumulative and
    # the animfix is off by default, so the driver's own client-tick counter
    # counts ticks from before the feature was ever switched on.
    animfix_enabled_client_ticks: int
    animfix_client_tick_seen: bool
    animfix_corrected_ticks: int
    animfix_snapshot_interval_ms: int
    animfix_rejected: int
    animfix_data_valid: bool
    animfix_server_aim_yaw: float | None
    animfix_predicted_aim_yaw: float | None
    animfix_aim_yaw_delta: float | None
    # The **codec counters** (`docs/re/animfix.md` §10): what the hooked
    # codec did, call by call. They are the diagnosis of a capture that
    # never happened — "the codec never ran" (H1) against "the codec ran,
    # but not for the local player" (H2).
    animfix_codec_calls: int
    animfix_codec_other_controller: int
    animfix_codec_not_local: int
    animfix_codec_local_calls: int
    animfix_codec_diagnosis: str


class MoveSampleDict(TypedDict):
    """One decoded movement probe (``Service::input_status``'s ``move_json``)."""

    valid: bool
    forward: float
    side: float
    up: float


class StepProbeDict(TypedDict):
    """One captured sub-tick step (``Service::input_status``'s ``step_dump``)."""

    when: float
    forward: float
    left: float
    button: str
    pressed: bool


#: ``move`` answers with the input-status block (``Service::do_move``), so the
#: type is shared rather than copied.
MoveResult = InputStatus

#: ``bhop`` answers with the input-status block (``Service::do_bhop``).
BhopResult = InputStatus

#: ``strafe`` answers with the input-status block (``Service::do_strafe``).
StrafeResult = InputStatus

#: ``animfix`` answers with the input-status block (``Service::do_animfix``).
AnimfixResult = InputStatus


class LookResult(TypedDict):
    """The angles the runner aimed at — **its** answer, not the payload's.

    The payload's ``look`` command answers with the input-status block; the
    runner's helper answers with the angles it aimed at (the console path
    waits for them, the ``usercmd`` path writes them).
    """

    pitch: float
    yaw: float
    roll: float


class PressResult(TypedDict):
    """The **runner's** answer to ``press`` — not the payload's.

    The console path drives the engine's own ``+<button>`` command and reports
    the button it drove; the ``usercmd`` path reports the same plus the backend
    it used.
    """

    button: str
    held: bool
    via: NotRequired[InputBackend]


#: ``release`` answers with one of two objects: the console path returns the
#: ``exec`` acknowledgement, the ``usercmd`` path the payload's input status.
ReleaseResult = Union[ExecResult, InputStatus]


class HitboxDict(TypedDict):
    """One studio hitbox of the target pawn (``Service::do_aim``)."""

    bone: int
    mesh: int
    group: int
    shape: int
    name: str
    radius: float
    min_x: float
    min_y: float
    min_z: float
    max_x: float
    max_y: float
    max_z: float
    a_x: float
    a_y: float
    a_z: float
    b_x: float
    b_y: float
    b_z: float
    bone_x: float
    bone_y: float
    bone_z: float
    quat_x: float
    quat_y: float
    quat_z: float
    quat_w: float
    world_x: float
    world_y: float
    world_z: float


class AimResult(TypedDict):
    """The ragebot solve (``Service::do_aim``).

    The payload answers with an error instead of a result when there is no
    hittable target, so every key is present when the answer is there — with
    one exception: the Monte-Carlo's own numbers are sent **only** when the
    Monte-Carlo produced a verdict, so they are ``NotRequired`` here.
    """

    valid: bool
    yaw: float
    pitch: float
    value: float
    hitchance: float
    cone: float
    weapon_spread: float
    weapon_inaccuracy: float
    cells: int
    aim_u: float
    aim_v: float
    wrote_view: bool
    hitboxes: list[HitboxDict]
    hitcount: int
    weapon_damage: float
    weapon_headshot: float
    weapon_range_modifier: float
    weapon_penetration: float
    #: The objective **by name** (``"damage"``, ``"hitchance"``,
    #: ``"expected"``, ``"pellets"``, ``"monte_carlo"``).
    objective: str
    #: The sample count in force (the Monte-Carlo objective).
    mc_samples: int
    #: The sample count the caller asked for (it may have been ignored).
    mc_samples_requested: int
    #: What the operator has to know about the count: empty when there is
    #: nothing to say, otherwise why it was ignored or that it does not fit
    #: the tick.
    mc_samples_note: str
    #: The Monte-Carlo objective's own numbers.  The payload publishes them
    #: **only when the Monte-Carlo produced a verdict** — under the other four
    #: objectives, and when the solver refused, they are **absent**.  So they
    #: are optional here: a type that promised them would promise keys the
    #: payload does not send, and a reader that trusted it would look for a
    #: key that is not there (``Service::do_aim``'s
    #: ``if (result.monte_carlo.has_value())`` block).
    mc_hits: NotRequired[int]
    mc_hitchance: NotRequired[float]
    mc_expected_damage: NotRequired[float]
    mc_target: NotRequired[int]
    mc_hitgroup: NotRequired[int]
    mc_can_kill: NotRequired[bool]
    #: How many hitboxes the filter dropped for the core (a dropped hitbox
    #: makes the measured probability a lower bound on the truth).
    mc_filtered: NotRequired[int]
    #: The seed policy's name (``"sequence"`` or ``"angle_grid"``).
    mc_seed: NotRequired[str]
    #: Whether the seed's sequence is a **fallback** (the prediction tick
    #: stands in for the command's sequence number).
    mc_seed_fallback: NotRequired[bool]
    #: What the seed's number means.
    mc_seed_note: NotRequired[str]
    #: The reason the solver refused (empty when it did not).
    mc_refusal: str
    #: Whether the solve fell back to another objective.
    mc_fallback: bool


class RagePerfDict(TypedDict, total=False):
    """The ragebot's instrumentation (``Service::do_rage``'s ``perf``)."""

    frames: int
    snapshot_frames: int
    total_us: float
    last_us: float
    max_us: float
    sample_us: float
    query_us: float
    build_us: float
    project_us: float
    solve_us: float
    backtrack_us: float
    solve_project_us: float
    solve_occlude_us: float
    solve_argmax_us: float
    solve_calls: int
    candidates: int
    hitboxes: int
    occluders: int
    poses: int
    cells: int


class RageResult(TypedDict, total=False):
    """The hold-key ragebot's state (``Service::do_rage``).

    Wide and growing (the instrumentation block alone is ~20 keys), so
    ``total=False``: the client reads the fields it cares about.
    """

    enabled: bool
    key: int
    fire: bool
    force: bool
    active: bool
    target: int
    hitbox: int
    solves: int
    writes: int
    #: The **engine's** shots: the clip fell while the ragebot was pressing
    #: (a reload and a failed clip read are not shots).
    shots: int
    #: The ragebot's **attempts**: the ticks the fire path decided to assert
    #: the attack.
    presses: int
    #: The release->re-press **edges** the fire path produced.
    edges: int
    #: The weapon's fire mode as the payload reads it: ``auto``, ``semi`` or
    #: ``unknown`` (``docs/ragebot.md`` §15).
    fire_mode: str
    #: The fire path's gate's verdict: ``ok`` when the shot can be taken, the
    #: blocking condition's name when it cannot (``no-solve``, ``fire-off``,
    #: ``clip-unknown``, ``no-ammo``, ``reloading``, ``deploying``, ``edge``,
    #: ``interval``, ``rate-unknown``).
    fire_refusal: str
    #: Whether the **player** is known to be holding the trigger.
    player_attack: bool
    #: What the payload knows about the **player's** own press of the trigger:
    #: ``held``, ``released`` or ``unknown`` (the payload cannot tell the
    #: player's press from its own, and an unknown reading never removes the
    #: attack bit).
    player_press: str
    occluders: int
    occlusion_query_complete: bool
    occlusion_query_issues: int
    backtracks: int
    record: int
    #: Whether the last tick skipped the backtracking because the frame had
    #: already blown the 12 ms guard.
    backtrack_over_budget: bool
    #: The objective the last solve **ran**, **by name** (the per-weapon
    #: profile's when there is one — the solve runs the effective objective,
    #: so the status names that one).
    objective: str
    #: The effective objective's value (the profile's byte).
    objective_value: int
    #: The **global** objective's name (the setting, not the effective one).
    objective_global: str
    #: Who owns the objective **in force** (the effective one): ``"menu"``,
    #: ``"control"`` or ``"profile"`` — a per-weapon profile's objective
    #: comes from the profile file, so no channel owns it.
    objective_source: str
    #: Who owns the **global** objective (the setting the two channels
    #: write): ``"menu"`` or ``"control"``.
    objective_global_source: str
    #: The objective the **menu's tab is showing** (the shared block's byte).
    #: It differs from :attr:`objective` when the control channel owns the
    #: objective: the menu has three options and cannot show the
    #: Monte-Carlo one, so the tab and the ragebot can disagree.
    menu_objective: str
    #: The rage fields the **menu** owns, comma-separated. A field that is
    #: not in the list belongs to the control channel. This is the
    #: **ownership map**, not a log of recent edits.
    menu_fields: str
    #: The Monte-Carlo objective's own state: the sample count in force, the
    #: seed policy, the numbers the last solve produced and the reason it
    #: refused if it did.
    mc_samples: int
    mc_samples_note: str
    mc_seed: str
    #: Whether the seed's sequence is a **fallback** (the prediction tick
    #: stands in for the command's sequence number).
    mc_seed_fallback: bool
    #: What the seed's number means.
    mc_seed_note: str
    mc_hitchance: float
    mc_expected_damage: float
    mc_target: int
    mc_hitgroup: int
    mc_hits: int
    #: Whether the damage attributed to the target reaches its health.
    mc_can_kill: bool
    #: How many hitboxes the filter dropped for the core (a dropped hitbox
    #: makes the measured probability a lower bound on the truth).
    mc_filtered: int
    #: The reason the solver refused (empty when it did not).
    mc_refusal: str
    #: Whether the solve fell back to another objective.
    mc_fallback: bool
    #: Whether the last solve produced a Monte-Carlo verdict at all.
    mc_valid: bool
    hitbox_mode: int
    fov: float
    min_damage: float
    min_hitchance: float
    autostop: int
    autoscope: int
    autoreload: bool
    only_visible: bool
    recoil: bool
    backtrack: bool
    stop_speed: float
    visible: bool
    stops: int
    mode: int
    per_weapon: bool
    weapon: str
    profiles: int
    perf: RagePerfDict


class BasisDict(TypedDict):
    """The chart's basis (``Service::do_heatmap``)."""

    forward: Vec3Dict
    right: Vec3Dict
    up: Vec3Dict


class HeatmapWeaponDict(TypedDict):
    """The weapon model the chart was valued with."""

    damage: float
    headshot: float
    range_modifier: float
    penetration: float


class HeatmapTargetDict(TypedDict):
    """One charted enemy (``Service::do_heatmap``'s ``targets``)."""

    index: int
    name: str
    team: int
    health: int
    distance: float
    origin: Vec3Dict
    records: int


class HeatmapCellDict(TypedDict):
    """One tagged silhouette cell (``Service::do_heatmap``'s ``cells``)."""

    poly: list[list[float]]
    value: float
    base_value: float
    target: int
    hitgroup: int
    record: int
    age: float


class CompoundDict(TypedDict, total=False):
    """The compound-shape probe (``Service::do_heatmap``'s ``walls.compound``)."""

    address: str
    instances: int
    meshes: int
    hulls: int
    g100: int
    g110: int
    g120: int


class WallsDict(TypedDict, total=False):
    """The occlusion report (``Service::do_heatmap``'s ``walls``).

    A diagnostic blob of ~50 keys that grows with every RE probe, so
    ``total=False``.
    """

    enabled: bool
    called: bool
    ok: bool
    faulted: bool
    query_complete: bool
    incomplete_reasons: list[str]
    records: int
    bodies: int
    shapes: int
    hulls: int
    type_counts: list[int]
    iface: str
    world: str
    filter: str
    filter_vtable: str
    filter_custom: bool
    filter_mask: int
    filter_group: int
    filter_flags: int
    mesh_clusters: int
    enum_box_kept: int
    enum_pyramid_kept: int
    enum_outside_box: int
    enum_box_only_between: int
    enum_targets: int
    enum_target_max_distance: float
    occluders: int
    penetration_model: str
    occlusion_candidates_complete: bool
    max_penetrations: int
    pen_mod: float
    dmg_mod: float
    surface_array: str
    surface_index: int
    surface_distance: float
    surface_material: int
    surface_hits: int
    material_token: str
    material_float: float
    material_records: int
    compound: CompoundDict
    excluded_records: int
    world_cache_body: str
    world_cache_hulls: int
    world_box_kept: int
    world_box_rejected: int
    hulls_no_planes: int
    record_handles: list[str]
    occluder_polygons: list[list[list[float]]]


class HeatmapResult(TypedDict, total=False):
    """The multi-target damage heatmap (``Service::do_heatmap``).

    Wide (the chart carries a cell per silhouette) and growing, so
    ``total=False``.
    """

    valid: bool
    eye: Vec3Dict
    basis: BasisDict
    fov: float
    extent: float
    view_yaw: float
    view_pitch: float
    weapon: HeatmapWeaponDict
    sampling: bool
    history: int
    capacity: int
    tick_base: int
    rewind_tick: int
    max_unlag: float
    interp: float
    window_ticks: int
    walls: WallsDict
    targets: list[HeatmapTargetDict]
    cell_count: int
    cells: list[HeatmapCellDict]


#: The verdicts the trace probe's difference carries
#: (``Service::do_trace_probe``): the engine's answer and ours agree, only the
#: engine found something, only we did, both found something the other did
#: not, or the engine's answer could not be read at all (``unknown`` — the
#: difference was **not** computed over an empty engine list).
TraceVerdict = Literal["agrees", "engine_only", "ours_only", "both", "unknown"]

#: The solid kinds the probe reports (``cs2::aim::SolidKind``): a brush hull is
#: a ``polytope`` and the payload calls it a hull.
TraceSolidKind = Literal["hull", "sphere", "capsule", "mesh"]


class TraceIntervalDict(TypedDict):
    """One interval record of the engine's trace (``Service::do_trace_probe``).

    The record is 0x38 bytes and only two of its fields are mapped: the
    fraction at ``+0x0C`` and the enter/exit byte at ``+0x32``. The probe
    publishes the record's **raw bytes** so a later RE pass can map the rest
    from the probe's own output, and it names the surface ``"unmapped"``
    rather than inventing a field's meaning.
    """

    fraction: float
    distance: float
    enter_exit: int
    surface: str
    raw: str


class TraceOccluderDict(TypedDict):
    """One of **our** occluders the ray enters (the solver's own answer)."""

    index: int
    kind: str
    cluster: int
    entry: float
    exit: float


class TraceSpanDict(TypedDict):
    """An enter/exit span the engine found and our set has nothing for."""

    entry: float
    exit: float
    enter_record: int
    exit_record: int


class TraceRayEngineDict(TypedDict):
    """The engine's answer for one ray."""

    ok: bool
    faulted: bool
    #: The engine wrote **past** the results buffer (the canary tripped): the
    #: records are not ours to read, so the answer is refused like a dropped one
    #: (``answer_missing`` is true).
    overrun: bool
    interval_count: int
    intervals_overflow: bool
    #: The engine's answer could **not** be read (the call faulted, the container
    #: did not hold the buffer the probe handed it, the count was out of range, or
    #: the buffer was overrun). The difference's verdict is then ``"unknown"`` —
    #: never ``"agrees"``.
    answer_missing: bool
    #: The interval record's surface is unmapped (only two of its fields are
    #: pinned). The flag says it once for the whole ray.
    surface_mapped: bool
    intervals: list[TraceIntervalDict]


class TraceDifferenceDict(TypedDict):
    """The difference between the engine's answer and ours for one ray."""

    verdict: str
    #: Whether the engine's answer could be read at all. When it is false the
    #: verdict is ``"unknown"`` and the lists are empty.
    engine_answer_usable: bool
    engine_only: list[TraceSpanDict]
    ours_only: list[TraceOccluderDict]


class TraceRayDict(TypedDict):
    """One probed ray: its endpoints, both answers and the difference."""

    start: Vec3Dict
    direction: Vec3Dict
    end: Vec3Dict
    depth: float
    engine: TraceRayEngineDict
    ours: list[TraceOccluderDict]
    difference: TraceDifferenceDict


class TraceProbeEngineDict(TypedDict):
    """The probe's own health: whether the engine call succeeded.

    ``ok`` is "**at least one** ray's engine call returned" and ``faulted`` is
    "the **instrument** failed": a ray's engine call faulted, **or** the engine
    wrote past the results buffer (the canary tripped). ``overrun`` says which
    of the two it was; ``rays_ok``/``rays_faulted``/``rays_overrun`` give the
    counts. A fault is the **instrument** failing, not the game.
    """

    ok: bool
    faulted: bool
    #: Whether the engine wrote **past** the results buffer (the canary tripped).
    #: The affected rays' answers are refused.
    overrun: bool
    rays_ok: int
    rays_faulted: int
    #: The rays whose results buffer was overrun.
    rays_overrun: int
    iface: str
    filter: str
    filter_mask: int
    filter_group: int
    filter_flags: int
    occluders: int
    occluder_hits: int
    #: How many of our solids the probe projects at most.
    occluder_cap: int
    #: Whether the enumeration produced more solids than the cap (the answer is
    #: then incomplete).
    occluders_truncated: bool
    #: Whether the static world body was already parsed when the probe ran. When
    #: it was not, the probe **refused** to answer (see ``refused``): it does not
    #: pay the world's parse (10k brush shapes) inside the input hook.
    world_cached: bool
    #: Whether the probe **refused** to answer. A refusal is not a fault: the
    #: probe did not ask the engine anything, so every ray's
    #: ``engine.answer_missing`` is true and the verdict is ``"unknown"``.
    refused: bool
    #: Why the probe refused, or ``""``: ``"world_cache_cold"`` means the
    #: world's static body was not parsed yet — warm the cache (the heatmap, or
    #: the ragebot's own enumeration) and run the probe again.
    refusal_reason: str
    #: The pad the probe's target box gets, in world units. It decides which
    #: solids the enumeration keeps.
    target_pad: float


class TraceProbeResult(TypedDict):
    """The trace probe's answer (``Service::do_trace_probe``).

    The probe asks the engine its own question (``TraceLineIntervals``, the
    call the real bullet makes) for a ray and publishes **both** answers side
    by side plus the difference.
    """

    eye: Vec3Dict
    yaw: float
    pitch: float
    spread: float
    count: int
    depth: float
    max_rays: int
    max_intervals: int
    engine: TraceProbeEngineDict
    ran: bool
    timeout: bool
    #: Why the answer is missing when it is: ``"answered"``, ``"busy"`` (the
    #: game thread is running but did not answer in time) or ``"stalled"`` (the
    #: game thread is not running at all).
    game_thread: str
    rays: list[TraceRayDict]


# --- the `wait_for` predicate DSL -------------------------------------------
# The server's grammar is 11 kinds (``payload/control/src/predicate.cpp``):
# ``in_map``, ``local_alive``, ``on_ground``, ``playable`` (bool);
# ``players_gte``, ``alive_gte``, ``local_hp_gte`` (int); ``team`` (a team name
# or number); ``all`` / ``any`` (a list of predicates); ``not`` (a predicate).
# An unknown key is a hard error on the server, so the type is closed.
#
# The functional syntax is not a style choice: ``not`` is a Python keyword, so
# the class syntax cannot spell that key at all.
WaitExpr = TypedDict(
    "WaitExpr",
    {
        "in_map": bool,
        "local_alive": bool,
        "on_ground": bool,
        "playable": bool,
        "players_gte": int,
        "alive_gte": int,
        "local_hp_gte": int,
        "team": TeamName,
        "all": list["WaitExpr"],
        "any": list["WaitExpr"],
        "not": "WaitExpr",
    },
    total=False,
)


# --- the transport ----------------------------------------------------------
@runtime_checkable
class ControlTransport(Protocol):
    """What the state machine and the readiness layer need from a channel.

    The higher layers (``screens.Machine``, ``awaiting``) program against this
    protocol, not against the concrete :class:`~scenario.control.Control`, so a
    fake channel can be substituted in a test. ``Control`` satisfies it.
    """

    def request(self, cmd: str, args: Mapping[str, Any] | None = ...) -> Any:
        """One raw wire call — the escape hatch below the typed helpers."""
        ...

    def state(self) -> StateSnapshotDict:
        """The current snapshot."""
        ...

    def events(self, limit: int = ..., subscribe: str | list[str] | None = ...) -> EventsResult:
        """The recent game events."""
        ...

    def wait_for(self, *, timeout: float = ..., event: str | None = ...,
                 event_args: Mapping[str, Any] | None = ...,
                 **predicates: Unpack[WaitExpr]) -> WaitResult:
        """Wait for the predicates (and the optional event) to hold."""
        ...

    def exec(self, line: str) -> ExecResult:
        """Queue a console line."""
        ...

    def press(self, key: str, hold: float = ...,
              via: InputBackend | None = ...) -> PressResult:
        """Hold a button."""
        ...

    def release(self, key: str, via: InputBackend | None = ...) -> ReleaseResult:
        """Release a button (the two backends answer differently)."""
        ...

    def move(self, *, forward: float = ..., side: float = ..., up: float = ...,
             clear: bool = ..., quantize: bool = ..., step: bool = ...,
             raw_entry: bool = ...) -> MoveResult:
        """Set the analog move axes."""
        ...

    def look(self, *, yaw: float | None = ..., pitch: float | None = ...,
             roll: float | None = ..., timeout: float = ...,
             via: InputBackend | None = ..., command: bool = ..., raw: bool = ...,
             step_yaw: float | None = ...) -> LookResult:
        """Aim the view."""
        ...


# --- the runtime validator --------------------------------------------------
#: The cached, resolved annotations of every ``TypedDict`` the validator walks.
_HINT_CACHE: dict[type, dict[str, Any]] = {}


def _hints(shape: type) -> dict[str, Any]:
    """The resolved annotations of a ``TypedDict`` (cached).

    ``get_type_hints`` is slow (it evaluates every annotation) and the state is
    polled, so the result is kept.
    """
    cached = _HINT_CACHE.get(shape)
    if cached is None:
        cached = get_type_hints(shape, include_extras=True)
        _HINT_CACHE[shape] = cached
    return cached


def _is_shape(hint: object) -> TypeGuard[type]:
    """Whether a hint is a ``TypedDict`` (the shape of one wire object).

    The guard narrows the hint to a **class**, which is what the shape
    walkers below need: they read the class attributes (``__required_keys__``,
    the resolved annotations) that only a class has.
    """
    return isinstance(hint, type) and hasattr(hint, "__required_keys__")


def _required_keys(shape: type) -> set[str]:
    """The keys the client insists on.

    ``__required_keys__`` is the starting point, but under
    ``from __future__ import annotations`` the ``NotRequired`` wrapper survives
    only as a string, so the runtime cannot see it — the resolved annotations
    are consulted as well and win.
    """
    # ``__required_keys__`` is a class attribute of every ``TypedDict``, but it
    # is not declared on ``type`` — so it is read reflectively.
    declared: frozenset[str] = getattr(shape, "__required_keys__", frozenset())
    required = set(declared)
    for key, hint in _hints(shape).items():
        origin = get_origin(hint)
        if origin is NotRequired:
            required.discard(key)
        elif origin is Required:
            required.add(key)
    return required


def _describe(hint: object) -> str:
    """A short, human name of an expected type (for the error message)."""
    origin = get_origin(hint)
    if origin is Union or origin is types.UnionType:
        return " or ".join(_describe(arg) for arg in get_args(hint))
    if origin is list:
        args = get_args(hint)
        return "a list of " + (_describe(args[0]) if args else "values")
    if origin is dict:
        return "an object"
    if hint is bool:
        return "a bool"
    if hint is int:
        return "an int"
    if hint is float:
        return "a number"
    if hint is str:
        return "a string"
    if hint is type(None):
        return "null"
    if _is_shape(hint):
        return "an object"
    return str(hint)


def _actual(value: object) -> str:
    """The name of a value's type (for the error message)."""
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "a bool"
    if isinstance(value, int):
        return "an int"
    if isinstance(value, float):
        return "a number"
    if isinstance(value, str):
        return "a string"
    if isinstance(value, Mapping):
        return "an object"
    if isinstance(value, list):
        return "a list"
    return type(value).__name__


def _check(value: object, hint: object, path: str) -> None:
    """Raise :class:`ProtocolError` unless ``value`` matches ``hint``."""
    origin = get_origin(hint)
    if origin is Required:
        # ``Required[int]`` is the annotation of an optional-in-the-class key
        # the client still insists on.
        args = get_args(hint)
        _check(value, args[0] if args else Any, path)
        return
    if origin is None:
        if hint is bool:
            if not isinstance(value, bool):
                raise ProtocolError(f"{path}: expected a bool, got {_actual(value)}")
        elif hint is int:
            # A bool is an int in Python, but not on the wire.
            if isinstance(value, bool) or not isinstance(value, int):
                raise ProtocolError(f"{path}: expected an int, got {_actual(value)}")
        elif hint is float:
            # The payload prints a whole double as an integer (``json.cpp``),
            # so a float field arrives as an int when it has no fraction.
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ProtocolError(f"{path}: expected a number, got {_actual(value)}")
        elif hint is str:
            if not isinstance(value, str):
                raise ProtocolError(f"{path}: expected a string, got {_actual(value)}")
        elif hint is Any:
            pass
        elif _is_shape(hint):
            _check_shape(value, hint, path)
        return
    if origin is Union or origin is types.UnionType:
        first_error: ProtocolError | None = None
        for arg in get_args(hint):
            if arg is type(None):
                if value is None:
                    return
                continue
            try:
                _check(value, arg, path)
                return
            except ProtocolError as error:
                # Keep the first (most precise) complaint: for ``X | None`` it
                # names the offending key inside ``X``.
                if first_error is None:
                    first_error = error
        if first_error is not None:
            raise first_error
        raise ProtocolError(f"{path}: expected {_describe(hint)}, got {_actual(value)}")
    if origin is list:
        if not isinstance(value, list):
            raise ProtocolError(f"{path}: expected a list, got {_actual(value)}")
        args = get_args(hint)
        item_hint = args[0] if args else Any
        for index, item in enumerate(value):
            _check(item, item_hint, f"{path}[{index}]")
        return
    if origin is dict:
        if not isinstance(value, Mapping):
            raise ProtocolError(f"{path}: expected an object, got {_actual(value)}")
        args = get_args(hint)
        value_hint = args[1] if len(args) == 2 else Any
        for key, item in value.items():
            if not isinstance(key, str):
                raise ProtocolError(f"{path}: expected a string key, got {_actual(key)}")
            _check(item, value_hint, f"{path}.{key}")
        return


def _check_shape(value: object, shape: type, path: str) -> None:
    """Check one ``TypedDict`` value: the required keys, and nothing else.

    Extra keys are **accepted** — a newer payload may answer with fields this
    client does not know, and that is not an error.
    """
    if not isinstance(value, Mapping):
        raise ProtocolError(f"{path}: expected an object, got {_actual(value)}")
    hints = _hints(shape)
    for key in sorted(_required_keys(shape)):
        if key not in value:
            raise ProtocolError(
                f"{path}: missing required key '{key}' (the payload does not know "
                "this field - rebuild it and re-inject once)"
            )
        _check(value[key], hints[key], f"{path}.{key}")


def validate_snapshot(raw: Mapping[str, Any]) -> StateSnapshotDict:
    """Check a ``state`` answer against :class:`StateSnapshotDict`.

    Raises :class:`ProtocolError` naming the missing or mistyped key; extra
    keys are accepted (a newer payload may add fields).
    """
    _check_shape(raw, StateSnapshotDict, "state")
    return cast(StateSnapshotDict, raw)


def validate_ping(raw: Mapping[str, Any]) -> PingResult:
    """Check a ``ping`` answer against :class:`PingResult`.

    Raises :class:`ProtocolError` naming the missing or mistyped key; extra
    keys are accepted (a newer payload may add fields — the command-surface
    version among them).
    """
    _check_shape(raw, PingResult, "ping")
    return cast(PingResult, raw)
