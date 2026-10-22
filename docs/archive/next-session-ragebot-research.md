# Next session — ragebot: act on the research (perf + correctness)

> Original notes written at the end of the 2026-10-19 evening session. **Start with the
> current follow-up below**; the older status and action list are historical context.
> Branch `feat/occlusion-penetration` is ahead of its remote and the worktree is currently
> uncommitted. Preserve all existing changes; do not reset or clean the tree.

## Current handoff — follow-up session (2026-10-19)

> **This section supersedes older action items below when they conflict.** The original
> notes remain useful background, but candidate-grid exactness, the per-fragment AABB
> shortcut, and the single-call engine-stack assumption must not be treated as settled.

### Work completed in this follow-up

- `occlusion.cpp` now exposes a prepared convex clipper; `damage.cpp` prepares each
  occluder's edge planes once and reuses them for all fragments. `aim-occlusion` checks
  output topology/vertices/area against the unprepared path. **Speedup not benchmarked.**
- Added `penetration_damage_at_target`: free-flight range falloff is applied before and
  after each wall, wall loss uses damage arriving at that wall, hitgroup scaling happens
  after penetration, and the 3,000-unit gate checks wall exit. These are host-model fixes;
  **live engine differential is still outstanding**.
- The Gaussian ascent now includes the pitch derivative of its yaw covariance. The
  off-level regression compares the returned aim against a dense pitch scan.
- The 24-per-hitbox `max_occluders` cap is still present, but an overflow now marks
  `SolveResult::occlusion_candidates_complete = false`; `solve`/`solve_backtrack` fail
  closed, and the rage status reports `occlusion-cap`. Heatmap JSON also reports the
  per-hitbox candidate-completeness flag. This is a safety stop, **not cap removal**.
- Host verification: `meson test -C build-review --print-errorlogs` → **17/17 pass**.
  `build-review/` is ignored. No saved-scene performance benchmark or live damage A/B was
  run.
- Upstream occlusion-query completeness is now explicit in `OcclusionQueryResult`. The
  result tracks query faults/unavailability, the 8192-record and 4096-record-scan bounds,
  the 256-body bound, parser/shape/mesh limits, the 8192-world-hull cap, target/exclusion
  array truncation, unresolved compounds, and unavailable/invalid surface metadata. Rage
  solves fail closed on an incomplete query; `rage` and heatmap diagnostics expose the
  status/reasons. This remains distinct
  from `occlusion_candidates_complete`, which covers only the downstream per-hitbox cap.
  `meson compile -C build-payload` succeeds, compiling `payload/control` and linking
  `payload.dll` with the existing native VS/clang-cl build; no toolchain setup changes were
  needed. The host suite was rerun: **17/17 pass**. No live engine test was run.
- At the user's request, `rea setup --client opencode --yes` refreshed the OpenCode
  registration and installed the bundled REA skill (backup:
  `C:\Users\gpmpo\.config\opencode\opencode.jsonc.rea.backup`); OpenCode was not restarted.
  REA opened the current Steam `client.dll` (SHA-256
  `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`) and
  `analyze_function(FUN_1808a1820)` succeeded under Ghidra 12.1.4.
- The user asked to verify the cache path from a shell-launched MCP client. `rea mcp doctor`
  reports a healthy v6.7.0 server (207 tools); `rea cache-list` with the configured env
  reports mode `on` and a valid warm entry for the exact `659940c1…` hash; `rea cache-verify`
  reports 6/6 entries valid. However, the actual shell MCP `open_binary` logged
  `Ghidra project cache lookup failed; importing fresh`. The cache root
  `C:\Users\gpmpo\.rea\cache\ghidra-projects` had an inherited, unprotected DACL and owner
  `BUILTIN\Administrators`; REA 6.7 requires an owner-only private DACL on Windows. At the
  time, `analyze_function` from that shell MCP process was blocked until the cache root
  was fixed or a matching private cache was selected; the previous 60-second shell MCP
  analysis attempt timed out. **That is resolved — see the next bullet.**
