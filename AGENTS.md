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

## Where to resume (current work)

**Start at [`docs/next-session-ragebot-research.md`](docs/next-session-ragebot-research.md).**

The current, ordered handoff is the **follow-up section at the top of**
[`docs/next-session-ragebot-research.md`](docs/next-session-ragebot-research.md). It
supersedes the older ranked list in that file. Current state: the prepared-clipper,
penetration-order, pitch-gradient, and per-hitbox cap fail-closed changes are uncommitted;
the host suite passes 17/17. No live engine differential or saved-scene benchmark was run.
Do not reset or discard the working tree; the handoff identifies the remaining exactness
gaps. The REA blocker (the cache root) is **resolved** — see the RE tooling section.

The short orientation brief is [`docs/ragebot-research.md`](docs/ragebot-research.md); the
three deep briefs (with measured benchmarks, primary-source citations, licence notes and
the bugs they found in our code) are indexed at
[`docs/research/README.md`](docs/research/README.md).

**Live measurement caveat:** a "before/after" is *not* comparable across a game restart —
the bots respawn elsewhere, so the cell count changes and dominates the timings. Use the
host benchmark over a saved cell set for timing, and live runs (`scripts/perf-live.py`)
for correctness.

The ragebot itself (Tier-2 occlusion, angle-correct field, multi-target, assists,
per-weapon profiles) is described in `docs/ragebot.md` and audited in
`docs/ragebot-review.md`; the perf work is in `docs/ragebot-perf.md`. The payload,
injection, live state and overlay are validated on native Windows — see
`docs/handoff.md` and `docs/windows-validation.md`.

**Uncommitted:** yes. Preserve the current worktree, including edits in this follow-up and
the existing `AGENTS.md` / `docs/style-guide.md` changes.

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
