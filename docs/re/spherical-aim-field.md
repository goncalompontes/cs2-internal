# Spherical damage and probability field

The rage path now evaluates a damage field over directions and applies a weighted shot/pose kernel. It does not use the highest raw-damage silhouette's center as a substitute for hitchance. This is a functional heatmap evaluated on demand, not a screen-space image or an angular pixel raster.

## Implemented

1. **Silhouette proposals.** OBBs and studio capsules project into tangent-plane charts. Sphere endpoints use their actual3D tangent cones, preserving off-axis conics. Each target gets a chart centered on it, so target selection can cover enemies behind the camera without forcing an entire sphere into one flat chart.
2. **Depth-correct field.** Rays intersect the real capsule/OBB geometry; the first intersected surface wins. Range falloff uses that surface's distance. A high-multiplier hitbox hidden behind another hitbox cannot contribute its damage. Long-range quadratic intersections use double intermediates to avoid float cancellation turning misses into hits.
3. **World correction.** Native OBB volumes produce ordered entry/exit intervals; geometry beyond the target is ignored. World silhouette boundaries split proposal regions, including narrow visible windows. Opaque volumes block. Optional supplied transmission factors use actual ray thickness. A calibrated world-damage callback can replace that material approximation completely.
4. **Kernel evaluation.** Every offset forms `normalize(forward + x*right + y*up)` in the candidate aim's local basis, then queries the same physical field. Weights are normalized. Expected damage, probability of positive damage and probability of meeting minimum damage are distinct outputs. Pose uncertainty is an independent Gaussian stencil, not a replacement for weapon spread.
5. **Selection/refinement.** Every projected component gets seeds. Known-offset compensation can propose an aim outside the unspread silhouette. Minimum-damage/probability constraints are enforced before accepting a result. Optional completeness requirements reject unknown spread/world models. Proposal search uses a smaller kernel by default; finalists, local correction and final acceptance use the entire supplied kernel. This is an approximate multi-start optimizer, not a guaranteed global maximum.
6. **Live rage wiring.** All live enemies are compared, rather than selecting the nearest enemy before solving. Other players' geometry is cached per snapshot and acts as opaque capsule blockers in the fallback model. A calibrated complete world backend handles player interactions itself. Autostop remains in the command path; its movement logic and silent aim are separate. The selected target's armor/helmet and weapon range/armor ratio are propagated.

## Relationship to the recovered binary

The verified source behavior at `0x526290` evaluates8 batches of8 directions and normalizes count/damage aggregates by1/64. The separate worker at `0x51D320` evaluates damage for64 spread directions; selected candidates get refinement and rechecks. The new implementation reuses that direction construction, separation of damage/probability, threshold gates and final recheck, while using the requested field representation and native analytic geometry.

The binary's two middle probability outputs, exact comparator order, original sample-buffer key provenance and full material-loss callbacks are not all recovered. The new API uses explicit probability meanings; it does not rename unknown source counters as verified hitchance. Its material transmission law is not presented as the binary's penetration algorithm.

## Model and backend contracts

`payload/aim/ballistics.hpp` exposes analytic intersections, weighted kernels and two callbacks. `Service::set_aim_damage_queries(world, target)` publishes validated backends under the same model mutex used by configuration.

- World callback receives eye, unit direction, target distance and weapon. It returns damage after all intervening interactions/range loss, before the final target/hitgroup calculation, plus whether coverage is known. An unknown response produces no invented damage.
- Target callback receives pre-target damage, hitbox, weapon and target defense/identity and returns final target damage. This can replace the fallback armor model.
- Native volume fallback: opaque unless explicitly supplied a retention-per-unit factor. The illustrative law is `damage *= retained_per_unit^(thickness / penetration)`. This is a configurable material model, **not verified CS2 penetration**. Overlapping transparent volumes apply independently.
- Fallback target armor is a stated standard-armor model; heavy armor and build-specific server rules need a calibrated backend.

An empty kernel means an unspread estimate, with `spread_known=false`. Explicit spread/inaccuracy or offsets describe a supplied model. `spread_known=true` means the model was supplied; it does not prove that a current command seed or live inaccuracy was recovered. For a genuinely known single-projectile offset, supply one kernel node. A list of nodes is a weighted distribution of possible projectile offsets, not a correlated multi-pellet shotgun volley.

