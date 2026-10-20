# Aim-solver design review — research plus source audit

> **Date:** 2026-10-19
>
> **Scope:** the current `payload/aim` and `payload/control` implementation, the three
> research briefs in this directory, and the earlier formulation review. This is a
> source/document review; I did not build the project, launch CS2, or re-run live/host
> benchmarks in this pass. Source claims below are directly checkable in the linked
> files; RE, benchmark, and community-source claims retain the confidence labels of the
> source documents.

## Executive decision

**Remove the labelled arrangement from the per-tick aim path; keep it as an optional
heatmap/debug representation. Do not replace it with a fixed-resolution raster or a
finite ray grid and call that globally optimal.** The next aim-path evaluator should
trace each tested ray against the complete scene, produce every collision interval
needed by the penetration model, and evaluate the exact wall sequence. Use the existing
BVHs as broad phases, but first make their ray query return the right thing.

The source review found reasons not to ship the existing “exact field” as an oracle:

1. Each fragment receives one wall stack evaluated along its **centroid ray**. Wall
   thickness and sometimes the intersected surfaces vary inside that fragment.
2. Hitbox fragments from different hitboxes are added independently. Their projected
   polygons are not made disjoint, yet probability and expected-damage code sums their
   Gaussian masses. `value_at`, by contrast, takes the maximum value of overlapping cells.
3. The current rage path still sets `max_occluders = 24`; the report in
   `ragebot-perf.md` saying caps were reverted is stale relative to `service.cpp`.
4. The custom mesh span returns the nearest two triangle crossings, not a proven set of
   all solid intervals for a triangle soup or a spatially cut mesh cluster.
5. At audit time, the copied penetration evaluator applied range falloff to the hitbox
   distance before processing walls, and applied the hitgroup multiplier before wall
   loss. The current patch now applies free-flight falloff segment by segment and hitgroup
   scaling after wall penetration, and checks the 3,000-unit gate at wall exit; it still
   needs live engine differential validation.

So there are **two distinct problems**: the arrangement is the wrong per-tick cost
shape, and several code paths currently do not establish the claimed exactness. Replacing
the arrangement with exact traces fixes the first only after the trace/stack semantics
are validated. Exact trace results at a finite set of rays still do not prove a global
maximum over continuous aim directions.

## What the code currently does

The live path in `solver.cpp` projects each hitbox, filters occluders by target, calls
`occlude_polygon`, appends each returned fragment to `DamageField`, then optimizes that
field. `damage.cpp` ranks candidate solids by a conservative eye-to-solid distance,
clips every current fragment by each candidate silhouette, and values each final
fragment using `fragment_layers` at `polygon_centroid(fragment.polygon)`.

The shape broad phase is not the whole cost. `service.cpp` still assigns
`PenetrationParams::max_occluders = 24`, explicitly truncating the nearest-ranked
occluders. Separately, `occlusion_query.cpp` has hard limits including 8,192 emitted
occluders, 256 unique bodies, and 128 mesh clusters. The 24-solid limit is explicitly
non-exact; the other limits also need a documented proof that reaching them cannot drop
attenuating geometry. The compound shape type 4 is still unresolved/skipped according to
`docs/re/occlusion.md` and `docs/research/autowall-implementations.md`.

The mesh path projects a conservative hull of cluster AABB corners, then calls
`ray_mesh_span`/`ray_mesh_bvh`. Both routines retain only the nearest two triangle hits.
The BVH test proves equality with the flat scan for that same “first two hits” rule; it
does **not** prove those hits delimit the engine’s first solid interval. A spatial cluster
of a triangle soup is not necessarily a closed convex solid, so pairing its first two
crossings needs a live differential test, especially for open boundaries, overlapping
surfaces, and rays starting inside geometry.

