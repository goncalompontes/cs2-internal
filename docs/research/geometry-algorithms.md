# Geometry, visibility and spatial-index research

> Written 2026-10-19. Extends [`docs/ragebot-research.md`](../ragebot-research.md).
> The question is narrow: **`occlude_polygon` costs 6–70 ms/tick** because it builds a
> 2-D arrangement of `hitbox silhouette − every occluder silhouette`. Which libraries,
> algorithms and papers would the state of the art use instead, and which of them can
> survive a manually mapped DLL?
>
> Every number below is tagged **[measured]** (on this machine, this session, and the
> scene is described), **[reported]** (from the cited source) or **[unverified]**
> (engineering estimate; no source).

---

## 0. TL;DR — the decision table

| Need | Recommendation | Why | Integration cost | License | Risk |
|---|---|---|---|---|---|
| **`occlude_polygon` (the hot path)** | **Stop building the arrangement.** Score a finite candidate set with `ray_span` + `penetration_after_walls` | [measured] `ray_span` on a 6-plane hull is **51.5 ns**; a whole candidate solve is ~2–5 ms vs 600 ms | 2–3 days, mostly deleting code | own code | thin visible slivers can be missed; fix with a sample grid of spacing ≤ σ |
| Same, as a stopgap | **Per-fragment AABB re-prune** | [measured] **10–14×** faster on the same scene; drops spurious cells, changes no model | ~1 hour | own code | none (pure acceleration) |
| 2-D convex difference primitive | **Keep `subtract_convex`** | [measured] **0.55 µs**/op vs Clipper2 **3.26 µs** (6× faster) | 0 | own code | sliver degeneracies (bounded, see §7) |
| General / robust 2-D booleans **off** the hot path | **Clipper2** (`Point64` API, `-fno-exceptions`) | BSL-1.0, no deps, widely used, integer-robust | 1–2 days | BSL-1.0 | ±1 integer unit minimum feature; silently no-ops on error with exceptions off |
| Exact arrangement for offline proof / debugging | **CGAL** `Polygon_set_2` / `Arrangement_2` with EPECK | the only guaranteed-robust option; it *is* the arrangement reference | weeks + GMP/MPFR, multi-MB | **GPL/LGPL** | not shippable in a closed payload; exceptions + RTTI + size |
| Header-only booleans | **Boost.Geometry** | BSL-1.0, header-only, OGC semantics | days | BSL-1.0 | heavy templates, compile time, long history of overlay bugs |
| Ray/triangle BVH | **Keep `MeshBvh`**; switch to **tinybvh** only if mesh tracing becomes hot | MIT, header-only, SAH, fastest reported build+single-ray traversal | 1–2 days | MIT | built-in `std::thread` pool must be disabled for the mapper |
| Static AABB broad phase | **Keep `AabbBvh`** | [measured] 2.6 ms build / 0.57 µs query at 10k boxes | 0 | own code | none at our scale |
| Visibility queries ("is it visible / how many walls") | **Per-candidate rays** now; **layered angular depth buffer** as the research option | rays are 51 ns each; a raster is O(pixels), not O(arrangement) | rays: days; raster: 1–2 weeks | own code (MSOC is Apache-2.0 if used) | raster is approximate; "holes"/leakage are inherent |
| Spherical indexing / seam-free directions | **Do not rewrite.** Keep the chart; only S2 if a *global* direction index is ever needed | seam already solved by unwrapping; `1/cos` is the correct chart metric | — | S2 Apache-2.0 | S2 is huge and spherical edges change all the math |
| Exact predicates | **Shewchuk `orient2d`** only if a crack/mass bug appears | [measured] 4–14 ns; public domain | hours | public domain | none |
| Mesh booleans (Manifold, trueform) | **Not applicable** | 3-D solids, not 2-D chart regions | — | — | — |

**The single sentence:** our clipping primitive is already better than the libraries;
the *algorithm* around it is wrong. The fix is to stop enumerating regions and start
evaluating the objective at a bounded set of directions.

---

## 1. How the numbers were produced

All **[measured]** figures: AMD Ryzen 5 3600, Windows, GCC 14.2 (MinGW-w64), `-O2
-DNDEBUG -std=c++20` (the payload's `debugoptimized` level), single-threaded. The code
compiled is the project's own `payload/aim/src/{geometry,occlusion,bvh}.cpp` plus
Clipper2 `main` (cloned 2026-10-19, `CPP/Clipper2Lib`). The benchmark harness lives in
`%TEMP%\opencode\geom-bench` and is deliberately **not** committed.

Scene: one convex "hitbox" silhouette (7 vertices after hulling 16 random points in a
disc of 0.012 rad) and *N* convex "occluders" (hulls of 8 random points, 0.003–0.013 rad
radius, Gaussian-scattered around the silhouette). This is denser than the live average
(14 candidate solids per hitbox after the eye→enemy pyramid) and is meant to expose the
*scaling*, not to reproduce the 0.6 s figure.

Reproduce with the command line in §12. Numbers move ±10% run to run; the ratios are
stable.

---

## 2. Where `occlude_polygon` actually spends its time

The brief says 57 hitboxes × 312 solids → 15,670 cells → ~0.6 s. That is consistent
with a **superlinear** fragment explosion, and the measurements show exactly where it
comes from.

### 2.1 The primitive is fine

| operation | time | throughput |
|---|---|---|
| `subtract_convex`, 8v × 8v | **0.55 µs** | 1.8 M pairs/s |
| `clip_half_plane`, 8v | **~0.1 µs** | 10 M/s |
| `ray_span`, 6-plane hull | **51.5 ns** | 19 M rays/s |
| `OccluderCell::lower_bound_distance` | sub-µs (plane dot products) | — |

`subtract_convex` is 6× faster than Clipper2 for the same convex-convex difference
(§3.2). **There is nothing to win by replacing it.**

### 2.2 The arrangement is superlinear

One silhouette, *N* occluders, all overlapping it:

| N | fragments | `subtract_convex` calls | time | time/cell |
|---:|---:|---:|---:|---:|
| 4 | 8 | 17 | 0.03 ms | 3.8 µs |
| 8 | 45 | 81 | 0.07 ms | 1.6 µs |
| 16 | 190 | 815 | 0.57 ms | 3.0 µs |
| 32 | 668 | 7,196 | 5.10 ms | 7.6 µs |
| 64 | **2,487** | **56,061** | **36.1 ms** | **14.5 µs** |

Fragments grow roughly as N² and clip calls as N²·log N; cost/cell grows because each
cell is a smaller polygon but the per-call constant (allocation, edge normals, the
`inward_edges(b)` rebuild) dominates. At the live worst spot this is the 0.6 s.

Note also the **fragment count is inflated**: a piece that does not overlap the next
occluder is still split by that occluder's *supporting lines* (`subtract_convex` clips
against every edge of `b` unconditionally). Those extra pieces carry the same covering
set, so the field is correct but the arrangement is larger than it needs to be.

