# Hand-off — overlay / payload state

> **NEXT SESSION: start at [`docs/next-session-ragebot-perf.md`](next-session-ragebot-perf.md).**
> The in-process Present renderer landed (ADR-0023; the Vulkan layer is now a fallback),
> and the ragebot was instrumented and optimised this session. The remaining work is the
> **occlusion query volume** (cone/frustum over the `AabbBvh`, plus a depth-sorted
> "fully impenetrable" early-exit), **moving the remaining game logic into the frame
> stages** (out of the renderer), and the lag-comp/inaccuracy correctness items. Perf
> numbers, instrumentation, the uncommitted-file list and the run/gotcha details are all
> in that doc.

> **Previous session: [`docs/next-session-present-hook.md`](next-session-present-hook.md).**
> That was the in-process renderer + ragebot-follow-ups brief; the renderer part is done
> (ADR-0023), the ragebot follow-ups (auto-stop, recoil, cursor) are still open and are
> carried into the new doc.

## ⏸ Wrap-up (2026-10-19) — in-process renderer + ragebot instrumentation/perf

**Resume at [`docs/next-session-ragebot-perf.md`](next-session-ragebot-perf.md).**

- **In-process Present renderer (ADR-0023).** ESP + HUD + menu now render from the
  game's own `IDXGISwapChain::Present` via the DXGI method-table interposer and the
  payload's ImGui D3D11 backend. New `features::Menu` (payload menu on the present
  thread: WndProc subclass, cursor release, and an SDL3 `GetRawInputBuffer` IAT hook so
  the game's mouse-look does not move the camera behind the menu). `payload_main`
  defaults to it, with the Vulkan layer as a fallback (env `CS2_PAYLOAD_RENDER`).
  `scripts/launch-cs2.cmd` now defaults to D3D11 (no `-vulkan`). The historic DXGI probe
  crash was the payload thread skipping `DLL_THREAD_ATTACH` — the in-process install
  runs on a normal thread. Validated live (de_dust2, ESP + menu drawn in-process).
- **Ragebot perf/instrumentation (`docs/ragebot-perf.md`).** The solve was ~12 ms/frame
  on the render thread; root causes were (a) a full `solve` per backtrack pose and (b) a
  41×41×32 sampled objective. Fixed: exact Gaussian-mass objective (boundary integral)
  with **gradient ascent** (analytic flux gradient — beats the old grid), the solve moved
  into the **CreateMove hook** (once per tick, before the raw writes), rewind-tick pose
  selection, exact blocked-fragment pruning (no caps), a **mesh triangle BVH** for
  `ray_span`, and a **world-hull AABB BVH** for the enumeration. Remaining bottleneck:
  the **world-solid projection** (hundreds of solids at some positions) — see the new
  handoff.
- **Uncommitted.** Everything since `77f2548` is uncommitted (31 modified, 8 new);
  commit before continuing.

> **Archived brief (occlusion): [`NEXT_SESSION_OCCLUSION.md`](../NEXT_SESSION_OCCLUSION.md).**
> It is self-contained: the goal (make the Tier-2 enumeration match the engine's trace
> **exactly**, no inference, using the `rea` plugin), what is already reversed, how to
> reverse the big DLLs, the live workflow + saved position, and the acceptance criteria.

> **Validated on native Windows (2026-10).** The payload was
> manual-mapped into CS2 on a real Windows host and read live player state; the
> Vulkan overlay layer rendered the HUD + ESP into the swapchain; a
> windowed/fullscreen toggle did not freeze the game. See
> [docs/windows-validation.md](windows-validation.md). The historic "native
> Windows — untested" marker has been removed. Originally validated on build
> **25815307**; re-validated on **25947352** (the game auto-updated — see
> [Running the scenario harness](#running-the-scenario-harness--operational-hand-off-2026-10-15),
> §2).

Read alongside `decisions.md` **ADR-0015/0016/0017/0018/0019/0020** and the RE
agent guide `docs/re/README.md`.

> **Always cross-check RE findings against community sources.** Before trusting a
> decompiled offset/slot/pattern, search the community reverse for the same
> symbol — UnknownCheats (esp. the CS2 *Reversal, Structs and Offsets* thread and
> the `CreateMove`/`CUserCmd`/`CCSGOInput` threads), GitHub CS2 internal/cheat
> sources, and `cs2-dumper`. Use it to predict, confirm and sanity-check, then
> re-derive on our binary (their build ≠ ours) and cite the sources (URL + build
> date) in `docs/re/<module>.md`. This is step 4 of the loop in
> `docs/re/README.md`.

> **Active feature:** the programmatic scenario/control harness. The RE is done
> and the **control core + input + game events are implemented and validated**
> (`payload/control/` + `scripts/scenario/`). The **`usercmd` input backend is
> implemented and validated live**: discrete buttons (`usercmd-move` drives jump +
> bhop) and the **sub-tick view angles** (`look(..., via="usercmd")` writes every
> `input_history` entry's `view_angles`; `subtick-aim` shows it is silent,
> `subtick-shot` proves the shot follows it). **Analog movement is validated
> live** — `move(forward=1.0)` walks 204 units/s (1.01× the engine's own console
> `+forward`) and `move(forward=0.5)` walks 115 (0.57×): the write needed the
> engine's units (`±1`, not 450) and a clear test spot (`movement-probe` pins one).
> Resume from the operational hand-off below,
> [`docs/control-harness.md`](control-harness.md) (design, decisions, next
> actions), [`docs/re/subtick.md`](re/subtick.md) (the sub-tick structures, the
> command build and the movement consumer) and
> [`docs/re/client-dll.md`](re/client-dll.md) (the input path + game events).
>
> **As of 2026-10-17 the active work is the ragebot heatmap + the occlusion layer**
> — see the wrap-up immediately below (`docs/ragebot.md` §0, `docs/re/occlusion.md`,
> `docs/re/lagcomp.md`).

## ⏸ Wrap-up (2026-10-18) — the rage solve is occlusion-aware

**Resume here.** Closes the #1 functional gap (`docs/handoff.md` 2026-10-18 "Next").

- **The occluders now feed the solve.** `build_input` never filled
  `SolveInput::occluders`, so the ragebot aimed at fully-occluded hitboxes; only the
  `heatmap` command used the walls. The hitbox-occlusion logic was extracted from
  `build_heatmap` into a shared **`occlude_polygon`** (`payload/aim/{include/cs2/aim/
  damage.hpp,src/damage.cpp}`), used by both the heatmap and `solve` (when
  `SolveInput::occlusion` is non-empty). `service.cpp` projects the game-thread
  enumeration into the solve's chart (the same projection the heatmap does), sets
  `PenetrationModel::engine` + the weapon's real `m_flPenetration`, and the rage path
  samples/enumerates whenever it is active. `rage` reports `occluders`/`backtracks`/
  `record`; `aim-solver` gained thin-wall-block/gate cases. Host **16/16**, payload
  builds, preflight clean.

## ⏸ Wrap-up (2026-10-18) — enumeration AABB cache + compound recursion

**Resume here.** Two perf/coverage passes on the occlusion layer.

- **Per-hull AABB cache (`occlusion_query.cpp`).** The per-query broad phase used to
  recompute each hull's AABB **from its vertex array** (`hull_bounds`, O(vertices)). Each
  cached hull now carries its own `RnHull_t` header AABB (`+0x14`/`+0x20`, read once at
  parse time; `OcclusionHull::has_bounds`), so the filter is **O(1) per solid**.
  **Live-verified exact:** the header AABB equals the vertex-derived AABB for **all
  10143** world hulls (worst error `0.0000`).
- **Compound (4) recursion (`occlusion_query.cpp` `case 4`).** The pool is now read:
  `RnCompound_t` (0x160 B at `shape+0xC0`) stores children **by value** — `RnHull_t[]`
  at `+0xF0`/`+0xF8` (stride `0xF8`), `RnMesh_t[]` at `+0x28`/`+0x30` (stride `0xC0`) —
  and the payload recurses into both (`parse_hull`/`parse_mesh`), transformed by the body
  transform, with bounds checks. **Finding:** CS2 gameplay props are multi-hull **bodies**
  (many `RnHull_t` shapes in one `CPhysicsBody`), **not** compound shapes — the world
  body is `{hull 10143, mesh 6}`, spawned `prop_physics`/dropped weapons are all hulls,
  and no type-4 shape was found live — so the path is inert in practice (implemented, not
  live-validated). The shape's separate 0x30-byte child table at `shape+0xD0` and the
  per-child transforms remain unpinned (need a live compound).
- **BVH sweep — still blocked.** `EntitiesAlongLinearSweep` (`FUN_1809db9a0`) takes a
  caller **`Ray_t`/shape descriptor** as `param_2` (passed to `world->vtbl[0x5a8]`),
  exactly like `TraceShape` — and that descriptor is not yet pinned (`docs/re/aim.md`
  §"The call convention"). The box query is already BVH-accelerated at the record level,
  so guessing the descriptor risks a misbehaving engine call for no gain; pin it, then
  swap the box for the swept query (P1.8).
- Verified: `meson test -C build` **16/16**, payload builds, preflight clean; live
  (viewer `--pen engine`, ~23 ms/poll warm). Docs: `docs/re/occlusion.md`,
  `docs/re/vphysics2.md`.

## ⏸ Wrap-up (2026-10-18) — the real penetration model + the `penetration` render mode

**Resume here.** Adds the engine's real penetration/damage model and surfaces it as a
new debug-renderer mode.

- **Real penetration (`payload/aim/penetration.hpp`).** The occluded cells used the
  first-pass `∏(1 − t·mod/m_flPenetration)`; now `pen=engine` applies the engine's
  per-wall loss (the community-verified `CBaseEntity::HandleBulletPenetration`
  arithmetic): a fixed entering fraction (0.16; 0.05 through a grate/glass), a
  `modifier·3·max(0, 3.75/m_flPenetration)` term and a `thickness²·modifier/24` term,
  over a **combined penetration modifier** from the entry/exit surface properties, with
  the 4-wall budget and the reversed loop's ≈90-unit crossed-thickness cap
  (`FUN_18089ca80`, re-decompiled this session). `OccluderCell` carries the surface
  (`surface_modifier`/`damage_modifier`/`material`/`grate`); `HeatCell::base_value` is
  the unoccluded damage; `HeatmapOptions::penetration` selects `simple`/`engine`. Full
  write-up + citations: **`docs/re/penetration.md`**. Host test `aim-penetration`,
  **16/16 pass**; payload builds; preflight clean.
- **New render mode `penetration`.** `scenario/heatmap.py :: render(mode="penetration")`
  fills each cell with the engine-model damage, overlays the projected solids, and
  outlines each cell by its attenuation fraction (`value/base_value`; blue = untouched,
  red = removed). Wired into `heatmap-view.py --mode penetration --pen [engine|simple]`,
  the live viewer (`heatmap-live.py`, a *penetration* mode + `pen`/`pen_mod`/`dmg_mod`
  controls), `heatmap-walls.py` (a `*_pen.png` per view) and the `heatmap` scenario
  (`artifacts/heatmap_pen.png`). The `heatmap` command gained
  `pen`/`pen_mod`/`dmg_mod`/`max_pen`/`material`/`grate` and reports
  `walls.penetration_model` (+ per-cell `base_value`).
- **The exact CS2 formula + the real per-surface modifiers (closed, same session).**
  `HandleBulletPenetration` was found and decompiled (`client.dll` `FUN_1808a1820`):
  `combined = min` of the crossed surfaces' `bulletPenetrationDistanceModifier` (with the
  material special cases), `lost = t²·(1/combined)/24 + (3/pen·1.25)·(1/combined)·3 +
  entry_fraction·damage`, a `< 0.1` distance-modifier gate, 0.16/0.05/1e-5 entry
  fractions. The **per-shape surface** is now read live: each shape's material object at
  `shape+0x48` → its `+0x10` indexes the `SurfacePropertiesList`
  (`CGameSurfaceProperties` 0x20 B: distance modifier `+0x08`, damage modifier `+0x0C`,
  `gamematerial` `+0x14`) — parser `FUN_1809cd960`, resolver `FUN_1809e3560`, manager
  `client+0x254e100`. **Live-verified**: the world's 10149 brushes resolve to 29 distinct
  surfaces (concrete 0.5, metal 0.4, wood 0.9, glass 0.99). The payload reads them
  (`occlusion_query.cpp` `read_shape_surface`); the args are now overrides. Full
  write-up + decompile: `docs/re/penetration.md`. **Remaining tightening:** the
  `surface_properties_list` global is still a literal RVA (ADR-0012 signature pending),
  and a live `subtick-shot` damage reading should confirm the predicted post-wall damage.

