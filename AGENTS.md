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

- C++ style (naming, idioms, file layout, tests, automated fixes):
  `docs/style-guide.md`. Normative sources are `.clang-format`, `.clang-tidy`
  and ADR-0010 in `docs/decisions.md`.
- Bulk style/modernisation changes go through `scripts/clang-tidy-fix.py`
  (one check group per commit; verify with all four `meson compile` targets plus
  `meson test -C build`). See §7 of the style guide.

## Where to resume (current work)

The active feature is the **programmatic scenario / control harness** (drive CS2
for tests: launch, inject, join a bot match, add/place bots, aim, input
sequences, await events). Resume from:

- `docs/control-harness.md` — design, decisions, command surface, next actions.
- `docs/re/engine2.md` — RE of the engine console-command path (the `exec`
  primitive; `-concommandpipe` finding).

The payload, injection, live state and the Vulkan overlay are already validated
on native Windows — see `docs/handoff.md` and `docs/windows-validation.md`.

## Static RE tooling (REA + Ghidra)

> **Start here: [`docs/re/README.md`](docs/re/README.md)** — the agent guide for
> doing RE with REA (tool discovery, the string → xref → decompile loop, vtables/
> RTTI, turning findings into dumper signatures, gotchas, warm projects, and the
> current RE map).

- REA is installed from the local checkout at `C:\Users\gpmpo\rea-src-install`
  (v6.4.0 + the persistent Ghidra project cache), `npm link`ed to the global
  `rea`. The PR source is `C:\Users\gpmpo\rea-src` (branch
  `feat/ghidra-project-cache`, merged with `main`); **Windows cannot run the
  merged build** because its native loader requires an addon field the prebuilt
  v6.4.0 artifact lacks (the addon lane is Linux + MinGW only). Provider:
  **Ghidra 12.1.4** at `C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC` (Java 21 at
  `C:\Program Files\Java\jdk-21`). Configured in the global OpenCode config's
  `mcp.rea` entry (`C:\Users\gpmpo\.config\opencode\opencode.jsonc`).
- **Warm analysis is automatic.** The MCP entry sets
  `REA_GHIDRA_PROJECT_CACHE=on`, so a target analyzed once is reused by later
  sessions: a hit opens the stored project with `-process -noanalysis` (seconds
  vs minutes) and switching targets needs no config edit. `rea cache-list` shows
  what is warm. Setup, seeding and the switch policy are in
  `docs/re/README.md` §8.
- The running `rea` MCP server **hot-reloads** config edits; do **not** run
  `opencode service restart` (it kills the agent session).
- **Prefer the integrated MCP tools** (`tools.rea.*`, called from the Code Mode
  `execute` runtime) over shelling out to the `rea` shim or driving the MCP
  server by hand: the tools reuse the bound session and return normalized,
  confidence-annotated results, while a shell call starts a fresh process. See
  `docs/re/README.md`.
- Windows Ghidra is **read-only** (no annotation).
