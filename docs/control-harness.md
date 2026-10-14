# Programmatic scenario / control harness (current feature)

**Status: control core + input implemented and validated live** (2026-10, CS2
build 25815307). `payload/control/` + the Python `scripts/scenario/` runner drive
a real game: launch via the `CS2Launch` task, inject, `exec`/`state`/`wait_for`,
plus runner-side `look`/`press`/`noclip`/`teleport` (engine `setang` and button
commands through `exec`). The `join-bot-match` scenario runs end to end and each
piece is verified live. **Game events** are implemented **and validated live**:
the payload patches the client manager's dispatch vtable (`FireEvent` /
`FireEventClientSide`) and records the fired names, and `events` /
`wait_for(event=…)` expose them. **The `usercmd` input backend is implemented and
validated live for discrete buttons**: the payload hooks
`CCSGOInput::CreateMove` and writes the `CUserCmd` button mask, and
`usercmd-move` drives `jump` and `bhop` (repeated hops) in a real game. Analog
movement (`forward`/`moveleft`) and view angles are **subtick** in this build and
stay on the console backend for now. Remaining: **console-output capture**, and
`usercmd` subtick move/angles.

This is the resume document for the feature we are actively working on: a way to
programmatically drive CS2 for testing — launch, inject, join a bot match, add /
place bots, aim, run input sequences, await events, assert.

## Goal

A scriptable harness that can set up and run test situations against the real
game, e.g.:

```python
with cs2_session() as g:          # launch (CS2Launch task) + inject + connect
    g.exec("sv_cheats 1")
    g.exec("game_type 0; game_mode 0; map de_dust2")
    g.wait_for(local_alive=True, in_map=True, timeout=90)
    g.exec("bot_add_t; bot_add_ct")
    g.wait_for(players_gte=3)
    g.look(yaw=0, pitch=0)
    g.press("w", hold=1.0)
    g.exec("bot_place")
    assert len(g.state()["players"]) >= 3
```

## Decisions already made

| Topic | Decision |
|---|---|
| Transport (payload ↔ runner) | **Win32 named pipe** + NDJSON (default `\\.\pipe\cs2-internal-control`; env `CS2_CONTROL_PIPE`). Chosen over TCP/shm: pure Win32, cross-session same-user, openable from Python stdlib (`open(r'\\.\pipe\name','r+b')`), no ports. |
| `exec` (console commands) | Run **in-process** in the payload (primary): `Cbuf_AddText` = `FUN_1801b8790(&g_InputService /*RVA 0x8dbf20*/, 5, line, 0)` (RVA `0x1b8790`), thread-safe, no launch flags. `-concommandpipe` (built-in external named-pipe console channel, `-insecure`-gated) is kept as a **no-injection fallback / debug channel**; note it is input-only in this build. See `docs/re/engine2.md`. |
| Aim / `look` | **Direct view-angle control via the engine's own `setang` command** (needs `sv_cheats 1`), executed through the payload `exec` — no synthetic mouse, no offsets, no hook. Validated live. `look(yaw=…)`/`look(pitch=…)` read the current angles from `state` and keep the omitted components. |
| `press` (keys/buttons) | **Engine button commands** (`+forward`…`-forward`, `+attack`, `+jump`, `+duck`, …) through the same `exec`, *not* OS `SendInput`: the runner may be in session 0 while the game is in session 1, so synthetic input from the runner cannot reach it. `hold` = `+cmd` … sleep … `-cmd`. Validated live. |
| Scenario runner | **Python** package under `scripts/`. |
| First milestone | **Control core only**: pipe + `ping/state/exec/wait_for/quit` + RE of the console entry + Python client/launcher + a join-bot-match scenario. |

## Architecture (planned)

```
 scenario runner (Python, session 0)
        │  NDJSON over named pipe  \\.\pipe\cs2-internal-control
        ▼
 payload.dll inside cs2.exe (session 1)
   control thread ── state  → StateReader (RPM self, safe)
                  ── exec   → Cbuf_AddText (FUN_1801b8790, RVA 0x1b8790)
                                [fallback: -concommandpipe]
                  ── input  → engine button commands (+forward/…) & `setang`,
                                injected through exec (no OS SendInput)
                  ── wait   → predicate over snapshots
                  ── events → IGameEventListener2 registered with the client
                                CGameEventManager (client.dll), fed into an
                                EventRing (docs/re/client-dll.md §"Game events")
   publish thread ── shm ──▶ Vulkan overlay layer (render)
```

