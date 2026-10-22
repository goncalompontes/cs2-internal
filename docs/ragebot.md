# Ragebot — design & direction

> Status: **design / direction locked (ADR-0022)**. The aim core is implemented
> (`payload/aim`, host-tested); live wiring is in progress.
> RE it depends on: `docs/re/aim.md` (client.dll shot model),
> `docs/re/lagcomp.md` (backtracking) and `docs/re/vphysics2.md` (collision
> geometry).

## 0. Implementation status

- **`payload/aim/`** — the aim core, platform-independent and host-tested:
  - `geometry` (the angular chart, OBB/sphere/**capsule** projection, hulls,
    clipping, the nearest-point and Chebyshev-centre optima) — test `aim-geometry`;
  - `field` (the damage field: cells, the weapon value model, the pose-blurred
    maximum) — test `aim-field`;
  - `hitbox` (bone/AABB → oriented box; **the bone transform → world capsule**) and
    `solver` (build the field, pick the aim) — test `aim-solver`;
  - `reader` (the game bridge: snapshot → solver input, reading the **real studio
    hitbox set** — see below);
  - `records` (the backtrack record ring + the rewind-tick formula) — test
    `aim-records`.
- **The real studio hitboxes are in use (`docs/re/aim.md` §"The runtime studio
  hitbox set").** `read_model_hitboxes` walks pawn → scene node → `CModelState` →
  `CModel` → render mesh → hitbox set → 19 **capsules** (bone-local endpoints +
  radius), resolves each box's bone through `CModel::GetBoneIndexForHitboxForMesh`
  and transforms the endpoints by the bone (`{pos, quat}`, stride `0x20`). The
  hitgroup (`m_nGroupId`) supplies the multiplier (head = `m_flHeadshotMultiplier`).
  **The collision-AABB head/body proxy remains only as a fallback** when the model
  cannot be resolved. Live: `aim-probe` — 19 boxes, solve within **0.02°** of the
  bot's head.
- **The weapon model comes from the game.** The solve uses the shooter's real
  `CCSWeaponBaseVData` (`weapon + 0x388` = `m_nSubclassID + 8`): `m_nDamage`
  (`+0x820`), `m_flHeadshotMultiplier` (`+0x824`), `m_flPenetration` (`+0x82c`),
  `m_flRangeModifier` (`+0x834`). The `aim`/`rage` commands' explicit args still
  win when passed. Live: `aim-probe` reports a Glock as `weapon_damage 50`
  (vs the old hard-coded 100).
- **Ragebot keybind — working.** The solve runs on the render thread (the
  overlay's per-frame snapshot sink has the local player + every enemy); the aim
  is stashed in `Input` and `Input::apply()` writes it each tick as the **silent**
  sub-tick `view_angles`, pressing `IN_ATTACK` while the key is held. Owned by the
  overlay menu's **Ragebot** tab (`wire::Settings.rage/rage_key/rage_fire`) and by
  the `rage` control command (`on`, `key`, `fire`, `force`, `objective`, …).
  - **The auto-fire timing bug is fixed.** The attack is now asserted
    **persistently** (in `apply()` step 1, backend-independent) for as long as a
    solve is active, so *every* command the engine simulates carries the attack
    together with the silent angle that tick — the same reason the manual
    `press("attack")` path always hit. Previously the press was only applied on
    the command `rage_tick` happened to see (the slot-25 `create_move` **copy**
    vs the slot-5 simulated command), so a shot could be released on a command the
    silent write had not reached.
- **Hitbox renderer — off by default, needs a live re-check.** The overlay draws each
  live enemy's real hitbox capsules (coloured by hitgroup, rage target highlighted) on
  the render thread. The draw is now the **true silhouette**: a capsule is the convex
  hull of its two end spheres, so the tangent circle of each sphere as seen from the
  eye is built in 3D, sampled and projected, and the two are hulled in screen space —
  an honest pill with the correct tangents and per-endpoint perspective (it replaces
  the previous screen-space stadium approximation). The read path's degenerate cases
  are fixed too: a box whose bone cannot be resolved is skipped instead of leaving its
  endpoints at the origin, and a mesh with no hitbox-set table is skipped instead of
  being read as an inline array. `EspConfig::hitboxes` is still **off** and has **not
  been re-validated live** — toggle it from the menu's ESP tab and compare against the
  engine's own `sv_showplayerhitboxes`. See `docs/re/aim.md` §"The runtime studio
  hitbox set".
- **`aim` control command** — solves and returns the aim direction/value and the
  raw hitbox list; with `write: true` it also applies the solve to the silent
  sub-tick view angles.
- **Remaining:** occlusion/penetration (the engine trace — RE pinned, see
  `docs/re/aim.md` §"Calling the trace"), wiring the backtrack ring into the
  per-frame path, the pose blur, and the hitbox renderer.



## 1. Goal

A ragebot that picks the aim direction maximising **damage** and **reliability**
for a given shooter position and enemy hitboxes, using the real weapon model and
real world geometry — without the cost of a per-candidate × per-pellet trace
storm.

## 2. The model (what is actually true)

From `docs/re/aim.md`:

- A shot fires `K` pellets (usually 1). Each pellet direction is
  `f(a) + o_k·(right, up)` where `a` is the aim and `o_k` is a **deterministic**
  offset generated by `RandomSeed(cmd_seed)` (seed = `MD5_PseudoRandom(sequence)`).
- A pellet's damage is a deterministic ray integral:
  `m_nDamage · μ_hitgroup · m_flRangeModifier^(L/500) · Π_walls (1 − t_w·penMod/m_flPenetration)`,
  0 if blocked.
- The **only** genuine random variable is the target's pose at the shot tick
  (lag-compensated rewind vs. the interpolated pose you see).

**Consequence.** "Hitchance over 256 spread samples" is integrating a delta —
there is nothing random in the spread. The decision is not an integral, it is a
**registration / optimisation** problem.

## 3. The decision problem

Define the **damage field** `D(u)` over world directions `u` from the eye: the
damage a bullet fired along `u` deals (after walls, armour, hitgroup). Choosing
the aim is

```
maximise_a   Σ_k  D(a + o_k)          (damage)
             subject to  p_hit(a) ≥ H (reliability),  or  maximise P(kill)
```

`D` is **aim-independent** — build it once; every aim is a lookup. Multipoint is
the special case `K=1`, `D` binary-visibility, no penetration.

### Structure of `D`

- Directions hitting hitbox `i` form its projection `Ω_i` (a convex spherical
  polygon; a disk for a sphere/capsule head).
- Occluders cut `Ω_i` into cells; inside a cell `D` is smooth and ≈ constant
  (`μ_hitgroup`), varying only by mild range falloff and wall thickness.
- So `D` is **piecewise**, with cells bounded by the projected silhouette edges
  of occluders. The heat map is a *small set of polygons with values*, not a
  raster.

### Optima (exact, finite)

- **Max damage** = a cell's point nearest the eye.
- **Max hitchance** = a cell's Chebyshev (largest-inscribed-circle) centre.
- **Robust combined** = maximise the pose-blurred field
  `Ê = Σ_cells value · blur(indicator)`; the pose kernel is small, so this is a
  closed-form blur of the polygons (+ 1–2 Newton steps).

## 4. Tiers

### Tier 1 — few-trace exact (no raw geometry)

1. Project hitboxes → `Ω_i` (analytic).
2. Probe `Ω_i` vertices + centroids with one `TraceShapeWithSortedResults`
   (or `TraceLineIntervals`) each — the trace returns the **whole sorted layer
   stack** in one call, so penetration is `O(1)` calls.
3. Where the value changes between adjacent probes, reconstruct the occluder
   edge from its reported entry/exit points (faces are planar → 3 points pin a
   plane) → the exact boundary.
4. Assemble cells → nearest point / Chebyshev centre / blurred max.

Cost: tens of calls. Fallback: bisect a straddle to tolerance.

### Tier 2 — zero-trace geometry (the chosen direction)

Read the collision hulls/meshes directly (`docs/re/vphysics2.md`):

```
cone query → shape*;  type = *(int*)(shape+0x18)
type 2 (hull):  hull = *(RnHull_t**)(shape+0xC0);  verts = *(Vector**)(hull+0x78), n = *(int*)(hull+0x70)
type 3 (mesh):  mesh = *(RnMesh_t**)(shape+0xC8)
type 4 (compound): recurse
```

Project the hull edges/faces → exact cells + thicknesses → the analytic heat
map. **No per-ray traces at all.**

## 5. Decision (ADR-0022)

**Go with Tier 2 as the target, with Tier 1 as the shipping fallback.**

- Tier 1 works **today** (only client.dll RE needed) and yields an exact
  piecewise heat map with tens of traces — enough to ship and validate.
- Tier 2 is the end state: confirmed **feasible** (`docs/re/vphysics2.md`), and
  **cheaper than tracing** (one enumeration + planar geometry vs. hundreds of
  BVH traversals), but it needs a bounded RE pass (finish `RnHull_t` arrays +
  signatures) and is build-fragile.
- Both share the same heat-map assembly, so Tier 1 is not throwaway work.

## 6. Performance

| | naive multipoint + hitchance | Tier 1 | Tier 2 |
|---|---|---|---|
| spread | 256 samples | `K` exact (1–9) | `K` exact |
| aim search | grid × `K` traces | tens of full-stack traces | 0 traces |
| penetration | re-walked per ray | `O(1)` per trace (sorted stack) | per-cell 1-D reduction |
| world cost | per candidate per tick | tens of traces, cacheable | one enumeration + projection, cacheable (static world) |
| per-tick | tens of ms | ~0.1 ms | µs |

**Do not build a per-pixel world raster.** The geometry is piecewise; the
optimum lies in a finite candidate set (nearest-visible points, arrangement
vertices, Chebyshev centres). Sampling a dense grid is strictly worse.

## 7. Pose uncertainty

Model the target's rewound pose as a small translation distribution `q` (width ≈
displacement / distance). It moves the *target* sub-field while the world field
is fixed → `Ê` = the target mask convolved with `q` over the layer stack. Report
the Pareto set `(p_hit, Ê)` (or `argmax Ê s.t. p_hit ≥ H`, or `P(kill)`), so the
razor-thin head sliver vs. the solid chest trade is decided exactly.

## 8. Backtracking (lag records)

The pose uncertainty of §7 is realized by the target's **lag records**: the
server rewinds the target to a past tick for hit registration, so the ragebot
can fire at a *past* pose within the lag-comp window (`docs/re/lagcomp.md`).

- **Records.** Each tick, snapshot each enemy's hitbox OBBs + `m_flSimulationTime`
  + tick into a ring buffer sized to the window.
- **Window.** The server's is authoritative (`CLagCompensationManager`,
  `docs/re/lagcomp.md`): it rewinds to the **command's target time clamped to
  `sv_maxunlag` / `sv_maxunlag_player`** and keeps `EntityLagData` records
  (`0x480` B, tick `+0x34`). The client predicts the same tick
  (`FUN_1808a0390`); size the ring to ~`sv_maxunlag` (~0.2 s ≈ 13 ticks @ 64).
- **Selection.** Evaluate the heat map for each valid record; choose the record
  maximizing damage / reliability. This *is* the backtrack aim — the discrete
  record set replaces the continuous pose distribution, with the interp/
  extrapolation residual as the only continuous part.
- **Validation.** The server reports its rewind vs. the client's
  (`CUserMessageLagCompensationError`), so the rewind model can be calibrated.

## 9. Validation

The control harness already does silent sub-tick aim + fire and reads health
drops (`docs/control-harness.md`, `subtick-shot`). Use it as ground truth: sweep
aim × weapon × distance × target motion, fire, observe hit/damage; check the
heat map's predicted `(p_hit, damage)` against observed (calibration, not just
"did it hit"). Seed the RNG locally (replicate `CUniformRandomStream`, or
save/restore the engine stream) to keep predictions exact.

## 10. Next actions

1. **Hitbox renderer (live re-check).** The draw was rewritten to the true capsule
   silhouette (§0) and the read path's degenerate cases fixed (unresolved-bone boxes
   skipped; no-hitbox-set meshes skipped). Re-run the close-up comparison: project the
   same boxes externally (`aim-probe`'s `hitboxes[]` carries bone-local and world
   centres) and compare pixel for pixel with the drawn image. `EspConfig::hitboxes`
   stays **off** until it lines up.
2. **Occlusion / wallbang (Tier 1).** The engine trace call convention is pinned
   from a live call site (`docs/re/aim.md` §"Calling the trace from the payload"):
   `TraceShapeWithSortedResults` is
   `(iface, <rdx>, start, end, filter, &results)` called on the singleton at
   `*client_base + 0x221f058`, with the filter built inline (vtable
   `client_base + 0x1ace848`, mask `0x1c1003`) and a static default at
   `+0x221f070`. Still needs: a dumper **signature** for the singleton + the trace
   function (ADR-0012), the `Ray_t`/`CGameTrace` layouts, then live validation
   (a wrong call faults the process — the payload has no exception tables). A
   first step is per-hitbox **visibility** (one trace eye → hitbox centre; drop
   fully-occluded boxes).
3. **Weapon profile polish.** `m_flRange` / `m_nDamage` are wired; the pellet
   count (`K>1`), armour ratio and `m_WeaponType` could join `WeaponProfile`.
4. **Tier 2:** map the overlap record → shape pointer; add build signatures for
   `shape+0x18`/`+0xC0` and the `RnHull_t` offsets (`docs/re/vphysics2.md`).
5. **Backtracking:** the per-enemy record ring (hitbox snapshots per tick) and
   the lag-comp window/selection (`docs/re/lagcomp.md`).
6. **Shared:** pose-uncertainty model; the analytic polygon-blur.

## 11. Non-goals

- No dense angular raster / per-pixel world buffer (costs more than it saves).
- No spread-cone sampling (the cone is deterministic).
- No learned model (the inputs are computable; there is nothing to learn).

## Recovered autostop integration

The per-command rage path now includes grounded and airborne stopping with fractional movement events. Implementation, controls, estimated physics inputs and verification limits are documented in [re/autostop.md](re/autostop.md). Autostop runs only while a rage solve is active; it does not add a firing delay.


## Spherical damage/probability field implementation

The solver now uses weighted aim-local kernels, depth-ordered target/world intersections, probability constraints, target-centered charts and all-enemy selection. See [re/spherical-aim-field.md](re/spherical-aim-field.md) for controls, source-binary evidence, backend contracts, tests and the remaining live calibration gaps. The old highest-cell/Chebyshev shortcut is retained only as a geometric proposal helper, not as the production definition of hitchance.


## Current native activation: menu toggle

As requested on 2026-10-17, the Ragebot checkbox activates aiming and autostop continuously; holding X is no longer required. Auto Fire remains a separate checkbox. Turning Ragebot off cancels input activation even when a solve is already running. Legacy key fields are retained for compatibility and do not gate this mode. Target, data-quality and freshness checks still apply.