- **RESOLVED (2026-10-20) — the REA blocker is fixed.** Two defects in the upstream
  persistent-project-cache PR (`morluto/rea#3531`, our branch
  `feat/ghidra-project-cache`, head `d4b8afbba`) kept the cache unusable on a real
  Windows host: (1) a cache root that was not owner-only was rejected for the whole
  session, and the default root inherits its DACL from the user profile, so on an
  administrator account it is owned by `BUILTIN\Administrators` and always failed the
  check — no supported remedy existed; (2) the durable root was the native runtime root,
  which Windows keeps sealed (share mode without `FILE_SHARE_WRITE`) while the native
  handle lives, so path-based renames into it — the store's publish, and the rebuild's
  move of the stored projects — were refused. The fix creates the durable root as an
  unsealed child of the runtime root (same protected owner-only DACL) and rebuilds a
  non-owner-only root as a private directory that keeps the stored projects, reporting
  the rebuild in the session Evidence. Verified live: the root is now owner-only
  (user + SYSTEM), a hit reuses the stored project, and the rebuild limitation appears
  in the Evidence. The MCP server must be respawned to pick this up — a config edit does
  it (the service hot-reloads and respawns the MCP servers).
- Current-build call-path check: `FX_FireBullets` → `FUN_180887d60` →
  `FUN_1808830e0` / `FUN_18089ca80` → `TraceLineIntervals`, with repeated advance and a
  final `FUN_180200f20` query; the same per-pellet path calls `FUN_180885710` and
  `HandleBulletPenetration`. `TraceShapeWithSortedResults` is a distinct sorted-hit API
  with a different resolved caller set. The actual shot passes a 4-span limit; the
  helper also has separate `iVar6 > 4` and 90-unit stop guards. See the new section in
  `docs/re/bullet-trace.md`. This resolves the CreateTrace-vs-TraceLineIntervals
  **call-path dispute only**, not full engine parity.
- The first live rage run exposed a gate-order deadlock: failing closed before
  `input_.refresh()` prevented the CreateMove hook from draining the pending query. The
  hook is now installed before the completeness gate. Rebuilt `payload.dll`; host suite
  remains **17/17 pass**. A live `rage` smoke test now reports
  `occlusion_query_complete=true`, `occlusion_query_issues=0`, and 2108 occluders, and
  produced a health drop (100→90). That was an open-sight multi-shot smoke test, **not** a
  damage differential.
- **Pinned one-wall live damage check (user-provided scene):** heatmap query complete,
  14 occluders, no completeness reasons. At the selected attenuated head-cell centroid
  (model `72.134` damage, `142.698` unobstructed base; yaw `89.088°`, pitch `-0.690°`),
  one AK round changed clip `30→29`. The later `player_hurt` event/state showed target
  health `100→45`, armor `100→92`, `dmg_health=55`. Reconstructing raw damage with the
  armor accounting (`55 + 2×8 = 71`) is within **1.6%** of the model value. This is
  promising single-ray/single-wall evidence, **not** full parity: the exact wall surface,
  armor-ratio input, all interval records, and multi-wall behavior were not independently
  recorded.
- After authorization, CS2 was relaunched via `CS2Launch` and the latest payload was
  injected once. `aim-probe` loaded de_dust2 and passed without firing. A settled heatmap
  query reported `query_complete=true`, no issues, 1536 occluders, and a valid sampled
  surface (`modifier=0.4`, material 77). This is a smoke test only; the zero-mod wall case
  has not yet been exercised. The previous pinned target/health state was lost on reload;
  the user reports a remaining wallbang issue and is setting up a replacement scene.
