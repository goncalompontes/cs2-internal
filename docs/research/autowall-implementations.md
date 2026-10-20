# Autowall / occlusion implementations — what the public code actually does

> Research note, 2026-10-19. Extends [`docs/ragebot-research.md`](../ragebot-research.md).
> Scope: CS2 and CS:GO aimbot/"autowall" code that is publicly readable, plus the
> server-side / external BVH projects that solve the same geometric problem.
>
> **Method.** Every claim below was checked against the source file it is linked to
> (raw `raw.githubusercontent.com` URLs where possible), not against a README. Where a
> repository has moved, been archived, or no longer contains the feature, that is stated.
> Anything I could not read is marked **unverified**. Forum posts are cited by thread URL;
> a forum post is not a licence grant (see §8.5).
>
> Local engine facts are from this repo: [`docs/re/penetration.md`](../re/penetration.md),
> [`docs/re/bullet-trace.md`](../re/bullet-trace.md),
> [`docs/re/occlusion.md`](../re/occlusion.md).

---

## 0. TL;DR

1. **Every public implementation is a ray query, not a field.** Not one of the
   codebases below computes a projected damage field or an arrangement. The state of
   the art is "one trace per candidate direction (or per hitbox point), then N cheap
   penetration steps".
2. **There are two distinct CS2 trace designs.** Public `CreateTrace` releases describe
   a sorted `TraceData_t.mod_array` stack and per-entry `HandleBulletPenetration`. But
   the current build's actual `FX_FireBullets` path was rechecked in Ghidra: it uses
   `TraceLineIntervals` plus the repeated advance in `FUN_18089ca80`, then processes
   modulation entries. The public one-call wrapper is not evidence that the shipped
   shot path makes one call. See [`bullet-trace.md`](../re/bullet-trace.md) for the
   current-build call graph and the still-open interval/material oracle.
3. **The bugs are consistent and instructive**: averaging enter/exit penetration
   modifiers (CS:GO v1 rule applied to CS2), `0.18` entry loss instead of `0.16`,
   advancing the ray along **Z** instead of the direction, `fraction > 0.97` as a
   visibility shortcut, and treating every pair of triangle hits as a solid span.
4. **The closest public thing to our projected field is CS2FOW's 32×32
   target-fitted depth buffer** (a projected *visibility* buffer, no damage) and a
   Nixware Lua script that *scans points* around a target. Neither builds an angular
   damage arrangement. Our approach still appears to be novel (§7).
