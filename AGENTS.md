# Agent notes

> **New machine?** Read [`docs/setup-windows.md`](docs/setup-windows.md) first —
> it installs and verifies everything (tools, the `CS2Launch` task, the Vulkan
> layer registration, REA + Ghidra) and ends with an end-to-end checklist. Then
> run `scripts/preflight-windows.ps1` to see what is still missing.

## Launching the game on Windows

The agent (and anything else you run over SSH / from the OpenCode background
service) runs in **Windows session 0**, which has **no GPU and no interactive
desktop**. Launching the game directly from here means it cannot create a
Direct3D device and fails to start ("no GPU available" / no window).

Always start the game through the **`CS2Launch` scheduled task**, which runs in
the interactive session (session 1) where the GPU lives:

```powershell
schtasks /run /tn CS2Launch
# or
powershell -NoProfile -c "Start-ScheduledTask -Name CS2Launch"
```

That task runs `scripts/launch-cs2.cmd` in session 1 (Steam `-applaunch 730`
with the offline flags `-insecure -novid`). To change the flags, edit
`scripts/launch-cs2.cmd`.

Do **not** run `cs2.exe` or `steam.exe` directly from an agent/SSH shell for
play/injection testing — it will land in session 0 and there will be no GPU.

## Conventions

- For C++ work, apply the shared user-level `cpp-coding-standards` skill as the
  cross-project baseline. This repo's C++ style, naming, tests, and tooling
  overrides are in `docs/style-guide.md`; `.clang-format`, `.clang-tidy`, and
  ADR-0010 in `docs/decisions.md` are normative where they specify a rule.
- Bulk style/modernisation changes go through `scripts/clang-tidy-fix.py`
  (one check group per commit; verify with all four `meson compile` targets plus
  `meson test -C build`). See §7 of the style guide.

## The offsets do not need the game

A new **signature** (in `external/dumper/src/analysis.rs::known_signatures()`)
becomes usable with an **offline** refresh — no CS2, no process:

```powershell
python scripts\resolve-offsets-offline.py          # writes calibration/offsets.json
python scripts\resolve-offsets-offline.py --check  # compares, writes nothing
```

The dumper resolves the very same curated signatures from the module's file on
disk that the live calibration resolves in the process' memory, so the two
agree byte for byte. A signature that stops matching is **named**
(`unresolved: <name>`), not dropped - and the dump is **not** written: a
dump without that offset is a dump the payload cannot use, so the run exits
non-zero and the previous dump stays. `--allow-unresolved` writes the
degraded dump anyway, on purpose.

The **live** calibration (`scripts/calibrate-live.py`, a running game) is still
needed for the **interfaces** and the **schema** — they are runtime state, not
patterns in a binary. See [`docs/re/offsets.md`](docs/re/offsets.md) and
`docs/setup-windows.md` §11a.

## Where to resume (current work)

**Read [`docs/handoff.md`](docs/archive/handoff.md) only for history.** The
current work is the **cleanup** branch (`chore/cleanup`):

* **One render path.** The external Vulkan layer, the shared-memory model and
  the settings segment are gone; the payload draws in-process from the game's
  DXGI/D3D11 Present. The old layer documents are archived.
* **JSON-RPC control.** The named pipe is gone. The payload listens on an
  AF_UNIX socket (`%LOCALAPPDATA%\cs2-internal\control.sock`, TCP loopback as a
  fallback), framed with `Content-Length`, with a per-session token in the
  session file (`control.json`). The client and the MCP server are unchanged at
  the command level.
* **One state machine.** `payload/game/include/cs2/game/game_state.hpp` reduces
  every snapshot to a `GameScreen` and a capability mask; every feature asks it
  before acting. `state` publishes `screen` + `capabilities`.
* **No diagnostics in the feature code.** `RagePerf`, the trace probe, the
  heatmap *command*, the HUD status sections and the per-frame state dump are
  removed. The crash guard and the event ring stay.
