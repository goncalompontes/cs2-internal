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

**Start at [`NEXT_SESSION_WAVE3.md`](NEXT_SESSION_WAVE3.md) →
[`docs/handoff-wave3.md`](docs/handoff-wave3.md).**

That is the 2026-10-22 handoff: what landed (eight tasks — the chams' desc
cache, the Monte-Carlo core and its live wiring, the fire path, the spread
patterns, the settings ownership, the ledger and the flaky-test fix), the **two
branches that are not merged** (the payload's fault guard, and the engine trace
probe — **in that order**, and the probe must be reconciled with the guard),
and the **live test** the operator has been asking for: rebuild, restart the
game, inject once, point at a solid wall, and compare the engine's trace with
our occluder logic. The earlier handoff is
[`docs/handoff-wave2.md`](docs/handoff-wave2.md).

That is the 2026-10-21 handoff for the feature wave: where every artifact lives
(the repo, the task worktrees, the neverlose reversal, the Linux box, the OpenCode
config), what landed, what is open, and the **orchestration playbook** — a
written spec per task, one git worktree per agent, an implementer sub-agent, an
**adversarial** reviewer sub-agent that reads the same spec, a fix round, and a
merge with the full verify loop. The mechanics of that loop are a plugin: it
runs **plan → verify → review → fix → merge** over eight `tools.orchestrate.*`
tools: `plan` makes the task's worktree, `verify` runs the verify loop in that
worktree, `review` points a review worktree at the commit, `fix` returns the
findings to the implementer, `merge` merges the branch into the integration
branch, `status` is the task board, `size` explains the worktrees' size, and
`cleanup` removes the worktrees that are done with (report-only without
`apply: true`; a worktree with a modified tracked file needs `force: true`).
The plugin's **source** — the thing to read and the thing to edit — is its own
repository (`C:\Users\gpmpo\orchestration-plugin`); the installed copy is a
**copy** of it, in the same shape, so `node scripts/install.mjs` is what makes
an edit live. **The merge verifies the merged tree before it commits** and
**refuses a branch with no review** (or a *DO NOT SHIP* one) unless
`allow_unreviewed: true`; an *older* review still counts, with a warning. The
index is [`docs/orchestration/README.md`](docs/orchestration/README.md) — the
tool table, the merge rules and this repository's orchestration files are there
— and the detail is the plugin's own `README.md`.

**Do this first:** the operator reported that the movement (the auto-strafer or
the bhop) **no longer accelerates**. That is **settled** (`docs/handoff-wave2.md`
§5.1, `docs/re/strafing.md` §"The gate"): the silent encoding is the only one
that gains speed, it works out of the box (the server honours the sub-tick view
angles by default), and the payload now **reports** the outcome (`strafe_gain`)
and the gate (`strafe_steered` / `strafe_ignored`) instead of doing nothing
quietly. The next open item is the **chams' hook target** (§5.2).

**The earlier, ragebot-focused handoff is still in
[`docs/next-session-ragebot-research.md`](docs/next-session-ragebot-research.md)**
(it is the follow-up section at the top) and the deep briefs are indexed at
[`docs/research/README.md`](docs/research/README.md). It describes the ragebot's
remaining *exactness* gaps, which are a separate, deeper project from this wave.

**Live measurement caveat:** a "before/after" is *not* comparable across a game restart —
the bots respawn elsewhere, so the cell count changes and dominates the timings. Use the
host benchmark over a saved cell set for timing, and live runs (`scripts/perf-live.py`)
for correctness.

The ragebot itself (Tier-2 occlusion, angle-correct field, multi-target, assists,
per-weapon profiles) is described in `docs/ragebot.md` and audited in
`docs/ragebot-review.md`; the perf work is in `docs/ragebot-perf.md`. The payload,
injection, live state and overlay are validated on native Windows — see
`docs/handoff.md` and `docs/windows-validation.md`.

**Uncommitted:** no — the integration tree is clean (2026-10-22). The
open work is in the task worktrees under `C:\Users\gpmpo\cs2-wt\` (one per
agent, each on its own branch) and in the task board
(`orchestrate.status({project: "cs2"})`).

**What is open now** (the specifications are in `docs/orchestration/`):

* **T42 — the payload's fault guard.** It is the only safety net for an engine
  call that faults, and it **killed the payload 4 times in 12** on the fault it
  was written for. It is **implemented and green but was never reviewed** — run
  the review, then the fix round, then the merge. **This is first.**
* **T39 — the engine trace probe.** The instrument for the occlusion
  investigation: it asks the engine its own question (`TraceLineIntervals`) and
  publishes both answers plus the difference. It is **re-reviewed** and must
  land **after T42**, reconciled with T42's one-guard rule.
* **The live run.** The chams' detour, the Monte-Carlo objective, the animfix's
  graph capture and the **fixed** camera have never run in a real game. The
  procedure is in `docs/handoff-wave3.md` §4.
* **The earlier wave's items are closed or recorded**: the chams' draw hook
  (T26), the third-person camera's crash (T27) and the curated offsets (T28)
  are merged and re-reviewed — their findings are in
  `docs/known-issues.md` §2, §5 and §6. See `docs/handoff-wave2.md` §5.

**The protection pass is a separate project** — `C:\Users\gpmpo\cs2-protect`
(its own repository, CMake build, LLVM SDK, tests and docs). This repository
consumes the built `Cs2Protect.dll`; it does not build, test or document it.
See [`docs/protection.md`](docs/protection.md) for the wiring
(`-Dprotect_tools=<repo>`). The protection workstream's handoff, plan, specs
and research live in that repository's `docs/`.

**The live run** — the one thing static review cannot close — has its own
checklist: **[`docs/operator-live-check.md`](docs/operator-live-check.md)**.

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