## Command surface (v1)

Requests are `{"id": <n>, "cmd": <name>, "args": {…}}`; responses are
`{"id": <n>, "ok": <bool>, "result": {…}}` or `{"id": <n>, "ok": false,
"error": "<message>"}`.

| cmd | args | returns |
|---|---|---|
| `ping` | — | `{pong, pid, exec_ready, events_ready, input_ready, input_hooked, input_backend, state_ready}` |
| `state` | — | snapshot: `valid`, `in_map`, `local_pawn`, `local{…}`, `players[…]` |
| `exec` | `line` | `{queued}` (semicolon-separated console line) |
| `events` | `limit`, `subscribe` | `{count, events: [{name, seq}, …]}` — recent game events (oldest first); `subscribe` is a name or array of names to register on demand |
| `wait_for` | `expr`, `event`, `timeout_ms` | `{matched, snapshot}` |
| `input` | `via` (`"console"`\|`"usercmd"`) | backend + hook status `{backend, ready, hooked, slot, buttons_set, buttons_clear, bhop, calls, writes}` |
| `press` / `release` | `buttons` (name, array, or mask); `reset` | hold / release command buttons on the active backend |
| `bhop` | `on` | toggle the payload auto-jump |
| `quit` | — | closes the channel |

`wait_for`'s `expr` is a structured object, ANDed across keys, evaluated every
100 ms on the control thread (so a `wait_for` blocks other commands for its
duration — single-client v1):

```jsonc
{"local_alive": true} {"players_gte": 4} {"alive_gte": 2} {"local_hp_gte": 1}
{"team": "T"} {"in_map": true} {"on_ground": true}
{"all": [ … ]} {"any": [ … ]} {"not": { … }}
```

`in_map` is `valid && local player present`; `players_gte` counts every player,
`alive_gte` counts `life_state == 0`; `on_ground` is the local pawn's
`m_fFlags & FL_ONGROUND`. Unknown keys are rejected so a typo is an error rather
than an always-true predicate.

`wait_for` also takes an optional `event` (a game-event name, e.g.
`"player_death"`): it subscribes on demand and matches once that event has fired
*after* the request started. When both `expr` and `event` are given, both must
hold. Events themselves are read with the `events` command (or
`game.events(...)`), backed by the payload's listener (below).

## Input helpers (runner-side)

Input is **engine-native** — it never uses OS input (which cannot cross sessions).
The default backend rides `exec` (engine `setang` / `+button` commands, no offsets
or hooks); `via="usercmd"` instead writes the per-tick `CUserCmd` through the
payload's `CreateMove` hook (`docs/re/client-dll.md` §"Input path"). Provided by
`scripts/scenario/control.py` and mirrored on `Game`:

| helper | effect |
|---|---|
| `look(yaw=…, pitch=…, roll=…)` | `setang` to the given angles; omitted components keep their current value (read from `state`). Needs `sv_cheats 1`. (Console only for now; `usercmd` angles are subtick — a follow-up.) |
| `press(key, hold=0, via=None)` | hold a button. Default: engine `+forward` (works for analog movement). `via="usercmd"`: set the command's button bit (payload `press`) — works for **discrete** actions (`space`/`jump`, `ctrl`/`duck`, `mouse1`/`attack`, `r`/`reload`), not analog movement. `hold` releases after that many seconds. Keys map to buttons (`w`→`forward`, `space`→`jump`, `ctrl`→`duck`, `mouse1`→`attack`, `r`→`reload`, …). |
| `release(key, via=None)` | release a button held with `press(..., hold=0)`. |
| `input(via=None)` | select/query the backend (`"console"`/`"usercmd"`) and read the hook status. |
| `buttons(press=…, release=…, reset=False)` | raw `usercmd` button overrides (a name, array of names, or a mask). |
| `bhop(on=True)` | payload auto-jump: force `IN_JUMP` while grounded, clear it airborne. |
| `noclip(bool)` / `teleport(x, y, z=None)` | `noclip` / `setpos` (needs `sv_cheats 1`) — positioning for tests. |

Verified live: `look(yaw=90)` moved the eye angles; `press("w", hold=1.0)` moved
the player (~270 units along the facing direction); `press("space", via="usercmd")`
left the ground and `bhop(True)` hopped repeatedly.

### Readiness helpers (`scripts/scenario/awaiting.py`)