* **The protection pass is a separate repository** (`C:\Users\gpmpo\cs2-protect`).
* **One-shot documents are in [`docs/archive/`](docs/archive/).** The
  normative set is `AGENTS.md`, `docs/architecture.md`, `docs/decisions.md`,
  `docs/style-guide.md`, `docs/setup-windows.md`, `docs/control-harness.md`,
  `docs/known-issues.md`, `docs/operator-live-check.md`, `docs/ragebot*.md`,
  `docs/re/**` and `docs/research/**`.

**The work bench is the MCP server** (`tools.cs2.*`). It carries the game
(`cs2_launch`, `cs2_inject`, `cs2_state`, `cs2_screen`, `cs2_ensure`, input,
settings, scenarios) and the build (`cs2_build`, `cs2_test`, `cs2_offsets`,
`cs2_preflight`, `cs2_calibrate`). The one-time scripts are gone; do not add a
new `scripts/*.sh` or a new one-off `.md` for a task — extend the tool.

**Still owed:** the live run (chams, Monte-Carlo, animfix capture, fixed
camera), and the Tier-1 trace replacement after the probe's measurement. The
operator runs the live validation; the payload no longer carries instrumentation
for it.
## Static RE tooling (REA + Ghidra)

> **Start here: [`docs/re/README.md`](docs/re/README.md)** — the agent guide for
> doing RE with REA (tool discovery, the string → xref → decompile loop, vtables/
> RTTI, turning findings into dumper signatures, gotchas, warm projects, and the
> current RE map).

- REA is installed from the local checkout at `C:\Users\gpmpo\rea-src` (branch
  `feat/ghidra-project-cache`, head `d4b8afbba`), `npm link`ed to the global `rea`
  as `rea-agents@6.7.0`. The MCP entry runs
  `…\npm\node_modules\rea-agents\scripts\rea.mjs mcp`, which is that link. The older
  `C:\Users\gpmpo\rea-src-install` checkout is **stale — do not use it**. **Windows
  runs this build**: the native addon is present (`native\windows\build\rea-windows-x64.node`)
  and `rea doctor --provider ghidra` reports `ghidra-native_authority` healthy. Provider:
  **Ghidra 12.1.4** at `C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC` (Java 21 at
  `C:\Program Files\Java\jdk-21`). Configured in the global OpenCode config's
  `mcp.rea` entry (`C:\Users\gpmpo\.config\opencode\opencode.jsonc`).
- **Warm analysis is automatic.** The MCP entry sets `REA_GHIDRA_PROJECT_CACHE=on`
  (plus an explicit `REA_GHIDRA_PROJECT_CACHE_DIR`), so a target analyzed once is reused
  by later sessions: a hit opens the stored project with `-process -noanalysis` (seconds
  vs minutes) and switching targets needs no config edit. `rea cache-list` shows
  what is warm. Setup, seeding and the switch policy are in
  `docs/re/README.md` §8.
- **The cache root is private and self-repairing.** The root
  `C:\Users\gpmpo\.rea\cache\ghidra-projects` is owned by the current user with a
  protected DACL (that user + SYSTEM only). Upstream `d4b8afbba` (PR #3531) rebuilds a
  root that is not owner-only as a private directory that keeps the stored projects and
  reports the rebuild in the session Evidence, so a root that lost its DACL no longer
  disables the cache. The **REA blocker is resolved** (2026-10-20).
- The running `rea` MCP server **hot-reloads** config edits; do **not** run
  `opencode service restart` (it kills the agent session).
- **Prefer the integrated MCP tools** (`tools.rea.*`, called from the Code Mode
  `execute` runtime) over shelling out to the `rea` shim or driving the MCP
  server by hand: the tools reuse the bound session and return normalized,
  confidence-annotated results, while a shell call starts a fresh process. See
  `docs/re/README.md`.
- Windows Ghidra is **read-only** (no annotation).
