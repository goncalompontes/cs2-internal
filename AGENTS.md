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

**Start at [`docs/next-session-ragebot-research.md`](docs/next-session-ragebot-research.md).**

Committed at `33672a5` (tree clean, 17/17 host tests). This session profiled the ragebot,
fixed its two biggest costs, disabled lag-record backtracking by default, and ran three
deep-research threads. **The next step is acting on the research, not writing more
geometry.**

Ranked, in that doc:

1. **Hitchance σ bug** — one line: the σ is `√6` too wide vs the engine's kernel, so
   `min_hitchance` rejects shots that hit ~78 % of the time.
2. **Per-fragment AABB re-prune** in `occlude_polygon` — ~1 hour, measured 10–14×, no
   model change.
3. **Evaluate, don't enumerate** (2–3 days) — score candidates with `ray_span` +
   `penetration_after_walls` (~2–5 ms) and keep the arrangement only for the heatmap;
   then remove the stopgap occluder cap and the backtrack budget guard.
4. The carried-over items: depth-sorted early-exit (subsumed by 3), moving the state
   read / record sampling / occlusion build into the frame stages, lag-comp bone setup at
   the recorded `m_flSimulationTime` (needs RE), the inaccuracy-unit cross-check
   (`FUN_18080b880`), and the ragebot follow-ups (auto-stop oscillation, recoil/spray,
   menu cursor).

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

**Uncommitted:** nothing — the tree is clean at `33672a5`.

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