The payload is only useful once the game is in a **started round with an alive,
movable local player** — the game boots to the main menu, `mp_restartgame` leads
to warmup then a freeze period, and a dead player ignores input. That sequence
lives once, in `awaiting` (re-exported from `scenario` and mirrored on `Game`):

| helper | effect |
|---|---|
| `ensure_map(game, map_name="de_dust2")` | load a casual offline map if none is loaded |
| `ensure_live_round(game, …)` | map + end warmup/freeze + restart + wait for `round_start` **and** `round_freeze_end`, then **gate on movement actually working** (a console `+forward` nudge), restarting until it does |
| `wait_round_started(game)` | wait for `round_start` + `round_freeze_end` (the raw event waits) |
| `is_movable` / `wait_movable(game)` | whether a `+forward` actually moves the player (the liveness gate) |
| `wait_airborne` / `wait_on_ground(game)` | wait for the local player to leave / touch the ground |
| `measure_move(game, key="w", via=…, yaw=…)` | hold a key and return `{before, after, moved}` (horizontal displacement) |
| `local`/`origin`/`on_ground`/`alive`/`displacement` | state readers |

Use `game.ensure_live_round()` before any input test — testing during warmup or
freeze silently produces zero movement.

## Event helpers (runner-side)

Events come from hooks on the client manager's dispatch (`CGameEventManager::FireEvent`
/ `FireEventClientSide`; `docs/re/client-dll.md` §"Game events"). The payload
validates names against the build's own event list
(`payload/control/include/cs2/control/event_names.hpp`, generated by
`scripts/extract-gameevents.py`) and keeps a small ring of the most recent events
plus a total count.

| helper | effect |
|---|---|
| `events(limit=64, subscribe=None)` | recent events, oldest first: `{count, events: [{name, seq, args}, …]}`. `args` holds the event's declared fields (e.g. `player_death`'s `attacker`, `weapon`, `headshot`). `subscribe` registers names on demand (a small default set is always watched) and reports `accepted`/`rejected`, so an unknown name is an error. |
| `wait_for(event="player_death", event_args={"headshot": True}, timeout=…)` | wait until the (validated) event fires *after* the call **carrying those field values**, AND any state predicates hold. |

