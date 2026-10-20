# Gamesense (CS:GO) — static reversal notes

> Research note, 2026-10-20. Companion to
> [`cheat-dump-inventory.md`](cheat-dump-inventory.md) §4 (where the artifact was
> found) and [`cs2-chams-t7.md`](cs2-chams-t7.md) (what we took from it).
>
> **Artifact.** `gamesense.dll` — the gamesense/skeet **CS:GO** module, obtained
> from the community crack repositories. It is the real module, not a loader.
>
> | Property | Value |
> |---|---|
> | Quarantine path | `C:\Users\gpmpo\nea\quarantine\gamesense-csgo\gamesense.dll` |
> | Size | 3,423,744 bytes |
> | SHA-256 | `171c84fb008884711c283e432852b4df6a22e8be66308068de72054b18c853bd` |
> | Format | PE32 (x86, 32-bit), `machine 0x014C`, `magic 0x010B` |
> | Linker timestamp | `0x65A2E949` = 2024-01-13 19:49:29 UTC |
> | Image base / entry RVA | `0x10000000` / `0x20E32B` |
> | `SizeOfImage` | `0x745000` |
> | Export DLL name | `cheat.dll` |
> | PDB reference | `cheat.pdb`, GUID `a98e0824-2647-4845-97c9-1283597cf8f9`, age 5 (RSDS) |
> | Imports | 22 DLLs: `WS2_32`, `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `MSVCP140`, `d3dx9_43`, `urlmon`, `IMM32`, `VCRUNTIME140` + 12 `api-ms-win-crt-*` |
> | Sections | `.text` 0x20FAD9 / `.rdata` 0x6110A / `.data` 0x4BE684 / `.rsrc` 0x1E0 / `.reloc` 0x11620 — **no packer signature, no odd section names** |
>
> A second, older sample (`gs.dll`, 2022-10-31, 2,510,336 bytes, export name
> `gs.dll`, sha256 `cb099daed78ddd69dc2cb1bd3228a2bf64c67f06996d45d317793a5c54723b9d`)
> has the same shape. Everything below is from the 2024 sample.
>
> **Method.** REA (Ghidra 12.1.4 provider, native authority) against the file
> above. Evidence ID for the main decompilation: `ev_daffcd6e0dd64a2042fa5d5395fa5fa18d287adb8d037b3aba7dd63508961300`
> (retained; `inspect_analysis_view` with `facet: "pseudocode"` pages it).
> Addresses below are Ghidra virtual addresses in the 2024 sample.
>
> **Confidence.** `verified` = observed in the provider output; `inferred` =
> interpretation of observed behaviour; `unknown` = not determined.
>
> **Scope note.** Nothing was executed. This note describes structure and
> technique; no code is reproduced.

---

## 1. Shape of the module

| Fact | Value | Confidence |
|---|---|---|
| Procedures recovered | 13,433 | `verified` |
| Strings recovered | 10,979 | `verified` |
| `.text` | 0x10001000–0x10210C00 (2.16 MB) | `verified` |
| `.data` | 0x10273000–0x10731684 (4.97 MB virtual, 0xC1200 raw) | `verified` |
| Embedded runtime | **LuaJIT 2.0.5** and **Lua 5.1** (225 `lua`-matching strings) | `verified` |
| Protection | none observed — plain sections, plaintext config names, symbols stripped | `verified` |
| Signature table | 28 byte-patterns resolved against game modules at runtime | `verified` |

The Lua presence is not incidental: the product's configuration is addressed by
dotted names (`Ragebot.hitchance`, `Antiaim.desync`) and the module carries a Lua
interpreter, so those strings are the **config-variable registry**, not debug
labels. That registry is the cheapest complete map of the product's feature
surface.

### 1.1 Feature registry (338 config variables)

`verified` (regex sweep of the provider string inventory for
`^[A-Z][A-Za-z0-9_]*\.[a-z0-9_]+$`).

| Namespace | Count | Notable entries |
|---|---|---|
| `Esp` | 89 | per-entity ESP flags, `Esp.enable`, world/team/inventory splits |
| `Player` | 70 | `Player.backtrack_chams_material`, `Player.local_chams_type`, `Player.fake_chams_enable`, `Player.fake_chams_color`, `Player.fake_animated_material`, `Player.fake_double_material_color` |
| `Ragebot` | 43 | `Ragebot.hitchance`, `Ragebot.hitchance_amount`, `Ragebot.selection_type`, `Ragebot.adaptive_point_scale`, `Ragebot.static_point_scale`, `Ragebot.body_scale`, `Ragebot.hitboxes`, `Ragebot.ignore_unsafe_hitboxes`, `Ragebot.prefer_safe_points`, `Ragebot.safe_point_key`, `Ragebot.minimum_damage`, `Ragebot.minimum_visible_damage`, `Ragebot.minimum_override_damage`, `Ragebot.damage_override_key`, `Ragebot.max_misses`, `Ragebot.adaptive_shot_dmg`, `Ragebot.hide_shots`, `Ragebot.double_tap`, `Ragebot.instant_double_tap`, `Ragebot.defensive_double_tap`, `Ragebot.autowall`, `Ragebot.anti_exploit`, `Ragebot.pitch_correction`, `Ragebot.tolerance` |
| `Misc` | 40 | skins, sounds, keybinds |
| `Antiaim` | 38 | `Antiaim.desync`, `Antiaim.antiaim_type`, `Antiaim.desync_range`, `Antiaim.desync_spin_range`, `Antiaim.inverted_desync_range`, `Antiaim.body_lean`, `Antiaim.fake_lag`, `Antiaim.fake_lag_limit`, `Antiaim.fake_lag_type`, `Antiaim.freestand`, `Antiaim.lby_type`, `Antiaim.legit_lby_type`, `Antiaim.base_angle`, `Antiaim.yaw_jit`, `Antiaim.spin_speed`, `Antiaim.at_targets` |
| `Legitbot` | 29 | `Legitbot.fov`, `Legitbot.smooth`, `Legitbot.rcs`, `Legitbot.autofire_delay`, `Legitbot.resolver`, `Legitbot.silent_fov` |
| `Skins` | 10 | — |

Two entries matter for our own open questions:

- **`Ragebot.prefer_safe_points` + `Ragebot.safe_point_key` + `Ragebot.adaptive_point_scale` + `Ragebot.static_point_scale`**
  — the product's *point-safety* model is a per-shot choice over a scaled set of
  candidate points, with an explicit "prefer safe points" mode. This is the same
  family of idea as our safe-point work, expressed as a per-weapon config
  (`hitchance`, `pointscale`, `mindmg` in the Fatality CS2 config, §
  [`fatality-cs2-base.md`](fatality-cs2-base.md) §4) rather than as a damage
  field.
- **`Ragebot.minimum_damage` / `minimum_visible_damage` / `minimum_override_damage`**
  — the product gates shots on *damage*, with a visible/occluded split and a
  manual override. Again: damage thresholds, not a continuous field.

**Transferable conclusion.** Two top-tier CS:GO products structure the ragebot
around *per-point selection with a safety predicate* and *damage thresholds*.
Neither replaces our damage-field solver, but both confirm that "pick a point,
then score it" is the industry shape, and that a *safe point* is a first-class
concept (ours currently has safe-point logic but no "prefer safe point" mode).

---

## 2. The chams / material subsystem (CS:GO flavour)

This is the part that transferred to CS2 (see
[`cs2-chams-t7.md`](cs2-chams-t7.md)). Two stages: **material creation** and
**material override**.

### 2.1 Material creation

`verified` (decompiled at `0x1006c970`, 442 bytes; the call sites are inside the
large chams function at `0x1006cb30`).

- The module builds a **VMT text blob** per material and hands it to the game.
- The shader name is chosen by a flag: `param == 0` → `UnlitGeneric`,
  otherwise `VertexLitGeneric` (`0x1006c970`).
- The material name is generated as `"safeless_" + counter`
  (`DAT_106e2200` is incremented per material).
- Two game functions are **resolved by signature out of `client.dll`**, not
  imported:
  - `55 8B EC 83 E4 F8 83 EC 34 53 8B 5D 0C 89` → material creation,
  - `55 8B EC 56 8B F1 33 C0 8B 4D 0C 81 26 ? ? ? ? 89 46 10 8A 45 10 C6 46 03 00 89 46 14 88 46 18 C7 46 ...`
    → material lookup.
- Existing engine materials are fetched through
  `FUN_1006c900(name, "Model texture", a, b)` → `FUN_1001c4e0()` → 4-arg call,
  e.g. `models/inventory_items/dogtags/dogtags_outline`,
  `models/inventory_items/trophy_majors/crystal_clear`.

**Material library found in the binary** (`verified`; VMT text read verbatim from
the decompilation, `.rdata` addresses in parentheses):

| Address | Shader | Distinguishing parameters |
|---|---|---|
| `0x1022fd88` | `VertexLitGeneric` | `$basetexture vgui/white`, `$ignorez 0`, `$nofog 1`, `$model 1`, `$selfillum 1`, `$halflambert 1`, `$znearer 0`, `$flat 0`, `$wireframe 0` — the **opaque, z-tested** material |
| `0x1022fac8` | `VertexLitGeneric` | `vgui/white_additive`, `$ignorez 0`, `$envmap env_cubemap`, `$normalmapalphaenvmapmask 1`, `$envmapcontrast 1`, `$flat 1` — **additive glow** |
| `0x1022f938` | `VertexLitGeneric` | `$additive 0.5`, `$envmap models/effects/cube_white`, `$ignorez 1`, `$envmaptint [1 1 1]`, `$envmapfresnel 1`, `$envmapfresnelminmaxexp [0 1 2]`, `$alpha 1`, `$wireframe 0` — **fresnel rim, drawn through walls** |
| `0x1022fee8` | `VertexLitGeneric` | `$additive 1`, `$envmap models/effects/cube_white`, `$envmaptint [1 1 1]`, `$envmapfresnel 1`, `$alpha 0.7` — additive translucency |
| `0x1022f938` region | `VertexLitGeneric` | `$basetexture vgui/white` + **`$wireframe 1`** — **wireframe** material |
| `0x1022fac8` region | `UnlitGeneric` | `$basetexture vgui/white`, `$flat 1` — **flat/unlit** material |
| `0x10230520`, `0x10230990`, `0x10230ad0`, `0x10230c90` | `VertexLitGeneric` | second cluster: `vgui/white`, `vgui/white_additive`, `dev/dev_camo…` variants (used by a different caller) |

So gamesense's CS:GO chams palette is: opaque, additive, fresnel-rim,
translucent-additive, wireframe, unlit-flat, plus a camo variant — seven
recipes, each in a "z-tested" and a "through-wall" flavour.

### 2.2 Material override

`verified` (decompiled at `0x1006cb30`; the per-entity block is at
`0x1006d4be`–`0x1006d723`).

Per drawn entity the sequence is:

1. classify the model by name: `strstr` against `models/player`, `weapons/v_`,
   `arms`, `sleeve`, `_dropped.mdl`, `models/weapons/w` → the entity class
   (`player`, `teammate`, `localplayer`, `localplayerfake`, `wepvm`, `hands`,
   `weppvmcoll`, `handscoll`, …);
2. pick a material from the array at `DAT_106e2824` indexed by a per-class index
   (a 4-byte-stride load at `0x1006d4be` and `0x1006d723`);
3. set the colour through an interface slot at vtable offset `0x10` of a
   separate interface object (`0x1006d4be` area);
4. call the material's vtable slot `0x30` with no arguments (a getter, result
   unused);
5. call the material's vtable slot `0x74` with the arguments `0x8000` and `0` —
   a material *flag* setter called with `0x8000` (`inferred`: a "render through
   walls / ignore depth" style material flag);
6. call vtable slot `4` of the model-render interface with
   `(material, 0, 0)` — the **override** call;
7. call the resolved original draw at `DAT_1072f26c`.

`local_1a4` (step 6) is the engine interface fetched by name:
`FUN_101b61f0()` → `FUN_101b7590("engine.dll", "VEngineModel", 0)` — i.e. the
engine's model-render interface, whose **vtable slot 1** takes the material.
`DAT_1072f26c` is entry **0x15** of the resolved-function table
(`FUN_1006a480(index)` is the table getter, `0x1006a480`) and is called with
five arguments — the original `DrawModel` shape (the fifth is an out-parameter at
`DAT_106dbe34`).

**Takeaway (CS:GO).** The override is *global on the model-render interface*
(`OverrideMaterial`), not per-object, and the flag `0x8000` is what makes the
same material draw through walls. That single flag is the whole trick: **one
material, one flag, two passes** (draw with the flag unset for the visible
chams, set it for the through-wall pass).

---

## 3. The signature-resolution subsystem

`verified` (28 distinct pattern strings; resolution helper `FUN_101beee0` at
`0x101beee0`; per-feature resolvers each keep a lazily-initialised global).

Game modules the module touches by name: `client.dll`, `engine.dll`,
`materialsystem.dll`, `vgui2.dll`, `vguimatsurface.dll`, `shaderapidx9.dll`,
`datacache.dll`, `tier0.dll`, `vphysics.dll`, `localize.dll`,
`filesystem_stdio.dll`, `inputsystem.dll`, `serverbrowser.dll`, `vstdlib.dll`,
`shcore.dll`.

Representative resolutions (module → pattern → purpose, where the purpose is
`verified` from nearby strings and `inferred` from the pattern shape):

| Site | Module | Pattern | Purpose |
|---|---|---|---|
| `0x10009ec0` | `client.dll` | `55 8B EC 51 56 8B F1 85 F6 74 68` | function adjacent to the `CBreakableSurface` class names → **breakable/penetration surface** handling |
| `0x10027a10` | `client.dll` | `B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 5D 08 56 57 8B F9 33 …` | entity accessor |
| `0x1002f800` | `client.dll` | `A3 ? ? ? ? 66 0F 6E 86 …` / `89 35 ? ? ? ? F3 0F 10 48 …` | float/vector helpers (view angles) |
| `0x1004af20` | `client.dll` | `55 8B EC 53 8B 5D 08 56 57 8B F9 33 …` | HUD element access — nearby strings `CCSGO_HudRadar`, `skeleton`, `skelecol` → **radar + skeleton ESP** |
| `0x100367a0`, `0x100421f0` | `client.dll` | `55 8B EC 83 E4 F8 F3 0F 10 42 …` | `movss`+`movups` prologue → **vector/angle maths** helpers |
| `0x10042217` area | `client.dll` | `55 8B EC 83 E4 F8 83 EC 34 53 8B 5D 0C 89` | material creation (§2.1) |
| `0x1002ae40` | `client.dll` | `55 8B EC 51 A1 ? ? ? ? 85 C0 75 37` | nearby string `misc_rev_comp` → **revision/compensation** |
| `0x1003041c` | `engine.dll` | `53 56 57 8B DA 8B F9 FF 15 …` | engine call-through helper |
| `0x1006a480` (table) | — | — | index → resolved pointer, index `0x15` = the `DrawModel` used in §2.2 |

**Takeaway.** The product does not import game functions; it **scans the game
module for byte patterns at runtime**, once, and caches the result in a global.
That is the same technique we already use for our own pattern table, so the
transferable part is not the mechanism but the *inventory*: it is a compact
catalogue of which game functions matter for a CS:GO cheat.

---

## 4. What this sample does *not* answer

| Question | Status |
|---|---|
| Exact ragebot hit-chance maths | **partly extracted** (§5.1, §5.2): a spread-derived float compared against a per-weapon threshold, with a 50 % floor; the *sampling* model behind the float was not extracted. |
| Anti-aim angle computation | **not extracted.** `Antiaim.*` names are known, the computation is not. (`rage_aa_over`, `rage_antiaim_desync_limit` show the anti-aim is *coordinated* with the ragebot — the AA is overridden while the ragebot shoots.) |
| Resolver internals | **not extracted.** `Legitbot.resolver` and `Ragebot.resolver` are named only. |
| Lua API surface | **extracted** (§5.5) — the registered names, including the shot-result and damage-model entries. The *bindings* (what each returns) are only partly known. |
| Protection / anti-analysis | **none observed.** No packer, no VM, no opaque predicates in the parts read. (The 2024 crack is a *patched* build; the vendor build may differ.) |
| Point selection / safe-point algorithm | **not extracted.** The *vocabulary* is known (`rage_safepoint`, `rage_sp_enabled_style`, `rage_mphitbox`, `rage_unsfhitbox`, `safe_points` global), the algorithm is not. |

---

## 5. The ragebot surface (second pass)

Everything below is `verified` from the provider output: config key names are
the *literal* keys the code reads, the log labels are the literal strings the
code prints, and the function/offset statements come from the decompilation.

### 5.1 The shot-decision function

`FUN_10041510` (0x10041510, 1947 bytes) is the function that decides to shoot.
It reads exactly these configuration keys:

| Key | Role (inferred from use) |
|---|---|
| `rage_key_enabled` | the rage key is held |
| `rage_key_enabled_st` | the rage key variant ("st" = *silent/stealth*?) |
| `rage_silent_enabled` | silent aim |
| `auto_fire` | automatic trigger |
| `sv_maxunlag` | the **server's** unlag window — used for the backtrack/latency model |

and it emits these log labels when it fires: `Fired shot at `, `hitchance: `,
`hitbox: `, `damage: `, `backtrack: `.

So a shot is reported with **four numbers**: the hit chance, the hitbox chosen,
the damage expected, and the backtrack tick. That is the output of the
selection pipeline, and it is the shape our own telemetry should have.

The function also gates on `FUN_10041cb0() >= 0x32` (50) — an integer
threshold, `inferred`: the configured minimum hit chance in percent, rejected
before the expensive work.

### 5.2 The hitchance function

`FUN_10041cb0` (0x10041cb0, 1336 bytes) computes the hit chance. Its shape:

1. fetch the weapon/player state object;
2. obtain a float (the *spread/inaccuracy* of the current shot — the module
   exposes `get_spread` and `get_inaccuracy` to scripts, §5.4);
3. **compare that float against a per-weapon threshold at `+0x10C`** of the
   state object. The comparison is written as "less than **or equal**", i.e. a
   ≤ test against the configured hit chance;
4. a three-output helper (`FUN_101bbf30` → three vectors, each post-processed
   by `FUN_101bce10`) — `inferred`: a trace that returns start/end/hit;
5. **two more per-weapon floats, at `+0x150` and `+0x148`**, selected by a
   **flag bit** in a status word (`*puVar2 & 2`). The two values are the
   *client-side* and the *server-side* damage figures (§5.3).

**This is the direct answer to "how do they compute hitchance":** it is a
comparison of the current shot's spread-derived float against a per-weapon
threshold — *not* a sampled probability model. The 50 % floor in §5.1 and the
`Ragebot.hitchance` / `Ragebot.hitchance_amount` config pair are the two knobs.

### 5.3 Two damage models, and server feedback

The module exposes **two** damage quantities to scripts — `client_damage` and
`server_damage` — and a `damage_override` global. It also ships the log
message `missed shot due to spread`.

The event handler `FUN_10024f20` (0x10024f20, 2577 bytes) processes
`player_hurt` (plus `item_purchase`, `bomb_beginplant`, `bomb_begindefuse`) and
logs `… for <hitgroup> … <dmg_health> health remaining` under the
`misc_log_dam` option. It reads the *server's* hitgroup and damage.

**Reading (inferred, but well supported):** the cheat maintains a
**client-side damage model** (what the shot should do) and **observes the
server's damage** from `player_hurt` events (what it actually did). The
difference is available to scripts, and a miss attributable to spread is
detected and logged.

**Why this matters to us.** Our damage field is exactly a client-side damage
model. Two independent products (this one and Fatality CS2's `mindmg` /
`mindmg_override` pair) treat damage as a *predicted quantity with a
server-confirmed counterpart*, and both keep a *minimum-damage gate*. A
`player_hurt`-driven correction channel is the cheap way to keep a predicted
damage model honest, and it is available in CS2 as well (`player_hurt` exists
there too).

### 5.4 The ragebot configuration vocabulary

Fifty distinct `rage_*` keys (unique; the binary stores each several times
because the config is a script with style variants):

`rage_aa_over`, `rage_aa_over_style`, `rage_aim_step`, `rage_aimbot_keystyle`,
`rage_antiaim_desync_limit`, `rage_backtrack`, `rage_baim_enabled`(+`_style`),
`rage_delay_shot`, `rage_dt`, `rage_dt_key`(+`_style`), `rage_dtfl`, `rage_dthc`,
`rage_dtmode`, `rage_dtqs`, `rage_enabled`, `rage_fakeduck_keystyle`,
`rage_fd_enabled`(+`_style`), `rage_flip_keystyle`, `rage_forcebaim_keystyle`,
`rage_fov`, `rage_fps_miti`, `rage_friendlyfire`, `rage_hitbox`,
`rage_key_enabled`(+`_st`), `rage_logs`, `rage_mphitbox`, `rage_qpd`,
`rage_quick_peek_assist`(+`_col`), `rage_quickpeek_enabled`(+`_style`),
`rage_quickstop`(+`_enabled`/`_enabled_style`/`_keystyle`/`_options`),
`rage_remove_recoil`, `rage_resolver`, `rage_safepoint`, `rage_selectiodasdasdn`,
`rage_selection`, `rage_silent_enabled`, `rage_slowmotion_keystyle`,
`rage_sp_enabled`(+`_style`), `rage_unsfhitbox`.

The three that matter for us:

- **`rage_safepoint` + `rage_sp_enabled`** — a safe point is a named,
  switchable mode. `rage_sp_enabled_style` implies a *style* selector, i.e.
  more than one safe-point algorithm.
- **`rage_mphitbox`** — "multi-point hitbox": several candidate points per
  hitbox, and **`rage_unsfhitbox`** — "unsafe hitbox" exclusion.
  Together with `Ragebot.ignore_unsafe_hitboxes` (§1.1) this is the point
  selection model: *multi*-point per hitbox, with unsafe hits excluded.
- **`rage_selection`** — the point *selection* mode, sibling of
  `Ragebot.selection_type` and `Ragebot.static_point_scale` /
  `Ragebot.adaptive_point_scale` in the menu namespace.

### 5.5 The script API (what a Lua script can see)

The module embeds LuaJIT 2.0.5 and registers a large API. The names are
contiguous in `.rdata`, so the table can be read off directly. Selected groups
(`verified` — the names, not the bindings):

- **Shot result** — `server_hitbox`, `client_hitbox`, `client_damage`,
  `server_damage`, `hitchance`, `shot_info`, `target_name`, `result`.
  A script can ask *what the server said* and *what we predicted*.
- **World state** — `get_alive`, `get_angles`, `get_origin`, `get_health`,
  `get_dormant`, `get_choke`, `get_ping`, `get_spread`, `get_inaccuracy`,
  `get_hitbox_position`, `get_realtime`, `get_curtime`,
  `get_intervalpertick`, `get_send_packet`, `get_button_state`, `get_framerate`,
  `get_frametime`, `get_framecount`, `get_level_name`, `get_map_group_name`,
  `get_maxclients`, `get_server_address`, `get_local_player`,
  `get_player_by_index`, `get_player_for_user_id`, `get_player_info`,
  `get_prop_int|float|bool|string`, `get_user_name`.
- **Command state** — `in_attack`, `in_attack2`, `in_back`, `in_run`, `in_use`,
  `in_cancel`, `in_left`, `in_right`, `in_moveleft`, `in_moveright`,
  `buttons`, `backtrack_ticks`, `aim_point`.
- **Script globals (settable)** — `safe_points`, `damage_override`,
  `fakeduck`, `slowwalk`, `body_aim`, `legit_enable`, `double_tap`,
  `flip_desync`, `thirdperson`, `automatic_peek`, `edge_jump`, `hide_shots`,
  `manual_back|left|right`, `key_binds`, `toggle`, `legit_automatic_fire`.
- **GUI construction** — `new_checkbox`, `new_label`, `new_combobox`,
  `next_line`, `is_menu_open`, `set_visible`, `disable_tab`, `is_active`,
  `set_menu_color`, `get_menu_alpha|pos|size|color`, `execute`.
- **Scripting plumbing** — `load_script`, `unload_script`, `set_callback`,
  `set_event_callback`, `create_interface`, `is_key_active`, `entity`,
  `cross_product`, `normalize`, `color`, `set_prop_int|float|bool`.
- **Trigger bot** — `trigger_enabled`, `trigger_hitchance`,
  `trigger_mindamage`, `trigger_penetration`, `trigger_reactiontime`,
  `trigger_flashcheck`, `trigger_smokecheck`, `trigger_head_hb`,
  `trigger_chest_hb`, `trigger_stomach_hb`.

**`safe_points` and `damage_override` are script-settable globals.** That is
the strongest evidence that the safe-point set and the damage override are
*data* the product exposes, not hard-coded behaviour — the same data model our
config should have.

---

## 6. Takeaways for this project

1. **The CS:GO chams recipe is small and portable in spirit**: build a material
   from a text blob, override the model-render material, and toggle one flag
   (`0x8000`) for the through-wall pass. In CS2 the same shape exists with
   different plumbing — see [`cs2-chams-t7.md`](cs2-chams-t7.md).
2. **The product's ragebot is "pick a point, then score it"**, with safe points
   and damage thresholds as first-class options. That is the vocabulary to
   adopt in our config even if the solver stays ours.
3. **A per-entity material palette is how a top-tier product gets variety**:
   the material is selected by an *entity class* (player / teammate / weapon /
   hands / fake-chams) and each class has its own pair (visible / through-wall).
   Our chams config should mirror that shape (class × flavour), not a single
   colour pair.
4. **"Fake chams" (`Player.fake_chams_enable`, `localplayerfake`) and "double
   colour" (`*_double_material_color`)** confirm the double-draw trick: draw the
   model twice with two materials. In CS2 the same trick is two `draw_scene_object`
   passes with two different `scene_data->material` values.
5. **Safe points and the damage override are *data*, not behaviour** (§5.5):
   `safe_points` and `damage_override` are script-settable globals, and the
   ragebot exposes `client_damage` **and** `server_damage` side by side. Our
   config should expose the same three things: a safe-point set, a damage
   override, and the predicted-vs-actual damage pair.
6. **A miss attributable to spread is detected and logged** (§5.3) — the
   product watches its own misses. Combined with `Ragebot.max_misses` this is a
   closed loop: shoot, observe, adapt. Our solver currently has no such loop;
   `player_hurt` gives us the same channel in CS2.
7. **Point selection is multi-point per hitbox with an unsafe-hitbox exclusion**
   (`rage_mphitbox`, `rage_unsfhitbox`, `Ragebot.ignore_unsafe_hitboxes`), and
   the *style* selector (`rage_sp_enabled_style`, `Ragebot.selection_type`)
   implies more than one selection algorithm — consistent with Fatality CS2's
   `fallback_mode` (`off` / `adaptive` / `prefer_safepoint`).
