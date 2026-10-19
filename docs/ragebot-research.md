# Ragebot research brief — what to reuse instead of reinventing

> Written 2026-10-19 after the perf session. The ragebot works but a dense position can
> still cost tens of milliseconds per tick. This file is the *research phase* brief: what
> the industry does, which libraries exist, and which of our components should be
> replaced. Read this before writing more geometry code.
>
> **Deep research:** three companion briefs in [`research/`](research/) — read
> [`research/README.md`](research/README.md) for the index and the ranked findings. They
> contain measured benchmarks, primary-source citations, and **three concrete bugs found
> in our current code** (one of which is a gameplay-visible hitchance error).

## 1. Where the cost actually is (measured, not guessed)

Live, at a spot where the eye→enemy cones cross a lot of geometry:

| stage | cost | what it is |
|---|---|---|
| `solve_occlude` | **6–70 ms** | `occlude_polygon`: split each hitbox silhouette by every occluder |
| `argmax` | ~5–11 ms | the cone/hitchance objective (Gaussian mass over the cells) |
| `project` | 0.6–0.9 ms | projecting cached solids into the chart (fixed this session) |
| `query` | ~0.1–0.5 ms | the world-hull BVH enumeration (fixed this session) |

The dominant term is the **fragment arrangement**: `occlude_polygon` builds the full
2-D arrangement of `hitbox silhouette − occluder silhouettes`, one convex cell per region.
The number of regions grows with the number of solids in front, so cost is
`O(hitboxes × solids × fragments)`. At the worst spot that was 57 hitboxes × 312 solids →
**15,670 cells in ~0.6 s**.

That is not a constant-factor problem. It is the wrong algorithm for the question.

## 2. What the industry actually does

Every public implementation of "autowall" uses **repeated ray traces**, not projected
arrangements. The canonical loop (Source engine, CS:GO, CS2):

1. Trace the eye→target ray with the bullet mask.
2. If it hits a player, done. If it hits the world, read the surface's penetration
   modifiers, apply the per-layer damage loss, decrement the penetration budget
   (4), and **continue the trace from the exit point**.
3. Repeat until the budget is exhausted or the trace reaches the target.

References:

- **`iCollin/csgo-internal` — `hacks/autowall.cpp`**: the classic implementation
  (`SimulateFireBullet` / `HandleBulletPenetration`): `TraceRay`, `surfacedata_t`, the
  4-penetration loop. It is ~200 lines and does no geometry of its own — the engine's
  tracer does the work.
  <https://github.com/iCollin/csgo-internal/blob/master/hacks/autowall.cpp>
- **`Read1dno/ExternalAutoWallCS2`**: a CS2 *external* autowall. It extracts collision
  geometry from the `.vpk`, **builds a BVH**, ray-traces it, and applies the real material
  modifiers. This is the same idea without engine calls — and it is the closest analogue
  to what we could do payload-side.
  <https://github.com/Read1dno/ExternalAutoWallCS2>
- **Osiris** (the best-known open-source cheat): **MIT licensed**, and `master` contains
  **no aimbot, autowall or trace code at all** — it is visuals/HUD only. Its aimbot exists
  only on the `csgo-end-of-life` tag, and that one is a bone scan + visibility check with
  no probability model. Do not cite it as the CS2 combat reference.
  <https://github.com/danielkrupinski/Osiris> (verified: MIT `LICENSE`, and
  `Source/Features/` on `master` has no `Aimbot`.)
- **The CS2 lineage is `CreateTrace`, not a trace loop.** In CS2 the engine call returns
  the **entire sorted surface stack** for a ray in one go (`TraceData_t::mod_array`), and
  `HandleBulletPenetration` is pure arithmetic per stack entry. So a correct CS2 autowall
  is *one engine call + ≤5 arithmetic steps* — the CS:GO `TraceRay`/`TraceToExit` loop is
  the wrong shape. The public CS2 releases that do this are the UC "EngineTrace /
  TraceShape" line (UC 747109 → 608159 → 764290).
- UC: *Full Penetration / Autowall / Damage calculation* (CS2 structs + code).
  <https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html>

**Conclusion:** the arrangement/field is our own invention — **no public implementation
builds a projected damage field.** It is elegant (it answers "what is the best aimpoint"
rather than "can I hit this point"), but it is the reason we are slow. The industry trades
the global optimum for a bounded number of traces. See
[`research/autowall-implementations.md`](research/autowall-implementations.md) for the
full survey, and note the licences there before reading any code.

## 3. We are closer to the industry design than it looks

We already have the hard parts of a payload-side ray caster:

- `OccluderCell::ray_span(eye, dir)` — the exact ray/solid interval for hulls
  (half-space clipping) and meshes (triangle BVH). That *is* a ray caster.
- The cached world hulls carry vertices, planes, **triangles and a `MeshBvh`**.
- `EntityWithEntry`/`entry` distances already give us the nearest-first ordering.
- `penetration_after_walls()` (engine model) already walks a wall stack with the
  4-wall budget, the ~90-unit thickness cap and the `<1` damage stop.

