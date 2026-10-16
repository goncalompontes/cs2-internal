# `client.dll` — aim / shot model (build 25815307)

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.

> Companion to `docs/re/client-dll.md` (entity system, input path, view matrix).
> This file records the RE of the **shot model** the ragebot design
> (`docs/ragebot.md`) depends on: spread, inaccuracy, the shot seed, the trace
> API, hitboxes, and the penetration parameters.

## TL;DR / decision

**The bullet cone is deterministic.** `FX_FireBullets` → `FUN_180d23460` seeds
the RNG from the command's `random_seed` and draws the offset from it. Given the
command sequence (and the weapon's spread/inaccuracy magnitudes) the exact
pellet direction(s) are computable — there is no random cone to sample. The
remaining uncertainty is only the target's lag-compensated pose.

## The spread cone — `FUN_180d23460` (RVA `0xd23460`)

Called from `FX_FireBullets` (`FUN_180d22c50`, RVA `0xd22c50`), which passes
`(weaponId, numBullets, attackType, seed, spreadMag, inaccMag, dist, outX, outY)`.
Assembly-verified:

```
RandomSeed(seed);
for each pellet:
  r1 = RandomFloat(0, 1);   a1 = RandomFloat(0, 2*pi);   // spread      (1.0 @ 0x181ac0074, 2pi @ 0x181b00584)
  r2 = RandomFloat(0, 1);   a2 = RandomFloat(0, 2*pi);   // inaccuracy
  // weapon patterns:
  //   revolver (attackType 1):  r = 1 - r^2
  //   negev (dist < 3):         r = 1 - r^(2^k)
  //   shotgun / pattern mode:   FUN_1807cd1b0(table, weaponId, attackType, i, &angle, &radius)
  //   debug convars:            force r = 1 / force a = pi/2 (0x181c088cc = 1.5708)
  off_x = r1*cos(a1) + r2*cos(a2);
  off_y = r1*sin(a1) + r2*sin(a2);
```

The offset is applied in the `(right, up)` tangent frame of the aim direction.

### The seed

- `CBaseUserCmdPB::random_seed` (`+0x6c`, field 10) = **`MD5_PseudoRandom(sequence)`**
  (`docs/re/subtick.md` §"The command build"). So the seed is a pure function of
  the command number → the offsets of the next shots are computable too.
- The pawn carries per-attack seed words (`FUN_180940ff0` / `FUN_180941020`
  read `pawn+0x1918` / `+0x1920`).
- `CCSPlayerController::m_bFireBulletsSeedSynchronized` (`+2617`) = the
  client/server seed-sync flag.
- RNG globals: `RandomSeed` `0x1821b20c4`, `g_nRandomSeed` `0x181bd6ca0`,
  `g_nRandomSeedAlt` `0x181c87250`.

## Inaccuracy (the magnitudes the cone is scaled by)

| Function | VA / RVA | Role |
|---|---|---|
| `FUN_18080b880` | `0x18080b880` / `0x80b880` | base-virtual `GetSpread`/`GetInaccuracy`: sums the VData terms (stand/move/jump/land/fire/…), scales by convars (`weapon_air_spread_scale` `0x1824c0888`), blends jump **initial↔apex** by `|v_z|`, `pow(x,0.25)`, clamps ≤ 1. Present in ~50 per-weapon vtables. |
| `FUN_18080cba0` | `0x18080cba0` / `0x80cba0` | **move** term = `(speed/250) * (1 - |cos(vel, view)|)^e * convar` (directional). |

Convars: `weapon_accuracy_nospread` `0x181bcfeb0`, `weapon_air_spread_scale`
`0x181bcff30`, `weapon_debug_spread_show` `0x181caf7a0`,
`weapon_debug_inaccuracy_only_up` `0x181ca1518`, `weapon_debug_max_inaccuracy`
`0x181ca13f0`, `weapon_accuracy_shotgun_spread_patterns` `0x181ca1588`,
`sv_turning_inaccuracy_*` / `sv_strafing_inaccuracy_*`.

## Weapon data (`CCSWeaponBaseVData`) — penetration / damage parameters

Schema offsets (from `calibration/schema.json`, `CCSWeaponBaseVData`):

| Field | Offset |
|---|---|
| `m_flSpread` (FiringModeFloat) | 1872 |
| `m_flInaccuracyCrouch/Stand/Jump/Land/Ladder/Fire/Move` | 1880–1928 |
| `m_flRecoilAngle` / `…Variance` / `m_flRecoilMagnitude` / `…Variance` | 1936–1960 |
| `m_flInaccuracyJumpInitial` / `…JumpApex` / `…Reload` | 1976 / 1980 / 1984 |
| `m_nRecoilSeed` / `m_nSpreadSeed` | 2004 / 2008 |
| `m_nDamage` | 2080 |
| `m_flHeadshotMultiplier` | 2084 |
| `m_flArmorRatio` | 2088 |
| `m_flPenetration` | 2092 |
| `m_flRange` | 2096 |
| `m_flRangeModifier` | 2100 |

`FX_FireBullets` reads exactly these (`0x820`/`0x82c`/`0x830`/`0x834` = damage,
penetration, range, range-modifier). The **penetration algorithm itself is
server-side** (`server.dll`); the client has the parameters and the trace.

**Getting the VData pointer (resolved 2026-10-16).** The active weapon entity's
`C_CSWeaponBase::m_pWeaponVData` is `C_BaseEntity::m_nSubclassID` (`+0x380`) + `8`
= **`weapon + 0x388`** (verified live: an AK-47 reads `m_nDamage 36`,
`m_flHeadshotMultiplier 4.0`, `m_flPenetration 2.0`, `m_flRangeModifier 0.98`,
`m_iMaxClip1 30`). Cross-check: community `SCHEMA(CCSWeaponBaseVData*, 0x388,
m_pWeaponVData)` (YouGame.Biz 378413) and `m_nSubclassID + 0x08` (MitilcC
CS2-Internal-Cheat). The payload reads it in `StateReader::read_player`
(`weapon_vdata`) and `aim::read_weapon_profile` applies it, so the damage model is
the real weapon (`docs/ragebot.md` §0).

## Trace API — `CGamePhysicsQueryInterface`

Source: `src/game/shared/gamephysicsquery.cpp`; the interface is resolved from
module `vphysics2` as `VPhysics2_Interface_001` (see `docs/re/vphysics2.md`).

| Method | VA / RVA | Returns |
|---|---|---|
| `TraceShape` | `0x180a19140` / `0xa19140` | one `CGameTrace` |
| `TraceShapeToEntity` | `0x180a19bb0` / `0xa19bb0` | one `CGameTrace` |
| `TraceShapeWithSortedResults` | `0x180a1a0a0` / `0xa1a0a0` | **distance-sorted array of all hits** (grows) |
| `TraceLineIntervals` | `0x180a18fd0` / `0xa18fd0` | array of **56-byte interval records** (enter/exit) |
| `EntitiesInShape` / `EntitiesAlongLinearSweep` | `0x1809db220` / `0x1809daae0` | overlap records (see below) |
| `FilterOverlapResults` | `0x1809de620` / `0x9de620` | filters overlaps (worldspawn `0x8000`) |
| `GetPointContents` | `0x1809e7e50` / `0x9e7e50` | contents |

- **`CGameTrace`** is ~`0xB8` B: `start +0x78`, `end +0x84`, `fraction +0xac`,
  plus surface / hitbox / bone descriptors and an entity handle.
- **Overlap records** are `0x70` B (`ConvertOverlapMultiple` `0x1809d1f00`,
  `ConvertHitboxOverlapMultiple` `0x1809d1d80`): entity handle + **physics-object
  pointer** + transform/shape blob + material token.
- Trace filters (RTTI, for the recipe): `CTraceFilterNoNPCsOrPlayer`,
  `CCSTraceFilterSimple`, `CTraceFilterForPlayerHeadCollision`, … (`0x18230…`).

### Calling the trace from the payload (recipe)

The physics query interface is a **singleton**, not vtable-dispatched from the
caller's side: the global `PTR_DAT_18221f058` (`client_base + 0x221f058`) holds
its pointer (the object lives at `client_base + 0x2541c30`). `TraceShapeWithSortedResults`
(`FUN_180a1a0a0`) is then called **directly**:

```
FUN_180a1a0a0(interface, filter, start, end, results_vector, &count);
```

and forwards to the physics world's vtable `+0x560`. A **static default filter**
exists at `client_base + 0x221f070` (vtable `0x181ace848`); inline filters set
that vtable plus a collision mask (`0x1c1003`) and group. `CGameTrace` is
~`0xC0` bytes and the results buffer is a `CUtlVector<CGameTrace>` (the engine's
own call sizes it for 64 entries).

**Open before wiring it live:** pin the exact filter / `CGameTrace` / results
struct layouts and the argument order (`procedure_pseudo_code` on the call sites
is ambiguous) — a wrong call faults the game (the payload has no registered
exception tables), so this is not guessable. The aim solver already consumes
occluders (`SolveInput::occluders` → `DamageField::clip`), so only the probe that
produces them remains.

### The call convention, pinned from a live site (2026-10-16)

From `FUN_180b928b0` (a caller of `TraceShapeWithSortedResults`, VA `0xa1a0a0`):

```
MOV RCX, [0x18221f058]      ; arg1 = *(client_base + 0x221f058)  the query interface
LEA RDX, [RBP-0x70]         ; arg2 = a 0x10-byte + float descriptor (filled by FUN_180203c30)
MOV R9,  RDI                ; arg4 = end
MOV R8,  RBX                ; arg3 = start
LEA RAX, [RSP+0x30]         ; the filter, built inline:
   [RSP+0x30] = 0x181ace848 ;    vtable  (client_base + 0x1ace848)
   [RSP+0x38] = 0x1c1003    ;    collision mask
   [RSP+0x68] = 4, [+0x69] = flags, [+0x50] = id, [+0x60] = group …
MOV [RSP+0x20], RAX         ; arg5 = filter
LEA RAX, [RBP-0x40]         ; the results CUtlVector (count @ +0x10, rep @ +8)
MOV [RSP+0x28], RAX         ; arg6 = &results
CALL 0x180a1a0a0
```

So the real signature is `(this=iface, <rdx descriptor>, const Vector& start,
const Vector& end, ITraceFilter* filter, CUtlVector<CGameTrace>* results)` — **not**
the `(iface, filter, start, end, vec, count)` the earlier note assumed. Note the
2nd argument. A **static default filter** is available at `client_base + 0x221f070`
(0x18 after the query global), so a first version can pass that instead of
building one. Still required before calling: a dumper **signature** for the query
global and for `0xa1a0a0` (ADR-0012 — no hard-coded RVAs), the exact `Ray_t`
descriptor contents, and the `CGameTrace` field offsets (at least `fraction`
`+0xac`). Failed outcome is a process abort, so this needs its own focused RE +
live-validation pass.


## Hitboxes / bones

| What | Offset |
|---|---|
| `C_BaseEntity::m_pGameSceneNode` | `+0x330` (client) |
| `C_BaseEntity::m_CBodyComponent` | `+0x30` |
| `CSkeletonInstance::m_modelState` | `+0x140` (client; server `+0x120`) |
| `CSkeletonInstance::m_nHitboxSet` | `+0x3FC` (client; server `+0x3BC`) |
| `CModelState::m_hModel` / `m_ModelName` | `+0xA0` / `+0xA8` (StrongHandle: deref **twice**) |
| **`CModelState::m_pBoneArray`** | **`+0x80`** (= `CSkeletonInstance + 0x1C0`), pointer to the bone array, **stride `0x20`** |

**Resolved** via `CSkeletonInstance::GetTransformsForHitboxList` (`FUN_180aafca0`,
VA `0xaafca0`): it reads `*(longlong*)(skel + 0x1C0)` (the bone array — `+0x1C0`
= `m_modelState(+0x140) + 0x80`) and indexes bones at `boneIndex * 0x20`; it
reads `m_hModel` at `skel + 0x1E0` (= `m_modelState + 0xA0`), which pins
`m_modelState = +0x140`. Each bone is a 32-byte transform (`{Vector pos; Quat xyzw}`;
copied by `FUN_1801c9f40`). Hitgroups: `HITGROUP_HEAD…` (`0x181af2d60`),
`m_iHitGroupId`.

### The runtime studio hitbox set (resolved 2026-10-16, build 25947352)

The runtime hitbox structures are the **modellib schema layout** plus runtime
tail fields; verified live (external RPM) against a pawn and cross-checked with
the [CS2 Reference `CHitBox`/`CHitBoxSet`](https://cs2opendev.github.io/CS2OpenDev-Docs/schemas/modellib/CHitBox/)
(codegen build 25966348) and the community `c_hitbox`/`c_hitboxsets` structs
(UC *Reversal, Structs and Offsets* pp. 14/65):

```
model (from m_hModel)                        CModel
  +0x70  int   m_nRenderMeshes               (5 meshes on a player)
  +0x78  ptr   m_pRenderMeshes               (ptr per mesh)
  +0x90  ptr   m_pMeshHitboxSetMasks         (uint64 per mesh)
  +0x220 int   m_nHitboxBoneRemapCount       (must be non-zero)
  +0x228 ptr   m_pHitboxBoneRemap            (int16[])
  +0x240 ptr   m_pMeshRemapOffset            (uint16 per mesh)
  +0x2f0 ptr   m_pMeshRemapStart             (uint16 per mesh)

mesh                                        CRenderMesh
  +0x164 uint  flags                         (bit31: +0x168 is a pointer, else inline)
  +0x168 ptr   m_pHitboxSets                 (CHitBoxSet[] , stride 0x48)
  +0x174 int   m_nHitboxSets

hitbox set (stride 0x48)                    CHitBoxSet
  +0x20 uint   m_nNameHash                   ("default" = 0xde394220 on this build)
  +0x28 int    m_nHitBoxes                   (19 on a player)
  +0x30 ptr    m_pHitBoxes                   (CHitBox[])

CHitBox (stride 0x70; KV3 layout + 0x26 runtime tail)
  +0x10 char*  m_sBoneName                   ("head_0", "spine_1", …)
  +0x18 Vector m_vMinBounds  } capsule endpoint A (bone-local)
  +0x24 Vector m_vMaxBounds  } capsule endpoint B (bone-local)
  +0x30 float  m_flShapeRadius               (the capsule radius, e.g. head 4.3)
  +0x34 uint   m_nBoneNameHash
  +0x38 int    m_nGroupId                    **the hitgroup** (1 = HITGROUP_HEAD)
  +0x3C uint8  m_nShapeType                  (2 = capsule)
  +0x3D bool   m_bTranslationOnly
  +0x48 uint16 m_nHitBoxIndex
  +0x50/+0x60  runtime-only fields (the transform path)
```

**Hitboxes are capsules**, not AABBs: `m_vMinBounds`/`m_vMaxBounds` are the two
endpoints (in the owning bone's local space) and `m_flShapeRadius` is the radius.
The world capsule is `bone.pos + rotate(bone.quat, endpoint)` for each endpoint.

**Hitbox → bone** is a per-mesh remap, not a field on the box:
`CModel::GetBoneIndexForHitboxForMesh` (`FUN_18176cb40`) computes
`bone = m_pHitboxBoneRemap[ m_pMeshRemapStart[mesh] + m_pMeshRemapOffset[mesh] + m_nHitBoxIndex ]`.
The set's own name hash (`m_nNameHash`) is what the console's
`sv_showplayerhitboxes` selection matches; the skeleton's set index is
`m_nHitboxSet` (`+0x3FC`, `0` = `default`).

Live check (local pawn, de_dust2): mesh 0, set 0, 19 boxes; `head_0` →
`bone 7`, capsule `(-1,1.8,0)–(3.5,0.2,0)` r `4.3`, hitgroup `1`; the transformed
head centre lands on the pawn's **eye** — the correct bug this replaces was the
old "head cube on the eye / collision-AABB body" proxy.

**Consumed by** `payload/aim/src/reader.cpp` (`read_model_hitboxes`) → the ragebot
solver. There is no hard-coded RVA: only struct offsets, which are stable.


## Notes / gotchas

- **Spread is a delta, not a distribution.** The whole "sample 256 spread
  angles" approach solves a problem that has a closed form here.
- The spread the *client renders* (crosshair radius) comes from the weapon's
  inaccuracy + spread; the networked bullet's magnitudes arrive in
  `CMsgTEFireBullets` — but the client can compute them itself
  (`FUN_18080b880` / `FUN_18080cba0`).
- `m_flInterpolatedInaccuracy` / `m_vClientScopeInaccuracy` are client-side
  crosshair fields, not the shot values.
- `sv_showplayerhitboxes` / lag-comp hitbox strings (`0x181bd0b40`,
  `0x181c09050`) are the lag-compensation visualisers.
