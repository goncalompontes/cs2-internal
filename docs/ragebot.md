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
- **Hitbox renderer — three modes, menu-selectable.** The overlay draws each live
  enemy's real hitbox capsules on the render thread (`EspConfig::hitboxes`, menu → ESP
  → *Hitboxes (studio)*), in one of **Outline** (the true silhouette as a closed
  polyline: a capsule is the convex hull of its two end spheres, so the tangent circle
  of each sphere from the eye is built in 3D and the two are hulled in screen space),
  **Solid** (that silhouette filled — `wire::DrawKind::filled_poly` →
  `AddConvexPolyFilled`), or **Wireframe** (the capsule mesh cage: rim/cap rings plus
  longitudinal lines, back-facing edges dimmed). Colours are either the **automatic
  per-hitgroup** scheme (the head stands out; the rage target is highlighted) or a
  **custom** RGBA picked in the menu. The read path's degenerate cases are fixed too: a
  box whose bone cannot be resolved is skipped instead of leaving its endpoints at the
  origin, and a mesh with no hitbox-set table is skipped. Offline reference:
  `scripts/hitbox-view.py --style solid|wire` renders the same capsules from the live
  `aim` data. Compare with the engine's own `sv_showplayerhitboxes`. See
  `docs/re/aim.md` §"The runtime studio hitbox set".
- **`aim` control command** — solves and returns the aim direction/value and the
  raw hitbox list; with `write: true` it also applies the solve to the silent
  sub-tick view angles.
- **Multi-target damage heatmap (Layer 1).** `payload/aim/heatmap.hpp`
  (`build_heatmap`) projects **every** player's hitbox capsules — and their sampled
  **lag records** — into one shared angular chart about the eye, tagging each
  silhouette cell with its player (`target`), hitgroup, record id and age, and
  valuing it by damage (host test `aim-heatmap`). The `heatmap` control command
  returns the tagged cells with the eye/basis/weapon; the Python side rasterises
  them (`scripts/scenario/heatmap.py` + `scripts/heatmap-view.py` + the `heatmap`
  scenario) to a **damage** PNG and a **record-age** PNG. The render thread samples
  each enemy's capsules per frame into `aim::RecordStore` (a bounded 32-entry ring,
  `sample_records`), so the per-player lag records are always warm. The world
  `occluders` slot is wired through `HeatmapOptions` but nothing fills it yet.
- **Occlusion / penetration (Layer 2, in progress).** `payload/aim/occlusion.hpp`
  (`OccluderCell` + `thickness_along`, `project_occluder`, `clip_convex`/
  `subtract_convex`) projects the world solids into the chart; `build_heatmap` then
  **splits each hitbox silhouette** by them and scales every fragment by
  `Π(1 − t(u)·mod/m_flPenetration)` — **no rays** (host test `aim-occlusion`). The
  payload enumerates the solids with `CGamePhysicsQueryInterface::EntitiesInBox`
  (`payload/control/occlusion_query.cpp`, the `walls: true` arg), wrapped in
  `diag::call_guarded` — a setjmp/longjmp recovery guard, because the manually-mapped
  payload has no exception tables (a wrong engine ABI faults back instead of killing
  the process). **The record → shape chain is resolved live** (`docs/re/occlusion.md`):
  the record's `+0x58` is a **`CPhysicsBody`**, `body+8` is its data, `data+0x68` the
  shape array (`data+0x60` the count) of `IPhysicsShape`s (`+0x18` type 2 hull,
  `+0xC0` `RnHull_t*`). Entity bodies get the world transform from
  `body->vtbl[0x1b0]`; the **world body** (handle `0x8000`, 10149 brush shapes) is
  cached and AABB-filtered. Live: `hulls=4 occluders=3`. **Remaining:** confirm the
  world-brush hulls are world-space (per-shape transform / `RnCompound_t` recursion),
  signatures (ADR-0012), and the per-direction thickness refinement.
