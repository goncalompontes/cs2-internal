# `client.dll` — aim / shot model

> **Rewritten 2026-10-22 (T38).** The previous revision of this file described
> build `25815307` and was **wrong** about the spread model in three ways: it
> said the sampler draws four numbers *per pellet* (it draws two per pellet),
> it keyed the revolver's transform on a *distance*, and it carried the old
> build's addresses. The addresses, the draw order, the attack type, the negev's
> rule and the pattern table are re-derived below.
>
> **The attack type, decided.** The payload reads `m_weaponMode` and publishes
> it as **evidence**, but it **cannot** know the engine's attack type for the
> shot it is about to take, so it treats the weapon as **NOT** in secondary
> mode: the revolver's `1 − r²` is **not** applied and the cone is the standard
> one (§"The attack type" below).

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.

> Companion to `docs/re/client-dll.md` (entity system, input path, view matrix).
> This file records the RE of the **shot model** the ragebot design
> (`docs/ragebot.md`) depends on: spread, inaccuracy, the shot seed, the trace
> API, hitboxes, and the penetration parameters.

## The build

| | sha256 | size | mtime |
|---|---|---|---|
| the notes' original build (`25815307`) | `44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008` | 39 169 176 B | 2026-10-13 |
| the RE build (`659940c1`) | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` | 39 205 528 B | 2026-10-15 |
| **the live build** | **`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`** | 39 205 528 B | **2026-10-21 18:37** |

**The game auto-updated on 2026-10-21**: the module on disk is no longer
`659940c1`. The **six addresses in the table below** were re-checked against
the on-disk build with capstone and **all of them still hold** — the module's
size is unchanged and the code did not move. The **schema offsets** are
unaffected (they come from the schema dump, not from a signature).

> **What the re-check does *not* cover.** The **Inaccuracy** section below still
> carries the old build's `0x80b880` / `0x80cba0`: on the live build they
> resolve to unrelated functions, and that section says so. They are **not**
> among the six re-checked addresses, and the spread model's composition is
> still open work (`docs/ragebot.md` §0).

### The addresses

| | old (`44af2aa0`) | RE build (`659940c1`) | **live (`5a46a09b`)** |
|---|---|---|---|
| `FX_FireBullets` | `0xd22c50` | **`0xd237d0`** | **`0xd237d0`** |
| the cone sampler | `0xd23460` | **`0xd23fe0`** | **`0xd23fe0`** |
| the pattern lookup | `0x7cd1b0` | **`0x7cdad0`** | **`0x7cdad0`** |
| the pattern table global | `0x1822195f8` | **`0x1822195e8`** | **`0x1822195e8`** |
| the region-0/1 builder (recoil) | — | `0x7cae80` | `0x7cae80` |
| the region-2 builder (pellets) | — | `0x7cb180` | `0x7cb180` |

> **A trap.** On the current build the *old* `0x7cd1b0` falls **inside a
> different function** (`FUN_1807cd010`, the crosshair/recoil consumer). Anyone
> re-deriving by address translation lands on the wrong function.

## TL;DR / decision

**The bullet cone is deterministic.** `FX_FireBullets` → the cone sampler seeds
the RNG from the command's `random_seed` and draws the offset from it. Given the
command sequence (and the weapon's spread/inaccuracy magnitudes) the exact
pellet direction(s) are computable — there is no random cone to sample. The
remaining uncertainty is only the target's lag-compensated pose.

## The spread cone — `FUN_180d23fe0` (RVA `0xd23fe0`)

Called from `FX_FireBullets` (`FUN_180d237d0`, RVA `0xd237d0`), which passes
`(weaponId, numBullets, attackType, seed, spreadMag, inaccMag, recoilIndex, out)`.
**Simplified** from the disassembly (the three debug convars are not modelled —
see the note under the listing):

```
RandomSeed(seed);
r1 = 0; a1 = 0;                       // the shot's spread pair
for each pellet:
  if (pellet == 0 || shotgun_patterns_convar) {
     r1 = RandomFloat(0, 1);  a1 = RandomFloat(0, 2*pi);      // the **spread**
     r1 = revolver ? 1 - r1^2 : r1;
     r1 = negev    ? 1 - r1^(2^k) : r1;                        // k = 3 - recoilIndex
     if (weapon_debug_max_inaccuracy) r1 = 1;                  // pellet 0's block only
     if (weapon_debug_inaccuracy_only_up && spreadMag != 0) a1 = pi/2;
     r1 = r1 * spreadMag;
  }
  if (shotgun_patterns_convar) {
     (a2, r2) = pattern_table[recoilIndex * numBullets + pellet];   // or the random cone
  } else {
     r2 = RandomFloat(0, 1);  a2 = RandomFloat(0, 2*pi);      // the **inaccuracy**
  }
  r2 = revolver ? 1 - r2^2 : r2;
  r2 = negev    ? 1 - r2^(2^k) : r2;
  if (weapon_debug_max_inaccuracy) r2 = 1;
  if (weapon_debug_inaccuracy_only_up && spreadMag != 0) a2 = pi/2;
  off_x = spreadMag*r1*cos(a1) + inaccMag*r2*cos(a2);
  off_y = spreadMag*r1*sin(a1) + inaccMag*r2*sin(a2);