## ⏸ Wrap-up (2026-10-18) — the bullet trace reversed; the missing wall was a mesh

## ⏸ Wrap-up (2026-10-18) — the bullet trace reversed; the missing wall was a mesh

**Resume here for the ragebot.** The occlusion "missing world geometry" is fixed.

- **The engine's bullet path is reversed** (`docs/re/bullet-trace.md`, build
  `659940c1`): `FX_FireBullets` = `FUN_180d237d0` → per pellet `FUN_180887d60`
  (filter mask **`0x1c300b`**, filter vtable `client+0x1ad4838`, group `3`) →
  `TraceLineIntervals` (`FUN_180a19e90`) and the penetration loop `FUN_18089ca80`
  (repeated interval traces, ≈90 units total, `0x38`-byte interval records).
- **REA now works on `client.dll`.** Its project cache was corrupt on reopen (the
  known `LocalBufferFile.readHeader` bug) and a cold re-analysis then failed to
  launch from the MCP. Fixed by seeding the cache from the healthy persistent
  `clientoccl` project: a `rea-project`-layout copy with the program duplicated as
  `target-659940c1db36.dll` and a manifest (`scripts/ghidra/DuplicateProgram.java`).
  `rea cache-verify` ok; `tools.rea.*` then returns the full 76,948-procedure
  analysis.
- **The missing wall is a collision MESH.** The worldspawn body's shape histogram is
  `{hull 10143, mesh 6}` (live), and the payload **skipped every mesh** (a 160-vertex
  cap + an O(n⁴) convex hull). Ray-testing the meshes' triangles hit the wall at
  94.8 / 323.2 / 267.6 units on the eye→head rays. **Fixed:** `occlusion_query.cpp`
  reads meshes as a triangle soup (`RnMesh_t` vertices `+0x38`/count `+0x30`,
  triangle indices `+0x50`); `aim::project_mesh_occluder` + `OccluderCell::ray_span`
  (Möller–Trumbore) give the exact depth; the query mask is the engine's `0x1c300b`;
  a mesh broad-phase guard prevents map-wide meshes from fragmenting the chart.
  **Live-validated** (de_dust2): a wall between the eye and an enemy → head damage
  `0` (was full); an open line of sight is unchanged. `meson test` 15/15, payload
  builds, preflight clean.
- **Camera-independent enumeration.** The query box was `eye → camera_forward·1400`,
  so the solid set moved with the camera. It is now the **AABB over the eye and every
  enemy's hitbox geometry** (live pose + all lag records), with **8-unit** padding,
  built from world positions only. Live: `records`/`world_kept` (1909/1887) and the
  cell values are identical across camera yaws.
- **Absolute world yaw/pitch chart.** The chart the hitbox/occluder cells are projected
  into was a gnomonic plane about the **camera forward**; it is now the absolute world
  yaw/pitch map (`aim::Basis::world()`), a function of the world only. Live: the
  projected `occluder_polygons` are byte-identical across camera yaws.
- **The render uses the view angles.** The payload emits `view_yaw`/`view_pitch` and
  clips the chart to a `fov`-wide window around the camera (`aim::clip_to_view`, yaw
  unwrapped about the centre); the Python/JS renderers centre on it with screen-right =
  decreasing yaw. The *data* stays absolute — only the shown window is a view.
- **The render shows only the solids *between* the eye and a target.** `Heatmap` gained
  `used_occluders` (the per-hitbox broad-phase survivors) and
  `walls.occluder_polygons` reports only those. Live: 652 solids projected into the view
  window → **26** reported between (they attenuate the head to 105).
- **Live debug viewer:** `python scripts/heatmap-live.py --fov 90 --png artifacts/live.png`
  → `http://127.0.0.1:8765/`; opened on the game monitor with `schtasks /run /tn CS2LiveView`.

### Next (priority order)
1. **Feed the occluders into the ragebot solve.** `build_input` (`payload/aim/src/reader.cpp`)
   never fills `SolveInput::occluders`, so the rage path still aims at a walled hitbox —
   only the `heatmap` command consumes them. This is the one real functional gap: run the
   same enumeration (`query_world_occluders` → `project_*_occluder`) on the solve's target
   and pass the cells in.
2. **Enumeration efficiency — the AABB/BVH step.** Replace `EntitiesInBox` (a box of whole
   bodies) + walking the world body's ~10k shapes with **`EntitiesAlongLinearSweep`**
   (`0x1809db9a0`, the BVH-accelerated swept query), plus a per-shape AABB reject. The
   render already prunes to "between", but the enumeration itself is still O(world shapes).
3. **Compound (4) shapes.** Still skipped (`docs/re/occlusion.md` "Still to pin" 3) — the
   `RnCompound_t` instance transform is unresolved.
4. **Surface / penetration modifiers.** `OccluderCell::surface_modifier` is hard-coded
   `1.0`; the record's `+0x50` material token type is unresolved.
5. **Live cross-validation against the engine trace.** Fire the engine's own
   `TraceLineIntervals` (mask `0x1c300b`) for a segment and diff the solids — needs the
   `0x38`-byte interval record mapped end-to-end (`docs/re/bullet-trace.md`).
6. **Chart robustness:** a solid straddling the `±π` yaw seam over-covers; the per-solid
   silhouette is a convex hull (tighter silhouette / BVH for meshes).
7. **Signatures (ADR-0012)** for the trace functions and the `CPhysicsBody`/`RnHull_t`/
   `RnMesh_t` struct offsets (still literals).
8. **Commit.** Nothing is committed across the last several sessions.
9. **REA `client.dll` cache:** the seeded cache works, but the underlying
   `feat/ghidra-project-cache` reopen bug remains (a cold re-analysis from the MCP still
   fails to launch).

