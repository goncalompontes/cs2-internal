# Fatality CS2 base — what the leak actually contains

> Research note, 2026-10-20. Companion to
> [`cheat-dump-inventory.md`](cheat-dump-inventory.md) §2.3 (where it was
> found) and [`cs2-chams-t7.md`](cs2-chams-t7.md) §5 (what it corroborates).
>
> **Artifact.** `notphage/fatality.win-CS2` — a leaked **CS2** internal base of
> the Fatality product.
>
> | Property | Value |
> |---|---|
> | Quarantine path | `C:\Users\gpmpo\nea\quarantine\src\fatality.win-CS2` |
> | Files | 268 (≈6.1 MB) |
> | Created / pushed | 2024-01-18 (both) |
> | Stars at read | 32 |
> | README | a one-line taunt to the author ("…your cheat source code gets leaked twice by the same person…") — consistent with two leaks |
> | Licence | **none** ⇒ all rights reserved. Read-only reference; do not copy. |
>
> **Confidence.** `verified` = I read the file; `inferred` = interpretation;
> `unknown` = not determined.
>
> **Bottom line.** This is a **base, not the product**: it has the CS2 SDK, the
> hooks, the ESP, the movement, the GUI and a *complete Lua scripting layer*,
> but **no ragebot, no anti-aim and no chams implementation**. Its real value is
> that its `cfg` is the product's **configuration schema** — which, cross-read
> with the gamesense config registry, tells us the industry shape of a top-tier
> CS2 ragebot.

---

## 1. What is implemented

`verified` by tree listing and reading:

| Area | Files | Notes |
|---|---|---|
| **Hooks** | `src/hooks/client.cpp` (6.3 KB), `input_system.cpp`, `steam.cpp` | `prediction_update`, `create_move` (sub-tick aware), `frame_stage_notify`, `override_view`, `on_render_start`, plus `get_fov` |
| **Visuals / ESP** | `src/game/visuals.cpp` (23.7 KB) | entity iteration via `game_entity_system`, `c_player_pawn` → controller → pawn, box/name/health/weapon ESP, `world_to_screen`, per-player `model_alpha` (model fade) and `alpha` lerps driven by `frame_time` |
| **Movement** | `src/game/movement.cpp` (8 KB) | bunnyhop / strafe |
| **Config** | `src/game/cfg.h` (16.5 KB) + `cfg.cpp` (19.4 KB) | the whole product schema — see §3 |
| **GUI** | `src/gui/**` (~150 files) | full ImGui-style widget set: containers, tabs, sliders, colour picker, popups, notifications, text input, hotkeys, file browser, "public software" list |
| **Menu** | `src/menu/**` | tab implementations: `tab_rage.cpp` (30 KB), `tab_visuals.cpp` (37 KB), `tab_legit.cpp`, `tab_lua.cpp`, `tab_misc.cpp`, `tab_skins.cpp` — **UI only, bound to the config** |
| **Lua** | `src/lua/**` (~25 files) | a full scripting layer: `engine`, `state`, `script`, `helpers`, plus an API surface per area (`api/entity.cpp` 21 KB, `api/gui.cpp` 41 KB, `api/render.cpp` 39 KB, `api/utils.cpp` 22 KB, `api/mat.cpp`, `api/math.cpp`, `api/panorama.cpp`, `api/user_cmd.cpp`, `api/timer.cpp`, `api/cvar.cpp`, `api/event.cpp`, `api/fs.cpp`, `api/input.cpp`, `api/zip.cpp`, `api/database.cpp`) |
| **SDK** | `include/sdk/**`, `src/sdk/**` | generated schema headers, `offsets.h`, `offsets_schema.h` (751 KB), `proto.h` (306 KB), `schema/client.h` (285 KB), `schema/server.h` (469 KB), `schema/global_types.h` (827 KB), `sdk/utl/**`, `sdk/math/**`, `utils/{fnv1a,murmur2,value_obfuscation}.h`, `utils/hook.h` |

