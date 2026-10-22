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
  `0x89ca80`): it accumulates crossed thickness, stops at ≈ 90 units, and has a
  separate `iVar6 > 4` retry guard. The actual shot passes a 4-span limit to this
  helper; it then resolves the final hit with `FUN_180200f20`.

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
  (`*(longlong*)(results+2)` = `results + 8`), capacity `@+0x10`, flags `@+0x14`
  (the live build's two writers and the external-buffer bit are in §"The probe's
  call form" below). **Record stride `0x38`** (56 bytes).
- **`mask`** is a *pointer* (`filter + 8`), i.e. the qword holding the collision mask
  (`0x1c300b`); the low byte's bit 1 selects the second query. The engine's own
  **first** trace **clears** that bit and restores it afterwards
  (`FUN_1808830e0`: `and rax, 0xfffffffffffffffd; mov qword ptr [r14], rax`), so
  that trace is answered by the world query alone; the penetration loop's trace
  leaves it set and gets both query kinds. The probe passes the mask as it is —
  the bullet's `0x1c300b`, bit 1 set — so it asks the engine the **bullet's**
  question; both writers honour the external-buffer bit, so neither allocates.
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

## Current-build call-path cross-check (REA, 2026-10-19)

Rechecked with Ghidra 12.1.4 on the Steam `client.dll` whose SHA-256 is
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`:

- The observed shot chain is `FX_FireBullets` (`FUN_180d237d0`) → per-pellet
  `FUN_180887d60` → `FUN_1808830e0` → `FUN_18089ca80` →
  `TraceLineIntervals` (`FUN_180a19e90`). `FUN_1808830e0` calls the interval trace
  again after the penetration advance, and `FUN_18089ca80` contains the repeated
  advance loop plus the final `FUN_180200f20` query. `FUN_180885710`, called by the
  same per-pellet function, walks the modulation entries and calls
  `HandleBulletPenetration` (`FUN_1808a1820`).
- The `TraceShapeWithSortedResults` entry (`FUN_180a1af60`, world vtable `+0x560`)
  is a distinct API. It gathers/sorts `0x48`-byte hit records and converts them to
  `0xc0`-byte trace records via `FUN_1809d2f60`. Its complete *resolved direct*
  caller set is `0x18089fb40`, `0x180a1a510`, and `0x180b93860`; the verified
  per-pellet path above does not call it. This is consistent with the community
  `CreateTrace` releases using a sorted-result wrapper, but does not establish that
  wrapper as the game's `FX_FireBullets` implementation.
- In the direct shot caller, `FUN_180d237d0` places literal `4` at the sixth
  argument to `FUN_180887d60`. That value reaches `FUN_18089ca80` as its span-count
  limit; the decompile increments `local_res8` for counted solid spans and stops
  when `param_6 < local_res8`. The same helper has a separate `iVar6 > 4` retry
  guard and the accumulated `90.0`-unit thickness guard. Do not infer the game's
  actual shot budget from the fifth argument of a community `CreateTrace` helper.

This resolves the **call-path dispute**, not the complete trace oracle. The full
`0x38` interval-record surface mapping, exact mesh/open-soup behavior, and runtime
damage differential are still unverified. Ghidra pseudocode is provider output, not
original source; the caller set above covers resolved direct references only.

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

## The bullet filter & the mask the world query actually reads (RE, 2026-10-19)

Re-derived from the live build (`client.dll` sha `659940c1…`) with a local capstone
PE pass (`artifacts/re_filter.py`, output `artifacts/re_filter_out.txt`).

**`CTraceFilter::init` = `FUN_180352920`** (the bullet's filter constructor) writes,
for a filter at `rcx` and the shot's `(entity, mask, group, flags)`:

| field | value | note |
|---|---|---|
| `+0x00` | vtable `client+0x1ad4838` (`lea rax,[rip+0x1781ee7]`) | the *same* vtable as the static default |
| `+0x08` (qword) | `param_3` = **`0x1c300b`** | the collision mask |
| `+0x10` | 0 | |
| `+0x18` | 0 | |
| `+0x20` (dword) | `FUN_1809dd600(entity)` | the shooter's entity handle |
| `+0x20`/`+0x28` | `-1` before that | skip handles |
| `+0x30` (word) | `FUN_1809ea740(entity)` | a collision-group word |
| `+0x34` (dword) | `0xf00ffff` | bits `+0x34..0x36` + the `+0x37` flags seed |
| `+0x37` (byte) | `param_5` (flags) | the query-selection flags |
| `+0x38` (byte) | `param_4` = `3` | collision group |
| `+0x39` (byte) | `(old & 0xc9) \| 0x49` | |
| `+0x40` (byte) | 0 | |

**The mask the query reads is `*(qword*)(filter + 8)`, not the `contents`
argument.** `EntitiesInBox` = `FUN_1809dbd20`:

```
r14  = filter (arg4);  r12 = box_min;  r15 = box_max
if (filter[0x37] == 1) skip FUN_180b9e660(filter+8); else FUN_180b9e660(filter+8)
r13b = filter[0x37]
if ((r13b & 7) != 0) {                       // the primary box-overlap query
    r9 = filter + 8                          //  <-- the mask pointer
    edx = contents (stack arg)               //  the 'contents' argument, passed separately
    rcx = *(this)                            //  the physics world
    (*(world->vtbl + 0x5d0))(world, results, descriptor, ..., r9, edx, ...)
    ConvertOverlapMultiple(results, tmp)     // FUN_1809d2dc0 (0x70-byte records)
    FilterOverlapResults(this, results, filter)
}
if ((*(byte*)(filter+8) >> 1) & 1)  { ... FUN_1801e2160 ... ConvertHitboxOverlapMultiple }
if ((r13b & 8) != 0)                { ... }
```

So the same `filter+8` mask contract holds for `TraceLineIntervals` (`+0x520`),
`TraceShape` (`+0x558` single / `+0x560` sorted) and the box query (`+0x5d0`).

**The engine's static default filter object** (`client+0x2227070`, the object the
payload used to pass) is:

```
vtable = client+0x1ad4838   +0x08 = 0xffffffffffffffff (ALL contents!)
+0x20 = -1  +0x28 = -1      +0x34 = 0x00f00ffff
+0x37 = 0x0f (flags)        +0x38 = 3 (group)      +0x39 = 0
```

Its vtable slot `+8` (`ShouldHitEntity`) is `mov al,1; ret` (`client+0x22d900`), and
`FilterOverlapResults` (`FUN_1809df4e0`) short-circuits to "keep everything" when
`filter[0x40] == 0` — which the default is. **Consequence:** passing the static
default enumerates a *superset* of the bullet's set (mask `~0`, including
`CONTENTS_PLAYERCLIP` brushes a bullet flies through). The payload now passes a
**copy of that object with `+8 = 0x1c300b`, `+0x38 = 3`, `+0x37 = 0x0f`** — the
engine vtable is preserved by the copy, so the query is safe (a payload-built
vtable is not: the engine reads slots past `+8`). `Walls.filter_mask` /
`filter_custom` report it live.

**Why a wall/floor was still missing — the mesh cap (live, 2026-10-19).** The
worldspawn body's meshes on de_dust2 are (live `artifacts/floor_probe.py`):

| mesh | vertices | triangles | AABB (min → max) |
|---|---|---|---|
| 1 | 689 | 1221 | `[-2410,-1184,-130] → [1835,3429,808]` |
| 2 | 5944 | 8271 | `[-2312,-1200,-232] → [1868,3204,820]` |
| 3 | 7915 | 8831 | `[-2184,-157,-82] → [1720,3019,411]` |
| 4 | **101097** | **104429** | `[-4736,-2496,-320] → [3584,4608,964]` |
| 5 | 76 | 25 | |
| 6 | 218 | 175 | |

Mesh 4 spans the whole map footprint — and the reader's old **65536** vertex/triangle
cap silently dropped it, so every surface it carries (terrain/floor and large merged
walls) was absent from the occluders. Fixed: the cap is raised and a large mesh is
instead **split into spatial clusters** (`occlusion_query.cpp` `emit_mesh_clusters`,
≤2048 triangles each, each a bounded occluder with its own world AABB) so the floor
occludes where it is crossed and a map-wide mesh no longer becomes one chart-wide
silhouette. `walls.mesh_clusters` reports the count.

> Note the enumeration is still an `EntitiesInBox` over the eye→target AABB: it
> cannot *miss* a solid a shot crosses (the segment is inside the box), but it does
> not list world geometry merely *visible* around the target (e.g. the ground under
> the player). Making the list exactly the engine's trace set is the Tier-1
> `TraceLineIntervals` step (`docs/ragebot.md` §10).

## The engine call and the guard

Any engine call this work adds runs under `cs2::diag::call_guarded`
(`payload/diag`): the payload is manually mapped, so a fault inside an engine
call cannot be caught by the C++ machinery, and the guard is the tree's **one**
recovery mechanism — a vectored exception handler plus `setjmp`/`longjmp`. A
call that faults is logged and returns a failure; the process lives.

The guard's limits are its contract
(`payload/diag/include/cs2/diag/crash_handler.hpp`): one guarded call at a time
per process, the faulting thread only, a fixed set of recoverable exception
codes, and the guarded call must not own resources (the recovery is a
`longjmp`, so the frames it leaves are not cleaned up).

**There is one guard, not two.** The tree's test `diag-guard` reads the
payload's own sources and fails when a second recovery mechanism appears
outside `payload/diag/src/crash_handler.cpp`: a second vectored handler, a
second unhandled filter, a `setjmp`/`longjmp`, or an SEH `__try`/`__except`.
Two implementations is how the defect survived: an earlier probe branch
carried its own copy of the guard because it could not use the shared one —
and the shared one was the broken one.

**Which way the reconciliation goes — `cs2::diag` keeps the one mechanism.**
`cs2::diag` owns it; the trace probe's own guard goes away. The probe will
call `cs2::diag::call_guarded(&invoke_trace, &call)` where it calls
`trace_call_guarded(&invoke_trace, &call)` today — the two signatures are
identical, so the call site does not change — and
`payload/control/include/cs2/control/trace_guard.hpp` is **deleted**, with
`payload/control/tests/test_control_trace_probe_guard.cpp` and the two root
`meson.build` blocks that register it. (The steps are listed in
`docs/handoff-wave3.md` §5; the reconciliation is a fix round on T39's
branch, after this one has merged.)

**Why that way and not the other.** Two vectored handlers with **independent**
`jmp_buf`s and **independent** `active` flags is exactly the fragility this
task exists to remove: each one reads correctly on its own, each one calls
itself "the guard", and the one that is broken is the one nobody is
testing. That is how a broken guard survived **next to** a working one for a
whole wave of work. One mechanism, one owner, one test — and the tree's
`diag-guard` test fails the moment a second one appears.

## Still open

1. **Compound (4) shapes** — still skipped (`docs/re/occlusion.md` "Still to pin" 3).
2. **The interval record's full layout** — only the distance `+0x0C` and the
   enter/exit byte `+0x32` are pinned. Map the remaining `0x38` bytes and surface
   provenance before treating interval output as a complete damage oracle.
3. **Mesh interval semantics** — the aim-side mesh ray path keeps only two triangle
   crossings. The host BVH test proves BVH-vs-flat equivalence for that rule, not
   equivalence to the engine's ordered intervals, especially on open/disconnected
   clusters and rays starting inside a mesh.
4. **The mesh vertex scale** (`shape+0xB8`) is assumed uniform; a non-uniform mesh
   scale is not modelled.
5. The enumeration is still a body-box overlap; swapping it for the engine's own
   `TraceLineIntervals` segment trace (exact same solids, no over-inclusion) is the
   Tier-1 step.

## The probe's call form — verified against the live build (2026-10-22)

T39 added a **probe** (`trace_probe`): a control command that asks the engine its
own question for a ray and publishes **both** answers side by side plus the
difference (`docs/re/occlusion.md` §"the trace probe"). The call form below was
re-derived from the **live** build's own call sites — not from the decompile above
— because the decompile does not say how the two vectors are passed.

**The build.** The live `client.dll` is now sha256 `5a46a09b…` (the game updated on
2026-10-21; the notes above are for `659940c1…`). The **entry RVA is unchanged**:
`TraceLineIntervals` is still `FUN_180a19e90` at `0xa19e90` (the name string is now
at `client + 0x1c32448`; the function spans `0xa19e90..0xa19ff4`).

**The call.** The engine's own caller — the per-pellet function's first trace,
`FUN_1808830e0` — sets the arguments up like this:

```asm
0x883164  lea      r9, [rsp + 0x30]              ; arg4 = &the extension (a local)
0x883169  movss    xmm0, dword ptr [rip + 0x1242f3b]   ; -1.0f
0x883171  mov      r8, r15                       ; arg3 = &the start vector
0x883174  movss    dword ptr [rsp + 0x28], xmm0  ; arg6 = max_fraction = -1.0f
0x88317a  mov      rdx, rdi                      ; arg2 = &results
0x88317d  mov      qword ptr [rsp + 0x20], r14    ; arg5 = filter + 8
0x88318d  mov      rcx, qword ptr [rip + 0x19a3ec4]  ; arg1 = *(client + 0x2227058)
0x883196  call     0x180a19e90
```

so the call form is

```c
TraceLineIntervals(*(client + 0x2227058),  // arg1: the query interface
                    &results,               // arg2: the trace's results container
                    &start,                 // arg3: a POINTER to a 12-byte Vector
                    &delta,                 // arg4: a POINTER to the EXTENSION
                    filter + 8,             // arg5: the collision-mask qword
                    -1.0f);                 // arg6: max_fraction
```

(The mask is the **5th** argument and the float the **6th**: in `FUN_180a19e90`
they are read from `[entry_rsp + 0x28]` and `[entry_rsp + 0x30]`. An earlier note
in this file called them the 7th and the 8th — that was wrong.)

**The results container is NOT the overlap query's `CUtlVector`.** It is a
different type with a different header, and the probe got that wrong once (the
review's blocker). The layout was re-derived from the **live** build
(client.dll sha256 `5a46a09b…`, 2026-10-22) from the **two functions that write
it**:

* the physics world's own query — `vphysics2.dll`, the world's vtable **`+0x520`**,
  which `TraceLineIntervals` calls (`(*(*world + 0x520))(world, results, start,
  delta, mask, flags, max_fraction)`); and
* the append helper the **second** query kind uses — `client.dll`'s
  `FUN_18084d1a0` (`mov rcx, results; call append`).

Both read the count at **`+0x00`** and the record pointer at **`+0x08`**, both
compare the count against the **capacity** at **`+0x10`**, and both grow the
array **only** when `count == capacity` **and** bit 30 of the **flags** at
**`+0x14`** is clear:

```asm
; vphysics2.dll +0x64f80 (the world query)          ; client.dll FUN_18084d1a0
movsxd r13, dword ptr [r15]              ; 0x...75113  movsxd r14, dword ptr [rcx]      ; 0x18084d1b0
cmp    r13d, dword ptr [r15 + 0x10]      ; 0x...7511d  cmp    r14d, dword ptr [rcx + 0x10] ; 0x18084d1b9
jne    skip_growth                       ; 0x...75121  jne    skip_growth                ; 0x18084d1bd
test   dword ptr [r15 + 0x14], 0x40000000; 0x...75127  test dword ptr [rcx + 0x14], 0x40000000 ; 0x18084d1c3
jne    skip_growth                       ; 0x...7512f  jne    skip_growth                ; 0x18084d1ca
```

| offset | field |
|---|---|
| `+0x00` | `int32` count (the records) |
| `+0x08` | `void*` data (the record array) |
| `+0x10` | `int32` capacity (in records) |
| `+0x14` | `int32` flags: bit 30 (`0x40000000`) = **the buffer is external — never grow**; bits 30/31 (`0xC0000000`) = **never free the buffer** |

**So "the engine never allocates" is established for this container**: with bit 30
set, *both* writers jump straight past their growth, so neither the allocator nor
the reallocator is reached. The overlap vector's `0x80000000` at `+0x1C` belongs
to a **different** type and means nothing here — the probe used to set it, which
is what made the engine write through a null pointer.

Because the engine appends **at** `data[count]` (it increments the count
afterwards), a trace that produced more records than the advertised capacity
would write past the end of the buffer: the probe's buffer carries a slack of 64
records, and the answer publishes `intervals_overflow` when the count exceeds the
published maximum.

**The fourth argument is the EXTENSION (`end - start`), not the absolute end
point.** The engine's own caller fills it with `normalize(direction) * range`
(`FUN_18089ca80` writes `normalize(dir) * max_range` into it), and the interval
consumer scales the fractions by *its* length. So the segment the query walks is
`start → start + delta`, and the fraction at `+0x0C` is relative to `|delta|`.
(The decompile above names the argument "end"; it is the ray's `m_Delta`. Passing
an absolute end point instead would trace the wrong segment entirely.)

**The interval record.** The consumer (`FUN_180886090`) reads:

* `+0x0C` — a float, the **fraction** along the ray. The thickness it computes is
  `(leave - enter) * |delta|` — the fraction difference times the ray's length
  (`FUN_180165340` is `sqrt(x²+y²+z²)`, the length of the extension the consumer
  is handed). So `distance = fraction × |delta|`.
* `+0x32` — a byte: `1` = the ray **enters** a solid, `0` = it leaves one (the
  consumer pairs an `enter` record with the next `leave` record).
* the stride is `0x38` (`mov edx, 0x38` in the consumer).

**The live process confirms every offset** (read out of the running `cs2.exe`,
2026-10-22, read-only):

| what | value |
|---|---|
| `client.dll` base | `0x7fffd1f60000` |
| `*(client+0x2227058)` (the interface) | `0x7fffd44aaf30` — a live pointer |
| `*(client+0x2227070)` (the filter's vtable) | `client + 0x1ad4838` — the vtable named above |
| `*(client+0x2227078)` (the filter's mask) | `0xffffffffffffffff` — all contents, as above |
| the filter's flags / group (`+0x37` / `+0x38`) | `0x0f` / `3` |
| the bytes at `client + 0xa19e90` | `4c 8b dc 49 89 5b 08 …` — the same prologue as the file on disk |
| the name string at `client + 0x1c32448` | `TraceLineIntervals` |

**What the probe publishes** (`payload/control/src/occlusion_query.cpp`,
`probe_trace`): for each ray the engine's interval records — the fraction, the
distance (`fraction × |delta|`), the enter/exit byte and the record's **raw 0x38
bytes in hex** — our own occluders' spans, and the difference. The surface is
published as `"unmapped"`: only two fields of the record are mapped, and the raw
bytes are published so a later RE pass can map the rest **from the probe's own
output** without re-running the game.

> **Open: the entry point is not a curated signature.** `payload/sdk/offsets.hpp`
> has `physics_query_interface`, `physics_query_filter` and `entities_in_box` as
> curated (`OffsetSource::signature`) entries, but **no** `trace_line_intervals`:
> the probe therefore always uses the hard-coded fallback `0xA19E90`. The next game
> update moves the function and the probe would call a random address (the guard
> would probably catch it, but the failure would look like "the engine call
> faults" rather than "the offset moved"). The dumper's curated set
> (`external/dumper/src/analysis.rs`) is **not this task's file**, so this is
> reported as a follow-up instead of fixed here: the signature is the byte
> sequence of the prologue the live build has (`4c 8b dc 49 89 5b 08 …` at
> `0xa19e90`) together with the name string `TraceLineIntervals` (now at
> `client + 0x1c32448`).

**A finding about the shared fault guard (2026-10-22).** `cs2::diag::call_guarded`
— the guard every engine call in the payload uses — **cannot recover on a
clang-cl build**. Its vectored handler is a `noexcept` function, and clang-cl marks
a `noexcept` function `nounwind`, so MSVC's `longjmp` refuses to unwind through it
and raises `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) instead of returning. The
same code built with MSVC's `cl.exe` recovers, and so does the same code with the
handler's `noexcept` dropped — both measured standalone on this machine.