The generated64-node spread profile integrates two uniform-radius/angular components using deterministic quadrature. It is neither a Gaussian approximation nor the engine's seeded RNG. Revolver/Negev/shotgun-specific patterns need explicit offsets or a correct pattern provider. `sigma` is an additional small-angle pose model; it is not a full lag-compensation/record reconstruction.

## Controls

Both `aim` and `rage` accept:

| Argument | Meaning |
|---|---|
|`spread`, `inaccuracy`|Nonnegative tangent-plane spread magnitudes; generates64 weighted offsets.|
|`kernel`|`[[x,y,weight], ...]`,1..256 nodes; weight defaults1. Replaces generated offsets.|
|`spread_known`|Caller-declared model availability; explicit profile/offsets default true.|
|`min_damage`|Damage threshold for the qualifying-probability field.|
|`min_probability`|Required probability of meeting that damage threshold,0..1.|
|`require_spread`|Reject solutions when no spread model was supplied.|
|`world`|Up to128 volumes: `{mins:[x,y,z], maxs:[x,y,z], retained_per_unit:0}`. Opaque by default.|
|`world_complete`|Caller assertion that supplied scene coverage is complete. Replacing `world` resets this to false unless set explicitly.|
|`require_world`|Reject solutions without known coverage for evaluated target-hit rays.|
|`search_samples`|Proposal-kernel size,1..256; default8. Use64 for full64-node proposal evaluation at additional cost. Final acceptance always uses all supplied offsets.|
|`sigma`|Independent small-angle Gaussian pose uncertainty.|
|`objective`|`expected` (new default), `hitchance` (qualifying probability), or `damage` (nominal damage).|

Example model configuration (values must come from the desired weapon/state; these numbers are illustrative):

```json
{"id":1,"cmd":"rage","args":{"objective":"expected","spread":0.005,"inaccuracy":0.01,"min_damage":40,"min_probability":0.8,"require_spread":true}}
```

A scene known to be clear, or a complete supplied geometry scene, must be explicitly identified. The code does not mark an absent live world reader as a clear map:

```json
{"id":2,"cmd":"rage","args":{"world":[],"world_complete":true,"require_world":true}}
```

Do not use that empty-scene assertion unless the applicable shot region is actually clear. A real game integration should provide complete geometry or the validated world callback.

Responses now include `expected_damage`, `hit_probability`, `damage_probability`, `world_known` and `spread_known`. The HUD shows expected damage/qualifying probability and whether a model was supplied. Nominal `value` can be zero for a valid known-spread compensated shot; `expected_damage` is the relevant weighted score.

## Verification and performance

Run `scripts\test-rage-field-windows.cmd` (MSVC x64). Root Meson also registers `aim-heatmap`, `aim-options` and `aim-target-charts`, alongside the existing aim suites.

Verified tests cover the existing geometry/field/solver behavior plus: front/back hitbox ordering, opaque walls, walls behind targets, oblique interval thickness, callback overrides/unknown data, weighted probabilities, minimum-damage and probability rejection, broad body versus narrow high-damage head, exact known-offset compensation outside silhouettes, off-axis conics, narrow windows, long-range miss precision, player blockers, full-kernel final metrics, conservative culling against unculled intersections, separate pose kernels, rear-target chart construction, defense identity and transactional configuration.

The optimized local synthetic benchmark (MSVC `/O2 /fp:strict`,19 capsules,64 offsets, no world volumes or pose blur,5 solves) averaged about4.2ms per solve, down from about65ms before culling/coarse proposal search. This is not an in-game frame-time guarantee. Multiple targets, world partitions, pose stencils and engine callbacks add cost. Default proposal budgets are4096 seeds,256 partition regions,8 refinement starts and6 coarse iterations, followed by full-kernel finalist correction/recheck. No infinite/local-only retry loop is used.

## Still unverified or missing

The live checkout still lacks generated calibration/SDK artifacts. A complete calibrated DLL build and in-game validation have not been performed. The changes were compiled and tested as host math/adapter code; modified Service/reader translation units were separately compile-checked.

No automatic live map/surface reader, calibrated CS2 material-loss backend, per-command exact seed provider or measured live inaccuracy provider has been installed. Without those, world correction and spread remain supplied/estimated models, clearly flagged in results. Autostop does not automatically update a manually supplied inaccuracy profile. The solver must not be described as knowing exact server damage or hit probability under those missing inputs.
