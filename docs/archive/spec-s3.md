# S3 — the typed protocol layer (Python side)

**Goal:** make the scenario library **fully type safe** at the wire boundary.
Today every `Control` command returns `dict[str, Any]`, so a typo in a field
name is a runtime `KeyError` three frames away, and a typo in a command's
argument is a runtime `TypeError` from the payload.

**File ownership (you may ONLY touch these):**

- `scripts/scenario/protocol.py` *(new)*
- `scripts/scenario/control.py`
- `scripts/scenario/tests/test_protocol.py` *(new)*
- `scripts/scenario/__init__.py` (re-exports only)

**Do NOT touch** `screens.py`, `awaiting.py`, `session.py`, `settings.py`,
`heatmap.py` or the scenarios — a separate task (S4) owns those, and they
depend on your types.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

---

## 1. What is there now

`scripts/scenario/control.py` (540 lines) is the transport client:

- NDJSON over the Win32 named pipe `\\.\pipe\cs2-internal-control`.
- `Control.request(cmd: str, args: dict[str, Any] | None = None) -> Any` — the
  single wire call, and the **weakest link in the typing story**.
- ~25 command helpers, each returning `dict[str, Any]`: `ping`, `state`,
  `exec`, `events`, `wait_for`, `wait_for_expr`, `input`, `press`, `release`,
  `move`, `buttons`, `bhop`, `strafe`, `aim`, `rage`, `heatmap`, `look`,
  `wait_look`, `noclip`, `teleport`, `quit`.

The **payload side** of the contract is documented in
`docs/control-harness.md` and implemented in
`payload/control/src/service.cpp` (`Service::handle` and the `do_*` handlers).
The **snapshot** shape is in `payload/control/src/snapshot_json.cpp`.

## 2. The new module: `scripts/scenario/protocol.py`

A single home for the wire types. It must import **nothing** from the rest of
the package (so it can be imported by the MCP server and by the tests without
dragging in the session machinery) and it must depend on **nothing** outside
the standard library.

### 2.1 The result types — `TypedDict`

One `TypedDict` per command's result, named `<Command>Result`. Use
`total=False` for the **wide diagnostic** objects (`InputStatus` has ~90 keys;
`RageStatus` and `HeatmapResult` are large) so the client can read the fields
it cares about without the type demanding the whole block; use `total=True`
for the small, closed shapes.

Required shapes (match the payload exactly — `snapshot_json.cpp` and
`service.cpp` are the ground truth):

```python
class Vec3Dict(TypedDict):
    x: float
    y: float
    z: float

class AngleDict(TypedDict):
    pitch: float
    yaw: float
    roll: float

class PlayerSnapshotDict(TypedDict):
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

class LevelSnapshotDict(TypedDict):
    up: bool
    proxy: str
    rules_global: bool
    identities: int
    local_connected: int

class StateSnapshotDict(TypedDict):
    valid: bool
    in_map: bool
    local_pawn: float
    level: LevelSnapshotDict
    rules: RulesSnapshotDict
    local: PlayerSnapshotDict | None
    players: list[PlayerSnapshotDict]

class GameEventDict(TypedDict):
    name: str
    seq: int
    args: dict[str, str]          # every argument value is a STRING on the wire

class PingResult(TypedDict):
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
    protocol: int                 # the command-surface version (S1 adds it)

class ExecResult(TypedDict):
    queued: bool

class EventsResult(TypedDict):
    count: int
    subscribe: dict[str, list[str]]   # {"accepted": [...], "rejected": [...]}
    events: list[GameEventDict]
    last_seq: int                     # S1 adds it (the "since" cursor)

class WaitResult(TypedDict):
    matched: bool
    snapshot: StateSnapshotDict | None
```

plus `MoveResult`, `LookResult`, `PressResult`, `BhopResult`, `StrafeResult`,
`AimResult`, `RageResult`, `HeatmapResult` — each `TypedDict` with
`total=False` where the payload's shape is wide, and a docstring naming the
payload handler it mirrors.

### 2.2 The argument types