### 2.3 The cheapest real fix: re-prune per fragment

`occlude_polygon` broad-phases once, against the *original* silhouette
(`polygon_bounds_overlap` in `damage.cpp:122`), then splits **every** fragment by
**every** survivor. Re-testing each fragment's own (shrinking) bbox against the
occluder before splitting:

| N | today | per-fragment bbox | speed-up |
|---:|---:|---:|---:|
| 16 | 0.57 ms / 190 fr | 0.07 ms / 86 fr | **8.1×** |
| 32 | 5.10 ms / 668 fr | 0.39 ms / 231 fr | **13.1×** |
| 64 | 36.1 ms / 2,487 fr | 2.57 ms / 665 fr | **14.0×** |

This is equivalent up to the centroid approximation — and arguably *more* correct: a
fragment that a clip polygon does not touch should stay one cell, not be split into
slivers that are then valued at several arbitrary centroids. It costs one AABB test per
fragment/occluder pair. It is worth doing today even if the arrangement is deleted next
week — it also makes the debug heatmap cheap.

### 2.4 What the alternative costs

At **51.5 ns per `ray_span`**, a full per-candidate solve is:

- 57 hitboxes × 40 candidate directions × 14 solids = **31,920 rays ≈ 1.6 ms**, worst case,
  with no BVH and no arrangement;
- the same with the full 312 solids and 40 candidates: 711k rays ≈ **37 ms** (with 64
  candidates, 1.14 M rays ≈ **59 ms**) — still an order of magnitude better than today;
- a 4-wall penetration walk is 4 `ray_span`s ≈ **206 ns** per candidate.

That is the whole argument. The rest of this document is the evidence and the
alternatives.

---

## 3. 2-D boolean operations and arrangements

**Short answer:** polygon *booleans* are a solved problem (Clipper2, CGAL, Boost). The
thing `occlude_polygon` needs is not a boolean: it is a **labelled arrangement** — every
cell must know which walls cover it, so it can be valued. No library exposes that.
Clipper2 returns geometry with no labels; CGAL's `Arrangement_2` exposes the DCEL but is
GPL and heavy; the labelled version is the loop we already wrote. That is why the answer
is "keep the primitive, delete the algorithm", not "adopt a library".

### 3.1 The candidates

| Library | Algorithm | Convex-convex | General polygons | Robustness model | License | Verdict for us |
|---|---|---|---|---|---|---|
| **ours** | Sutherland–Hodgman per edge | **0.55 µs** | convex only | float, epsilon | own | **keep** |
| **Clipper2** | Vatti (extended) sweep-line | 3.26 µs | yes, holes, self-intersections | int64 grid, ±1 unit | BSL-1.0 | best general option; **not** the hot path |
| **CGAL** `Polygon_set_2` | `Arrangement_2` sweep + DCEL | ~µs–ms [unverified] | yes | exact with EPECK/EPICK | **GPL/LGPL** | offline reference only |
| **Boost.Geometry** | overlay sweep (rescaling off since 1.79) | ~µs [unverified] | yes | float + optional rescale | BSL-1.0 | viable, heavy |
| **Martinez–Rueda** | sweep-line, `O((n+k) log n)` | — | yes | float; sensitive | varies by implementation | no maintained C++ |
| **Vatti 1992** | sweep-line | — | yes | original is float | paper | Clipper is the practical Vatti |
| **Greiner–Hormann** | entry/exit lists | — | yes | **fails on degeneracies** | paper | no |
| **i_overlay** (Rust, 2023–) | integer overlay | [reported] very fast | yes | integer | MIT | Rust: not viable |
| **Skia pathops / wagyu** | sweep + snap rounding | fast | yes | snap rounding | BSD-3 / MIT | pulling Skia in is absurd here |

### 3.2 Measured: ours vs Clipper2

Same 8-vertex convex pair, same compiler and flags:

| operation | ours | Clipper2 | ratio |
|---|---:|---:|---:|
| difference, 8v × 8v | **0.55 µs** | **3.26 µs** | **6.0×** |
| silhouette (7v) minus 8 clips, one call | — | 39 µs | — |
| … minus 16 clips | — | 102 µs | — |
| … minus 32 clips | — | 351 µs | — |
| … minus 64 clips | — | 1,468 µs | — |
| … minus 200 clips (separate scene, 13 overlapping) | — | 3,430 µs | — |
| our arrangement, 64 clips, per-fragment prune | 2,570 µs | — | — |

Normalised, Clipper2's one-shot costs **~23 µs per clip** at 64 clips and **~17 µs per
clip** at 200; a single `subtract_convex` call costs **0.5–1.5 µs** (it grows with the
fragment size). Our algorithm makes far more calls — that is the whole problem.

