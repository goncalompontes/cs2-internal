# The chams' colour — the material system's parameter setter

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.

> Companion to `docs/re/chams.md`'s predecessor (`docs/re/…` — the material
> system's first pass is in the feature brief) and to the chams
> implementation in `payload/features/src/chams.cpp`.
>
> **Build note.** Everything below was derived on the live
> `materialsystem2.dll` (sha256
> `b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6`, image
> base `0x180000000`) and the live `client.dll` (sha256 `659940c1…`).

## The question

The 3D chams substitute the game's own flat material in the scene system's
draw, but the material's **own** colour was used: the material system's "set
a parameter by name" entry point had not been located. This note records that
entry point, the colour parameter's name, and what is still open.

---

## 1. The setter — `CMaterialSystem2::SetMaterialVectorParam`

**Location.** `CMaterialSystem2` vtable **slot 23** (`+0xB8`) →
`materialsystem2.dll 0x18003bb30` (`FUN_18003bb30`).

**Signature** (from the decompilation):

```c
// this   = the CMaterialSystem2*
// handle = the handle FindMaterial returned
// name   = the parameter's name
// value  = "r g b a", parsed with sscanf("%f %f %f %f")
bool CMaterialSystem2::SetMaterialVectorParam(void* handle, const char* name, const char* value);
```

The decompiled body:

```c
if (handle == 0 || *(int *)(handle + 0x20) < 1) return false;   // the handle must be alive
void* material = *handle;                                         // the IMaterial2
if (material == 0) return false;
if (*(byte *)(material + 0x0b) & 2) return false;                // the material is not usable

float rgba[4] = {0, 0, 0, 0};
int parsed = V_sscanf(value, "%f %f %f %f", &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
if (parsed == 1) rgba[1] = rgba[2] = rgba[3] = rgba[0];            // "1" means "1 1 1 1"
else if (parsed == 2) rgba[3] = rgba[2];
else if (parsed == 3) rgba[3] = rgba[2];

float* entry = FUN_1800117b0(material, name);                    // find the parameter by name
if (entry != 0) {
  *(uint8 *)(entry + 0x38) = 0;                                  // clear the "from the .vmat" flag
  entry[0] = rgba[0]; entry[1] = rgba[1]; entry[2] = rgba[2]; entry[3] = rgba[3];
  … push the material onto the system's "changed" list …
  FUN_18000fa30(material, 0, material + 0x70);                    // mark the material dirty
  … notify the renderable listeners …
}
return entry != 0;
```

**The return value is the whole answer to "can this material be tinted?"**: the
entry point returns **false** when the material has no parameter with that
name. The payload uses that: when the setter refuses, the chams keep the
material's own colour and the feature stays alive (fail closed).