### 1.1 What is missing

`verified` by exhaustive tree listing of `src/`:

- **No ragebot.** No `ragebot.*`, no `aimbot.*`, no `hitscan.*`, no `autowall.*`.
  The rage *menu* exists (`tab_rage.cpp`) and is bound to `cfg`, but nothing
  consumes those settings.
- **No anti-aim.** `cfg.h` has the anti-aim enums (§3) but no implementation.
- **No chams.** No `draw_model`/`draw_scene_object` hook, no material creation
  (the `api/mat.cpp` material bindings are **commented out**, §5).
- **No inventory changer, no skin changer, no buybot, no skin/knife logic.**

So the leak is "everything except the parts that make money" — a skeleton with
the full config surface and the full SDK.

---

## 2. The SDK/schema assets (and their age)

`verified`. `include/sdk/schema/*.h` and `offsets_schema.h` are **generated CS2
schema dumps** (class names, field names, and offsets as of January 2024),
totalling ~2.6 MB of structured reference.

- **Useful:** the *class and field vocabulary* of CS2 (`C_CSPlayerPawn`,
  `C_CSGameEntity`, `m_iHealth`, …) as a cross-check for our own schema walk,
  and the `utl_*` container definitions.
- **Stale:** every offset. The repo is 2 years 9 months old; the game has been
  updated many times since. The offsets in `include/sdk/offsets.h` must not be
  used.
- **The `YEAHMAN563/fatality.cs2` fork** (2026-10-18, 346 files) — **read and
  verified; the "fixed offsets/schemas" claim does not hold up.** `verified`:
  - **Schema offsets are stale, exactly as in the original.** The fork's
    `include/sdk/schema/offsets.h` (13 classes, 471 member offsets) is
    **byte-identical** to the original's client-side subset of
    `offsets_schema.h` — which is stamped `// generated on: 11/6/2023`. Every
    shared class has the same members with the same values (0 added, 0 removed,
    0 changed). The fork's "fixed schemas" reduces to *deleting* 1,773 classes,
    not to re-dumping them.
  - **Only six RVAs actually moved**: `get_abs_origin` (0x149f60 → 0x219230),
    `get_entity_by_index` (0x60afe0 → 0x9f69c0), `get_view_angles`
    (0x74bd10 → 0xb70c90), `set_view_angles` (0x756060 → 0xb80af0),
    `pGlobalVariables` (0x16b7be0 → 0x2230090), `pCSGOInputInstance`
    (0x16d4838 → 0x257b640). Two code comments assert these come from
    **"CS2-SDK build 14184"**, with no dump, no date stamp and no provenance in
    the offsets files — treat as `unverified` and re-derive.
  - **The fork implements less, not more.** It *removes* the ESP
    (`src/game/visuals.cpp`), the autostrafe/bhop (`src/game/movement.cpp`) and
    the **entire 27-file Lua scripting engine**, and replaces the 8-hook manager
    with a single `Present` hook — the client/input hooks are explicitly
    disabled in the source with the comment that their vtable indices are *"not
    validated against the current SDK build"*.
  - **What it keeps is exactly the part we care about**: `cfg.h`, `cfg.cpp` and
    the rage/visuals menu tabs are **byte-identical** to the original.
  - **What it adds** (real engineering, worth reading): MinHook vendored
    in-tree, the ev0lve/renderer vendored as source (41 files), a `pe64.h`
    pattern scanner, a debug-log subsystem, a hardened deferred-init `DllMain`,
    and committed prebuilt `x64/{Debug,Release}/fatality_cs2.dll`.
  - **Build state**: the `.sln` survives, no submodules are needed (so it is
    closer to self-contained than the original, whose `contrib/` submodules are
    absent on disk), toolset `v145`; but the Debug config links the **32-bit**
    `minhook.lib`/`minhook_d.lib` into an x64 target, and `src/sdk/update.h`
    includes a header that does not exist. It does not build as-is.

  **Consequence:** the original leak remains the better source of the feature
  set and the config schema; the fork is only interesting as a *partial offset
  refresh* and as a vendored-renderer/MinHook reference.

