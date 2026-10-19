# Next session — ragebot performance (occlusion query volume + frame-stage logic)

> Written at the end of the 2026-10-19 session. **Start here.**
> Branch `feat/occlusion-penetration`, tip `77f2548` + a large **uncommitted** tree
> (see "Uncommitted state"). The in-process Present renderer (workstream A from
> `docs/next-session-present-hook.md`) landed and is validated; this session then
> instrumented and began optimising the ragebot. The remaining work is the perf and
> correctness items below.
>
> **Update (later 2026-10-19):** the work described here is committed (`5dd49fb`,
> `9296a42`). Two of the items below landed (§2.0 objective search, §2.1 pyramid
> enumeration), and the session's measurements invalidated this file's original premise
> — the objective search, not the world projection, was the bottleneck. The remaining
> cost is `occlude_polygon`'s fragment arrangement; **read
> [`docs/ragebot-research.md`](ragebot-research.md) before writing more geometry code** —
> the industry uses repeated ray traces, not a projected arrangement, and the brief lists
> the libraries worth adopting (Clipper2, tinybvh, the engine's own `TraceShape`).

## 0. What landed this session

- **In-process Present renderer (ADR-0023).** The ESP + HUD + menu render from the
  game's own `IDXGI SwapChain::Present` (DXGI method-table interposer) with the
  payload's ImGui D3D11 backend; the Vulkan layer is now a fallback. New
  `features::Menu` (payload-side menu, WndProc subclass, cursor release, SDL3 raw-input
  block while open). `docs/decisions.md` ADR-0023; `scripts/launch-cs2.cmd` defaults to
  `CS2_PAYLOAD_RENDER=present` (no `-vulkan`). **`docs/handoff.md` is stale on this
  point** — it still describes the Vulkan layer as the default.
- **Ragebot instrumentation** (`docs/ragebot-perf.md`): `SolveStats` in the aim core,
  `Service::RagePerf` in the payload, `[rage-perf]` log line + `rage` → `perf` JSON.
- **Exact objective** (no sampling): `hit_probability`/`expected` are the exact Gaussian
  mass over the convex cells (boundary integral), and `argmax_hit_probability` /
  `argmax_expected` use **gradient ascent with the analytic boundary-flux gradient**.
- **Correct occlusion** (no caps): `occlude_polygon` drops a fragment only when its
  *exact* value is already 0 (monotone); a **mesh triangle BVH** (`MeshBvh`) and a
  **world-hull AABB BVH** (`aim::AabbBvh`) accelerate `ray_span` and the enumeration.
- **Solve moved to the CreateMove hook** (`Service::rage_tick` from
  `on_game_thread`), once per tick, before the raw-angle writes.
- **Backtrack** selects the pose at the **rewind tick** (`aim::rewind_tick`), ±3 ticks.

Host tests **17/17** (`meson test -C build`, incl. new `aim-bvh`); `payload.dll` builds;
preflight clean.

## 1. Live perf (de_dust2 bot match, in-process renderer)

`[rage-perf]` / `rage.perf` (averages per rage-tick unless noted):

| field | value | note |
|---|---|---|
| `total_us` | **11.8 ms** (max 28.6 ms) | whole solve path, on the **game thread** |
| `solve_us` | **3.0 ms** | was 5.5 ms with the sampled objective |
| `project_us` | **8.5 ms** | the world-solid projection — **now the bottleneck** |
| `occluders` | **834** (was 64 at another spot) | wildly position-dependent |
| `backtrack_us` | ~0.6 ms | cheap now (pose selection + pruning) |
| `query_us` | ~8 µs | the world AABB BVH works |
| `frames` | ~75/s | the CreateMove hook calls the sink once per tick |

**Conclusion (corrected 2026-10-19, later session):** this table was misread. `total_us`
is the whole rage path and `solve_us`/`project_us` are *nested* inside it, so they are
not additive — and the `argmax` figure below the table is the real dominant term. A
fresh measurement on the same build (de_dust2, objective 0/damage, ~670 cells) gives:

| stage | per rage-tick |
|---|---|
| `total_us` | **~61 ms** |
| `solve_argmax_us` | **~43 ms** ← the objective search, the real bottleneck |
| `backtrack_us` | ~24 ms (nested: it runs `solve` per pose) |
| `project_us` | ~4.6 ms (the world-solid projection) |
| `solve_occlude_us` | ~3.4 ms |
| `query_us` | ~0.3 ms |

So the **objective search**, not the world projection, was the cost: `argmax_damage`
probed ~7400 candidate points and, for each, scanned all ~700 cells twice
(`value_at` + `field_margin`), i.e. O(cells²) — measured at **102 ms** on 717 real cells
in a host benchmark. See §2.0 (fixed) below. The cone query (§2.1) is still worth doing,
but it is no longer the biggest lever.

## 2. Next work (priority order)

### 2.0 Objective-search cost — **fixed this session** (was the real bottleneck)

Three exact changes in `payload/aim` (host benchmark on the live 717-cell field):

| change | before | after |
|---|---|---|
| `DamageField` uniform point grid (`cells_near`) | 102 ms | 24 ms |
| `field_candidates` only from value-eligible cells | (included) | 0.15 ms |
| `chebyshev` allocation-free clipping + tolerance | (included) | (included) |
| cone-objective Gaussian tail cutoff (`k_mass_cutoff`) | 514 ms | 34 ms |

- **Point grid** (`DamageField::PointGrid`): a uniform grid over the cells' AABBs,
  built lazily and invalidated by `add`/`clip`. `value_at`/`base_value_at`/`field_margin`
  probe only the bucket containing the point, so a probe is O(cells in bucket) instead of
  O(cells). It is a pure acceleration: a new `aim-field` test asserts the grid returns
  **exactly** the linear-scan value over a 17k-point sweep, including overlapping cells
  and a cell large enough to be kept in the `large` list.
- **Candidate restriction** (`field_candidates(cells, value_floor)`): the damage at a
  point is the max over its containing cells, so the optimal value is `max(cell.value)`
  and any point attaining it must lie in a cell with that value. `argmax_damage` seeds
  from `best_cell()->value`, `argmax_hitchance` from `0.5 × best` (its existing
  comparable-damage rule). Candidates that provably cannot reach the floor are skipped —
  no geometry is dropped.
- **`chebyshev` cost**: the 48-step bisection re-clipped the polygon every step through
  an allocating `clip_half_plane`, which was ~24 ms of the 102. It now clips into reused
  buffers (`clip_half_plane_into`) and stops at `1e-5` chart radians (far below the
  scale at which the choice of cell can change). Same result, ~2 ms.
- **Cone tail cutoff** (`k_mass_cutoff = 8`): a cell whose AABB lies entirely outside
  `±8σ` of the aim has mass at most the Gaussian tail `Φ(-8) = 6.2e-16`, so skipping it
  changes the objective by < `cells · 6.2e-16`. Every cell inside the box is still
  integrated exactly. This is a proven bound (not a heuristic), and it makes
  `hit_probability` ~0.01 ms instead of 5–50 ms.

**Still open:** the cone objective's remaining ~20–35 ms is `mass_ascent`'s 32
iterations × up to 12 line-search evaluations. The next exact step is to seed the ascent
from the *damage* optimum and stop once the step size falls below the aim's resolution
(the ascent is a maximization of a smooth objective, so a converged step is provably
optimal to that precision) — or to cache the scaled polygons across a line search.

### 2.1 Cone/frustum query over the `AabbBvh` — **done** (2026-10-19)

Implemented as the **union of the eye→enemy pyramids**, not one per hitbox:
`AabbBvh::query_pyramid(eye, enemy_aabb)` keeps every solid whose AABB may meet a segment
from the eye into that enemy. One base per enemy is deliberate — a per-hitbox list
overflows any fixed target array (19 hitboxes × 32 lag records per enemy) and silently
prunes the enemies it never reaches; that was a real live bug. The base unions the
enemy's current snapshot bounds with all its sampled lag poses (a respawned/teleported
pawn can carry a stale record ring).