> **The log evidence, re-measured (2026-10-22).** The payload's own log
> (`%LOCALAPPDATA%\cs2-internal\cs2-payload.log`, 70.6 MB, 548 370 lines) holds
> **1017** `[crash]` lines, of which **199** are `[crash] handler installed` and
> **12** are `[crash] guarded`. The earlier claim ("nine `[crash] guarded` lines,
> each followed by a fresh `[crash] handler installed` — the process died and was
> re-injected, every time") was **wrong on both counts**:
>
> * there are **twelve** guarded lines, not nine — four in the 88 k-line region
>   (lines 88135, 88387, 88409, 90369) and eight in the 533 k-line region (533404,
>   533478, 533601, 533680, 533728, 533890, 533950, 534203);
> * **only the first four** are followed by a fresh `[crash] handler installed` and
>   the payload's start-up banner — those are the process dying and being
>   re-injected. The other **eight** are followed by the payload's own continued
>   `[state]` output: **the process survived** them;
> * and the log **cannot** support the inference in any case. The `guarded` phase is
>   logged by the diag handler **on the recovery path**, immediately *before* the
>   `longjmp` (`payload/diag/src/crash_handler.cpp`: `log_exception("guarded",
>   info); std::longjmp(g_guard_jmp, 1);`), so a `guarded` line means the handler
>   was **entered** — it is written in the success case too. A failed recovery
>   leaves **no** line at all.

So the **diagnosis** stands (the shared guard cannot recover under clang-cl —
reproduced standalone: a `noexcept` vectored handler fast-fails with 0xC0000409,
the same program without the `noexcept` prints "recovered" and exits 0), but the
**evidence as stated was wrong**. The record is corrected here so the next task
does not cite it.

The repo-wide fix is **one word** — drop the `noexcept` from
`LONG CALLBACK vectored_handler(...)` in `payload/diag/src/crash_handler.cpp`.
Until that lands, the probe's engine call runs under **its own** guard
(`payload/control/include/cs2/control/trace_guard.hpp`, the same vectored handler
+ `setjmp`/`longjmp`, with a handler the unwinder will cross), and the host test
`control-trace-probe-guard` proves that guard recovers from a deliberate fault.