---

## 3. The configuration schema — the real payload

`verified` — `src/game/cfg.h`. This is the product's feature list, and it is
the most useful thing in the leak. Enum names verbatim:

**Rage / aim** (`struct aim_t`): `enabled`, `autofire`, `anti_exploit`,
`fallback_on_key`, `hsmode`, `knife_bot`, `zeus_bot`, `slide`, `fake_duck`,
`punch`, `doubletap`, `hideshot`, `silent`, `dormant`, `extend_peek`,
`visualize`, `baim_on_key`, `stop_in_air`; floats `zeus_hc`, `fov`,
`ping_spike_amt`; bitsets `low_perf`, `resolver_mode`, `dt_exploit`.

**Per-weapon config** (`struct cur_weapon_config_t` / `weapon_config_t`):
`hitchance`, `pointscale`, `mindmg`, `mindmg_override` (+`mindmg_override_enabled`),
`autostop`, `autorevolver`, `scout_jumpshot`, `autoscope`, `ignore_limbs`,
plus bitsets `autostop_mode`, `baim`, `extra_safety`, `force_safe`, `hitboxes`,
`pointscales`, `fallback_mode`.

**The three enumerations that define the ragebot's shape** (names verbatim):

- `enum safety` — `safety_air`, `safety_lethal`, `safety_moving`,
  `safety_standing`, `safety_limbs`, `safety_override`, `safety_risky`
- `enum fallback_mode` — `fallback_off`, `fallback_adaptive`,
  `fallback_prefer_safepoint`
- `enum autostop` — `autostop_lethal`, `autostop_inbetween`, `autostop_predictive`
- `enum hitboxes` — `hitboxes_head`, `hitboxes_neck`, `hitboxes_chest`,
  `hitboxes_stomach`, `hitboxes_legs`, `hitboxes_feet`, `hitboxes_arms`

**Anti-aim** (enums only, no code): `aa_yaw_base` (`viewangles`, `at_target`),
`aa_yaw` (`none`, `backwards`, `custom`, …), `aa_yaw_jitter`, `aa_pitch`
(`none`, `down`, `up`, `zero`, `custom`), `freestand_fake`, `choke_mode`
(`none`, `always_on`, `adaptive`), `fake_yaw` (`static`, `opposite`, …),
`roll_lean`, `defensive_lag`, `defensive_fake`, `resolver_mode`
(`res_roll`, `res_roll_disabled`).

**Movement**: `autostrafe` (`viewangles`, `movement_keys`, `easy_strafe`,
`jump_release`), `strafing` (`default`, `never_slide`, `always_slide`),
`leg_slide`.

**Visuals**: `cham_material` (see below), `esp_flags` (18 entries),
`world_esp`, `healthbar_style`, `vis_disablers`, `noscope`, `dpi_scale`,
`indicators`, `hitmarker`, `hitsound`, `spread_circle`.

**Materials** (`cfg.h:290`, `enum cham_material`): `cham_none`, `cham_normal`,
`cham_flat`, `cham_rim_glow`, `cham_contour`, `cham_flow`, `cham_acrylic`,
`cham_legacy`, `cham_sprite`, …

### 3.1 Cross-read with gamesense — the industry shape

`verified` (both sources), and this is the highest-value result of the whole
exercise:

