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
- **The triggerbot — the safe half of the aim (§12).** A **trigger** that fires when the
  crosshair is on a target at a computed hit chance and **never** writes a view angle: it
  reuses `aim::solve` and adds only the decision (the angle gate, the hitchance gate, the
  head hitbox's lower threshold, the reaction delay). It runs on the **game thread** and
  its solve is **gated** behind a cheap pre-check (is the crosshair plausibly on somebody?)
  and a rate limit — the trigger costs nothing when the crosshair is off the enemies. The
  settings travel through the `trigger` control command and the `CS2_PAYLOAD_TRIGGER`
  environment (the wire block is versioned and full); the shot is pulled through
  `Input::press`/`Input::release` and is always balanced.



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

## 12. The trigger (the safe half of the aim)

A **triggerbot**: pull the trigger when the crosshair is on a target at a
computed hit chance — and **never** write a view angle. It is the *conservative*
consumer of the ragebot's mathematics: it reuses `aim::solve` (the same solver,
the same weapon model, the same hit probability) and adds only the **decision**
on top.

- **Code:** `payload/control/include/cs2/control/trigger.hpp` (the decision — a
  pure function, header-only) and `payload/control/src/trigger.cpp` (the
  runtime). The service owns it (`Service::trigger_`) and the `trigger` control
  command configures it. Host test: `control-trigger`.
- **Where it runs:** the **game thread**, in the same sink the ragebot's
  `rage_tick` runs in (`Service::on_game_thread`, the CreateMove hook). Never on
  the render thread, never from the control thread — the control thread only
  *configures* it.

### 12.1 The decision

`decide(view, solve, config, now_ms, latch) -> TriggerDecision` is pure: the
view angles, the solve's answer, the configuration and the clock in, a
`{fire, hold, reason, delay_remaining_ms, latch}` out. The rules, in order:

