# `client.dll` — world occlusion geometry for the ragebot (build in progress)

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.

> Companion to `docs/re/vphysics2.md` and `docs/ragebot.md`. Records the client-side
> **world-solids enumeration** the occlusion heatmap (ADR-0022 Tier 2) needs: the
> ragebot projects the solids between the eye and a target into the angular chart
> instead of tracing rays.

> **Build note.** The game auto-updated while this was being written: the module
> under analysis is now sha256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`
> (was `44af2aa0…`). Everything below was re-derived on the **new** build.

## The query interface — `CGamePhysicsQueryInterface`

Ten methods, each registered by name (so **the name string xrefs the function** —
this is how the addresses below were recovered without symbols). Name strings live
in `client.dll` around `0x181c321xx`:

| Method | name string | xref (inside fn) | old RVA |
|---|---|---|---|
| `TraceShape` | `0x181c321a8` | `0x180a1a06e` | 0xa19140 |
| `TraceShapeToEntity` | `0x181c32208` | `0x180a1aad8` | 0xa19bb0 |
| `EntitiesInBox` | `0x181c32248` | `0x1809dbd67` | — |
| `EntitiesInSphere` | `0x181c32290` | `0x1809dc5ca` | — |
| **`EntitiesInShape`** | `0x181c322d0` | `0x1809dc147` | 0x9db220 |
| **`EntitiesAlongLinearSweep`** | `0x181c32320` | `0x1809dba13` | 0x9daae0 |
| `FilterOverlapResults` | `0x181c32370` | `0x1809df508` | 0x9de620 |
| `GetPointContents` | `0x181c323c0` | `0x1809e8d77` | 0x9e7e50 |
| `TraceShapeWithSortedResults` | `0x181c32410` | `0x180a1afd6` | 0xa1a0a0 |
| `TraceLineIntervals` | `0x181c32460` | `0x180a19ef2` | 0xa18fd0 |

The xref address is *inside* the function; its entry is found with
`getFunctionContaining` (e.g. `EntitiesAlongLinearSweep` entry = `0x1809db9a0`).

### `EntitiesAlongLinearSweep` — the enumeration primitive

`FUN_1809db9a0`, source `src/game/shared/gamephysicsquery.cpp` line `0x5cd`:

```c
// this = CGamePhysicsQueryInterface (arg1); forwards to the physics world vtable +0x5a8
EntitiesAlongLinearSweep(this, param_2, const Vector& start, const Vector& end,
                         params /* flags @+0x37, field @+8 */, CUtlVector* results);
```

One swept query from `start` (the eye) to `end` (the target) returns **all the
solids in between** as `results` — the "whole player in one go" enumeration. Two
tails convert the raw hits: `FUN_1809d2dc0` and `FUN_1809d2c40`.

## Materials (penetration modifiers)

`scripts/surfaceproperties_game.txt` (`0x181c32010`) + `SurfacePropertiesList`
(`0x181e49318`) — the surface-property list carrying the penetration/damage
modifiers the heat map multiplies in. Shape layer: `CGenericShapeProxy::InitPhysicsShape`
(`.?AV<lambda_N>@?1??InitPhysicsShape@CGenericShapeProxy@@…`).

## Resolved (decompiled on the new build)

### The overlap record — stride `0x70`

`FUN_1809d2dc0` = **`ConvertOverlapMultiple`** (`gamephysicsquery.cpp` line `0x17a`);
`FUN_1809d2c40` = **`ConvertHitboxOverlapMultiple`** (line `0x197`). Both append
`0x70`-stride records to the results `CUtlVector`. The fields that matter:

| Offset | Meaning |
|---|---|
| `+0x00` | transform (rotation/position blob) |
| `+0x18` | **entity handle** (`uint`) |
| `+0x30` | second transform / bounds blob |
| `+0x50` | a value from a shape vtable call (`lVar6+0x28`) |
| `+0x58` | **physics object/shape pointer** (the `vtable+0x10` result) |
| `+0x60` | 0 (hitbox variant) |
| `+0x68` | a value copied from the source hit |

`0x8000` is the **worldspawn** handle (the map's brushes) — exactly what the
occlusion heatmap needs.

### `FilterOverlapResults` — `FUN_1809df4e0` (line `0x6b2`)

- Reads `*(uint*)(record + 0x18)` (the handle).
- `handle == 0x8000` (worldspawn) → **kept unconditionally**.
- otherwise resolves it through the entity list (`DAT_18236fa70`; chunk
  `(h & 0x7fff) >> 9`, slot `(h & 0x1ff) * 0x70`, validate `*(uint*)(e+0x10) == h` —
  the known handle resolver) and keeps it iff the filter callback
  (`*(vtable + 8)(filter, entity)`) returns true.

### `EntitiesAlongLinearSweep` ABI — `FUN_1809db9a0`

```
EntitiesAlongLinearSweep(this, param_2, const Vector& start, const Vector& end,
                         filter /* flags @+0x37, field @+8 */, CUtlVector* results);
  world = *(void**)this;                     // the physics world object
  if (filter->flags & 7)   world->vtable[0x5a8/8](world, tmpRecords, param_2, &start, &delta, filter);
  if (filter->field8 bit1) FUN_1801e2160(...); // a second (sweep) query kind
  ConvertOverlapMultiple(results, tmpRecords);
  FilterOverlapResults(this, results, filter);