For a *single convex pair* the specialised clipper wins because it is O(n·m) with n,m ≤ 8
and no sweep-line setup, event queue, or path allocation — and ours returns the
intersection and the difference pieces in the same call. The asymptotically optimal
convex-convex intersection is linear (O'Rourke et al., *A New Linear Algorithm for
Intersecting Convex Polygons*, CGIP 19(4):384–391, 1982,
<https://www.cs.jhu.edu/~misha/Spring16/ORourke82.pdf>), but at 8 vertices the constant
term dominates and O(n·m) is already optimal in practice.

For *many* clips Clipper2's one-shot difference is ~1.7× faster than our pruned
arrangement — **but it answers a different question**: it returns the *visible* region
(2–3 paths), not the labelled arrangement (665 cells, each with the set of walls covering
it). The field needs the labels, because `fragment_layers` ray-casts each cell to get
per-wall thickness. Recovering labels from Clipper2 means incremental difference calls
on a growing fragment set, which is the same algorithm we already have, with bigger
constants.

**Conclusion:** Clipper2 is the right library for "robust general 2-D booleans" and the
wrong library for `occlude_polygon`.

### 3.3 CGAL

CGAL's `Polygon_set_2` is a thin wrapper over `Arrangement_2`: a doubly-connected edge
list built by a sweep, with boolean ops as set operations on the arrangement
(<https://doc.cgal.org/latest/Boolean_set_operations_2/index.html>). It is the canonical
implementation of exactly what `occlude_polygon` hand-rolls, and it is *correct* in a way
ours is not: with EPECK (exact predicates **and** exact constructions) the result is
guaranteed robust. The manual is explicit that with inexact arithmetic "nearly degenerate
configurations may result in abnormal termination of the program or even incorrect
results" (<https://doc.cgal.org/latest/Boolean_set_operations_2/index.html>, "Traits
Selection").

Why it is not the answer here:

- **License.** The kernel is LGPL, most algorithms are GPL; the project would need a
  commercial license to ship a closed static payload
  (<https://www.cgal.org/license.html>).
- **Size and dependencies.** GMP/MPFR, multi-megabyte templates, exceptions and RTTI.
  Wrong shape for a manually mapped DLL (§10).
- **Speed.** A DCEL arrangement with exact constructions is not a 60 fps primitive. No
  credible benchmark puts it near Clipper2; the oft-quoted "400× slower" is an anecdote
  (<https://news.ycombinator.com/item?id=32722691>), not a measurement — treat it as
  directional only.

Use CGAL only as an **offline oracle**: feed it the same projected polygons and check
that our fragment areas sum to the silhouette area. That is a genuinely useful test, and
it never enters the payload.

### 3.4 Boost.Geometry

Header-only, BSL-1.0, OGC Simple Features semantics. Two facts matter:

- **Rescaling is off by default since Boost 1.79** ("Rescaling is turned off by default.
  This avoids many related bugs in set operations and buffer",
  <https://www.boost.org/doc/libs/latest/libs/geometry/doc/html/geometry/release_notes.html>).
  Older advice about integer rescaling no longer applies.
- The release notes are a long list of overlay bug fixes (`difference` wrong/empty,
  self-intersections in output, spikes). It is much better than it was, but it is not
  Clipper2's equal on degenerate input.

It is a reasonable fallback if a Boost dependency already exists (it does not here).
Compile time and template bloat are the real costs.

### 3.5 Martinez–Rueda, Vatti, Greiner–Hormann

- **Martinez–Rueda–Feito (2009)**, `O((n+k) log n)` sweep, handles holes, multi-contours
  and self-intersections (<https://cdn.jsdelivr.net/gh/mfogel/polygon-clipping/paper.pdf>,
  <https://doi.org/10.1016/j.cageo.2008.08.009>). The authors' C++ implementation is still downloadable
  but the page says **"implementation is no longer supported"**
  (<http://www4.ujaen.es/~fmartin/bool_op.html>). The maintained implementations are
  JavaScript (`w8r/martinez`, `polygon-clipping`); the JS benchmark there is ops/sec on
  map data, not comparable to C++ (<https://github.com/w8r/martinez>).
- **Vatti (1992)**, "A generic solution to polygon clipping", CACM 35(7):56–63,
  <https://doi.org/10.1145/129902.129906>. Clipper/Clipper2 is the practical, maintained descendant.
  There is no reason to implement Vatti again.
- **Greiner–Hormann (1998)**, ACM TOG 17(2):71–83,
  <https://www.inf.usi.ch/hormann/papers/Greiner.1998.ECO.pdf>. Simple and fast, but the
  original paper handles degeneracies by *perturbing vertices*, and the algorithm
  genuinely fails when a vertex lies on an edge or edges overlap
  (<https://en.wikipedia.org/wiki/Greiner%E2%80%93Hormann_clipping_algorithm>). The fix
  is Foster–Hormann–Popa, "Clipping simple polygons with degenerate intersections",
  Computers & Graphics: X 2:100007, 2019,
  <https://www.sciencedirect.com/science/article/pii/S259014861930007X>. Our inputs are
  *full* of degeneracies (axis-aligned brushes, coplanar faces, shared edges), so this is
  the wrong family unless the degenerate version is used.

### 3.6 2023–2026 libraries

- **i_overlay** (Rust, MIT): the strongest recent overlay engine, integer and float APIs,
  multi-threading, and the engine behind `geo`'s boolean ops
  (<https://github.com/iShape-Rust/iOverlay>). Not usable: a Rust `staticlib` in a
  manually mapped C++ DLL drags in Rust's allocator, panic/unwind runtime and TLS, and
  the FFI boundary would cost more than the algorithm saves.
- **`trueform` / `Manifold`**: 3-D mesh booleans. Irrelevant to a 2-D angular chart.
- **`cavalier_contours`**, **`libtess2`**: offsetting / triangulation. Not needed.
- We found no maintained C++ 2-D boolean library that has displaced Clipper2 since
  2022; the active work is in Rust (`i_overlay`) and in the JS ecosystem.

---

## 4. Ray/BVH libraries

### 4.1 Comparison

| Library | Build | Single-ray traversal | SAH | Deps | License | Manual-map fit |
|---|---|---|---|---|---|---|
| **ours** `AabbBvh` / `MeshBvh` | median split, O(n log n) | plain binary, no SIMD | no | none | own | **excellent** |
| **tinybvh** | Bonsai / binned SAH, AVX2, threaded | AVX2, `BVH4_CPU` ~3.5× the plain layout | yes | none (header-only) | **MIT** | good, **disable its thread pool** |
| **Embree 4** | binned SAH, multi-threaded | AVX2/AVX-512, packets, streams | yes | TBB optional, large | Apache-2.0 | **poor** (size, own thread pool, imports) |
| **madmann91/bvh** | sweep SAH, binned SAH, mini-tree | scalar/robust | yes | std only (C++20) | MIT | good, same thread-pool caveat |
| **nanort** | SAH, OpenMP optional | scalar, robust 4-ulp | yes | none (single header, C++03) | MIT | **excellent**, but old and slow |
| **PBRT v4** | SAH + parallel | — | yes | pbrt core | Apache-2.0 | not a library |

### 4.2 What is published

- tinybvh's author, preliminary and self-published: **builds a SAH BVH faster than
  Embree and madmann91, and traces individual primary and any-hit rays faster**
  (<https://github.com/jbikker/tinybvh>). Concretely: the binned SAH builder processes
  **~200k triangles in < 10 ms**, refits 200k in **< 1 ms**, a shadow (any-hit) ray is
  **~30% faster** than a closest-hit ray, `BVH4_CPU` gives **~3.5× more rays/s**, and
  spatial splits + post-optimize buy **up to 30%** traversal
  (<https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/>).
- Embree itself claims "integrating Embree can double the performance of your CPU
  renderer" (<https://www.embree.org/>) and is the reference for AVX-512/streamed
  traversal (Wald et al., *Embree: A Kernel Framework for Efficient CPU Ray Tracing*,
  ACM TOG 33(4), 2014, <https://doi.org/10.1145/2601097.2601199>).
- madmann91's own chart compares against Embree, nanort and Fast-BVH
  (<https://github.com/madmann91/bvh>); the v2 library adds a mini-tree builder, an
  insertion optimizer and a C API.
- PBRT's BVH chapter is the best free explanation of SAH build and traversal
  (<https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies>).

### 4.3 What is measured here

| workload | result |
|---|---|
| `AabbBvh::build`, 10,000 random AABBs | **2.63 ms** |
| `AabbBvh::query`, 300 small boxes | **0.57 µs** each |
| `AabbBvh::query_pyramid`, 300 eye→box pyramids | **9.67 µs** each |
| `OccluderCell::ray_span`, 6-plane hull | **51.5 ns** |

The pyramid query is ~17× a box query because each node runs a SAT against the hull.
At one pyramid per enemy (≤ 10/tick) it is ~0.1 ms — fine. If it ever shows up in a
profile, cache the per-enemy result for the tick.

### 4.4 SAH vs median split, leaf size

For 10k **static AABBs built once**, SAH buys ~10–30% traversal and costs a more complex
builder. Our median split is 2.6 ms for 10k; tinybvh's SAH would be well under 1 ms
(extrapolated from 200k triangles < 10 ms) [unverified]. Neither matters at 50–300
queries/tick:
0.57 µs × 300 = 0.17 ms. **Do not switch for performance.** Switch only if mesh BVHs
(not AABBs) become the bottleneck, and then pick tinybvh for the CPU kernels or
madmann91 for the cleaner API.

Leaf size: tinybvh's `BVH4_CPU` uses **four triangles per leaf**; the plain layout uses a
small leaf (typically 1–4). For AABBs, 4–8 per leaf is the usual choice; ours is
unbounded by count and split by median, which is fine.

### 4.5 Licensing summary

MIT (tinybvh, madmann91, nanort), Apache-2.0 (Embree, PBRT, Intel MSOC, S2), BSL-1.0
(Clipper2, Boost), GPL/LGPL (CGAL). Only MIT/BSL/Apache are compatible with a closed
payload; Apache-2.0 adds a patent grant and a NOTICE obligation, which is fine for an
internal tool but worth knowing.

---

## 5. Visibility and occlusion culling

### 5.1 The techniques

| Technique | What it answers | Cost model | Published characteristics |
|---|---|---|---|
| **Hardware occlusion queries** | "did any sample of this box pass depth?" | one GPU round trip; results are async and reading them **stalls** | GPU Gems 3 ch. 29: results are asynchronous, so a naive per-object "query then read" **stalls the CPU** and needs batching/deferred reads (<https://developer.nvidia.com/gpugems/gpugems/part-v-performance-and-practicalities/chapter-29-efficient-occlusion-culling>) |
| **Hi-Z** | conservative box-vs-mip test | build a max-depth pyramid + 4 texel fetches per box | construction **< 0.2 ms** at 1024×768 on a 2010 GPU; culling "almost no cost for thousands of objects" (<https://www.rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/>) |
| **Intel MSOC** | same, on the CPU, with masks | 8×4 tiles, two depths + coverage mask per tile, AVX2 | **3× faster than previous work** while "culling 98% of all triangles culled by a full resolution depth buffer" (<https://www.intel.com/content/dam/develop/external/us/en/documents/masked-software-occlusion-culling-779241.pdf>); **archived by Intel**, Apache-2.0 (<https://github.com/GameTechDev/MaskedOcclusionCulling>) |
| **PVS** | "which rooms can be seen from this cell" | precomputed offline, free at runtime | Airey, Rohlf & Brooks, SIGGRAPH '90, <https://dl.acm.org/doi/10.1145/91385.91416>; wrong granularity for per-hitbox aim |

### 5.2 Could a low-resolution software depth buffer answer our question?

**Yes for "is this hitbox visible", and — with a twist — for "how many walls".**

A 512×256 angular buffer is 131k pixels. Rasterising 312 convex silhouettes, each
covering ~1–3% of the buffer, is ~0.4–1.2 M pixel tests: **on the order of a
millisecond** for a scalar scanline fill, less with SIMD and front-to-back early-out
[unverified estimate]. That is 2–3 orders of magnitude below the arrangement, and it
scales with *screen area*, not with the number of regions.

The catch is that a depth buffer gives only the **nearest** surface. The engine model
needs the *ordered stack* of wall intervals. Two ways out:

1. **Layered depth buffer (K = 4–8).** Rasterise each solid's entry and exit depth into
   per-pixel layers kept sorted front-to-back; K = 4 matches the engine's penetration
   budget. Each pixel then holds exactly what `PenetrationLayer` holds today, and the
   value is a per-pixel product. This is the natural formulation of our problem and
   removes the fragment explosion entirely. It is **not** what MSOC implements (MSOC
   answers a conservative visibility query), so treat it as an engineering proposal, not
   a published result [unverified].
2. **Depth peeling / repeated nearest-surface passes**: render the nearest surface, mask
   it out, repeat K times. Simpler, K× the raster cost, same result.

Precision caveats, all real:

- **Conservative depth.** MSOC stores two depth values and a mask per tile; it is
  *allowed* to say "visible" for something that is occluded, never the reverse. For an
  aimbot the asymmetry is acceptable in the safe direction (you may over-estimate
  visibility), but it means the field is not exact.- **Leakage/holes.** MSOC's README is explicit: "Leakage is part of the algorithm (and
  one reason for the high performance)" and the fast `QUICK_MASK` update trades accuracy
  for speed. A leaked pixel here is a phantom aimpoint through a wall — the failure mode
  we care about.
- **Resolution.** A hitbox is ~1–3° wide. At 512×256 over a 90°×45° chart (0.176°/px)
  that is 6–17 pixels wide — too coarse. You would need a *local* buffer per hitbox (e.g.
  64×64 over its silhouette) or a 2048×1024 atlas, which costs 2 M pixels and several ms.
  The angular resolution must be finer than the shot's spread, which is the same
  constraint as sampling (§11).
- **Conservative rasterisation.** MSOC needs `PRECISE_COVERAGE` to match D3D/GL rules
  (5–15% slower); without it, thin slivers vanish.

**Verdict:** a layered angular buffer is the most *scalable* formulation and a good
research spike, but per-candidate rays get 95% of the benefit in 5% of the code, and
they are exact per candidate. Do rays first.

### 5.3 What not to use

- **Hardware occlusion queries**: a GPU round trip inside a game's present path, with
  async results. Wrong layer, wrong latency.
- **PVS**: offline, per-room, no per-hitbox precision. It also cannot express "how much
  damage survives the wall".

---

## 6. Angular / spherical data structures

### 6.1 The two things we fight

1. **The ±π seam.** A silhouette straddling yaw = ±π collapses to a chart-wide hull. The
   code already fixes this with `convex_hull_unwrapped(..., reference_yaw)`
   (`geometry.cpp:337`): unwrap every yaw into the ±π window about the silhouette's own
   centre, build the hull, then map back. This is the standard "cut the sphere along a
   meridian chosen per query" trick, and it is correct as long as the silhouette is
   smaller than π (true: a hitbox is < 3°).
2. **The `1/cos(pitch)` metric.** `u` is a yaw about the world Z axis, so a true angular
   offset `r` spans `r` in `v` but `r/cos(v)` in `u`. That is not a bug — it is the
   chart's metric tensor, and `yaw_scale(pitch)` already applies it wherever an angular
   quantity is evaluated (cone radius, hitchance kernel, sphere silhouettes).

### 6.2 Would a spherical formulation help?

A spherical formulation makes a shot cone a **spherical cap** and a silhouette a
**spherical convex polygon** with great-circle edges. It removes the seam by
construction and makes the cone isotropic with no `1/cos` factor. The cost:

- all clipping becomes great-circle half-space clipping (`dot(n, p) <= 0` on the unit
  sphere) — the same Sutherland–Hodgman structure, but every intersection is a
  normalized cross product, and the Chebyshev centre / bivariate-normal mass have no
  planar analogue;
- the exact bivariate-normal mass over a convex polygon (our `polygon_normal_mass`, which
  matches the DTIC report) is a *planar* result; on a spherical cap you would need a
  different integral;
- the chart is local anyway (a few degrees), where the planar approximation error is
  O(θ²) ≈ 1e-3 relative at 2°.

**Verdict: do not rewrite.** The seam is already handled, the metric is already applied,
and the planar local chart is what makes the exact normal integral possible. Spherical
geometry would trade a solved problem for an unsolved one.

### 6.3 If you ever need a global direction index

- **S2** (<https://s2geometry.io/about/overview.html>) is the right tool: a hierarchical
  cube-sphere cell decomposition with geodesic edges, 64-bit `S2CellId` with Hilbert
  locality, `S2CellUnion` set operations, `S2RegionCoverer` for caps, and
  `S2BooleanOperation` for spherical booleans. Google's announcement states the design
  goal explicitly: "no seams or singularities, using a single coordinate system, and with
  low distortion everywhere"
  (<https://opensource.googleblog.com/2017/12/announcing-s2-library-geometry-on-sphere.html>).
  Robustness comes from exact predicates **and snap rounding** — both are documented
  design choices, and both cost performance.
  Cells and indexing: <https://s2geometry.io/devguide/s2cell_hierarchy>,
  <https://s2geometry.io/devguide/s2shapeindex>.
  It depends on Abseil and is a large C++ codebase; integration is weeks, not days.
- **HEALPix** (Górski et al. 2005, ApJ 622:759,
  <https://iopscience.iop.org/article/10.1086/427976>): equal-area, iso-latitude,
  hierarchical. Equal area is nice for a *uniform* angular heatmap; iso-latitude is for
  spherical harmonics, which we do not need. S2's cell edges are geodesics and its
  locality is better for indexing (S2's own docs compare the two and note HEALPix's cell
  boundaries are not geodesics:
  <https://s2geometry.io/devguide/s2cell_hierarchy>, appendix).
- **d3-geo-polygon** (<https://github.com/d3/d3-geo-polygon>) is a JS implementation of
  spherical polygon clipping (`geoClipPolygon`, `geoIntersectArc`) used for projections.
  It is a useful *reference* for great-circle clipping, not a dependency.

There is no standard "index of occluders overlapping a spherical polygon" that beats
what we already do: the eye→hitbox pyramid query in 3-D is exact, cheaper, and needs no
spherical machinery.

---

## 7. Exact predicates and robustness

### 7.1 When do we actually need exact arithmetic?

For **clipping two convex polygons**, almost never. The failure modes are:

- a sliver of area ~1e-7 rad² created when an edge is nearly parallel to a clip line;
- a vertex that lands exactly on an edge;
- two fragments that should share an edge but do not, bit-for-bit.

None of these change the *answer*: the value is constant per cell, the objective is a max
over aims, and the mass integral's error is proportional to the sliver area. What they do
change is the *cell count* (degenerate cells) and, in the worst case, whether two
adjacent cells leave a hairline gap in the hitchance integral.

Two properties of the current code help:

- `subtract_convex` computes the `inside` and `outside` pieces of one edge with the
  *same* parameter `t = dp / (dp - dc)`, so the two pieces share the exact same float
  endpoint. There is no crack along a split.
- `clip_half_plane` classifies with `dc <= 0` (no epsilon), while `polygon_contains` uses
  a `1e-6` tolerance. The only caller of the latter in this path is `occlude_polygon`'s
  "is the occluder's silhouette fully contained" broad phase, where a tolerance error
  only *keeps* a solid for splitting — conservative, not wrong.

### 7.2 What the libraries do, and what it costs

| Approach | Mechanism | Cost | Failure mode |
|---|---|---|---|
| **Clipper2** | scale to int64, ±1 unit grid | int conversion + sweep | features < 2 units vanish; documented on the Robustness page (<https://www.angusj.com/clipper2/Docs/Robustness.htm>) |
| **Boost.Geometry** | float overlay; rescaling *off* by default since 1.79 | float | degenerate overlays; long bug history |
| **CGAL EPICK** | filtered exact *predicates*, inexact constructions | ~2–10× predicates | constructions still round |
| **CGAL EPECK** | exact predicates **and** constructions | 10–100× slower [unverified magnitude] | none (correct) |
| **Shewchuk** | adaptive: fast float filter, exact expansion fallback | [measured] **3.9 ns** naive vs **14.2 ns** adaptive (random), **5.2 ns** structured-near-collinear | none for orientation/incircle |

The Shewchuk measurement is the important one: the adaptive predicate is **4–14 ns**,
i.e. the same order as the arithmetic it replaces, because the exact path only runs when
the float filter cannot decide. The code is public domain
(<https://www.cs.cmu.edu/~quake/robust.html>). Exact *constructions* (CGAL EPECK) are the
expensive part, not exact *predicates*.

### 7.3 The rule for us

1. **Do not add exact arithmetic to the hot path.** The measured 0.55 µs difference has
   no room for it, and the sliver error is below the shot's angular resolution.
2. If a *mass* bug ever appears (hitchance sums that do not add up), the cheap fixes in
   order are: (a) snap chart coordinates to a fixed grid (e.g. 2⁻²⁰ rad ≈ 1e-6 rad)
   before clipping — this is Clipper2's trick without Clipper2; (b) use Shewchuk
   `orient2d` for the side tests; (c) only then consider CGAL, offline.
3. Keep the invariant that a split's two sides share endpoints (already true). Do not
   "clean up" coordinates between the `inside` and `outside` computations.

---

## 8. Spatial indexes for many small queries

| Structure | Build (10k boxes) | Query | Best for |
|---|---|---|---|
| **uniform grid** | O(n) | O(cells along ray + k); tuning-dependent | dense, uniformly distributed scenes |
| **BVH (median split)** | [measured] **2.63 ms** | [measured] **0.57 µs** | sparse/non-uniform, static or rebuilt |
| **BVH (SAH)** | 2–3× the same builder's median cost; a SIMD binned builder is < 1 ms at 10k [unverified] | ~10–30% fewer node tests | static, quality matters |
| **loose octree** | O(n log n) | similar to BVH, more memory | dynamic scenes |
| **Morton / LBVH** | O(n), parallel | worse quality, faster build | per-frame rebuilds |

Published comparisons agree on the shape of the answer: BVHs win for non-uniform scenes
and have no tuning parameters; uniform grids win for dense, uniform, single-mesh scenes
and can beat BVHs when most of the scene is occluded
(<https://graphics.stanford.edu/~boulos/papers/efficient_notes.pdf>;
Thrane & Simonsen, *A Comparison of Acceleration Structures for GPU Assisted Ray Tracing*,
<a href="https://www.ks.uiuc.edu/Research/vmd/projects/ece498/raytracing/GPU_BVHthesis.pdf">thesis</a>).
At 10k AABBs and 50–300 queries/tick the difference is under 0.2 ms either way.

**Verdict:** keep `AabbBvh`. The only measured surprise is `query_pyramid` at **9.67 µs**
(SAT per node); if it ever matters, cache per enemy per tick rather than changing the
structure.

---

## 9. What we already have is good enough

Being honest about where a rewrite is a net loss:

| Component | Evidence | Verdict |
|---|---|---|
| `clip_half_plane` | 0.1 µs, 8 vertices; allocates one vector per call (an `_into` variant already exists for the Chebyshev search) | keep |
| `subtract_convex` | 0.55 µs; **6× faster than Clipper2**; exact convex semantics | keep — improve the *caller* |
| `convex_hull` (monotone chain) | O(n log n), n ≤ 16; optimal | keep |
| `convex_hull_unwrapped` | solves the ±π seam correctly | keep — this is the part people get wrong |
| `chebyshev_center` / radius | bisection + half-plane clip; no library does this better at this size | keep |
| `OccluderCell::ray_span` | **51.5 ns**, handles polytope/sphere/capsule/mesh | keep — it is the foundation of the replacement |
| `AabbBvh` | 2.6 ms build / 0.57 µs query at 10k | keep |
| `MeshBvh` | median split; adequate; built once per mesh at parse time | keep until mesh tracing is hot |
| `polygon_normal_mass` | exact bivariate normal over a convex polygon, matches the DTIC report | keep; only replace with Drezner's CDF if the cone objective stays hot |
| `DamageField::PointGrid` | 102 ms → 0.15 ms on `argmax` | keep |
| `penetration_after_walls` | engine model, already validated | keep — the replacement reuses it verbatim |

The thing to delete is not a primitive. It is `occlude_polygon`'s **arrangement loop** on
the aim path.

---

## 10. The manual-mapping constraint

What the loader actually does (from `docs/architecture.md`, ADR-0016/0019 and
`injector/src/main.cpp`):

- sections mapped, relocations applied, imports resolved by reading the target's export
  tables; API-set names rewritten (`api-ms-win-crt-*` → `ucrtbase.dll`);
- `.pdata` registered with `RtlAddFunctionTable`;
- the PE entry point (`DllMain`) is called. With a static CRT that entry is
  `_DllMainCRTStartup`, which normally runs the CRT initialisers; the injector ships a
  `crt_dll` fixture specifically to isolate this behaviour. **Verify it on the current
  loader before relying on a library's global constructors** — do not assume.
- the payload is built with `b_vscrt=mt` (static CRT, `toolchain/windows-msvc.ini`);
- the loader does **not** initialise TLS for mapped modules; the project has a
  `payload/tls` bootstrap, and payload threads use
  `THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH` (ADR-0017).

That gives a precise filter:

| Constraint | Consequence | Libraries |
|---|---|---|
| **No new import table entries** beyond the CRT/Win32 sets | header-only or a few TUs preferred | tinybvh, Clipper2, Boost.Geometry, madmann91, nanort, Shewchuk |
| **`thread_local` is risky on `SKIP_THREAD_ATTACH` threads** | avoid libraries with TLS caches or per-thread default state | Embree/TBB are worst; tinybvh has a process-wide `std::thread` pool, madmann spawns one per parallel build (disable parallel builds) |
| **No static-init-time work** (constructors of globals, pool creation, locale) | prefer POD/`constexpr` tables; create pools lazily or not at all | Clipper2: a grep found no namespace-scope objects; tinybvh: pool on first threaded build |
| **Exceptions** | `.pdata` is registered, so SEH unwinding works, but every `throw`/`catch` pulls `__CxxFrameHandler3`/`_CxxThrowException` and unwind data; avoid on principle | Clipper2 guards its throws behind `__cpp_exceptions` (`clipper.core.h:73–95`) and compiles with `-fno-exceptions`; CGAL is exception-heavy |
| **RTTI** | `dynamic_cast`/`typeid` pull `__RTtypeid`, `__RTDynamicCast` and type descriptors into `.rdata` | tinybvh/madmann/nanort: none; CGAL/Embree: avoid |
| **DLL size** | header-only costs only what you instantiate | Clipper2 +~100–200 KB [unverified]; tinybvh +~50–100 KB [unverified]; Embree static: multi-MB [unverified]; CGAL + GMP/MPFR: tens of MB [unverified] |

**Practical recipe for Clipper2 in this payload:**

- use the `Point64` API (integers) directly — the float `PointD` path is where the
  precision/scale `throw`s live;
- compile with `-fno-exceptions` (the guard makes the error path a silent no-op, so
  validate ranges yourself);
- include it in **one** TU (`clipper.engine.cpp` + `clipper.offset.cpp` if offsetting is
  used) to keep the template instantiation out of everything else.

**Practical recipe for tinybvh:**

- `#define TINYBVH_IMPLEMENTATION` in one TU;
- `#define TINYBVH_NO_BUILTIN_POOL` and `settings.enableThreading = false` — a 10k-AABB
  build is ~1 ms single-threaded, and you avoid a process-wide pool created under the
  mapper;
- keep `NO_INDEXED_GEOMETRY` / `NO_VOXEL_SUPPORT` if unused (size).

---

## 11. Recommendation for `occlude_polygon`

### Stage 0 — one hour, no behaviour change

Re-prune per fragment: before `subtract_convex(fragment, occluder)`, test the fragment's
AABB against the occluder's AABB; if disjoint, keep the fragment unchanged. Measured
**8–14×** at the densities that matter. This alone turns the worst live case from
"tens of milliseconds" into "a few milliseconds", and it makes the heatmap cheap enough
to keep.

Cheap follow-ups in the same pass:

- hoist `inward_edges(b)` out of the fragment loop (it is recomputed for every fragment);
- reuse a per-fragment scratch `Polygon`/`std::vector<Polygon>` instead of allocating;
- stop re-running `fragment_layers` for fragments whose covering set and centroid are
  unchanged.

### Stage 1 — the actual fix (2–3 days): evaluate, don't enumerate

On the **aim** path, replace the arrangement with a candidate search.

One point of honesty first: the arrangement's "exactness" is partly illusory.
`fragment_layers` evaluates the wall stack at the fragment's **centroid**
(`damage.cpp:54`), so the value is already assumed constant across a cell even though the
wall thickness varies continuously inside it. A candidate sample is therefore *more*
accurate at the point it evaluates, not less — the arrangement buys exact region
boundaries for an approximate per-region value.

The steps:

1. **Candidates per hitbox.** A grid in the hitbox's own chart frame with spacing ≤ σ (the
   shot's angular standard deviation), so any region wider than σ contains a sample, plus
   the Chebyshev centre, the centroid, and the 4 extreme points. At σ ≈ 0.3° and a 2°
   hitbox that is 7×7 = 49 points; spacing σ/2 is the belt-and-braces version at ~4× the
   rays.
2. **Occluders per candidate.** The eye→hitbox pyramid list (`query_pyramid`) is already
   the exact conservative set (14 solids live). A `polygon_contains` test per solid
   prunes to the ones actually covering the direction.
3. **Value.** `ray_span` per surviving solid, sort by entry, `penetration_after_walls`.
   This is 4–8 rays per candidate at 51 ns each.
4. **Score.** `damage × P(cone lands in the hitbox)`; the probability is the existing
   exact `polygon_normal_mass` on the *unoccluded* silhouette (occlusion is already
   accounted for by the candidate's damage). Refine locally around the winner.
5. **Keep the arrangement** only for `heatmap.cpp`, off the hot path.

Expected: **2–5 ms** for 57 hitboxes, versus 600 ms. The dominant cost is the ray casts,
and they are exact per candidate.

Risks and mitigations:

- **A thin visible sliver between two walls.** If its angular width is below σ it is not
  a meaningful aimpoint anyway; if it is above σ, the grid spacing guarantee means a
  sample lands in it. For extra safety, seed candidates from the previous tick's
  best cells and from the arrangement's cell centres when the arrangement is available.
- **Hitchance semantics change** (cell mass → point value × silhouette mass). Validate
  against the current solver on saved scenes; the difference should be below the spread.
- **Occluders that cover only part of the cone** are handled per candidate, which is
  *more* correct than a per-cell constant, not less.

### Stage 2 — optional research: layered angular buffer

Only if a *rendered* occlusion field is wanted (heatmap, debug, or a future
visibility-aware path). One 512×256 chart atlas, K = 4 layers of
`(entry, exit, surface)`, rasterised front-to-back. Cost is O(pixels), not
O(regions). Caveats in §5.2: conservative depth, leakage, and the resolution must beat
σ.

### What to remove afterwards

- `PenetrationParams::max_occluders` — it is a correctness cap, not an optimisation, and
  Stage 1 makes it unnecessary (the candidate set is bounded by construction).
- The `fragment_layers` re-raycast per fragment on the aim path.
- The full arrangement from `solve`.

---

## 12. Reproduction

The harness (not committed) was built with:

```sh
g++ -O2 -DNDEBUG -std=c++20 \
  -I payload/aim/include -I payload/game/include \
  bench.cpp \
  payload/aim/src/occlusion.cpp payload/aim/src/geometry.cpp \
  clipper2/CPP/Clipper2Lib/src/{clipper.engine,clipper.offset,clipper.rectclip}.cpp
```

- `bench.cpp` — pairwise difference (ours vs Clipper2), one-shot Clipper2 against N
  clips.
- `scaling.cpp` — the N = 4…64 arrangement table, with and without per-fragment pruning.
- `fragtest.cpp` — how many pieces `subtract_convex` returns for a disjoint clip (it
  depends on which edge separates them), and the collapse variant.
- `bvhbench.cpp` — 10k-AABB build/query/pyramid and the `ray_span` microbenchmark.
- `predbench.cpp` + Shewchuk `predicates.c` — naive vs adaptive `orient2d`.

Scenes are seeded (`mt19937(12345)` / `(7)` / `(3)`) and described in §1; rerun on the
payload's real target if a number looks load-bearing.

---

## 13. What not to do

- **Do not port `occlude_polygon` to Clipper2.** It is 6× slower per convex pair, its
  one-shot difference is the wrong semantics (visible region, not labelled cells), and
  the labelled version is the same algorithm with a bigger constant.
- **Do not add CGAL to the payload.** License, size, exceptions, RTTI, and it solves a
  problem we are about to delete.
- **Do not make the chart spherical.** The seam is already fixed; the metric is already
  applied; the exact normal integral is planar.
- **Do not swap `AabbBvh`/`MeshBvh` for a "faster" BVH.** Measured 0.57 µs/query; there
  is nothing to win at 300 queries/tick.
- **Do not use hardware occlusion queries or PVS** for this question.
- **Do not cap occluders** to make the arrangement fast. The cap is what Stage 1 removes.
- **Do not trust a before/after across a game restart** (the bots move, the cell count
  changes). Use the saved-cell-set host benchmark, as `docs/ragebot-perf.md` says.

---

## 14. References

Primary sources, in the order they are used.

**2-D booleans / arrangements**

- Clipper2 — repository and docs: <https://github.com/AngusJohnson/Clipper2>,
  <https://www.angusj.com/clipper2/Docs/Overview.htm>,
  robustness: <https://www.angusj.com/clipper2/Docs/Robustness.htm>,
  benchmark chart (Clipper1 vs Clipper2): <https://www.angusj.com/clipper2/Docs/Changes.htm>
- CGAL 2D Regularized Boolean Set-Operations:
  <https://doc.cgal.org/latest/Boolean_set_operations_2/index.html>; license:
  <https://www.cgal.org/license.html>; 2-D arrangements:
  <https://doc.cgal.org/latest/Arrangement_on_surface_2/index.html>
- Boost.Geometry release notes (rescaling off by default in 1.79):
  <https://www.boost.org/doc/libs/latest/libs/geometry/doc/html/geometry/release_notes.html>;
  design rationale:
  <https://www.boost.org/doc/libs/1_86_0/libs/geometry/doc/html/geometry/design.html>
- Martínez, Rueda & Feito, *A new algorithm for computing Boolean operations on
  polygons*, Computers & Geosciences 35(6):1177–1185, 2009:
  <https://cdn.jsdelivr.net/gh/mfogel/polygon-clipping/paper.pdf>;
  author's implementations (unsupported): <http://www4.ujaen.es/~fmartin/bool_op.html>
- Vatti, *A generic solution to polygon clipping*, CACM 35(7):56–63, 1992,
  <https://doi.org/10.1145/129902.129906>
- Greiner & Hormann, *Efficient clipping of arbitrary polygons*, ACM TOG 17(2):71–83,
  1998: <https://www.inf.usi.ch/hormann/papers/Greiner.1998.ECO.pdf>; degeneracy
  limitation: <https://en.wikipedia.org/wiki/Greiner%E2%80%93Hormann_clipping_algorithm>;
  fix: Foster, Hormann & Popa, *Clipping simple polygons with degenerate intersections*,
  Computers & Graphics: X 2:100007, 2019:
  <https://www.sciencedirect.com/science/article/pii/S259014861930007X>
- O'Rourke, Chien, Olson & Naddor, *A new linear algorithm for intersecting convex
  polygons*, CGIP 19(4):384–391, 1982:
  <https://www.cs.jhu.edu/~misha/Spring16/ORourke82.pdf>
- i_overlay: <https://github.com/iShape-Rust/iOverlay>

**BVH / ray tracing**

- tinybvh: <https://github.com/jbikker/tinybvh>; manual (build/traversal numbers):
  <https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/>; advanced:
  <https://jacco.ompf2.com/2025/01/25/tinybvh-manual-advanced/>
- Embree: <https://www.embree.org/>; Wald et al., *Embree: A Kernel Framework for
  Efficient CPU Ray Tracing*, ACM TOG 33(4), 2014, <https://doi.org/10.1145/2601097.2601199>
- madmann91/bvh: <https://github.com/madmann91/bvh>
- nanort: <https://github.com/lighttransport/nanort>
- Ize, *Robust BVH Ray Traversal*, JCGT 2(2), 2013:
  <https://jcgt.org/published/0002/02/02/>
- PBRT, *Bounding Volume Hierarchies*:
  <https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies>

**Visibility**

- Hasselgren, Andersson & Akenine-Möller, *Masked Software Occlusion Culling*, HPG 2016:
  <https://www.highperformancegraphics.org/wp-content/uploads/2016/HPG2016_MSOC.pdf>;
  Intel copy (3× / 98% claim):
  <https://www.intel.com/content/dam/develop/external/us/en/documents/masked-software-occlusion-culling-779241.pdf>
- Intel `MaskedOcclusionCulling` (archived, Apache-2.0):
  <https://github.com/GameTechDev/MaskedOcclusionCulling>
- RasterGrid, *Hierarchical-Z map based occlusion culling*:
  <https://www.rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/>
- GPU Gems 3, ch. 29, *Efficient Occlusion Culling*:
  <https://developer.nvidia.com/gpugems/gpugems/part-v-performance-and-practicalities/chapter-29-efficient-occlusion-culling>
- Airey, Rohlf & Brooks, *Towards image realism with interactive update rates in complex
  virtual building environments* (PVS), SIGGRAPH '90:
  <https://dl.acm.org/doi/10.1145/91385.91416>

**Spherical**

- S2 overview: <https://s2geometry.io/about/overview.html>; cells:
  <https://s2geometry.io/devguide/s2cell_hierarchy>; shape index:
  <https://s2geometry.io/devguide/s2shapeindex>; announcement:
  <https://opensource.googleblog.com/2017/12/announcing-s2-library-geometry-on-sphere.html>
- HEALPix: Górski et al., ApJ 622:759, 2005,
  <https://iopscience.iop.org/article/10.1086/427976>; <https://healpix.sourceforge.io>
- d3-geo-polygon (spherical polygon clipping, JS):
  <https://github.com/d3/d3-geo-polygon>

**Exact predicates**

- Shewchuk, *Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric
  Predicates*, DCG 18:305–363, 1997; code (public domain):
  <https://www.cs.cmu.edu/~quake/robust.html>
- Hobby, *Practical segment intersection with finite precision output*, Computational
  Geometry 13(4):199–214, 1999 (snap rounding), <https://doi.org/10.1016/S0925-7721(98)00036-4>

**Spatial indexes**

- Boulos, *Notes on efficient ray tracing*:
  <https://graphics.stanford.edu/~boulos/papers/efficient_notes.pdf>
- Thrane & Simonsen, *A Comparison of Acceleration Structures for GPU Assisted Ray
  Tracing*:
  <https://www.ks.uiuc.edu/Research/vmd/projects/ece498/raytracing/GPU_BVHthesis.pdf>
- Aila et al., *On Quality Metrics of Bounding Volume Hierarchies*, HPG 2013:
  <https://users.aalto.fi/~ailat1/publications/aila2013hpg_paper.pdf>

**Project**

- `docs/ragebot-research.md`, `docs/ragebot-perf.md`, `docs/ragebot.md`
- `payload/aim/src/damage.cpp` (`occlude_polygon`), `payload/aim/src/occlusion.cpp`
  (`subtract_convex`, `ray_span`), `payload/aim/src/geometry.cpp` (`clip_half_plane`),
  `payload/aim/src/bvh.cpp`, `payload/aim/include/cs2/aim/*.hpp`
- `docs/architecture.md` (manual map), `docs/decisions.md` ADR-0016/0019, ADR-0023
