# The penetration / damage model (`docs/re/penetration.md`)

Build under analysis: `client.dll` sha256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`
(image base `0x180000000`; RVA = VA − base), the live CS2 build.

> Companion to `docs/re/occlusion.md` (the world-solids enumeration) and
> `docs/re/bullet-trace.md` (the engine's bullet trace). This file records the
> **damage a bullet deals after crossing walls** — the penetration model the
> occluded hitbox cells are valued with (`payload/aim/penetration.hpp`,
> `build_heatmap`). It is what the `heatmap` command's `pen=engine` selects and what
> the renderer's **`penetration`** mode draws.

## TL;DR

- CS2 has **`sv_penetration_type 2`** (the reworked path; `1` = CS:GO)[^uc-svpen].
- The per-wall loss is `client.dll` `FUN_1808a1820` (`HandleBulletPenetration`),
  called from `FUN_180885710` over the `BulletModulationEntry_t` (0x18) array of
  `TraceData_t` (`docs/re/bullet-trace.md`). It is, exactly:
  ```
  combined = min(bulletPenetrationDistanceModifier over the crossed surfaces)
  inverse  = max(0, 1 / combined)
  power    = max(0, (3 / m_flPenetration) * 1.25)
  lost     = t² · inverse / 24 + power · inverse · 3 + entry_fraction · damage
  damage  -= max(0, lost)      (blocked when damage − lost < 1)
  ```
  with `entry_fraction` = **0.16** (0.05 through a grate/glass, 1e-5 through a
  grate-contents span). The material special cases below override `combined`.
- The **per-surface** value is `bulletPenetrationDistanceModifier` from the game's
  **`SurfacePropertiesList`** (`CGameSurfaceProperties`, 0x20 B). Each shape points at
  its material object via **`shape+0x48`**; that object's `+0x10` is the list index
  (`FUN_1809e3560`). **Live-verified:** the world body's brushes resolve to 29
  distinct surfaces (concrete 0.5, metal 0.4, wood 0.9, glass 0.99, …).
- A shot also stops at the **4-wall budget** and the **≈90-unit crossed-thickness
  cap** (the reversed trace-advance `FUN_18089ca80`, `docs/re/bullet-trace.md`).

## The surface table — `CGameSurfaceProperties`

`FUN_1809cd960` parses the game's `surfaceproperties_game.txt` KV into a **0x20-byte**
record indexed by material name (stride 0x20, array at `SurfacePropertiesList`
manager `+0x28`). Fields (live-verified on the current map):

| offset | field |
|---|---|
| `+0x00` | `maxspeedfactor` (float) |
| `+0x04` | `jumpfactor` (float) |
| `+0x08` | **`bulletPenetrationDistanceModifier`** (float) |
| `+0x0C` | `bulletPenetrationDamageModifier` (float) |
| `+0x10` | `legacymaterialtag` (uint hash) |
| `+0x14` | **`gamematerial`** (int16, the `CHAR_TEX_*` id) |
| `+0x16` | `soundstackmaterial` (int16) |
| `+0x18…` | `climbable`/`isstairs`/`isslippery`/`allowsmokethrough`/`reflectbullets`/`reduceweaponimpact`/`bulletspenetrate` (bool) |

The list manager is the global `DAT_18254e100` (`client + 0x254e100`); the array is
`*(manager + 0x28)`. `FUN_1809e3560(param)` resolves an entry:
`*(int*)(param + 0x10) * 0x20 + *(manager + 0x28)`.

**The per-shape surface.** Each `IPhysicsShape` holds a pointer to its surface-material
object at **`shape + 0x48`**; that object's `+0x10` is the `SurfacePropertiesList`
index. Live (de_dust2, 10149 world brushes) resolves to **29 distinct** surfaces:

| material | `CHAR_TEX_*` | distance modifier |
|---|---|---|
| `'C'` 67 | concrete | 0.5 |
| `'M'` 77 | metal | 0.27 / 0.4 / 0.5 |
| `'W'` 87 | wood | 0.85 / 0.9 |
| `'D'` 68 | dirt | 0.6 |
| `'Y'` 89 | glass | 0.99 |
| `'G'` 71 | grate | 0.95 |
| `'U'` 85 | cardboard | 0.95 |
| `'L'` 76 | plastic | 0.7 / 0.75 |

(The overlap record's `+0x50` = `*(hit->vtbl[0x30]() + 0x28)` is the *same* object as
the hit shape's `+0x48` — that is what the payload's `walls.material_token` reports.)

## The per-wall loss — `FUN_1808a1820` (`HandleBulletPenetration`)

Re-decompiled on the live build with the `rea` plugin (Ghidra 12.1.4). Arguments:
`(TraceData_t* trace, BulletPenetrationData_t* bullet, BulletModulationEntry_t* entry,
int maxSecondaryTraces, void* out)`. `bullet = { Damage, Penetration, RangeModifier,
TailLength, PenetrationCount, PenetrationStopped }`; `entry = { StartFraction,
EndFraction, Damage, maxSecondaryTraces, uint16 SurfaceStart, uint16 SurfaceEnd,
uint8 Flags }` (0x18).

```c
// 1. The non-solid case (Flags bit 0 clear): range falloff over the segment.
if ((entry->Flags & 1) == 0) { damage *= pow(RangeModifier, (TailLength*End - TailLength*Start)/500); return 0; }