```

`EntitiesInShape` (`FUN_1809dc0e0`, line `0x599`) dispatches on
`*(char*)(param_2 + 10)`: `1` → box (`FUN_1809dc550`), `2` → sphere
(`FUN_1809dbd20` = `EntitiesInBox`), else a shape query via world vtable `+0x5b0`.

## Resolved: the record → shape chain (live, 2026-10-17)

The §"overlap record" note above is **superseded**: the record's `+0x58` is not a
shape pointer, it is a **`CPhysicsBody*`** (vphysics2.dll). The record **build** was
re-decompiled on the client.dll pass (2026-10-17, `ConvertOverlapMultiple`
`FUN_1809d2dc0`, client.dll `659940c1`), which pins each field to its producer:

| Object | Field | Producer / meaning |
|---|---|---|
| overlap record | `+0x00..0x20` | `hit->vtbl[0x50]()` — the transform blob |
| overlap record | `+0x18` | `body->vtbl[0x478]()` — entity **handle** (`0x8000` = worldspawn) |
| overlap record | `+0x50` | `*(hit->vtbl[0x30]() + 0x28)` — a per-*hit* **material token**. Note the slot `0x30` is on the **client-side hit object** (whose `[0x10]` yields the body), *not* the vphysics2 shape. An **8-byte** load; whether `+0x28` is a float modifier or a pointer is unresolved (see "Still to pin" 5). |
| overlap record | `+0x58` | `hit->vtbl[0x10]()` = **`CPhysicsBody*`** (vtable `vphysics2+0x3f44e0`, RTTI `.?AVCPhysicsBody@@`) |
| overlap record | `+0x60` | 0 |
| `CPhysicsBody` | `+0x08` | body **data** block |
| body data | `+0x00` | entity handle (int) |
| body data | `+0x60` | **shape count** (int) |
| body data | `+0x64` | capacity / flags (`& 0x7fffffff`; ≤1 ⇒ array is inline) |
| body data | `+0x68` | **shape pointer array** (pointer when count > 1, else the inline element) |
| shape | `+0x18` | type (2 = hull, per `docs/re/vphysics2.md`) |
| shape | `+0xC0` | `RnHull_t*` (`+0xB8` = uniform scale) |
| `RnHull_t` | `+0x70`/`+0x78` | vertex count / vertices (stride `0xC`) |

The build loops over the query's raw hits, so a record is **per hit** (per body for
the worldspawn). `walls.material_token` / `material_float` (a one-record RE probe in
`occlusion_query.cpp`) report `+0x50` raw so a live run can settle its type.

### The engine's trace path (reversed live, 2026-10-18)

`FUN_180a1a000` (`CGamePhysicsQueryInterface::TraceShape`, `gamephysicsquery.cpp:0x3a9`)
is the ground truth for "which solids are between these two points":

```
(*(*iface + 0x560))(world, results, descriptor, &start, &delta, filter)   // world query
sort(results)                                  // 0x48-byte layers, distance-sorted
for each layer:
    handle = layer.hit->vtbl[0x478]()          // the entity handle
    if handle == 0x8000: accept                // worldspawn, unconditional
    else entity = resolve(handle)
         if filter->vtbl[8](filter, entity): accept
