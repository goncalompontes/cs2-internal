# `client.dll` — the bullet trace path (build `659940c1`)

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.
Build: `client.dll` sha256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`
(the live build). Reversed with the `rea` plugin (Ghidra 12.1.4) against the persistent
project seeded from `C:\Users\gpmpo\ghidra-analysis`.

> Companion to `docs/re/aim.md` (the shot model / spread cone), `docs/re/occlusion.md`
> (the world-solids enumeration) and `docs/ragebot.md` (ADR-0022). This file records
> the **engine's own bullet trace**: which query function a shot uses, its collision
> mask, its filter, and how the result is turned into penetration. The occlusion
> layer must reproduce this solid set (NEXT_SESSION_OCCLUSION.md §3).

## TL;DR

- A shot is **`FX_FireBullets` = `FUN_180d237d0`** (RVA `0xd237d0`). It reads the
  shooter's `CCSWeaponBaseVData` (`+0x820` `m_nDamage`, `+0x830` `m_flRange`,
  `+0x82c` `m_flPenetration`, `+0x834` `m_flRangeModifier`) and loops over the
  weapon's pellet count (`*(VData + 0x730)`), calling `FUN_180887d60` per pellet.
- The per-pellet function **`FUN_180887d60`** (RVA `0x887d60`) builds a
  `CTraceFilter` with **collision mask `0x1c300b`** and group `3`, then traces.
- The trace itself is **`TraceLineIntervals`** = `FUN_180a19e90` (not
  `TraceShape`), called as
  `TraceLineIntervals(*(client+0x2227058), &results, start, end, filter+8, -1.0f)`;
  its results are `0x38`-byte interval records in a `CUtlVector`.
- Penetration is a **loop** of `TraceLineIntervals` in `FUN_18089ca80` (RVA
  `0x89ca80`): it accumulates the crossed thickness and stops at ≈ 90 units (or 6
  iterations), then resolves the final hit with a second query (`FUN_180200f20`).

## The call graph

```
FX_FireBullets                     FUN_180d237d0   (RVA 0xd237d0)
 ├─ CCSWeaponBaseVData reads: +0x820 +0x830 +0x82c +0x834
 └─ for each pellet (i < *(VData+0x730)):
      FUN_180887d60 (per pellet)    (RVA 0x887d60)
       ├─ FUN_180352920 (filter init: vtable client+0x1ad4838, mask 0x1c300b, group 3)
       ├─ FUN_1808830e0 (the first trace)      (RVA 0x8830e0)
       │    └─ TraceLineIntervals FUN_180a19e90(iface, results, start, end, filter+8, -1.0f)
       │    └─ FUN_180886090 (consume the interval records -> impacts)
       └─ FUN_18089ca80 (penetration loop)     (RVA 0x89ca80)
            └─ TraceLineIntervals ... (repeated)
            └─ FUN_180200f20 (final hit query)
