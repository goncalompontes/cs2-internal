# Ragebot — implementation review vs. the Tier-2 plan

> **Scope.** An audit of the shipped ragebot (`payload/aim`, `payload/control`,
> `overlay`) against the design locked in `docs/ragebot.md` (ADR-0022, Tier 2) and
> the brief in `NEXT_SESSION_OCCLUSION.md`, plus the fixes applied. RE evidence is in
> `docs/re/bullet-trace.md` (the filter/mesh findings) and `docs/re/occlusion.md`.
>
> Method: read the whole aim core + the payload bridge + the live wiring; re-derived
> the bullet *filter* from the live `client.dll` with a capstone pass
> (`artifacts/re_filter.py`); probed the live world geometry in a de_dust2 bot match
> (`artifacts/floor_probe.py`, `artifacts/seam_probe.py`).

## 1. Plan vs. implementation

| Tier-2 plan (`docs/ragebot.md` §3/§4) | Implementation | Verdict |
|---|---|---|
| Read collision hulls/meshes directly; **no per-ray traces** | `occlusion_query.cpp` reads hulls/spheres/capsules/meshes; `aim` projects them; `ray_span` is planar/triangle arithmetic | ✅ architecture held |
| `cone query → shape*`, hull `+0xC0`, mesh `+0xC8`, compound recurse | all read; compound mapped but inert live (`docs/re/occlusion.md`) | ⚠️ compound unvalidated |
| Project hull edges → exact cells + thicknesses → analytic heat map | `project_*` + `clip_convex`/`subtract_convex` + `OccluderCell::ray_span` | ✅ |
| The **bullet mask** `0x1c300b` on the enumeration | payload passed the engine *static default* filter (mask `~0`) and a `contents` arg | ❌ **fixed** (§2.1) |
| Charts of every shape the engine traces (incl. displacements/meshes) | meshes read as triangle soups, but capped at 65536 | ❌ **fixed** (§2.2) |
| Occluder set = "every solid between eye and hitbox" | `ray_span` depth test + conservative broad phase | ✅ |
| `max damage` = cell's **nearest** point; `hitchance` = Chebyshev centre | `argmax_damage` used the point nearest the **chart origin** (world +X) | ❌ **fixed** (§2.3) |
| Pose-blurred field `Ê` | `argmax_expected` (Gaussian quadrature) | ✅ (metric now fixed) |
| Deterministic spread / pellet model | `spread.hpp` + pellet objective | ✅ (pending live cross-check, unchanged) |
| Backtracking over the lag record ring | `records.hpp` + `solve_backtrack` | ✅ |
| Chart = absolute world yaw/pitch, camera-independent | `Basis::world()` | ✅ (metric bugs below) |

## 2. Flaws found and fixed

### 2.1 The enumeration filter did not use the bullet's collision mask

**Finding.** `EntitiesInBox` reads the collision mask from **`*(qword*)(filter + 8)`**,
not from its `contents` argument (`FUN_1809dbd20`; `r9 = filter+8` handed to the
world query at vtable `+0x5d0`). The engine's static default filter
(`client+0x2227070`) carries `+8 = 0xffffffffffffffff` — **all** contents — so the
payload enumerated a superset, including `CONTENTS_PLAYERCLIP` brushes a bullet
passes through. Full disassembly and the field map of the bullet filter
(`FUN_180352920`: `+8 = 0x1c300b`, `+0x37 = flags`, `+0x38 = 3`) are in
`docs/re/bullet-trace.md` §"The bullet filter".

**Fix.** `occlusion_query.cpp` now builds a **custom filter**: it byte-copies the
engine's static filter object (keeping the engine vtable the query reads far past
`+8` — a payload-built vtable faults the query) and writes the bullet mask, group and
flags. `walls.filter_custom` / `filter_mask` / `filter_group` / `filter_flags` report
it live.