// 2. The entry surface's distance modifier (index SurfaceStart into TraceArray, 0x38).
fVar18 = SurfaceOf(record[SurfaceStart]).distance_modifier;   // +0x08
if (3000.0 < TailLength*End || fVar18 < 0.1) PenetrationCount = 0;   // gate -> stop

// 3. combined = min over SurfaceStart..SurfaceEnd of the surfaces' distance modifier.
for (i = SurfaceStart; i <= SurfaceEnd; ++i)
    fVar18 = min(fVar18, SurfaceOf(record[i]).distance_modifier);

// 4. The material special cases (only when the entry and exit materials match).
final = 0.16;
if (0.1 <= fVar18 && entry_material == exit_material) {
    if (material == 'U' 85 || material == 'W' 87) fVar18 = 3.0;
    else if (material == 'L' 76)                  fVar18 = 2.0;
    if (thickness >= 6.0) { if (grate bits) fVar18 = 3.0; }
    else {
        if (material == 'G' 71 || material == 'Y' 89) { final = 0.05; fVar18 = 3.0; }
        if (grate bits)                               { fVar18 = 32.0; final = 1e-5; }
    }
}

// 5. The loss.
inverse = max(0, 1 / fVar18);
power   = max(0, (3.0 / bullet->Penetration) * 1.25);
lost    = thickness² · inverse / 24 + power · inverse · 3 + final · bullet->Damage;
bullet->Damage -= max(0, lost);
if (bullet->Damage < 1) PenetrationStopped = true;         // blocked
else --PenetrationCount;
```

`thickness` is the world distance between the span's entry and exit positions. The
shot-level budget is the caller's `PenetrationCount` (the engine's own autowall uses
**5** — "4 penetrable walls" plus the loop's off-by-one[^uc-release]) and the ≈90-unit
crossed-thickness cap from the trace advance.

## What the code does

- `payload/aim/include/cs2/aim/penetration.hpp` — `SurfaceProperties`,
  `PenetrationLayer`, `PenetrationParams`, `penetration_layer_damage`,
  `penetration_after_walls` (the model above).
- `OccluderCell` and `OcclusionHull` carry the surface (`surface_modifier` =
  `bulletPenetrationDistanceModifier`, `damage_modifier`, `material`, `grate`).
- The payload reads the **real per-shape surface**: `occlusion_query.cpp`
  `read_shape_surface` (`shape+0x48` → `+0x10` index → `SurfacePropertiesList`), with
  the `pen_mod`/`dmg_mod`/`material`/`grate` args as overrides.
- `HeatmapOptions::penetration` selects the model: `simple` (the first-pass
  `∏(1 − t·mod/m_flPenetration)`) or `engine` (the model above).
- `HeatCell::base_value` is the damage *without* the walls, so a render can show the
  attenuation fraction (`value / base_value`).
- Test `aim-penetration` pins the arithmetic to hand-computed numbers.

## The render mode

`scenario/heatmap.py :: render(mode="penetration")` (driven by
`heatmap-view.py --mode penetration --pen engine`, `heatmap-live.py`'s *penetration*
mode, and the `heatmap` scenario) fills each cell with the engine-model damage, overlays
the projected solids, and outlines each cell by its attenuation fraction (blue =
untouched, red = mostly removed). `docs/ragebot.md` §0.

[^uc-svpen]: UnknownCheats, *HandleBulletPenetration* thread (2023-10) — the
    `sv_penetration_type` registration (`"What type of penetration to use. 1 = CS:GO,
    2 = CS2"`).
    <https://www.unknowncheats.me/forum/counter-strike-2-a/604186-handlebulletpenetration.html>
[^uc-release]: UnknownCheats, *[Release] Full Penetration / Autowall / Damage
    calculation* (2026-07) — the CS2 structs (`TraceData_t`, `BulletModulationEntry_t`,
    `BulletPenetrationData_t`, `CGameTrace`) and the `HandleBulletPenetration`
    signature (`48 8B C4 44 89 48 ? 48 89 50 ? 48 89 48 ? 55 57`), plus the
    penetration-count-5 note. Matches the decompile below.
    <https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html>
