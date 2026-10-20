# T7 — CS2 chams: how the material override actually works

> Research note, 2026-10-20. Answers the open T7 question from
> [`../handoff-wave2.md`](../handoff-wave2.md) §5.3: our payload has chams as
> **configuration only** (`ChamsMode`, `chams_color`) with no implementation, and
> the Neverlose mining answered "chams pass model, not material override" — so
> we needed a *shipped CS2 cheat's* material-override path.
>
> **Answer in one line.** In CS2 the override is **per scene object, not global**:
> you create a material with the KV3 material format, then in the
> `draw_scene_object` hook set `scene_data->material` to it, write the colour
> into the same scene data, and call the original draw — twice if you want both
> the visible and the through-wall variant.
>
> Evidence: Weave CS2 (`cs2/counterstrike2/…`), corroborated by the Fatality CS2
> base's material API, contrasted with gamesense's CS:GO implementation.
> Confidence labels as in [`neverlose-mining.md`](neverlose-mining.md).

---

## 1. Where the answer comes from

| Source | What it contributes | Confidence |
|---|---|---|
| `cacamelio/weave-v3-csgo-and-cs2`, `cs2/counterstrike2/feature/chams/` + `hooks/vtables/animatable_object_scene.cpp` + `sdk/interfaces/c_resource_system.hpp` | the **complete** CS2 path: material creation, material API, hook point, override call | `verified` (source read) |
| `notphage/fatality.win-CS2`, `src/lua/api/mat.cpp` (commented) + `src/game/cfg.h` | independent corroboration of the CS2 `IMaterial` surface and the product's material taxonomy | `verified` (source read) |
| `gamesense.dll` (CS:GO) | the CS:GO implementation, for contrast | `verified` (decompiled) — see [`gamesense-csgo-re.md`](gamesense-csgo-re.md) |