1. the feature is on and the trigger is **engaged** (`mode`: `hold` the key,
   `always`, or `toggle` on the key's press edge);
2. the solve found a target;
3. the **crosshair is on the solved aim point** — the yaw error and the pitch
   error, each against its own tolerance (`yaw_tolerance` / `pitch_tolerance`,
   in degrees);
4. the solve's **hit chance** is good enough — a **head** hitbox uses the lower
   `head_hitchance` threshold (the classic "head = trigger" behaviour);
5. the **reaction delay** (`delay_ms`) has elapsed since the conditions first
   held.

Any rule that fails disarms the delay and releases the shot, so the next
continuous aim starts the delay again. The `reason` says *why* the trigger
stayed quiet — the operator's window: `disabled`, `key not held`, `no target`,
`no solution`, `yaw error`, `pitch error`, `hitchance too low`, `reaction
delay`, `on target`, `already firing`.

`fire` is an **edge** ("pull the trigger now") and `hold` a **level** ("the shot
must stay down"): the runtime presses on the edge and releases when the level
falls, so **one continuous aim is one press and one release**.

### 12.2 The cost — the solve is gated

The ragebot's solve is measured at ~5 ms of the tick at the best spot and much
worse at dense spots, so the trigger never runs it unconditionally:

- **The cheap pre-check (the gate).** Before the solve, the trigger asks a
  question that needs no engine read at all: *is the crosshair plausibly on
  somebody?* The players' positions are already in the snapshot, so the test is
  a dot product per player: the view direction against the cone the player's
  body subtends (the feet-to-eyes band, plus a slack). Below the gate the
  trigger costs a handful of floating point operations per player and **no
  solve at all**.
- **The solve is single-target.** The trigger solves for **one** target — the
  one under the crosshair — so its cost does **not** grow with the number of
  enemies in the scene (the ragebot's solve is multi-target).
- **The solve is rate-limited** (`solve_interval_ms`, default 16 ms ≈ every
  other tick at 64 Hz). The decision itself still runs every tick, with the
  **fresh** view angles against the **cached** solve.
- **The cost is reported:** the `trigger` command returns `evaluations`,
  `gate_skips`, `solves`, `shots` and `last_solve_us`.

The estimate: the trigger's solve is one target's hitbox projection plus the
`argmax_hitchance` search — the same stages the ragebot's per-stage
instrumentation attributes (`docs/ragebot-perf.md` §3.6: `argmax_hitchance`
1.1 ms on the 717-cell **merged** field, `argmax_damage` 0.15 ms) over a field
an order of magnitude smaller, plus the model's hitbox read. So ~0.2–0.6 ms per
solve, ~0.1–0.3 ms per tick amortized at the default rate — and **zero** when
the crosshair is off the enemies. **Not measured live**: the game cannot be
launched from the agent's session (no GPU), so the per-tick cost above is an
estimate from the ragebot's measured stage breakdown, and `last_solve_us` is the
live number to read.

### 12.3 The settings surface

The trigger's settings are **not** in `wire::Settings` (the block is versioned
and full, and adding a field means bumping `wire::k_version` **and** recomputing
the golden settings checksum). They travel through the **control channel** and
the **environment**, like the ragebot's own tuning:

- **`trigger` control command** — `on`, `hitchance` (0..1), `head_hitchance`
  (0..1), `yaw_tolerance` (degrees), `pitch_tolerance` (degrees), `delay_ms`,
  `key` (a Win32 virtual-key code, 0 = none), `fire` (whether the trigger
  pulls the trigger itself), `mode` (`hold` / `always` / `toggle`), plus
  `force` (bypass the key's check — a scenario cannot synthesize a key press)
  and `solve_interval_ms`. **`trigger` with no arguments returns the current
  state** and the last decision (`reason`, `firing`, `target`, the counters).
- **`CS2_PAYLOAD_TRIGGER`** — the environment's defaults, comma-separated:
  `on`/`off`, `fire`/`nofire`, `force`, `key=<vk>`, `mode=hold|always|toggle`,
  `hitchance=<f>`, `head_hitchance=<f>`, `yaw_tolerance=<deg>`,
  `pitch_tolerance=<deg>`, `delay_ms=<n>`, `solve_interval_ms=<n>`.

### 12.4 The firing path

- The trigger pulls the trigger through the **existing input backend**:
  `Input::press(button::k_attack)` / `Input::release(button::k_attack)` — not a
  console command, not a synthetic OS event.
- **The press/release is balanced.** The runtime presses when the decision
  starts the shot and releases when it stops. The state is **reset** when the
  feature is disabled, when the game state changes (a new pawn, a dead local
  player, no snapshot) and when the control channel disconnects
  (`Service::run`/`Service::stop`) — a trigger that pressed and never released
  would leave the gun firing.
- **The trigger never writes a view angle.** It has no path to one: the only
  input backend calls it makes are `press`/`release`, and the decision function
  has no angle output. That is a property of the code, not a convention.
- **The trigger key's state is read from the operating system's key state**, not
  from the command's button mask — the mask is the trigger's *own* output (the
  attack bit), so reading it would be the auto-strafe's latch landmine
  (`docs/re/strafing.md`): the trigger would latch on its own press.

### 12.5 Known limitations

- **The trigger's solve is occlusion-free.** It does not project the world
  solids (the expensive part of the ragebot's solve), so the trigger fires at
  a target behind a wall as well — a wallbang trigger, not a "shoot what you
  can see" one. The ragebot's `only_visible` gate is the occlusion-aware path.
- **The press needs the `usercmd` backend.** `Input::press`/`release` write the
  command's button mask, which `Input::apply` only applies in the `usercmd`
  backend (the same rule the control channel's `press` command has —
  `docs/control-harness.md` §"Input backends"). The `trigger` command reports
  the backend.
- **The button mask is shared.** The trigger's release clears the attack bit in
  the shared mask, so while the trigger is enabled it owns the trigger button
  (a manual `press("attack")` from the control channel is overridden until the
  trigger fires again).

## 13. The Monte-Carlo hit-chance solver (alongside, not instead)

> Added 2026-10-21 — task T34 (`docs/orchestration/spec-t34-montecarlo.md`).
> **The core only.** The module (`cs2/aim/montecarlo.hpp`, `cs2/aim/src/montecarlo.cpp`)
> is host-tested (`aim-montecarlo`) and benchmarked (`aim_bench`), but it is **not
> wired into the live path**: no `AimObjective`, no dispatch, no `wire::Settings`, no
> per-weapon profile value. The damage-field solver of §2/§3 is **untouched** and
> stays the live one; the new solver is the community's estimator, available for a
> side-by-side comparison.

### 13.1 Why a second solver

The live objective blurs the damage field with a **Gaussian**
(`payload/aim/src/field.cpp`). The engine's spread kernel is not Gaussian
(`docs/re/aim.md` §"The spread cone", `docs/research/aim-objective-math.md` §3.1):

* its **support** is exactly `|off| <= S + I` — a Gaussian puts ~2.5e-3 of its mass
  beyond that radius, where the truth assigns exactly zero;
* its **radial law** is uniform in radius (rim-heavy), not area-uniform;
* the field's cells **overlap**, so the Gaussian's per-cell sum double-counts
  (`docs/known-issues.md` §11.2) — a Monte-Carlo sample asks "where does *this one*
  bullet land?", and the **nearest** hit wins.

The new solver therefore samples the engine's **own** kernel over seed indices and
tests every sample against the target's hitboxes with a nearest-hit ray test — the
community's estimator (velocity's 64/128/256 seed loop).

### 13.2 The kernel

The draw is the engine's, in the engine's order (`docs/re/aim.md`):

```
per pellet, sequential across the pellets:
  r1 = RandomFloat(0, 1);   a1 = RandomFloat(0, 2pi)   // spread
  r2 = RandomFloat(0, 1);   a2 = RandomFloat(0, 2pi)   // inaccuracy
  off = spread·r1·(cos a1, sin a1) + inaccuracy·r2·(cos a2, sin a2)
```

with the revolver's `r1 -> 1 - r1²` transform. The RNG and the seed derivation are
the repo's own (`UniformRandomStream`, `md5_pseudo_random` — `cs2/aim/spread.hpp`).
The sampler's assembly of the offsets is a **second implementation of the recipe,
not a second RNG** — but it is the **same expression**, so the host test's
cross-check against `pellet_offsets` is a **drift detector** (if one of the two is
edited, the test fails), not an independent derivation of the engine's recipe. The
test asserts the two are **bit-identical** (exact equality), so the claim is
enforced rather than asserted; the recipe itself rests on the repo's RE
(`docs/re/aim.md`), which `spread.hpp` still marks as unvalidated against the live
engine.

The offset is applied in the **(right, up) tangent frame** of the aim direction, and
the chart's local metric carries it into the chart's coordinates: a true angular
offset of `r` radians spans `r` in the chart's `v` (pitch) and `r / cos(v)` in its
`u` (yaw) — `yaw_scale`, `cs2/aim/geometry.hpp`.

> **The existing solver places the same pellet elsewhere.** `DamageField::
> value_at_pellets` (`payload/aim/src/field.cpp`) adds the **raw** offset as a
> chart displacement, without the yaw scale: at a 45° pitch the two put the pellet
> `1/cos(v) = 1.41×` apart in yaw, at 60° `2×` apart. The scaled form is the
> correct one — every spherical angular quantity has to be scaled, or the model is
> wrong at a steep aim (`docs/research/aim-objective-math.md`) — so **the two
> solvers genuinely disagree at a steep pitch**. The existing solver is not this
> task's to change; the divergence is stated here (the review's m3) and in the
> code.

**The weapons whose pellet pattern is not modelled are refused**, not silently given
the standard pattern: the shotgun's pattern table (`FUN_1807cd1b0`) and the negev's
`r -> 1 - r^(2^k)`. `WeaponProfile` carries the weapon *type* only, so the
machineguns are refused with the negev (`pattern_supported`).

> **Every entry point that can produce a number refuses them** — `spread_kernel`,
> `evaluate_sample`, `evaluate_hit_chance` and `solve_montecarlo` all return
> `MonteCarloError::unsupported_pattern`. The first version of this module guarded
> only `spread_kernel` and `solve_montecarlo`, and `evaluate_hit_chance` — the
> cheap, natural entry point — happily scored a Nova with the **standard** pattern
> (the review's M1). `evaluate_sample` therefore returns an
> `std::expected<Sample, MonteCarloError>`: a sample for an unmodelled weapon is an
> error, not a number.

**The revolver's pattern is the caller's to state.** `WeaponProfile` carries the
weapon *type* only, and a revolver is type 3 like every rifle, so the module cannot
infer its `r -> 1 - r²` transform. `spread_kernel`'s pattern argument therefore has
**no default**: the caller states it. A default would have dropped the transform
without a word — the silent fallback the refusal exists to prevent.

### 13.3 The hit test

Every sample's pellet becomes a ray from the eye; the ray is intersected with
**every** hitbox of **every** candidate target and the **nearest** hit wins — not
the sum (that double-counts the overlapping cells) and not the best-value hit (that
ignores the geometry). The hit's `multiplier`, the weapon's damage and the range
falloff over the hit distance give the sample's damage; the hitbox's `group` is the
hitgroup.

> **An exact tie is broken by the rule, not by the caller's array.** The nearer
> hit wins; at the *same* distance the **hitgroup preference** decides (the
> selection rule's third key), then the damage multiplier, then the target index.
> The first version kept the first hitbox in the caller's list, so `[head, body]`
> and `[body, head]` reported different hitgroups and different damage (the
> review's m2).

The intersection is **exact**: a capsule is the union of its two cap spheres and
its side cylinder, and an oriented box is a slab test in its own frame. Both use the
same span arithmetic as the occluders — it now lives in `cs2/aim/ray_span.hpp`, so
the two share one implementation instead of two (`payload/aim/src/occlusion.cpp` is
unchanged in behaviour).

A multi-pellet weapon's sample **hits when any of its pellets hits** (the union,
not the sum — `docs/research/aim-objective-math.md` §6); the sample's damage is the
sum over the pellets that hit (the engine's per-pellet damage).

**A degenerate hitbox is rejected, not turned into a hit.** The slab test clamps the
interval with comparisons, and *every comparison against a NaN is false* — so a NaN
half-extent used to swallow the clamp and degenerate the box into an unbounded
solid: the hit landed at distance **0**, won every nearest-hit contest, and the
solver reported a **100 % hit chance** on a target that cannot be hit (the review's
M5). The hit test now validates the shape (`hitbox_valid`: every component finite,
a positive radius, positive half-extents), and a scene that still holds one is
**refused** with `MonteCarloError::invalid_geometry` rather than scored without it.

> The validation lives in the **hit test**, not in the span helpers: the span
> arithmetic is shared with the occluders, whose behaviour must not change, and it
> answers a different question — *"where does the ray lie inside this solid?"* —
> which is ill-posed for a solid that does not exist. The hitbox is where the
> geometry is interpreted, so that is where it is checked. The check runs **once
> per evaluation**, not once per ray.
>
> `payload/aim/src/reader.cpp` (not this module's file) reads `m_flShapeRadius`
> raw and only rejects a non-positive radius for the *non-capsule* shape, so a
> capsule with `radius <= 0` does reach the solver today. The next task (the live
> wiring) must filter the reader's hitboxes with `hitbox_valid` — the solver now
> refuses them rather than pretending they are solid.

### 13.4 The seed policies — the open question, made a parameter

The engine's seed is **not settled** (`docs/known-issues.md` §11.2): the repo's own
RE says `MD5_PseudoRandom(sequence)` (a pure function of the command number), the
community says the seed is a function of the view angles and the tick. The solver
takes the seed source as a **parameter** and implements both:

| policy | the seed of sample `i` | what the number means |
|---|---|---|
| `sequence` (the default) | `md5_pseudo_random(sequence + i)`, `i` in `[0, N)` | the **expectation over the next N shots** — the seed advances with the command number, so `sequence + i` is a *different shot* |
| `angle_grid` | `md5_pseudo_random(hash(probe yaw, probe pitch, tick))`, the probes sweeping the candidate's neighbourhood at `probe_step` | the **probability over the shot that would be taken** — the seed is a function of the aim, so within a cell there are many seeds and the lottery is the *choice* |

> **The `angle_grid` sweep's width, and the candidate's own seed.** The probe
> angles sweep the candidate's neighbourhood at `probe_step`; the sweep's centre
> index is **integral** (`sample_count / 2`), so **the candidate's own angle is one
> of the probes** and its own seed is in the sample set. (With the first version's
> half-integer centre it never was: the centre was 31.5 / 63.5 / 127.5 for
> N = 64 / 128 / 256, and the candidate's own seed was never drawn — the review's
> m1.) The sweep reaches `floor(N / 2)` steps either side — **±15.95°** at N = 64,
> **±31.9°** at N = 128 and **±63.8°** at N = 256, with the default
> `probe_step` of 0.0087 rad — and is asymmetric by one step for an even count.
>
> The stand-in hash is injective **on the quantisation grid**, not on the raw
> angles: the quantisation is deliberate (the stand-in models "the same aim gives
> the same seed"), so two probe angles within one quantum (1e-4 rad) share a seed.
> The probe step is 87 quanta, so two neighbouring probes never collide (the
> review's m8).

> **The `angle_grid` hash is a STAND-IN.** The engine's `get_tick_view_angles` is
> **not reverse-engineered** (`docs/re/aim.md` §"The seed"; the RVAs there resolve to
> unrelated functions on the live build), so the hash is *ours*: the probe angles are
> quantised onto a fixed grid and mixed with the tick. It reproduces the **shape** of
> the community's model and nothing more — **the numbers that policy produces are
> indicative, not exact.**

### 13.5 The search and the selection rule

The objective is not differentiable, so the search is a **bounded evaluation over a
candidate set**: the best cells' **Chebyshev centres** and **centroids** — the same
two candidates the existing solver's `field_candidates` builds — capped by a
**documented budget** (`CandidateBudget`: 16 cells, a cap of 64 candidates, **32 in
practice** — 16 cells × 2), never a function of the field's size.

> `field_candidates` is file-static in `payload/aim/src/field.cpp`, a file this task
> does not own, so the candidate set is rebuilt from the same two primitives it
> uses (`chebyshev_center`, `polygon_centroid`, both exposed by
> `cs2/aim/geometry.hpp`) rather than by calling it. The candidate **kinds** are
> the same; the budget is the module's own.
>
> **The divergence is explicit** (the review's m6): the existing generator emits
> **four** candidates per cell — the Chebyshev centre, the centroid, the polygon's
> **vertices** and the point **nearest the field centroid** — plus the **field
> centroid** itself, and it filters by `value_floor` (a cell below the floor cannot
> improve the objective). The Monte-Carlo generator emits the centre and the
> centroid only and ranks the cells by value instead. The two candidate sets
> therefore differ, and the Monte-Carlo search evaluates **fewer** candidates.
> Exposing `field_candidates` would remove the divergence — that file is not this
> task's, so the divergence is reported rather than fixed.

The selection is the community's **lexicographic** rule
(`docs/research/aim-objective-math.md` §2):

1. a candidate that **can kill** beats one that cannot;
2. then the **highest probability of any hit**;
3. then the **hitgroup preference** — the repo has no ordered hitgroup list of its
   own (the "head beats body" test works through the damage multiplier, not through
   an order), so the module states the order it uses: **head, chest, stomach,
   generic (neck/pelvis), arms, legs** (`hitgroup_preference`);
4. then the highest expected damage (the final tie-break).

> **"Can kill" is judged per target.** The damage a sample does belongs to the
> target it hit, so the key is `the damage attributed to the candidate's target ≥
> that target's remaining health` (`HitChance::target_damage`). The scene's mean
> damage (`expected_damage`) is a different number — in a multi-target scene it is
> the mixture of every target a sample hit — and it says **nothing** about whether
> the named target dies. The first version compared the aggregate with one target's
> health (the review's M2): a chest target with 40 hp left and 35 damage per hit
> was reported as `can kill` because a *second* target's head hits were in the
> average, and since "can kill" is the rule's **first** key, that candidate beat
> every honest one.

### 13.6 The measured cost

The committed benchmark (`payload/aim/bench/bench_montecarlo.cpp`, the `aim_bench`
target) replays the committed saved cell set `artifacts/live-heatmap.json` (717
cells) and publishes p50/p95/p99 for the community's sample counts, next to the
existing objective's cost on the same field. It runs **from the repository root**
(the data path is relative):

```
meson compile -C <build>
<build>/aim_bench.exe
```

Measured on this machine (the host build, `debugoptimized`; the 717-cell saved set,
a cone radius of 0.002 rad). The numbers are the **median of six runs**; the
run-to-run spread of the p99 is quoted where it matters.

**Two hitbox sets are measured, and the difference is the budget.** The saved set
stores the field's *chart* cells, so the benchmark rebuilds one world-space capsule
per (target, hitgroup) group — **8 per target, 24 in all**. A live CS2 player has
**19** hitboxes (`docs/re/aim.md`: `m_nHitBoxes (19 on a player)`), so a live
three-target solve hands the solver **57** hitboxes — **2.4×** the reconstruction,
and the solver's cost is linear in the hitbox count. The benchmark therefore
measures **both**: the reconstruction, and a **live-sized** set (the same
silhouettes, cut along their axes into 19 pieces per target — the union of the
pieces is exactly the original capsule, so the silhouette is preserved and only the
count changes).

**The live-sized set (57 hitboxes) — what a live solve costs:**

| what | N=64 | N=128 | N=256 |
|---|---|---|---|
| `evaluate_hit_chance` (one candidate) — p50 / p95 / p99 | 0.13 / 0.18 / 0.19 ms | 0.26 / 0.35 / 0.37 ms | 0.52 / 0.70 / 0.75 ms |
| `solve_montecarlo` (the whole search, 32 candidates) — p50 / p95 / p99 | **4.3 / 5.0 / 5.1 ms** | **8.9 / 10.1 / 10.7 ms** | **19.2 / 23.2 / 23.8 ms** |

**The reconstructed set (24 hitboxes) — the old, understated measurement:**

| what | N=64 | N=128 | N=256 |
|---|---|---|---|
| `evaluate_hit_chance` (one candidate) — p50 / p95 / p99 | 0.07 / 0.10 / 0.10 ms | 0.14 / 0.19 / 0.22 ms | 0.28 / 0.38 / 0.42 ms |
| `solve_montecarlo` (the whole search, 32 candidates) — p50 / p95 / p99 | 2.4 / 2.9 / 3.2 ms | 4.8 / 5.4 / 5.6 ms | 9.7 / 10.5 / 11.9 ms |

| the existing objective, on the same field | p50 | p95 | p99 |
|---|---|---|---|
| `argmax_hit_probability` (σ = 0.002) | 60 ms | 65 ms | 67 ms |
| `hit_probability` (one evaluation) | 3.5 ms | 8.3 ms | 9.1 ms |

One sample (one seed, one pellet) costs **1.0 µs** against the 24 hitboxes and
**2.0 µs** against the 57.

**The verdict (at the live-sized hitbox count).** The Monte-Carlo search is **3–14×
cheaper than the existing objective** on the same field (60 ms ÷ 19.2 ms at 256
samples, 60 ms ÷ 4.3 ms at 64) — not the 4–20× the 24-hitbox reconstruction
suggested. Against the budget:

* **64 samples fit with room to spare**: p99 5.1 ms (the six runs' p99 ranged
  4.9–7.4 ms) against the 15.6 ms tick and the service's 12 ms guard.
* **128 samples are *at* the guard**: the median p99 is 10.7 ms, but the six runs'
  p99 ranged **9.2–12.8 ms** — it crosses the 12 ms guard on a loaded machine. This
  is the mode the next task must size from, and it is not comfortably inside the
  budget.
* **256 samples do not fit at all**: the p50 (19.2 ms) is already over the 15.6 ms
  tick and the p99 (23.8 ms) is over the guard.

The existing objective does **not** fit the tick at all (p50 60 ms).

The published figure for the existing objective is 34 ms (`docs/ragebot-perf.md`
§3.9); the out-of-tree harness that produced it measures 29–50 ms on this machine
for the same call, so the two are the same order — the numbers above are the
committed, reproducible ones.

**Two caveats that the cost ratio alone does not carry.**

* **The two objectives do not model the same dispersion** (the review's m4). The
  Monte-Carlo kernel is `S = I = 0.002 / √2`, so its **support** is
  `S + I = 0.00283` rad and its variance-matched σ is
  `√(S² + I²) / √6 = 0.00082` rad — the Monte-Carlo cone is **2.4× tighter** than
  the Gaussian's `σ = 0.002` (whose support is unbounded). The *cost* comparison is
  unaffected (the Monte-Carlo cost is set by the sample count, not by the radius),
  but the **hit chances** the two print are not comparable — which is why the
  benchmark prints "hit chance 1.000" for its chosen candidate: the cone is
  smaller than the target.
* **The two searches do different work.** The Monte-Carlo search evaluates **32
  fixed candidates** (the top-16 cells' Chebyshev centres and centroids) with
  **no refinement inside a cell**, while `argmax_hit_probability` runs `mass_ascent`
  (`payload/aim/src/field.cpp`) — 8 seed candidates plus up to 32 compass
  iterations that refine to a local optimum over the 717 cells. The Monte-Carlo
  search is cheaper **and coarser**: the ratio is a cost ratio, not a
  quality-for-quality comparison.

**The benchmark's hitboxes are a reconstruction.** The saved set stores the field's
*chart* cells, not the world-space solids they came from, so the benchmark rebuilds
one world-space capsule per (target, hitgroup) group, and the **live-sized** set
cuts those capsules into 19 pieces per target. The **cost** is what is measured;
the **hit chances** the benchmark prints are not meaningful (a proxy geometry, and
a cone 2.4× tighter than the Gaussian's).

### 13.7 What the new solver does not model

* **The world.** The repo's design is Tier-2: it projects the world solids into the
  chart instead of tracing rays (`docs/ragebot.md` §3). The Monte-Carlo solver
  scores the **hitbox geometry only** — whether the shot is *visible* (or
  penetrates a wall) stays the existing solver's job (`only_visible` / `penetrated`).
* **The `sqrt(r)` question.** The RE listing has no `sqrt` between `RandomFloat` and
  the `cos/sin`, and the repo's RE is the authority, so there is none
  (`docs/research/aim-objective-math.md` §3.1). The host test pins the **linear**
  radial law, so a `sqrt` would fail it loudly.
* **The RNG's first-draw bias.** The repo's `UniformRandomStream` has a biased
  **first** output — the sampler uses it for `r1`, the spread magnitude, on every
  sample: over 200 000 seeds its mean is 0.486220 (−21.3 σ), while draws 2–8 are
  unbiased. The **model** of the kernel is therefore **≈3 % tighter** than the
  ideal uniform-in-radius law (the per-axis variance is 2.7 % low, not "within
  1 %"), so every hit chance the solver publishes is slightly optimistic. The
  bias is **documented, not fixed**: `UniformRandomStream` and `pellet_offsets`
  must stay bit-identical (the spec §4), and the first draw is a property of the
  engine's own generator — changing it would change the existing solver's offsets.
  The host test pins the *measured* law (a band around 0.485476) instead of a
  ±0.02 window around 0.5, so it can no longer hide the bias. See
  `docs/research/aim-objective-math.md` §11.1.
* **The seed's truth.** Both policies are implemented; neither is declared the
  engine's. That needs RE.
* **The `√6` σ correction.** Not applied — it is blocked on the `sqrt(r)` question
  and on the inaccuracy's units. A solver that uses the true kernel does not need it.

### 13.8 The files

| file | what |
|---|---|
| `payload/aim/include/cs2/aim/montecarlo.hpp` | the solver's API: the kernel, the seed policies, the sample, the hit test, the search |
| `payload/aim/src/montecarlo.cpp` | the implementation |
| `payload/aim/include/cs2/aim/ray_span.hpp` | the exact ray/solid span arithmetic, shared with the occluders |
| `payload/aim/tests/test_aim_montecarlo.cpp` | the host test (`aim-montecarlo`) |
| `payload/aim/bench/bench_montecarlo.cpp` | the committed benchmark (`aim_bench`) |
| `artifacts/live-heatmap.json` | the saved cell set the benchmark replays (committed; the directory is git-ignored **except** this file, which `.gitignore` un-ignores explicitly) |

> **The saved set is one line, and it stays that way.** The file is 244 KB of
> machine-written JSON on a single line. Reformatting it would be a 244 KB diff
> that carries no information — it is a *capture*, it does not change, and there is
> no history to blame — so the one-line format is kept and the ignore rule was
> narrowed instead (`artifacts/*` plus `!artifacts/live-heatmap.json`), so the tree
> no longer contradicts its own ignore rule and a new artifact is still ignored
> (the review's m11).

## 14. The live Monte-Carlo objective (alongside, not instead)

> Added 2026-10-21 — task T35 (`docs/orchestration/spec-t35-montecarlo-live.md`).
> **The core is in the live path now.** `AimObjective::monte_carlo` is selectable
> from the control channel and from the per-weapon profile, so the operator can
> live-test the Monte-Carlo solver against the damage-field one. The four
> existing objectives (`damage`, `hitchance`, `expected`, `pellets`) are
> **unchanged** and stay selectable — the host test proves it against the
> field's own optima (`aim-solver`), not by assertion.

### 14.1 How to select it

The objective is selected over the **control channel** (`rage`) or from the
**per-weapon profile** (the `objective` byte in `cs2-rage.cfg`):

```json
{"command": "rage", "args": {"objective": "monte_carlo"}}
{"command": "rage", "args": {"objective": "hitchance"}}
```

The names, the values and the seed policy's names are **pure rules**
(`cs2/control/rage_objective.hpp`) and they are host-tested
(`control-rage-objective`): the `rage` command maps a name to the profile's
byte, the `aim` command maps a name to the solver's `AimObjective`, and the
status maps the byte back to a name. The service's call sites **use** those
rules — a source check in the same test proves they do not re-implement the
mapping inline.

| name | the profile's byte | the `aim` command's objective |
|---|---|---|
| `damage` | 0 | `AimObjective::damage` |
| `hitchance` | 1 | `AimObjective::hitchance` |
| `expected` | 2 | `AimObjective::expected` |
| `monte_carlo` | 3 | `AimObjective::monte_carlo` |
| `pellets` | — (the default) | `AimObjective::pellets` |

**An unknown name selects the hitchance** — the value the control channel has
always used for a name it did not know. So a typo cannot silently switch the
solver, and it cannot silently switch it to the *new* objective either.

The per-weapon profile's `objective` byte round-trips through a save and a load
(`control-rage-config`), and **a garbage byte in a config file loads as the
documented safe default** — never as an arbitrary objective.

### 14.2 The numbers mean something different — read this before comparing

**This is the most important thing to know about the new objective.** The two
solvers report the *same field names* with *different meanings*.

| | the old objectives | the Monte-Carlo objective |
|---|---|---|
| `hitchance` | a **Gaussian mass** over the field, whose σ is **√6 too wide** (`docs/research/aim-objective-math.md` §"the √6 sigma") | the **measured** chance that the shot hits: the fraction of the samples that hit |
| `value` | the damage of a shot that **hits** (the field's value at the aim) | the **expected** damage: a miss contributes 0 |

The two consequences for the gates:

* **`min_hitchance` passes more shots under the Monte-Carlo objective.** For a
  head-sized target the truth is **0.777** and the old model reports **0.221** —
  the old gate was asking for a *much* higher true probability than its number
  suggested. The same operator value therefore blocks far less under the
  Monte-Carlo objective. To keep the old strictness, raise the value to about
  the truth (**0.78** for that head-sized case), not the 0.22 the old model
  showed.
* **`min_damage` blocks more shots under the Monte-Carlo objective.** A 50 % hit
  chance on 100 damage reports **50**, not 100. So a `min_damage` tuned against
  the old objective (which saw the *hit* damage) is too high here: lower it, or
  read it as "the damage I expect".

The status says which objective produced the numbers (§14.6), so the two are
never confused.

### 14.3 The seed context: the tick, and the sequence's fallback

The Monte-Carlo solver needs a **sequence** number and a **tick**
(`cs2/aim/montecarlo.hpp`).

* The **tick** is the prediction tick the service already works with: the local
  controller's `m_nTickBase`, the tick the client is simulating
  (`docs/re/lagcomp.md`). It is exact.
* The **sequence** is the command's sequence number — the thing
  `MD5_PseudoRandom(sequence)` is derived from (`docs/re/aim.md` §"The seed").
  **It is not reachable from where the service sits.** The `Input` module owns
  the `CUserCmd` pointer and exposes no accessor for the command's number, and
  the one that exists (`bhop_cmd_number`) is refreshed by the **bunnyhop assist**
  alone — so it is zero when the assist is off and stale when it is on. A number
  that is the game's own must not be presented as such, so the **documented
  fallback** is used: the prediction tick stands in for the command number (the
  seeds are `md5(tick + i)` — the same structure, a tick instead of a command).
  The status says so: `mc_seed_fallback` is **true** and `mc_seed_note` names
  the reason.

The **seed policy** is selectable the same way the objective is:

```json
{"command": "rage", "args": {"seed": "angle_grid"}}
```

| policy | the number means | the status |
|---|---|---|
| `sequence` (the default) | the **expectation over the next N shots** | `mc_seed_fallback: true` — the tick stands in for the command number |
| `angle_grid` | the **probability over the shot that would be taken** | `mc_seed_fallback: false`, and `mc_seed_note` says the numbers are **indicative, not exact** |

### 14.4 The weapon's pattern: the revolver's `1 - r²`

The core's `spread_kernel` takes the weapon's **pattern** as a required
argument — there is no default, because a default would drop the revolver's
`r → 1 - r²` transform without a word (§13.2). `WeaponProfile` carries the
weapon *type* only, and a revolver is type 3 like every rifle, so the pattern
cannot be inferred from it.

**The rule is keyed on the designer name** (`cs2/control/rage_objective.hpp`):
`weapon_revolver` gets the revolver's pattern, every other name gets the
standard one. The name is what the game's own `PlayerSnapshot::weapon` carries
(`weapon_ak47`, `weapon_revolver`, …).

**A weapon the payload could not name** (an empty designer name) gets the
standard pattern: it is the pattern every weapon except the revolver uses, so
it is the conservative choice. The status carries the weapon's name, so an
operator can always see which weapon was classified.

### 14.5 The sample count is 64

The **default is 64** — the only count that fits the frame budget at a
live-sized hitbox set (57 hitboxes for three targets; 128 is over the 12 ms
guard and 256 is over the tick, §13.6). A different count must be an
**explicit** choice:

```json
{"command": "rage", "args": {"samples": 128}}
```

and the status always shows the count in force (`mc_samples`).

**The cost with the backtracking pass.** One Monte-Carlo solve is ~4.3 ms at a
live-sized set (§13.6), and the backtracking pass is a **full solve per pose**.
With `backtrack=1` the Monte-Carlo objective therefore costs `poses × ~4.3 ms`
per tick, which does **not** fit the 12 ms guard. Turn the backtracking off
while live-testing the Monte-Carlo objective.

### 14.6 What the status shows

The `rage` command's payload carries the objective **by name** (not by number —
the numbers mean different things under each objective, §14.2) and the
Monte-Carlo's own state:

| field | what it is |
|---|---|
| `objective` | the objective's **name** (`"monte_carlo"`, `"hitchance"`, …) |
| `objective_value` | the profile's byte (0…3) |
| `mc_samples` | the sample count in force (**64** by default) |
| `mc_seed` | the seed policy (`"sequence"` / `"angle_grid"`) |
| `mc_seed_fallback` | whether the seed's sequence is the fallback (**true** under `sequence`) |
| `mc_seed_note` | what the number means, and whether it is exact |
| `mc_hitchance` | the Monte-Carlo's measured probability |
| `mc_expected_damage` | the Monte-Carlo's expected damage |
| `mc_target` | the target the nearest hit named |
| `mc_refusal` | the reason the solver refused ("" when it did not) |
| `mc_fallback` | **true** when the aim came from the `damage` objective instead |
| `mc_valid` | whether the last solve produced a Monte-Carlo verdict |

**A refusal is never silent.** When the Monte-Carlo solver refuses — the
weapon's pattern is not modelled (the shotgun, the machineguns), the sample
count is not positive, the scene is malformed — the objective falls back to
`damage` (the cheapest, always-available one) and the **reason** lands in
`mc_refusal` with `mc_fallback: true`. The one-line HUD status carries the
objective's name as well (`obj monte_carlo`).

The `aim` command (the one-shot probe) reports the same verdict in its own
payload: `mc_samples`, `mc_hits`, `mc_hitchance`, `mc_expected_damage`,
`mc_target`, `mc_hitgroup`, `mc_can_kill`, `mc_seed`, `mc_refusal`.

### 14.7 The limits

The two limits of §13.7 are the live objective's limits too:

* **The hitbox geometry only.** The Monte-Carlo solver does not model the
  world: it scores the hitboxes, not the walls. Whether the shot is *visible*
  (or penetrates a wall) is still the existing machinery's job — the candidate
  set is the field's **occlusion-filtered** cells, and the `only_visible` gate
  still uses the field's `penetrated` flag.
* **The stand-in hash.** Under the `angle_grid` policy the seed is a function
  of a hash that is **ours**, not the engine's unreverse-engineered
  `get_tick_view_angles`. The numbers that policy produces are **indicative,
  not exact**.

And the two the live path adds:

* **The seed's sequence is a fallback** (§14.3): the tick stands in for the
  command's sequence number, and the status says so.
* **The `√6` σ correction is not applied** — it is still blocked on the
  `sqrt(r)` RE question (§13.7). It does not matter for the Monte-Carlo
  objective (the kernel is sampled, not blurred), but it is why the *old*
  objective's numbers are not comparable with the new one's.

### 14.8 The files

| file | what |
|---|---|
| `payload/aim/include/cs2/aim/solver.hpp` | the `monte_carlo` objective, the `SolveInput`'s Monte-Carlo parameters, the verdict and the refusal |
| `payload/aim/src/solver.cpp` | the dispatch: the core's input, the search over the field's cells, the fallback |
| `payload/aim/tests/test_aim_solver.cpp` | the objective's host test (`aim-solver`) |
| `payload/control/include/cs2/control/rage_objective.hpp` | the **pure rules**: the names, the values, the seed policy, the weapon's pattern |
| `payload/control/tests/test_control_rage_objective.cpp` | their host test (`control-rage-objective`) |
| `payload/control/src/service.cpp` | the live path: the objective switch, the seed context, the health, the status |
| `payload/control/include/cs2/control/rage_config.hpp` | the profile's `objective` byte and the line codec |
| `payload/control/tests/test_control_rage_config.cpp` | the profile's round trip (`control-rage-config`) |

### 14.9 The review round (2026-10-22)

The adversarial review of T35 found four majors and a dozen minors. The fixes
that change what the operator sees are here; the rest are in the ledger
(`docs/known-issues.md` §13).

**The status names the *effective* objective.** With a per-weapon profile the
solve runs the **profile's** objective, not the global one, so `objective` is
the effective one and `objective_global` is the setting; `objective_value` is
the effective objective's byte.

**The status carries the verdict's whole shape.** `mc_can_kill`, `mc_hitgroup`
and `mc_hits` are there now (they were only in the `aim` command's payload),
and the verdict is published as **one value** — a reader can never see a
half-published verdict, so a refusal can no longer be hidden by a torn read.

**The `aim` command's payload carries the seed's disclosure**
(`mc_seed_fallback`, `mc_seed_note`) and the sample count's note.

**The HUD line carries the Monte-Carlo's marker.** `obj monte_carlo mc83%` is
the measured probability; `mc-refused` says the solver refused and the aim
came from the `damage` objective (§2.4: a fallback must never be silent, and
the HUD is where the operator actually looks). The line's tail no longer
carries the backtracking record and the occluder count — both are in the
`rage` command's JSON status — so the line still fits its 128-byte buffer.

**The sample count behaves the same way in both entry points.** A count
outside 1..256 is **ignored** (the count in force is unchanged) and
`mc_samples_note` says so; a count above 64 is honoured, and the note says it
does not fit the tick.

**The backtracking budget is visible.** `backtrack_over_budget` is true when
the last tick skipped the backtracking because the frame had already blown
the 12 ms guard — otherwise the operator sees the backtrack counter stop
incrementing with no explanation.

**The `aim` command's Monte-Carlo kernel is never degenerate.** The weapon's
spread, its inaccuracy and its type always come from the game (the caller's
profile sets only the damage model), so a probe with `damage=36` measures a
real weapon's spread instead of a perfect one. The weapon type falls back to
the snapshot's own read when the VData could not be read, so a shotgun is
refused rather than scored with the standard pattern.

**The health list and the hitboxes speak about the same target.** The reader
stamps the hitboxes with the index of the target it chose, and the health
entry is built from that same index, so the Monte-Carlo's can-kill key works
in the `aim` path too (it was dead there: `mc_target` was always -1 and
`mc_can_kill` always false).

**A dropped hitbox is visible.** The reader does not validate the shape it
reads, so a malformed hitbox reaches the solver; the core refuses a scene
that holds one, so the objective filters the set for the core. A dropped
hitbox makes the measured probability a **lower bound** on the truth, so
the count travels with the verdict and the status carries it as
`mc_filtered` — a silent lower bound is a number the operator would read
as exact.

**The HUD line's buffer is 192 bytes.** The line carries the objective's
name, the Monte-Carlo's marker and the two occlusion markers, and the old
128-byte buffer cut the last of them off.

## 15. The fire path — the edge and the gate

> **The one-line version:** the ragebot used to hold `IN_ATTACK` down on
> **every** command while a solve was active, so a **semi-automatic** weapon
> (a pistol, the R8, the scout) fired **once** and never again — the engine
> needs a release→re-press **edge**. And nothing in the fire path asked
> whether the shot could be taken at all: the clip, the fire interval, the
> weapon's state and the fire mode were all unread.

Both defects were reported by the operator by **playing the game** on
2026-10-22 and then verified in the code
(`docs/orchestration/spec-t37-fire-path.md`).

### 15.1 What the payload reads, and where from

Every field is read through the generated client schema
(`payload/sdk/schemas/client_dll.hpp`, ADR-0001) — never through a
hard-coded offset. The offsets live in two places, each with the schema
lookup and this build's value as the fallback:

| field | class | what it means |
|---|---|---|
| `m_bIsFullAuto` | `CCSWeaponBaseVData` | the weapon's **fire mode**: it fires while the trigger is held, or it fires once per press |
| `m_flCycleTime` | `CCSWeaponBaseVData` | the weapon's **fire interval** in seconds (the fallback for the engine's own record) |
| `m_iClip1` | `C_BasePlayerWeapon` | the rounds in the **clip** |
| `m_bInReload` | `C_CSWeaponBase` | the weapon is **reloading** — the engine ignores the trigger |
| `m_nDeployTick` | `C_CSWeaponBase` | the weapon's **deploy tick** — while it is in the future the weapon is still being deployed |
| `m_nNextPrimaryAttackTick` | `C_BasePlayerWeapon` | the engine's **own** record of the tick at which the weapon may fire again |
| `m_nTickBase` | `CBasePlayerController` | the tick the client is simulating — the clock the record above is written in |

**Why `m_nNextPrimaryAttackTick` and not `m_flFireInterval` /
`m_flRefireTime`.** The specification listed three candidates "the schema
has all three". It does not: in this build's schema `m_flFireInterval`
belongs to `PointAngularVelocitySensor` and `m_flRefireTime` to
`TimerEntity` — **neither is a weapon field**. The weapon's own rate record
is `m_nNextPrimaryAttackTick`, and it is the field the engine itself
consults before it takes a shot, so it is the one the fire path uses. The
VData's `m_flCycleTime` is kept as the **fallback** for the case where that
record cannot be read.

**`m_flCycleTime` is a firing-mode float *pair*.** The schema types it as
`FiringModeFloat` (`0x738` and `0x73c`, two floats), and the dump does not
describe the struct's members, so **which** of the two the engine uses for the
weapon's mode is not established by the repo. The payload reads the **first**
one — that is the payload's reading, not a claim about the engine's. The
value is only kept when it is plausible: no weapon fires slower than 20 ms or
faster than two seconds.

**The rate is read fresh, on the game thread.** The VData's fields (the fire
mode, the cycle time) are stable and travel in the snapshot; the weapon
*instance*'s fields (the clip, the reload, the deploy, the next-attack tick)
are read **in the command path's tick**, where the weapon's state is
readable. A stale readiness record would let the ragebot press *inside* the
interval — on a semi-automatic weapon that is **the shot being wasted** — so
it is read per tick, not per frame.

### 15.2 The edge — the release a semi-automatic weapon needs

**The rule.** On a **semi-automatic** weapon (and on a weapon whose fire mode
is **unknown**), the ragebot's shots are separate shots: after a press, the
ragebot **withdraws** its attack contribution and keeps it withdrawn until
the weapon is ready again, so the engine sees a **release→re-press edge**
between two shots.

**When does it release?** On the first tick after the press, and then for as
long as the weapon is not ready. The window is deliberately longer than one
command tick (two ticks, `k_fire_release_window`): the release must be
visible on a command the engine **simulates**, and the tick boundary and the
command boundary are not the same instant.

**Why "until the weapon is ready again" and not "for one tick"?** The
engine's own next-attack record is the weapon's rate, so re-pressing on the
first tick the weapon is ready costs no rate at all. Releasing for a fixed
one tick would re-press *inside* the interval on a fast weapon and waste the
shot.

**Why not on a full-automatic weapon?** A full-automatic weapon fires
*while the trigger is held* — the engine's rate governs it, and the edge
would only cost a tick per shot. So the trigger stays down there, and the
behaviour is unchanged from before this section.

#### The withdrawal is real: the attack bit is **replaced**, not merged

The repo's rule (`strafe_geometry.hpp`'s `compose_button_mask`): *the
features' bits are only ever **set**, never cleared — a feature that releases
the attack button must not take the player's own press of the same button
away.*

That rule protects the **player's** press. It does not say that the payload
may never take **its own** press back — and it must be able to: a
contribution that can only be *added* can never be *removed*, so a withdrawal
would leave the press on the command and the engine would never see the
release. (The composition was add-only until this section, and that is
exactly why the ragebot could not withdraw: the command it re-wrote every
tick still carried the press of the tick before.)

So the command's attack bit is the one bit that is **replaced**, and it is
recomputed from its **contributors** for every command
(`compose_command_mask`):

| contributor | what it is | survives the ragebot's withdrawal |
|---|---|---|
| the **player** | the engine's own mask, read **before** the payload's write — and what the payload **knows** about it (see below: it can be **unknown**) | **yes** |
| the **control channel** | an explicit `press("attack")` | **yes** |
| the **triggerbot** | its own hold (a feature bit) | **yes** |
| the **ragebot** | the fire path's decision for this command | **no** — this is the one that is withdrawn |

The features' **other** bits are still only ever set, never cleared. Only the
attack bit is replaced, and only because the ragebot's contribution to it has
to be replaceable.

#### An **unknown** reading never removes the bit

The player's own press is the one contributor the payload has to **read**, and
the reading is not always a fact: the engine **reuses** the command objects
and the payload writes to two commands per tick, so a command can carry the
payload's own press as well — and then the player's press and the payload's
press cannot be told apart.

So the reading is **three-valued** (`PlayerPress`): `held`, `released`, and
**`unknown`**. And the invariant is:

> An **unknown** reading never **removes** the attack bit. The bit is cleared
> only on the strength of a reading that is **known** to be the player's own
> state.

An unknown reading leaves the command's own attack bit **as it is** — the
payload never takes away a bit it cannot prove it set — and the other
contributors (the control channel, the triggerbot, the fire path) still
apply. The cost is that the **release** can be a tick late on a command the
payload cannot read; the benefit is that the player's own press is never
swallowed. *When in doubt, do not fire* applies to the **player's** press
symmetrically.

**A clear bit is a fact.** The payload never clears the engine's own attack
bit (that is the invariant above), so a command whose attack bit is **clear**
says the player is not pressing — on a copy as well as on the command the
engine has just built. Only a **set** bit can be ambiguous.

#### The control channel's own **release**

The operator's `release("attack")` removes **the control channel's own**
contribution — and only that one. The **player's** own press and the
**features'** bits are only ever set, never cleared (the repo's rule), so the
triggerbot's hold and the ragebot's press survive it. (Before this rule the
release cancelled them as well, which changed the **triggerbot** — a feature
this task does not own — and left the operator with a silently silenced
weapon: the refusal still read `ok`, because the fire path's *decision* was
still "press". The default backend is `console`, where the control's masks
never reach the command at all, so the blast radius was the `usercmd` backend
only.)

#### The engine's own press is read **per command**

The payload writes to **two** commands per tick — the slot-25 copy of the
command (`create_move`) and the slot-5 command the engine has just built
(`create_move_input`) — and the engine **reuses** the command objects: a
command the payload wrote last tick still carries the payload's own press.
Reading the mask naively would therefore read the **payload's own press back
as the player's**, and the withdrawal would never clear the bit.

The payload caches what it knows about the engine's own attack bit **per
command number** (`CUserCmd::nSequenceNumber`, `FireCommandCache`):

| the command | the reading |
|---|---|
| the engine has **just built** it (the slot-5 command) | the mask is the engine's own — a **clean** reading: `held` when the attack bit is set, `released` when it is clear |
| a command the payload has **not** put its own press on | the same: a clean reading, refreshed |
| a command the payload **has** pressed | **not** clean: the reading is the one taken before the payload's write |
| the **copy** of a command the cache has not seen | **unknown** when the attack bit is set (the copy may carry the payload's own press), `released` when it is clear |

The cache never answers with a **stale** value: a command's reading is either
clean, or the one taken before the payload wrote to it. (The earlier version
answered a copy it did not know with the last **clean** reading it had — a
value from an earlier tick — and the payload's replacement of the attack bit
took that stale answer for the player's own state: the player's own press was
**deleted** from the command, from the PB's held word and from the movement
services' word. The status line's `player?` is what that case looks like now.)

**The premise, and where it holds.** "A command the payload has not pressed
carries the engine's own mask" is established for the command the engine has
**just built** — the engine builds it, so its mask is the engine's own — and
it is **not** established for the **copy**: the engine copies the command the
payload has just written to, so the copy may carry the payload's own press.
That is why the copy's reading is **unknown** when the attack bit is set, and
why the payload does not depend on the order of the two slots (which was
never observed at runtime, `docs/re/angle-split.md`).

**What happens when the player holds attack.** Then the engine's own mask
carries the attack bit on every command, so the engine sees **one continuous
press**: the semi-automatic weapon fires **once**, exactly as it does for a
human who holds the trigger. **The edge is then the player's to make, not the
ragebot's** — the ragebot's withdrawal cannot take the player's press away
(that is the rule above), so it cannot manufacture an edge the player has
removed. The status says so: `player` on the HUD line, and `player_attack` in
the `rage` command's JSON.

### 15.3 The gate — can the weapon take the shot?

The gate is evaluated on the **game thread**, once per tick, and its verdict
is published to the command path. The conditions, **in the order the code
checks them** — the first one that blocks is the one the operator is told
about:

| condition | refusal | why |
|---|---|---|
| no active solve | `no-solve` | the ragebot is not aiming at anybody |
| the fire path is **disabled** | `fire-off` | the operator's `fire` toggle is off, or the visibility gate (`only_visible`) refused the shot — a different fact from "no solve" |
| the clip could **not** be read | `clip-unknown` | the payload does not know whether the weapon can shoot — *when in doubt, do not fire* |
| the clip is empty | `no-ammo` | the shot cannot be fired; the auto-reload assist already holds `IN_RELOAD` |
| the weapon is reloading | `reloading` | the engine ignores the trigger |
| the weapon is still being deployed | `deploying` | same |
| the deliberate release the edge needs | `edge` | not a block: the fire path is *choosing* to release so the next shot is a new edge |
| the weapon's fire interval has not elapsed | `interval` | the shot would be dropped by the engine — and on a semi-automatic weapon a press inside the interval is **the shot being wasted** |
| the weapon's rate could not be read at all | `rate-unknown` | neither the engine's record nor the VData's cycle time was available: the rate is *unknown*, which is not the same fact as "the interval has not elapsed" |

**The "unknown" degradation.** A weapon whose VData could not be read has an
**unknown** fire mode. The fire path then treats it as **semi-automatic** —
the conservative direction: the re-press edge costs at most a tick per shot
and is always correct, while a held trigger on a semi-automatic weapon fires
**once** and never again. The status says `unknown`; it never guesses `auto`.

**The rate's two sources.** The engine's own record
(`m_nNextPrimaryAttackTick`) is authoritative. When it cannot be read, the
VData's cycle time is the fallback. When **neither** is available the gate
refuses the shot — and it says `rate-unknown`, not `interval`: a refusal the
operator cannot act on is a silent refusal with a name on it. *A gate that is
wrong in the "don't fire" direction is safe; a gate that is wrong in the
"fire" direction is not.*

### 15.4 What the operator sees

The HUD line (`rage: …`) and the `rage` command's JSON carry the fire path's
verdict. The line:

```
rage: AIM tgt 12 box 1 obj hitchance s12 p13 e12 semi ok
```

| field | meaning |
|---|---|
| `tgt` / `box` | the target's index and the hitbox |
| `obj` | the objective's name |
| `s` | the **engine's** shots — the clip fell while the ragebot was pressing |
| `p` | the ragebot's **attempts** — the ticks the fire path decided to assert the attack |
| `e` | the release→re-press **edges** the fire path produced |
| `semi` / `auto` / `unknown` | the weapon's fire mode as the payload reads it |
| `ok` / `no-solve` / `fire-off` / `clip-unknown` / `no-ammo` / `reloading` / `deploying` / `edge` / `interval` / `rate-unknown` | the gate's verdict: `ok` when the shot can be taken, the blocking condition's name when it cannot |
| `player` | the **player** is holding the trigger — the engine sees one continuous press, so the edge is the player's to make |
| `player?` | the payload **cannot tell** whether the player is holding the trigger (its own press may be on the command it is reading) — the bit is left alone in that case, never removed |

**The line fits the HUD's buffer.** The HUD's rage line has a **192-byte**
buffer (`payload/features/src/overlay.cpp`), and the line must fit it in the
**worst** case — every field at its longest. The fields that do not fit are
the aim **writes** and the **backtracks**: they are diagnostics, and they are
in the `rage` command's JSON (`writes`, `backtracks`) instead. The `fire`
toggle is not on the line either: the refusal says `fire-off` when the fire
path is disabled, which is the same fact and costs less. The host test drives
every field to its longest and measures the line (**186** characters, with
`player?` and `rate-unknown` as the longest ones).

The JSON carries the same facts under `shots`, `presses`, `edges`,
`fire_mode`, `fire_refusal`, `player_attack` and `player_press`, plus
`writes` and `backtracks`.

**`player_attack` and `player_press`.** `player_attack` is the **bool**: the
player is *known* to be holding the trigger. `player_press` is the same fact
with all three states: `"held"`, `"released"` or `"unknown"` (the payload
cannot tell the player's own press from its own — and an unknown reading
never removes the attack bit, so the player's press is safe either way).

**`shots` changed meaning.** The counter that used to answer `shots` counted
the **commands** the attack was asserted on — the ragebot's *attempts*, not
shots (on a full-automatic weapon it grew at the tick rate). It is now the
**engine's** shots, detected by the clip falling, and the attempts are
`presses`.

**A reload is not a shot, and a failed read is not a shot.** The detector
needs **both** clip readings to be known and **neither** to be a reload: a
manual reload empties the magazine (30 → 0 → 30), and a **failed** read (a
stale weapon entity, a weapon switch between the snapshot and the read)
reads as zero. Neither is the engine's shot.

**`e` is not always zero.** On a full-automatic weapon the trigger stays
down, so there is no edge — but an automatic weapon whose clip empties
produces a withdrawal (`no-ammo`), and the next press after the reload counts
as an edge.

### 15.5 What the host proves and what only the game can prove

**The host tests prove that the payload *emits* the edge** — and they drive
the **whole wiring**, not a neighbour of it:

| what | the test |
|---|---|
| the **decision** — the edge, the gate, the fire mode, the refusals | `decide_fire` is a pure function (`cs2/control/strafe_geometry.hpp`), and the test drives it |
| the **write** — the command's mask, the PB's button words and the movement services' word | `apply_fire_write` is the payload's own write, and the test drives it against plain memory and reads the **bytes** back |
| the **edge on the command** | the test drives the payload's own per-tick loop: press → withdraw → press, and asserts that the command's attack bit goes **1 → 0 → 1** |
| the **player's press survives** | the same loop, with the player holding the trigger: the withdrawal must leave the bit set |
| the **unknown reading** | the cache's three-valued reading, and the invariant: an unknown reading leaves the command's attack bit **as it is** — the review's own probe (the **copy first**, the player pressing) must show `attack=1` |
| the **command cache** | `FireCommandCache` is a pure class, and the test drives it |
| the **shot counter** | `is_engine_shot` is a pure function: a reload and a failed read are not shots |
| the **status line** | `format_rage_status` is a pure function, and the test drives every field to its longest |
| the **wiring** | the test reads the payload's own source (`input.cpp`), **anchored** to `Input::apply_command`, and asserts that the write is the pure function's, that all three channels are **passed**, that the ragebot's press is the **service's decision**, and that the player's own press is what the cache knows |

The last one is what catches the breaks the reviews named: a ragebot that
holds `IN_ATTACK` down on every command (the original defect), a withdrawal
that takes the player's own press away (the rule the repo states), a write
that is computed and then **ignored**, and a channel that is **dropped** are
all one line away from the pure functions, and the pure functions' tests
cannot see a line that is not called. The check is **anchored**: the
statements have to be inside `Input::apply_command` itself, so a neighbouring
occurrence elsewhere in the file cannot satisfy it, and they have to be
**present** — a channel that is not written fails the check.

The **behavioural** test is the other half: the payload's write is one call
(`apply_fire_write`), the test drives that call with the three channels as
plain memory, and asserts what each of them carries. A call site that
composes the mask and then ignores it cannot exist, because the composition
**is** the write.

**They cannot prove that the engine *honours* the edge.** Whether a
semi-automatic weapon really needs a release→re-press, and whether the engine
reads the release from the command's mask, from the PB's button state or from
the movement services' held word, is **engine behaviour**: only the
operator's live run can show it.

#### The three channels, and what each of them carries

| channel | what the payload writes | the release |
|---|---|---|
| the command's legacy mask (`cmd + 0x60`) | the attack bit, **replaced** by the contributors | the bit goes to **zero** |
| the PB's `CInButtonStatePB` | the **held** word (`buttonstate1`) follows the command's mask — **always**, not only when there is something else to write; the **changed** word (`buttonstate2`) carries the attack on a press and on a withdrawal | the held word goes to **zero** |
| the movement services' `CInButtonState` (word 0) | the **held** word follows the command's mask — **always** | the word goes to **zero** |

So the release is written to **all three** — and the earlier claim was false
for two of them, because the PB's block was guarded by "there is something to
write" and the movement services' word was only ever *set*.

**The PB's changed word is a best-effort mirror, not a proven edge channel.**
The engine's own edge machine is not fully re-derived: the press edge is
`FUN_1804c0f10(&svc->m_nButtonState, bit)`, a three-word machine
(`edge = C || (A && B)`: held now, changed since the command start, pressed and
released). The payload writes the PB's changed word when **its own**
contribution changed — a press or a withdrawal — because a release is a change
too. Which of the three channels the engine's edge machine actually reads is
**not** established: the operator observed a semi-automatic weapon firing
**once** under the old code, so the engine evidently does not take the press
edge from the PB's changed word. The release that matters is the **held** word
going to zero, on all three channels — which the fix does deliver.

### 15.6 The known limits

- **A burst weapon is treated as semi-automatic.** `m_bHasBurstMode` is not
  read, so a weapon with a burst mode (the Tec-9, the R8) is classified
  `semi` and the edge is required between its shots. The burst's own
  repetition is the engine's.
- **The rate's tick comparison is conservative.** The client's tick base is
  the tick it is simulating, and the engine's record is a tick at least that
  far ahead, so the gate can be **late** by a tick but never early.
- **The shot attribution is a best guess.** `shots` counts the clip falling
  while the ragebot was pressing; with the player firing as well, the two
  cannot be told apart. `presses` and `player_attack` are there to read it.
- **The decision is made once per tick**, so every command of the tick
  carries the same answer. Which of the two commands the engine reads first
  is not observed at runtime (`docs/re/angle-split.md`): slot 5 runs first
  and slot 25 second, but the engine's own ordering of the two slots follows
  from the signatures, not from a measurement.
- **The engine's own press is read per command.** A command the payload has
  already put its own press on is not a clean reading, so the cached value is
  used for it. The engine builds a new command every tick, so the reading is
  refreshed every tick. The **copy** of a command the cache has not seen is
  the exception: its reading is **unknown** when the attack bit is set (the
  copy may carry the payload's own press), and an unknown reading leaves the
  bit **as it is** — so the player's own press is never removed, and the
  release can be a tick late on that command. Which of the two slots runs
  first is not observed at runtime, so the payload does not depend on it.
- **Whether the engine honours the edge is engine behaviour** — the host
  tests prove the payload *emits* it, the operator's live run proves the
  engine *reads* it.