```
The engine **traces** and takes the first filter-accepted layer; the filter is a
`ShouldHitEntity` at vtable slot `+8`.

**The engine's static default filter accepts everything.** `*(client+0x2227070)` ->
vtable `client+0x1ad4838` -> `ShouldHitEntity` at **`client+0x22d900`**, which is
literally `mov al,1; ret`. Our payload passed exactly that filter, so *every* entity —
including the players' collision bodies and the local player's own — came back as an
occluder. **Fixed**: the players are now dropped payload-side, resolving each record's
`+0x18` handle exactly as `FilterOverlapResults` does (paged identity table) and
comparing the entity to the players' pawns (`resolve_entity` in `occlusion_query.cpp`).
A payload-built filter is **not** usable: the engine reads more of the filter vtable
than slot `+8`, so a minimal one makes the query return nothing (and crashes).

**Still different from the engine (the missing world geometry):** we enumerate with
`EntitiesInBox` — a box overlap of whole **bodies** — and walk every shape, whereas the
engine **traces** and takes the layers on the segment. So geometry not on the
eye→target line, and some world brushes, never enter the chart. The Tier-1
`TraceShapeWithSortedResults` path (`docs/ragebot.md` §4, and the recipe in
`docs/re/aim.md`) is the engine-faithful replacement.

`CPhysicsBody` vtable slots used here (from the concrete vtable at `0x1803f44e0`):

| slot | impl | behaviour |
|---|---|---|
| `0x38` | `0x180011340` | `*(int*)(*(body+8) + 0x60)` — **shape count** |
| `0x48` | `0x180011350` | **shape accessor**, index in EDX; `shapes[index]` |
| `0x50` | `0x180011390` | iterate shapes (its engine use) |
| `0x1b0` | `0x180011fb0` | **world transform** — writes 8 floats: `pos(x,y,z), 1.0, quat(x,y,z,w)` |
| `0x478` | `0x1800137c0` | `*(int*)(*(body+8))` — **entity handle** (confirmed by the client.dll record build, 2026-10-17) |

> The engine's own walk is `CGamePhysicsQueryInterface::TraceShapeToEntity`
> (`FUN_180a1aa70`), which goes *entity → body → shape* rather than through the
> record: `FUN_18021a620(entity, i)` (body list) → `[0x10]` count, `[0x20](i)` a
> `CPhysicsBody` → `[0x38]` shape count, `[0x48](j)` shape, `[0x1b0]` transform;
> it traces with `shape->[0x80]` and reads the material via `shape->[0x30]()`.
> The record's `+0x58` gives the same `CPhysicsBody` directly, so the payload skips
> the entity walk.

**Live facts (de_dust2).** The overlap record for the worldspawn handle `0x8000`
carries the **world body**, whose shape count is **10149** (one `IPhysicsShape` per
map brush) — the map-brush source. Entity/prop bodies have 1–2 shapes (type 2 hull).
Hull vertices of entity bodies are **body-local** (apply the body transform); the
world body's hulls are already world-space.

**Cost.** Walking the world's ~10k brush shapes every tick is far too slow (it
stalled the game): the payload parses them **once** into a cache keyed by the body
pointer, then per query filters the cached hulls by the eye→target AABB (cap 512).

**Gotchas.**
- `Calling the `0x48` shape accessor from the payload returned garbage; the direct
  `*(data+0x68)` array read is the one that works live.
- Do **not** call a per-shape vtable slot you have not validated: calling
  `shape->[0x50]` on every brush (it is a collision routine for hull shapes)
  faulted. **Today** that fault is survivable — the call goes through
  `cs2::diag::call_guarded`, the guard logs it and returns a failure — but
  the guard is a safety net, not a licence: a call that faults inside the
  engine's own code is still a wrong call.
- A namespace-scope `std::vector` / a function-local `static` adds a CRT import
  (`SleepConditionVariableSRW`) the manual-map injector cannot resolve — keep
  payload globals POD.

## Resolved: the hull planes + the per-cell depth test

`RnHull_t` faces: **plane count `+0x88`**, **`RnPlane_t* +0x90`**, stride `0x10`
(`normal.xyz` + `dist`). The payload reads them into `OcclusionHull::planes` and the
aim side orients each inward (`dot(n, x) <= d`) from the **vertex centroid** — the
engine's stored winding is not assumed, the centroid decides the sign.

