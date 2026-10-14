# Programmatic scenario / control harness (current feature)

**Status: control core + input implemented and validated live** (2026-10, CS2
build 25815307). `payload/control/` + the Python `scripts/scenario/` runner drive
a real game: launch via the `CS2Launch` task, inject, `exec`/`state`/`wait_for`,
plus runner-side `look`/`press`/`noclip`/`teleport` (engine `setang` and button
commands through `exec`). The `join-bot-match` scenario runs end to end and each
piece is verified live. Remaining: **game events** and **console-output
capture**.

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
                  ── events → GameEventSystemClientV001 listener (in progress)
   publish thread ── shm ──▶ Vulkan overlay layer (render)
```

## Command surface (v1)

Requests are `{"id": <n>, "cmd": <name>, "args": {…}}`; responses are
`{"id": <n>, "ok": <bool>, "result": {…}}` or `{"id": <n>, "ok": false,
"error": "<message>"}`.

| cmd | args | returns |
|---|---|---|
| `ping` | — | `{pong, pid, exec_ready, state_ready}` |
| `state` | — | snapshot: `valid`, `in_map`, `local_pawn`, `local{…}`, `players[…]` |
| `exec` | `line` | `{queued}` (semicolon-separated console line) |
| `wait_for` | `expr`, `timeout_ms` | `{matched, snapshot}` |
| `quit` | — | closes the channel |

`wait_for`'s `expr` is a structured object, ANDed across keys, evaluated every
100 ms on the control thread (so a `wait_for` blocks other commands for its
duration — single-client v1):

```jsonc
{"local_alive": true} {"players_gte": 4} {"alive_gte": 2} {"local_hp_gte": 1}
{"team": "T"} {"in_map": true}
{"all": [ … ]} {"any": [ … ]} {"not": { … }}
```

`in_map` is `valid && local player present`; `players_gte` counts every player,
`alive_gte` counts `life_state == 0`. Unknown keys are rejected so a typo is an
error rather than an always-true predicate.

## Input helpers (runner-side)

Input is **engine-native** — it rides the same `exec` path, so it needs no
offsets, no hooks and no OS input (which cannot cross sessions). Provided by
`scripts/scenario/control.py` and mirrored on `Game`:

| helper | effect |
|---|---|
| `look(yaw=…, pitch=…, roll=…)` | `setang` to the given angles; omitted components keep their current value (read from `state`). Needs `sv_cheats 1`. |
| `press(key, hold=0)` | hold an engine button (`+forward`); with `hold` release after that many seconds. Keys map to buttons (`w`→`forward`, `space`→`jump`, `ctrl`→`duck`, `mouse1`→`attack`, `r`→`reload`, …); a raw button name also works. |
| `release(key)` | release a button held with `press(..., hold=0)`. |
| `noclip(bool)` / `teleport(x, y, z=None)` | `noclip` / `setpos` (needs `sv_cheats 1`) — positioning for tests. |

Verified live: `look(yaw=90)` moved the eye angles (and `press("w", hold=1.0)`
moved the player +236 units along the facing direction).

### Input backends (target design)

Console commands are a *proxy* for input: `setang` sets the view for one frame
and `+forward` toggles the engine's own button state. The intended primary path
is the **real `CUserCmd`** the client builds each tick, with the console path
kept as an alternative backend.

| backend | how | notes |
|---|---|---|
| **`usercmd`** (target default) | hook the client's usercmd creation (client.dll) and write the desired view angles / button bitmask / move axes into the `CUserCmd` before it is submitted | exact, per-tick, survives the engine overwriting state; enables aim/movement tests and is the same seam an aim feature would use |
| `console` (current, kept) | `setang` + button commands through `exec` | no offsets or hooks; useful when the payload is not loaded, and as a fallback if the usercmd hook is unavailable |

Payload-side state (shared by both backends):

- **view angles** — an optional absolute `(pitch, yaw, roll)`; the `usercmd`
  backend applies it every tick, the `console` backend emits `setang` on change.
- **buttons** — a held bitmask (`IN_ATTACK`, `IN_JUMP`, `IN_DUCK`, `IN_FORWARD`,
  `IN_MOVELEFT`, …); `usercmd` writes it every tick, `console` emits
  `+<button>`/`-<button>` on transitions.
- **move axes** — optional analog `forward`/`side`/`up` (usercmd-only).

Control surface (payload): `look`, `press`, `release`, and `input` to
select/query the backend. Runner wrappers (`look`, `press`, …) take
`via="usercmd" | "console"` to override the active backend per call.

RE anchors already found in client.dll (RVA): `CreateMove` `0x1c9c334`,
`CCSGOInput` `0x2320304`, `usercmd` `0x1b26ed2`, `m_nButtons` `0x1c338a8`,
`InputHistory` `0x1bc66cc`, plus the event names `round_start` `0x1b27fc8` /
`player_death` `0x1b27fe8` (their xrefs will pin the listener registration).

## Code layout

- `payload/control/` (new): `include/cs2/control/{json,predicate,snapshot_json,
  console,channel,service}.hpp` + `src/*.cpp` + `meson.build`
  (`control_lib`/`control_dep`). Started from `install_thread` in
  `payload/src/payload_main.cpp`, gated by `CS2_PAYLOAD_NO_CONTROL`. Uses
  `payload/game`'s `StateReader` for snapshots. `json`/`predicate`/
  `snapshot_json` are platform-independent and built host-side by the
  `control-core` Meson test (`payload/control/tests/test_control_core.cpp`).
- `scripts/scenario/`: `session.py` (kill stale `cs2.exe` → run the `CS2Launch`
  task → wait for `client.dll` → inject `build-payload/payload.dll` →
  connect → teardown), `control.py` (pipe client + typed helpers),
  `scenarios/*.py`; `scripts/run-scenario.py` (CLI).
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

The old Linux-only local `rea-agents` project-cache patch was removed. REA is
now built from **upstream PR #2358** (existing-project launch path), and the one
Windows gap in it was fixed and pushed upstream (commit `9adad2c`).

- REA checkout: `C:\Users\gpmpo\rea-src` (branch `pr-2358`, HEAD `9adad2c23`);
  `npm link`ed so the global `rea` shim points here.
- Ghidra **12.1.4**: `C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC`.
- Java 21: `C:\Program Files\Java\jdk-21`.
- OpenCode MCP entry (`C:\Users\gpmpo\.config\opencode\opencode.jsonc`): command
  `node C:\Users\gpmpo\rea-src\scripts\rea.mjs mcp`, env
  `REA_ANALYSIS_PROVIDER=ghidra`, `GHIDRA_INSTALL_DIR`, `JAVA_HOME`,
  `REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000`.
- Warm project (fast opens, ~13 s): `C:\Users\gpmpo\ghidra-analysis\rea-project`
  with program `engine2.dll`; target copy
  `C:\Users\gpmpo\ghidra-analysis\engine2.dll`.
- The running `rea` MCP server **hot-reloads** config edits; do **not**
  `opencode service restart` (it kills the agent session). Windows Ghidra P0 is
  **read-only** (no annotation).

Warm-analysis env for CLI use:

```powershell
$env:GHIDRA_INSTALL_DIR          = "C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC"
$env:JAVA_HOME                   = "C:\Program Files\Java\jdk-21"
$env:REA_ANALYSIS_PROVIDER       = "ghidra"
$env:REA_GHIDRA_EXISTING_PROJECT = "C:\Users\gpmpo\ghidra-analysis\rea-project"
$env:REA_GHIDRA_EXISTING_PROGRAM = "engine2.dll"
```

## Next actions (pick up here)

1. **Game events** — a `GameEventSystemClientV001` listener in the payload, so
   scenarios can await/assert on events (`player_death`, `round_start`,
   `player_hurt`, …) instead of polling state. **RE in progress**, recorded in
   `docs/re/engine2.md` §"Game event system": the interface is engine2 (not
   client.dll), `create()` returns a static singleton at RVA `0x91c9e0`, the
   dispatch pump is at vtable `+0x90`, and `IGameEvent::GetName` is at event
   vtable `+0x18`. The remaining piece — the exact `AddListener` slot/calling
   convention and the `IGameEventListener2` shape — will be pinned from
   client.dll call sites (analysis in progress:
   `C:\Users\gpmpo\ghidra-analysis\client-project`).
2. **Console-output capture** for `wait_for(text=…)` and authoritative `exec`
   results. `exec` returns `{queued}` only and `-concommandpipe` is input-only,
   so output must come from `-condebug` (console log), a payload hook on the
   console-print/logging path, or a game-event listener.
3. Then `screenshot`, and a `players_lte`/`text` predicate.

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