```python
InputBackend = Literal["console", "usercmd"]
AimObjective = Literal["damage", "hitchance", "expected", "pellets"]
HeatmapPen = Literal["simple", "engine"]
HeatmapEnum = Literal["pyramid", "box", "both"]
RageMode = Literal["hold", "always", "toggle"]
TeamName = Literal["T", "CT", "t", "ct", 2, 3]
ButtonName = Literal["attack", "mouse1", "jump", "space", "duck", "crouch", "ctrl",
                     "forward", "w", "back", "s", "use", "e", "left", "right",
                     "moveleft", "a", "moveright", "d", "attack2", "mouse2",
                     "run", "reload", "r", "speed", "shift", "walk", "zoom"]
```

`BUTTONS` in `control.py` becomes `Mapping[str, ButtonName]` — so
`press("spcae")` is a **type error**, not a silent `+spcae` to the console.

### 2.3 The `wait_for` predicate DSL

The server's grammar is 12 kinds (`payload/control/include/cs2/control/predicate.hpp`
and `predicate.cpp`): `in_map`, `local_alive`, `on_ground`, `playable` (bool);
`players_gte`, `alive_gte`, `local_hp_gte` (int); `team` (a team name or
number); `all` / `any` (a list of predicates); `not` (a predicate). An
**unknown key is a hard error** on the server.

Model it as a **recursive** `TypedDict`:

```python
class WaitExpr(TypedDict, total=False):
    in_map: bool
    local_alive: bool
    on_ground: bool
    playable: bool
    players_gte: int
    alive_gte: int
    local_hp_gte: int
    team: TeamName
    all: list["WaitExpr"]
    any: list["WaitExpr"]
    not: "WaitExpr"
```

(`from __future__ import annotations` is already used, so the forward references
are fine.)

### 2.4 A transport `Protocol`

The higher layers (`screens.Machine`, `awaiting`) should not need the concrete
`Control` class. Declare:

```python
class ControlTransport(Protocol):
    """What the state machine and the readiness layer need from a channel."""
    def request(self, cmd: str, args: Mapping[str, Any] | None = ...) -> Any: ...
    def state(self) -> StateSnapshotDict: ...
    def events(self, limit: int = ..., subscribe: str | list[str] | None = ...) -> EventsResult: ...
    def wait_for(self, *, timeout: float = ..., event: str | None = ...,
                 event_args: Mapping[str, Any] | None = ..., **predicates: Any) -> WaitResult: ...
    def exec(self, line: str) -> ExecResult: ...
    def press(self, key: str, hold: float = ..., via: InputBackend | None = ...) -> PressResult: ...
    def release(self, key: str, via: InputBackend | None = ...) -> PressResult: ...
    def move(self, *, forward: float = ..., side: float = ..., up: float = ...,
             clear: bool = ..., quantize: bool = ..., step: bool = ...,
             raw_entry: bool = ...) -> MoveResult: ...
    def look(self, *, yaw: float | None = ..., pitch: float | None = ...,
             roll: float | None = ..., timeout: float = ..., via: InputBackend | None = ...,
             command: bool = ..., raw: bool = ..., step_yaw: float | None = ...) -> LookResult: ...
```

`Control` must **satisfy** it — add a `static_assert`-style check in the test
(`def test_control_satisfies_the_transport_protocol()` that assigns a `Control`
to a `ControlTransport`-typed variable; a type checker will accept it, and at
runtime it is an `isinstance` check against a `runtime_checkable` Protocol).

### 2.5 The runtime validator — the half static types cannot do

Static types describe one *build's* expectations. The payload is a **separately
built binary**; a newer payload may answer with a field the client does not
know, and an older one may omit a field the client needs. Add:

```python
class ProtocolError(ControlError):  # or its own base
    """The payload answered with a shape this client does not understand."""

def validate_snapshot(raw: Mapping[str, Any]) -> StateSnapshotDict: ...
def validate_ping(raw: Mapping[str, Any]) -> PingResult: ...
```

- `validate_snapshot` checks the **required** keys and their types (the
  `required=True` `TypedDict`s) and raises `ProtocolError` with a precise
  message naming the missing or mistyped key.