Quarantine copies live outside the repository under
`C:\Users\gpmpo\nea\quarantine\src\`.

**Caveat, stated up front.** Weave's CS2 tree is of unverified provenance (a
leak vs a community port — see [`cheat-dump-inventory.md`](cheat-dump-inventory.md)
§2.1). Its *structure* is internally consistent and the API indices below are
self-describing, but **every offset and index must be re-verified against the
current game binary** before use.

---

## 2. The CS2 material model

### 2.1 Materials are KV3 documents, not VMT

`feature/chams/chams.cpp:21-150` — every chams material is a **KV3 text blob**
(Source 2's material format), not a Source-1 `.vmt`:

`verified`. The format is `format:generic:version{…}`, and the *shader* is a
`.vfx` name (`csgo_unlitgeneric.vfx`, `csgo_complex.vfx`, `solidcolor.vfx`,
`csgo_effects.vfx`). The blob itself is a KV3 table — a header comment plus
key/value pairs; the full key list and the observed values are in the table
below, and the exact header string is given at the end of this subsection.

| Key | Kind | Meaning |
|---|---|---|
| `Shader` | string | the `.vfx` shader to use |
| `F_IGNOREZ` | int | draw through walls (1) or respect depth (0) |
| `F_DISABLE_Z_BUFFERING` | int | skip the depth test |
| `F_DISABLE_Z_WRITE` | int | skip depth writes |
| `F_TRANSLUCENT` | int | alpha-blended pass |
| `F_BLEND_MODE` | int | blend mode (1 = additive in the observed blobs) |
| `g_vColorTint` | float4 | the colour multiplier applied to the texture |
| `g_tColor` | resource | `materials/dev/primary_white_color_tga_*.vtex` (white) |
| `g_tNormal`, `g_tRoughness`, `g_tMetalness` | resource | `materials/default/default_normal_tga_*.vtex` |
| `g_tAmbientOcclusion` | resource | `materials/default/default_ao_tga_*.vtex` |
| `g_tMask1..3`, `g_tSceneDepth` | resource | `materials/default/default_mask_tga_*.vtex` (glow type only) |
| `g_flFresnelExponent` / `Falloff` / `Max` / `Min` | float | the rim effect of the glow type (7.0 / 10.0 / 0.1 / 1.0) |
| `TextureNormal` | resource | `materials/default/default_normal.tga` (the `csgo_complex` type) |

Every blob also carries the same two-part header: a
`kv3 encoding:text:version{e21c7f3c-…}` and a
`format:generic:version{7412167c-…}`. `verified`

### 2.2 The material API (vtable indices)

`verified` — `sdk/interfaces/c_resource_system.hpp`:

| Call | Vtable index | Notes |
|---|---|---|
| `i_material_system::FindMaterial(i_material*** out, const char* name)` | **14** | find an existing material |
| `i_material_system::CreateMaterial(i_material*** out, const char* name, void* material_data)` | **29** | called as `(this, out, name, kv3, 0, 0, 0, 0, 0, 1)` — the trailing `1` is a flag argument (`inferred`: create-if-missing) |
| `i_material_system::SetCreateDataMaterial(void* data, i_material*** mat)` | **37** | associates created data with a material (note: Weave's wrapper passes `(material, data)` into a slot declared `(data, material)` — **looks like an argument-order bug in their code**, worth re-deriving) |
| `i_material_system::SetColor(void* data, color_t)` | *not virtual* | writes RGBA **into the data struct at `+0x40 … +0x43`** |
| `i_material::get_name()` | **0** | |
| `i_material::get_name_with_mod()` | **1** | |
| `i_material::find_parameter(name)` → `material_paramater` | — | named parameter access (`g_vColorTint` etc.) with a `vec_value` |
| `i_material::update_parameter()` | — | push parameter changes |
| `i_material::ColorModulate(const color_t&)` | — | |

`SetColor` is the interesting one: it is a **plain struct write at `+0x40`** of
whatever object is passed. Weave passes the *scene data* to it, which is why
the chams colour is set on the draw call rather than on the material.

### 2.3 Creating a material from a KV3 blob

`verified` — `feature/chams/chams.cpp:240-264`, described rather than quoted:

1. **Initialise a `KeyValues3`** via a pattern-scanned setter in `client.dll`
   (`40 53 48 83 EC 20 48 8B 01 48 8B D9 44`), called with the basic type pair
   `kv_basic_null` / `kv_basic_string`. The `KeyValues3` object is treated as
   opaque (a 0x100-byte pad), and the leak carries a hand-written copy of the
   game's `kv_basic_type` / `kv_field_type_t` enumerations.
2. **Parse the KV3 text** via the pattern named `load_kv3`, called with the
   buffer, a `KV3ID_t` and an out-parameter. The `KV3ID_t` is
   `{ name = "generic", key = 0x469806E97412167C, key2 = 0x0E73790B53EE6F2AF }`
   — the `generic` format's GUID pair. A source comment records a hardcoded
   client address (`materialsystem + 0x00000001800C6308`) for the format
   descriptor.
3. **Create the material** via a pattern in **`materialsystem2.dll`**
   (`48 89 5C 24 08 48 89 6C 24 18 56 57 41 56 48`), called as
   `(nullptr, &out_material, "weave", &kv3, 0, 1)` — i.e. the material name is
   the cheat's own name, the KV3 is the definition, and the trailing `1` is the
   create-if-missing flag. The declared prototype is
   `void(__fastcall*)(uintptr_t*, i_material**, const char*, KeyValues3*, int, uint8_t)`.
4. **Return `*out_material`.**

`find_material` (`chams.cpp:9-16`) is the plain `FindMaterial` path for
materials that already exist in the game.

---

## 3. The override: `draw_scene_object`

`verified` — `hooks/vtables/animatable_object_scene.cpp:6-75`, installed at
`hooks.cpp:386` from the pattern table entry
`tls.cpp:539`: `{"scenesystem.dll", "48 8B C4 53 41 54 41 55 48 81 EC ? ? ? ? 4D 63 E1", 0, false, "draw_scene_object"}`.

The hook's signature is
`void(__fastcall*)(void* animtable_scene_object, void* dx11, c_scene_data* scene_data, int, void* scene_view, void* scene_layer, void*, void*)`.
Its body, in order:

1. **Refresh the local player** from the entity list
   (`get_split_screen_view_player(0)`), and bail to the original if the game is
   not in a live session or the local player is null.
2. **Resolve the owner** — `scene_data->scene_animatable_object->owner` is a
   `c_handle`; the pawn is fetched through the entity list, and the class is
   checked by **name hash** (`C_CSPlayerPawn`) rather than by a schema offset.
   A missing/invalid owner bails to the original.
3. **Enemy branch** — for an enemy player without gun immunity:
   - if the through-wall chams are enabled: set
     `scene_data->material` to the *invisible* material of the selected type,
     write the invisible colour, and call the original draw;
   - then, if the visible chams are enabled: set `scene_data->material` to the
     *visible* material, write the visible colour, and call the original draw
     again.
   - if neither is enabled, the original draw is called once, unmodified.
4. **Attachment branch** — an entity whose owner is a pawn but which is not
   itself a pawn (i.e. a weapon attached to a player) gets material type 1
   ("regular") and the attachment-chams colour, then the original draw.
5. Everything else falls through to the original draw.

**The mechanism, precisely:**

1. **Hook point** — the game's scene-object draw, in `scenesystem.dll`. The
   hook receives the scene data.
2. **Owner resolution** — `scene_data->scene_animatable_object->owner` is a
   `c_handle`; the pawn is fetched through the entity list. The check is by
   **class name hash** (`C_CSPlayerPawn`), not by a schema offset.
3. **Override** — `scene_data->material = <our material>`. The material lives
   on the *scene object*, so the override is per-draw, not global.
4. **Colour** — `SetColor(scene_data, colour)` writes RGBA at `scene_data+0x40`.
   The colour belongs to the **draw**, not to the material — which is why one
   material can serve many colours.
5. **Two passes** — through-wall first, then visible (or the reverse); each
   pass sets its own material and colour, then calls the original draw.
   The two materials differ by the KV3 flags (§4), not by being separate
   objects per colour.

**Attachment handling** is a nice detail: a *weapon* attached to a player is
not a `C_CSPlayerPawn`, so it takes the `is_attachment()` branch and gets the
"regular" material (type 1) — i.e. weapon chams come from the same draw hook.

---

## 4. The material library

`verified` — `feature/chams/chams.h` + `chams.cpp:18-152`. The class declares an
enum of material types — `flat`, `regular`, `metalic`, `glow`, `sky`
(`max_count` terminator) — a two-dimensional `Materials[type][2]` array, and an
accessor that returns `Materials[type][1]` for the through-wall variant and
`Materials[type][0]` for the visible one.

`verified`: **four** material sets are created (indices 0–3 → `flat`,
`regular`, `metalic`, `glow`); the `sky` entry is declared but never
initialised — a latent bug in the leaked tree.

| Type | Shader | Visible variant | Through-wall variant |
|---|---|---|---|
| 0 `flat` | `csgo_unlitgeneric.vfx` | `F_DISABLE_Z_BUFFERING = 0` | `F_DISABLE_Z_BUFFERING = 1` |
| 1 `regular` | `csgo_complex.vfx` | `F_IGNOREZ = 0`, `F_DISABLE_Z_WRITE = 0`, `F_DISABLE_Z_BUFFERING = 0` | `F_IGNOREZ = 1`, `F_DISABLE_Z_WRITE = 1`, `F_DISABLE_Z_BUFFERING = 1` |
| 2 `metalic` | `solidcolor.vfx` | `F_IGNOREZ = 0` | `F_IGNOREZ = 1` (+ z-write/buffering off) |
| 3 `glow` | `csgo_effects.vfx` | `F_IGNOREZ = 0` | `F_IGNOREZ = 1` |

Common to all: `F_TRANSLUCENT = 1`, `g_vColorTint = [1,1,1,a]`, and
`g_tColor` = `materials/dev/primary_white_color_tga_*.vtex` (a white texture so
the per-draw colour tint decides the appearance). The `glow` type adds
`g_flFresnelExponent 7.0`, `g_flFresnelFalloff 10.0`, `g_flFresnelMax 0.1`,
`g_flFresnelMin 1.0` plus `g_tMask1..3` and `g_tSceneDepth` — i.e. a
scene-depth-driven rim effect.

**The through-wall variant is a different *material*, not a runtime flag.**
That is the key structural difference from CS:GO.

---

## 4.1 The richer material API (found in the tree's newer revision)

`verified`. The checked-in `feature/chams/chams.cpp` is **not** the newest
revision in the leak: `chams.cpp~RF617ff31.TMP` (20 KB vs 11.8 KB) contains
material code that the checked-in file does not. It adds two capabilities that
change the picture:

**1. Material enumeration.** `IResourceSystem::enumerate_material(fourcc, array,
flags)` — called with the four-character code `0x74616D76` (`"vmat"`) and
flags `2` — fills a `resource_array_t` (`m_nCount`, `m_aResources`, an array of
`i_material**`). The tree uses it to walk **every material in the game**
(`chams.cpp~RF617ff31.TMP:343`, `:403`).

**2. Material cloning.** `c_chams::reconstruct_material(i_material*)` takes an
*existing* game material and produces a **new** material that copies its
appearance, with two paths (`:268-330`):

- if the source material has a `g_tColorA` parameter → build a
  `csgo_simple_2way_blend.vfx` material and copy the `g_tColorA`, `g_tColorB`,
  `g_tNormalA`, `g_tNormalB` **texture** values across;
- otherwise → build a `generic.vfx` material and copy the `g_tColor` and
  `g_tNormal` textures.

The clone is then installed by **replacing the pointer in the resource array**
(`*pmat = material`). The two-way-blend path is what makes a *two-colour*
material possible — the "double colour" chams variant that gamesense also has
(§6 of [`gamesense-csgo-re.md`](gamesense-csgo-re.md)).

**3. Direct parameter editing.** `i_material::find_parameter(name)` returns a
`material_paramater` with a `vec_value` (`vector_4d`) and a `texture_value`;
`i_material::update_parameter()` pushes the change. The tree recolours
materials by writing `g_vColorTint` and calling `update_parameter()`
(`:377-390`).

**Why this matters for us.** There are two ways to chams a model in CS2:

- **build a material from scratch** (the checked-in Weave path: hardcoded KV3
  blobs, fixed shaders, fixed textures) — simple, but the look is whatever the
  eight hardcoded blobs give you; or
- **clone the model's own material and edit it** (the newer path) — the chams
  then look like the weapon/skin, and you can still recolour it. That is the
  technique a modern product would use, and it also solves the
  "colour per draw" problem differently: the material carries the texture, the
  `g_vColorTint` parameter carries the colour.

The clone path needs `enumerate_material` to find the material and a way to
swap the pointer the renderer uses — which the scene-object override
(`scene_data->material`, §3) provides directly, without touching the resource
array at all. **Our implementation should prefer cloning**, because it does not
depend on hardcoded resource paths that change between game updates.

---

## 5. Corroboration: Fatality CS2


`verified` — the Fatality CS2 base (`notphage/fatality.win-CS2`, 2024-01)
independently shows the same API shape:

- `src/lua/api/mat.cpp` (commented out in that leak) exposes exactly this
  surface: `material:modulate(color)`, `material:set_flag(flag, state)`,
  `material:get_flag(flag)`, `material:find_var(name)`,
  `material:get_name()`, `material:get_texture_group_name()`, and
  `interfaces::material_system()->CreateMaterial(name, kv)` /
  `FindMaterial(name, group)`. So **Fatality CS2 also creates materials from a
  KV3 blob and addresses them by name/parameter** — the same design.
- `src/game/cfg.h:290` has the product's material taxonomy, which is *richer*
  than Weave's: `cham_none`, `cham_normal`, `cham_flat`, `cham_rim_glow`,
  `cham_contour`, `cham_flow`, `cham_acrylic`, `cham_legacy`, `cham_sprite`, …

  i.e. rim glow, contour (outline), flow (animated), acrylic, sprite. Only the
  *names* leaked — the implementations did not — but the taxonomy tells us what
  a shipped CS2 product considers worth supporting: **outline/contour** and
  **animated flow** are the two we have no idea how to do.

**Consequence.** Two independent CS2 products both use *KV3 material creation +
per-scene-object material override*. That is the CS2 idiom; it is not a Weave
quirk.

---

## 6. Contrast: the CS:GO implementation (gamesense)

`verified` (decompiled) — [`gamesense-csgo-re.md`](gamesense-csgo-re.md) §2.

| | CS:GO (gamesense) | CS2 (Weave / Fatality) |
|---|---|---|
| Material format | **VMT** text (`VertexLitGeneric`, `$ignorez`, `$additive`, `$envmapfresnel`, …) | **KV3** text (`csgo_unlitgeneric.vfx`, `F_IGNOREZ`, `g_vColorTint`, `g_tColor`) |
| Material creation | `client.dll` functions found by pattern; material named `safeless_<n>` | `materialsystem2.dll` pattern + `KeyValues3`/`LoadKV3`; material named per cheat |
| Override target | **global**: `model_render->forced_material_override(material)` (engine interface `VEngineModel`, vtable slot 1) | **per scene object**: `scene_data->material = material` |
| Through-wall | one material + a flag setter called with `0x8000` | a **separate material** whose KV3 sets `F_IGNOREZ = 1` |
| Colour | set on an interface slot before the override | `SetColor(scene_data, rgba)` — RGBA written at `scene_data+0x40` |
| Draw entry | `DrawModelExecute` hook, vtable index 21 (Weave CS:GO) | `draw_scene_object` hook in `scenesystem.dll` |

Weave's own CS:GO tree shows the CS:GO variant of the override in one place
(`csgo/csgo_internal/src/hooks/model_render_hook.cpp`): it hooks
`DrawModelExecute`, skips re-entry via `studio_render->is_forced_material_override()`,
sets `model_render->forced_material_override(nullptr)` afterwards, and drives
the chams through `chams->is_working(state, info, draw_callback)` — the callback
form is how the "fake chams" / matrix-modified draw is done.

---

## 7. What this means for our payload

1. **The hook is `scenesystem.dll`'s scene-object draw**, not the model's
   `DrawModel`. Our payload already hooks render paths for the overlay; the
   scene-object draw is a *different* function and must be added.
2. **The override is a struct write**, not a vtable call: `scene_data->material`
   plus RGBA at `scene_data+0x40`. That is cheap and does not require
   `IModelRender`-equivalent state.
3. **We need one material per (type × through-wall) pair** — but build them by
   **cloning** the model's own material (§4.1) rather than from hardcoded KV3
   blobs: the clone survives game updates (no hardcoded resource paths) and
   can be recoloured per draw. Fall back to the hardcoded-blob approach only if
   cloning proves unreliable.
4. **Colour belongs to the draw** (`SetColor` at `scene_data+0x40`, or the
   `g_vColorTint` parameter on a cloned material), so one material serves many
   colours — do not create a material per colour.
5. **Our existing `ChamsMode` / `chams_color` config should become
   (type, flavour, colour)** — the product taxonomies (4–9 material types,
   visible + through-wall) are the vocabulary to match.
6. **The "contour"/outline and "flow"/animated variants are the interesting
   unknowns**: a shipped CS2 product supports them, Weave's public tree does not
   implement them, and we have no public reference for how they are built
   (`unknown`).

## 8. What is still open

- **Re-verify every index** against the current client: `FindMaterial` = 14,
  `CreateMaterial` = 29, `SetCreateDataMaterial` = 37, `i_material` vtable
  0/1, the `scene_data->material` field offset, and the `+0x40` colour offset.
  Weave's tree is CS2-era and unprovenanced.
- **The KV3 format GUID pair** (`0x469806E97412167C` / `0x0E73790B53EE6F2AF`)
  and the `format:generic:version{…}` header are version-specific.
- **How the "sky" material type (index 4) was meant to work** — declared,
  never created in the leaked tree.
- **Whether the colour write belongs on the scene data or on the material**:
  Weave writes it on the scene data, which suggests the draw carries a
  per-draw tint. If we instead set `g_vColorTint` on the material
  (`i_material::find_parameter` + `update_parameter()`), we would need one
  material per colour. The scene-data route is better; we should confirm it
  against a current build.
- **Which revision is the real one?** The tree contains a `.TMP` backup
  (`chams.cpp~RF617ff31.TMP`, 20 KB) with material *cloning* and *enumeration*
  code that the checked-in file lacks (§4.1), and similar larger backups exist
  for `player_esp`, `hooks`, `tls` and `inventory_changer`. The leak is a
  working tree, so the newest revision of several modules is in the backups.
  A diff of the `.TMP` set against the checked-in set is cheap and may
  recover working code the checked-in files lost.
- **Neverlose answered "chams pass model"** — i.e. that product did *not* do a
  material override. So the two top-tier CS2 products differ in approach, and
  the override is a *choice*, not a requirement. Our implementation should keep
  the pass-model path as a fallback.