```

### `FX_FireBullets` — `FUN_180d237d0`

Decompilation-verified (the three `DevMsg` strings
`"FX_FireBullets: GetItemDefinition failed"`, `…GetWeaponEconDataFromItem failed…`,
`…GetCSWeaponDataFromItem failed…` are referenced inside it at VAs `0x181ca96b8`,
`0x181ca96f0`, `0x181ca9730`). Key reads:

```c
local_518 = *(undefined4 *)(lVar10 + 0x820);   // m_nDamage
uVar1     = *(undefined4 *)(lVar10 + 0x830);   // m_flRange
uVar2     = *(undefined4 *)(lVar10 + 0x82c);   // m_flPenetration
uVar3     = *(undefined4 *)(lVar10 + 0x834);   // m_flRangeModifier
...
for (uVar17 = 0; (int)uVar16 < *(int *)(lVar10 + 0x730); ++uVar17) {   // pellet count
    FUN_180887d60(plVar9[0x2ae], &origin, &dir, uVar1, uVar2, 4, uVar4, local_518,
                  ..., plVar9, local_408, ..., lVar10, ..., param_6, 0, param_20);
}
```

(`lVar10` is the `CCSWeaponBaseVData` — `docs/re/aim.md`; `+0x730` is the pellet
count / `m_nNumBullets`-like field.)

### The trace filter — `FUN_180352920`

`FUN_180352920(filter, entity, param_3 /*mask*/, param_4 /*group*/, param_5 /*flags*/)`
is the filter constructor. It writes:

| filter field | value |
|---|---|
| `+0x00` | vtable `PTR_FUN_181ad4838` (`client + 0x1ad4838`) |
| `+0x08` (qword) | `param_3` = the **collision mask** — the shot passes `0x1c300b` |
| `+0x34` | `0xf00ffff` |
| `+0x37` (byte) | `param_5` (flags) |
| `+0x38` (byte) | `param_4` = `3` (collision group) |
| `+0x39` (byte) | `(old & 0xc9) \| 0x49` |
| `+0x20`,`+0x28` | `-1` (skip-entity handles) |
| `+0x20` (dword `[4]`) | the entity handle (`FUN_1809dd600(entity)`) |

So the **bullet collision mask is `0x1c300b`** — this is what the occlusion query
must use (the payload previously passed `0x1c1003`).

### `TraceLineIntervals` — `FUN_180a19e90` (RVA `0xa19e90`)

Name string `0x181c32460`; source
`C:\buildworker\csgo_rel_win64\build\src\game\shared\gamephysicsquery.cpp` line `0x81a`.
Decompiled:

```c
void FUN_180a19e90(undefined8 *iface, int *results, undefined8 start, undefined8 end,
                   byte *mask, undefined4 max_fraction /* -1.0f */)
{
  plVar1 = (longlong *)*iface;                     // the physics world
  (*(*plVar1 + 0x520))(plVar1, results, FUN_1801ca290(&local, start), end, mask,
                       flags, max_fraction);       // world query vtable +0x520
  if ((*mask >> 1 & 1) != 0) {                     // a second query kind
    if (FUN_1801c1a50(PTR_PTR_18221e710, results, start, end, mask, 0, max_fraction))
      FUN_1809bffc0(*(longlong *)(results + 2), *results * 0x38 + *(longlong *)(results + 2),
                    *results, ...);                // sort the 0x38-byte records
  }
}
```

- **Result buffer**: a `CUtlVector`-like header — count `@+0x00`, data pointer `@+0x08`
  (`*(longlong*)(results+2)` = `results + 8`). **Record stride `0x38`** (56 bytes).
- **`mask`** is a *pointer* (`filter + 8`), i.e. the qword holding the collision mask
  (`0x1c300b`); the low byte's bit 1 selects the second query.
- **`max_fraction`** = `0xbf800000` = `-1.0f` from every live caller.
- The world query is the physics-world vtable **`+0x520`** (the sibling `TraceShape`
  uses `+0x560` for the sorted branch, `+0x558` for the single-trace branch).

### The interval record (stride `0x38`) — consumer `FUN_180886090`

`FUN_180886090(results, param_2)` walks the `0x38`-stride records and reads, per
record: a float at **`+0x0C`** (the hit distance / fraction) and a byte at **`+0x32`**
(`1` = the ray is *entering* a solid — it pairs consecutive enter/exit records into
impact spans). It re-orders the record array so an entry/exit pair is contiguous. It
does **not** surface the hit shape pointer in this path, so the interval trace alone
gives the *depth stack* (entry/exit distances) — not the geometry.

### Penetration loop — `FUN_18089ca80` (RVA `0x89ca80`)

Decompiled: it loops, accumulating the total crossed thickness `fVar11`
(`fVar11 * fVar8`), stopping when `90.0 <= total` (or after 4 retries), each pass:

```c
FUN_180a19e90(PTR_DAT_182227058, param_1, param_3, param_2, param_5 + 8, 0xbf800000);
param_1[0x744] = 1.0f;
FUN_180886090(param_1, param_2);
...
FUN_180200f20(PTR_DAT_182227058, &local_198, &local_1a8, param_5, local_178);
```

`FUN_180200f20(iface, start, end, filter, out)` is the final-hit query (a
`TraceShape`-family call through the interface). The `+0x744` float is the
"fraction reached" (1.0 = nothing crossed).

## The query-method entries (name-string xrefs, build `659940c1`)

| Method | name string | entry (VA) |
|---|---|---|
| `TraceShape` | `0x181c321a8` | `0x180a1a000` |
| `TraceShapeWithSortedResults` | `0x181c32410` | `0x180a1af60` |
| `TraceLineIntervals` | `0x181c32460` | `0x180a19e90` |
| `TraceShapeToEntity` | `0x181c32208` | `0x180a1aa70` |
| `EntitiesInBox` | `0x181c32248` | `0x1809dbd20` |
| `EntitiesAlongLinearSweep` | `0x181c32320` | `0x1809db9a0` |

The interface singleton global is `PTR_DAT_182227058` = `client + 0x2227058`
(`docs/re/occlusion.md`). All query methods take it as arg1.

## What this means for the occlusion enumeration (the fix)

The engine's bullet traces a **segment** with `TraceLineIntervals` and takes the
distance-sorted layer stack — it never enumerates whole bodies. Our Tier-2 layer
instead reads the world body's collision shapes and projects them. To make the two
**identical**, the enumeration must read **every shape type the engine traces**.
A live probe of the worldspawn body (`docs/re/occlusion.md` §"the record → shape
chain") shows its shape histogram is **`{type 2 (hull): 10143, type 3 (mesh): 6}`** —
and the map's **large/merged wall surfaces are the mesh shapes**.

> **Live finding (2026-10-18).** Standing in de_dust2 with a wall between the eye and
> three enemies, the payload reported *no* occluder on the eye→head rays, yet the
> wall is clearly visible. Ray-testing the world body's six mesh shapes directly
> (Möller–Trumbore over their `+0x50` triangle indices) hits them at 94.8 / 323.2 /
> 267.6 units — i.e. the wall is a **mesh**, and the payload **skipped every mesh**
> (the old reader capped meshes at 160 vertices and built an O(n⁴) convex hull, so all
> six — 689…101097 vertices — were dropped).

**Fix (implemented):** the payload now reads type-3 meshes as a **triangle soup**
(`RnMesh_t` vertices `+0x38`/count `+0x30`; triangle vertex indices `+0x50`, 3 int32
per triangle; verified live) and the aim side projects them with
`aim::project_mesh_occluder` (silhouette = convex hull of the projected vertices, used
only for the broad phase) and computes the exact depth/thickness with a
**ray/triangle intersection** (`OccluderCell::ray_span` for `SolidKind::mesh`). The
bullet collision mask was also corrected to the engine's **`0x1c300b`**.

`RnMesh_t` layout used (live-verified): vertex count `+0x30`, vertices `+0x38`
(stride `0xC`); triangle count `+0x18`; **triangle vertex indices `+0x50`** (3 ×
`int32` per triangle).

## Still open

1. **Compound (4) shapes** — still skipped (`docs/re/occlusion.md` "Still to pin" 3).
2. **The interval record's full layout** — only the distance `+0x0C` and the
   enter/exit byte `+0x32` are pinned; the surface/material field is not, so a
   trace-based *oracle* (fire the engine's own `TraceLineIntervals` and diff the
   solids) still needs that record mapped end-to-end.
3. **Surface / penetration modifiers** — `OccluderCell::surface_modifier` is still
   `1.0` (`docs/re/occlusion.md` "Still to pin" 5).
4. The map's ~10k-triangle mesh is intersected per fragment; a mesh BVH (`RnNode_t`)
   is the performance follow-up.