- **Occlusion correctness pass (2026-10-17, second session).** (a) The heatmap's
  **broad phase** was wrong — it kept a solid only when the ray to the hitbox
  *centre* entered it, dropping solids that cover only *part* of a silhouette. It is
  now a true overlap broad phase (silhouette overlap ∧ a **conservative** depth lower
  bound, `OccluderCell::lower_bound_distance`), with the exact per-fragment
  `ray_span` test deciding attenuation (new `aim-occlusion` regressions). (b) The
  world-hull cache is now keyed on the level's geometry identity (body data + first
  shape pointer), so a map change cannot serve stale brush hulls. (c) The per-query
  RE scaffolding (`offset_type_hits`/`deep_type_hits` and the wide per-record
  `record_dump` string building) is gone — it allocated per record every tick.
  (d) The payload now reads **sphere (type 0), capsule (type 1) and mesh (type 3)**
  collision shapes from the game (`shape+0xB8`/`+0xC4`/`+0xD0` for the primitives;
  `shape+0xC8` → `RnMesh_t` vertices `+0x38`/count `+0x30` for meshes —
  `docs/re/vphysics2.md`) and projects them: spheres/capsules analytically
  (`project_sphere_occluder`/`project_capsule_occluder`) and meshes as the exact
  convex hull of their vertices (`convex_hull_3d`), all host-tested. A capsule- or
  mesh-bodied prop now occludes. **Compound (4)** shapes are still skipped (their
  `RnCompound_t` child layout is unresolved).
- **Bullet trace reversed; mesh occluders fixed (2026-10-18).** The engine's own
  bullet path was reversed with the `rea` plugin: `FX_FireBullets` (`FUN_180d237d0`)
  → per pellet `FUN_180887d60` (filter mask **`0x1c300b`**, filter vtable
  `client+0x1ad4838`) → `TraceLineIntervals` (`FUN_180a19e90`) and the penetration
  loop `FUN_18089ca80` (repeated interval traces, ≈90 units total). Full write-up:
  `docs/re/bullet-trace.md`. A live probe found the **missing wall is a collision
  mesh**: the worldspawn body's shape histogram is `{hull 10143, mesh 6}`, and the
  payload **skipped every mesh** (a 160-vertex cap + an O(n⁴) hull). Fixed: the
  payload reads meshes as a triangle soup (`RnMesh_t` vertices `+0x38`/count `+0x30`,
  triangle indices `+0x50`), `aim::project_mesh_occluder` projects them, and
  `OccluderCell::ray_span` intersects their triangles (Möller–Trumbore). The bullet
  mask was corrected to `0x1c300b`, and a mesh broad-phase guard keeps the chart from
  fragmenting on map-wide meshes. **Live-validated:** a wall between the eye and an
  enemy now yields head damage `0` (was full); an open line of sight is unchanged.
- **Real penetration model + a `penetration` render mode (2026-10-18).** The occluded
  cells were valued with the first-pass approximation `∏(1 − t·mod/m_flPenetration)`.
  `payload/aim/penetration.hpp` now implements the engine's real per-wall loss — the CS2
  `HandleBulletPenetration` (`client.dll` `FUN_1808a1820`, `sv_penetration_type 2`):
  `combined = min` of the crossed surfaces' `bulletPenetrationDistanceModifier`, with
  the material special cases, `lost = t²·(1/combined)/24 + (3/pen·1.25)·(1/combined)·3 +
  entry_fraction·damage` (`entry_fraction` 0.16, 0.05 through a grate/glass, 1e-5
  through a grate-contents span), a `< 0.1` distance-modifier gate, and the 4-wall
  budget + the reversed loop's ≈90-unit crossed-thickness cap (`FUN_18089ca80`).
  **The per-surface modifiers are read from the game** (`occlusion_query.cpp`
  `read_shape_surface`): each shape's material object is at `shape+0x48` → its `+0x10`
  indexes the `SurfacePropertiesList` (`CGameSurfaceProperties` 0x20 B,
  `FUN_1809e3560`/`FUN_1809cd960`), giving the distance modifier `+0x08`, damage
  modifier `+0x0C` and `gamematerial` (`CHAR_TEX_*`) `+0x14`. **Live-verified:** the
  world's 10149 brushes resolve to 29 distinct surfaces (concrete 0.5, metal 0.4, wood
  0.9, glass 0.99). `OccluderCell`/`OcclusionHull` carry the surface;
  `HeatmapOptions::penetration` selects `simple`/`engine`; `HeatCell::base_value` is the
  unoccluded damage. The `heatmap` command takes `pen`(+`pen_mod`/`dmg_mod`/`max_pen`/
  `material`/`grate` overrides) and the Python debug renderer gained a **`penetration`**
  mode (`scenario/heatmap.py :: render(mode="penetration")`, `heatmap-view.py --mode
  penetration --pen engine`, the live viewer's *penetration* mode) that fills by the
  engine-model damage, overlays the solids and outlines each cell by its attenuation
  fraction. Host test `aim-penetration` (16/16). Full decompile:
  `docs/re/penetration.md`.
- **The rage solve is occlusion-aware (2026-10-18).** `build_input` never filled
  `SolveInput::occluders`, so the rage path still aimed at fully-occluded hitboxes —
  only the `heatmap` command consumed the walls. The shared `occlude_polygon`
  (`payload/aim/damage.hpp`, extracted from `build_heatmap`) now splits a hitbox
  silhouette by the world solids and values each fragment with the engine penetration
  model, and `solve` uses it whenever `SolveInput::occlusion` is non-empty. The rage
  path projects the game-thread enumeration into the solve's chart (exactly as the
  heatmap does) and takes the weapon's real `m_flPenetration`; the `rage` command
  reports `occluders`/`backtracks`/`record`. Host test `aim-solver` gained the
  thin-wall-attenuates and `< 0.1`-gate-blocks cases.