`NEXT_SESSION_OCCLUSION.md` is now **done** (its goal — make the enumeration match the
engine — is addressed by the mesh fix + camera-independence + between-pruning); treat
this wrap-up as the resume point.

## ⏸ Wrap-up (2026-10-17, second session) — occlusion correctness pass

**Resume here for the ragebot.** Built on the layer-1/layer-2 wrap-up below. This
session audited the implementation against `docs/ragebot.md` / ADR-0022 and fixed
the divergences that were host-testable:

- **Broad-phase bug (correctness).** `build_heatmap` kept a solid only when the ray
  to the hitbox **centre** entered it, so a solid covering only *part* of a
  silhouette was dropped and its covered part kept full damage — contradicting the
  design ("occluders cut `Ω_i` into cells", by overlap then by depth). Now a real
  overlap broad phase (silhouette AABB overlap ∧ `OccluderCell::lower_bound_distance
  < hitbox_distance`), with the exact per-fragment `ray_span` deciding attenuation.
  `lower_bound_distance` is a **conservative** bound (hull plane distances), so
  pruning can never drop a solid that is in front. New `aim-occlusion` regressions.
- **World-cache lifetime.** `g_world_cache` is now keyed on the level's geometry
  identity (body data block + first shape pointer), so a map change that reuses the
  body allocation cannot serve stale brush hulls.
- **Stale diagnostics removed.** `offset_type_hits`, `deep_type_hits` and the wide
  per-record `record_dump` string building (a heap alloc per record **per tick**) are
  gone; the JSON keeps `type_counts`. `scripts/heatmap-view.py` updated.