Live: **1792 → 14** world hulls at one spot, with the *used* occluder silhouettes
identical between the two volumes (13 vs 13, zero difference) — same cells, same
attenuation. See §3.7 "Fixed" for the two bugs this exposed and the diagnostics
(`heatmap enum=box|both`, `scripts/perf-live.py compare`).

The engine's own swept query (`EntitiesAlongLinearSweep`) remains an alternative but is
**blocked on pinning its `Ray_t` descriptor** (`docs/re/aim.md` "The call convention");
the payload-side pyramid avoids that RE entirely.

- Data structure is ready: `aim::AabbBvh` (`payload/aim/include/cs2/aim/bvh.hpp`) is
  built once over the cached world hulls (`occlusion_query.cpp`,
  `g_world_bvh`) and supports a box query; add a **frustum/cone query** (hitbox-AABB
  vs node-AABB SAT against the pyramid planes) and return hull indices.
- The consumer is `Service::cache_occlusion_targets` / the projection loop in
  `rage_tick` — it already filters by `hull.has_bounds`; feed it the tighter set.
- The engine's own swept query (`EntitiesAlongLinearSweep`) remains an alternative but
  is **blocked on pinning its `Ray_t` descriptor** (`docs/re/aim.md` "The call
  convention"); the payload-side cone avoids that RE.

### 2.2 Depth-sorted occlusion with a correct "fully impenetrable" stop

**Idea (from the operator):** sort the world solids by depth and **stop projecting once
no further solid can change any result** — i.e. once every relevant direction is
already fully impenetrable (the engine's own budgets: **4 penetrations**, ≈90-unit
crossed-thickness cap, the `<1` damage stop, the `<0.1` distance-modifier gate).

Design notes / requirements:
- The projection is **shared across all hitboxes**, so the stop test must be "no
  remaining solid can affect *any* hitbox cell". Conservative per-hitbox bound: once the
  accumulated thickness / wall count along the eye→hitbox ray to its **nearest** point
  already exhausts the budget, no farther solid can add damage. Sum over the targets.
- Must remain **exact to the model** (the user's rule: account for everything, then
  optimise — do not drop a solid that could still attenuate). The stopping bound must
  therefore be a *proven* budget exhaustion for every candidate direction, not a
  heuristic count.
- Practical shape: (a) sort the BVH query results by `lower_bound_distance` from the
  eye; (b) walk them nearest-first, projecting, and after each step evaluate whether the
  best achievable value at every hitbox is already 0 given the accumulated wall budget;
  (c) stop. Because the value only decreases as more walls are added, this is exact.
- This composes with 2.1 (the cone already cuts the count; 2.2 bounds the tail).

### 2.3 Move the remaining game logic out of the renderer

The operator's point: **all game logic should run in the frame stage that owns it**
(`CreateMove`, `FrameStageNotify`, …), following the game's own update order — not in
`Overlay::update` on the render thread.

Still on the render thread (`Service::on_snapshot`, called from `Overlay::update`):
- `sample_records(snapshot)` — reads every enemy's hitboxes per frame (memory, but game
  logic; it should run at the same stage as the solve, or on `FrameStageNotify`).