- **A real hitchance + the weapon inaccuracy terms (2026-10-18).** `hitchance` was the
  Chebyshev-centre margin heuristic; it is now the **hit probability** — the fraction of
  the weapon's cone that lands on the target (`DamageField::hit_probability` /
  `argmax_hit_probability`, the same deterministic Gaussian quadrature as `expected`,
  `docs/ragebot.md` §2). The `CCSWeaponBaseVData` inaccuracy terms
  (`m_flInaccuracyCrouch/Stand/Move/JumpInitial/JumpApex` at
  `0x758/0x760/0x788/0x7B8/0x7BC`, `docs/re/aim.md` §"Inaccuracy") are read into
  `WeaponProfile`, and `aim::inaccuracy(...)` is the first-order stand/move cone (the
  engine's exact composition `FUN_18080b880` still needs re-reversing — the doc's RVAs
  are from the old build). The `aim` command takes `cone` (radians; defaults to the
  weapon's inaccuracy+spread) and reports `hitchance`/`cone`; the `hitchance` objective
  maximizes the probability when a cone is given. Host tests `aim-field`/`aim-solver`.
  **Next:** re-reverse the exact inaccuracy composition (+ the `weapon_air_spread_scale`
  convar, the jump initial↔apex `|v_z|` blend) and the shotgun/negev pattern table
  (`FUN_1807cd1b0`) on the current build, then live cross-check the pellet seed.
- **The ragebot is usable (2026-10-18).** Fleshed out end to end: **target selection**
  (nearest enemy within a configurable **FOV**, `docs/ragebot.md` §3), a **hitbox filter**
  (auto head-first / head-only / body-only), an **objective** selector (damage /
  hitchance / expected), and **min-damage / min-hitchance** gates on the aim and fire.
  The **effective inaccuracy is read live** from the pawn
  (`CsPlayerPawn::m_flInterpolatedInaccuracy`, client `+0x1F50` — the engine's own
  composition, so no formula), driving the cone for the hitchance objective and the
  pellet offsets. All of it is menu-owned (the overlay's `Ragebot` tab: objective /
  hitbox / FOV / min-damage / min-hitchance sliders) and mirrored on the `rage` control
  command; `rage` reports `objective`/`hitbox`/`fov`/`min_*`/`occluders`/`backtracks`/
  `record`. The solve stays occlusion-aware (`docs/handoff.md`). The solve projects only
  the solids whose cached AABB can lie between the eye and the target (the enumeration
  box spans every target, and projecting it in full on the publish thread froze the
  overlay once). The menu's settings block grew, so `wire::k_version` was bumped to 8 —
  a stale `cs2-overlay.shm` from an older layout must be deleted (or the version bump
  makes the layer ignore it).
- **The enumeration is camera-independent (2026-10-18).** The query box had been
  `eye → camera_forward·1400 ±96`, so the enumerated solid set (and therefore the
  render *and* the attenuation) changed as the player looked around. It is now the
  **AABB over the eye and every enemy's hitbox geometry** — the live pose *and* every
  sampled lag record — with an **8-unit** padding, built from world positions only.
  The hitboxes are the ones `sample_records` already read this frame, so the box needs
  no extra memory walk. **Live-validated:** the overlap `records` / `world_box_kept`
  (1909 / 1887) and the per-cell damage values are identical across camera yaws (only
  the chart window — an inherently view-relative projection — changes which
  solids/hitboxes fall inside it).
- **The chart is absolute world yaw/pitch (2026-10-18).** The angular chart (into which
  the hitbox and occluder cells are projected) was a gnomonic tangent plane about the
  **camera forward**, so the whole damage field rotated with the view. `aim::Basis` is
  now the absolute world yaw/pitch map — `(u,v) = (atan2(d.y,d.x), asin(d.z/|d|))` —
  a function of the world geometry only (`Basis::world()`; `basis_from_angles` is
  gone). The heatmap clips yaw to `±extent` (default the full `±π`) and pitch to `±π/2`.
  **Live-validated:** the projected `occluder_polygons` and the enumeration are
  byte-identical across camera yaws; the cells vary only with the enemy pose (idle
  animation), not the camera. The **render** is still centred on the camera direction
  (`view_yaw`/`view_pitch` in the JSON): the payload clips the chart to a `fov`-wide
  window around it (a view concern, not the computation), and the Python/JS renderers
  map absolute yaw/pitch to pixels with screen-right = decreasing yaw, so it looks like
  the in-game view. Follow-ups: a solid straddling the `±π` yaw seam over-covers, and
  the per-solid silhouette is a convex hull (a BVH / tighter silhouette is the perf
  step).
- **The render reports only the solids *between* the eye and a target (2026-10-18).**
  The enumeration box is a superset, so it also caught solids behind or beside the
  enemies, which the renderer drew. `Heatmap` now carries `used_occluders` — the subset
  that passed the per-hitbox broad phase (silhouette overlap ∧ in-front depth bound) —
  and `heatmap.walls.occluder_polygons` reports only those. **Live:** looking at an
  enemy, 652 solids projected into the view window but only **26** were between (and
  they attenuate: head 105). The remaining enumeration cost (walking the world body's
  shapes) is the `EntitiesAlongLinearSweep`/BVH step.
- **Deterministic spread / pellet model (2026-10-17, second session).**
  `payload/aim/spread.hpp` replicates the engine's `CUniformRandomStream` (Park–Miller
  + Bays–Durham shuffle) and `MD5_PseudoRandom(sequence)`, and `pellet_offsets(...)`
  draws the `K` pellet offsets (`spread·r1·(cos a1, sin a1) + inacc·r2·(cos a2, sin a2)`,
  revolver `r→1−r²`); host test `aim-spread` (the MD5 vector and the RNG's first draws
  are pinned to an independent Python model). The field gained the **pellet-sum**
  objective `DamageField::value_at_pellets`/`argmax_pellets` and `AimObjective::pellets`
  (handled by `solve`), reachable from the `aim` command
  (`objective: "pellets"`, args `seed`/`sequence`/`spread`/`inaccuracy`/`pellets`).
  **Not live-validated:** the exact `MD5_PseudoRandom` input construction and the
  per-weapon inaccuracy (`FUN_18080b880`; `m_flSpread` is now read into the weapon
  profile) still need a live cross-check; the shotgun/negev pattern table
  (`FUN_1807cd1b0`) is not modelled.