- The user reported the ragebot selecting a wall that the engine will not penetrate. A
  concrete parser bug was found: `read_shape_surface` treated a valid zero
  `bulletPenetrationDistanceModifier` as invalid and silently substituted neutral `1.0`.
  The parser now preserves zero (the damage model's `<0.1` gate blocks it) and marks
  unavailable/invalid surface metadata incomplete so rage fails closed. Added a zero-mod
  penetration regression. `meson compile -C build-payload` succeeds and host tests pass
  **17/17**. The fix is now loaded in the running game; test it against a pinned wall
  whose engine surface modifier is known to be zero before closing the issue.

### Current blockers and next actions, in order

1. **Finish the exact per-ray oracle.** Map the full `0x38` interval record, especially
   surface/material provenance; validate ordered interval and mesh/open-soup behavior,
   the `HandleBulletPenetration` secondary-trace argument, and stop boundaries against
   the live engine. The call-path dispute is resolved, but no runtime differential was
   run. The custom mesh path still keeps only two triangle crossings; the current host
   BVH test proves only BVH-vs-flat equivalence for that rule.
2. **Current-build damage parity.** The static RE path is available again, but host
   penetration tests are still model tests. Compare the prepared damage calculation to
   the actual shot on a pinned/saved scene before claiming parity.
3. **Replace the aim-path arrangement only with a completeness story.** A fixed ray grid
   is an incumbent generator, not a global-optimum proof. Overlapping hitbox polygons are
   not disjoint in `DamageField`; hit probability/expected value can double-count and the
   pointwise max does not model nearest-hit target order. Build the correct field/trace
   objective, then use conservative aim-cell upper bounds and report the remaining gap.
4. **Resolve spread and pose semantics.** Confirm the current binary's radial `sqrt(r)`
   question, inaccuracy units, seed mapping, and pellet patterns. Do not apply the `√6`
   sigma change until the transform and units are pinned. Pose uncertainty remains a
   single global sigma; use record mixtures/per-hitbox covariance when those inputs are
   available.
5. **Measure on saved scenes.** Benchmark the prepared-clipper change and the eventual ray
   path on the same saved cell/geometry set; publish p50/p95/p99 and the full-tick budget.
   Do not infer a speedup from the previous primitive microbenchmarks.

### Exactness warning

The previous proposed per-fragment AABB re-prune is **not shipped**: it preserves polygon
coverage but can change the current centroid-sampled damage values, with no bound against
the continuous damage field. Keep it out of the aim path unless that score error is bounded
or the centroid valuation is replaced. The full source audit is
[`research/aim-solver-design-review.md`](research/aim-solver-design-review.md).

**Worktree note:** `docs/style-guide.md` has pre-existing edits not made in this follow-up.
`AGENTS.md` also contains pre-existing C++-standards edits and was updated here only to point
at this handoff and record the current worktree state. Preserve those edits. The code/docs
changes from this follow-up are also uncommitted. No commits were created.

## 0. Read these first (in order)

1. [`docs/research/README.md`](research/README.md) — index of the three deep briefs, the
   ranked cheapest wins, and the bugs they found in our code.
2. [`docs/ragebot-research.md`](ragebot-research.md) — the short orientation brief.
3. [`docs/ragebot-perf.md`](ragebot-perf.md) §3.9 + §2.0 — what this session measured and
   fixed, with numbers.

## 1. What this session established (measured, live)

The previous handoff said the world projection was the bottleneck. **It was not** — the
profile said the *objective search* was:

| stage | before this session | after the fixes |
|---|---|---|
| `argmax` (objective) | **43 ms** of a 61 ms tick | ~5 ms |
| `project` (world solids) | 12.3 ms at a dense spot | 0.6–0.9 ms |
| `solve_occlude` (fragment split) | **600–730 ms** at a dense spot | 6–70 ms (spot-dependent) |
| worst single tick | **11.6 s** | ~15 ms |

Fixes (all committed, 17/17 host tests):

- **Objective search** — `DamageField::PointGrid` (uniform grid over cell AABBs),
  value-eligible candidates only, allocation-free Chebyshev bisection, and a proven `8σ`
  Gaussian-tail cutoff. Host benchmark on a live 717-cell field: `argmax_damage`
  **102 ms → 0.15 ms**, `argmax_hitchance` 105 → 1.1 ms, `hit_probability` 28 → 0.01 ms.
- **Enumeration** — `AabbBvh::query_pyramid`: the union of the eye→enemy pyramids instead
  of the single map-wide AABB (`1792 → 14` world hulls live), one base per enemy covering
  its snapshot bounds *and* its lag poses. Two live-only bugs fixed (the apex was the
  query-box corner, not the eye; a per-hitbox target list overflowed the fixed array).
- **Occluders are tagged by target**, so each hitbox only sees its own enemy's solids.
- **Mesh clusters project 8 AABB corners**, not the whole triangle soup
  (772 µs → 0.69 µs per cluster).

**Still the dominant cost:** `occlude_polygon` builds a full 2-D arrangement of each
hitbox silhouette minus every occluder silhouette. At one spot: 57 hitboxes × 312 solids
→ **15,670 cells, ~0.6 s**. It is superlinear in the number of solids in front.

**Stopgap in place (should be removed once the real fix lands):**
`PenetrationParams::max_occluders = 24` (nearest-first per hitbox) in `service.cpp`, plus
a 12 ms budget that skips backtracking on the following tick. Both are documented as
*not* exactness-preserving.

## 2. Lag-record backtracking is OFF by default

Requested and done: `wire::Settings::rage_backtrack` → menu checkbox **"Backtrack (lag
records)"** → `rage(on=..., backtrack=true)`. When off we also skip record sampling
(~0.6 ms/tick) and the redundant live-pose re-solve (`solve_backtrack` was re-solving the
multi-target field for nothing, ~4 ms/tick).

## 3. The next session's work, in priority order

### 3.1 One-line gameplay bug: the hitchance σ is `√6` too wide (do first)