- **Sphere/capsule/mesh occluders (design §10 item 4).** The payload reads **sphere
  (0)**, **capsule (1)** and **mesh (3)** collision shapes and the aim side projects
  them: spheres/capsules analytically and meshes as the exact convex hull of their
  vertices (`convex_hull_3d`) — all host-tested. The `vphysics2` geometry was RE'd this
  session: sphere centre `+0xB8`/radius `+0xC4`; capsule A `+0xB8`, B `+0xC4`, radius
  `+0xD0`; mesh `RnMesh_t` vertices `+0x38`/count `+0x30` (from the shape ctors,
  vtables' point tests and `RnMeshClone` — `docs/re/vphysics2.md`). **Still skipped:**
  compounds (4), whose `RnCompound_t` child layout is unresolved.
- **Backtracking into the solve (design §8).** `aim::solve_backtrack` scores the live
  pose and every sampled lag record (newest-first, live wins ties) and the rage path
  now aims at the best record instead of only the live pose; `rage_status` reports
  `bt<count>` / `rec<tick>`. Host-tested (`aim-solver`). Feedback: the render thread
  samples each enemy into `RecordStore` (already wired by the heatmap).
- **Deterministic spread / pellet model (plan P3.16).** `payload/aim/spread.hpp`
  replicates the engine's `CUniformRandomStream` + `MD5_PseudoRandom(sequence)` and
  draws the `K` pellet offsets; `DamageField` gained `value_at_pellets`/`argmax_pellets`
  and `solve` handles `AimObjective::pellets`, reachable from the `aim` command
  (`objective: "pellets"` + `seed`/`sequence`/`spread`/`inaccuracy`/`pellets`). New
  host test `aim-spread` (MD5 + RNG pinned to an independent Python model) — **15/15**.
  Not live-validated (the `MD5_PseudoRandom` input construction and the weapon
  magnitudes need a live cross-check). A `set_seed` sign bug (Source stores `-seed`)
  was caught by the new test.
- **Occlusion RVAs are now signatures (ADR-0012).** `physics_query_interface`
  (`0x2227058`), `physics_query_filter` (`0x2227070`) and `entities_in_box`
  (`0x9dbd20`) are curated signatures in `external/dumper/src/analysis.rs` — anchors
  derived **statically** from the build-659940c1 `client.dll` and uniqueness-verified
  with a PE/capstone script. They flow through `calibration/offsets.json` →
  `payload/sdk/offsets.hpp`, and `occlusion_query.cpp` reads them via
  `sdk_client_offset(name, literal)` (literal fallback), so a game update no longer
  needs a source edit. The dumper test fixture was extended (`pattern.rs`); `cargo
  test` 32/32.
- **Live session 2 (2026-10-18): the engine's own trace reversed; player boxes fixed.**
  Reversed `CGamePhysicsQueryInterface::TraceShape` (`FUN_180a1a000`): the engine
  traces, sorts the layers, keeps worldspawn `0x8000`, and calls the filter's
  `ShouldHitEntity` (vtable `+8`) for the rest. **The static default filter we passed
  is `mov al,1; ret` (`client+0x22d900`) — it accepts every entity**, so the players'
  collision bodies (and the local player's own) were enumerated as occluders. Fixed by
  dropping the players payload-side (`resolve_entity`, mirroring `FilterOverlapResults`)
  — verified live: standing in front of an enemy, `records=4 → excluded=4,
  bodies=0`. A payload-built filter is unsafe (the engine reads more of the filter
  vtable than slot `+8`; a minimal one returns nothing and crashes). **Remaining (the
  missing world geometry):** we enumerate with `EntitiesInBox` (box overlap of *bodies*)
  and walk shapes, whereas the engine **traces** — so the Tier-1
  `TraceShapeWithSortedResults` path is the faithful fix. Occluder polygons are also
  now clipped to the chart (near geometry projected far off-canvas). Also this session:
  the enumeration box was widened to a **view volume** (eye → camera-forward·1400 ±96,
  Tier 2, no traces) and the caps raised (`k_max_occluders`/`k_max_records` = 8192) —
  `world_box_kept` jumped 512 → ~4000. **Still open:** standing with a wall between,
  the enemy head cells stay at full damage and the render shows scattered brush
  fragments (not a coherent wall/floor); verified *not* empty planes
  (`hulls_no_planes=0`). The next session makes our enumeration **exactly** the engine's
  trace — see **`NEXT_SESSION_OCCLUSION.md`**.
- **Live session (2026-10-18).** Launched the game via `CS2Launch` + injected
  (session 1) and ran a bot-match probe with `heatmap(walls=True, walls_call=True)`.
  The text says: `ok=true records=7 bodies=2 hulls=2 occluders=2 type_counts=[0,0,2,…]`.
  **`record+0x50` is a pointer, not a float** (`material_token 0x26c45d77d00`,
  `material_float` garbage) — the material object read live shows surface-like fields
  (`+0x28=0.8`, `+0x2C=0.25`, `+0x30=900`, `+0x34=0.1`) but the penetration field is
  unidentified and the record is **per hit**, so per-brush penetration stays unwired.
  The `compound` probe was zero (no type-4 shape in the box). Found + fixed a real
  scenario bug: `scenarios/heatmap.py` never passed `walls_call`, so it never actually
  ran the engine query. Use `walls_call=walls` (docs/re/occlusion.md §"Still to pin").
- **client.dll pass (2026-10-17).** Re-decompiled the overlap-record build
  (`ConvertOverlapMultiple` `FUN_1809d2dc0`) statically (PE + capstone — REA's Ghidra
  analysis fails on the 39 MB target). The record's `+0x50` material token is
  `*(hit->vtbl[0x30]() + 0x28)` — slot `0x30` is on the **client-side hit object**
  (its `[0x10]` yields the body), correcting the earlier "shape vtbl[0x30]" note; the
  `+0x28` type is still unresolved (an 8-byte load), so a one-record probe
  (`walls.material_token`/`material_float`) is emitted live to settle it. The record
  fields are now pinned to their producers (`+0x18`←`body->[0x478]()`, `+0x58`←
  `hit->[0x10]()`, `+0x00..0x20`←`hit->[0x50]()`).
- **Per-tick enumeration: deliberately NOT cached.** `cache_occlusion_targets`
  re-runs the physics query every frame — the eye→target segment is the exact query
  box, so caching it would make the solid set stale. Only the *parse* of the static
  world body is cached; the per-query AABB filter and the chart projection always run
  fresh. (Correctness over cost.)
- **Surface modifiers — corrected finding.** The docs attributed the material lookup
  to `shape->vtbl[0x30]()`; a check on `vphysics2.dll 1d768045…` shows `vtbl[0x30]` is
  a **cast/intersect** routine on hull/sphere/capsule (`FUN_180254870`/`FUN_18024cac0`/
  `FUN_18024fd30`), so `record+0x50` cannot supply per-brush materials. Leave
  `surface_modifier` at 1.0 until the real per-shape field is resolved
  (`docs/re/occlusion.md` "Still to pin" 5) — do not guess it.

Verified: `meson test -C build` **14/14**, `meson compile -C build-payload` clean.
Nothing is committed (still uncommitted from both sessions).

### Remaining (priority order from `.opencode/plan/occlusion-handoff.md`)
1. `vphysics2` **compound (4)** — geometry pool mapped, **instance transform not**.
   `RnCompound_t` stores typed geometry by value (`RnHull_t[]` stride `0xF8`,
   `RnMesh_t[]` stride `0xC0`, spheres `0x10`, capsules `0x1C`), and the compound shape
   has a 0x30-byte instance-record vector at `shape+0xD0`/`+0xD8`. Return the instance
   record's transform (its first 6 floats) before enumerating the pool — otherwise the
   sub-shapes might be instanced, not shape-local. A live probe (`walls.compound`)
   reports the group counts (`docs/re/vphysics2.md`).
2. Surface/penetration modifiers — **the recorded recipe was wrong**: `vtbl[0x30]`
   is a cast routine, not the material accessor (see the wrap-up above). The material is
   **game-side** (no surface accessor in vphysics2), so resolving the real per-shape
   surface-properties field needs a **client.dll** pass on `ConvertOverlapMultiple`.
3. Signatures (ADR-0012): the query interface/filter + `EntitiesInBox` are **done**
   (`physics_query_interface`/`physics_query_filter`/`entities_in_box` in
   `external/dumper/src/analysis.rs`, read from `cs2/sdk/offsets.hpp`). The
   `CPhysicsBody`/`RnHull_t`/`RnMesh_t` *struct offsets* remain literals (struct
   offsets don't move like RVAs).
4. `EntitiesAlongLinearSweep` for the enumeration (perf). **Do not cache the
   eye→target enumeration** — accuracy over cost.
5. Accuracy: the record window now feeds `solve`; the **pellet model (K>1) + the
   deterministic RNG/seed are implemented and host-tested** — remaining is the live
   cross-check of `MD5_PseudoRandom` / the weapon spread/inaccuracy magnitudes, the
   shotgun/negev pattern table, and pose blur.
6. Cross-validate the attenuation and the backtrack aim against the engine trace
   (`subtick-shot`) — everything here is host-verified only, not live-validated.

---

## ⏸ Wrap-up (2026-10-17) — heatmap layer 1 (shipped) + occlusion layer 2 (**next session starts here**)

**Resume here for the ragebot heatmap / occlusion.**

### Layer 1 — the multi-target damage heatmap (shipped, live-validated)
- `heatmap` control command (`docs/control-harness.md`): projects **every** enemy's
  real studio hitbox capsules *and* their sampled **lag records** into one shared
  angular chart, valued by damage, returning tagged cells. Args: `target`, `fov`,
  `history`, `sample`, `clear`, `all`, `walls`/`walls_call`, weapon overrides.
- **Lag records are real**: the render thread samples each enemy every frame into
  `aim::RecordStore`, keyed by the game tick (`m_nTickBase`) with
  `m_flSimulationTime`, and trimmed to the lag-comp window via `aim::rewind_tick`
  (`docs/re/lagcomp.md`). `PlayerSnapshot` gained `simulation_time`/`tick_base`.
- Python: `scripts/scenario/heatmap.py` (pure-stdlib rasteriser, modes `damage` /
  `record`, `fit`/`focus`), `scenarios/heatmap.py` (the `heatmap` scenario),
  `scripts/heatmap-view.py`. New host tests `aim-heatmap` (+ `aim-occlusion`);
  **14/14 pass**; `payload.dll` builds.
- Session images (4 enemies): damage <https://files.catbox.moe/3tfzfl.png>,
  records <https://files.catbox.moe/oimap8.png>.

### Layer 2 — occlusion/penetration (**record → shape chain resolved live**)
The design (ADR-0022 Tier 2) is **project the world solids into the chart**, never
trace rays.
- **Working:** `payload/aim/occlusion.hpp` (`OccluderCell`, `project_occluder`,
  `thickness_along`, `clip_convex`/`subtract_convex`) + `build_heatmap` splits each
  hitbox silhouette by the projected solids and scales each fragment by
  `Π(1 − t(u)·mod/m_flPenetration)`. Host test `aim-occlusion`.
- **Working live:** `payload/control/occlusion_query.cpp` calls
  `CGamePhysicsQueryInterface::EntitiesInBox`
  (`*(client_base + 0x2227058)`, RVA `0x9dbd20`, static filter
  `client_base + 0x2227070`), then walks each record's **`CPhysicsBody`** at `+0x58`
  → `body+8` data → shape array at `data+0x68` (count `data+0x60`) → the `IPhysicsShape`
  hulls (`+0x18` type 2, `+0xC0` `RnHull_t*`). Entity bodies get their world
  transform from `body->vtbl[0x1b0]`; the **world body** (handle `0x8000`, **10149
  brush shapes**) is parsed once into a cache and filtered by the query AABB.
  Live: `walls: ok=True faulted=False records=8 hulls=4 occluders=3` (real hulls in
  the chart). Full detail: `docs/re/occlusion.md` §"the record → shape chain".
- **Superseded:** the old blocker ("the record carries entities, not shapes") — the
  `+0x58` pointer is a `CPhysicsBody`, **not** an entity (RTTI-confirmed).

### Occlusion corrected + orientation fixed (this session)
- **The occlusion is now the intended design** (`docs/ragebot.md` §3, ADR-0022
  Tier 2): the solids between the eye and a hitbox are decided **per cell by depth**,
  using the hull's own planes — `OccluderCell::ray_span(eye, dir)` clips the ray
  against the half-spaces (planar arithmetic, no ray casts). `build_heatmap` keeps
  only solids with `entry < hitbox_distance` and applies the in-front thickness
  `min(exit, distance) − entry`. The floor under a target and walls behind it no
  longer attenuate (new host test; **14/14 pass**).
- **Deleted the band-aids**: `walls_radius` (payload arg + member + Python + viewer
  control), the `separates` eye→target AABB test, the 24-unit minimum distance, and
  the `reach > 8·extent` projection drop. The enumeration is now a plain box around
  eye→(farthest target) with an 8-unit margin — completeness only.
- **Chart was mirrored**: `basis_from_angles` had `right = (-sin yaw, cos yaw, 0)`,
  the negation of Source's `AngleVectors` `(sin yaw, -cos yaw, 0)`. Fixed; verified
  `dot(right, forward×up) = 1.000` and the payload's cell `u` now matches an
  independently computed projection for 3/3 targets. This mirrored the whole chart
  (hitboxes *and* occluders), so any older render is mirrored.
- Plane winding is **not assumed**: the planes are oriented inward from the hull's
  vertex centroid, so an inward- or outward-stored `RnHull_t` both work.
- Live numbers: `base 21818 → walls 10909` (×0.50) with **190 cells surviving**
  (before, the floor blanked everything). Renders: `artifacts/walls_level_{base,
  cells,walls,occluders}.png`.

### Live debug viewer (shipped this session)
`scripts/heatmap-live.py` — a **local HTTP server the agent runs** (session 0) that
polls the payload's `heatmap` command (with the world solids) and serves a canvas page
the user opens in **session 1** (`localhost` and the named pipe both cross sessions for
the same user):

```powershell
python scripts/heatmap-live.py --png artifacts/live.png      # http://127.0.0.1:8765
```

Live controls: `mode` (cells / cells+occluders / occluders), `fov`, `history`,
`target`, poll `rate`, walls on/off, include-teammates, pause, sample/clear.
`--png` also writes each frame for headless inspection. Endpoints: `/` (page),
`/_data` (latest payload + status), `/_set?k=v`, `/_frame.png`, and — because the
payload's pipe is single-client — the passthrough `/_look?yaw=&pitch=` and
`/_exec?line=` for driving the game while the viewer runs.

Verified live: ~10 Hz (14 ms/poll with the warm world cache), 200 cells, 115 occluder
polygons, the frame renders the map interior + enemy silhouettes (`artifacts/live.png`).
Second new script: `scripts/heatmap-walls.py` (offline base/cells/walls/occluders
comparison renders for verification).

**The full prioritised plan for the occlusion layer (viewer first, then perf, then
correctness) is at `C:\Users\gpmpo\.opencode\plan\occlusion-handoff.md`** — it lists the
perf work (batch reads, per-hull AABB reject, `EntitiesAlongLinearSweep`, stop the
per-tick recompute), the correctness work (hull-accurate separation, all shape types,
surface modifiers), the RE hardening (signatures, slot policy, POD-only globals) and
the accuracy work (spread/records, trace cross-validation).

### Exactly what the next session should do
1. **Verify the world-brush hulls are world-space** (the entity/prop path applies
   `body->[0x1b0]`; the world body currently assumes identity) and confirm the
   occluded enemies look right in the rendered image.
2. **Per-shape transform / compound recursion** if the world geo needs it (do not
   call `shape->[0x50]` — it is a collision routine; find the real accessor or use
   the record's own `+0x00..+0x28` transform).
3. **Signatures (ADR-0012)** for `EntitiesInBox`, the `CPhysicsBody` shape-count/
   shape-array offsets and the `RnHull_t` offsets; the payload still hard-codes RVAs.
4. **Cost**: the world cache is per-level; re-validate that the per-frame query
   stays cheap (cap 512 occluders, AABB filter) and that the cache invalidates on a
   level change.
5. Then the per-direction thickness refinement (`docs/ragebot.md`).

### Gotchas (learned this session)
- **A payload global with a non-trivial constructor, or a function-local `static`,
  adds a CRT import the manual-map injector cannot resolve.** A namespace-scope
  `std::vector` (needs dynamic init) and a function-local `static std::vector`
  (magic-static guard) both pulled in `KERNEL32.dll!SleepConditionVariableSRW`, and
  the injector failed with `unresolved import`. Keep payload globals **POD**
  (pointers/ints) and allocate heap arrays explicitly.
- **Do not call an unvalidated per-shape vtable slot.** Calling `shape->[0x50]` on
  every world brush aborted the process (it is a collision routine for hulls, not a
  transform getter). Validate a slot on one entity-shaped body before looping it
  over the world's ~10k shapes.
- **Walking the world's ~10k brush shapes every tick stalls the game** (CPU pinned,
  control pipe unresponsive). Parse once into a cache; filter per query.
- **REA's Ghidra cache is corrupt on reopen** (`LocalBufferFile.readHeader:
  Unrecognized file format`); `rea cache-verify` still says ok. **Use the persistent
  project** instead:
  `analyzeHeadless C:\Users\gpmpo\ghidra-analysis clientoccl -process client.dll
  -noanalysis -scriptPath scripts/ghidra -postScript DumpTargets.java` (warm once
  with `-import`; `-process -noanalysis` is fast). `scripts/ghidra/DumpOne.java`
  takes `outPath addr…` args.
- **Engine calls that allocate must run on a game thread.** With
  `CS2_PAYLOAD_PUB_THREAD=1` (set in `scripts/launch-cs2.cmd`) the overlay refresh —
  and any query in `on_snapshot` — runs on the payload's *own* thread, which has **no
  tier0 TLS**: the allocator (`FUN_1801ffe10` = `*(*(GS:[0x58]) + _tls_index*8) + …`)
  faults with `read addr=0x40`. Fix (done): a `GameThreadSink` on `Input`; the
  CreateMove hook (`Input::apply`) runs the query. `diag::call_guarded`
  (VEH + `setjmp/longjmp`, no unwind tables needed) is the safety net and is proven.
- **The game relaunch is flaky** — the map load sometimes crashes on startup; just
  run `scripts/run-scenario.py` again.
- The game auto-updated mid-session: `client.dll` sha `659940c1…` (was `44af2aa0…`);
  the RVAs in `docs/re/occlusion.md` are for the new build.
- Showing images on the game's monitor (session 0 can't): `scripts/photo-view.ps1`
  (Photos) / `scripts/open-on-game.ps1` (Paint) via a scheduled task with an
  Interactive principal; `scripts/capture-screen.ps1` to verify.

### Repro
```powershell
meson compile -C build-payload
python scripts/run-scenario.py heatmap --keep-running           # layer-1 images
python scripts/heatmap-view.py --walls --walls-call --json artifacts/walls.json
curl.exe -s -F reqtype=fileupload -F fileToUpload=@artifacts/heatmap.png https://catbox.moe/user/api.php
```

---

## ⏸ Wrap-up (2026-10-16, session 2) — weapon model, auto-fire fix, hitbox renderer

**State:** `payload.dll`, `cs2_overlay_layer.dll`, `injector.exe` all build; the 12
host tests pass. The ragebot is **working** (real studio hitboxes + keybind) and
the **auto-fire timing bug is fixed**. Two things landed on top:

- **Weapon model from the game.** The solve now reads the shooter's real
  `CCSWeaponBaseVData` (`weapon + 0x388`, `m_nDamage`/`m_flHeadshotMultiplier`/
  `m_flPenetration`/`m_flRangeModifier`); explicit `aim`/`rage` args still win.
  Live: `aim-probe` reports a Glock `weapon_damage 50` (was the hard-coded 100).
- **Auto-fire fix.** The attack is asserted **persistently** in `Input::apply()`
  step 1 (backend-independent) while a solve is active, so every simulated command
  carries the attack with the silent angle — the manual-fire path worked for the
  same reason. (Previously the press was only on the command `rage_tick` saw.)

**Hitbox renderer — three modes, menu-selectable.** `EspConfig::hitboxes` draws each
live enemy's real studio hitbox capsules. The mode and colour are menu-owned (ESP tab):
**Outline** (the true silhouette as a closed polyline — a capsule is the convex hull of
its two end spheres, so the tangent circle of each sphere from the eye is built in 3D
and the two are hulled in screen space), **Solid** (that silhouette filled via the new
`wire::DrawKind::filled_poly` → ImGui's `AddConvexPolyFilled`), and **Wireframe** (the
capsule mesh cage: rim/cap rings plus longitudinal lines, back-facing edges dimmed).
Colours are the **automatic per-hitgroup** scheme (rage target highlighted) or a
**custom** RGBA from the menu's colour picker. Two degenerate cases were fixed in the
read path as well (a box whose bone cannot be resolved is skipped instead of leaving its
endpoints at the map origin; a mesh with no hitbox-set table is skipped instead of being
read as an inline array). `scripts/hitbox-view.py` renders the same capsules offline as
a reference. Compare against the engine's `sv_showplayerhitboxes`.
`docs/ragebot.md` §10 item 1.

**Occlusion (item 3) — RE pinned, not implemented.** The trace call convention is
resolved from a live call site: `TraceShapeWithSortedResults` =
`(iface, <rdx descriptor>, start, end, filter, &results)` on
`*client_base + 0x221f058`, filter vtable `+0x1ace848`, mask `0x1c1003`, static
default filter at `+0x221f070` (`docs/re/aim.md` §"The call convention, pinned from
a live site"). It was **not** wired up: it needs a dumper signature for the
singleton + the function (ADR-0012) and live validation, and a wrong call aborts
the process (the payload has no exception tables), so it deserves its own pass.

**Tooling added:** `scripts/capture-screen.ps1` and `scripts/capture-game.ps1`
capture the interactive session (run them via a scheduled task —
`schtasks /create … /it`, like `CS2Launch`; session 0 has no visible desktop).
`capture-game.ps1` restores + foregrounds the game window and crops to its client
area.

---

## ⏸ Wrap-up (2026-10-16) — ragebot: real studio hitboxes + hold-key ragebot

**Resume here for the ragebot.** Design + direction: **ADR-0022** and
[`docs/ragebot.md`](ragebot.md) (which carries an implementation-status §0 and
the next actions). RE: [`docs/re/aim.md`](re/aim.md) (the shot model + the trace
recipe + **the runtime studio hitbox set**), [`docs/re/lagcomp.md`](re/lagcomp.md)
(backtracking), [`docs/re/vphysics2.md`](re/vphysics2.md) (collision geometry).

**Implemented + host-tested** (`payload/aim/`, 4 tests: `aim-geometry`,
`aim-field`, `aim-solver`, `aim-records`):

- `geometry` — the angular chart (gnomonic projection), OBB/sphere projection,
  convex hulls, half-plane clipping, and the two optima (nearest point,
  Chebyshev centre).
- `field` — the damage field (cells, the weapon value model, the pose-blurred
  maximum), `solver` — build the field and pick the aim.
- `hitbox` — bone/AABB → oriented box; `reader` — the snapshot → solver bridge;
  `records` — the backtrack ring + the rewind-tick formula.

**Live-validated** (`payload.dll` builds; `aim` + `rage` wired into the control
service; scenarios `aim-probe`, `aim-shot`, `rage`):

- `aim-probe` — the solve points at a bot's head within **0.02°**, using the real
  studio hitbox set (`hitcount` 19).
- `rage` — **the hold-key ragebot works**: with the feature enabled (the overlay
  menu's `Ragebot` tab, or the `rage` command) the payload solves each frame and
  the per-tick `Input::apply()` writes the silent sub-tick aim + `IN_ATTACK`;
  solves/writes/shots climb and the bot's health drops. **Firing manually while
  the ragebot holds the aim always hits.**

### The real studio hitboxes (done 2026-10-16)

`aim::read_model_hitboxes` now walks pawn → scene node → `CModelState` → (double
deref) `CModel` → render mesh → hitbox set → **19 capsules** (bone-local endpoints
`m_vMinBounds`/`m_vMaxBounds` + `m_flShapeRadius`), resolves each box's bone
through `CModel::GetBoneIndexForHitboxForMesh`, transforms the endpoints by the
bone (`{pos, quat}`, stride `0x20`) and reads the hitgroup from `m_nGroupId`
(head ⇒ `m_flHeadshotMultiplier`). Full layout + the live verification:
**`docs/re/aim.md` §"The runtime studio hitbox set"**. The collision-AABB head/body
proxy is now only a fallback. New geometry: `project_capsule` (a capsule is the
convex hull of two spheres, so its silhouette is the hull of the two circle
projections); `aim-solver` grew a capsule case.

**What is NOT done (the resume points, in priority order):**

1. **Ragebot auto-fire timing (minor).** With `fire` on, a shot is *sometimes*
   released on a command whose history the silent write did not reach that tick,
   so it follows the camera. Cause: `apply()` runs for both the slot-25
   `create_move` **copy** and the slot-5 simulated command, and the attack press is
   asserted per-`apply()`; manual fire works because the press is **persistent**
   across every command. Fix: assert the attack into `set_mask_` while the rage is
   active (or gate the press to the slot-5 command).
2. **Occlusion / wallbang** — the solver consumes occluders
   (`SolveInput::occluders` → `DamageField::clip`) but nothing produces them. The
   engine trace recipe (interface singleton `client_base+0x221f058`, the direct
   `FUN_180a1a0a0` call, the static filter at `+0x221f070`) is in
   `docs/re/aim.md` §"Calling the trace from the payload"; the struct layouts /
   arg order still need pinning from **assembly** (a wrong call faults the game).
3. **Backtracking** — `records` is host-tested but not sampled per frame.
4. **Pose blur** wiring; pellet-pattern registration (`K>1`); pulling the weapon's
   real damage/headshot/range from `CCSWeaponBaseVData` (the `aim`/`rage` commands
   still take a fixed profile).

## ⏸ Wrap-up (2026-10-15/16) — sub-tick bhop implemented from the RE

The menu-enabled movement features **engage** (`Input::apply()` runs them
regardless of the control backend and gates only the explicit `press`/`move`/
`look` commands).

- **Bunnyhop — re-implemented correctly (read-only, no engine calls).** The
  authoritative design is UC 771389 / 774033 (InkCrow), researched in
  [`docs/re/research-bhop.md`](re/research-bhop.md) and re-derived on
  `client.dll` 25815307 in [`docs/re/prediction.md`](re/prediction.md) §"RE
  findings". `Input::bhop_tick` is a `CreateMove` **post** hook that reads
  `m_ModernJump`'s landing record and the command's build tick, then appends a
  **release+press pair sharing one `when`** to `base->subtick_moves`:
  - `build = m_nTickBase + (fresh ? 1 : 0)`; the record is in `{build-1, f}`, so
    the pair goes into `build == landed_tick + 1` (placement A), with a B
    catch-up window for anything one to five ticks late;
  - `when = f == 0 ? 3/64 : clamp(q(f) + 3/64, q(f) + 1/64, 62/64)` — past the
    recorded contact and inside `sv_bhop_time_window * 0.5 = 16/64`;
  - a lone press is **not** a press edge while the key is held (the edge machine
    is `C || (A && B)`), so the pair is required;
  - the pair is re-derived and re-appended **every frame** (the command's table
    is rebuilt each frame), idempotent by *array content*, budget-checked (`32`)
    and insertion-sorted so the admission gate cannot discard the array;
  - the whole-tick `IN_JUMP` bit is **not** cleared — it is the held-key read;
  - the assist still only acts while the player actually holds jump.
  The `RunCommand` simulation the earlier session tried **crashes** without the
  full `CPrediction` envelope (`client+0xa51526 read addr=0x0`) and the
  authoritative design does not use it; it is kept behind `predict(on)` purely as
  a reference path.
  **Validated live (2026-10-15, `bhop` scenario, de_dust2 bot match, operator
  confirmed):** bhop enabled + jump held → 93 pair writes / 5+ hop cycles in 5 s,
  and placement **A fired** (`bhop_a` > 0), which is the phase proof — the pair
  lands in `build == landed_tick + 1`. `bhop_age` spans the whole airtime between
  landings (the record only moves on a new contact), so the phase is read from the
  `bhop_a`/`bhop_b` counters, not from sampling `bhop_age`.
- **Auto-strafe — re-implemented as a normal, silent air-strafe (2026-10-15).** The
  payload now computes the `AirAccelerate` optimum `theta*` every airborne tick and
  pushes the wish perpendicular to the *current* velocity on the side of the goal
  taken from the held keys in the view frame (`W`/nothing forward, `S` behind; a
  held `A`/`D` stands it down), so nothing writes a view angle and the camera/eye
  angles stay put. `Input::strafe_tick` / `write_strafe_wish`; wired into the menu
  (`wire::Settings.auto_strafe`), the `strafe` control command and the `autostrafe`
  scenario. Two encodings: `subtick=0` (default) camera-frame analog; `subtick=N>=1`
  keeps `(1, 0)` and rotates the movement basis with `N` `yaw_delta` steps. RE and
  concise re-derivation: [`docs/re/strafing.md`](re/strafing.md); community sources:
  [`docs/re/research-strafe.md`](re/research-strafe.md).
  - Decisive re-derivation this session: `m_flCmdForwardMove` is
    `CBaseUserCmdPB::forwardmove` **unscaled** (`UpdateInputState`, `FUN_180a764d0`),
    and the engine's own unit analog (`1.0`) reaches `AirMove` as the full-speed
    request (`move(forward=1.0)` = 204 u/s, 1.01x the console `+forward`), so a unit
    analog still gets the full `A` budget and `theta* = 90°`. The silent-yaw gate is
    the convar `sv_subtick_movement_view_angles`, read by `DoMovement::Subtick`'s
    `(*vtable+0x150)()` predicate.

**Resume here, in this order:**

0. **⚠️ Footgun fixed 2026-10-15:** a `move()` request with all-zero axes used to
   strip the movement bits out of every command (silently removing *all* movement —
   a scenario that ended with `move(forward=0, side=0)` left the operator unable to
   move). It now only overrides the bits when the wish is non-zero; `move(clear=True)`
   turns the override off. Also note the **overlay menu owns the settings block** (the
   layer re-writes it every frame), so a Python `settings.write()` can be clobbered —
   toggle the features in the menu when a write "does not stick".
1. **Bhop — validated live** (2026-10-15: `python scripts/run-scenario.py bhop`,
   de_dust2 bot match, operator-confirmed): hops, `bhop_writes`/`bhop_presses`
   climb, and placement **A fires** (`bhop_a`), i.e. the phase
   (`build == landed_tick + 1`) is right. Remaining polish: watch that `bhop_a`
   dominates `bhop_b` (B is the beta catch-up — a hop, but the ground-slice
   clamp keeps the speed loss), and consider the optional fresh-frame
   hull-trace guess.
2. **Strafer — validated live (2026-10-15); the silent sub-tick yaw rotation is the
   working recipe.** `Input::strafe_tick` + `write_strafe_wish`
   (`payload/control/src/input.cpp`) compute the `AirAccelerate` optimum every
   airborne tick and push the wish perpendicular to the *current* velocity on the
   side of the goal taken from the held keys in the view frame (`W`/nothing forward,
   `S` behind; a held `A`/`D` stands it down). The sub-tick rotation is **simulated
   per slice** (the community's `ref_air_accel_sim`): an *even* split ramps the basis
   (the engine accumulates the deltas) and only the last slice lands on `theta*`;
   setting each step's `yaw_delta` from the simulated velocity makes every slice gain.
   Measured on a de_dust2 bot match (listen server, 250 u/s ground speed,
   `sv_quantize_movement_input 1`, enabled purely through the menu's settings block):
   - `strafe(subtick=8)` (**default**): **+85.1 median / +104.9 max u/s per hop**, and
     the gain scales with the slice count — `subtick=1` +15.4, `4` +41.9, `8` +85.1
     (all vs an ≈ 0 control). The even split (an earlier revision) gained +17.4.
   - `strafe(subtick=0)` (camera-frame analog): nearly flat — the `{-1,0,1}` snap
     loses the exact perpendicular angle, so the rotation channel is the one to use.
   The `strafe` control command, the `wire::Settings.auto_strafe` byte and the menu
   toggle are wired back in; the `autostrafe` scenario observes it. Drive the settings
   block (`scripts/scenario/settings.py`) for a test — the menu owns the feature and
   reverts a control-side enable on the next published frame. RE:
   `docs/re/strafing.md`.
3. Re-check the bhop's `bhop_b` share (B is the beta catch-up; a high share means
   the phase slips) and consider the optional fresh-frame hull-trace guess.

Diagnostics on `input()`: the `bhop_*` block above plus `bhop_holding_ticks`,
`bhop_air_ticks`, `input_local_pawn`; `predict_ready`/`predict_runs`/
`predicted_landing_*` cover the retired simulation path, and a cyan **`PRED`** box
in the overlay draws its predicted origin. The local pawn is forwarded every frame
from the overlay refresh (`Overlay::set_local_pawn_sink`), so the features see it
fresh during menu-only play.

**Enabled-by-menu only** (unchanged): the payload re-applies `wire::Settings` every
published frame, so the menu owns the features; settings reset to defaults on
launch.

<details><summary>Earlier wrap-up (superseded)</summary>

### ⚠️ Wrap-up (2026-10-15, earlier session) — movement features were NOT working

**Operator-confirmed:** with the overlay menu's `Movement` toggles enabled (auto-strafe
and bunnyhop), **neither had an in-game effect**. That is now explained: `apply()`
early-returned on the default `console` backend, so the flags were never applied.

The original resume list (kept for context): (1) confirm the enable path live,
(2) check whether the held jump is visible in the command mask, (3) have the
strafe also write the raw axes, (4) re-verify with `scripts/scenario/autostrafe.py`.
Items 1 and 3 are done (see above); item 2 was answered by the diagnostics
(`bhop_holding_ticks` shows the mask *does* carry the held jump).

</details>

## One-line summary

The portable Vulkan present layer (`overlay/`) renders the overlay from shared
memory and is proven in isolation. On native Windows (D3D11/DXGI, no DXVK) the
full stack now works: the injected payload reads game state and publishes a
`wire::Model`, and the Vulkan layer renders it — **no freeze on the display-mode
change that wedged the game under Wine/Proton (ADR-0017)**.

## What is proven

- **`overlay/` (Vulkan present layer)** — intercepts `vkCreateSwapchainKHR` /
  `vkQueuePresentKHR`, renders the `wire::Model` (HUD + `DrawList`) with
  ImGui+Vulkan, and draws a real HUD + ESP in CS2. Validated on native Windows
  with the payload (`-vulkan`, implicit layer registered under
  `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers`, `CS2_OVERLAY=1`) and, earlier,
  without a payload under Proton.
- **State → shm model** — `ModelPublisher` serializes `render::OverlayModel`
  into the shared segment (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm` on
  native Windows; under Proton the prefix's `drive_c`); the layer writes the
  presented extent back so the payload can project world coordinates.
- **State introspection** — local player + every player (health, armor, team,
  weapon, ammo, origin/eye/velocity/angles), live-verified on the updated build.
- **Payload plumbing** — injector `.pdata` registration (`RtlAddFunctionTable`,
  ~2043 entries), per-thread TLS bootstrap for the manually-mapped image
  (`payload/tls`), `NtCreateThreadEx(SKIP_THREAD_ATTACH)` thread helper, crash
  handler + freeze watchdog (`payload/diag`).
- **RE** — the game's present is `CRenderContextBase::Present`
  (`rendersystemdx11.dll + 0x16dd0`, build 25815307), dispatching to
  `CRenderContextDx11::Present` at `*(this)+0x510`; the swapchain wrapper is
  `CSwapChainDx11`. **Do not inline-hook it** — its prologue has RSP-relative
  home stores the epilogue reads back; use a vtable-slot hook or a
  stack-correct hook (ADR-0017).

## Windows notes (learned 2026-10)

- **Session 0 has no GPU.** The agent/SSH shell cannot start the game; use the
  `CS2Launch` scheduled task (`scripts/launch-cs2.cmd`), which runs in the
  interactive session. See `AGENTS.md`.
- **Medium integrity.** The game runs as the standard user; it cannot create
  files in `C:\` root. All payload/overlay output paths must be under
  `%LOCALAPPDATA%` (or another user-writable dir).
- **API-set imports.** The injector maps `api-ms-win-crt-*` → `ucrtbase.dll`
  (and `api-ms-win-core-*` → `kernelbase.dll`) when resolving imports.

## Running the scenario harness — operational hand-off (2026-10-15)

The harness drives the game *programmatically* over a named pipe and **cannot see
the screen**. Every claim it makes is therefore a *state* claim; anything that is
really about the picture (did the camera turn? did the bullet hit? did the player
walk?) must be confirmed by the operator watching the game. Treat them as
complementary: the runner asserts numbers, the operator confirms the image.

### 1. Prerequisites

- Steam running in the interactive session (session 1); CS2 installed.
- The `CS2Launch` scheduled task exists (`schtasks /query /tn CS2Launch`).
- `build-injector/injector.exe` and `build-payload/payload.dll` are built.
- **The payload's SDK matches the *running* game build** (see §2).
- The runner may be in session 0 (no GPU) — fine: the named pipe is same-user and
  crosses sessions. The agent **cannot** launch `cs2.exe` directly (session 0 has
  no GPU); it goes through the scheduled task.

### 2. The game build is not pinned — re-calibrate after every update

CS2 auto-updates. On 2026-10-15 it went from **25815307 (1.41.9.0)** to
**25947352 (1.41.9.1)** mid-session and every client.dll feature silently broke
(`exec` faulted in the engine; `state`/`events`/`input` all failed) because the
RVA offsets were stale.

**Symptoms of a stale SDK:** `ping` shows `events_ready=false`,
`input_ready=false`, `state.valid=false`, and an `exec` **crashes the game**
(`[crash] … at engine+0x1c4b15 read addr=0x0`).

Fix — all curated signatures still match across patches, only the RVAs move:

```sh
schtasks /run /tn CS2Launch            # game running (payload not needed)
python scripts/calibrate-live.py       # rewrites calibration/*.json (needs a live process)
python scripts/gen-sdk.py calibration payload/sdk
meson compile -C build-payload         # rebuild against the fresh SDK
```

Then re-inject once and confirm with `ping` (`events_ready` / `input_hooked` /
`state_ready` true) and the `status` scenario.

### 3. Launch + inject — two modes

**Self-contained (launches the game):**

```sh
python scripts/run-scenario.py <scenario>
```

It kills a stale `cs2.exe`, runs the `CS2Launch` task (session 1), waits for
`client.dll`, injects `payload.dll`, connects, runs, and tears down (kills the
game) unless `--keep-running` is given.

**Attach to a session you are already in (do NOT relaunch or re-inject):**

```sh
python scripts/run-scenario.py <scenario> --no-launch --no-inject --keep-running
```

Use this once the game is up and injected. **Never inject twice into the same
process**: two payload instances install two pipe servers on the same name and
clients then reach a random one — this caused a long, confusing failure. To load a
new payload, kill + relaunch the game, then inject **once**.

Manual equivalent: `schtasks /run /tn CS2Launch` → wait for `cs2.exe` +
`client.dll` → `build-injector\injector.exe <pid> build-payload\payload.dll
--load-missing`. Logs: `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`,
`cs2-overlay-layer.log`.

### 4. The screens and states — input is ignored in most of them

The **screen state machine** (`scripts/scenario/screens.py`, documented in
`docs/control-harness.md` §"The screen state machine") is the authoritative map of
this. It reduces the payload state to one screen, says which operations that
screen allows, and drives the transitions — so scenarios no longer re-derive
"is it safe to send input?" from raw fields:

```python
awaiting.ensure(game, Screen.LIVE)   # drive: map -> jointeam -> end warmup -> wait freeze
awaiting.require(game, "move")       # "blocked: dead does not allow move"
```

The signals it reads (`state`): `in_game` — **the main-screen rule**, true only
when the level's `C_CSGameRulesProxy` entity exists (the `game_rules` global is
readable at the main menu too, so it cannot be the test) — the `rules` block
(`warmup`, `freeze`, `round_win_status`, `t_cant_buy`/`ct_cant_buy`,
`game_phase`, `round_time`, `bomb_planted`, …), plus `local.team` (0 =
team-select, 1 = spectator, 2/3 = T/CT) and `local.alive`.

| Screen / state | How it looks | What to do |
|---|---|---|
| **Engine not ready** | `valid=false` (right after injection, or mid level-teardown) | **wait** — `exec` faults inside the engine before its input service is up, and the game dies with it |
| **Main menu** | `in_game=false`, `in_map=true`, `origin=(0,0,0)`, `team=0` | load a map (`map de_dust2`); `exec` can fault if issued too early |
| **Loading** | `in_game=false` (once a level's rules are up) / `valid=false` right after the command | wait — never re-issue `map` while a load is in flight (it restarts the load; the 2026-10-15 livelock) |
| **Team-select** | `in_game=true`, `team=0`, `alive=false` | input is ignored — **join a team** (`jointeam 2`) |
| **Warmup** | `rules.warmup=true` | `mp_warmup_end` |
| **Freeze** | `rules.freeze=true` | wait for it to clear (input is ignored) |
| **Round over** | `rules.round_win_status != 0` | wait for the next `round_start` |
| **Dead** | `alive=false`, `life_state=2` | input is ignored — wait for respawn |
| **Bomb carrier** | `weapon="weapon_c4"`, `clip=-1` | `+attack` does **not** fire a gun — select a weapon (`slot2`) first |
| **Bots moving** | `player_hurt` / `player_death` events keep arriving | `bot_stop 1` freezes them (a round restart un-freezes them) |

`awaiting.ensure_live_round` (= `ensure(Screen.LIVE, verify_movable=True)`) gates
on the screen **plus** "a console `+forward` actually moves you" — **but it issues
`mp_restartgame 1`**, which respawns everyone, moves you to spawn and un-freezes
the bots. Do **not** call it when the operator has set up a specific position;
check `awaiting.require(game, "move")` (or `wait_for(playable=True)`) instead. A
round can also end *during* a test, so re-check right before acting.

The `playable` predicate (payload) is `alive && team ∈ {T, CT}` — false on the
team-select screen, as a spectator, or when dead. `status` prints it implicitly
via `team`/`alive`.

### 5. Validation — prefer direct state over events

- **`wait_for(event=…)` only matches events that fire *after* the call starts.**
  Calling it *after* firing a shot can never see the hit it just caused. Verify
  actions with a **direct state read** (target health before/after, origin
  distance, `clip`), not a `player_hurt` wait. (This produced a long false "silent
  aim doesn't work" conclusion.)
- `status` (scenario) prints `valid`, `in_map`, `alive`, `team`, `on_ground`,
  `weapon`, `clip`, `players`, and the tail of the event stream — the fastest way
  to see why a test did nothing.
- The sub-tick scenarios embed that state in their result/error, so a failure says
  whether it was a bad state or a real mechanism miss.
- **Operator confirmation is required** for anything visual: whether the camera
  actually turned, whether the player actually walked, whether the shot looked
  like it landed. The runner's numbers and your eyes should agree.

### 6. Scenarios

| scenario | what it does | needs |
|---|---|---|
| `status` | read-only state + recent events | — |
| `join-bot-match` | load a map, add bots | — |
| `move-and-aim` | `setang` + console `+forward`; asserts movement | live round |
| `game-events` | asserts game events fire | live round |
| `usercmd-move` | jump + bhop via the command button mask | live round, usercmd hook |
| `subtick-aim` | silent angle write lands, camera does not follow | live round, usercmd hook |
| `subtick-shot` | camera 45° off a bot, silent aim, target **health drops** | live round, a gun, an enemy |
| `subtick-move` | raw-input analog move, asserts displacement | live round, usercmd hook |
| `movement-probe` | dumps the engine's own move convention, then tries every movement write variant (quantize/step/raw_entry) and reports which moved | live round, usercmd hook |
| `screens` | prints the state machine's tables and walks `→ LIVE → kill → DEAD → LIVE`, asserting the capability gate | — |
| `strafe-probe` | decides which yaw channel steers movement and whether it is silent (console/usercmd, silent history, raw history, command angles, step yaw) | live round, usercmd hook |
| `autostrafe` | **observation-only** check of the payload's auto-strafe/bhop: reads the menu's settings (never writes), watches the payload's own `strafe_airborne`/`strafe_writes`/`strafe_delta` while the feature runs in a live round, and reports the speed gained and the eye-angle drift (silence) | live round, features enabled in the menu's `Movement` tab |
| `move-to` | walks to `(x, y)` with `move()`; logs pos/dist/writes; stops ≤ `tol` | live round, usercmd hook |

All in attach form:
`python scripts/run-scenario.py <name> --no-launch --no-inject --keep-running [--set k=v …]`.

### 7. What is validated vs open (2026-10-15)

Validated live on **25947352**:

- launch + inject + connect; `state`, `events`, `exec`;
- `usercmd` **discrete** buttons — jump + bhop (`usercmd-move`);
- **analog movement end-to-end** — `move(forward=1.0)` walks **204 units in 1 s**
  (1.01× the engine's own console `+forward`, 203) and `move(forward=0.5)` walks
  115 (0.57×), i.e. the analog magnitude is honoured; needs the engine's units
  (`1.0`, not 450) and an unobstructed spot (`movement-probe` pins one, sweeps for
  a clear heading and waits for the teleport *and* zero velocity before each
  measurement);
- **the silent movement yaw channel** (`strafe-probe`) — a `CSubtickMoveStep` carrying
  only a `yaw_delta` (one per tick, `when = 1`) turns the walk by exactly that delta
  (**89.9°** from a 90° step, 209 units) with the pawn's eye angles **unchanged before
  and after** — so this is the **silent** auto-strafe seam
  (`look(step_yaw=<desired yaw − camera yaw>)` + `move(forward/side)` per tick). The
  alternatives are dead ends: the command's own `viewangles` steers but turns the eye
  angles with it, and neither the silent-aim `input_history.view_angles` nor the raw
  per-sub-tick input history (`CCSGOInput + 0xbd0`) affect movement;
- **screen state machine** (`screens`): main screen → `not-ready`/`in-game` false
  (the rule: `C_CSGameRulesProxy`-style signals all fail — see
  [`docs/re/client-dll.md`](re/client-dll.md) §"Am I in a game?"), `load_map` out
  of the main screen, `team-select → join_team → freeze → live`, `kill → dead`
  with `move`/`attack`/`look` refused, `wait_respawn → live`;
- **silent aim end-to-end** — camera 45° off a bot (yaw 137.06), sub-tick aim at
  yaw 92.06, hits landed (hp 67→51, 51→34) with the camera unmoved
  (`subtick-aim`, `subtick-shot`).

**Analog movement — implemented, not yet validated live.**

- `create_move_input` *derives* `forwardmove`/`leftmove` from `CCSGOInput + 0x270`
  each tick, and with `sv_quantize_movement_input` on the server **rounds the
  analog floats to integers** (only the nine `{-1,0,1}²` states survive) —
  UC 729264 / 753644;
- our plain write lands (jump works; `move_writes` climbs) but the player does not
  move — and since console `+forward` **does** move, the local movement reads the
  **engine's own input state**, not the command copy;
- `+0x270` is re-derived *inside* slot 5 by `FUN_180b5f7b0` from the per-controller
  **input entry** (array `CCSGOInput + 0xb58`, stride `0x440`, move pair at
  `+0x18`), so writing `+0x270` before the original is clobbered.
- **Analog movement — validated live (2026-10-15).** `move(forward=1.0)` walks 204
  units in a second (1.01× the engine's console `+forward`, 203) and
  `move(forward=0.5)` walks 115 (0.57×): the analog magnitude is honoured. The
  missing ingredient was the **unit** — the engine writes `±1` (`cl_forwardspeed`
  is applied downstream), so the payload's old `450` moved nothing — plus a clear
  test spot. `movement-probe` pins the spot (teleport *and* zero velocity waited
  for before every measurement), sweeps for a clear heading, kicks the bots,
  asserts the screen is movement-capable, and reports a console baseline per run.
  **`raw_entry` remains unsafe** (writing `CCSGOInput + 0xb58 + 0x18` crashed the
  game with an input-path AV, `client.dll + 0xb64a80`, and reads denormals).
- Fallback that works today: the console `+forward` path (`move-and-aim`).

Also open: **console-output capture** (`exec` returns `{queued}` only;
`-concommandpipe` is input-only), and relating `dwViewAngles` to `view_angles`.

### 8. The `move-to` scenario

`move-to` exercises the **sub-tick `move()`** (not the console `+forward`): each
tick it takes the world direction to `(x, y)` and rotates it into the view frame
using the player's current yaw (`forward = u·(cos yaw, sin yaw)`,
`side = u·(-sin yaw, cos yaw)`), so it is independent of where the player looks —
no `setang`. It logs `dist`, `pos`, `yaw`, the computed `move fwd/side`, and the
payload's `raw_writes`/`move_writes`/`cmd+0x58` every 0.5 s, and stops within
`tol` (default 5): `--set x=… --set y=… --set tol=5`.

### 9. Build / run cheat-sheet

```sh
# Host build (native)
meson setup build && meson compile -C build && meson test -C build

# Native Windows (clang-cl; from a vcvars64 environment with LLVM on PATH)
set CC=clang-cl && set CXX=clang-cl
meson setup build-payload payload && meson compile -C build-payload
meson setup build-injector injector && meson compile -C build-injector
meson setup build-overlay overlay && meson compile -C build-overlay

# Payload (cross, Windows target)
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload

# Live calibration (game running) + SDK regeneration
python scripts/calibrate-live.py            # writes calibration/*.json
python scripts/gen-sdk.py calibration payload/sdk
meson compile -C build-payload              # rebuild against the fresh SDK
```

Run flow on Windows: `schtasks /run /tn CS2Launch` (or the AGENTS.md command) →
wait for `client.dll` to load → `build-injector\injector.exe <pid>
build-payload\payload.dll` → read `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`
and `cs2-overlay-layer.log`.

Scenario harness (see §1–§8 above):

```sh
python scripts/run-scenario.py --list
python scripts/run-scenario.py status       --no-launch --no-inject --keep-running
python scripts/run-scenario.py screens      --no-launch --no-inject --keep-running
python scripts/run-scenario.py subtick-shot --no-launch --no-inject --keep-running
python scripts/run-scenario.py movement-probe --no-launch --no-inject --keep-running
python scripts/run-scenario.py move-to      --no-launch --no-inject --keep-running --set x=… --set y=… --set tol=5
```

Diagnostic env switches: `CS2_PAYLOAD_NO_DIAG`, `CS2_PAYLOAD_NO_PUBLISH`,
`CS2_PAYLOAD_PUB_THREAD` (dedicated publish thread instead of the game-present
hook), `CS2_PAYLOAD_PUB_NOSTATE`, `CS2_PAYLOAD_PUB_NOWRITE`,
`CS2_PAYLOAD_PUB_NOSHM`, `CS2_PAYLOAD_PUB_INTERVAL_MS`, `CS2_PAYLOAD_DIR`,
`CS2_PAYLOAD_NO_CONTROL`, **`CS2_PAYLOAD_MOVEMENT`** (comma-separated `strafe`/`bhop`:
the payload-native movement features, seeded into the overlay menu's `Movement` tab —
read from the **game's** environment, which is Steam's, so setting it in
`launch-cs2.cmd` only applies when that script also starts Steam; a user env var +
Steam restart, the menu, or the control commands are the reliable paths);
and on the layer `CS2_OVERLAY`, `CS2_OVERLAY_SHM`, `CS2_OVERLAY_LOG`,
`CS2_OVERLAY_NO_DRAW`, `CS2_OVERLAY_NO_WAIT`, `CS2_OVERLAY_NO_CHAIN`.

## Open items

- The overlay layer is enabled via a machine-wide implicit-layer registry entry
  (HKLM) scoped by `CS2_OVERLAY=1`; a per-user/portable registration is still
  open.
- Decide whether the Windows default should be the dedicated publish thread or
  the game-present hook (the hook path was not exercised in the validated run).
- A strict swapchain-recreation stress (resolution change) was not captured on
  Windows (the windowed/fullscreen toggle did not recreate the swapchain); the
  layer's `vkCreateSwapchainKHR` handling is unchanged from the Proton-validated
  path.