> ⚠️ This was **not** why the floor was missing — the all-ones mask is a superset, so
> it cannot hide geometry. It *does* remove false occluders (playerclip/nodraw) so
> the set matches what a bullet crosses.

### 2.2 The floor (and every large surface) was silently dropped by a mesh cap

**Finding (live).** The worldspawn body's 6 collision meshes on de_dust2 include one
with **101097 vertices / 104429 triangles** spanning the whole map footprint
(`[-4736,-2496,-320] → [3584,4608,964]`). The reader's `k_max_mesh_vertices` /
`k_max_mesh_triangles` were **65536**, and `parse_mesh` *returned false* for a larger
mesh — so that surface was absent from the occluders entirely. Evidence:
`artifacts/floor_probe.py` output, recorded in `docs/re/bullet-trace.md` §"Why a
wall/floor was still missing".

**Fix.**
- Raised the caps to `1 << 20`; a bigger mesh is no longer skipped.
- `emit_mesh_clusters` splits a mesh into spatially bounded clusters (≤2048
  triangles, split by the widest axis at the median) so a map-wide mesh becomes
  many small occluders with their own world AABB — the floor occludes exactly where
  crossed, instead of becoming one chart-wide silhouette that fragments everything
  and makes `ray_span` scan the whole soup.
- Mesh clusters now cache a world AABB (identity transform) so the per-query
  broad phase is O(1) per cluster.
- `project_occluder`/`project_mesh_occluder` drop silhouette vertices **behind the
  solid's own centre**, so a solid the eye stands beside cannot wrap around the chart.

`walls.mesh_clusters` reports the count.

### 2.3 The chart metric (the angle-dependent failure) and the yaw seam

The chart is absolute world **yaw/pitch**: `u` is a yaw about `+Z`, so one chart
radian of yaw is only `cos(pitch)` of a true angular radian. Three places ignored
this; all made the ragebot's behaviour depend on the target's **bearing/pitch** —
"works level with a target, stops working up/down or across the map".

1. **`project_sphere`** drew a chart circle of angular radius `rho`; the true
   silhouette is `rho/cos(pitch)` wide in yaw. **Fixed:** the silhouette is now built
   from the exact **cone of directions** in 3D and projected (exact at any pitch).
2. **`hit_probability` / `argmax_expected`** integrated an isotropic chart Gaussian;
   the physical cone is an ellipse in the chart (`sigma_u = sigma/cos(pitch)`).
   **Fixed:** anisotropic kernel (`gaussian_offsets_aniso` + `yaw_scale(pitch)`).
3. **`argmax_damage`** returned the best cell's point nearest the **chart origin**
   (world `+X`), not the target. The value is constant across a cell, so this was
   arbitrary; worse, the measured `hitchance` at that edge fed the **fire gate**, so
   the gate passed/failed by the target's absolute bearing. **Fixed:** the best cell's
   Chebyshev centre (most robust point; also angle-independent).
4. **±π yaw seam.** `project_capsule`/`project_obb`/`project_occluder` hulled chart
   points without unwrapping yaw, so a target to the *west* (world yaw ≈ ±π) produced
   a chart-wide hull across the seam. **Fixed:** `convex_hull_unwrapped` shifts every
   yaw into a ±π window about the silhouette's own centre. (The live `seam_probe.py`
   showed the aim sitting at `u ≈ -3.13`, i.e. exactly on the seam.)

Regression tests: `aim-geometry` "sphere yaw width scales with `1/cos(pitch)`" and
"unwrapping keeps the seam silhouette tight"; `aim-field` "hitchance is
pitch-invariant".

### 2.4 The hitchance cone is fed unvalidated units

