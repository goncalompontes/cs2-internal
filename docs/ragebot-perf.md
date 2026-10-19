# Ragebot performance — instrumentation, findings, fixes

> Written 2026-10-19. The ragebot now runs **in-process on the game's render thread**
> (workstream A, ADR-0023), so its per-frame cost lands directly on the frame and its
> aim cadence now matters. This note covers the instrumentation (`rage` → `perf`, and
> a `[rage-perf]` log line), what it shows, the causes, and the fixes.
>
> Companion: `docs/ragebot.md` (§0 status), `docs/ragebot-review.md` (correctness
> audit), `docs/re/lagcomp.md`, `docs/re/aim.md`, `docs/re/subtick.md`.

## 0. TL;DR

The ragebot's frame cost had two multipliers stacked on top of each other:

1. **`solve_backtrack` called the full `solve` once per pose** — the live pose plus
   every lag record in the rewind window (~13 ticks at 64 tick, plus the live pose). Each
   `solve` re-projects every hitbox, re-runs `occlude_polygon` (fragment split +
   penetration) and runs the objective's optimum search.
2. The default objective is **`hitchance`**, whose optimum is
   `argmax_hit_probability(sigma, grid=40)` — a **41×41 = 1681-point** grid, each point
   running `hit_probability` = **32** kernel samples × `value_at` (O(cells)). That is
   ~10^5–10^6 `polygon_contains` per `solve`, **× poses** per frame, on the render
   thread. That is the stutter/latency.

**Fixed / added:** the **objective search is now exact and cheap** — the sampled
41×41 grid × 32-point kernel is gone, replaced by the analytic Gaussian mass over the
convex cells (`hit_probability_exact`) plus a compass search (§3.2); and the backtrack
poses are capped (the live pose + the newest 6 records). A "one union field over all
poses" variant was evaluated and **rejected**: the objective search is O(cells²), so
merging the poses' hitboxes multiplies `cells` by the pose count and is *slower* (§3.1).
Host tests still pass; measure the effect live (§2).

## 1. How it runs (pipeline + cadence)

```
game render thread                    game tick thread (CreateMove)
────────────────────                  ─────────────────────────────
DXGI Present detour
  └─ Overlay::update()               Input::apply(cmd)
        (throttled to 33 ms ≈ 30 Hz)    ├─ game-thread sink (occlusion query on a
        └─ on_snapshot(snapshot)       │   game thread with tier0 TLS)
              ├─ sample_records()      └─ writes the silent sub-tick aim + IN_ATTACK
              ├─ cache_occlusion_targets()   from the stashed rage aim
              ├─ build_input() × candidates
              ├─ project the world solids
              ├─ aim::solve(multi)         ← one target field, all candidates
              └─ aim::solve_backtrack()    ← per-pose (was: solve per pose)
```

Cadence is the first problem after cost: the whole solve runs at the **overlay's
30 Hz throttle** (`features::Overlay::update`, 33 ms), while the game simulates at
64 tick and presents far faster. The aim is stashed and re-written on **every**
`CreateMove`, so between two solves the *same* aim is held — i.e. the aim is on
average ~½ a solve-interval (≈16 ms ≈ 1 tick) stale, plus the thread-handoff latency.
Against a strafing target that is the difference between a hit and a miss.

## 2. Instrumentation

`payload/aim/src/solver.cpp` gained an optional `SolveStats*` (per-phase µs:
`project_us`, `occlude_us`, `argmax_us`; counts: solves, hitboxes, fragments, cells).
`solver.hpp` declares it; the default (`nullptr`) keeps the hot path unmeasured.

`payload/control/src/service.cpp` wraps each stage of `on_snapshot` and accumulates
into `Service::RagePerf` (`perf_`): `sample_us`, `query_us`, `build_us`,
`project_us`, `solve_us`, `backtrack_us`, `total_us`, `last_us`, `max_us`, `solve_calls`
and the last frame's `candidates`/`hitboxes`/`occluders`/`poses`/`cells`.

