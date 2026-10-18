# `vphysics2.dll` (build 25815307)

Image base `0x180000000`. Addresses below are **absolute VAs**; RVAs are
`VA - 0x180000000`.

> Target: local copy `C:\Users\gpmpo\ghidra-analysis\vphysics2.dll`
> (sha256 `1d7680455eb1ca3ad7c9b92165515351f607e96db66cf466140aaf3f00946dba`),
> copied from the Steam depot
> `…\game\bin\win64\vphysics2.dll`. Analyzed with Ghidra 12.1.4 via REA
> (`docs/re/README.md` §1). 7,658 procedures, 4,045 strings.

## Why this file exists

The ragebot design (`docs/ragebot.md`) wants the **collision geometry** (the
convex hulls / meshes) of the world in the aim cone, so it can build an
occlusion + penetration *heat map* **without per-ray traces** ("Tier 2"). This
file records the RE that establishes the geometry is present, traversable, and
readable from the client.

## TL;DR / decision

`vphysics2` carries a complete, traversable collision-geometry model — the
**Rubikon "Rn"** API — plus a game-facing shape layer:

- **Hulls** are convex **half-edge** structures: `RnHull_t` / `RnVertex_t` /
  `RnHalfEdge_t` / `RnFace_t` / `RnPlane_t`.
- **Meshes** are triangle soups (`RnMesh_t`), **compounds** are multi-hull
  (`RnCompound_t`).
- A game-facing shape hierarchy (`IPhysicsShape` → `IFnPhysicsShape`, concrete
  impls `CRnHullShape` / `CFnPhysicsShapeBaseImpl<…>`) exposes shapes; each
  concrete shape stores a **type tag** and a **pointer to its geometry**.

So reading collision geometry in-process is **feasible**. The work is a bounded
vtable/field mapping (below), not a research dead end.

## The Rn geometry model

| Type (string) | Meaning |
|---|---|
| `RnHull_t`, `RnVertex_t`, `RnHalfEdge_t`, `RnFace_t`, `RnPlane_t`, `RnTriangle_t`, `RnWing_t` | convex half-edge hull |
| `RnMesh_t`, `RnNode_t` | triangle mesh (+ BVH node) |
| `RnCompound_t`, `RnCompoundTree_t`, `RnCompoundTreeNode_t` | multi-shape compound |
| `RnSphere_t`, `RnCapsule_t` | primitives |
| `RnSphereDesc_t`, `RnCapsuleDesc_t`, `RnHullDesc_t`, `RnMeshDesc_t`, `RnCompoundDesc_t`, `RnShapeDesc_t`, `RnBodyDesc_t` | descriptors |
| `VPhysics2ShapeDef_t` | the cross-interface shape definition |

Named free functions (Ghidra recovered these names):

| Function | VA | RVA |
|---|---|---|
| `RnHullCreate` | `0x1801ac5c0` | `0x1ac5c0` |
| `RnHullCreateBox` / `Cone` / `Cylinder` | `0x1801ac890` / `0x1801acfd0` / `0x1801ad370` | `0x1ac890` / `0x1acfd0` / `0x1ad370` |
| `RnHullCreateRuntime` / `RnHullCreateRuntimeFromPlanes` | `0x1801ad740` / `0x1801ad7b0` | `0x1ad740` / `0x1ad7b0` |
| `RnHullClone` / `RnHullDestroy` | `0x1801ac5b0` / `0x1801ad860` | `0x1ac5b0` / `0x1ad860` |
| `RnMeshCreate` / `RnMeshClone` / `RnMeshDestroy` | `0x1801ad8a0` / `0x1801ad890` / `0x1801ae9a0` | `0x1ad8a0` / `0x1ad890` / `0x1ae9a0` |
| `RnMeshHideTriangles` / `RnMeshUpdateChangedVertices` / `RnMeshUpdateTriangleAABBs` | `0x1801ae9d0` / `0x1801af6f0` / `0x1801af9b0` | `0x1ae9d0` / `0x1af6f0` / `0x1af9b0` |
| `RnCompoundClone` / `RnCompoundDestroy` | `0x1801ac3e0` / `0x1801ac580` | `0x1ac3e0` / `0x1ac580` |

The geometry is also **dumpable**: a hull summariser prints
`"Source Hull F=%d V=%d hE=%d"` (`0x1803f1670`, in `FUN_1803b35b0`) and
`CHullIVR::SaveObj` writes `hullIvr-N.obj` (`0x1803f1520`, in `FUN_18039ea60`),
iterating faces/vertices/half-edges. So the topology is concrete and enumerable.