- **Remaining:** wiring the recorded window into the *solve* (the heatmap reads it;
  `DamageField`/`solve` does not yet), the pose blur, and pellet-pattern
  registration.
- **Angle-correctness + filter audit (2026-10-19).** A full review against the Tier-2
  plan is in `docs/ragebot-review.md`. Fixed: (a) the chart's **yaw/pitch metric** —
  `project_sphere` is now the exact 3-D silhouette cone, the hitchance/pose kernels are
  anisotropic (`yaw_scale(pitch)`), and `argmax_damage` is the best cell's Chebyshev
  centre instead of the point nearest the chart origin (world `+X`), which had made the
  aim — and the fire gate — depend on the target's absolute bearing; (b) the **±π yaw
  seam** (`convex_hull_unwrapped`, a target due west no longer becomes a chart-wide
  hull); (c) the **bullets' collision mask** (`EntitiesInBox` reads the mask from
  `filter+8`, not its `contents` argument — the payload now passes a copy of the engine
  filter re-masked to `0x1c300b`, `docs/re/bullet-trace.md`); (d) the **mesh cap** that
  silently dropped the map's largest collision mesh (101097 verts) — the floor/terrain
  — now split into spatial clusters. New features: **auto-stop, auto-scope,
  auto-reload, only-visible** (`wire::Settings` v9, the `rage` command). Still open:
  the inaccuracy unit/scale (§2.4 of the review).
