# Research index — ragebot

Three deep-research briefs, written 2026-10-19 by parallel agents, each grounded in
primary sources and with its own measurements. Start with
[`../ragebot-research.md`](../ragebot-research.md) (the short orientation brief), then
whichever of these matches your task.

| Brief | Covers | Read it when |
|---|---|---|
| [`autowall-implementations.md`](autowall-implementations.md) | What every public CS2/CS:GO cheat actually does: architecture, the canonical trace loop, data structures, licenses | You are changing how occlusion/penetration is computed |
| [`geometry-algorithms.md`](geometry-algorithms.md) | Measured benchmarks of our primitives vs Clipper2/CGAL/Boost, BVH libraries, visibility algorithms, the manual-map constraints | You are about to write geometry or pick a library |
| [`aim-objective-math.md`](aim-objective-math.md) | The probability model, the spread kernel, numerics of the polygon mass, multimodal optimisation, pellets | You are touching the objective search or hitchance |

## The three findings that matter most

1. **The algorithm is wrong, not the primitive.** [`geometry-algorithms.md`](geometry-algorithms.md) §2
   measured `subtract_convex` at **0.55 µs** per convex difference — **6× faster than
   Clipper2** (3.26 µs) on the same pair. The problem is that `occlude_polygon` *enumerates
   an arrangement*: one silhouette against 4→64 overlapping clips goes 8 → 2,487 fragments
   and 0.03 → 36 ms. Stop enumerating regions; evaluate the objective at a bounded set of
   directions (`ray_span` is **51.5 ns**, so a candidate solve is ~2–5 ms instead of 600 ms).

2. **Every public cheat traces; none builds a field.** [`autowall-implementations.md`](autowall-implementations.md) §0/§7.
   In CS2 the engine call returns the **whole sorted surface stack** in one go, so the
   correct shape is *one trace per candidate + ≤5 arithmetic steps* — not the CS:GO
   `TraceRay`/`TraceToExit` loop. Our projected damage field is novel; it is also why we
   are slow.

3. **The hitchance σ is 2.449× too wide — a gameplay-visible bug.**
   [`aim-objective-math.md`](aim-objective-math.md) §0/§3. `service.cpp` sets
   `spread_sigma = sqrt(S² + I²)`, but the engine's actual spread kernel has per-axis
   variance `(S² + I²)/6`, so the matched σ is `sqrt(S² + I²)/√6`. Measured: for a
   head-sized target the true hit probability is **0.777** where our model says **0.221**
   — so `min_hitchance` rejects shots that hit ~78 % of the time.

## Bugs found in our code

| # | Where | Issue | Status |
|---|---|---|---|
| 1 | `payload/control/src/service.cpp` (`spread_sigma`) | σ is `√6` too wide vs the engine's actual spread kernel (`(S²+I²)/6` per axis, not `S²+I²`) | **verified** — the line is as described; `min_hitchance` over-rejects |
| 2 | `payload/aim/src/damage.cpp` (`fragment_layers` / `wall_params`) | the layer uses the record's own `min(enter, exit)` and sets `exit = enter` (one surface per brush), rather than `min()` over every surface in the span | **verified simplification** — documented in-code; bounded (only multi-surface brushes) |
| 3 | `payload/aim/src/penetration.cpp` | the survey reports the `<1` stop / thickness terms are evaluated at the target rather than at the wall | **reported, not confirmed** — our falloff is a single `range_falloff(total)` (equivalent to the product of per-segment falloffs); the open question is the `<1` gate's timing. See the brief before acting. |

The σ bug (#1) is the one to fix first: it is one line and it changes gameplay.

## Cheapest wins, in order

1. **Per-fragment AABB re-prune** before splitting — ~1 hour, **10–14×** on the hot path,
   no model change ([`geometry-algorithms.md`](geometry-algorithms.md) §2.3).
2. **Fix the σ** — one line, and it un-breaks `min_hitchance`
   ([`aim-objective-math.md`](aim-objective-math.md) §8).
3. **Evaluate, don't enumerate**: score `field_candidates` with `ray_span` +
   `penetration_after_walls`; keep the arrangement only for the heatmap
   ([`geometry-algorithms.md`](geometry-algorithms.md) §11, stage 1). Then the stopgap
   occluder caps can be removed.

## Licensing constraints (from the surveys)

- **Osiris is MIT** (my original brief said GPL-3.0 — wrong), but its `master` has no
  combat code; the aimbot is only on the CS:GO tag.
- **No licence file ⇒ all rights reserved**: `iCollin/csgo-internal`, `Ripper99-x/CS2-Internal`,
  `FrezzyHook-4`, `OneTap` — read for understanding, do not copy.
- **GPL-2.0**: Valthrun, `mvtnorm` — ideas only. **GPL/LGPL**: CGAL — offline oracle only.
- Safe to adopt: **Clipper2** (BSL-1.0), **tinybvh** (MIT), **Boost.Geometry** (BSL-1.0),
  **MaskedOcclusionCulling** (Apache-2.0), and our own code (preferred where measured faster).

## Manual-mapping constraints (applies to any library we add)

From `geometry-algorithms.md` §10: the payload is manually mapped, so there is **no CRT
dynamic initialisation, no `thread_local` caches, no process-wide thread pools**, and
`payload.dll` size matters. Also the injector must resolve every CRT import (see
`docs/handoff.md` gotchas). Header-only + static-CRT + `-fno-exceptions` is the safe shape.