## The game-facing shape layer

The shape **base constructor** `FUN_180254ac0(shape, type)` (VA `0x180254ac0`)
stores the shape **type** at `shape + 0x18` and installs the base vtable
`0x1803e1268`. Each concrete ctor overrides the vtable:

| type | shape | ctor | vtable | geometry ptr |
|---|---|---|---|---|
| 0 | sphere | `FUN_18024b920` | `0x1803e0f18` | — (radius at `+0xBC..`) |
| 1 | capsule | `FUN_18024d610` | `0x1803e1030` | — |
| **2** | **hull** | `FUN_180250030` | `0x1803e1140` | `*(RnHull_t**)(shape+0xC0)` |
| 3 | mesh | `FUN_1802416f0` | `0x1803e0d60` | `*(RnMesh_t**)(shape+0xC8)` |
| 4 | compound | `FUN_18025a360` | `0x1803e1398` | `shape+0xC0` (+ array) |

Common fields: the shape **type** at `shape + 0x18`; the uniform **scale** is stored
at a **per-type** offset (hull `+0xB8`, sphere `+0xD8`, capsule `+0xF0`; the mesh
uses a 3-vector at `+0xB8/+0xBC/+0xC0`). The type is also a schema field
(`PhysGenericShapeType_t`, `m_nShapeType`, `GENERIC_SHAPE_HULL`).

### Shape geometry offsets (resolved 2026-10-17)