Names and fields are the game's own (e.g. `player_death`, `round_start`,
`round_freeze_end`, `bomb_planted`); `not_a_real_event` is rejected rather than
silently waited on. Values are compared as strings against the captured format, so
JSON scalars match (`7`, `1.5`, `true`/`false`, a string, or a player **slot**
index for `userid`/`attacker`/`assister`). `args` holds at most 6 fields per
event (field order comes from the game's definitions).

### Input backends (implemented)

Console commands are a *proxy* for input: `setang` sets the view for one frame
and `+forward` toggles the engine's own button state. The primary path is the
**real `CUserCmd`** the client builds each tick; the console path is kept as a
fallback.

| backend | how | notes |
|---|---|---|
| `usercmd` | hook `CCSGOInput::CreateMove` (client.dll) and write the `CUserCmd`'s button bitmask after the original runs | exact, per-tick; **discrete** buttons (jump/bhop/attack/duck/reload) work; analog movement/angles are subtick |
| `console` (default) | `setang` + button commands through `exec` | no offsets or hooks; works for analog movement too; the fallback when the payload is not loaded |

Payload-side state (shared by both backends):

- **buttons** — a held set/clear mask (`IN_ATTACK`, `IN_JUMP`, `IN_DUCK`,
  `IN_FORWARD`, `IN_MOVELEFT`, …); `usercmd` writes it every tick, `console`
  emits `+<button>`/`-<button>` on transitions.
- **bhop** — the payload forces `IN_JUMP` while grounded and clears it airborne.
- **view angles / move axes** — `console` uses `setang`; the `usercmd` path is a
  follow-up (angles/analog are subtick — see below).

Control surface (payload): `input` (select/query the backend), `press`,
`release`, `bhop`. Runner wrappers (`press`, `release`, `look`) take
`via="usercmd" | "console"` to override the backend per call.

### `usercmd` backend — implementation

**RE done and cross-checked** (`docs/re/client-dll.md` §"Input path"); the
payload implements it in `payload/control/{include/cs2/control/input.hpp,
src/input.cpp}`:

- On `refresh` it resolves the `CCSGOInput` singleton (`ccsgo_input`) and scans
  its vtable for the `create_move` signature (`+0xd01610`, slot 25 in build
  25815307), then patches that slot (`VtableHook`-style, ADR-0015). The RE
  anchors: `CCSGOInput` object `+0x2572460`, vtable `+0x1c97b28`, handle
  (`dwCSGOInput`) `+0x2233d10`.
- The detour calls the original first, then writes `cmd->buttons`
  (`cmd + 0x60`, the `CInButtonState` mask) from the held set/clear mask and the
  `bhop` state. Writes are **inert unless `via="usercmd"`** is selected, so the
  console path is unaffected.
- `CUserCmd`: stride `0x98` (150 per `CUserCmdArray` global `+0x221bd10`);
  command number `+0x8`, `CSGOUserCmdPB` `+0x10`, `InButtonState` `+0x58`.
- `on_ground` comes from the local pawn's `m_fFlags` (`C_BaseEntity`), noted from
  the last `state`/`wait_for` snapshot (also exposed as a `wait_for` key).

**Live-validated (build 25815307).** `usercmd-move` drives `jump` (leaves the
ground) and `bhop` (repeated hops) with the write in place. The button-mask offset
is confirmed live: a console `+forward` sets `cmd + 0x60` bit 3 and `+jump` bit 1.
The hook also carries a diagnostic capture of the command's first 16 qwords
(`input` → `cmd_words`), used to map fields.

**Analogue movement and view angles are subtick.** Writing only the legacy
`cmd + 0x60` bit does **not** produce `forward` movement — the magnitude comes
from the subtick move steps / the `CUserCmdPB` floats (`m_flForwardMove`/
`m_flSideMove`), and the angles from the `CSubtickMoveStep` history. That is the
next step for autostrafe/aim; discrete buttons (jump/bhop/attack/duck/reload)
already work.

The `console` backend (`setang` + button commands through `exec`) stays as the
no-injection fallback. The console-input RE anchors in the older notes
(`CreateMove` `0x1c9c334`, `CCSGOInput` `0x2320304`, `m_nButtons` `0x1c338a8`)
were **strings**, now superseded above.

## Code layout

- `payload/control/` (new): `include/cs2/control/{json,predicate,snapshot_json,
  console,events,event_ring,input,channel,service}.hpp` + `src/*.cpp` +
  `meson.build` (`control_lib`/`control_dep`). Started from `install_thread` in
  `payload/src/payload_main.cpp`, gated by `CS2_PAYLOAD_NO_CONTROL`. Uses
  `payload/game`'s `StateReader` for snapshots. `json`/`predicate`/
  `snapshot_json`/`event_ring` are platform-independent and built host-side by
  the `control-core` Meson test (`payload/control/tests/test_control_core.cpp`).
  `input.cpp` is the `usercmd` backend (hooks `CreateMove`).
- `scripts/scenario/`: `session.py` (kill stale `cs2.exe` → run the `CS2Launch`
  task → wait for `client.dll` → inject `build-payload/payload.dll` →
  connect → teardown), `control.py` (pipe client + typed helpers),
  `awaiting.py` (readiness: map/round/alive/movable + `measure_move`),
  `scenarios/*.py` (`join-bot-match`, `move-and-aim`, `game-events`,
  `usercmd-move`); `scripts/run-scenario.py` (CLI).
- PID/module discovery: `external/dumper/target/release/cs2-dumper.exe
  --process cs2.exe --json`; injection: `build-injector/injector.exe`.

```
python scripts/run-scenario.py --list
python scripts/run-scenario.py join-bot-match
```

### Console-output capture (open)

`exec` returns `{queued}` only — the Cbuf path produces no text. `-concommandpipe`
is input-only, so console output must come from `-condebug` (console log), a
payload hook on the console-print/logging path, or a game-event listener. Until
then, `wait_for` predicates are state-based (an `exec_output`/`text` predicate is
TBD).

## RE status for `exec`

**Complete** — see **`docs/re/engine2.md`** for the full detail. Summary:

- `CInputService` singleton at `0x1808dbf20` (`&g_InputService`); its console
  command buffers live at `+0x588` (6 slots, stride `0x86d8`), drained every
  frame by `FUN_1801c6300`.
- **In-process `Cbuf_AddText`**: `FUN_1801b8790(&g_InputService, 5, line, 0)`
  (wrapper → `CCommandBuffer::AddText` `FUN_1801c70e0`). Slot 5 is the same slot
  the engine uses for its own queued/pipe input. Thread-safe (per-slot atomic
  mutex). **This is the primary `exec` for the payload.**
- **`-concommandpipe <in>,<out>`** (needs `-insecure`) is a real built-in
  named-pipe console channel: the engine is the pipe *client* (`CreateFile`
  `OPEN_EXISTING`), reads command lines from `<in>` and feeds them through the
  same Cbuf path. In this build the `<out>` handle is never written, so it is
  effectively **input-only** (no console output back); console output must come
  from `-condebug`/a payload hook/the logging system.
- Both primitives are covered by curated signatures in
  `external/dumper/src/analysis.rs` (`input_service`, `cbuf_add_text`) so the
  RVAs re-resolve across builds (ADR-0012).

## What already exists to build on (validated)

- Injection + state + overlay are proven on native Windows — see
  `docs/handoff.md` and `docs/windows-validation.md`.
- Launcher: `CS2Launch` scheduled task → `scripts/launch-cs2.cmd` (session 1).
- `payload/tls::create_thread_no_attach` for the control thread; `payload/game`
  `StateReader` for state; `payload/features` for the publish thread pattern.

## RE tooling (REA + Ghidra) — how to resume analysis

REA now has a **persistent Ghidra project cache**: a target is imported and
analyzed once and reused by later sessions (CLI and MCP). The one-time Windows
gap in the existing-project launch path was fixed upstream (PR #2358, commit
`9adad2c`); the cache itself is the new PR `feat/ghidra-project-cache` (merged
with `main`).

- Installed build: `C:\Users\gpmpo\rea-src-install` (v6.4.0 + cache), `npm link`ed
  so the global `rea` shim points there. The PR source is
  `C:\Users\gpmpo\rea-src` (branch `feat/ghidra-project-cache`). **Windows cannot
  run the merged build** — its native loader needs an addon field the prebuilt
  v6.4.0 artifact lacks, and the addon lane is Linux + MinGW.
- Ghidra **12.1.4**: `C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC`; Java 21:
  `C:\Program Files\Java\jdk-21`.
- OpenCode MCP entry (`C:\Users\gpmpo\.config\opencode\opencode.jsonc`): command
  `node C:\Users\gpmpo\rea-src-install\scripts\rea.mjs mcp`, env
  `REA_ANALYSIS_PROVIDER=ghidra`, `GHIDRA_INSTALL_DIR`, `JAVA_HOME`,
  `REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000`, **`REA_GHIDRA_PROJECT_CACHE=on`**.
- Warm targets are automatic and keyed by target, so no config edit to switch:
  `engine2.dll` (first open 256.8 s → hit ~31 s) and `client.dll` (~21 min → hit
  ~49 s) are seeded from `ghidra-analysis\rea-project` / `client-warm`. See
  `docs/re/README.md` §8 for setup, seeding and `rea cache-*`.
- The running `rea` MCP server **hot-reloads** config edits; do **not**
  `opencode service restart` (it kills the agent session). Windows Ghidra P0 is
  **read-only** (no annotation).

Warm-analysis env for CLI use:

```powershell
$env:GHIDRA_INSTALL_DIR       = "C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC"
$env:JAVA_HOME                = "C:\Program Files\Java\jdk-21"
$env:REA_ANALYSIS_PROVIDER    = "ghidra"
$env:REA_GHIDRA_PROJECT_CACHE = "on"
# Run the installed build so the native addon matches:
node C:\Users\gpmpo\rea-src-install\scripts\rea.mjs analyze <target> --provider ghidra --json
```

## Next actions (pick up here)

1. **`usercmd` view angles / move axes** (subtick) — the aim / autostrafe
   backend. Writing the legacy `cmd + 0x60` bit drives discrete buttons but **not**
   analog movement; the magnitude lives in the subtick move steps / the
   `CUserCmdPB` floats (`m_flForwardMove`/`m_flSideMove`) and the angles in the
   `CSubtickMoveStep` history. Use the payload's `input` → `cmd_words` capture to
   locate the `CSGOUserCmdPB`/`CBaseUserCmdPB` field offsets, then write them in
   the `CreateMove` detour. Also find `dwViewAngles` (camera-vs-view-angles).
2. **Console-output capture** for `wait_for(text=…)` and authoritative `exec`
   results. `exec` returns `{queued}` only and `-concommandpipe` is input-only,
   so output must come from `-condebug` (console log), a payload hook on the
   console-print/logging path, or the logging system.
3. Then `screenshot` and a `players_lte` / `text` predicate.

## Live validation notes (2026-10)

The full runner path works (`python scripts/run-scenario.py join-bot-match`).
A few issues cost time; all are handled now:

- **`injector.exe` is quarantined by Windows Defender after it runs** (a
  manual-map injector trips the heuristic — see `toolchain/README.md`). Rebuild
  it (`meson compile -C build-injector`) before each run, or add a folder
  exclusion.
- **Do not inject a stale/terminating instance.** `CreateRemoteThread` into a
  process that is still tearing down fails with `ACCESS_DENIED (5)`. The session
  now waits for the killed process to exit and injects the **exact PID** it
  launched (not by name).
- The payload originally imported `IMM32.dll` (which the game does not load at
  boot) via Dear ImGui's default Win32 IME handler; disabled in
  `payload/third_party/imgui/imconfig.h` (the overlay builds no Win32 backend).
  The injector still runs with `--load-missing` for robustness.
- `exec` is reliable, including `;`-separated lines. Adding several `bot_add_*`
  in one frame races the game's bot processing (an add can silently not stick),
  so scenarios pace adds and confirm each with `wait_for`.

**Game events (2026-10).** Getting events took several dead ends worth recording:

- The client manager's definitions load **per level**, not at injection: its tree
  is empty until a map loads (the payload reports `defs`/`entries`).
- `CGameEventManager::AddListener` must not be called from a non-game thread. From
  the control thread it returns `-1` for every name and, when it *does* find one,
  faults inside `tier0` (`tier0+0x1ffce9`, `read addr=0x40`).
- The engine's `CGameEventSystem` queue (`+0x100..+0x114`) is never populated in
  this build (`cap=0 data=null`), so the engine pump is not the path.
- Working approach: patch the two dispatch vtable slots (`+0x38`/`+0x40`). Their
  prologues are 18/16 bytes and contain a RIP-relative load, so the inline hook's
  16-byte trampoline rejects them (`unsupported_instruction`) — a vtable patch
  (ADR-0015 technique) works and needs no signature beyond `game_event_manager`.
- A listen server dispatches each event through both slots, so identical names
  within ~50 ms are de-duplicated.

**`usercmd` input backend (2026-10).** Validated live; worth recording:

- The hook installs cleanly (patch the `CCSGOInput` vtable slot holding
  `create_move`, slot 25) and the detour runs every tick in a live round.
- **Wait for the round to actually start.** `mp_restartgame` gives a warmup then a
  freeze period; during warmup/freeze nothing moves and the hook looks broken.
  Wait for `round_start` **and** `round_freeze_end`, then gate on a console
  `+forward` nudge actually moving the player — see `scripts/scenario/awaiting.py`.
- **A dead player ignores input.** Check `local_alive` first.
- The legacy button mask is `cmd + 0x60`: a console `+forward` sets bit 3 and
  `+jump` bit 1 (read live via the payload's `input` → `cmd_words` capture).
- **Writing that mask drives discrete actions but not analog movement.** `jump`
  leaves the ground and `bhop` hops repeatedly, but `forward` moves ~0 — CS2's
  movement magnitude is subtick (the `CUserCmdPB` floats / `CSubtickMoveStep`).
  Keep analog movement on the console backend until the subtick write lands.
- `exec` can fault (`engine2!CCommandBuffer::AddText`, `read addr=0x0`) if it is
  issued *very* early after launch (before the engine input service is up); give
  the game a moment after connecting.

## Notes / cautions for the next session

- **Do not edit `.cpp`/`.hpp`** while the user's automated C++ tidy/fix pass is
  running.
- **C++ style**: follow `docs/style-guide.md` (normative: `.clang-format`,
  `.clang-tidy`, ADR-0010). New `.cpp` definitions use `auto f() -> T`
  (non-void); declarations in headers keep the leading return type.
- The generated SDK (`payload/sdk/*.hpp`) and calibration dumps
  (`calibration/*.json`) are **gitignored** and build-specific. `exec` needs the
  engine2 `input_service` / `cbuf_add_text` offsets, which
  `scripts/calibrate-live.py` now emits (it resolves `client.dll` +
  `engine2.dll`); regenerate with `scripts/calibrate-live.py` or `--file` dumps.
- Game build for all addresses here: **25815307** (patch 1.41.9.0).