**Finding.** `Input::apply`'s ragebot path computes
`spread_sigma = sqrt(inaccuracy² + spread²)` from the weapon profile and uses it as
**radians**. Live (`artifacts/seam_probe.py`) the reported cone was **0.0935 rad
≈ 5.4°** for an idle player — implausibly large for a standing shot, which drives the
hitchance down (`0.125` at the seam) so any realistic `min_hitchance` blocks firing.
The docs already flag `m_flInterpolatedInaccuracy` as a client *crosshair* field and
`FUN_18080b880` (the composition) as un-reversed (`docs/re/aim.md` §"Inaccuracy").

**Status.** The metric and the plumbing are now correct, and `aim`/`rage` report
`cone` / `weapon_inaccuracy` / `weapon_spread`; the **unit/scale of
`m_flInterpolatedInaccuracy` still needs a live cross-check** against the engine's
shot magnitudes (`CMsgTEFireBullets` / `FUN_18080b880`). Until then a non-zero
`min_hitchance` can over-gate. This is the top remaining correctness item.

### 2.5 Smaller observations (not changed)

- The rage path's `damage`/`headshot_multiplier` menu values are overwritten by the
  real weapon VData (`build_input(..., weapon_from_game=true)`), so the sliders are
  mostly inert — intentional but confusing in the menu.
- `argmax_hit_probability` is a grid search over the whole field bounds; fine for a
  single target, costlier for the heatmap.
- `occlude_polygon` attributes a fragment to an occluder by its **convex-hull**
  silhouette then verifies with `ray_span`; a mesh cluster's hull still over-covers,
  which is harmless (the exact test decides) but is the perf weak point.
- The enumeration is a body-box overlap, so the *rendered* occluder list is not
  literally the engine's trace set (over-inclusive around the target). Correctness for
  a shot is unaffected (the segement is inside the box).

## 3. Features added (`docs/ragebot.md` §0)

Menu (`wire::Settings`, `k_version` 8 → **9**) + `rage` command + payload:

| Feature | Values | Behaviour |
|---|---|---|
| **Auto-stop** (`rage_autostop`) | 0 off / 1 always / 2 when firing | zeros the move axes, clears the move bits and suppresses auto-strafe while a solve is live |
| **Auto-scope** (`rage_autoscope`) | 0 off / 1 always / 2 when firing | holds `IN_ZOOM`/`IN_ATTACK2` while solving |
| **Auto-reload** (`rage_autoreload`) | bool | holds `IN_RELOAD` when the clip is empty |
| **Only visible** (`rage_only_visible`) | bool | fires only when no wall attenuates the chosen aim (`SolveResult::penetrated`) |

`rage` now reports `autostop`/`autoscope`/`autoreload`/`only_visible`/`visible`/
`stops`, and the HUD status shows `stop`.

### 3b. Second pass — trigger modes, global optima, per-weapon configs (2026-10-19)

- **Trigger modes.** `wire::Settings.rage_mode` (menu *Trigger* combo, `rage mode=…`):
  0 = hold the key, 1 = always on, 2 = toggle on the key. The key is optional (with
  *always on* no key is needed; `key = 0` in hold mode means always, for back-compat).
