# Programmatic scenario / control harness (current feature)

**Status: designed, not yet implemented. RE for the `exec` primitive is
**done** (`docs/re/engine2.md`): both the in-process `Cbuf_AddText` call and the
launch-flag `-concommandpipe` channel are located and characterised (with
signatures added to the dumper registry). Next is the control core itself.**

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
| Aim / `look` | **Direct view-angle control** (user's choice), not synthetic mouse. Requires RE of the client view-angle source / a small hook. |
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
                  ── input  → in-process (view angles / CUserCmd), not SendInput
                  ── wait   → predicate over snapshots
                  ── events → GameEventSystemClientV001 listener (phase 3)
   publish thread ── shm ──▶ Vulkan overlay layer (render)
```

## Command surface (planned v1)

| cmd | args | returns |
|---|---|---|
| `ping` | — | `{pong, pid, build}` |
| `state` | — | snapshot: `valid`, `in_map`, `local{…}`, `players[…]` |
| `exec` | `line` | `{queued}` (semicolon-separated console line) |
| `wait_for` | `expr`, `timeout_ms` | `{matched, snapshot}` |
| `quit` | — | closes the channel |

`wait_for` predicate is a structured object (`{"local_alive":true}`,
`{"players_gte":4}`, `{"team":"T"}`, `{"in_map":true}`, `{"all":[…]}`),
evaluated ~100 ms on the control thread.

## Planned code layout

- `payload/control/` (new): `include/cs2/control/{channel,console,predicate,json}.hpp`
  + `src/*.cpp` + `meson.build` (`control_lib`/`control_dep`), started from
  `install_thread` in `payload/src/payload_main.cpp`, gated by
  `CS2_PAYLOAD_NO_CONTROL`. Its own `StateReader`.
- `scripts/scenario/`: `session.py` (kill Steam+cs2 → run `CS2Launch` →
  wait for `client.dll` → inject → connect → collect logs → teardown),
  `control.py` (pipe client + typed helpers), `scenarios/*.py`,
  `scripts/run-scenario.py` (CLI).
- PID/module discovery: reuse `external/dumper/target/release/cs2-dumper.exe
  --process cs2.exe --json`; injection: `build-injector/injector.exe`.

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

1. **Implement `payload/control/`** (pipe + `ping/state/exec/wait_for/quit`) per
   the milestone-1 scope. `exec` now maps to
   `FUN_1801b8790(g_InputService, 5, line, 0)` (RVA `0x1b8790` /
   `0x8dbf20`; see `docs/re/engine2.md`), resolved via the dumper's
   `input_service` / `cbuf_add_text` signatures.
2. **Decide console-output capture** — the open RE question now. `exec` alone
   returns no text; `wait_for(text=…)` / assertions need output. Candidates:
   `-condebug` console log, a payload hook on the console print / logging
   system, or a game-event listener. (`-concommandpipe`'s `<out>` pipe is not
   written in this build.)
3. Write `scripts/scenario/` + `scripts/run-scenario.py` and the smoke scenario
   (join bot match + assert state).
4. Later: `input`/`look`. These live in `client.dll` (view angles / `CUserCmd`),
   so they need the client.dll analysis project — and because the runner is in
   Windows session 0 while the game is in session 1, input **must** be injected
   in-process (session-local `SendInput` from the runner will not reach the
   game). Then game events (`GameEventSystemClientV001`) and `screenshot`.

## Notes / cautions for the next session

- **Do not edit `.cpp`/`.hpp`** while the user's automated C++ tidy/fix pass is
  running (was active when this was written).
- The repo has **unstaged** REA-teardown changes (`docs/decisions.md`,
  `docs/re/client-dll.md`, `toolchain/SETUP.md` modified; `docs/rea-project-cache.md`,
  `scripts/rea-mcp.{mjs,sh}`, `scripts/apply-rea-project-cache.sh`,
  `scripts/rea-project-cache.patch` deleted) — review/commit before layering
  more doc changes.
- Game build for all addresses here: **25815307** (patch 1.41.9.0).