**The handle.** `FindMaterial` (`CMaterialSystem2` vtable slot 14, `+0x70`)
writes the material system's *handle* into its out parameter and **returns
the out parameter itself** (the machine code ends with `MOV RAX, RDI` — the
out pointer — so the return value is not the handle). The handle's first word
is the material and its reference count is at `+0x20`; the client's own call
site (`CPhysicsGameSystem::GameActivate`, `client.dll 0x1803b45c0`) increments
that reference count, which is what proves the handle is a separate object
and not the material itself (the material's `+0x20` is its parameter array).

So:

| value | what it is |
|---|---|
| `FindMaterial`'s return value | the address of the caller's out variable |
| `*out` | the **handle** (the reference count at `+0x20`) |
| `*handle` | the **material** (the `IMaterial2*` the scene system's draw takes) |

The payload therefore keeps both: `game::Material` (the material, for the
draw) and `game::MaterialHandle` (the handle, for the setter).

> **A bug this found.** The payload's `find_material` read the material from
> the *return value* of `FindMaterial` — that is the address of the out
> variable, so it got the **handle** where the material belongs, and the
> chams substituted the handle for the material in the scene system's draw.
> The indirection is now `*handle`, and the material's vtable pointer is
> checked against `materialsystem2.dll` before the material is handed out.

### The material's own parameter table

`CMaterial2` (vtable `materialsystem2.dll + 0x129ad0`):

| offset | what |
|---|---|
| `+0x18` | the number of parameters |
| `+0x20` | the parameter array, **0x40 bytes per entry** |
| entry `+0x00` | the parameter's **float4** value |
| entry `+0x28` | the parameter's **name** (a C string) |
| entry `+0x38` | "the value came from the material's data" flag |

Verified from the material's own accessors: `FUN_1800117b0` (`CMaterial2`
vtable slot 36's helper) walks the array and compares the name at `+0x28`
with `V_stricmp_fast`; `FUN_180011680` (vtable slot 35) reads the float4 at
`+0x00` of the entry it found. The name is a **plain string**, not a hash —
the hash is only used for the *global* shader constants and for the
`uint32` map at `CMaterial2+0x180`.

### Reading a parameter back

`CMaterial2::GetVectorParam` — vtable **slot 35** (`+0x118`) →
`materialsystem2.dll 0x180011680`:

```c
void CMaterial2::GetVectorParam(float4* out, const char* name, const float4* fallback);
```

It writes the material's own value into `out`, or the fallback when the
material has no such parameter. This is how the payload remembers the
material's own colour so it can put it back.

### The `uint32` map at `CMaterial2+0x180` — not the colour

The map at `CMaterial2+0x180` (read by `FUN_18001c710` and by the inlined
SIMD lookup in `CMaterial2` vtable slot 10, `0x18000bb80`) maps a **name
hash** to a **uint32**. It holds the material's *integer* parameters
(the two texture handles the previous round found: `0xCF382AB`,
`0x28C67ADC`). A colour is a float4, so it does **not** live there — this is
why the previous round could not find a colour setter there.

---

## 2. The colour parameter's name

The colour parameter's name is **per shader**, and CS2 does not ship the
`.vmat` sources — only the *compiled* material (`.vmat_c`) and the compiled
shader (`.vcs`). The names were recovered from the compiled shader's own
**parameter table**, which stores each parameter's name as the material
system's 32-bit name hash.

### How the names were read out

The compiled shader's `DATA` chunk is a binary KeyValues3 document. Inside
it, the parameter table is a run of

```
0x1d <u32 name hash> <type/default …>
```

records. The hash is the material system's own name hash
(`material_parameter_hash`, MurmurHash2 case-folded, seed `0x31415926`), so
hashing a candidate name and comparing it identifies the parameter.

* `shaders_pc_dir.vpk` → `shaders/vfx/tools_wireframe_pc_50_features.vcs`
  → **`g_vOverrideColor`** (`0xD4BD94B3`).
* `shaders_pc_dir.vpk` → `shaders/vfx/csgo_unlitgeneric_pc_50_features.vcs`
  → **`g_vColorTint`** (`0x20A77985`), `g_vTexCoordScale` (`0xB0C3F217`),
  `g_nScaleTexCoordUByModelScaleAxis` (`0xAF097E8F`),
  `g_nScaleTexCoordVByModelScaleAxis` (`0xE67E163F`).
* `materialsystem2.dll`'s own shader parameter table (`.rdata`, around
  `0x180102C28` … `0x18010D475`) → **`g_vWireframeColor`** (`0xCE09A55C`),
  `g_vColorTint` (`0x20A77985`, `float3`), `g_flModelTintAmount`
  (`0x55FC14C5`), `g_vTexCoordScale`/`Offset`, …

The encoding was validated on the `csgo_unlitgeneric` shader: four of its
marked hashes are the hashes of names that are in the material system's own
parameter table (`g_vColorTint`, `g_vTexCoordScale`, `g_vTexCoordOffset`,
`g_nScaleTexCoord{UV}ByModelScaleAxis`), so the marked words really are the
parameter names' hashes. The remaining names were recovered by hashing
candidate names against the table (the engine's `g_v…`/`g_t…`/`g_fl…`
convention, over the engine's own vocabulary).

The four names the payload uses, with their hashes (the host test pins
both):

| name | hash | where it comes from |
|---|---|---|
| `g_vOverrideColor` | `0xD4BD94B3` | the `tools_wireframe` shader (the wireframe material) |
| `g_vWireframeColor` | `0xCE09A55C` | the material system's embedded wireframe shader |
| `g_vColorTint` | `0x20A77985` | the `csgo_unlitgeneric` shader (the "single colour" material) |
| `g_vColor` | `0xF05CC739` | the conventional unlit colour name |

The payload asks the **material** which of these it has (the material's own
parameter table lists the names), and falls back to the material's own
parameter names when it has exactly one colour-like name. When the material
has none, the setter refuses and the chams keep the material's own colour.

---

## 3. The VPK route — what the archives actually contain

The spec's first suggestion was to read `materials/physics/flat_basic.vmat`
out of the VPK. A VPK reader was written for it (the format is a directory
tree of file entries; the Source 2 version 2 tree is
`{ext}{path}{name}{crc, preloadBytes, archiveIndex, entryOffset,
entryLength, terminator, preload}` and the archives are
`pak01_<nnn>.vpk` next to `pak01_dir.vpk`). The previous round's reader had
two bugs — the file-entry field widths (`<IHHIH` instead of `<IHIIH`) and a
tree start 16 bytes too late — and parsed nothing; the corrected reader
walks `pak01_dir.vpk` exactly to the end of the tree (137 695 entries).

What the archives show:

* **CS2 ships only *compiled* materials.** The tree has 22 010 `vmat_c`
  entries and **no** `vmat` entries at all: the `.vmat` sources are not
  shipped, so the parameter names cannot be read as text.
* **`materials/physics/flat_basic.vmat` is not in the archives.** Neither are
  `materials/physics/wireframe_basic.vmat`,
  `materials/physics/wireframe_hidden.vmat` or
  `materials/tools/show_collision_geometry_wire*.vmat`. The four names the
  *client* uses for its physics debug draw (`client.dll 0x181b06908` …
  `0x181b06998`, next to the `cl_hitbox_debug` convar) are **engine
  built-ins**: the material system creates them, the VPK does not carry
  them. So the material's own parameters are not in the game data at all —
  which is why the name had to come from the *shader* side.
* The materials that *are* shipped are useful as cross-checks:
  `materials/engine/singlecolor.vmat_c` (the shader `csgo_unlitgeneric.vfx`,
  the texture parameter `g_tColor`, three float parameters) and
  `materials/dev/player_visibility_datacolor.vmat_c` (the engine's own
  data-colour material — it has **no** float parameters, which is why the
  engine tints the player-visibility path through a *scene* value and not
  through the material).
* The compiled material's parameter names are in a **binary KeyValues3**
  document whose strings are stored in a shared table; the document was not
  decoded within this round's budget. The *shader*'s parameter table, which
  stores the names as hashes, was.

---

## 4. The route taken, and what it costs

The payload calls the engine's own setter. There is **no write into engine
memory**: the payload only *reads* the material's parameter table (through
the fault-safe `game::read` helper) to learn the parameter's name, and then
calls `SetMaterialVectorParam` with that name and a `"r g b a"` string.

Runtime cost:

* **Per colour change**: one walk of the material's parameter table (a
  handful of entries) and one engine call.