5. **Licensing correction:** Osiris is **MIT**, not GPL-3.0, at both `master` and
   `csgo-end-of-life` ([LICENSE](https://raw.githubusercontent.com/danielkrupinski/Osiris/master/LICENSE)).
   `iCollin/csgo-internal`, `Ripper99-x/CS2-Internal`, `FrezzyHook-4` and `OneTap`
   have **no licence file** → all rights reserved, do not copy. Valthrun is **GPL-2.0**
   → ideas only. Read1dno, qo0-base, OpenSource2, CS2FOW, BvhTrace are MIT (§8.5).

---

## 1. Comparison table

| Project | Approach | Data structure / traversal | Perf notes | Licence | What we should take |
|---|---|---|---|---|---|
| **Osiris** (CS:GO `csgo-end-of-life`) [Aimbot.cpp](https://raw.githubusercontent.com/danielkrupinski/Osiris/csgo-end-of-life/Source/Hacks/Aimbot.cpp) | Engine `TraceRay` + engine's own `TraceToExit`; 4-hit loop; `canScan` only on bone points | none (engine) | 6 bones/target; one visibility trace per bone; wallbang only if not visible | **MIT** | The engine-call *shape*; the damage formula; the "visible → skip wallbang" ordering |
| **Osiris** (CS2 `master` / `cs2-release`) | **No aimbot, no autowall, no trace code at all** — visuals/HUD only | — | — | MIT | Proof that Osiris is *not* a CS2 combat reference |
| **iCollin/csgo-internal** [`hacks/autowall.cpp`](https://raw.githubusercontent.com/iCollin/csgo-internal/master/hacks/autowall.cpp) | Canonical CS:GO loop: `TraceRay` → `TraceToExit` (4-unit steps, 90 u) → `HandleBulletPenetration`, 4 walls | none | 1 trace + up to 23 point-content probes + 1 back-trace per wall | **none** (no LICENSE) | The readable reference for the *CS:GO* model — read it, don't copy it |
| **rollraw/qo0-base** [`autowall.cpp`](https://raw.githubusercontent.com/rollraw/qo0-base/master/base/features/autowall.cpp) | CS2-era CS:GO-style loop; adds `ClipTraceToPlayers`, breakables, `SURF_NODRAW`, convars, 3000 u gate | none | same as iCollin + clip trace | **MIT** | Best-documented "all the edge cases" loop; `TraceToExit` handling of breakables/NODRAW |
| **alza54/opensource2** [`aimbot.cpp`](https://raw.githubusercontent.com/alza54/opensource2/main/OpenSource2-SDK/src/features/aim_enhancement/aimbot.cpp) | `TraceShape` per head; checks `HitEntity`; **no autowall** | none | 1 trace per player per tick | MIT | `TraceShape` call convention + `C_TraceFilter` layout ([c_trace.hpp](https://raw.githubusercontent.com/alza54/opensource2/main/OpenSource2-SDK/src/sdk/source-sdk/classes/types/c_trace.hpp)) |
| **Ripper99-x/CS2-Internal** [`Tracing.h`](https://raw.githubusercontent.com/Ripper99-x/CS2-Internal/main/SourceEngine/Tracing.h) | Wraps `TraceShape`, `CreateTrace`, `HandleBulletPenetration`; aimbot uses `TraceShape` per bone only | none | 1 trace/bone, cached in entity list | **none** | Signatures + the `C_TraceFilter`/`TraceData_t` layouts; not the visibility test |
| **Read1dno/ExternalAutoWallCS2** [`VisCheck.cpp`](https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/VisCheck.cpp) | Own **triangle BVH from VPK**; collect *all* hits, pair them as enter/exit; arithmetic per span | median-split BVH, longest axis, leaf ≤ 4 tris; cached to `.bvh` | ~**1 ms/ray** in the sibling project ([VisCheckCS2](https://raw.githubusercontent.com/Read1dno/VisCheckCS2/main/README.md)) | **MIT** | The VPK→triangles pipeline; the *warning* that pairing hits is fragile |
| **UC 747109** (FOrna) [`EngineTrace / TraceShape release`](https://www.unknowncheats.me/forum/counter-strike-2-a/747109-enginetrace-traceshape-release.html) | Public `CreateTrace(pen=4)` wrapper over sorted trace results → loop `mod_array` → `GetTraceInfo` → `HandleBulletPenetration`; **not the verified current `FX_FireBullets` call path** | engine's `TraceData_t` / sorted result API | 1 wrapper call per candidate; O(#spans) arithmetic | forum (no grant) | Candidate alternative API/model; verify field semantics against the shipped shot |
| **UC 764290** (airduck) [Full Penetration / Autowall](https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html) | Same pipeline; `PenetrationCount = 5`; calls `HandleBulletPenetration` *before* the hit test; adds `ScaleDamage` | engine | same | forum | `ScaleDamage` (VData headshot multiplier, body scale convars, armour formula); the 5-vs-4 note |
| **UC 608159** (W1lliam1337) [CS2 AutoWall](https://www.unknowncheats.me/forum/counter-strike-2-a/608159-cs2-autowall-penetration-system.html) | Same pipeline (2023); includes a decompile of `HandleBulletPenetration` | engine | same | forum | The decompile that confirms `min()` over surfaces and the special cases |
| **Valthrun** `aim-assist` branch | External VPK parse + own raycast (POC); maintainer says perf needs work | own (map tracer) | "significant improvements … proof of concept" ([issue #180](https://github.com/Valthrun/valthrun-cs2/issues/180)) | **GPL-2.0** | What *not* to do at 1 ms/ray; the honest perf caveat |
| **CS2FOW** (archived) [CODE_TOUR](https://raw.githubusercontent.com/karola3vax/CS2FOW/main/docs/CODE_TOUR.md) | Server-side **BVH8** over `world_physics`, AVX, 19 capsules, **32×32 target-fitted depth buffer** | BVH8, palette-indexed per-triangle masks | claims "old trace approach ~60 ms, ~1 ms avg, ~8 ms worst" in a search-indexed README (**unverified in current README**) | MIT (bakes: `DATA_NOTICE`) | The projected-depth-buffer idea; conservative "fail open" design |
| **cs2-bvhtrace** (GitLab) [PLAN.md](https://gitlab.com/cs2-server-plugins/cs2-bvhtrace/-/raw/main/docs/PLAN.md) | Baked **masked** BVH + **swept SAT hull** queries, entity-collision layer | median/SAH-ish BVH, leaf packets, per-tri layer palette | README: de_mirage over-occlusion 9.2%→3.8%, ~2.9 ms serial for 1000 agents (search snippet; **README not re-read**) | MIT | The correctness findings: `world_physics` ≠ all solids; entity layer is mandatory |
| **FrezzyHook-4** [`Autowall.cpp`](https://raw.githubusercontent.com/sezzyaep/FrezzyHook-4/main/Autowall.cpp) | CS:GO loop + **linear memo cache** of `(point → damage)` | `std::vector` scan | O(n) lookup, unbounded growth | **none** | The only public "damage cache" — and why a linear one is a bad field |
| **Panguins/OneTap** [`autowall.cpp`](https://raw.githubusercontent.com/Panguins/OneTap/main/autowall.cpp) | CS:GO loop + `ClipTraceToPlayers`; `GetDamageToPoint`-style API | none | — | **none** | `clip_trace_to_players` maths (40 u extension) |
| **Nixware "Aimbot Visualizer"** (Lua, closed download) [workshop](https://nixware.cc/workshop/749/) | Scans points around the target for wallbang spots and draws them | none | "Scan distance … max distance"; tunable | proprietary | The *market* wants a wallbang field; it is implemented as a scan, not an arrangement |

---

## 2. Taxonomy (why these are all the same idea)

```
candidate point (bone / multipoint / scan cell)
        │
        ├─► current CS2 shot: TraceLineIntervals ─► advance/retrace ─► damage entries
        ├─► public CS2 wrapper: CreateTrace / sorted TraceShape results ─► mod_array
        ├─► CS:GO: TraceRay + TraceToExit ─────────► layer spans
        └─► own raycast ───────────────────────────► spans
              VPK/engine shapes → BVH → ray query
```

All of them answer **"what is the damage to this point?"**. None answers
**"what is the best direction?"** — that is left to a candidate loop (bones,
multipoints, or a scan). The search space is always a finite list of points, never the
angular domain.

The two engine paths differ in a way that matters for us:

- **CS:GO**: `TraceRay` stops at the first solid; you must find the exit yourself by
  marching 4 units at a time and tracing backwards. That loop is the expensive,
  fragile part (23 point-contents probes worst case per wall).
- **Public CS2 `CreateTrace` wrapper**: given the full segment, it returns modulation
  spans (`mod_array`) with fractions and surface indices, without exit-marching. This
  describes that wrapper's contract, not the current build's `FX_FireBullets` path;
  current-build RE shows the shot uses `TraceLineIntervals` and repeated advance. The
  repo's Tier-1 design must validate which API best serves its candidate oracle.

---

## 3. Deep dives

### 3.1 Osiris — the reference is not what the brief says

**Current `master` has no combat features.** The tree contains
`Source/Features/Combat/SniperRifles/NoScopeInaccuracyVis` only; there is no
`Aimbot`, `Triggerbot`, `Trace`, `Autowall` or `EngineTrace`
([tree](https://github.com/danielkrupinski/Osiris/tree/master/Source/Features)).
The `cs2-release` tag (commit `8c2d5bf`) is a 223-file visuals/HUD-only tree
([tree](https://github.com/danielkrupinski/Osiris/tree/cs2-release/Source)) — so
**Osiris is not evidence about CS2 aimbot behaviour at all**. (Earlier drafts of the
brief implied it was; it is only a CS2 *SDK/visuals* reference.)

**The CS:GO tag does have one** (`csgo-end-of-life` →
`Source/Hacks/Aimbot.cpp`). Its architecture:

- Iterates players, then bones `{8,4,3,7,6,5}`; computes the FOV with
  `hypot(dx,dy)`; rejects smoke via `lineGoesThroughSmoke`.
- `entity.isVisible(...)` first; only if invisible **and** not `visibleOnly` does it
  call `canScan` (the wallbang test) — i.e. **visibility is the fast path and the
  autowall is the fallback**. This ordering is worth copying.
- `canScan` traces with mask **`0x4600400B`**, and on a world hit calls
  `handleBulletPenetration`, which calls the **game's own `traceToExit`** through a
  memory pattern (an x86 `__asm` thunk, so the signature is exact).
- Budget: `int hitsLeft = 4`, decremented per wall, loop condition
  `damage >= 1 && hitsLeft`.
- Damage: `damage -= 11.25 / penetration / penetrationModifier + damage * damageModifier + thickness² / 24 / penetrationModifier`
  — algebraically the CS:GO form of the CS2 formula (`3/pen·1.25·3 = 11.25/pen`).
- Hitgroup multipliers: head 4.0, chest/arms 1.0, stomach 1.25, legs 0.75; armour
  `*= armorRatio*0.5` (helmet check for head only).

Caveats: this is **CS:GO v1 penetration** (average of enter/exit modifiers, 0.16
entry loss, no `min()` over surfaces); the range falloff is applied *before* the wall
loss with `fraction * range / 500`; `TraceToExit`'s
`plane.normal.dot(dir) <= 1.0` is true for almost every unit normal (it is not a real
"exited the solid" test). Also the CS:GO code we read is not the CS2 one.

**Licence: MIT** (2018–2026), verified at `master` and at `csgo-end-of-life`
([LICENSE](https://raw.githubusercontent.com/danielkrupinski/Osiris/master/LICENSE)).
The brief's "GPL-3.0" is wrong.

### 3.2 iCollin/csgo-internal — the canonical loop, and its bugs

The file the brief cites is real and is the cleanest CS:GO reference. Concretely:

- `data.penetrate_count = 4`; loop `while (penetrate_count > 0 && damage >= 1)`.
- `TraceLine(src, end, MASK_SHOT, localPlayer)`; the code then traces a *second* and
  *third* time (`Ray_t` with a filter, then `TraceLine` again) — a historical artifact;
  only the last one is used.
- On a player hitgroup `> GENERIC && <= RIGHTLEG`: apply range falloff, team check,
  `ScaleDamage`, return.
- Otherwise `HandleBulletPenetration`: reads `enter_surface_data` and
  `*(float*)((uint8_t*)surface + 76)` (the CS:GO `flPenetrationModifier` offset),
  bails if `trace_length > 3000` or modifier `< 0.1`, calls `TraceToExit`.
- `TraceToExit`: `distance += 4` while `<= 90`; `GetPointContents`; if the point is
  in a hull but not a hitbox, continue; else trace **backwards 4 units**; handle
  `startsolid && SURF_HITBOX` by tracing to the start with mask `0x600400B`; accept when
  `(fraction < 1 || allsolid) && !startsolid`; the final accept test is
  `exit_trace->plane.normal.Dot(dir) <= 1.0f` (again almost always true).
- Thickness `= |exit.endpos − enter.endpos|`; combined modifier is
  `(enter + exit) * 0.5`, with `3.0` for grate/glass (and `final = 0.05`), `3.0` for
  wood/cardboard, `2.0` for plastic.
- Loss: `max(0, 1/combined)`; `lost = damage*0.16 + (3/pen*1.25)*3*inverse + t²*inverse/24`;
  stop if `lost > damage` or `damage < 1`.

**Bugs / caveats:** the double trace; `thickness` uses `enter_trace.endpos` (which for
a *hitbox* start can be inside the player); no `ClipTraceToPlayers`, so hitboxes
outside the collision AABB are missed; `combined` is an average (CS:GO v1), not the
CS2 `min`; `damage < 1` vs `lost > damage` ordering differs slightly from the engine.
**Licence: none** — the repository has no `LICENSE` file and the GitHub API reports a
null licence. Treat it as *all rights reserved*: read for understanding, do not copy.

### 3.3 rollraw/qo0-base — the best "all edge cases" CS2-era loop

MIT, credits `outlassn`. It is still the CS:GO-shaped loop but written against a CS2
SDK, and it is the most complete public handling of the ugly cases:

- `iPenetrateCount = 4`; `while (count > 0 && damage >= 1)`.
- `TraceRay(vecPosition, end, MASK_SHOT_HULL | CONTENTS_HITBOX, filter)` **then**
  `ClipTraceToPlayers(vecPosition, end + dir*40, same mask, filter, enterTrace)`.
  The 40-unit extension is for hitboxes that poke outside the collision bounds.
- `iPenetrateCount == 0` is only fatal if the material is **not** grate/glass and
  `SURF_NODRAW` is absent (so a final grate can still be crossed).
- `TraceToExit` reproduces the CS:GO semantics including `sv_clip_penetration_traces_to_players`
  (convar added 2021), `SURF_HITBOX` start-solid handling, breakable two-way entities,
  and `SURF_NODRAW`.
- Special cases: grate/glass → `final = 0.05`, `combined = 3`; `(contents>>3)&CONTENTS_SOLID`
  or `SURF_LIGHT` → `combined = 1`; flesh/teammate path uses
  `ff_damage_bullet_penetration`; same-material wood/cardboard → 3, plastic → 2.
- Uses `LengthSqr()` and comments that the game converts back to squared.
- `ScaleDamage` is the good version: `mp_damage_scale_{ct,t}_head/body` convars,
  heavy armour (`0.25/0.33/0.20`), `damage_to_armor > armor` clamp.

Caveat it admits in a comment: *"[side change] we only handle new penetration method
and expect that `sv_penetration_type` is 1"* — the comment is wrong for CS2 (the live
value is 2, per [`docs/re/penetration.md`](../re/penetration.md)), and the code still
uses the CS:GO average/`0.16` arithmetic, so it is **not** the CS2 formula despite the
SDK. This is a common confusion in the 2024-era bases.

### 3.4 alza54/opensource2 — visibility only

`AimBot::IsVisibleByTraceLine` builds `C_TraceFilter(0x1C3003, localPawn, nullptr, 4)`,
calls `fn::TraceShape(EngineTrace, &ray, &start, &end, &filter, &trace)`, and returns
`trace.HitEntity && trace.HitEntity->GetRefEHandle().GetEntryIndex() == iEntryIndex`.
No penetration, no autowall, no BVH. Useful for: the exact `TraceShape` argument order
and the `C_GameTrace`/`C_TraceFilter` layouts
([c_trace.hpp](https://raw.githubusercontent.com/alza54/opensource2/main/OpenSource2-SDK/src/sdk/source-sdk/classes/types/c_trace.hpp)).
Note the mask: `0x1C3003` is the *visibility* mask; the bullet/autowall mask is
`0x1c300b` ([`docs/re/bullet-trace.md`](../re/bullet-trace.md) §"the trace filter").
Using `0x1C3003` for a wallbang is a real, repeatable bug (see the UC thread where
changing to `0x1C100B` "works perfectly"
([751031](https://www.unknowncheats.me/forum/counter-strike-2-a/751031-autowall-printing-0-damage.html))).

### 3.5 Ripper99-x/CS2-Internal — signatures, not correctness

`Tracing.h` wraps `InitTraceInfo`, `InitFilter`, `ClipTraceToPlayers`, `GetTraceInfo`,
`HandleBulletPenetration`, `CreateTrace`, `TraceShape` — including the useful
`CreateTrace(trace, start, end, filter, penetrationCount)` and a 9-argument
`HandleBulletPenetration`. But `Aimbot.cpp` / `Entity.cpp` only call `TraceShape` per
bone and set `RayTraceVisible`; the mask is `0x1C3003` and the test is

```cpp
return Trace.HitEntity && Trace.HitEntity->GetRefEHandle().GetEntryIndex() == Enemy->... || Trace.Fraction > 0.97f;
```

Operator precedence makes `fraction > 0.97` sufficient on its own, which reports
"visible" for a ray that ends 97 % of the way even if the last 3 % is a wall (and for
a start-solid trace where the fraction can be 1 with no hit entity). **Do not copy
this test.** No `LICENSE` file → unlicensed.

### 3.6 Read1dno/ExternalAutoWallCS2 — the only VPK-BVH autowall

Pipeline (all from [VisCheck.cpp](https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/VisCheck.cpp)):

1. `Parser` reads `world_physics` (`m_meshes`, `m_hulls`) from the extracted `.vphys`,
   converting hex blobs to `TriangleCombined{v0,v1,v2,materialIndex}`
   ([Parser.h](https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/Parser.h)).
   Hulls are turned into **triangles from their AABB** (`generateTrianglesFromAABB`),
   so every convex brush is approximated by a box.
2. `BuildBVH`: bounds = union of triangle AABBs; split on the **longest axis at the
   median of triangle-centre coordinates**; leaf at ≤ 4 triangles; recurse. Cached to
   `<phys>.bvh`.
3. `CollectIntersections` traverses *every* node whose AABB the ray hits and pushes
   **every** triangle hit `(t, material)`, then sorts by `t`.
4. `IsPointVisible` pairs hits `(0,1), (2,3), …` into `(gap, entryMat, thickness, exitMat)`.
5. `handle_bullet_penetration` walks those spans and applies the CS:GO arithmetic.

Concrete defects (all visible in the source):

- **Pairing is not solid detection.** Two overlapping brushes, a doorway, a mesh with
  inverted triangles, or a ray that starts inside a solid produce 3, 4, or 1 hits and
  the pairing shifts for the rest of the ray. The README itself warns about "malformed
  geometry and inverted triangles".
- **`inside_distance *= 2.54f`** — an unexplained inch→cm conversion; CS units are
  inches, and the engine uses the raw units.
- **`current_pos.z += inside_distance`** — advances the ray along world Z, not along
  the shot direction. Correct only for a perfectly vertical shot.
- **`combined_pen_mod = (entry + exit) / 2`** — CS:GO v1, not CS2 `min`.
- **`0.18` entry loss** (CS:GO v1), not `0.16`; no 4-wall budget; no 90-unit cap; no
  3000-unit gate; no `entry_fraction` for grates/glass.
- Arms/legs/armour are not modelled at all.

Its sibling [VisCheckCS2 README](https://raw.githubusercontent.com/Read1dno/VisCheckCS2/main/README.md)
states **~1 ms per ray**. That is the honest cost of a naive median BVH + Python
boundary + collect-all-hits; a competent packet BVH should be 2–3 orders of magnitude
faster (§5). Both projects are **MIT**.

### 3.7 The UC CS2 engine-pipeline lineage (747109 → 608159 → 764290)

This is the most important architectural finding. FOrna's release
([747109](https://www.unknowncheats.me/forum/counter-strike-2-a/747109-enginetrace-traceshape-release.html))
establishes the shape:

```cpp
InitTraceData(&td);
InitFilter(&filter, skipPawn, MASK_AUTOWALL, /*traceType*/4, /*filterType*/7);
CreateTrace(&td, start, dir * range, &filter, /*penetrationCount*/4, true);

for (i = 0; i < td.mod_array.size; ++i) {
    entry = &td.mod_array.data[i];
    surf  = &td.m_Arr[entry->surfEnd & 0x7FFF];
    InitTraceInfo(&tr);
    GetTraceInfo(&td, &tr, entry->startFrac, surf);
    if (tr.m_pHitEntity == target) { /* damage * falloff */ }
    if (HandleBulletPenetration(&td, &bullet, entry, team, nullptr)) break;
    if (bullet.penetrationCount <= 0 || bullet.penetrationStopped) break;
}
```

Key points, each visible in the post:

- **Within FOrna's public wrapper, `CreateTrace` is the whole trace.** `mod_array` is
  the sorted stack; no `TraceRay`/`TraceToExit` loop exists in that wrapper. This does
  not describe the current `FX_FireBullets` implementation (see §4.0 and
  [`bullet-trace.md`](../re/bullet-trace.md)).
- **`penetrationCount` is passed twice**: as the 5th argument to `CreateTrace` (4) and
  in `handle_bullet_data_t` (4). airduck's later post uses **5** and says it fixes
  multi-wall cases (the "loop 10 times but run 9 or 11" off-by-one)
  ([764290](https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html)).
  This 4-vs-5 difference belongs to the public wrapper lineage. The current shot path's
  `FUN_18089ca80` span limit is separately re-derived as 4 in
  [`docs/re/penetration.md`](../re/penetration.md); do not transfer the wrapper's
  `penetrationCount` argument to that call path.
- **Order of the hit test vs the penetration call differs** between the two releases.
  airduck deliberately calls `HandleBulletPenetration` *before* checking the hit
  entity; FOrna checks the entity first. This changes how many spans are consumed
  before the target's span. The safe implementation is: test the target first, but
  keep the budget/bookkeeping identical to the engine's caller (documented, unit-tested).
- FOrna's `GetDamage` returns `finalDmg * 4.0f` — a **hardcoded SSG headshot
  multiplier** — and hardcodes `damage=88, penetration=2.5, rm=0.85, range=8192`.
  airduck's version reads real VData and has a proper `ScaleDamage` (weapon
  `m_flHeadshotMultiplier`, per-team body/head convars, armour with
  `armor_ratio = m_flArmorRatio*0.5`, heavy-armour variant). Use airduck's.
- W1lliam1337's 2023 post ([608159](https://www.unknowncheats.me/forum/counter-strike-2-a/608159-cs2-autowall-penetration-system.html))
  is the same pipeline and includes the `HandleBulletPenetration` decompile that
  confirms:
  - `combined = min(surface.modifier)` over `SurfaceStart..SurfaceEnd` (the loop is
    visible in the listing: `combined_penetration_modifier = fminf(..., combined)`);
  - same-material special cases via `((exit_material - 85) & 0xFFFFFFFD) != 0`
    (85 wood / 87 cardboard → 3.0; 76 plastic → 2.0);
  - `0.16` entry loss, `1/24` thickness term, `3.75/pen * 3` power term;
  - the **showimpacts debug path uses different constants** (`3.54/pen`, `2.8`,
    `0.15`) — do not mine those numbers.
- The known failure mode is reported in
  [747821](https://www.unknowncheats.me/forum/counter-strike-2-a/747821-correct-call-getdamagetopoint-autowall-object-param.html):
  every `mod_array` entry reports the **world entity** as `HitEntity`, never the
  player, when the trace is issued with the wrong mask / filter arguments. The engine
  also has a `GetDamageToPoint` (`4C 8B DC 53 55 41`) but the thread reports it always
  returns failure; **its call convention is unverified**.

### 3.8 The BVH family (external / server-side): correctness lessons, not code

These do not implement autowall, but they are the only public evidence about *how
accurate a static-geometry BVH can be* and *what it costs*.

- **CS2FOW** ([CODE_TOUR](https://raw.githubusercontent.com/karola3vax/CS2FOW/main/docs/CODE_TOUR.md)):
  bakes `world_physics` from the VPK into **BVH8** packets, with per-triangle
  interaction-layer masks; at runtime it projects **19 hitbox capsules** into a
  **32×32 target-fitted CPU depth buffer** and only falls back to 8 AABB corners +
  muzzle ray when the whole silhouette is blocked; smoke is copied live and an HE can
  clear a gap. It also enumerates **five recipient origins** (eye, RTT-scaled
  shoulders, +16 u, feet) — i.e. it solves visibility for a *set of rays*, not one.
  Runtime is a worker thread, immutable published matrix, "fail open" on any doubt.
  This is the closest public design to "a field" — and it is still a raster of
  *visibility*, with no damage dimension.
- **cs2-bvhtrace** ([PLAN.md](https://gitlab.com/cs2-server-plugins/cs2-bvhtrace/-/raw/main/docs/PLAN.md)):
  the most useful correctness document. Findings:
  - `world_physics` is only the **Static** object-set bucket; doors, `func_wall`,
    breakables, movers and `prop_physics` carry collision in **per-entity `.vphys`**
    — "593 model-bearing entities / 23 classes" on one map. A static-only bake clips
    through all of them.
  - clip-brush semantics (playerclip/npcclip) must be classified per triangle; unknown
    tags must **fail solid**, never fail open.
  - a live A/B diff against the engine trace is mandatory; the README reports
    de_mirage over-occlusion **9.2 % → 3.8 %** after fixes (search-indexed README;
    **the current README was not re-read — treat as unverified**).
  - hull sweeps are 13-axis swept SAT, ~2.9 ms serial for 1000 agents (≈3 µs/sweep)
    per the same source.
- **Valthrun `aim-assist`**: VPK parse + own raycast; the maintainer's own comment is
  that it "still requires significant improvements … proof of concept"
  ([issue #180](https://github.com/Valthrun/valthrun-cs2/issues/180)). The repo is
  **GPL-2.0**; the branch inherits it. Ideas only.

**Common lesson:** a static VPK bake is *not* the engine's solid set. Our payload
already avoids this by enumerating live shapes through `EntitiesInBox`
([`docs/re/occlusion.md`](../re/occlusion.md)); that is a real advantage — do not
trade it for a VPK bake.

### 3.9 CS:GO-era extras worth knowing

- **FrezzyHook-4** ([Autowall.cpp](https://raw.githubusercontent.com/sezzyaep/FrezzyHook-4/main/Autowall.cpp))
  is the only public code with a **damage cache**: `scanned_points` / `scanned_damage`
  are parallel `std::vector`s, linearly scanned with exact `Vector::operator==`
  before recomputing. It never evicts, is keyed on the exact endpoint, and is not
  thread-safe. It is a memo, not a field. (Also: `thickness` is `LengthSqr()`, and
  `0.18` entry loss; unlicensed.)
- **OneTap** ([autowall.cpp](https://raw.githubusercontent.com/Panguins/OneTap/main/autowall.cpp))
  has a clean `clip_trace_to_players` with the 40-unit extension and a
  `length² · inverse · 0.0416` thickness term; unlicensed.
- **Nixware's Lua visualizer** ([workshop](https://nixware.cc/workshop/749/)) is the
  only public artefact that tries to show *where* a wallbang exists: it scans points
  around the target ("Scan distance is how far to search for the edge … max distance
  is the total radius it can scan up to") and draws green/yellow/red circles with
  damage text. Source is behind a login ("No permission to download"); behaviour is
  **verified only from the description**. It is a 1-D/2-D point scan, not an
  arrangement.

---

## 4. Canonical algorithm

### 4.0 Which engine entry point does what

There are two different "trace" APIs in `client.dll` and the public code mixes them
up. From our RE ([`bullet-trace.md`](../re/bullet-trace.md)) and the public releases:

| Entry point | Returns | Use it for |
|---|---|---|
| `CGamePhysicsQueryInterface::TraceShape` (`0x180a1a000`, vtable `+0x558`) | **first** hit only (`CGameTrace`: entity, hitbox, surface, fraction, end) | visibility; a single-point damage estimate is *not* enough |
| `TraceShapeWithSortedResults` (`0x180a1af60`, `+0x560`) | sorted raw hits converted to `0xc0`-byte trace records; distinct from the interval API | candidate sorted-hit stack; not used by the verified `FX_FireBullets` path |
| `TraceLineIntervals` (`0x180a19e90`) | `0x38`-byte interval records (distance `+0x0C`, enter/exit `+0x32`); **full material/surface layout remains unmapped** | used by the actual shot, with repeated advance/retrace in `FUN_18089ca80` |
| `EntitiesInBox` / `EntitiesAlongLinearSweep` | body list (`0x70` records), then shapes | broad phase / our current occluder enumeration ([`occlusion.md`](../re/occlusion.md)) |
| Public `CreateTrace` + `GetTraceInfo` + `HandleBulletPenetration` | community-described whole sorted surface stack + per-span arithmetic | an alternative wrapper/model; do not label it the current shot implementation |
| `ClipTraceToPlayers` | fixes up a trace with player hitboxes outside the collision AABB | CS:GO-era capsule mismatch; CS2's `CreateTrace` includes hitboxes via the mask |

The important distinction: **`TraceShape` is a first-hit query**. Public `CreateTrace`
code wraps sorted-result APIs, but the current build's actual shot path is different:
`FX_FireBullets` calls `TraceLineIntervals` through `FUN_1808830e0` and the repeated
advance in `FUN_18089ca80`. The sorted-results wrapper may still be useful for a new
candidate oracle, but its equivalence to the shot's intervals, surfaces, and stop rules
is not established.

### 4.1 Public `CreateTrace` whole-stack model (not the verified shot path)

The following pseudocode summarizes the public UC release lineage, **not** a confirmed
decompilation of the current build's `FX_FireBullets` call path. Types and offsets are
from the public releases and this repo's separate `HandleBulletPenetration` analysis
([`docs/re/penetration.md`](../re/penetration.md),
[`docs/re/bullet-trace.md`](../re/bullet-trace.md)).

```
struct BulletPenetrationData {      // 0x18
    float damage;                   // +0x00
    float penetration;              // +0x04   weapon m_flPenetration
    float range_modifier;           // +0x08   weapon m_flRangeModifier
    float tail_length;              // +0x0C   distance travelled so far
    int   penetration_count;        // +0x10
    bool  penetration_stopped;      // +0x14
};

DamageToPoint(eye, point, weapon, target, skip_pawn):
    dir = normalize(point - eye)

    td = {}; InitTraceData(&td)                       // 0x1D60-ish scratch
    InitFilter(&filter, skip_pawn, MASK_AUTOWALL, 4, 7)
    CreateTrace(&td, eye, dir * weapon.range, &filter, /*count*/ 4, true)

    b = { weapon.damage, weapon.penetration, weapon.range_modifier,
          0, /*count*/ 5, false }

    for i in 0 .. td.modulate_count-1:
        e = td.modulate_entries[i]
        s = e.surface_end & 0x7FFF
        if s >= 0x80: break
        tr = {}; InitTraceInfo(&tr)
        GetTraceInfo(&td, &tr, e.start_fraction, &td.trace_array[s])
        b.tail_length = length(td.tail_end)

        if tr.hit_entity == target:
            d = b.damage * pow(weapon.range_modifier,
                               length(tr.end - eye) / 500.0)
            return ScaleDamage(d, tr.hitbox.hitgroup, target, weapon)

        if HandleBulletPenetration(&td, &b, &e, /*team*/ 3, null):
            break                                  // blocked
        if b.penetration_count <= 0 or b.penetration_stopped:
            break
    return 0
```

`HandleBulletPenetration` over one span (from the decompile in
[608159](https://www.unknowncheats.me/forum/counter-strike-2-a/608159-cs2-autowall-penetration-system.html)
and our [reimplementation](../../payload/aim/src/penetration.cpp)):

```
if (entry.flags & 1) == 0:                 // not solid: free flight
    damage *= pow(range_modifier,
                  (tail_length*entry.end_fraction - tail_length*entry.start_fraction)/500)
    return 0

mod = surface_modifier(entry.surface_start)
if tail_length * entry.end_fraction > 3000 or mod < 0.1:
    penetration_count = 0

for j in surface_start .. surface_end:
    mod = min(mod, surface_modifier(j))

t = thickness(entry)                       // |exit - enter|, world units

entry_loss = 0.16
if mod >= 0.1 and enter_material == exit_material:
    if material in {85 cardboard, 87 wood}: mod = 3.0
    elif material == 76 plastic:            mod = 2.0
    if t >= 6.0:
        if grate_bits:                      mod = 3.0
    else:
        if material in {71 grate, 89 glass}: entry_loss = 0.05; mod = 3.0
        if grate_bits:                       mod = 32.0; entry_loss = 1e-5

inverse = max(0, 1/mod)
power   = max(0, (3/penetration) * 1.25)
lost    = t² * inverse / 24 + power * inverse * 3 + entry_loss * damage

damage -= max(0, lost)
if damage < 1: stopped = true
else: --penetration_count
```

Notes that are easy to get wrong:

- `t` (thickness) is `|exit − enter|` **of the span**, in world units. The engine
  already knows the exit; there is no walking.
- `mod` starts from the **entry** surface and is `min()`-reduced over **all** surfaces
  in `[SurfaceStart, SurfaceEnd]` — not `min(enter, exit)` and definitely not the
  average. For a single brush the two coincide; for a mesh cluster or a compound they
  do not. Our `wall_params()` currently does `min(enter, exit)`
  ([`penetration.cpp`](../../payload/aim/src/penetration.cpp)) — correct for one
  surface per brush, wrong if a span covers several surfaces.
- The special cases only apply when `enter_material == exit_material`; grates also
  need the `grate` bits.
- `0.16` / `0.05` / `1e-5` are **entry fractions of the current damage**, and the
  current damage is the damage *at that wall* (falloff applied up to it). Our code
  applies falloff once at the target; that is exact for the proportional term but not
  for the thickness/power terms or the `< 1` gate (see §8.4, item 1).

### 4.2 Payload-side trace loop (no engine call) — resume from exit

If we keep our own ray caster, the shape is:

```
DamageAlongRay(eye, dir, distance, weapon, params):
    // 1. one BVH ray query, nearest-first, clipped to [0, distance]
    spans = query_spans(eye, dir, distance)      // OccluderCell::ray_span over AabbBvh
    spans.sort(by entry)

    damage   = weapon.damage
    travelled = 0
    walls    = 0
    crossed  = 0

    for s in spans:
        // 2. free flight before the wall
        damage *= pow(weapon.range_modifier, (s.entry - travelled) / 500)
        travelled = s.entry
        if travelled > params.max_trace_length: return 0        // 3000
        if s.enter_modifier < 0.1:               return 0
        if walls >= params.max_penetrations:     return 0        // 4
        if crossed + s.thickness > params.max_total_thickness: return 0  // 90

        // 3. the wall
        dmg = ApplyWall(damage, s, weapon.penetration)
        if dmg is none: return 0                                 // < 1
        damage = dmg
        crossed += s.thickness
        walls  += 1
        travelled = s.exit

    // 4. free flight to the hitbox
    damage *= pow(weapon.range_modifier, (distance - travelled) / 500)
    return damage
```

Invariants worth testing:

- `ApplyWall` must be the same function used by the heatmap and the debug renderer.
- Range falloff is multiplicative, so applying it per segment and once at the end are
  equivalent **only if** the wall loss is applied after the correct segment falloff.
  Applying falloff once at the target *before* the wall losses is not equivalent
  (the entry fraction then acts on too-small a number).
- `spans` must be `ray_span` results, not projected silhouette membership: a fragment
  whose centroid direction misses a solid can still be partly covered by it. That is
  why `fragment_layers` currently span-tests each covering occluder
  ([`damage.cpp`](../../payload/aim/src/damage.cpp)) and why per-candidate rays are
  exact by construction.

### 4.3 CS:GO `TraceToExit` (for reference only)

```
dist = 0
while dist <= 90:
    dist += 4
    end = start + dir*dist
    c = GetPointContents(end, MASK_SHOT_HULL | CONTENTS_HITBOX)
    if (c & MASK_SHOT_HULL) and !(c & CONTENTS_HITBOX): continue
    TraceRay(end, end - dir*4, MASK_SHOT, exit)       // backwards 4 u
    if exit.startsolid and exit.surface.flags & SURF_HITBOX:
        TraceRay(end, start, 0x600400B, skip=exit.entity, exit)
        if (exit.fraction < 1 or exit.allsolid) and !exit.startsolid:
            return end = exit.endpos
        continue
    if !(exit.fraction < 1 or exit.allsolid or exit.startsolid) or exit.startsolid:
        if enter.entity == world and isBreakable(enter.entity): ...
        continue
    if exit.surface.flags & SURF_NODRAW and !(enter.surface.flags & SURF_NODRAW): continue
    if exit.plane.normal.dot(dir) <= 1.0:
        return end - dir * (exit.fraction * 4)
return false
```

This is the loop our `docs/ragebot-research.md` describes; it is **obsolete for CS2**
(`sv_penetration_type 2`, [604186](https://www.unknowncheats.me/forum/counter-strike-2-a/604186-handlebulletpenetration.html)).
Keep it only as a fallback/oracle for CS:GO maps, not as our model.

### 4.4 Damage scaling (after the bullet arrives)

From airduck's `ScaleDamage` ([764290](https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html)):

```
dmg *= hitgroup_multiplier(hitgroup, weapon.headshot_multiplier)
   head: weapon.m_flHeadshotMultiplier
   chest/arms/neck: 1.0
   stomach: 1.25
   legs: 0.75
   (per-team mp_damage_scale_{ct,t}_{head,body} additionally multiply)

if armored(hitgroup, has_helmet, armor > 0):
    ratio = weapon.m_flArmorRatio * 0.5
    to_health = dmg * ratio
    to_armor  = (dmg - to_health) * 0.5
    if to_armor > armor: to_health = dmg - armor / 0.5
    dmg = to_health
```

Heavy armour changes `0.5 → 0.33`, `ratio *= 0.20`, and head scale `*= 0.5`
(qo0-base's version; [autowall.cpp](https://raw.githubusercontent.com/rollraw/qo0-base/master/base/features/autowall.cpp)).
The `4×` head multiplier in Osiris/iCollin is CS:GO; CS2 uses the weapon's
`m_flHeadshotMultiplier`.

---

## 5. Data structures and performance

| Structure | Where | Build | Query | Published cost |
|---|---|---|---|---|
| Engine physics world (BVH internal) | Valve | Valve | `TraceShape` / sorted results / `TraceLineIntervals` | implementation not published; the current shot uses interval queries and repeated advance |
| Median-split triangle BVH, leaf 4 | Read1dno | longest axis, median centre, cached `.bvh` | collect **all** hits, sort | ~1 ms/ray ([VisCheckCS2](https://raw.githubusercontent.com/Read1dno/VisCheckCS2/main/README.md)) |
| Our `AabbBvh` (median split over hull AABBs) | `payload/aim/src/bvh.cpp` | once per level | `query_pyramid`, O(log n + k) | 1792 → 14 hulls at one spot ([`ragebot-perf.md`](../ragebot-perf.md) §3.7) |
| Our `MeshBvh` (median split over triangles) | `payload/aim/src/bvh.cpp` | at parse | O(log n) `ray_span` | replaced O(2048) scan per ray |
| BVH8 + per-tri layer palette | CS2FOW | offline bake, CRC-validated | AVX packet traversal | 1 ms avg / 8 ms worst claimed (**unverified**) |
| Baked masked BVH + swept SAT | cs2-bvhtrace | offline, cache v2 | 13-axis SAT, ordered descent | ~3 µs/sweep (search snippet) |
| No acceleration | Osiris, iCollin, qo0, UC posts | — | engine | public wrappers: 1 trace/candidate; current shot path: interval/retrace loop |

**What this means for a 50-candidate solve.** A public `CreateTrace`-style candidate
oracle would make 50 sorted-result calls if that API is validated for our objective.
The shipped shot instead uses the interval/retrace path. Our own caster would make 50
BVH queries, each aggregating the candidate's geometric spans. None of these costs has
been measured on a saved scene; the current measured hot-path cost is still the
**arrangement**, not a validated replacement trace path.

---

## 6. Caveats catalogue

### 6.1 Engine/model

- **`sv_penetration_type`** is 2 in CS2; the CS:GO `TraceToExit` loop and the
  average/0.18 arithmetic are dead code paths. Threads that "work" with them are
  approximating.
- **Budget off-by-one**: engine caller uses 5 while the design intent is 4 walls; the
  public CS2 code is split between 4 and 5. Validate live, per weapon class.
- **`min` vs average**: CS2 takes the minimum over the span's surfaces. Averaging is
  wrong for multi-surface spans and for differing enter/exit materials.
- **`0.16` vs `0.18`/`0.15`**: 0.16 is the CS2 entry fraction; 0.05 (grate/glass) and
  1e-5 (grate contents) are the special cases. The `3.54`/`2.8` numbers in the
  showimpacts branch of the decompile are debug-only.
- **3000 u** is a *trace* gate (the shot has travelled too far to keep penetrating),
  separate from weapon range falloff and from the 90 u *thickness* cap.
- **Range falloff ordering**: entry loss is proportional to damage, so falloff must be
  applied to the segment before the wall, not once at the target.
- **Start-solid / all-solid**: neither public loop handles a trace that begins inside
  a solid (spawn in a wall, clipped geometry) beyond `startsolid` checks in
  `TraceToExit`. The engine pipeline handles it via the span flags; our ray caster
  should return "blocked" for `entry <= epsilon` unless the solid is the shooter's own.

### 6.2 Geometry

- **Static VPK bakes miss entity-carried solids** (doors, `func_wall`, breakables,
  movers, props) — 593 model-bearing entities on one map
  ([PLAN.md](https://gitlab.com/cs2-server-plugins/cs2-bvhtrace/-/raw/main/docs/PLAN.md)).
  Our live `EntitiesInBox` enumeration does not have this bug; keep it.
- **Meshes are not brushes**: a large merged mesh spans the map. The payload splits
  into ≤2048-triangle clusters and BVHs them ([`bullet-trace.md`](../re/bullet-trace.md)).
  Read1dno does neither (it pairs triangle hits), which is why it needs a README
  warning.
- **Convex hull vs AABB**: Read1dno generates hull triangles from the AABB — a box is
  a conservative *over*-estimate for a slanted brush, so it can invent walls.
- **Silhouette membership ≠ ray hit**: a fragment's centroid ray can miss a solid
  that covers part of the fragment. Per-candidate rays avoid the question entirely.
- **Compound shapes (type 4)** are still skipped in our enumeration
  ([`occlusion.md`](../re/occlusion.md) "Still to pin").
- **Mesh scale** is assumed uniform (`shape+0xB8`); non-uniform scale is unmodelled.

### 6.3 Gameplay state

- **Smoke** is not geometry in any of these implementations. Osiris CS:GO uses
  `lineGoesThroughSmoke` as a separate boolean; CS2FOW models smoke as a live copied
  occluder with HE clearing. Our ragebot must keep smoke as a separate gate (and a
  wallbang through smoke is still a wallbang — do not conflate).
- **Glass/grate** are materials in the penetration model, but *breakable* glass is a
  dynamic entity whose collision disappears after the first shot. A cached world
  silhouette will keep treating it as solid.
- **Player colliders**: hitboxes extend outside the collision AABB. CS:GO loops add
  `ClipTraceToPlayers` with a 40 u (iCollin/qo0) or 48–60 u (Frezzy/OneTap) extension;
  CS2's `CreateTrace` with `CONTENTS_HITBOX` includes them. Our hitbox silhouettes are
  the authority, so this is only relevant if we ever trace the world only.
- **Armour/hitgroup** must be applied *after* penetration, with the weapon's real
  headshot multiplier and the per-team convars — not a hardcoded 4×.
- **Lag-comp/backtrack**: every damage number is for a *pose*. The public autowalls
  all evaluate the current pose; our record loop is already ahead here
  ([`docs/re/lagcomp.md`](../re/lagcomp.md)).
- **Trace mask**: `0x1c300b` for the bullet; `0x1C3003`/`0x1C100B` appear in public
  code and at least one thread confirms `0x1C100B` fixes a zero-damage autowall
  ([751031](https://www.unknowncheats.me/forum/counter-strike-2-a/751031-autowall-printing-0-damage.html)).
  Our live RE says `0x1c300b` ([`bullet-trace.md`](../re/bullet-trace.md)); keep that.

---

## 7. What nobody does

**No public CS2/CS:GO implementation builds a projected damage field.** I searched
GitHub (repository search for `cs2 autowall`, `cs2 penetration`, `cs2 bvh`), the
UnknownCheats CS2 section (autowall/penetration/trace threads), and the general web
for "damage field / damage map / visibility polygon aimbot". What exists is:

1. **Per-point evaluation** (bones, multipoints, hitbox centres) — Osiris, iCollin,
   qo0, OpenSource2, Ripper99, every UC release.
2. **Point scanning around a target** — Nixware's Lua visualizer searches outward
   from a spot until it finds a wallbang ("scan distance … max distance"), drawing the
   result. This is the closest *product* analogue, but it is a 1-D search, not a
   partition of the angular domain, and the source is not downloadable.
3. **A projected depth buffer for visibility** — CS2FOW rasterises 19 capsules into a
   32×32 target-fitted depth view and queries it. This is the same *machinery* as a
   field (project occluders, compare depth) but the output is a boolean visibility
   mask per target, with no damage values and no aim-direction optimisation.
4. **A memo cache** — FrezzyHook-4 stores `(endpoint → damage)` in two linear vectors.
   No spatial structure, no interpolation, no eviction.

So our `D(u)` formulation — partition the angular chart by occluder silhouettes,
attach an exact per-cell damage, and optimise over cells — is not present in the
public corpus. The **only** public acknowledgement that the problem is
"where can I shoot through" rather than "can I shoot this point" is the Nixware
script and CS2FOW's silhouette buffer, and neither does the damage algebra.

The geometry itself is not novel: partitioning the plane by the projections of
obstacles is the classical **visibility polygon / visibility decomposition** of
computational geometry, and the same "critical points where visibility changes are
the projections of obstacle vertices" argument appears in recent robotics work
(e.g. *Visibility in Polygonal Environments with Holes*, arXiv 2601.22405,
<https://ar5iv.labs.arxiv.org/html/2601.22405>). What appears to be novel here is
using that decomposition as a **damage** field (occluder thickness and material per
cell, weapon falloff, cone mass), rather than a binary visibility region. Treat
"novel" as "not found in public CS code", not as "unpublished anywhere".

**How would a public implementation keep a field fast if it had one?** The evidence
suggests the pieces they would use:

- CS2FOW: low-resolution target-fitted raster (32×32) instead of exact polygons, with
  early-out when the silhouette is fully blocked, and a worker thread with a fixed
  cycle budget (75 ms).
- cs2-bvhtrace: bake once, palette-indexed per-triangle masks, ordered closest-hit
  descent, and a *packet cache* keyed by the pair ("reuses the triangle packet that
  blocked the same pair's earlier muzzle ray").
- BvhTrace: per-tick immutable snapshots so workers never touch engine objects.

These are all compatible with our design and are worth stealing as *implementation*
tactics (cache the blocking solid per (eye-cell, candidate) pair; evaluate lazily),
not as a replacement for the field.

---

## 8. Recommendation for our payload

### 8.1 Keep the field, change what is exact

The arrangement is the right *objective*, but `occlude_polygon` computes the entire
partition whether or not any candidate needs it. Two exact, incremental options:

**A. Lazy field (recommended first step).** Evaluate `DamageAlongRay` only at the
finite candidate set `field_candidates` already produces (hitbox centres, vertices,
Chebyshev centres, axis points). Then refine only where two adjacent candidates
disagree: the discontinuity is an occluder silhouette edge; bisect between the two
directions to the tolerance the cone objective needs (a few tenths of a degree), and
insert the resulting cells. Cost is `O(candidates × spans)` plus refinement, and the
result is identical to the arrangement at the sampled resolution (the same argument
Tier 1 in [`ragebot.md`](../ragebot.md) §4 makes). This removes the fragment
explosion without giving up "best aimpoint".

**B. Keep the arrangement for debug/heatmap only.** The renderer already runs off the
hot path ([`ragebot-research.md`](../ragebot-research.md) §5.1). Do not delete it;
gate it behind the heatmap command.

**C. If we ever need the full field per tick**, build it in *screen/chart space* at a
bounded resolution (CS2FOW's 32×32 is the existence proof) and only for the damage
values that can beat the current best (the `value_floor` pruning already in
`field_candidates`). But measure A first; it should be enough.

**Cost sanity check (estimate, not measured on our machine).** A `ray_span` is a
half-space clip for a hull (tens of ns) or a BVH traversal for a mesh (~0.1–1 µs in
the published external code, which is ~1 ms/ray only because it collects *all*
triangles in Python and sorts them). 57 hitboxes × ~6 candidates each ≈ 340 rays ×
~5 spans is O(10 k) spans — order 0.1–1 ms serial, and far less with the existing
`query_pyramid` narrowing (14 hulls at the measured spot). The 0.6 s figure comes
from 15 670 fragment polygons, not from the rays. Use the host benchmark over a saved
cell set ([`ragebot-perf.md`](../ragebot-perf.md) §0) to replace this estimate with a
number.

### 8.2 Copy conceptually

- **Aggregate all relevant spans per candidate.** Our `ray_span` returns one solid's
  interval; a BVH query can gather candidate intervals nearest-first. A single engine
  `TraceShapeWithSortedResults` call is another candidate, but it is not the verified
  `FX_FireBullets` path and is not yet proven equivalent for damage/materials.
- **Nearest-first + exact early-out** (already in `occlude_polygon`'s blocked-fragment
  pruning): stop as soon as the 4-wall budget / 90 u / `< 1` damage / `< 0.1`
  modifier gates fail. This is exact because adding walls only decreases damage.
- **Validate against the shipped shot path.** Compare candidate traces and damage on
  the current build using the actual `TraceLineIntervals`/advance path. UC 747109/764290
  can be a separate sorted-results comparison, not the sole engine oracle. Same eye and
  point; compare ordered spans and damage on saved geometry.
- **Material data from the engine**, not from a VPK: ours already reads
  `shape+0x48 → +0x10 → SurfacePropertiesList` ([`penetration.md`](../re/penetration.md)).
- **Conservative visibility ordering** (Osiris): visible → shoot; invisible →
  wallbang. Never pay for an autowall on a visible target.
- **`ScaleDamage`** with the weapon's `m_flHeadshotMultiplier` and the team convars,
  not 4×.

### 8.3 Avoid

- Do **not** adopt `TraceToExit` or the CS:GO `0.18`/average arithmetic for CS2. The
  live game is `sv_penetration_type 2`.
- Do **not** use `Trace.Fraction > 0.97` as a visibility oracle (Ripper99, FOrna,
  Rust port). It reports visible for a ray that hits a wall in the last 3 % and for
  start-solid traces.
- Do **not** pair triangle intersections into spans (Read1dno). It is wrong for
  overlapping brushes, open meshes, and rays starting inside geometry.
- Do **not** advance the ray along `z` (Read1dno) or convert units by `2.54`.
- Do **not** replace our live shape enumeration with a VPK bake: static bakes miss
  entity-carried solids (doors/walls/breakables), and our maps are live.
- Do **not** copy `occlude_polygon`'s capped `max_occluders` into the new path; it was
  a documented stopgap ([`ragebot-perf.md`](../ragebot-perf.md) §3.7).
- Do **not** use `GetDamageToPoint` until its call convention is reversed; the public
  report is that it always fails ([747821](https://www.unknowncheats.me/forum/counter-strike-2-a/747821-correct-call-getdamagetopoint-autowall-object-param.html)).
- Do **not** trust the `3.54`/`2.8`/`0.15` constants from the decompiled showimpacts
  branch.

### 8.4 Two concrete correctness fixes this survey found in our code

1. **Falloff ordering in `fragment_value`/`penetration_after_walls`.** `base` is
   `damage * μ * range_falloff(range_modifier, distance_to_hitbox)`, then every wall
   loss is subtracted from that single number. The engine carries the damage **forward
   from the muzzle**: it applies falloff on each free-flight gap, subtracts the wall
   loss at the wall, and the remaining falloff then attenuates that loss. For the
   damage-proportional term (`0.16 * damage`) the two are algebraically identical
   (`f_w · f_rest = f_total`), but for the **non-proportional** thickness and power
   terms ours over-subtracts by `f_rest`, and it evaluates the `< 1` stop at the
   target rather than at the wall. The error is small (typically < 1 %, a few damage
   at long range through thick walls, and only at the block boundary), but it is a
   systematic ordering difference. The clean fix is to pass the span entry distances
   into `penetration_after_walls` (it already receives `layer.entry`) and evaluate
   the stack forward; `penetration_after_walls` already has the data, it just needs
   the falloff folded into the loop.
2. **`wall_params` uses `min(enter, exit)`.** The engine takes `min()` over **every**
   surface in the span. For our one-surface-per-brush enumeration this is equal; for a
   mesh cluster or a compound (if we ever add type 4) it is not. Either document the
   invariant ("one surface per layer") assertively, or carry all surfaces in the
   layer.

Neither change is made here (research-only).

### 8.5 Licensing constraints (critical)

| Repo | Licence | Can we copy code? |
|---|---|---|
| `danielkrupinski/Osiris` | **MIT** (verified `master` + `csgo-end-of-life`) | Yes, with attribution. (Brief's GPL-3.0 claim is wrong.) |
| `rollraw/qo0-base` | MIT | Yes, with attribution |
| `alza54/opensource2` | MIT | Yes, with attribution |
| `Read1dno/ExternalAutoWallCS2`, `VisCheckCS2` | MIT | Yes, with attribution |
| `karola3vax/CS2FOW` | MIT (generated bakes under `DATA_NOTICE`) | Code yes; **do not** ship game-derived bakes |
| `cs2-server-plugins/cs2-bvhtrace` | MIT | Yes |
| `Valthrun/Valthrun` (`aim-assist` branch) | **GPL-2.0** | **No** — a fork would force GPL on the payload. Ideas only. |
| `iCollin/csgo-internal` | **none** | **No** (all rights reserved) |
| `Ripper99-x/CS2-Internal` | **none** | **No** |
| `sezzyaep/FrezzyHook-4`, `Panguins/OneTap` | **none** | **No** |
| UnknownCheats posts (747109, 764290, 608159, 604186) | forum ToS, no licence grant | **No** — reimplement from the described behaviour, don't paste |

The safe pattern for this repo: read MIT code for structure, read unlicensed/forum
code for *facts about the engine*, and write our implementation from the engine
decompilation we already have (`docs/re/penetration.md`). Facts are not
copyrightable; expression is.

---

## 9. Open questions / unverified

1. **Exact stop semantics and accounting for the current shot.** Static RE now shows
   `FX_FireBullets` passes 4 through `FUN_180887d60` into the `FUN_18089ca80` span
   limit, alongside an independent retry guard and 90-unit cap. Map how that limit,
   `FUN_180885710`'s modulation entries, and `HandleBulletPenetration`'s separate
   secondary-trace argument interact; do not reuse the public `CreateTrace` 4-vs-5
   convention as the live shot's count. Runtime N-wall differential remains open.
2. **`GetDamageToPoint`** (`4C 8B DC 53 55 41`) — exists, call convention unknown,
   public attempts fail. Potentially a one-call oracle; worth RE if we want a
   ground-truth harness.
3. **`EntitiesAlongLinearSweep` / `TraceLineIntervals` record layout** — only
   distance `+0x0C` and enter/exit `+0x32` are pinned in
   [`bullet-trace.md`](../re/bullet-trace.md); the material field is not. That is the
   remaining blocker for using the engine's interval trace directly as our span
   source.
4. **CS2FOW / BvhTrace performance numbers** — quoted from search-indexed READMEs, not
   re-read from the current files (CS2FOW is archived and its README was rewritten).
   Treat 60 ms→1 ms and 9.2 %→3.8 % as indicative, not measured by us.
5. **Nixware visualizer** — behaviour from the workshop description only; the script
   is behind a login.
6. **Whether any closed-source cheat ships a real damage field.** Unknowable from
   public sources. The Nixware script and CS2FOW's depth buffer are the only public
   hints that the problem is being attacked that way.

---

## Appendix — URL index

Primary sources (read):

- Osiris: <https://github.com/danielkrupinski/Osiris> ·
  <https://raw.githubusercontent.com/danielkrupinski/Osiris/master/LICENSE> ·
  <https://raw.githubusercontent.com/danielkrupinski/Osiris/csgo-end-of-life/Source/Hacks/Aimbot.cpp> ·
  <https://github.com/danielkrupinski/Osiris/tree/cs2-release/Source>
- iCollin: <https://github.com/iCollin/csgo-internal> ·
  <https://raw.githubusercontent.com/iCollin/csgo-internal/master/hacks/autowall.cpp>
- qo0: <https://raw.githubusercontent.com/rollraw/qo0-base/master/base/features/autowall.cpp> ·
  <https://raw.githubusercontent.com/rollraw/qo0-base/master/base/features/autowall.h>
- OpenSource2: <https://raw.githubusercontent.com/alza54/opensource2/main/OpenSource2-SDK/src/features/aim_enhancement/aimbot.cpp> ·
  <https://raw.githubusercontent.com/alza54/opensource2/main/OpenSource2-SDK/src/sdk/source-sdk/classes/types/c_trace.hpp>
- Ripper99: <https://raw.githubusercontent.com/Ripper99-x/CS2-Internal/main/SourceEngine/Tracing.h> ·
  <https://raw.githubusercontent.com/Ripper99-x/CS2-Internal/main/SourceEngine/Aimbot.cpp> ·
  <https://raw.githubusercontent.com/Ripper99-x/CS2-Internal/main/SourceEngine/Entity.cpp>
- Read1dno: <https://github.com/Read1dno/ExternalAutoWallCS2> ·
  <https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/VisCheck.cpp> ·
  <https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/vischeck_module.cpp> ·
  <https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/VisCheck.h> ·
  <https://raw.githubusercontent.com/Read1dno/ExternalAutoWallCS2/main/Parser.h> ·
  <https://raw.githubusercontent.com/Read1dno/VisCheckCS2/main/README.md>
- UC: <https://www.unknowncheats.me/forum/counter-strike-2-a/747109-enginetrace-traceshape-release.html> ·
  <https://www.unknowncheats.me/forum/counter-strike-2-a/764290-penetration-autowall-damage-calculation.html> ·
  <https://www.unknowncheats.me/forum/counter-strike-2-a/608159-cs2-autowall-penetration-system.html> ·
  <https://www.unknowncheats.me/forum/counter-strike-2-a/604186-handlebulletpenetration.html> ·
  <https://www.unknowncheats.me/forum/counter-strike-2-a/747821-correct-call-getdamagetopoint-autowall-object-param.html> ·
  <https://www.unknowncheats.me/forum/counter-strike-2-a/751031-autowall-printing-0-damage.html>
- Valthrun: <https://github.com/Valthrun/valthrun-cs2/tree/aim-assist> ·
  <https://github.com/Valthrun/valthrun-cs2/issues/180>
- CS2FOW: <https://github.com/karola3vax/CS2FOW> ·
  <https://raw.githubusercontent.com/karola3vax/CS2FOW/main/docs/CODE_TOUR.md>
- bvhtrace: <https://gitlab.com/cs2-server-plugins/cs2-bvhtrace> ·
  <https://gitlab.com/cs2-server-plugins/cs2-bvhtrace/-/raw/main/docs/PLAN.md>
- CS:GO extras: <https://raw.githubusercontent.com/sezzyaep/FrezzyHook-4/main/Autowall.cpp> ·
  <https://raw.githubusercontent.com/Panguins/OneTap/main/autowall.cpp> ·
  <https://github.com/Shaxzy/NIXWARE-CSGO/blob/master/Nixware-CSGO/features/autowall/autowall.cpp> ·
  <https://nixware.cc/workshop/749/>
- Engine trace background: <https://crowbar.team/posts/cs2-ray-tracing/>

Local (this repo):

- <../ragebot-research.md> · <../ragebot.md> · <../ragebot-perf.md>
- <../re/penetration.md> · <../re/bullet-trace.md> · <../re/occlusion.md>
- <../../payload/aim/src/damage.cpp> · <../../payload/aim/src/penetration.cpp>