```

> **The two `pi/2` forcings are gated on the spread magnitude being non-zero**
> (`test r15b,r15b` — the spread magnitude — at `0x180d24248` and
> `0x180d243b2`): "inaccuracy only up" forces the **spread** angle and the
> **inaccuracy** angle to π/2, and only when the shot has a spread to speak
> of. The listing above is a **simplification** of the disassembly, not a
> decompilation: the payload models none of the three debug convars.

**The draw order is the engine's, and it is not "four numbers per pellet":**

* the **spread** radius and angle (`r1`, `a1`) are drawn **once**, for pellet 0,
  and again for every pellet **only** when the
  `weapon_accuracy_shotgun_spread_patterns` convar is on;
* the **inaccuracy** radius and angle (`r2`, `a2`) are drawn **per pellet**.

So a single-pellet shot draws exactly `r1, a1, r2, a2` — the same four numbers,
in the same order, as a "four per pellet" model would draw for pellet 0 — and a
multi-pellet shot **reuses** the spread pair. The offset is applied in the
`(right, up)` tangent frame of the aim direction.

> **The repo's model was wrong here.** It drew all four numbers per pellet, so
> its RNG stream desynchronised from pellet 1 onward: every pellet after the
> first was wrong. The payload's model is fixed (`payload/aim/src/spread.cpp`),
> and **the single-pellet case is bit-identical for the standard pattern** (the
> golden vector in the Monte-Carlo core's test pins it).
>
> **The R8's offsets change by design.** The old model applied the revolver's
> `1 − r²` to the spread radius whenever the *designer name* was `revolver` —
> always. The engine's rule is the weapon **id** **and** `attackType == 1`
> (§"The revolver" below), the transform covers the **inaccuracy** radius too,
> and the payload cannot prove the pending shot's attack type (§"The attack
> type"), so the R8 is scored with the **standard** cone. **This is a
> §2.1-vs-§2.3 conflict, resolved in favour of the engine:** the specification's
> §2.1 asks for the single-pellet case to be bit-identical "for the rifles
> **and the R8**", and §2.3 mandates the engine's rule, which moves the R8. The
> two cannot both hold; the engine's rule wins, and the R8's move is recorded
> here rather than hidden.

### The three debug convars

The cone sampler reads three convars. Each one is reached through a **global
slot** that holds a pointer to a `ConVarRef`; the slot's image in the file is
**zero** — the pointer is filled in at run time — so the **slot's** address is
what a reader can name, and the `ConVarRef` it points to is a **run-time
object** with no address in the file. The sampler does **not** dereference a
`ConVar*`: it reads the **value** at `+0x58` off the `ConVarRef`.

| the global slot the sampler reads | name | what it does |
|---|---|---|
| `0x1825becb0` | **`weapon_debug_max_inaccuracy`** | forces both radii to 1 ("Force all shots to have maximum inaccuracy") |
| `0x1825becc0` | **`weapon_debug_inaccuracy_only_up`** | forces the angles to π/2 ("Force weapon inaccuracy to be in exactly the up direction") |
| `0x1825bece0` | **`weapon_accuracy_shotgun_spread_patterns`** | selects the **pattern table** instead of the random cone (off by default) |

The identities are pinned by the **registration code** (the `ConVarRef` each
name is registered into), not by the order of the globals.

### The negev

* the "distance" the engine tests is **`m_flRecoilIndex`**
  (`*(float*)(weapon_entity + 0x1A28)`) — a **per-shot counter**, `+1.0` after
  every shot, reset to 0 when the weapon stops firing. **Not a distance.**
* the branch is `weaponId == negevId && recoilIndex < 3`, and the exponent is
  `k = 3 - recoilIndex`: recoil index 0 → `r → 1 - r⁸`, 1 → `1 - r⁴`,
  2 → `1 - r²`. At 3 and above the branch is not taken at all — **no
  transform**. So it is the **first three shots of a burst**.
* the transform is applied to **both** radii (the engine has an identical copy
  of the branch for each).

### The revolver

* the transform `r → 1 - r²` needs **the weapon id** *and* **the attack type ==
  1**. The engine compares the weapon's id with the **`weapon_revolver` item
  definition's** id (`*(short*)(itemDefinition + 0x10)`), not with a designer
  name.
* the transform is applied to **both** radii.
* the payload reads the **VData's** `m_bIsRevolver` flag (VData `+0x71E`) as
  its stand-in for the id comparison. The attack type it needs is the
  **pending** shot's, which it **cannot** know (see below).

## The attack type

`*(int32*)(weapon_entity + 0x1A00)` = `C_CSWeaponBase::m_weaponMode`, the
engine's `CSWeaponMode`: `Primary_Mode = 0`, `Secondary_Mode = 1`.

**The payload's SDK already has it** — `payload/sdk/schemas/client_dll.hpp`:
`{ "weapon_mode", "SWeaponMode", 0x1a00, "m_weaponMode" }`. **No new dumper
signature is needed.**

### The four fire paths

`FX_FireBullets` has **three** callers, and the one that reaches
`CSBaseGunFire` (`FUN_180803830`) has **four** call sites. The **attack type**
each of them passes:

| the call instruction | the enclosing function | the attack type it passes |
|---|---|---|
| `0x1808227b4` | `0x180822610` | **`m_weaponMode`** — `*(int*)(weapon + 0x1A00)` |
| `0x180824a14` | `0x180824740` | the literal `1` (the secondary path; it *sets* `m_weaponMode = 1` first) |
| `0x180831047` | `0x180831010` | `ammo_in_clip & 1` (a virtual call) |
| `0x180831080` | `0x180831060` | the literal `0` |

The third caller of `FX_FireBullets` (`0x18080ac20`) passes the literal `0`,
and the first (`0x1807a5d50`) passes a **shot-struct field** (`+0x68`) of
unknown provenance.

**The field is sticky, and only one path clears it.** The secondary path
**sets** it to 1 (`0x1808249ef`) and nothing clears it there; the primary path
clears it **after** the fire (`0x180822881`); the two other paths never touch
it. A payload that computes the cone *before* the engine fires therefore reads
the engine's **previous** decision — and for three of the four paths the field
is not the engine's attack type at all.

### What the payload does with it

**The payload reads the field and publishes it as evidence. It never lets it
drive the transform.**

* **The read** (`weapon + 0x1A00`) lands in the weapon profile as
  `WeaponProfile::observed_attack_type` — what the field said, and nothing
  more. It is published because it is information; it is **not** knowledge.
* **The transform** (`cs2::aim::radius_transform`) reads the **pending** shot's
  attack type (`SpreadRules::pending_attack_type`), which the payload **cannot
  know**: there is no signal that proves the shot it is about to take is the
  secondary one — the field it would read is the engine's *previous* decision,
  and three of the four fire paths do not pass it to the cone sampler at all.
  The pending value therefore stays **unknown**, and the kernel's builder does
  not copy the observed value into it.

**So the payload treats the weapon as NOT in secondary mode**: the revolver's
`1 − r²` is **not** applied, and the cone is the **standard** one. This is the
specification's §2.2 rule ("where the payload cannot know the engine's attack
type, the conservative choice is to treat the weapon as NOT in secondary mode"),
applied literally.

> **The old claim, and why it was wrong.** An earlier revision of this file said
> "in practice the field is cleared by the fire path, so the payload reads 0
> (primary) for every shot it takes". That holds only if every shot went
> through the one clearing path **and** no secondary-mode shot preceded it — and
> the secondary path leaves the field at 1. A stale 1 would have put `1 − r²` on
> a shot the engine fires with the standard cone: a wrong direction, silently,
> up to half the spread radius.

## The pattern table

A global map at **`0x1822195e8`** (RVA `0x22195e8`): `uint16 weapon id` → a
**0x608-byte block**.

| offset | what |
|---|---|
| `+0x004` | region 0: 64 × `{float angle, float radius}` — the recoil/crosshair path |
| `+0x204` | region 1: the same shape — the recoil/crosshair path |
| `+0x404` | **region 2: the shotgun pellet pattern** — the one the *bullet* path reads |
| `+0x604` | an "invalid" flag (set when the weapon fires fewer than two bullets) |

Each region is 64 entries of `{angle, radius}` (8 bytes each), so region 2 ends
at `+0x603` and the flag is at `+0x604`.

**The lookup** (`FUN_1807cdad0`, RVA `0x7cdad0`):

```
if (!invalid && index < 0x40) { angle = block[0x404 + index*8]; radius = block[0x408 + index*8]; return; }
// the fallback: the random cone, from the **global** stream
angle = RandomFloat(0, 2*pi);  radius = RandomFloat(0, 1);
```

Note the fallback draws the **angle first**, then the radius — the opposite of
the normal path.

**The index** is `recoilIndex * numBullets + pelletIndex` and must be `< 0x40`;
otherwise the engine falls back to the random cone.

**The two builders** (both run at start-up, per weapon):

* `FUN_1807cae80` (RVA `0x7cae80`) fills **regions 0 and 1** from the VData's
  `recoil_angle` (`0x790`), `recoil_angle_variance` (`0x798`),
  `recoil_magnitude` (`0x7A0`), `recoil_magnitude_variance` (`0x7A8`) and
  `recoil_seed` (`0x7D4`). **The bullet path does not use them.**
* `FUN_1807cb180` (RVA `0x7cb180`) fills **region 2** from the VData's
  `num_bullets` (`0x730`) and `spread_seed` (`0x7D8`):

  ```
  CUniformRandomStreamImpl<CThreadNullMutex> stream(spread_seed);
  if (num_bullets < 2) { invalid = true; return; }
  N = min(num_bullets, 0x40);
  for (i = 0; i < 0x40; ++i) {
     angle  = RandomFloat(0, 2*pi);                 // the **angle first**
     radius = RandomFloat(k/N, (k+1)/N);            // k cycles 0,1,…,N-1,0,1,…
  }
  ```

**The band bounds are computed in single precision** — `(float)k * (1.0f /
(float)N)` (`0x1807cb2b4` / `0x1807cb2ee`) — and the payload computes them the
same way. In **double** the reciprocal and the product round differently: over
N = 1…64 the two computations disagree on **464 of the 2 144** band boundaries
(`k/N`) by **exactly one ulp** (5.96e-8), and the drawn radius moves with them.
The payload's table is therefore the single-precision one; the only remaining
difference from the engine is the float conversion of the draw itself (§"The
RNG" below).

**The convar gate.** The whole path is gated on
`weapon_accuracy_shotgun_spread_patterns`, which is **off by default** — so a
shotgun uses the **random cone** unless the server enables it. The convar is
the payload's problem: it is a `ConVar` the payload has no address for, and
reading it would need a **dumper signature** the repo does not have
(ADR-0012).

**So the Monte-Carlo core still refuses the shotgun — and the reason is a
policy, not a modelling limit.** The table **is** reproducible (the payload
builds it, above), and the convar is **off by default**, so the payload *could*
model the **default** (the random cone) and be right unless a server enables
the convar. It refuses anyway: the payload cannot tell which cone the server
enabled, and **a wrong number is worse than no number**. Reading the convar
would need a dumper signature this task may not add (ADR-0012), so the refusal
stands. It is **not** a regression: the shotgun was refused before this change
too.

## The RNG

The cone sampler uses the engine's **global** `RandomFloat` (tier0.dll), which
is `CUniformRandomStreamImpl<CAtomicMutex>::GenerateRandomNumber_Locked` on a
global stream seeded by `RandomSeed(seed)`. The pattern builder uses the same
core with a `CThreadNullMutex` template argument — **the same generator**.

The generator is RANROT: Park–Miller (`16807`, `127773`, `2836`, `2^31-1`) with
the 32-entry Bays–Durham shuffle. The payload's `UniformRandomStream`
(`payload/aim/src/spread.cpp`) is a faithful re-implementation, and it was
checked **instruction by instruction** against tier0's
`GenerateRandomNumber_Locked` (tier0.dll `0x15e1b0`): the warm-up (40 steps,
the table filled from `j = 39` down to `0`), the Schrage step
(`m*16807 - q*2147483647`) and the shuffle index (`(iy + (iy<0 ? 0x3ffffff :
0)) >> 26`, i.e. `NDIV = 2^26`) are **identical** — the integer stream matches
bit for bit.

**One difference, and it is the payload's.** tier0's `RandomFloat` scales the
31-bit integer in **single** precision (`(float)i * 2^-31`, capped at
`0x3F7FFFFE` = `0.99999988f`) and finishes in single precision; the payload's
`random_float` scales it in **double** (`1 / 2147483647`) and finishes in
double. The two agree on the integer and differ by **at most one float ulp**
(≤ 6·10⁻⁸) on the scaled value.

**Measured** (6 seeds × 100 000 draws each, an independent re-implementation of
both float paths): **1.37 %** of the `(0,1)` draws and **24.81 %** of the
`(0,2π)` draws differ, and the largest difference is **one ulp** in both
(5.96e-8 at the `(0,1)` magnitude, 4.77e-7 at the `(0,2π)` one). That is far
below anything the cone's physics can notice, but it means the payload's
offsets are **not bit-identical** to the engine's.

### The seed

- `CBaseUserCmdPB::random_seed` (`+0x6c`, field 10) = **`MD5_PseudoRandom(sequence)`**
  (`docs/re/subtick.md` §"The command build"). So the seed is a pure function of
  the command number → the offsets of the next shots are computable too.
  Replicated in `payload/aim/spread.hpp` (`md5_pseudo_random` + `UniformRandomStream`
  + `pellet_offsets`); host-tested (`aim-spread`), **pending a live cross-check** of
  the exact input construction.
- The engine's own `FX_FireBullets` takes the seed from the **pawn's** per-attack
  seed words (`FUN_180941eb0` / `FUN_180941ee0`, one per attack type), not from
  the command directly.
- The pawn carries per-attack seed words (`FUN_180940ff0` / `FUN_180941020`
  read `pawn+0x1918` / `+0x1920`).
- `CCSPlayerController::m_bFireBulletsSeedSynchronized` (`+2617`) = the
  client/server seed-sync flag.

## Inaccuracy (the magnitudes the cone is scaled by)

| Function | VA / RVA | Role |
|---|---|---|
| `FUN_18080b880` | `0x18080b880` / `0x80b880` | base-virtual `GetSpread`/`GetInaccuracy`: sums the VData terms (stand/move/jump/land/fire/…), scales by convars (`weapon_air_spread_scale` `0x1824c0888`), blends jump **initial↔apex** by `|v_z|`, `pow(x,0.25)`, clamps ≤ 1. Present in ~50 per-weapon vtables. |
| `FUN_18080cba0` | `0x18080cba0` / `0x80cba0` | **move** term = `(speed/250) * (1 - |cos(vel, view)|)^e * convar` (directional). |

Convars: `weapon_accuracy_nospread` `0x181bcfeb0`, `weapon_air_spread_scale`
`0x181bcff30`, `weapon_debug_spread_show` `0x181caf7a0`, and the three the
cone sampler reads (see above).

**Current build (2026-10-18).** The RVAs above are for build `25815307`; on the
live build they resolve to unrelated functions, so the composition must be
re-found. The **VData offsets are current** (`CsWeaponBaseVData`, `calibration/schema.json`):
`m_flSpread 0x750`, `m_flInaccuracyCrouch 0x758`, `m_flInaccuracyStand 0x760`,
`m_flInaccuracyJump 0x768`, `m_flInaccuracyLand 0x770`, `m_flInaccuracyLadder 0x778`, `m_flInaccuracyFire 0x780`, `m_flInaccuracyMove 0x788`,
`m_flInaccuracyJumpInitial 0x7B8`, `m_flInaccuracyJumpApex 0x7BC`,
`m_flInaccuracyReload 0x7C0`. The payload reads them (`read_weapon_profile`) and
`aim::inaccuracy(...)` is a first-order stand/move cone; the exact engine composition
(the applicable-term sum, the `weapon_air_spread_scale` convar, the jump initial↔apex
`|v_z|` blend, `pow(x, 0.25)`) is the next RE step (`docs/ragebot.md` §0).

## Weapon data (`CCSWeaponBaseVData`) — penetration / damage parameters

Schema offsets (from `calibration/schema.json`, `CCSWeaponBaseVData`):

| Field | Offset |
|---|---|
| `m_flSpread` (FiringModeFloat) | 1872 (`0x750`) |
| `m_flInaccuracyCrouch/Stand/Jump/Land/Ladder/Fire/Move` | 1880–1928 |
| `m_flRecoilAngle` / `…Variance` / `m_flRecoilMagnitude` / `…Variance` | 1936–1960 |
| `m_flInaccuracyJumpInitial` / `…JumpApex` / `…Reload` | 1976 / 1980 / 1984 |
| `m_nRecoilSeed` / `m_nSpreadSeed` | 2004 / 2008 |
| `m_bIsRevolver` | 1822 (`0x71E`) |
| `m_nNumBullets` | 1840 (`0x730`) |
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
`C_CSWeaponBase::m_pWeaponVData` is `C_BaseEntity::m_nSubclassID` (`+0x380`) +
`8` = **`weapon + 0x388`** (verified live: an AK-47 reads `m_nDamage 36`,
`m_flHeadshotMultiplier 4.0`, `m_flPenetration 2.0`, `m_flRangeModifier 0.98`,
`m_iMaxClip1 30`). Cross-check: community `SCHEMA(CCSWeaponBaseVData*, 0x388,
m_pWeaponVData)` (YouGame.Biz 378413) and `m_nSubclassID + 0x08` (MitilcC
CS2-Internal-Cheat). The payload reads it in `StateReader::read_player`
(`weapon_vdata`) and `aim::read_weapon_profile` applies it, so the damage model is
the real weapon (`docs/ragebot.md` §0). `read_weapon_profile` also reads
`m_flSpread` (`+0x750`) into `WeaponProfile::spread`, used by the deterministic
pellet objective when no explicit `spread` is passed.

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
is ambiguous) — so this is not guessable. A wrong call does **not** have to
kill the game: made through `cs2::diag::call_guarded` (the payload's only
recovery mechanism, `payload/diag`), the fault is logged and the call returns a
failure. The guard is a safety net, not a licence to guess — one guarded call
at a time per process, the faulting thread only, a fixed set of recoverable
exception codes, and the guarded call must not own resources (the recovery is a
`longjmp`). The aim solver already consumes
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
const Vector& end, ITraceFilter* results…)` — **not**
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
VA `0xaafca0`): it reads `*(longlong*)(skel + 0x1C0)` (the bone array — `0x1C0`
= `m_modelState(+0x140) + 0x80`) and indexes bones at `boneIndex * 0x20`; it
reads `m_hModel` at `skel + 0x1E0` (= `m_modelState + 0xA0`), which pins
`m_modelState = +0x140`. Each bone is a 32-byte `CTransform` — **`{Vector
m_vPosition; float m_flScale; Quaternion m_xyzw}`**: the position at `+0x00`, a
constant `1.0` at `+0x0C`, and the rotation at **`+0x10`** (not `+0x0C`). Measured
live 2026-10-16: the float at `+0x0C` is `1.0000` for every bone and the naive
`{Vector pos; Quat xyzw}` read yields non-unit quaternions (|q| ~ 1.0-1.4) that
**stretch** the hitboxes instead of rotating them; copied whole by
`FUN_1801c9f40`. Hitgroups: `HITGROUP_HEAD…` (`0x181af2d60`), `m_iHitGroupId`.

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
  +0x164 uint  flags                         (0 = no hitbox-set table; else +0x168 is the array)
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
- **The spread pair is shared across the pellets.** A weapon with no inaccuracy
  fires every pellet along the same ray; a model that draws a fresh spread pair
  per pellet scatters them.
- The spread the *client renders* (crosshair radius) comes from the weapon's
  inaccuracy + spread; the networked bullet's magnitudes arrive in
  `CMsgTEFireBullets` — but the client can compute them itself
  (`FUN_18080b880` / `FUN_18080cba0`).
- `m_flInterpolatedInaccuracy` / `m_vClientScopeInaccuracy` are client-side
  crosshair fields, not the shot values.
- `sv_showplayerhitboxes` / lag-comp hitbox strings (`0x181bd0b40`,
  `0x181c09050`) are the lag-compensation visualisers.
- **The payload's offsets are not bit-identical to the engine's** (the RNG's
  float conversion, §"The RNG"). The difference is at most one float ulp; the
  live check has still to confirm it.
