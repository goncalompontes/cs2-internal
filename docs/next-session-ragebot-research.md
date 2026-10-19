# Next session — ragebot: act on the research (perf + correctness)

> Written at the end of the 2026-10-19 (evening) session. **Start here.**
> Branch `feat/occlusion-penetration`, tree **clean** at `33672a5`. Everything is
> committed: `5dd49fb` (perf + backtrack off), `9296a42` (renderer/menu), `e8ba263` +
> `33672a5` (research).
>
> This session instrumented and profiled the ragebot properly, fixed the two biggest
> costs, disabled lag-record backtracking by default, and ran three deep-research
> threads. The **next step is not more geometry** — it is acting on the research briefs.

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
`ray_span` + `penetration_after_walls` (nearest-first, 4-wall budget). Measured
`ray_span` = **51.5 ns**, so ~57 hitboxes × 49 candidates × 14 solids ≈ **2–5 ms**. Keep
the arrangement only for the heatmap render. Details, pseudocode and the caveats (thin
visible slivers; keep a sample grid of spacing ≤ σ) are in
[`research/geometry-algorithms.md`](research/geometry-algorithms.md) §11, and the CS2
trace shape (one call returns the whole sorted surface stack) is in
[`research/autowall-implementations.md`](research/autowall-implementations.md) §4.

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