- **Global point selection, trigger modes, per-weapon configs (2026-10-19, second
  session).** (a) The point optima are now **global over the whole field**: `argmax_damage`
  maximizes damage then the local margin across **every** cell/fragment (not one
  best cell's own optimum), `argmax_hitchance` maximizes the margin among
  comparable-value cells, and `argmax_hit_probability` seeds from every cell. A new
  `polygon_margin`/`field_margin` supplies the robustness score. (b) The ragebot
  trigger is a mode (`wire::Settings.rage_mode`): **hold key / always on / toggle
  key**. (c) **Per-weapon configs** (`payload/control/rage_config.hpp`): a profile
  (objective/hitbox/mode/key/fov/min-damage/min-hitchance/assists/fire) per weapon
  designer name, applied when `per_weapon` is on, and **persisted** to
  `%LOCALAPPDATA%\cs2-internal\cs2-rage.cfg`, loaded at startup. Editable from the menu
  (weapon field + Save/Load/Delete) and the `rage` command
  (`weapon=`, `per_weapon=true`, `profile_save`/`profile_load`/`profile_delete`,
  `save`/`load`, `mode=hold|always|toggle`). (d) **Auto-stop / auto-scope fixed**:
  auto-stop now zeroes every channel the engine moves from (the command base and both
  button states, the raw `CCSGOInput + 0x270` axes *before* the engine copies them, and
  the engine's own held-button word); auto-scope/auto-reload mirror zoom/attack2/reload
  into the button-state *changed* word (a press edge) and into the engine held-button
  word. `wire::Settings` v10.
- **Predictive assists, speed-limited stop, recoil, weapon gate (2026-10-19, third
  session).** Auto-stop/auto-scope are decided **before** the trigger from target
  selection ("can deal damage"), so the player is already still/scoped before firing;
  auto-scope drives the engine's own `+zoom`/`-zoom` via `Cbuf` (the command button mask
  does not reach the weapon zoom — measured live). Auto-stop is a **counter-strafe down
  to `rage_stop_speed`** (default 34% of the weapon's max speed; 0 = full stop). **Recoil
  compensation** subtracts `m_pAimPunchServices -> m_pPredictableBaseAngle` from the
  silent aim. The silent aim also writes the command's own `viewangles` (the fallback the
  shot uses when the sub-tick history has no entry) — the intermittent "fires with my
  current view". And the ragebot is gated to **firearms** (`m_WeaponType` 1–6): the knife,
  C4, taser, grenades and equipment are excluded. `wire::Settings` v11.



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
2. Probe `Ω_i` vertices + centroids with a trace oracle. `TraceShapeWithSortedResults`
   returns sorted hit records, while the current `FX_FireBullets` path uses
   `TraceLineIntervals` plus repeated penetration advance; neither API's full material
   and mesh semantics are yet validated as a drop-in oracle for our field.
3. Where the value changes between adjacent probes, reconstruct the occluder
   edge from its reported entry/exit points (faces are planar → 3 points pin a
   plane) → the exact boundary.
4. Assemble cells → nearest point / Chebyshev centre / blurred max.

Cost and retry count depend on the selected trace API; do not budget Tier 1 as one
whole-stack call until its behavior is validated. Fallback: bisect a straddle to tolerance.

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

1. **Hitbox renderer (live re-check).** The three modes and the colour picker are wired
   (§0). Re-run the close-up comparison against `sv_showplayerhitboxes`, with
   `scripts/hitbox-view.py` as the offline reference for the same boxes.
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