- It must **not** reject *extra* keys — a newer payload may add fields.
- Wire it into `Control.state()` (and `ping()`), so the client fails loudly
  and early rather than three frames later.
- It must be **cheap** — the state is polled; do not do this per frame in a
  hot path without a reason. (A `state()` call is once per poll, not per
  frame — fine.)

## 3. `control.py` — the typed client

- Every command helper gets its **exact** return type from `protocol.py`.
- `Control.request` gets `@overload`s, one per command, so the generic escape
  hatch is still available but the helpers are precise:

  ```python
  @overload
  def request(self, cmd: Literal["ping"], args: None = ...) -> PingResult: ...
  @overload
  def request(self, cmd: Literal["state"], args: None = ...) -> StateSnapshotDict: ...
  ...
  def request(self, cmd: str, args: Mapping[str, Any] | None = None) -> Any: ...
  ```

  (With `from __future__ import annotations` the overloads are fine.)
- The `**predicates` in `wait_for` become `**predicates: Unpack[WaitExpr]` if
  the Python version allows it, or keep `Any` and add a typed
  `wait_for_expr(expr: WaitExpr, ...)` overload. **Check what the interpreter
  supports** (the environment is Python 3.11.9) and do what is correct there.
- `press`/`release`'s `key` becomes `ButtonName | str` (a `str` is still
  allowed for the engine's own button names, but the `BUTTONS` map is typed so
  the *known* names are checked).
- `buttons(press=..., release=...)` — the `Any` becomes
  `ButtonName | Sequence[ButtonName] | int | None`.
- `aim`'s `objective` becomes `AimObjective | None`; `heatmap`'s `pen` becomes
  `HeatmapPen | None`; `enum` becomes `HeatmapEnum | None`; `rage`'s `mode`
  becomes `RageMode | None`.
- **Do not change the runtime behaviour.** The wire format, the argument names,
  the defaults and the error handling must be identical. This is a typing task.

## 4. `scripts/scenario/tests/test_protocol.py`

`unittest` (the repo's convention — `scripts/scenario/tests/test_screens.py`
uses it). Cover:

- `validate_snapshot` accepts a well-formed snapshot.
- it **rejects** a missing required key, with a message naming the key.
- it **rejects** a mistyped value (e.g. `health` as a string).
- it **accepts** extra keys (a newer payload).
- `validate_ping` likewise.
- `Control` satisfies `ControlTransport` (the runtime check).
- The **`BUTTONS` map's values are all in `ButtonName`** — a static-ish test
  that catches a typo in the table (compare against the `ButtonName` literal's
  values with `typing.get_args`).

## 5. Quality bar

- `from __future__ import annotations` at the top (already the convention).
- Every public function and class documented; the docstrings explain *why*,
  matching the repo's style (see `screens.py`'s module docstring).
- `snake_case` functions/variables, `UPPER_SNAKE` constants, `PascalCase`
  classes, `SCREAMING_SNAKE` type aliases — matching the existing package.
- The package is **stdlib only** — no `pydantic`, no `typing_extensions`
  (unless it is already installed *and* you justify it; prefer not).
- Python **3.11** is the floor (the environment is 3.11.9). Do not use 3.12+
  syntax.

## 6. Verification

- `python -m unittest discover -s scripts/scenario/tests` → green.
- `python -c "import scenario"` from `scripts/` → no import errors.
- **Prove the tests can fail**: break the validator on purpose, watch it fail.
- The **existing** scenario suite must still import and run:
  `python -m unittest discover -s scripts/scenario/tests` includes
  `test_screens.py`, and `python scripts/run-scenario.py --list` must still
  print the 19 scenario names (it imports the package).

## 7. Report

- The type inventory: how many `TypedDict`s, `Literal`s and `Protocol`s you
  added, and the one-line rationale for each group.
- The validator's rules and its error messages.
- The `wait_for` typing decision (what the interpreter supports).
- Anything in the payload's shape you could **not** type faithfully, and why.