* **Per material change** (a mode switch): the same, plus the restore of the
  old material's colour.
* **Per frame**: nothing. The chams' render path (the scene system's draw)
  is untouched; the colour is a property of the material, not of the draw.

---

## 5. The restore path

1. Before the first write the payload reads the material's own value with
   `CMaterial2::GetVectorParam` (`vtable` slot 35) and keeps it.
2. When the operator changes the colour, the material's value is simply
   overwritten; the *kept* value stays the material's own, so the restore
   still puts the original back.
3. When the chams are switched off, or the mode (and therefore the
   material) changes, the payload writes the kept value back with
   `SetMaterialVectorParam` and clears its "applied" flag.
4. **Caveat.** The setter clears the entry's "the value came from the
   material's data" flag (`entry+0x38`) and does not set it back. The
   *value* is restored exactly; the flag is not. Nothing in the engine
   reads that flag at draw time (it is only used when the material's
   parameters are re-initialised), so the visible result is the same.

---

## 6. What is still unknown

1. **The flat material's colour parameter name.** The wireframe material's
   is `g_vOverrideColor` (read out of the `tools_wireframe` shader's own
   parameter table) and the unlit one is `g_vColorTint`. The
   `visualize_physics` shader — the one the physics debug's flat material
   uses — has six parameters whose hashes are `0x495EB854`, `0x1E1DEB11`,
   `0xB82AE51E`, `0x109FE95A`, `0x8004723A` and `0x6843C13D`; their names
   could not be resolved from the binaries within this round's budget (the
   brute force covered ~14 M generated names from the engine's vocabulary).
   The payload therefore also scans the material's own parameter names for a
   colour-like one, and applies the colour when there is exactly one.