That gives the exact occlusion test the design needs (`docs/ragebot.md` §3,
ADR-0022 Tier 2): `OccluderCell::ray_span(eye, direction)` clips the ray against the
solid's half-spaces and returns `{entry, exit}` — **planar arithmetic, no ray casts**.
`build_heatmap` then (a) keeps only the solids the shot to a hitbox reaches
*before* it (`entry < hitbox_distance`) and (b) applies the in-front thickness
`min(exit, distance) − entry`. This is what makes the set literally *"every solid
between the eye and the hitbox"*: the floor under the target and walls behind it are
excluded by geometry, not by a bounding-box heuristic.

**Deleted** (superseded band-aids): the `walls_radius` box widening,
the `separates` eye→target AABB test, the 24-unit minimum distance, and the
`reach > 8·extent` projection drop. **Kept:** the box enumeration as a *superset*
(eye → farthest target, 8-unit margin) — completeness only; correctness comes from
the per-cell depth test.

**Fixed (2026-10-17, second session).** The **broad phase** in `build_heatmap` was
wrong: it kept a solid only when the ray to the hitbox **centre** entered it, so a
solid covering only *part* of a silhouette (a pillar beside the centre ray) was
dropped and its covered part kept full damage — contradicting §3 ("occluders cut
`Ω_i` into cells", i.e. by **overlap** then by depth). It is now a real overlap
broad phase (silhouette AABB overlap ∧ `OccluderCell::lower_bound_distance(eye) <
hitbox_distance`), and the exact per-fragment `ray_span` depth test decides
attenuation. `lower_bound_distance` is a **conservative** lower bound (from the
hull's plane distances), so pruning can never drop a solid that is genuinely in
front. New regression tests in `aim-occlusion`.

## Still to pin

1. ~~Entry addresses + signatures for the method table (ADR-0012).~~ **Done
   (2026-10-17, second session)** for the three client.dll RVAs this file uses:
   `physics_query_interface` (`0x2227058`), `physics_query_filter` (`0x2227070`) and
   `entities_in_box` (`0x9dbd20`) are now curated signatures in
   `external/dumper/src/analysis.rs` (anchors derived statically from the build-659940c1
   binary and uniqueness-verified). The payload reads them from `cs2/sdk/offsets.hpp`
   (`sdk_client_offset`, literal fallback) instead of hard-coding. **Remaining:** the
   `CPhysicsBody`/`RnHull_t`/`RnMesh_t` *struct offsets* are still literals (struct
   offsets don't move like RVAs, so this is lower priority; a schema/signature pass can
   cover them later).
2. The filter struct / `param_2` layouts for the call.
3. ~~**Compound (4)**~~ **Implemented (2026-10-18), not live-validated.** The pool is
   read and recursed: `RnCompound_t` (0x160 B at `shape+0xC0`) holds the children **by
   value** — `RnHull_t[]` at `+0xF0`/`+0xF8` (stride `0xF8`) and `RnMesh_t[]` at
   `+0x28`/`+0x30` (stride `0xC0`) — and the payload emits them (`occlusion_query.cpp`
   `case 4`), transform by the body transform, with bounds checks. **Finding:** CS2
   gameplay props are multi-hull **bodies** (many hull shapes in one `CPhysicsBody`),
   not compound shapes — the world body is `{hull 10143, mesh 6}`, spawned props and
   dropped weapons are all hulls, and no type-4 shape was found live — so this path is
   inert in practice. The shape's separate 0x30-byte child table at `shape+0xD0` and the
   per-child transforms remain unpinned (need a live compound). See
   `docs/re/vphysics2.md` §"Shape geometry offsets".
4. **Non-hull shape types**: **spheres (0), capsules (1) and meshes (3) are now read**.
   The payload parses their geometry (`shape+0xB8`/`+0xC4`/`+0xD0` for the primitives;
   `shape+0xC8` → `RnMesh_t` vertices `+0x38`/count `+0x30` **and triangle indices
   `+0x50`** for meshes — `docs/re/vphysics2.md`, `docs/re/bullet-trace.md`), and the
   aim side projects spheres/capsules analytically
   (`project_sphere_occluder`/`project_capsule_occluder`) and **meshes as a triangle
   soup** (`project_mesh_occluder`; the exact ray test intersects the triangles,
   `OccluderCell::ray_span`), all host-tested. This matters: the worldspawn body's
   shape histogram is `{hull 10143, mesh 6}` and the map's **large walls are meshes**
   (`docs/re/bullet-trace.md`). **Compounds (4) are still skipped**: their children are
   a typed sub-array structure (`RnCompound_t` at `shape+0xC0`) that is not fully mapped.
5. ~~**Surface/penetration modifiers**~~ **Resolved (2026-10-18).** Each
   `IPhysicsShape` carries a pointer to its surface-material object at **`shape+0x48`**
   (live-verified: exactly the value the record's `+0x50 = *(hit->vtbl[0x30]()+0x28)`
   reports for the hit shape). That object's `+0x10` is the index into the game's
   `SurfacePropertiesList` (`FUN_1809e3560`: `*(int*)(obj+0x10) * 0x20 + [manager+0x28]`,
   manager = `client+0x254e100`). The `CGameSurfaceProperties` record (0x20 B) is
   parsed by `FUN_1809cd960`; the penetration fields are
   **`bulletPenetrationDistanceModifier` @+0x08**, `bulletPenetrationDamageModifier`
   @+0x0C and `gamematerial` (int16 `CHAR_TEX_*`) @+0x14. **Live:** the world body's
   10149 brushes resolve to **29 distinct** surfaces — concrete 0.5, metal 0.4, wood
   0.9, glass 0.99, … — so **`occlusion_query.cpp` now reads the real per-brush surface**
   (`read_shape_surface`) and the model is engine-exact (`docs/re/penetration.md`). The
   `pen_mod`/`dmg_mod`/`material`/`grate` args remain as overrides.
6. **The cone enumeration / BVH.** The per-query broad phase no longer re-scans the
   vertex arrays: each cached hull now carries its **own `RnHull_t` AABB**
   (`+0x14`/`+0x20`, read once at parse time — `occlusion_query.cpp`
   `OcclusionHull::has_bounds`), so `hull_bounds` is O(1)/solid. **Live-verified:** the
   header AABB equals the vertex AABB for **all 10143** world hulls (worst error
   `0.0000`). `EntitiesAlongLinearSweep` (`FUN_1809db9a0`) is still blocked: like
   `TraceShape`, it takes a caller **`Ray_t`/shape descriptor** as `param_2`
   (`world->vtbl[0x5a8](world, out, param_2, &start, &delta, filter)`), and that
   descriptor is not yet pinned (`docs/re/aim.md` §"The call convention"); guessing it
   risks a misbehaving engine call for no gain while the box query is already
   BVH-accelerated at the record level. Remaining: pin the descriptor, then swap the box
   for the swept query (P1.8).

## Resolved: the singleton + call form

From a live caller (`FUN_1809c6740`):

- **The interface singleton** = `*(client_base + 0x2227058)` — Ghidra's
  `PTR_DAT_182227058` (old build `client_base + 0x221f058`; the data section moved
  ≈ `+0x8000`). It is passed as **arg1** to every query method.
- **The entity-list global** used by `FilterOverlapResults` = `DAT_18236fa70`.

```c
// filter: { void* vtable; ...fields... }  — vtable of the static default is
// PTR_FUN_181ad4838 (RVA 0x1ad4838); flags byte at +0x37, field at +8.
local_1c98 = &PTR_FUN_181ad4838;     // filter vtable
local_1c48[0] = 0;                   // results CUtlVector (count only; grows)
... filter fields copied from the caller's object ...
FUN_1809db9a0(PTR_DAT_182227058 /* this */, obj + 8 /* param_2 */,
              start = obj + 0x40, end = obj + 0x4c, &filter, results);
```

So a payload implementation is: read `*(client_base + 0x2227058)`, build a filter
(there is a **static default filter** object, `+0x18` after the query global →
`client_base + 0x2227070`), call `EntitiesAlongLinearSweep` (or `EntitiesInBox`,
which takes two box corners: `FUN_1809dbd20(this, box_min, box_max, filter,
contents, results, flag)`), then walk the `0x70`-stride records.

> **Risk.** A wrong ABI used to abort the process. **Not any more:** the
> injector registers the payload's `.pdata` (`RtlAddFunctionTable`), so the
> image *does* have exception tables, and the engine calls go through
> `cs2::diag::call_guarded`, whose vectored handler recovers a fault on the
> armed thread, logs it, and returns a failure.
>
> **The guard's limits, so the risk is not read as a licence:** one in-flight
> guarded call per process (a second concurrent one is refused); only the
> recoverable codes (`EXCEPTION_ACCESS_VIOLATION` and friends); only a fault
> **on the thread that entered the guard** (a fault on another thread is not
> guarded); and the recovery is a `longjmp`, so the guarded call's cleanups
> are skipped — the guarded call must not own resources.
>
> Validate the first call (`EntitiesInBox` with the static filter, count the
> records) before projecting anything, and derive signatures rather than
> hard-coding RVAs (ADR-0012).

## Tooling note — REA's Ghidra cache is broken on this host

> **Resolved 2026-10-20.** The reopen failure below was a cache-root problem, not a
> stored-DB problem: the root was not owner-only, and the durable root was the sealed
> native runtime root. Upstream `d4b8afbba` (`morluto/rea#3531`) fixes both, and the
> cache root is now private and self-repairing. The entries seeded before the fix were
> lost, so re-seed them (`docs/re/README.md` §8).

REA (`v6.4.0`, `REA_GHIDRA_PROJECT_CACHE=on`) writes a cached Ghidra project for a
new target, but **reopening it fails** with
`java.io.IOException: Unrecognized file format` at `LocalBufferFile.readHeader`
(`db.1.gbf`). `rea cache-verify` reports the entry `ok`, so the corruption is in the
stored DB, not the index — REA appears to cache the project before Ghidra cleanly
closes it. Consequence: any second REA call on that target cold-analyzes (and
`execute`/CLI budgets cut it off).

Workaround (used here): drive Ghidra headless against a **persistent** project and
keep it for `-process -noanalysis` afterwards —

```
analyzeHeadless C:\Users\gpmpo\ghidra-analysis clientoccl -import <client.dll> \
  -scriptPath scripts/ghidra -postScript DumpTargets.java
# later, fast:
analyzeHeadless C:\Users\gpmpo\ghidra-analysis clientoccl -process <dll> -noanalysis \
  -scriptPath scripts/ghidra -postScript DumpTargets.java
```

`scripts/ghidra/DumpTargets.java` decompiles a fixed address list to
`artifacts/ghidra-occlusion.txt`.

## The trace probe: the engine's own answer for the same ray (T39, 2026-10-22)

The payload's occluder set and the engine's bullet trace are **two different
answers** to "what is between the eye and the target", and until now there was no
way to compare them. T39 added the instrument: the **`trace_probe`** control
command asks the engine its own question — `TraceLineIntervals`, the call the real
bullet makes (`docs/re/bullet-trace.md`) — for a given ray, and publishes **both**
answers side by side plus the **difference**.

It **measures**; it does not fix. The fix is a later task, driven by the answer
this one produces.

### What the command is

`trace_probe` (the control channel; `payload/control/src/service.cpp`,
`do_trace_probe`):

| argument | meaning | default |
|---|---|---|
| `rays` | which rays to probe | `"view"` — the current view ray |
| `eye` | the ray origin, `[x, y, z]` | the local player's eye |
| `yaw` / `pitch` | the view direction, in **degrees** (the Source pitch is positive down) | the local player's eye angles |
| `spread` | an extra angular fan around the view ray, in degrees | `0` |
| `count` | how many rays in the fan | `1` |
| `depth` | the ray's length, in world units | the engine's own trace length (the weapon's `m_flRange`), or **8192 units** (the engine's maximum trace length) when the weapon's range cannot be read |
| `abi` | how the engine is called: `correct` / `swapped` / `static_filter` | `correct` |

The fan is **deterministic**: ray 0 is the view ray itself and rays 1..n-1 are the
golden-angle spiral points of the disc of radius `spread` degrees around it, so two
runs compare. The direction comes from the chart's own convention
(`Basis::unproject` of the absolute world yaw/pitch), so the probe's ray and the
solver's ray are the **same** 3-D segment — the host test `control-trace-probe`
pins that.

The answer, per ray: the engine's intervals (the fraction, the distance, the
enter/exit byte and the record's **raw 0x38 bytes in hex**), our own occluders'
spans (with their kind), and the **difference** — the spans we have nothing for,
the solids the engine did not report, and a one-line verdict (`agrees` /
`engine_only` / `ours_only` / `both` / `unknown`).

**`unknown`** is the verdict when the engine's answer could not be read at all
(the call faulted, or the container did not hold the buffer the probe handed the
engine). The difference is then **not** computed over an empty engine list — that
is the whole point: the defect the probe exists to expose must not be reported
inverted. The per-ray `engine.answer_missing` and the per-ray
`difference.engine_answer_usable` say so explicitly.

**The difference's rule.** A span and one of our occluders are the same solid
only when our occluder **covers** the engine's span — it starts no later than the
span and ends no earlier (both within a 2-unit position tolerance) — and the two
share a stretch of the ray. The tolerance compares the two spans' **positions**;
it is **not** a widening of the overlap test. A solid that merely sits in front of
the engine's wall, or that covers only a part of it, is a **different** solid and
does not answer for the wall: the wall stays in `engine_only`.

### The invocation

The command is answered on the control channel (`\\.\pipe\cs2-internal-control`),
so it runs from the scenario harness — or from any client on that pipe. The
copy-pasteable form (from the repository root, with the payload injected and the
game running):

```powershell
@'
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path("scripts").resolve()))
from scenario.control import Control
with Control() as control:
    answer = control.request("trace_probe", {})          # the view ray
    print(json.dumps(answer, indent=2))
'@ | Set-Content -Path $env:TEMP\trace_probe.py -Encoding utf8
python $env:TEMP\trace_probe.py
```

The arguments (all optional): `rays` (`"view"`), `eye` (`[x, y, z]`), `yaw`,
`pitch` (degrees), `spread` (degrees), `count`, `depth` (world units), `abi`.

The raw wire form (one NDJSON line on the pipe):

```json
{"id": 1, "cmd": "trace_probe", "args": {"count": 5, "spread": 4.0}}
```

**Run the heatmap once first** (`{"cmd": "heatmap"}`), so the static world body is
already parsed: the probe's own enumeration then reuses that cache and the probe
does not pay the world's parse inside the input hook. The answer says which
happened (`engine.world_cached`).

**The proof that the probe can fail** — the deliberately wrong calls (they must
give a visibly wrong answer or a caught fault, never a crash):

```json
{"id": 2, "cmd": "trace_probe", "args": {"abi": "swapped"}}        // the start and the delta are swapped
{"id": 3, "cmd": "trace_probe", "args": {"abi": "static_filter"}}  // the engine's ~0 mask instead of the bullet's
```

### What the orchestrator must watch for

The live run is the orchestrator's (with the operator). Three things decide
whether the answer means anything:

1. **The engine's answer must have been read.** For every ray:
   `engine.answer_missing == false` and `difference.engine_answer_usable == true`.
   If either is false, the verdict is `unknown` and **nothing can be concluded** —
   the instrument is broken (not the game). The same goes for
   `engine.faulted == true`.
2. **A verdict of `agrees` with an empty engine list must be impossible** — and it
   is: the difference is computed over the engine's intervals, and when they were
   dropped the verdict is `unknown`. If a live run ever shows `agrees` with
   `engine.interval_count == 0` **and** `ours` non-empty, that is a bug in the
   probe, not a result: report it.
3. **The ray must actually cross the wall.** `engine.interval_count > 0` and at
   least one `enter_exit == 1` record is the minimum; if the engine reports
   nothing, the ray missed the wall (aim elsewhere) and the measurement says
   nothing about the geometry.

Then the **numbered answer** (§"the measurement" above): which of the six places
the world geometry is lost.

### The measurement — **OWED** (not yet run)

The live measurement needs the **operator**: stop the bots, point at a wall that is
100 % solid, and report the view angles. The probe is built and the engine call's
form is verified against the running game (the offsets, the interface pointer, the
filter's mask, the entry RVA and the **results container's layout** —
`docs/re/bullet-trace.md` §"The probe's call form"), but the probe has **not** been
run inside a live game yet: the game on this machine was already running a payload,
and the rule is not to inject into it.

So the **numbered answer** (which of the six places the world geometry is lost) is
still open. The measurement settles it, and this is how to read the result:

| the probe says | the world geometry is lost at |
|---|---|
| `engine.answer_missing` is true (or the verdict is `unknown`) | **the instrument is broken** — the engine's answer was dropped (the call faulted, or the container did not hold the buffer the probe handed it). **Nothing can be concluded**; re-run. A `faulted: true` means the instrument, not the game. |
| `engine.faulted` is true | the engine call faulted (the guard caught it) — the instrument is broken, not the game |
| the engine's intervals are empty **and** ours is empty | the ray misses everything — **not** a geometry loss (aim elsewhere) |
| `engine_only` (the engine found spans, we found nothing for them) | **3** (the shapes are parsed but dropped: the cluster cap, the compound skip, the BVH's broad phase, the per-query selection) or **5** (projected, but the ray does not hit them) or **6** (the engine sees something we have no shape for at all) — the `difference.engine_only` entries say *which* span, and the ray's `ours` list says what we had |
| `ours_only` (we found solids the engine did not report) | the **over-inclusion** the enumeration is known for (a body-box overlap is a superset) — not a loss |
| `both` | the two answers differ in both directions — the over-inclusion **and** a loss. The `engine_only` list says whether a loss is in there. |
| `agrees` | the two sets are the same for this ray: the world geometry is **not** lost here (the loss, if any, is elsewhere — e.g. the ray does not cross the wall) |

**`agrees` is only a real answer when the engine's answer was read.** The
difference is computed over the engine's intervals; when they were dropped the
verdict is `unknown` and the lists are empty — so "agrees with an empty engine
list" is **impossible** by construction. The check the operator can make in one
line: `rays[].engine.answer_missing == false` and
`rays[].difference.engine_answer_usable == true` for every ray.

The raw records are in the answer (`rays[].engine.intervals[].raw`), so the
**interval record's** remaining fields can be mapped from the probe's own output
without re-running the game.

### What the probe publishes about itself

| key | what it means |
|---|---|
| `engine.rays_ok` / `engine.rays_faulted` | how many rays the engine answered, and how many faulted |
| `engine.filter_mask` / `filter_group` / `filter_flags` | the filter **actually** passed (in the `static_filter` mode: the engine's static one, not the bullet's) |
| `engine.occluder_cap` / `occluders_truncated` | how many of our solids the probe projects at most, and whether the enumeration produced more |
| `engine.world_cached` | whether the static world body was already parsed. **`false` means this run paid the world's parse** (10k brush shapes) inside the input hook — it happens **once** per session, so run the heatmap once first and the probe is cheap |
| `engine.target_pad` | the pad the probe's target box gets, in world units. It is **not** cosmetic: the pad decides the pyramid's face planes, i.e. **which solids the enumeration keeps** — it is part of what "our answer" contains |
| `game_thread` (top level) | `answered` / `busy` (the game thread is running but did not answer in time) / `stalled` (it is not running at all — the CreateMove hook is not installed) |
| `depth` | the ray's length: the weapon's own `m_flRange`, or **8192 units** (the engine's maximum trace length) when the weapon's range cannot be read |

### The probe's bounds (it must not be able to stall the game)

* **8 rays** per command (`count`), **64 intervals** published per ray;
* the results buffer is **fixed** (256 records + 64 records of slack) and the
  engine is given the **external-buffer** bit, so it never allocates;
* the projections are capped at **256** solids;
* the enumeration runs **once per command**, never per tick;
* the **worst case** is the world's first parse (10k brush shapes), which happens
  **once per session** and is reported as `engine.world_cached == false`. Run the
  heatmap once before the probe and the probe's answer is cheap.

### Process notes (recorded here, because they were recorded nowhere)

* **Ownership.** The T39 spec's file list does not include the root
  `meson.build` or a new `payload/control/include/cs2/control/trace_guard.hpp`,
  and the branch's earlier commits touched both: the `meson.build` edit is two
  additive `executable()` + `test()` blocks (the host tests have to be registered
  somewhere), and the guard header is the probe's own fault guard. The deviation
  is stated in the fix commits rather than left implicit.
* **A deletion in the shared temp directory.** The branch's earlier report
  mentioned deleting **~3 GB** from the shared temp directory
  (`%LOCALAPPDATA%\Temp\opencode`). The deletion is not in the branch, so it
  cannot be reviewed from here; it is recorded so the next agent knows the
  directory was touched. The REA cache (8 entries) and the Ghidra projects under
  `C:\Users\gpmpo\ghidra-analysis` were intact when the review checked them.
  **The shared temp directory is shared**: a deletion there is a process risk and
  belongs in the report, not only in a scratch file.
* **The entry point is not a curated signature** (`trace_line_intervals` is absent
  from `payload/sdk/offsets.hpp`): the probe uses the hard-coded `0xA19E90`
  fallback, and the dumper's curated set is not this task's file — the follow-up
  is described in `docs/re/bullet-trace.md`.