The repo's RE documents now distinguish two paths. Fresh Ghidra analysis of current
`client.dll` (`659940c1…`) confirms the `FX_FireBullets` call path uses
`TraceLineIntervals`, `FUN_18089ca80`'s repeated advance, and a final-hit query. The
community single-call `CreateTrace` / sorted-results model is a different API and is not
the verified shot call path. This resolves the call-path discrepancy, **not** parity:
the full interval/material record and the mesh crossing rule still need a differential
against the engine on saved/live geometry.

## Source-level correctness findings

| Finding | Direct evidence | Consequence / test |
|---|---|---|
| **Centroid-valued fragments are not constant-damage regions.** `fragment_layers` calls `basis.unproject(polygon_centroid(...))`, obtains spans there, and stores one value for the whole polygon. | [`damage.cpp`](../../payload/aim/src/damage.cpp#L47-L95), [`damage.cpp`](../../payload/aim/src/damage.cpp#L173-L223) | Trace several directions inside one fragment and compare thickness, stack, and damage. A disagreement disproves per-fragment exactness. An arrangement can encode topology; it cannot make the path lengths constant. |
| **Cross-hitbox polygons are not proven disjoint.** `solve` adds each hitbox’s fragments separately. `DamageField::add` simply appends. `value_at` takes the maximum of containing cells, while `hit_probability_exact` sums their masses and clamps to 1, and `expected_exact` sums `value × mass`. | [`solver.cpp`](../../payload/aim/src/solver.cpp#L37-L99), [`field.cpp`](../../payload/aim/src/field.cpp#L323-L329), [`field.cpp`](../../payload/aim/src/field.cpp#L481-L565) | Overlapping hitbox/target silhouettes can double-count probability and expected damage. The pointwise max and integrated sum are different fields. Also, a real bullet generally hits the nearest applicable hitbox/target along a ray, not whichever overlapping cell has the largest damage. Test overlapping projected capsules and targets with exact ray traces. |
| **The 24-solid cap remains active.** `rage_tick` sets `max_occluders` to 24; `occlude_polygon` truncates after sorting by a lower-bound distance from the eye. | [`service.cpp`](../../payload/control/src/service.cpp#L1557-L1565), [`damage.cpp`](../../payload/aim/src/damage.cpp#L149-L165) | A farther-ranked solid can be the first intersected solid for some directions. The solver now marks its per-hitbox candidate list incomplete and fails closed when this cap triggers; it still causes aim unavailability and is not an exact pruning rule. This status does not cover upstream enumeration/parser limits. |
| **Range falloff and hitgroup order were wrong in the audited source.** `base` was faded to target distance and hitgroup-scaled before wall loss; the trace gate checked entry rather than exit. | [`damage.cpp`](../../payload/aim/src/damage.cpp#L79-L110), [`penetration.cpp`](../../payload/aim/src/penetration.cpp#L95-L162), [engine model notes](../re/penetration.md#L85-L121) | **Fixed in the current working tree:** `penetration_damage_at_target` fades each free-flight segment, checks the exit against the trace gate, and `fragment_value` applies hitgroup scaling afterward. Host tests pin this ordering; live engine differential validation remains outstanding. |
| **A layer stores two surface records, not the engine’s whole surface range.** `wall_params` takes `min(enter, exit)`, while the RE describes the engine reducing the modifier over every surface index in a span. | [`penetration.cpp`](../../payload/aim/src/penetration.cpp#L32-L64), [`penetration.hpp`](../../payload/aim/include/cs2/aim/penetration.hpp#L51-L58), [engine span reduction](../re/penetration.md#L89-L116) | This is equivalent for the documented one-surface-per-brush case, not proven for multi-surface mesh/compound spans. Carry the full surface range or prove the input invariant. |
| **A mesh cluster is reduced to one first-two-hit span.** `ray_mesh_span` and `ray_mesh_bvh` keep only the nearest two positive triangle crossings. | [`occlusion.cpp`](../../payload/aim/src/occlusion.cpp#L141-L195), [`occlusion.cpp`](../../payload/aim/src/occlusion.cpp#L445-L511) | Equality between BVH and brute-force first-two-hit results is only an implementation test, not proof of a valid entry/exit pair for arbitrary non-convex/open soup. Compare full ordered intervals and damage against the engine on mesh-heavy scenes. |
| **A fully containing over-approximation can be discarded by one center-ray test.** If a projected occluder polygon contains the hitbox silhouette, the code drops that occluder when the centroid ray does not intersect it. | [`damage.cpp`](../../payload/aim/src/damage.cpp#L129-L147) | This is safe for an exact convex projection under the documented assumptions, but not automatically safe for an AABB/convex-hull over-approximation of mesh triangles with holes or disconnected coverage. Construct a mesh whose projected hull contains the target while only an off-center ray hits triangles. |
| **Search is local, not globally certified.** `mass_ascent` seeds from up to four high-valued cells, chooses one best seed, and follows one normalized-gradient/backtracking path. The repository measured a second basin 8.9% above a centre-start result. | [`field.cpp`](../../payload/aim/src/field.cpp#L239-L319), [optimizer experiment](aim-objective-math.md#L465-L495) | “Exact mass evaluation” is not “exact global argmax.” Report the incumbent and a bound, or use an explicit global search. |
| **Pellet candidate generation is not a face-completeness proof.** `argmax_pellets` tests cell anchors and those anchors shifted by each pellet offset; it does not enumerate intersections between edges of different shifted copies. | [`field.cpp`](../../payload/aim/src/field.cpp#L601-L649), [pellet analysis](aim-objective-math.md#L552-L586) | The sum-of-damage objective is correct for the deterministic offsets only if the candidate set reaches every face of the shifted arrangement. Add pairwise shifted-edge events or use a bounded search/smoothed surrogate with an explicit error. |
| **The yaw-scaled Gaussian gradient omitted a chain-rule term.** Evaluation uses `sigma_u = sigma * yaw_scale(aim.y)`, where `yaw_scale=1/cos(pitch)` away from its clamp. | [`field.cpp`](../../payload/aim/src/field.cpp#L42-L124), [`field.cpp`](../../payload/aim/src/field.cpp#L214-L255), [`geometry.cpp`](../../payload/aim/src/geometry.cpp#L135-L138) | **Fixed in the current working tree:** the polygon boundary integral returns `∂mass/∂sigma_u`, and the ascent adds `∂sigma_u/∂pitch`. An off-level `aim-field` regression compares the result with a dense pitch search. |
| **The stated √6 spread correction is conditional.** `service.cpp` uses `sqrt(inaccuracy² + spread²)`. The research brief derives `/√6` if the engine uses radius `r ~ U[0,1]`; the same brief flags an unresolved `sqrt(RandomFloat)` in the live instruction stream. | [`service.cpp`](../../payload/control/src/service.cpp#L1567-L1573), [kernel derivation and open RE question](aim-objective-math.md#L157-L201) | If the radius is uniform, per-axis variance is `(S²+I²)/6`; if the engine applies `sqrt(r)`, it is `(S²+I²)/4`. Verify the current build’s instructions and inaccuracy units before changing the constant. The reported 0.221→0.777 result is for the former kernel/target setup, not a consequence of variance alone. |
| **Seed and objective semantics are unresolved across sources.** `spread.cpp` implements a stream and `MD5_PseudoRandom(sequence)`; the RE/research notes disagree about whether the live firing seed depends on command sequence or aim/tick. The shotgun/Negev pattern path is also not fully modelled. | [`spread.cpp`](../../payload/aim/src/spread.cpp#L139-L168), [open seed question](aim-objective-math.md#L203-L224) | Do not call a Gaussian probability the actual current-shot hitchance until the seed mapping and pellet transforms are live-validated. A known deterministic seed calls for scoring its actual offsets; an unknown seed calls for an explicit prior. |

Two more implementation limits are worth tracking: `parse_hull` rejects hulls above 65,536 vertices, and mesh parsing has a 1M-vertex/triangle limit. The mesh cluster limit appears intended to merge pending ranges rather than discard them, but its worst-case completeness should be tested. Limits that do discard input must be counted and surfaced, not silently treated as exact.

## Mathematical formulation and complexity

For a ray direction `u`, let `T(u)` be its complete, ordered world/prop/mesh/hitbox
intersection sequence. The exact per-direction value is `D(u) = Penetration(T(u), weapon,
pose)`. With commanded aim `a`, the desired objective is one of:

```text
J_damage(a) = E[D(u(a, seed), pose)]
J_hit(a)    = P(D(u(a, seed), pose) > 0)
J_kill(a)   = P(D(u(a, seed), pose) >= target_health)
```

Those are not interchangeable. For one projectile and a known current seed, the objective
is deterministic. If pose records are plausible alternatives, use a mixture over records
and per-hitbox angular covariance, not one isotropic blur for the whole target set.

The present solver is an incremental labelled polygon decomposition. If `P_i` is the
number of fragments before occluder `i`, and `m_i` is its projected edge count, its
dominant clipping work is output-sensitive, roughly

```text
per hitbox:  Σ_i P_i · clip_cost(fragment_i, m_i) + fragment/layer valuation
```

The planar arrangement of `E_h` projected boundary pieces can have `Θ(E_h²)` features;
revisiting all extant fragments for every insertion can add another factor in the worst
case. Across hitboxes the arrangement term is `Σ_h O(E_h²)`, or `O(H E²)` when each
hitbox sees comparable `E` edges. Mesh silhouettes and wall-stack labels add work beyond
the planar boundary intersections. A single `subtract_convex` benchmark does not predict
this cost; the measured fragment counts in `geometry-algorithms.md` already show the
output-growth problem.

For `Q` exact rays through a balanced BVH over `N` primitives, the expected query cost is
`O(Q(log N + V + K log K))`, where `V` is visited nodes and `K` is the returned interval
count; the worst case remains `O(QN)`. A correct ray query must return all relevant
intervals, not just the nearest surface or first two triangle hits. `TraceShape` is
documented by the local RE as first-hit; do not substitute it for a stack query without
re-tracing. The existing `AabbBvh` AABB-query time is not a ray-BVH timing.

A raster costs about `O(T_raster + R²)` for one depth layer and `O(T_raster + L R²)` for
`L` layers, but finite resolution cannot prove no thin wall/sliver was missed. Intel
MaskedOcclusionCulling is a conservative visibility test, not an exact material/thickness
stack. Use it only for ordering or safe rejection backed by an exact upper bound.

## Recommended architecture

### 1. Build an exact ray oracle before changing the search

Prefer the current engine’s validated bullet interval path if its ABI, allocation, filter,
and thread behavior are safe in `CreateMove`. Otherwise add a scene-level ray traversal
over the full cached hull/mesh set, collect all intersections/intervals, include dynamic
shapes, sort by entry, and run the penetration evaluator on that sequence. Keep the
current `AabbBvh` for scene enumeration; add a ray traversal rather than treating an
eye-to-target pyramid or its AABB as a per-ray trace.

The parity test must compare surface/interval order, entry/exit distances, material data,
damage, and stop behavior. Current `TraceLineIntervals` RE notes say interval records do
not expose the material pointer; that gap must be resolved before claiming a custom stack
matches the engine. Also resolve 4-vs-5 penetration count and wall-budget ordering on the
current build.

### 2. Search aim space adaptively, with a real error bound

Use hitbox angular regions to seed a small set of plausible aims (center, nearest point,
extrema), but regard those only as lower-bound samples. For each aim cell `C`, compute an
upper bound using the maximum possible unobstructed damage and the probability that any
spread ray from `C` could reach a target. For a target angular set `T` and additive spread
offset `ε`, the possible-hit offsets satisfy

```text
ε ∈ T ⊕ (-C)
```

so the mass of that expanded region gives a safe probability upper bound. Ignore walls
only in this **upper bound**; every evaluated lower-bound ray still traces all geometry.
Split cells whose upper bound can beat the best exact ray. A fixed grid with spacing `≤ σ`
is not, by itself, a proof that no valuable narrow region was missed.

```text
best = exact_score(revalidated_previous_aim)
queue = target_angular_domains_with_conservative_upper_bounds()

while budget remains and queue.max_upper > best.score + epsilon:
    C = queue.pop_max_upper()
    a = representative(C)
    score = exact_score(full_scene_trace(a, current_seed_or_certified_spread_integral))
    best = max(best, (a, score))
    for child in split(C):
        if conservative_upper(child) > best.score + epsilon:
            queue.push(child)

return best, queue.max_upper - best.score
```

If the seed is itself a function of aim, the upper bound must cover that discrete,
possibly discontinuous aim-to-seed map. The Gaussian/Minkowski probability bound applies
to a stochastic additive-offset model; it is not automatically a bound for an
aim-dependent deterministic seed lottery.

The result is exact per ray; optimality is certified only when the global gap closes.
There is no fixed small worst-case evaluation count for arbitrary geometry. A practical
initial budget of 50–250 objective evaluations for a few broad 2-D basins is an
**unverified engineering estimate**, not a guarantee. The existing 717-cell saved-set
measurement reports 34 ms for `argmax_hit_probability`; that already exceeds a 15.6 ms
tick before a certified global search.

One further scoring detail: `solve()` reports `result.value = field.value_at(aim)` even for
the `expected` objective, which selected the aim using `expected_exact`; `result.hitchance`
is likewise a separate functional. Do not use either reported value as if it were the
objective that selected the aim without making that contract explicit.

### 3. Keep the field as a diagnostic product, not the aim-path oracle

The heatmap/occlusion field remains useful for visual diagnosis and offline comparisons.
Do not let its centroid labels, per-hitbox overlaps, or candidate heuristic define the
correctness of the live ray score. Per-fragment AABB re-pruning is a reasonable temporary
acceleration for that diagnostic path, but it does not repair those semantic issues or
make the 24-solid cap exact.

## Probability, polygon numerics, and optimization

### Spread and pose

Under the `r ~ U[0,1]`, uniform-angle kernel, one radial component of scale `c` has
per-axis variance `c²/6`, hence the sum of independent spread and inaccuracy components
has `(S²+I²)/6`. If confirmed against the current binary, use
`sigma_axis = sqrt(S²+I²)/sqrt(6)` for a variance-matched Gaussian approximation. The
actual kernel remains compactly supported and non-Gaussian; variance matching does not
prove equal probabilities. The repository’s own unresolved `sqrt(r)` RE question changes
the variance, so settle that first.

Pose uncertainty should be a mixture over records, with a world-space covariance mapped
to each hitbox’s local angular tangent plane. Adding spread and pose covariance is valid
only under the matching independence/linear-Gaussian approximation. For deterministic
spread given aim and tick, score the actual computed pellet offsets; if the seed is
unknown, explicitly model a seed prior. For multiple pellets, expected total damage is
additive, but `P(any hit)` is a union probability and cannot be computed by summing
overlapping pellet masses.

### Gaussian polygon mass

For an isotropic 2-D Gaussian with center `a`, `r = ||x-a||`, and density
`φσ(r) = exp(-r²/(2σ²))/(2πσ²)`, define

```text
A(r) = ∫₀ʳ s φσ(s) ds = (1 - exp(-r²/(2σ²))) / (2π)
F(x) = A(r) (x-a) / r²
```

Then `div F = φσ(r)` and the polygon mass is

```text
M(P,a) = ∮∂P (1 - exp(-r²/(2σ²))) ((x-a)·n) / (2π r²) ds.
```

This confirms the requested `∮ g(r)((x-a)·n)/r ds` form with
`g(r)=(1-exp(-r²/(2σ²)))/(2πr)`. For an edge at signed distance `d` from `a`, parameterized
by tangent coordinate `t`, its contribution is

```text
d/(2π) ∫ [1 - exp(-(d²+t²)/(2σ²))] / (d²+t²) dt.
```

Convexity gives a simple `O(edges)` boundary, not a universal elementary closed form.
The changing aim prevents a single precomputed mass; edge frames and a normalized 1-D
kernel table can be cached. For composite midpoint quadrature with edge length `ℓ`, step
`h`, and `M₂ = sup |q''|` for the edge integrand, a rigorous bound is

```text
|error_edge| ≤ |d|/(2π) · ℓ h² M₂ / 24.
```

Sum the edge bounds and separately account for floating-point and projected-geometry
error. The GL-6/GL-8 errors in `aim-objective-math.md` are **measured on sampled polygons**,
not a uniform proof for all inputs. Keep the existing boundary-flux gradient unless a
replacement’s derivative is independently derived and tested; the current yaw-dependent
covariance also needs its missing pitch chain-rule term.

### Global optimizer

One convex cell’s Gaussian mass is log-concave in the mean, but a weighted sum need not
be. The repository’s 459-cell test found two basins with an 8.9% value gap. Use
multi-start or branch-and-bound, not one ascent trajectory. The existing useful bound
`J(a) ≤ Dmax · P(hit at a)` is rigorous only when `P(hit)` is the correct union/nearest-hit
probability; current independently appended overlapping hitboxes do not establish that
condition. A tighter bound for an aim cell comes from the expanded target regions above.

## Migration and acceptance tests

1. **Reconcile instrumentation and caps.** On one saved scene, report the same workload’s
   solve count, hitboxes, occluders, fragments/cells, and p50/p95/p99 timings. Resolve the
   conflict between `ragebot-perf.md` “no caps” and `service.cpp`’s 24 cap. Add counters for
   every geometry/parser limit and fail the test if any is reached silently.
2. **Do not ship per-fragment AABB re-pruning on the aim path yet.** Although an AABB miss
   proves the polygon intersection empty, skipping a spurious split changes fragment
   centroids, and those centroids determine the sampled damage values. No error bound
   versus the continuous damage field is established. Instead, the current patch prepares
   each occluder’s clip-edge planes once and reuses them for its fragments; this avoids
   repeated normal construction while preserving the exact fragment vertices. Benchmark
   it before claiming a speedup.
3. **Validate complete per-ray interval semantics.** Compare custom/engine stacks on
   brushes, compound shapes, mesh clusters, start-inside rays, coplanar edges, and dynamic
   props. Require zero unexplained interval/material mismatches before removing caps.
4. **Validate the per-ray damage evaluator.** Range falloff is now applied on free-flight
   segments and hitgroup scaling after walls in the host model. Verify wall-count ordering,
   exit-distance trace gating, and multi-surface modifier reduction; compare final damage
   against the current live engine build.
5. **Replace aim-path arrangement with adaptive exact-ray search.** First ship a fixed
   candidate shortlist as an incumbent generator; then add conservative cell upper bounds
   and report the remaining optimality gap. Keep arrangement generation on the diagnostic
   heatmap path.
6. **Repair the uncertainty semantics.** Resolve radius transform, seed mapping, inaccuracy
   units, pose records, overlapping hitboxes, and pellet patterns. Compare empirical
   seed/pose outcomes with probability estimates on saved fixtures.
7. **Delete caps only after completeness tests pass.** Removing `max_occluders=24` while
   leaving an 8,192-shape, 256-body, mesh, or compound-shape omission is not a complete
   migration.

## Falsifiable predictions

- Removing arrangement construction from `solve` eliminates `occlude_us` growth with
  fragment count; the new dominant term is measured full-stack ray traversal. Sweep ray
  counts and publish trace-time distributions. If the exact candidate path still exceeds
  budget at the required search resolution, the “rays are cheap enough” prediction fails.
- Two rays inside one stored `DamageFragment` will sometimes produce different spans or
  damage when a wall is oblique; if not, the centroid-constant objection is falsified for
  those fixtures, not universally.
- Two overlapping projected hitboxes will make summed Gaussian mass differ from the
  union mass; if all actual candidate cells are disjoint in a representative corpus, the
  overlap issue is inactive there but remains an unenforced invariant.
- A mesh cluster with multiple/open surface crossings can produce an incorrect
  first-two-hit span; compare against the engine’s full intervals. If no such mismatch is
  found over adversarial and live mesh fixtures, the mesh concern is reduced, not proved
  away by BVH-vs-flat equality.
- If current-build spread code has no radial `sqrt`, the observed per-axis variance should
  converge to `(S²+I²)/6`; if the `sqrt` exists, it should converge to the corresponding
  area-uniform value. Fire-decal measurements and RE must agree.
- Pitch finite differences will disagree with the ascent’s current analytic gradient
  where `yaw_scale` varies; fixing the chain rule should reduce that discrepancy.
- A local optimizer should miss the known secondary basin on the saved two-basin fixture
  from at least some starts; a certified branch-and-bound run must either locate it or
  retain an upper bound above the incumbent.

## References

### Project evidence

- [`geometry-algorithms.md`](geometry-algorithms.md) — fragment scaling, BVH measurements,
  raster tradeoffs, manual-map constraints, and the earlier candidate-ray proposal.
- [`aim-objective-math.md`](aim-objective-math.md) — spread kernel, polygon mass, pose,
  optimizer experiment, pellet semantics, and open RE questions.
- [`autowall-implementations.md`](autowall-implementations.md) — public trace architectures
  and the distinction between first-hit and stack queries.
- [`ragebot-perf.md`](../ragebot-perf.md), [`ragebot-review.md`](../ragebot-review.md),
  [`re/bullet-trace.md`](../re/bullet-trace.md), [`re/penetration.md`](../re/penetration.md).

### External literature / primary docs

- Pocchiola & Vegter, *The Visibility Complex*: https://doi.org/10.1145/160985.161159
- CGAL, *2D Arrangements*: https://doc.cgal.org/latest/Arrangement_on_surface_2/index.html
- Hasselgren, Andersson & Akenine-Möller, *Masked Software Occlusion Culling*:
  https://www.highperformancegraphics.org/wp-content/uploads/2016/HPG2016_MSOC.pdf
- Intel MaskedOcclusionCulling (archived): https://github.com/GameTechDev/MaskedOcclusionCulling
- Di Donato, Jarnagin & Hageman, *Bivariate normal integral over arbitrary polygons*:
  https://apps.dtic.mil/sti/citations/ADA102466
- Sommariva & Vianello, *Product Gauss cubature over polygons based on Green’s integration
  formula*: https://doi.org/10.1007/s10543-007-0131-2
- Jones, Perttunen & Stuckman, *Lipschitzian optimization without the Lipschitz constant*
  (DIRECT): https://doi.org/10.1007/BF00941892

## Review status

The prepared-clipper overload is used by `damage.cpp`; `aim-occlusion` verifies that the
prepared path preserves fragment topology, vertices, and area. `penetration_damage_at_target`
now applies free-flight falloff around walls, checks wall exit against the trace gate, and
applies hitgroup scaling after penetration. The yaw-scaled Gaussian ascent includes the
covariance chain rule. A capped solve now reports an incomplete per-hitbox candidate list
and fails closed; the heatmap exposes the same scoped completeness flag. Host tests cover
these changes and pass 17/17 in `build-review`. The saved-scene benchmark and live A/B have
not been run.

The current Steam `client.dll` SHA-256 was verified as
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`, matching
`docs/re/bullet-trace.md`. `open_binary` accepted that artifact, but the subsequent
`analyze_function` call failed with a Ghidra bridge handshake identity error after the
headless analysis log reported completion. `rea doctor` reports stale/missing provider
registration configuration; I did not run `rea setup` or restart OpenCode because those
change global client configuration. Current-build differential validation is blocked.
Next: repair the REA provider configuration, verify the full stack and penetration count,
then propagate enumeration/parser completeness before removing the caps.