2. **Whether the built-in physics materials resolve at all.**
   `materials/physics/flat_basic.vmat` is not in the VPK; whether
   `FindMaterial` creates it (or returns the error material) was not
   verified — the game could not be launched from this session (no GPU in
   session 0).
3. **The material's shader.** The flat material's shader is inferred
   (`visualize_physics`, from the `materials/physics/` path and the shader
   list) and not proven.
4. **Whether the renderer re-reads the parameter's value per draw.** The
   setter marks the material dirty and notifies the renderable listeners, so
   the engine's own path is sound; the payload uses the same entry point, so
   it gets the same treatment — but this was not observed live.
5. **The `uint32` map at `CMaterial2+0x180`** still holds two unidentified
   parameter hashes (`0xCF382AB`, `0x28C67ADC`). They are *not* colour
   parameters (a float4 does not fit a uint32 map), and they are not the
   hash of any string in `materialsystem2.dll`, `client.dll` or
   `rendersystemdx11.dll` (172 304 distinct strings hashed, no hit).

## 7. The evidence index

| VA / RVA | Module | What it proves |
|---|---|---|
| `0x18003bb30` / `0x3bb30` | materialsystem2 | `CMaterialSystem2::SetMaterialVectorParam` — the setter |
| `0x18012d920` / `0x12d920` | materialsystem2 | the `CMaterialSystem2` vtable (slot 23 = `+0xB8`) |
| `0x180037200` / `0x37200` | materialsystem2 | `CMaterialSystem2::FindMaterial` (slot 14 = `+0x70`) |
| `0x180129ad0` / `0x129ad0` | materialsystem2 | the `CMaterial2` vtable (slot 35 = `+0x118`) |
| `0x180011680` / `0x11680` | materialsystem2 | `CMaterial2::GetVectorParam` — reads a parameter's value |
| `0x1800117b0` / `0x117b0` | materialsystem2 | find a parameter entry by name (the array at `+0x20`, the name at `+0x28`) |
| `0x18000bb80` / `0xbb80` | materialsystem2 | `CMaterial2` slot 10 — the `uint32` map lookup at `+0x180` |
| `0x18001c710` / `0x1c710` | materialsystem2 | the `uint32` map getter (hash → uint32) |
| `0x180102C28` … `0x18010D475` | materialsystem2 | the material system's own shader parameter table (`g_vColorTint`, `g_vWireframeColor`, …) |
| `0x181b06908` … `0x181b06998` | client | the physics debug's four material paths (built-ins, not in the VPK) |
| `shaders/vfx/tools_wireframe_pc_50_features.vcs` | shaders_pc_dir.vpk | the `tools_wireframe` shader's parameter table (`g_vOverrideColor`) |
| `shaders/vfx/csgo_unlitgeneric_pc_50_features.vcs` | shaders_pc_dir.vpk | the `csgo_unlitgeneric` shader's parameter table (`g_vColorTint`, …) |
| `materials/engine/singlecolor.vmat_c` | pak01_dir.vpk | the "single colour" material (the unlit shader, `g_tColor`) |
| `materials/dev/player_visibility_datacolor.vmat_c` | pak01_dir.vpk | the engine's data-colour material (no float parameters) |