- **Global point selection.** `argmax_damage` now maximizes damage and then the local
  margin over **every** cell (not the best cell's own optimum), `argmax_hitchance`
  maximizes the margin among comparable-value cells, and `argmax_hit_probability` seeds
  from every cell. `polygon_margin`/`field_margin` provide the per-point robustness.
- **Per-weapon configs + persistence.** `payload/control/rage_config.hpp` — a POD
  `RageProfile` per weapon designer name (objective/hitbox/trigger/key/fov/min-damage/
  min-hitchance/assists/fire), `RageConfigStore` (32 profiles), persisted to
  `%LOCALAPPDATA%\cs2-internal\cs2-rage.cfg` (loaded in the `Service` constructor).
  `per_weapon` applies the profile matching the active weapon at solve time. Edited
  from the menu (weapon field; Save-to / Load-from / Delete / Save-file / Load-file)
  and the `rage` command (`weapon=`, `per_weapon=`, `profile_save`/`profile_load`/
  `profile_delete`, `save`/`load`). `wire::Settings` v10.
- **Auto-stop / auto-scope fixed.** The earlier implementation only wrote the command's
  base move and cleared the move button bits — but the engine derives the move from the
  raw axes (`CCSGOInput + 0x270`) and its own button word, so the player's WASD still
  moved the shot. Auto-stop now zeroes **every** channel (raw axes *before* the engine's
  copy in `write_raw_move`, the command base + both button states after, and the engine's
  held-button word at the movement services), and suppresses the auto-strafe. Auto-scope
  / auto-reload now also mirror zoom/attack2/reload into the button-state **changed**
  word (so the engine sees a fresh press edge) and into the engine held-button word.

### 3c. Third pass — predictive assists, speed-limited stop, recoil, weapon gate (2026-10-19)

- **Predictive assists.** Auto-stop / auto-scope are decided **before** the trigger from
  the target selection (a target in FOV/range = "can deal damage"), so the player is
  already still/scoped before firing; mode 2 means "when a damageable target exists"
  rather than "when firing". They are applied through `Input` even when no solve is
  active (`clear_rage_aim_keep_aux`).
- **Speed-limited auto-stop.** `rage_stop_speed` (units/s, menu slider): 0 = a full stop;
  > 0 = **counter-strafe** only until the horizontal speed is at/below the limit (the
  fastest movement that does not add spread) — the move axes are computed opposite the
  velocity in the aim frame, then released.
- **Recoil compensation.** `rage_recoil` (`m_pAimPunchServices -> m_predictableBaseAngle`)
  is subtracted from the silent aim each tick, so the shot (which inherits the punch)
  lands on the target.
- **Sub-tick fallback.** The silent aim now also writes the command's own `viewangles`
  (`base+0x40`), the legacy field the shot falls back to when the `input_history` has no
  entry — the cause of the intermittent "fires with my current view".
- **Firearm gate.** Only `SWeaponType` 1–6 (pistol/smg/rifle/shotgun/sniper/machinegun)
  are ragebot targets; the knife, C4, taser, grenades and equipment are excluded
  (`is_rage_weapon`, with a designer-name deny-list fallback). `wire::Settings` v11.

## 4. Verification

- Host tests: **16/16** (`meson test -C build`), including the new geometry/field
  regressions and the `penetrated` assertion.
- `meson compile -C build-payload` clean. `build-overlay` compiles and links (a
  running game holds the DLL, so CS2 must be restarted before relinking).
- **Live (de_dust2 bot match, the rebuilt payload).** The `heatmap` JSON now reports:
  `filter_custom = true`, `filter_mask = 1847307` (**`0x1c300b`**), `filter_group = 3`,
  `filter_flags = 15`; `mesh_clusters = 83` and `world_cache_hulls = 10226` (was
  `10148` — the 78 newly parsed solids are the clusters of the mesh the old cap
  dropped). `artifacts/floor_probe.py` had shown that mesh at 101097 verts / 104429
  tris. `artifacts/seam_probe.py` (old payload) showed the camera-independent value and
  the aim sitting at `u ≈ -3.13` (the seam) with `cone 0.0935`.

## 5. Next actions

1. **Inaccuracy units** — reverse the current build's `GetInaccuracy` composition
   (`FUN_18080b880` equivalent) and pin the radian scale; cross-check against
   `CMsgTEFireBullets`. (§2.4)
2. **Live re-validate** the floor: rerun the heatmap with a wall/floor between eye
   and target and confirm `mesh_clusters > 0` and attenuation.
3. **Tier-1 trace oracle** — swap the box enumeration for the engine's own
   `TraceLineIntervals` segment trace so the solid set is exactly the bullet's
   (`docs/re/bullet-trace.md` §"Still open").
4. **Mesh BVH** for `ray_span` and a tighter per-cluster silhouette.
5. Compound (type 4) live validation.