So a ray-based rewrite is mostly *deleting* code: evaluate a candidate set of aim
directions, trace each, and pick the best — instead of building the arrangement.

## 4. Libraries worth adopting

| Need | Library | Why / caveat |
|---|---|---|
| 2-D polygon booleans (if we keep any field/heatmap) | **Clipper2** (`AngusJohnson/Clipper2`) | Robust integer-based Vatti clipping, actively maintained, used widely. Our `clip_convex`/`subtract_convex` are correct for convex fragments but hand-rolled; Clipper2 handles degenerate input and non-convex sets. |
| BVH build + traversal | **tinybvh** (`jbikker/tinybvh`) | Header-only, no deps, SAH build; author's benchmarks put it ahead of Embree for single-ray builds/traversal. Drop-in for our `AabbBvh`/`MeshBvh` if we start tracing a lot. |
| BVH (heavy) | **Embree** | AVX2/AVX-512, multi-threaded build, production-grade — but a big dependency and overkill for a payload that is already inside the game process. |
| Exact predicates / robust geometry | **CGAL**, **Boost.Geometry** | Correctness for degenerate cases. Heavy; only worth it if we hit robustness bugs we cannot reason about. |
| Triangulation (only if we rasterise) | **earcut**, **poly2tri** | Not needed for the current design. |
| Software visibility | **Intel Masked Occlusion Culling** (`GameTechDev/MaskedOcclusionCulling`) | A *different formulation* of the same problem: rasterise occluders into a masked hierarchical depth buffer and query visibility. 3× faster than prior Hi-Z work in the paper. Interesting if we ever move the visibility query onto a low-res depth buffer. |

Papers / background:

- *Computation of the Bivariate Normal Distribution Over Convex Polygons* (Di Donato,
  Jarnagin, Hageman, NSWC/DL TR-3866) — this is exactly the integral our
  `polygon_normal_mass` computes via Green's theorem. Confirms the method and offers
  faster CDF approximations.
  <https://apps.dtic.mil/sti/tr/pdf/ADA102466.pdf>
- *Masked Software Occlusion Culling* (HPG 2016) — the masked Hi-Z variant.
  <https://www.highperformancegraphics.org/wp-content/uploads/2016/HPG2016_MSOC.pdf>
- *Hierarchical-Z map based occlusion culling* (RasterGrid) — the classic Hi-Z writeup.
  <https://www.rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling>
- Drezner, *Computation of the bivariate normal integral* — fast bivariate CDF.

## 5. Recommended direction (in order)

### 5.1 Replace the arrangement with per-candidate ray traces (the real fix)

Instead of `hitbox silhouette − all solids`, evaluate a bounded candidate set:

1. Build the candidate directions from the hitboxes: each hitbox's Chebyshev centre,
   centroid, vertices, and the head/body axis points — the same finite set
   `field_candidates` already produces.
2. For each candidate, trace eye→point through the cached world hulls (our `ray_span`
   already answers "where is this solid on the ray"; a BVH over the hulls makes it
   `O(log n + k)`), sort the crossings nearest-first, and run
   `penetration_after_walls`.
3. Score = damage × hitchance (the cone probability can still use the analytic mass at
   that point, or a cheaper approximation) and pick the best.

Cost becomes `O(candidates × crossings)` with **no fragment explosion**, and it is the
same model the engine validates against. The field/heatmap can keep the arrangement for
rendering (it is a debug view and runs off the hot path).

### 5.2 Or call the engine's own tracer

`CGamePhysicsQueryInterface::TraceShape` / `TraceShapeWithSortedResults` is already
RE'd with its call convention pinned (`docs/re/aim.md`), and `EntitiesAlongLinearSweep`
is the remaining unknown (`Ray_t` descriptor). Using the engine's tracer makes the
occlusion *exactly* what the bullet sees — no re-derivation of materials, no
mistakes about what counts as solid. This is what every public cheat does.

### 5.3 Keep the math, drop the cost

- The bivariate-normal boundary integral is the right objective; it matches the DTIC
  report. If the cone objective stays hot, precompute per-cell edge data and use
  Drezner's CDF rather than per-edge Gauss-Legendre quadrature.
- If any polygon booleans remain, move to **Clipper2** instead of maintaining our own.
- If BVH work grows, move to **tinybvh** (SAH build is also better than our median split).

### 5.4 Measure against a known-good baseline

The external autowall above is a useful reference for *what* to compute. For *how fast*,
the target is one 4-penetration trace per candidate: a trace is microseconds, so a
50-candidate solve should be well under a millisecond.

## 6. What not to do

- **Do not** cap occluders or fragments to make the arrangement fast — that is what we did
  as a stopgap (`PenetrationParams::max_occluders = 24`) and it can under-report a
  wallbang. It should be removed once 5.1/5.2 lands.
- **Do not** project mesh triangle soups: the silhouette is the convex hull of the
  projected points, so the AABB corners are sufficient (already fixed).
- **Do not** trust a "before/after" across a game restart: the bots respawn elsewhere, so
  the cell count changes and dominates the timing. Use the host benchmark over a saved
  cell set, or a scenario that pins the bots.
