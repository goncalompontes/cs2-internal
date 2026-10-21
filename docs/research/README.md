# Research index — ragebot

Four deep-research briefs, written 2026-10-19 by parallel agents, each grounded in
primary sources and with its own measurements. Start with
[`../ragebot-research.md`](../ragebot-research.md) (the short orientation brief), then
whichever of these matches your task.

| Brief | Covers | Read it when |
|---|---|---|
| [`autowall-implementations.md`](autowall-implementations.md) | What every public CS2/CS:GO cheat actually does: architecture, the canonical trace loop, data structures, licenses | You are changing how occlusion/penetration is computed |
| [`geometry-algorithms.md`](geometry-algorithms.md) | Measured benchmarks of our primitives vs Clipper2/CGAL/Boost, BVH libraries, visibility algorithms, the manual-map constraints | You are about to write geometry or pick a library |
| [`aim-objective-math.md`](aim-objective-math.md) | The probability model, the spread kernel, numerics of the polygon mass, multimodal optimisation, pellets | You are touching the objective search or hitchance |
| [`aim-solver-design-review.md`](aim-solver-design-review.md) | Combined research + source audit; exactness gaps, cap/doc conflicts, and a bounded ray-query migration | You need a decision before changing the live solver |

Third-party reference material has its own notes:

| Note | Covers | Read it when |
|---|---|---|
| [`neverlose-mining.md`](neverlose-mining.md) | The reversed Neverlose CS2 module: movement, chams pass model, schema/ESP, protection — with evidence addresses | You are comparing our design against a shipped CS2 cheat |
| [`cheat-dump-inventory.md`](cheat-dump-inventory.md) | What other CS2 source leaks and binary dumps are public, what each actually is, authenticity confidence, and the ranked reading order | You are about to spend time on a third-party artifact, or need a reference for chams/ragebot/anti-aim |
| [`cs2-chams-t7.md`](cs2-chams-t7.md) | **T7 answered:** the CS2 chams/material-override path (KV3 material creation, `draw_scene_object` hook, per-scene-object material + colour), CS:GO contrast | You are implementing chams, or need the CS2 material API indices |
| [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) | The only public **CS2 ragebot**: 256-sample hitchance with the game's RNG, hash-seeded no-spread, engine-priced penetration, lag-record backtrack, and the CS2 command-acceptance plumbing (input history, attack history, fake tick, move CRC) | You are implementing the CS2 aim path, the backtrack, or the command plumbing |
| [`gamesense-csgo-re.md`](gamesense-csgo-re.md) | Reversal of the gamesense **CS:GO** module: feature registry (338 config vars), VMT material library, override sequence, signature-resolution subsystem | You want the industry feature/ragebot vocabulary, or a CS:GO chams reference |
| [`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) | The only readable top-tier ragebot: point generation and the radius-scale formula, 64-sample hit chance with hitbox-pinned counting, the penetration damage model, the **safe-point** tiers, and the **resolver as a hypothesis set updated from server feedback** | You are changing how points are scored, how safe points are defined, or how the resolver should work |
| [`fatality-cs2-base.md`](fatality-cs2-base.md) | The Fatality CS2 base leak: what is implemented vs missing, and the product's full **config schema** (safety, fallback, autostop, hitboxes, materials) | You are naming config knobs, or cross-checking the CS2 SDK/schema |
| [`plaguecheat-protection.md`](plaguecheat-protection.md) | The Plaguecheat module reviver: segment extraction, IAT/OEP rebuild, custom VEH, snapshot spoofing — a readable protection design | You are hardening our loader, or analysing a protected module |
| [`angle-split-in-the-wild.md`](angle-split-in-the-wild.md) | How the real cheat ecosystem splits the **camera angles** from the **player/eye angles**: the CS2 angle channels (base `viewangles` / sub-tick `input_history` / `CCSGOInput` view angles / `m_angEyeAngles`), the validate-input and view-setup camera pins, the movement re-base every anti-aim needs, Valve's `sv_subtick_movement_view_angles` gate, and the CS2 failure modes | You are building the anti-aim on top of the camera/eye split |

[`frontier-prompt.md`](frontier-prompt.md) is a **self-contained prompt** (all the measured
numbers and constraints inline) for handing the whole problem to a frontier reasoning
model, asking it to formalise the problem, compare designs, and propose the optimal
architecture.

## Findings that matter most

1. **The algorithm is wrong, not the primitive.** [`geometry-algorithms.md`](geometry-algorithms.md) §2
   measured `subtract_convex` at **0.55 µs** per convex difference — **6× faster than
   Clipper2** (3.26 µs) on the same pair. The problem is that `occlude_polygon` *enumerates
   an arrangement*: one silhouette against 4→64 overlapping clips goes 8 → 2,487 fragments
   and 0.03 → 36 ms. Stop enumerating regions; evaluate the objective at a bounded set of
   directions (`ray_span` is **51.5 ns** as a primitive microbenchmark; the 2–5 ms
   end-to-end candidate estimate is not yet measured) instead of the current 600 ms
   arrangement worst case.

2. **Public implementations surveyed use per-point traces; none builds a damage field.**
   [`autowall-implementations.md`](autowall-implementations.md) §0/§7. Community CS2
   `CreateTrace` releases describe a whole sorted stack. Current-build RE now confirms
   `FX_FireBullets` uses `TraceLineIntervals` plus repeated penetration advance instead;
   the community wrapper is a distinct API, not evidence of the shipped shot path.
   Full interval/material and mesh differential work remains open. The projected damage
   field is still the measured cost driver.

3. **The hitchance σ may be 2.449× too wide — a gameplay-visible bug if the radial
   transform is confirmed.** [`aim-objective-math.md`](aim-objective-math.md) §3. `service.cpp`
   sets `spread_sigma = sqrt(S² + I²)`. Under the documented `r ~ U[0,1]` radial kernel,
   per-axis variance is `(S² + I²)/6`, and the head-sized-target measurement is 0.221
   reported vs 0.777 under that kernel. The live RE brief flags a possible `sqrt(r)`;
   verify the current binary and inaccuracy units before applying the correction.

4. **The source audit adds correctness blockers to the performance decision.** The live
   service still sets `max_occluders = 24` (the solver now fails closed when it truncates);
   fragments are valued using a centroid ray; independently appended hitboxes are not
   made disjoint before probability masses are summed; and mesh ray spans keep only the
   nearest two triangle crossings. See
   [`aim-solver-design-review.md`](aim-solver-design-review.md) before treating the current
   field or the proposed candidate grid as exact.

## Bugs found in our code

| # | Where | Issue | Status |
|---|---|---|---|
| 1 | `payload/control/src/service.cpp` (`spread_sigma`) | σ is `√6` too wide if the engine uses the documented `r ~ U[0,1]` radial draws; a `sqrt(r)` in the live instruction stream would change the variance | **conditional** — verify the current-build radial transform and inaccuracy units before changing the line |
| 2 | `payload/aim/src/damage.cpp` (`fragment_layers` / `wall_params`) | the layer uses the record's own `min(enter, exit)` and sets `exit = enter` (one surface per brush), rather than `min()` over every surface in the span | **verified simplification** — documented in-code; bounded (only multi-surface brushes) |
| 3 | `payload/aim/src/penetration.cpp` | the survey reports the `<1` stop / thickness terms are evaluated at the target rather than at the wall | **reported, not confirmed** — our falloff is a single `range_falloff(total)` (equivalent to the product of per-segment falloffs); the open question is the `<1` gate's timing. See the brief before acting. |

The σ line is cheap to change, but only after the kernel transform and units are pinned.
Row 3's old range-falloff note is superseded: the current working tree applies free-flight
falloff segment-wise, checks the trace gate at wall exit, and applies hitgroup scaling after
walls. Host tests cover this; live engine differential validation remains pending. See
[`aim-solver-design-review.md`](aim-solver-design-review.md).

## Cheapest wins, in order

1. **Avoid per-fragment AABB re-pruning on the aim path for now.** Although it preserves
   polygon coverage, centroid valuation means it can change sampled damage without a
   bound. The current patch instead prepares each occluder's clip planes once and reuses
   them across its fragments; test for exact output parity and benchmark this behavior-
   preserving optimization. See [`aim-solver-design-review.md`](aim-solver-design-review.md).
2. **Verify then fix the σ** — one line after the live radial transform and units are pinned
   ([`aim-objective-math.md`](aim-objective-math.md) §8).
3. **Evaluate, don't enumerate**: use full-scene exact ray scores to seed an adaptive
   search; add conservative bounds before claiming a global error guarantee. Keep the
   arrangement only for diagnostics/heatmaps. Remove the stopgap caps only after the ray
   oracle and every enumeration limit pass completeness tests
   ([`aim-solver-design-review.md`](aim-solver-design-review.md)).

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