| Concept | Fatality CS2 (config schema) | Gamesense CS:GO (config registry) |
|---|---|---|
| per-shot safety predicate | `enum safety` — air, lethal, moving, standing, limbs, override, risky | `Ragebot.prefer_safe_points`, `Ragebot.safe_point_key` |
| choose among candidate points | `pointscale` (bitmask `pointscales`), `fallback_prefer_safepoint`, `fallback_adaptive` | `Ragebot.adaptive_point_scale`, `Ragebot.static_point_scale`, `Ragebot.prefer_safe_points` |
| damage gate | `mindmg`, `mindmg_override`, `mindmg_override_enabled` | `Ragebot.minimum_damage`, `minimum_visible_damage`, `minimum_override_damage` |
| hit probability gate | `hitchance` (per weapon) | `Ragebot.hitchance`, `Ragebot.hitchance_amount`, `Ragebot.double_tap_hitchance` |
| stop before shooting | `autostop` (lethal / inbetween / predictive) | `Ragebot.autostop_conditions` |
| hitbox set | `enum hitboxes` (7) | `Ragebot.hitboxes`, `Ragebot.ignore_unsafe_hitboxes` |
| miss budget | — | `Ragebot.max_misses`, `Ragebot.max_misses_amount` |
| anti-aim model | `aa_yaw_base` / `aa_yaw` / `aa_pitch` / `choke_mode` / `resolver_mode` | `Antiaim.antiaim_type`, `Antiaim.desync*`, `Antiaim.lby_type` |

**Conclusion (high confidence).** Two independent, top-tier products — one
CS:GO, one CS2 — structure the ragebot identically:

1. pick a **hitbox set**,
2. pick a **point** on it (scale per hitbox),
3. accept or reject that point by a **safety predicate** (air, lethal, moving,
   limbs, …) — with an explicit *prefer-safe-point* fallback mode,
4. accept the shot only if **damage ≥ threshold** and **hit chance ≥ threshold**,
5. **stop first**, then shoot.

That is the vocabulary our own config should speak. It does **not** tell us
how the safety predicate is computed (no implementation leaked), so it does not
close our exactness gap; but it tells us the *shape* of the intended answer and
gives us defensible names for the knobs.

---

## 4. Lua surface

`verified`. The Lua layer is unusually complete for a "base" leak: a state
machine, a script loader, helpers, and an API per area. Notable API files and
their size hint at coverage: `gui.cpp` (41 KB), `render.cpp` (39 KB),
`utils.cpp` (22 KB), `entity.cpp` (21 KB), `math.cpp`, `mat.cpp`, `panorama.cpp`.

This matters to us for one reason: **the Lua `render`/`mat` API is the
scripting-level equivalent of the chams path**, and in this leak the material
functions are present but commented out — i.e. the product had a scriptable
material API, and the leak cut it. The commented code is still informative
(§5 of [`cs2-chams-t7.md`](cs2-chams-t7.md)).

---

## 5. Value, limits, legality

**Value**

- The config schema as an industry reference (§3) — high value, zero risk.
- The generated schema headers as a *vocabulary* cross-check for our own
  schema resolution — medium value, must not be used for offsets.
- The `create_move` / `frame_stage_notify` hook shapes as a reference for
  sub-tick handling in CS2 — medium value.
- The GUI/menu structure — low value for us (we have our own).

**Limits**

- No combat code. Nothing here changes the exactness gap in
  [`aim-solver-design-review.md`](aim-solver-design-review.md).
- Offsets are 2 years 9 months stale.
- Provenance of the *vendor's* code is asserted by the taunting README, not
  proven; the tree is self-consistent and compiles as a Visual Studio solution,
  which is consistent with a real leak (`inferred`).

**Legality.** No licence file ⇒ all rights reserved. Read for understanding,
do not copy, do not redistribute. The same rule as every other artifact in
[`cheat-dump-inventory.md`](cheat-dump-inventory.md) §8.

---

## 6. Suggested next step (cheap, high value)

Diff the Fatality CS2 `enum safety` / `enum fallback_mode` / `enum autostop`
/ `enum hitboxes` against **our** current config and rename/extend our knobs
to match the industry vocabulary. It costs an hour, it makes the UI and the
docs speak the same language as the products we are compared against, and it
makes the *missing* pieces (a `prefer_safepoint` fallback, a per-hitbox point
scale, a predictive autostop) explicit TODO items rather than omissions.