- `cache_occlusion_targets(snapshot)` — builds the enumeration box (game logic).
- The **state read** itself (`StateReader::snapshot`) is done by `Overlay` on the render
  thread and handed over; the ESP build is legitimately render-side, but the *state* and
  *records* want a frame-stage hook.

Suggested: hook the client frame stage (`FrameStageNotify` / the client's `FSN`) or use
the existing CreateMove hook to sample state + records + occlusion **before** the
solve, so the whole rage path is one coherent per-tick game-thread stage. Keep only the
ESP/model build on the render thread (it needs the render matrices).

### 2.4 Lag-comp accuracy (correctness, needs RE)

- The hitboxes are read from the **interpolated** live bone array; the server validates
  against the **rewound** pose at `m_flSimulationTime`. Force a bone setup at the
  record's exact `m_flSimulationTime` (UC: "update bones before getting their position";
  "force a bone setup at the exact m_flSimulationTime"). This needs RE of the client's
  bone-setup path.
- Record validity must subtract the **lerp** (`cl_interp`, `cl_interp_ratio`) — verify
  `rewind_tick`/`RecordStore::trim` use the player's live values.

### 2.5 Inaccuracy units (correctness)

`m_flInterpolatedInaccuracy` is used as radians without a live cross-check
(`docs/ragebot-review.md` §2.4). Reverse `FUN_18080b880` on the current build and
cross-check against `CMsgTEFireBullets`.

### 2.6 Ragebot follow-ups still open (from `docs/next-session-present-hook.md`)