`service.cpp` sets `spread_sigma = sqrt(inaccuracy² + spread²)`. The engine's spread
kernel (`r = RandomFloat(0,1)`, `θ = RandomFloat(0,2π)`, twice) has **per-axis variance
`(S²+I²)/6`**, so the variance-matched σ is `sqrt(S²+I²)/√6`. Measured consequence: for a
head-sized target the true hit probability is **0.777** where our model reports **0.221** —
`min_hitchance` is rejecting shots that hit ~78 % of the time.
See [`research/aim-objective-math.md`](research/aim-objective-math.md) §0/§3/§8. Verify
against a live `min_hitchance` run before/after.

### 3.2 The one-hour 10–14× on the hot path: per-fragment AABB re-prune

Before splitting, drop fragments whose AABB cannot overlap the occluder. Measured in
[`research/geometry-algorithms.md`](research/geometry-algorithms.md) §2.3: **36.1 → 2.57 ms**
at 64 clips, no model change, and it removes spurious cells. Do this before anything else
in `occlude_polygon`.

### 3.3 The real fix: evaluate, don't enumerate (2–3 days)

Replace the arrangement with a bounded candidate search: score `field_candidates` with
`ray_span` plus a penetration objective validated against the current
`TraceLineIntervals`/advance path. `ray_span` = **51.5 ns** is a geometric primitive
microbenchmark, not a full trace/damage measurement; the prior ~2–5 ms candidate estimate
is therefore provisional. Keep the arrangement only for heatmap rendering. Details,
pseudocode and caveats (thin visible slivers; keep a sample grid of spacing ≤ σ) are in
[`research/geometry-algorithms.md`](research/geometry-algorithms.md) §11. Public
`CreateTrace` wrappers are a separate candidate API, not the verified shot path.

**Then** delete `max_occluders` and the backtrack budget guard.

### 3.4 Numerics + optimisation (optional, measured)

- Switch the cell mass integrand to the **divergence form** `∮ g(r)((x−a)·n)/r ds` — no
  `erfc`, one Gauss rule per edge, and kernel-agnostic (GL-8: 4e-10 error at 3–8 nodes/edge
  vs our 40–128). **Do not** change the gradient (1/r³ singularity).
- The objective is genuinely **multimodal** (measured: two basins, 8.9 % apart) — better
  *seeds* matter more than a better local solver. Add Newton with the analytic Hessian and
  prune with the certified bound `f(a) ≤ v_max·P_hit(a)`.

### 3.5 Everything carried over from the previous brief

Still open: depth-sorted "fully impenetrable" early-exit (subsumed by 3.3), moving the
state read / record sampling / occlusion-box build out of `Overlay::update` into the frame
stages, lag-comp bone setup at the record's exact `m_flSimulationTime` (needs RE),
inaccuracy-unit cross-check (`FUN_18080b880`), and the ragebot follow-ups (auto-stop
oscillation, recoil/spray, menu cursor).

## 4. Instrumentation and repro

- `scripts/perf-live.py` — `spot`, `save-spot`, `restore`, `place`, `baseline`, `enum`,
  `occlusion`, `compare`, `watch`. `watch` is the one that found the 600 ms ticks.
- `heatmap enum=box|both` forces/compares the old single-AABB enumeration on the same tick
  (`enum_outside_box` must be 0).
- Host benchmark: `docs/ragebot-perf.md` §9 / the bench sources in that section; save a
  live cell set to `artifacts/live-heatmap.json` and replay it.
- **Measurement caveat (learned the hard way):** a "before/after" is **not** comparable
  across a game restart — the bots respawn elsewhere, so the cell count changes and
  dominates the timings. Pin the bots (`bot_stop 1`, `scripts/perf-live.py place`) and
  compare *stage* costs, not totals.

## 5. Gotchas that cost time this session

- The **game relaunch is flaky** (map load fails); just run the scenario again. If the
  payload never comes up, check `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`.
- **Restarting the game loses the live scene.** Save the spot (`perf-live.py save-spot`)
  and restore it; but the *bots* respawn, so reproduce the geometry with `bot_place`.
- The **menu owns the settings** and re-applies them every frame, so a control-side
  `rage(on=true, ...)` for `enabled`/`objective` is clobbered — only `force` sticks.
- A namespace-scope `std::vector` in the payload needs CRT dynamic init, which the
  manual-map injector cannot resolve. Keep scratch containers local.
- `std::min`/`std::max` are macros under `<windows.h>` in `service.cpp`.