Two ways to read it:

- **`rage` command JSON** — a `perf` object with the averages since start
  (`total_us`, `solve_us`, `backtrack_us`, `build_us`, `project_us`, `query_us`,
  `sample_us`), `last_us`, `max_us`, and the last frame's counts:
  ```powershell
  python scripts/run-scenario.py rage --no-launch --no-inject --keep-running
  # or, from a scenario: game.rage(force=True)
  ```
- **payload log** — a `[rage-perf] …` line every 256 rage frames (averages, worst
  frame, counts). `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`.

How to read it:

| symptom | meaning |
|---|---|
| `backtrack_us` ≫ `solve_us`, `poses` ≫ 1 | the per-pose cost (fixed; should now scale with hitboxes, not poses) |
| `solve_us` ≫ (`build`+`project`+`query`), `cells` large | the objective search (`argmax_hit_probability` grid) dominates |
| `project_us` ≫ others, `occluders` large | the world-solid projection / `occlude_polygon` fragment split dominates |
| `query_us` large | the world enumeration (`EntitiesInBox` + walking shapes) dominates |
| `max_us` ≫ `total_us` | frame spikes (GC-free; usually a big occlusion pass or a busy world) |

`total_us` is the whole rage path per frame; on the render thread it is a direct
addition to the frame time, so `total_us` > ~2000 µs (2 ms) at 200 fps is ~40 % of a
5 ms budget.

## 3. Findings (ranked) and fixes

### 3.1 `solve_backtrack` solves every pose — **pose cap applied**

`solve_backtrack` loops the poses and calls `solve(input)` per pose to pick the pose
with the best value. With the live pose + the ~13 records in the rewind window, that is
~14 full fields + 14 objective searches per frame.

**Rejected fix — one union field.** Building a single field over every pose's hitboxes
and solving once does *not* help: `argmax_hit_probability` is **O(cells²)** (each of the
`12·cells` candidate points runs `hit_probability` → 32 kernel samples → `value_at`,
itself O(cells)), so merging the poses multiplies `cells` by the pose count and the
search gets ~pose-count× **more** expensive, not less. This is the key structural
constraint: the cost is super-linear in the number of silhouettes in one field, so the
fix has to reduce the *number of solves*, keeping each field small.

**Applied fix.** Select the poses by the **rewind tick**, not by count: the server
validates the shot against the pose at `aim::rewind_tick(local_tick, max_unlag, interp)`
(`floor_tick`), so the service now keeps the live pose and only the records **nearest
that tick** (±3 ticks, `payload/control/src/service.cpp`). This is both cheaper (~4-5
solves instead of ~14) and **more correct** than either extreme: aiming at "all
in-window records" is wasteful, and "the newest few" is wrong — the rewind tick is the
*oldest* in-window tick, so the newest records are the wrong pose. (An earlier version
of this fix capped the newest records; that was a correctness bug, now removed. When the
tick base is unknown it falls back to the newest few.)

### 3.2 The objective search sampled the cone — **replaced by an exact analytic form**

`argmax_hit_probability(sigma, grid = 40)` scanned a 41×41 grid plus the cell
candidates, and each point called `hit_probability` = 32 kernel samples × `value_at`
(O(cells)). That is a 2-D quadrature of a 2-D integral — the "sampling" the design set
out to avoid — and it was the dominant per-`solve` cost, multiplied by the pose count.

**Applied fix (exact, no sampling).** The field is a disjoint union of convex cells with
constant value, so the cone probability has a closed form:

```
P(a) = Σ_i value_i · mass(cell_i − a)
```