- Auto-stop oscillation (command view-yaw frame + hysteresis).
- Recoil/spray: verify what the shot inherits; per-weapon pattern table.
- Menu cursor release (needs operator confirmation; the raw-input block is in).

## 3. Instrumentation reference

- **Read it live:**
  ```python
  # connect to the control pipe and print `rage` (includes perf)
  ```
  `scripts/run-scenario.py rage --no-launch --no-inject --keep-running` (or the
  `rage` command via `scenario.control.Control`). Enable the solve with
  `rage on=true force=true fire=false` (force bypasses the key); disable with
  `rage on=false force=false`.
- **`[rage-perf]` log line** (every 256 hook frames) in
  `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`.
- Fields: `total/last/max` (whole rage path), `solve` (with `proj`/`occ`/`argmax`
  breakdown), `back`, `build`, `proj` (occluder projection), `query`/`sample`
  (per snapshot), and counts `cand`/`hb`/`occ`/`poses`/`cells`.
- Note: `sample_us`/`query_us` are per-`on_snapshot`; the others per hook-tick. The
  reported averages now use separate denominators.

## 4. Uncommitted state

Everything since `77f2548` is **uncommitted** (modified + new files — `git status`).
Notable new files: `payload/aim/{bvh.hpp,bvh.cpp}`, `payload/features/{menu.*,settings.*}`,
`docs/ragebot-perf.md`, `payload/aim/tests/test_aim_bvh.cpp`, `scripts/perf-live.py`.
**Commit before continuing** (suggest splitting: renderer/menu; instrumentation; exact
objective; BVHs + occlusion; solve-in-hook).

### 4.1 Session log (2026-10-19, later)

Started from this doc and found its premise stale: live profiling showed the **objective
search**, not the world projection, was the cost. Landed, all host-tested (17/17):

- **Objective search** (`docs/ragebot-perf.md` §3.9): a uniform point grid in
  `DamageField`, value-eligible candidate filtering, an allocation-free Chebyshev
  bisection, and a proven `8σ` Gaussian-tail cutoff. Host benchmark over a live
  717-cell field: `argmax_damage` **102 ms → 0.15 ms**, `argmax_hitchance` 105 → 1.1 ms,
  `hit_probability` 28 → 0.01 ms, `argmax_hit_probability` 514 → 34 ms.
- **Occlusion enumeration** (§2.1): per-enemy pyramid union; 1792 → 14 world hulls live,
  with identical used occluders.
- **New scripts:** `scripts/perf-live.py` (`spot` / `save-spot` / `restore` / `place` /
  `baseline` / `enum` / `occlusion` / `compare`).

**Measurement caveat recorded the hard way:** a live "before/after" is not comparable
across a game restart — the bots respawn elsewhere, so the cell count changes and
dominates the timings. The controlled numbers are the host benchmark over a saved cell
set (`artifacts/live-heatmap.json`); live runs are for correctness, not for timing.

**Still open:** the cone objective's remaining ~20–35 ms is `mass_ascent`'s 32 iterations
× up to 12 line-search evaluations (§2.0 "Still open"); the rest of §2.2–§2.6 is
untouched.

## 5. Build / run (unchanged mechanics)

```powershell
meson compile -C build                 # host
meson test -C build                    # 17/17 expected
meson compile -C build-payload         # payload.dll
python scripts/run-scenario.py bot-match --keep-running --set map_name=de_dust2 `
    --set per_team=4 --set bot_stop=false
# reattach: add --no-launch --no-inject --keep-running
```

Gotchas (unchanged unless noted):
- Never inject twice.
- The **game restarted fresh** this session; `CS2Launch` now defaults to the
  in-process D3D11 path (no `-vulkan`). The `menu-open` marker is removed.
- `build-overlay` can only relink when CS2 is stopped; not needed for the default path.
- The watchdog emits `[freeze]` reports during level load (heartbeats=1/2/…) — benign.
- REA/Ghidra: `docs/re/README.md`.

## 6. Reference docs

`docs/ragebot-perf.md` (this work, incl. the rejected union-field idea §3.1),
`docs/ragebot-review.md`, `docs/ragebot.md` §0, `docs/re/{aim,lagcomp,occlusion,
penetration,bullet-trace,vphysics2}.md`, `docs/decisions.md` (ADR-0015/0016/0022/0023).