From the shape constructors and the vtables' own point tests on this build
(`vphysics2.dll` `1d768045…`). The constructors **pre-multiply** the geometry by the
shape's own scale, so the stored centre / endpoints / radius are already scaled — the
payload reads them directly (and applies only the owning body's rigid transform):

| type | shape | geometry |
|---|---|---|
| 0 | sphere | centre `(Vec3)` at `+0xB8`, radius at `+0xC4` — ctor `FUN_18024b920`, point test `FUN_18024ca60` |
| 1 | capsule | endpoint A at `+0xB8`, endpoint B at `+0xC4` (each a `Vec3`), radius at `+0xD0` — ctor `FUN_18024d610`, point test `FUN_18024faa0` |
| 2 | hull | `RnHull_t*` at `+0xC0`; scale at `+0xB8` |
| 3 | mesh | `RnMesh_t*` at `+0xC8`, scale (`Vec3`) at `+0xB8`; in `RnMesh_t`: vertex count `+0x30` / vertices `+0x38` (stride `0xC`), triangle count `+0x18` / array `+0x20` (stride `0x20`), index count `+0x48` / array `+0x50` (from `RnMeshClone`/`RnMeshUpdateChangedVertices`) |
| 4 | compound | `RnCompound_t*` (0x160 B) at `+0xC0`: a **pool of typed geometry stored by value** — `RnMesh_t[]` (stride `0xC0`) at `+0x28`/`+0x30`, **`RnHull_t[]` (stride `0xF8`)** at `+0xF0`/`+0xF8`, and groups at `+0x100`, `+0x110` (stride `0x10` → sphere: `Vec3`+radius) and `+0x120` (stride `0x1C` → capsule: two `Vec3` + radius). The compound *shape* carries a 0x30-byte **instance-record** vector at `+0xD0` (count) / `+0xD8` (POD block) that selects pool entries: each record is `{6 floats; u32 kind|index; u32 index; 4 floats}`. The strides match the consumers (`FUN_180252670` reads `+0xA8` of a hull entry = `RnHull_t`; `RnMeshDestroy` frees `0xC0`). **Unresolved:** the instance record's transform — enumerating the pool assumes shape-local geometry |

The payload consumes sphere (0), capsule (1) and **mesh (3, as the convex hull of the
mesh's scaled, transformed vertices — `aim::convex_hull_3d`)** into the occlusion chart
(`payload/control/src/occlusion_query.cpp` → `OcclusionHull` →
`aim::project_*_occluder`). **Compounds (4)** are still skipped: the pool geometry is
readable but the 0x30-byte instance record's transform is not pinned, and enumerating
the pool without it assumes the sub-shapes are shape-local. A live probe
(`walls.compound` — the first type-4 shape's group counts) is emitted so a session can
finish it.

Every hull vtable method reads the geometry the same way, e.g.
`FUN_180250100` (VA `0x250100`) / `FUN_180250460` (`0x250460`):

```c
lVar2 = *(longlong *)(shape + 0xC0);   // RnHull_t*
fVar3 = *(float   *)(shape + 0xB8);    // scale
// AABB: min @ hull+0x14 (x,y,z @ +0x14/+0x18/+0x1c), max @ hull+0x20 (+0x20/+0x24/+0x28)
```

`FUN_180251800` (VA `0x251800`) transforms hull vertices by the shape's matrix —
a ready-made traversal showing the vertex array.

## `RnHull_t` layout (size `0xF8` = 248 bytes)

**Resolved** from the builder `FUN_1801a7310` (`0x1a7310`, which fills the
struct), cross-checked against `RnHullDestroy` (frees at size `0xf8`),
`RnHullClone` (`0x1801ac5b0`), and the readers `FUN_180251800` (vertex transform),
`FUN_180250100`/`FUN_180250460` (AABB), `FUN_1801302f0` (plane support),
`FUN_180292700` (face volumes).

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | `Vector` | centroid (mean of vertices) |
| `+0x0C` | `float` | radius (max vertex distance from centroid) |
| `+0x10` | `float` | min plane distance (support bound) |
| `+0x14` | `Vector` | AABB min (x,y,z) |
| `+0x20` | `Vector` | AABB max (x,y,z) |
| `+0x2C`/`+0x30`/`+0x34` | `float×3` | `0.25` (tolerance vector) |
| `+0x70` | `int` | **vertex count** |
| `+0x78` | `Vector*` | **vertices**, stride `0xC` (12) |
| `+0x88` | `int` | **plane count** (= face count) |
| `+0x90` | `RnPlane_t*` | **planes**, stride `0x10` (`normal.xyz` + `dist`) |
| `+0xB0` | `int` | per-vertex array count |
| `+0xB8` | `uint8*` | **vertex → half-edge** (1 B/vertex) |
| `+0xC8` | `int` | **half-edge count** |
| `+0xD0` | `uint8*` | **half-edges**, 4 B each: `{next, twin, originVertex, face}` |
| `+0xE0` | `int` | **face count** |
| `+0xE8` | `uint8*` | **face → a half-edge in its loop** (1 B/face) |
| `+0xA8` | ptr | runtime sub-struct, `0x30` B (lazy cache; not needed for projection) |

This is a compact **half-edge ("winged")** hull: `half-edge[2]` = origin vertex
index, `half-edge[0]` = next (so a face's vertex loop is walkable), and
`plane[face]` gives the face normal. Vertices were confirmed three ways
(`RnHullClone`, `FUN_180251800`, the builder's fill loop).

For the projection, a **silhouette edge** is a half-edge whose two adjacent faces
have opposite `sign(dot(plane_normal, view_dir))` — directly readable from
`+0x90` (planes) + `+0xD0` (half-edges).

## Getting the shapes in the aim cone

Two options:

1. **Client query (already mapped):** `CGamePhysicsQueryInterface::EntitiesInShape`
   / `EntitiesAlongLinearSweep` + `FilterOverlapResults` return arrays of
   **`0x70`-byte overlap records** carrying entity handle, a **physics-object
   pointer, a transform/shape blob and a material token**
   (`ConvertOverlapMultiple` `0x1809d1f00` / `ConvertHitboxOverlapMultiple`
   `0x1809d1d80` in client.dll). Each object's type tag selects hull/mesh.
2. **World enumeration:** `VPhysics2_Interface_001` (`0x1803fcb30`) =
   `CVPhysics2Interface` (`…CreateDebugSceneObject`), world → bodies → shapes.
   The world query also takes a `std::function<bool(IPhysicsShape*)>`
   (`CRnWorld::Query`), i.e. a shape visitor.

## Materials (penetration modifiers)

Surface materials are here: `CPhysSurfaceProperties` (+ `…Physics`, `…Vehicle`,
`…Audio`, `…SoundNames`), `CVPhysXSurfacePropertiesList`, `m_surfacePropertyHashes`,
`SurfacePropertiesList`, and `scripts/surfaceproperties_game.txt`. These carry
the penetration/damage modifiers the heat map needs.

## Recipe (for the payload, once signatures exist)

```
cone query  -> shape*                     // client CGamePhysicsQueryInterface
type = *(int*)(shape + 0x18)
if type == 2:  hull = *(RnHull_t**)(shape + 0xC0);  scale = *(float*)(shape+0xB8)
                verts = *(Vector**)(hull + 0x78),  n = *(int*)(hull + 0x70)      // stride 12
                planes = *(RnPlane_t**)(hull + 0x90), nf = *(int*)(hull + 0x88)  // stride 16
                edges = *(uint8**)(hull + 0xD0)                                  // 4 B each
if type == 3:  mesh = *(RnMesh_t**)(shape + 0xC8)   // triangle soup
if type == 4:  compound -> recurse sub-shapes
```

## Gotchas

- **Build fragility.** The read path is vtable/field offsets
  (`shape+0x18`, `shape+0xC0`/`+0xC8`, `shape+0xB8`, `hull+0x70/+0x78`, …).
  These move per build → signature/scan layer mandatory (ADR-0012).
- **PDB not applied** (`vphysics2.pdb`, GUID `ecaa296d-8dd4-431d-aa7d-a3f0854c1bb5`).
  Names are partial; an empty name search does **not** prove absence.
- **Thread safety.** Snapshot geometry (the physics world ticks).
- **Collision ≠ render.** Use the collision hull/mesh and the *surface
  properties*, not render meshes.

## Open items (to finalize the reader)

1. ~~Resolve the `+0x88`/`+0x90` array and the `+0xA8` runtime sub-struct into
   planes / faces / half-edges.~~ **Done** — see the `RnHull_t` layout above
   (planes `+0x88/+0x90`, half-edges `+0xC8/+0xD0`, face→half-edge `+0xE0/+0xE8`,
   vertex→half-edge `+0xB0/+0xB8`).
2. ~~Map the overlap record → shape pointer.~~ **Done** (2026-10-17): the record's
   `+0x58` is a `CPhysicsBody`; see `docs/re/occlusion.md` §"the record → shape chain".
3. ~~Resolve the sphere/capsule/mesh geometry.~~ **Done** (2026-10-17): the shape
   geometry offsets in §"Shape geometry offsets"; the payload reads types 0/1/3.
   **Compound (4) — implemented (2026-10-18).** `RnCompound_t` (0x160 B, at
   `shape+0xC0`; `RnCompoundClone`/`RnCompoundDestroy`) is the geometry pool: the
   `RnHull_t[]` children at `+0xF0` (count)/`+0xF8` (block), stride **`0xF8`**, and the
   `RnMesh_t[]` at `+0x28`/`+0x30`, stride **`0xC0`** (`RnCompoundClone` copies both;
   `RnCompoundDestroy` frees `0x160`). The payload now recurses into both by value
   (`occlusion_query.cpp` `case 4`), transforming the children by the owning body's
   transform, with every count/pointer bounds-checked. **Finding:** CS2 gameplay props
   are multi-hull **bodies** (many `RnHull_t` shapes in one `CPhysicsBody`), *not*
   compound shapes — the world body is `{hull 10143, mesh 6}`, spawned `prop_physics`
   and dropped weapons are all hulls, and no live type-4 shape was found — so this path
   is implemented but **not live-validated** (it contributes nothing when absent). The
   remaining ambiguity (the shape's separate 0x30-byte child table at `shape+0xD0`, and
   per-child transforms) needs a live compound to settle.
4. ~~Map surface-property lookup → penetration modifier~~ **Resolved (2026-10-18).** The
   per-shape surface is `shape+0x48` → the material object → `+0x10` index →
   `SurfacePropertiesList` (`CGameSurfaceProperties`: distance modifier `+0x08`,
   damage modifier `+0x0C`, `gamematerial` `+0x14`). See `docs/re/penetration.md`.
5. Cut signatures for `shape+0x18` / `+0xC0` / `+0xC8` and the `RnHull_t`/`RnMesh_t`
   offsets (ADR-0012).

## Target inventory

| File | Path | sha256 (build 25815307) |
|---|---|---|
| `vphysics2.dll` | `C:\Users\gpmpo\ghidra-analysis\vphysics2.dll` | `1d7680455eb1ca3ad7c9b92165515351f607e96db66cf466140aaf3f00946dba` |

Neighbours that matter for the same pipeline: `physicsbuilder.dll` (builds
collision from models), `embree3.dll` (BVH raytracer), `meshsystem.dll` /
`worldrenderer.dll` (render geometry — *not* collision).