and `mass`, the standard bivariate-normal (ρ = 0, our kernel) mass over a convex polygon,
is a 1-D **boundary integral** (Green's theorem):

```
mass(P) = −∮_∂P φ(x)·Φ(y) dx
```

evaluated per edge (split into ~σ segments, 8-point Gauss-Legendre). This is exact to
quadrature precision — verified in `aim-field` against the closed form
`(Φ(u1)−Φ(u0))·(Φ(v1)−Φ(v0))`. Cost is **O(cells · edges · segments) per evaluation**
(no kernel, no grid), and `argmax_hit_probability` maximizes it by **gradient ascent with
a backtracking line search** (`mass_ascent`) using the exact boundary-flux gradient
`∂mass/∂a_u = −(1/σu)∮φφ dv`, `∂mass/∂a_v = +(1/σv)∮φφ du` — a handful of evaluations,
no grid, no sampling. The reported `hitchance` (`hit_probability`) uses the same exact
mass, so the fire gate is exact too. `argmax_expected` likewise (`expected_exact`, the
value-weighted mass).

The `aim-field` test proves the analytic argmax is **not beaten by a 41×41 grid** (it
found 0.76737 vs the grid's 0.76727). The old `grid` argument and the `rage grid=N` knob
were removed.

### 3.3 The solve now runs in the CreateMove hook (was the render thread) — **done**

The solve used to run in `on_snapshot`, i.e. inside `Overlay::update` on the **render
thread**, throttled to 33 ms — so (a) the cost landed on the frame, and (b) the aim was
up to ~1 tick stale and solved at ~30 Hz while the game ticks at 64.

**Now:** the solve is `Service::rage_tick`, called from `Service::on_game_thread`, which
`Input::run_game_thread_sink_now()` invokes from the **slot-5 `create_move_input`
detour** — once per tick, before the engine builds the command, so the raw-angle writes
and the command's silent history both use this tick's freshly solved aim. The render
thread only stores the latest snapshot and samples records / builds the occlusion box.
`apply()` (slot 25 and slot 5) no longer runs the sink, so the solve is not doubled.

The snapshot is handed over by the render thread; the **hitboxes are re-read inside the
solve** (on the hook), so only the slow-changing player list / eye / weapon come from it.
`perf.frames` now counts hook solves, and the stage timers measure the hook path.

### 3.4 The hitboxes are the *interpolated* client pose, not the server's rewound pose

`read_model_hitboxes` walks the live bone array, i.e. the **interpolated** pose you
see. The server validates the shot against the target rewound to
`tick − latency − interp`. Our lag records store hitboxes sampled from the same
interpolated array, so they are the client's *rendered* history, not the server's
rewound skeleton.

**Community (UC):** force a **bone setup at the exact `m_flSimulationTime`** of the
record before reading the hitboxes ("update bones before getting their position",
Exlodium; "force a bone setup at the exact m_flSimulationTime to get the actual
server-side skeleton", shakro). **Recommendation:** re-resolve the target's bones at
each record's `m_flSimulationTime` (the engine has a `SetupBones`/`CStudioHdr` path),
rather than sampling the interpolated array on the render tick.

### 3.5 Record validity / rewind window and interpolation

Community (`shakro`, UC "lagcomp"): the validity check must subtract the **lerp**
(`cl_interp`, `cl_interp_ratio`) from the tick base, not just compare simulation
times, or you aim at a pose the server has already dropped from the unlag buffer.
`rewind_tick` (`payload/aim/records.cpp`) does model `interp`; verify it uses the
*player's* live `cl_interp`/`cl_interp_ratio` (and `sv_maxunlag`), and that the record
trim uses the same floor. Also `m_flSimulationTime` must be checked to have *changed*
before storing (shakro) — our per-frame sampling stores duplicates otherwise.

### 3.6 The inaccuracy units are still unvalidated

`docs/ragebot-review.md` §2.4: `m_flInterpolatedInaccuracy` is used as radians without a
live cross-check; a live idle cone of ~5.4° is implausible. A wrong cone scale breaks
both the `hitchance` objective (bad aimpoint) and the `min_hitchance` gate
(over-gating → "doesn't fire / fires the wrong spot"). This is a correctness bug that
presents as "bad performance" too.

**Recommendation:** reverse the current build's `GetInaccuracy` composition
(`FUN_18080b880` equivalent) and cross-check against `CMsgTEFireBullets` / the
server-observed spread.

### 3.9 Objective-search cost (this session)

`argmax_damage`/`argmax_hitchance` were O(cells²): every candidate point scanned every
cell twice (`value_at` + `field_margin`). On the live 717-cell field that was **102 ms**
per call, and the rage path calls it once per pose (multi-target + backtrack). Fixed
exactly, with no geometry dropped:

- **Uniform point grid** (`DamageField::PointGrid` + `cells_near`): buckets the cells by
  AABB; a probe visits only its bucket (plus a `large` list for cells spanning > 64
  buckets). Built lazily, invalidated by `add`/`clip`, and revalidated against
  `cells.size()` (so `cells` staying public cannot serve a stale index).
- **Value-eligible candidates** (`field_candidates(cells, value_floor)`): a point's
  damage is the max over its containing cells, so only cells that can reach the target
  value can hold the optimum.
- **Allocation-free Chebyshev bisection** (`clip_half_plane_into`) with a `1e-5`
  tolerance.
- **Gaussian tail cutoff** (`k_mass_cutoff = 8σ`) in `hit_probability_exact` /
  `expected_exact` / `mass_gradient`: cells entirely outside the box have mass
  ≤ `Φ(-8) = 6.2e-16`.

Host benchmark over the live cell set (real damage values), before → after:

| call | before | after |
|---|---|---|
| `argmax_damage` | 102 ms | **0.15 ms** |
| `argmax_hitchance` | 105 ms | **1.1 ms** |
| `hit_probability` (σ=0.002) | 28 ms | **0.01 ms** |
| `argmax_hit_probability` (σ=0.002) | 514 ms | **34 ms** |

The remaining cone cost is `mass_ascent`'s iteration count (see §2.0 "Still open").

### 3.7 Occlusion cost — **correct pruning + mesh BVH (no caps)**

`occlude_polygon` splits every fragment by every candidate occluder, and
`subtract_convex` emits up to one piece per occluder edge — so the raw fragment count
grows with the number of solids in front. Two earlier *incorrect* mitigations (cap the
occluder count, cap the fragment count) were **reverted**: they could drop a wall that
matters or a region that still carries damage. The replacements are exact:

- **Blocked-fragment pruning (exact).** Candidates are processed nearest-first; a
  fragment whose **exact** value is already 0 — computed through the same model as the
  final valuation (`fragment_layers` + `penetration_after_walls`, with the engine's
  4-wall budget, the `< 0.1` modifier gate and the `< 1` damage stop) — is dropped,
  because adding more walls can only decrease the value. The result is identical to
  valuing every arrangement cell; it just stops early. A raw "≥ 5 walls" count is *not*
  used, because span-filtering can make a 5-covering fragment still hittable.
- **Mesh triangle BVH (exact).** A collision mesh's `ray_span` used to scan **every**
  triangle (up to 2048 per cluster) per ray. `MeshBvh` (a median-split BVH built once
  at parse time, `build_mesh_bvh`) makes it O(log n); `aim-occlusion` checks it returns
  the identical span to the flat scan for a grid of rays. This is the "mesh BVH" the
  review (§5.4) called for.

- **World-hull enumeration BVH (exact).** `query_world_occluders` used to walk **all
  ~10k cached world hulls** per query and deep-copy every kept one. `aim::AabbBvh`
  (`payload/aim/{include/cs2/aim/bvh.hpp,src/bvh.cpp}`, pure + host-tested `aim-bvh`)
  is built once over the cached hulls' world AABBs (rebuilds with the cache, i.e. once
  per level) and the per-query box overlap is now O(log n + k). The `aim-bvh` test
  proves the BVH query returns **exactly** the linear-scan set over 200 random boxes.

**Fixed (2026-10-19, later session):** the query volume is now the **union of the
eye→enemy pyramids** (`AabbBvh::query_pyramid`) instead of the single eye→all-hitboxes
AABB. The pyramid is the convex hull of the eye and the enemy's AABB; a solid can only
occlude a hitbox if it meets a segment from the eye into that hitbox, so it must lie
inside that hull. The test is a node-AABB vs hull face-plane SAT: it can keep a solid
that turns out not to occlude, never drop one that does.

- **Live effect:** at one spot the world-hull enumeration went from **1792 kept to 14**
  (and from 2615 to 819 at another, where the enemies were spread out).
- **Correctness proof (live, same tick):** the *used* occluder silhouettes — the solids
  `build_heatmap` decided actually stand between the eye and a hitbox — are **identical**
  between the AABB and pyramid enumerations (13 vs 13, zero difference in either
  direction), with the same cell count and the same attenuated-cell count.
- **Correctness proof (host):** `aim-bvh` samples rays from the eye through the hitbox
  volume (centre, 8 corners, random interior points) and asserts every box a ray crosses
  is kept.
- **Two bugs this exposed**, both invisible to a host test and caught only live:
  1. the pyramid was given the **query-box corner as its apex** instead of the eye (the
     eye was not threaded into `walk_body`), which pruned walls in the real line of sight;
  2. the target list was **one pyramid per hitbox**, so 19 hitboxes × 32 lag records
     overflowed the fixed 64-entry array after the first enemy — every later enemy's
     walls were silently pruned. It is now **one pyramid per enemy**, unioned over that
     enemy's current snapshot bounds *and* all its lag poses. The snapshot bounds matter:
     a pawn that just respawned or teleported can have a record ring that is empty or
     still holds its old position, and stale bases prune the walls that matter.

The diagnostic stays in the payload: `heatmap enum=box` forces the old AABB enumeration,
and `enum=both` also runs it on the same tick, reporting `enum_box_kept`,
`enum_pyramid_kept` and `enum_outside_box` (must be 0). `scripts/perf-live.py compare`
drives the live A/B and asserts the used-occluder silhouettes match.

### 3.8 Recoil / spray

`read_aim_punch` sums the predictable + unpredictable punch and subtracts it from the
silent aim. The community pattern is a per-shot incremental kick from the weapon's
recoil table; the total-punch subtraction should be equivalent *if* the shot inherits
exactly that punch at the commanded tick — verify live (does the bullet use
`viewangles + punch`, or does the punch only move the camera?). `docs/ragebot-review.md`
§2.2. Not measured by the instrumentation; add a per-shot "aim vs landed" probe.

## 4. What changed in this pass

- `payload/aim/include/cs2/aim/field.hpp` / `src/field.cpp` — **exact cone probability**:
  `hit_probability_exact` / `expected_exact` (the Gaussian mass via the boundary
  integral), `compass_max`, and the removal of the sampled-grid `argmax_hit_probability`
  / `argmax_expected` (§3.2).
- `payload/aim/src/damage.cpp` — `occlude_polygon` splits a fragment by occluders
  **nearest-first** and drops a fragment as soon as its **exact** value is 0 (the same
  model as the final valuation) — no candidate or fragment cap (§3.7).
- `payload/aim/include/cs2/aim/occlusion.hpp` / `src/occlusion.cpp` — `MeshBvh`
  (median-split triangle BVH), `build_mesh_bvh`, `ray_mesh_bvh`; `OccluderCell` carries
  it and `ray_span` uses it (§3.7).
- `payload/control/include/cs2/control/service.hpp` / `src/service.cpp` — the solve moved
  to `rage_tick` on the game thread (§3.3): `on_snapshot` stores the snapshot + samples
  records, `on_game_thread` (CreateMove) runs the occlusion query and the solve.
- `payload/control/include/cs2/control/input.hpp` / `src/input.cpp` —
  `run_game_thread_sink_now()` called once per tick from the slot-5 detour, before the
  raw-angle writes; `apply()` no longer runs the sink (so it is not doubled).
- `payload/control/.../occlusion_query.{hpp,cpp}` — build the mesh BVH per cluster at
  parse time; `service.cpp` passes it through; the world-hull enumeration uses
  `aim::AabbBvh`.
- `payload/aim/include/cs2/aim/bvh.hpp` / `src/bvh.cpp` + `tests/test_aim_bvh.cpp` — the
  generic AABB BVH and its exactness test (`aim-bvh`).
- `payload/aim/include/cs2/aim/solver.hpp` / `src/solver.cpp` — `SolveStats` (optional
  per-phase timings/counts).
- `payload/aim/tests/test_aim_field.cpp` — exact-vs-closed-form and optimality tests.
- `payload/control/include/cs2/control/service.hpp` / `src/service.cpp` — `RagePerf`
  stage timers, the `[rage-perf]` log line, the `rage` → `perf` JSON, and the backtrack
  pose cap (§3.1).

Host tests: **16/16** (`meson test -C build`).

> **Rejected:** the "one union field over all poses" rewrite of `solve_backtrack` (see
> §3.1) — it is slower because the objective search is O(cells²) in the merged field.
> Recorded here so it is not retried.

## 5. Community sources (CS2 lag-comp / ragebot)

- UC *Aimbot misses* — velocity-driven misses; "Extrapolate players, set input history
  angles and player tick count, update bones before getting their positions"
  (Exlodium); "if I just try to hit the front record with few prediction on bones
  accounting velocity etc all seems to work good" (poohshiesty).
  <https://www.unknowncheats.me/forum/counter-strike-2-a/632113-aimbot-misses.html>
- UC *Why is CreateMove tied to FPS?* — "In CS2 createmove is called at an interval of
  FPS / 64 … subtick"; rebuild the run command / add sub-tick move steps to get a
  consistent rate (Exlodium).
  <https://www.unknowncheats.me/forum/counter-strike-2-a/653035-createmove-tied-fps.html>
- UC *lagcomp* — store records on FSN stage 7 and only when `m_flSimulationTime`
  changed; don't store at the start of CreateMove (tied to your fps, not the server's
  update rate); **subtract the lerp** in the validity check and force a bone setup at
  the exact `m_flSimulationTime` (shakro).
  <https://www.unknowncheats.me/forum/counter-strike-2-a/751190-lagcomp.html>
- UC *Understanding CS2 lagcomp/backtrack* (634175), *Lag compensation hitbox analysis
  in CS2* (716979) — backtracks that "move too fast" miss without extrapolation.

## 6. Recommended next steps (ranked)

1. **Measure first:** enable the ragebot and read `rage.perf` / `[rage-perf]`. The
   stage split tells you the lever — `backtrack_us` (→ lower `k_max_backtrack_poses`),
   `solve_us` with big `cells` (→ lower `rage grid=N`), `project_us`/`occluders` (→
   §3.7), `query_us` (→ the BVH sweep). Record before/after each change.
2. **3.3 cadence:** solve per frame (or in the prediction/CreateMove path) so the aim
   is not ~1 tick stale — the biggest *accuracy* lever, once the cost is down.
3. **3.4/3.5 lag comp:** bone setup at the record's `m_flSimulationTime` + lerp-aware
   validity (needs engine RE).
4. **3.2 objective search:** done — the exact analytic cone probability replaced the
   sampled grid (§3.2). Re-measure; `solve_us` should now be small and scale with
   `cells`, not `cells²`.
5. **3.6 inaccuracy units** (correctness; gates the fire and the aimpoint).
6. **3.7 occlusion:** nearest-first split cap + the BVH sweep enumeration.
