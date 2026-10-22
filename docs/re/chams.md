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

---

## 8. The draw hook's target — re-derived from the class's own RTTI

> **Appended by task T26 (2026-10-21).** The sections above are the **material
> system's**; this one is the **hook target** — the address the chams' draw
> hook patches, how it was re-derived, and how it is now resolved and
> verified.
>
> **The build this was measured on** — the live module on this machine:
>
> | what | value |
> |---|---|
> | the file | `F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\scenesystem.dll` |
> | sha256 | `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5` |
> | PE timestamp | `0x6ACFCD4F` (1792003407) |
> | image base | `0x180000000` |
> | `SizeOfImage` | `0x978000` |

### 8.1 What was wrong with the old target

The address the hook patched was taken from the module's RTTI **lambda**
types: the mangled name

```
.?AV<lambda_1>@?1??DrawPrimitive@CAnimatableSceneObjectDesc@@QEAAXPEAVIRenderContext@@PEAUCMeshDrawPrimitive_t@@PEBVIMaterial2@@PEAVIMaterialMode@@AEAUDrawValues_t@@_NAEAUSCeneSystemPerFrameStats_t@@PEAVIScSceneLayer@@@Z@
```

decodes to a **member** function (`Q` public, `E` 64-bit, `A` __cdecl, `A`
near, `A` unqualified, `X` void, then `this` plus the eight arguments) — but
the type it was taken from is the type of a **lambda**, and a lambda's
`operator()` is **static**: it takes the eight arguments and has **no
`this`**. The detour assumes the member function's shape, so patching the
lambda's `operator()` shifts the whole argument list by one: the callee
receives the material pointer where it expects the mesh pointer and
dereferences it. That is the `0xc0000005` the live test produced, with the
fault inside the renderer.

The address was also **not verified**: the old guard asked only whether the
address was inside the module (`scene.contains(target)`), which a stale RVA
satisfies.

### 8.2 The re-derivation — the class's own RTTI and vftable

| what | address (VA) | RVA |
|---|---|---|
| the type descriptor's name string `.?AVCAnimatableSceneObjectDesc@@` | `0x18067C458` | `0x67C458` |
| the **type descriptor** (the name is at +0x10) | `0x18067C448` | `0x67C448` |
| the **complete object locator** | `0x180606DC0` | `0x606DC0` |
| the class hierarchy descriptor | `0x180606CF0` | `0x606CF0` |
| the pointer to the COL (the vftable's `[-1]` slot) — the **only** one in the module | `0x1805E5C88` | `0x5E5C88` |
| ⇒ the **vftable** starts at | `0x1805E5C90` | **`0x5E5C90`** |

This build's RTTI uses **32-bit RVA fields** inside the COL (`pTypeDescriptor`
at +0x0C, `pClassDescriptor` at +0x10, `pSelf` at +0x14), which is why the COL
is not the usual 8-byte-pointer shape. The vftable itself is a normal x64
array of **8-byte** pointers.

The vftable's first four slots:

| slot | RVA | VA | what |
|---|---|---|---|
| 0 | `0x5E5C90` | `0x1800541D0` | `lea rax,[rip+0x591D59]; ret` — returns the class-name string (a `GetTypeName` virtual) |
| 1 | `0x5E5C98` | `0x180059FC0` | the **batch** wrapper (see below) |
| 2 | `0x5E5CA0` | `0x18001C240` | a three-byte `ret 0` stub |
| 3 | `0x5E5CA8` | `0x18005B030` | a 1278-byte function |

**Slot 1 is not `DrawPrimitive`.** `FUN_180059FC0` is the *batch* entry
point: it takes a primitive, a **count**, and loops, calling the real draw
once per primitive. It has **no static callers** — it is reached only through
the vtable. Its own arguments are

```
(this, IRenderContext*, CMeshDrawPrimitive_t*, int count, const IMaterial2*,
 const IMaterialMode*, CSceneSystemPerFrameStats_t*)
```

— the material is the **fifth** argument there, *not* the fourth. (A detour
written for that shape would shift the arguments the same way the lambda's
did.)

### 8.3 The real draw

`FUN_180059FC0` calls the real draw at `0x18005A465` (RVA `0x5A465`), and the
callee is:

> **`scenesystem.dll` RVA `0x580A0`** (VA `0x1800580A0`)

It is a **nine-argument** member function, and its arguments are settled by
its own body and by the call site:

| register / stack | argument | how it was identified |
|---|---|---|
| `RCX` | `this` (the scene object desc) | it is passed to `FUN_18007D500(desc, …)`, which reads the desc's own fields (`+0x90` RWLock, `+0xD0` array count, `+0xD8` array) and names its own source file `scenesystem\sceneobjectdescs.cpp` |
| `RDX` | `IRenderContext*` | the body calls `(**(code **)(*ctx + 0x498))(ctx)` |
| `R8` | `CMeshDrawPrimitive_t*` | `FUN_18007D500(this, R8)` reads the primitive's 16-bit index at `+0x372A` |
| **`R9`** | **`const IMaterial2*`** | the body calls `[R9 + 0x130]` and `[R9 + 0x4B8]` — the `IMaterial2` vtable; `+0x18` of it is the material's parameter count |
| `[RSP+0x28]` | `const IMaterialMode*` | the body hashes parameters at `mode + 0x1F0` |
| `[RSP+0x30]` | `DrawValues_t*` | — |
| `[RSP+0x38]` | `bool` | read as a **byte** (`MOVZX EAX, byte ptr [RBP + 0x480]`) |
| `[RSP+0x40]` | `CSceneSystemPerFrameStats_t*` | — |
| `[RSP+0x48]` | `ISceneLayer*` | — |

**This is exactly the signature the payload already has** — nine arguments,
the material at **index 3** counting `this`:

```c
using DrawPrimitiveFn = void(__fastcall*)(void* desc, void* render_context, void* mesh_primitive,
                                          const void* material, const void* material_mode,
                                          void* draw_values, bool deferred, void* stats,
                                          void* layer);
```

**The payload's detour was never wrong; only its target address was.** The
detour and the typedef stay as they are.

### 8.4 The prologue, and the signature that finds it

```
0x1800580A0: 4C 89 4C 24 20   MOV qword ptr [RSP + 0x20], R9
0x1800580A5: 48 89 54 24 10   MOV qword ptr [RSP + 0x10], RDX
0x1800580AA: 55               PUSH RBP
0x1800580AB: 53               PUSH RBX
0x1800580AC: 56               PUSH RSI
0x1800580AD: 57               PUSH RDI
0x1800580AE: 41 54            PUSH R12
0x1800580B0: 41 55            PUSH R13
0x1800580B2: 41 57            PUSH R15
0x1800580B4: 48 8D AC 24 F0 FB FF FF   LEA RBP, [RSP - 0x410]
0x1800580BC: 48 81 EC 10 05 00 00      SUB RSP, 0x510
```

* The first **14 bytes** (the two `MOV`s and the four `PUSH`es) contain **no
  RIP-relative operand and no relative branch** → the repo's inline hook
  (`payload/hooks/src/inline.cpp`) can displace them; the hook's jump is 14
  bytes (`jmp [rip+0]` plus the 8-byte destination), so the displaced region
  is exactly those 14 bytes.
* The **20-byte** prologue occurs **exactly once** in `scenesystem.dll`.

The uniqueness was measured on the live file with the dumper's own scanner
(`cs2-dumper --file <scenesystem.dll> --pattern …`, which maps the file to
RVA addressing):

| the pattern | matches in `scenesystem.dll` |
|---|---|
| `4C 89 4C 24 20 48 89 54 24 10` (10 bytes) | 10 |
| `4C 89 4C 24 20 48 89 54 24 10 55 53` (12 bytes) | **1** — at `+0x580A0` |
| `… 56 57` (14 bytes) | **1** |
| `… 41 54` (16 bytes) | **1** |
| `… 41 55` (18 bytes) | **1** |
| `… 41 57` (20 bytes) | **1** |

The 20-byte prologue is the one the dumper's curated signature carries
(`external/dumper/src/analysis.rs`):

```rust
sig("scenesystem.dll", "scene_draw_primitive",
    "4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57", None, None)
```

and it resolves to the RVA above:

```
$ cs2-dumper --file scenesystem.dll --resolve-offsets --json
[ { "name": "scene_draw_primitive", "module": "scenesystem.dll",
    "rva": 360608, "source": "signature" } ]      # 360608 == 0x580A0
```

### 8.5 How the payload uses it

ADR-0012 forbids a hard-coded RVA, and a hard-coded RVA is exactly what
killed the game twice (the chams and the third-person camera). So the address
is **resolved**, not asserted:

1. `cs2::sdk::find_offset("scene_draw_primitive")` — the dumper's signature,
   carried by the calibration dump into the generated SDK. A **missing** offset
   means the dump predates the signature: the operator refreshes it with
   `scripts/calibrate-live.py` (which now calibrates `scenesystem.dll` too).
2. `game::find_module("scenesystem.dll")` — the module the offset belongs to.
3. The **20 bytes at the resolved address** are read through the fault-safe
   reader (`game::read_bytes` — the injector registers the payload's
   `.pdata`, so the image *does* have exception tables, and a bad address
   must never fault) and compared with the prologue above.

Only then is the entry patched. The rule itself is the pure function
`cs2::features::hook_target::decide`
(`payload/features/include/cs2/features/hook_target.hpp`), which the host
test `payload/features/tests/test_chams_target.cpp` drives: the right
prologue is accepted, a changed, a truncated or an unreadable one is refused,
and a missing offset is refused.

**The detour's re-entrancy guard is not a `thread_local`.** The payload is
manually mapped and its TLS block is not installed on the game's threads, so
a thread-local guard reads a null TLS base — the crash the camera's detour
hit (`read addr=0x4`; the mapped image's TLS slot was 154, above the 64 slots
the module's TLS bootstrap supports). The draw and the original it calls are
synchronous on one thread, so a plain depth counter gives the same
protection.

### 8.6 The lambda, for the record

The lambda's type descriptor is at VA `0x180678360` (its name string at
`0x180678370`), and it is the *lambda* type — not the member function. The
lambda's `operator()` is a **separate static function**; hooking it is what
broke the game. Nothing in the payload hooks a lambda.

### 8.7 What is still open

1. **The live run.** The calibration dump has to be refreshed
   (`scripts/calibrate-live.py`, with the game running) before the
   `scene_draw_primitive` offset exists; until it does, the chams stay off
   with the reason *no offset* and nothing is patched.
2. **The detour itself has never run against the real draw.** The address and
   the signature are proven (§8.2–§8.4), but the substitution — the material
   argument being swapped for the flat material — has not been observed in a
   live game.
3. **The nine arguments were read out of the body, not from a header.** There
   is no PDB for the shipped module; the identification rests on the call
   site and on the body's own uses of `R9` and the stack slots.

### 8.8 The evidence index for this section

| VA / RVA | Module | What it proves |
|---|---|---|
| `0x18067C448` / `0x67C448` | scenesystem | the type descriptor of `CAnimatableSceneObjectDesc` |
| `0x180606DC0` / `0x606DC0` | scenesystem | the complete object locator |
| `0x1805E5C88` / `0x5E5C88` | scenesystem | the vftable's `[-1]` slot — the only pointer to a COL in the module |
| `0x1805E5C90` / `0x5E5C90` | scenesystem | the vftable |
| `0x180059FC0` / `0x59FC0` | scenesystem | vftable slot 1 — the **batch** wrapper (not the draw) |
| `0x18005A465` / `0x5A465` | scenesystem | the wrapper's call into the real draw |
| `0x1800580A0` / `0x580A0` | scenesystem | the real `CAnimatableSceneObjectDesc::DrawPrimitive` |
| `0x180678360` / `0x678360` | scenesystem | the **lambda**'s type descriptor — the wrong target |


## 9. The draw detour's guard, and the install's refusal latch

This section records the two fixes the adversarial review of the draw hook
(commit `1bcb407`) forced, and the evidence behind them. Nothing here
changes the target: §8 stands.

### 9.1 The re-entrancy guard is per owner, not process-wide

§8.5 said the draw and the original it calls are synchronous on one thread,
so a plain depth counter gives the same protection as a `thread_local` one.
**That was wrong**, and the way it was wrong is the interesting part: a
process-wide counter does not merely mis-count, it **refuses**.

The guard's early return does not forward the draw. It returns without
calling the original, so the primitive is **not drawn at all** — a hole in
the model for that frame, and a silent one (the counters do not even see it:
the early return happens before `seen_`).

The second thread is not hypothetical. The live `scenesystem.dll`:

* imports `g_pThreadPool`, `CThreadedJob` and `CThreadSemaphore` from
  `tier0.dll`;
* carries the job bodies `CSceneSystem::Thread_RenderSceneDrawList`,
  `Thread_RenderViewLayer`, `Thread_RenderGpuCullingLayer` and
  `Thread_ProcessProceduralLayer`, the `RenderLayerDrawListPart` unit, the
  `CParallelLambdaJob` and `CUtlMultiJobProcessor` types, and the profiler
  string `"%.1fms culling, %.1fms rendering in %i jobs"`;
* carries the `sc_disableThreading` convar — the kill switch, so the
  threading is **on** by default.

`CAnimatableSceneObjectDesc::DrawPrimitive` is reached only through the
desc's vtable slot 1 (the batch wrapper), which is exactly the draw-list
path those jobs walk. So two jobs can be inside the detour at the same
instant, and the second one's draw was dropped.

The repository's own rule for this class of guard
(`docs/orchestration/spec-t27-thirdperson-crash.md` §4.1) says it outright:

> The guard must **not** refuse another thread's call … a shared counter that
> makes one thread's call return early would silently drop that thread's
> camera update. A **per-owner** design (the thread id that owns the depth,
> plus an atomic depth) satisfies both.

So the guard is now the **shared, per-owner** one
(`payload/features/include/cs2/features/recursion_guard.hpp`, taken from the
third-person camera's task — the tree holds exactly one implementation of
it, and the host test `payload/features/tests/test_chams_draw_hook.cpp` (the
Meson test `features-chams-draw-hook`) fails if a second one appears):

* the owner is the **OS thread id** (`GetCurrentThreadId()`), so a call from
  another thread is never mistaken for a recursion — it is drawn;
* the state is one 64-bit word per slot (owner in the high half, depth in
  the low half), so a claim and a release are one compare-exchange: no lock,
  no allocation, no `thread_local`;
* the guard asks **"am I already inside?" over every slot first**, and only
  then claims a free one. A single-pass guard that claims the first free
  slot lets the owner back in through that slot when a lower one has been
  freed meanwhile, and the recursion then runs until the stack overflows
  (0xc00000fd) and takes the renderer with it. The host test drives that
  case (`test_the_owner_is_refused_through_a_freed_slot`);
* the guard is held **across** the trampoline call — that is the recursion
  it exists to break, so a guard released before the call would protect
  nothing — and it is released on **every** path. The holder
  (`cs2/features/guard_scope.hpp`) enters the guard in its constructor and
  leaves it in its destructor, so an early return cannot forget it.

A `thread_local` is still not an option: the payload is manually mapped and
its TLS block is not installed on the game's threads, so a thread-local read
on that path faults inside the payload (`read addr=0x4` on the camera's
detour).

### 9.2 The refusal latch follows the operator's switch

The install path keeps a **refusal latch**: a refused install is reported
once (`game::append_log` opens, writes and closes a file per call, and the
path runs on the render thread), so the attempt is not repeated every frame.

The latch was never cleared. The operator switched the chams off and on
again and the latch survived the switch-off, so:

* the next switch-on never reached the report at all — the refusal was
  never reported again, and the log was silent about it;
* the HUD read `hook=installing` **forever**, which is a lie: no install
  was being attempted, and the reason the hook is not installed was gone.

The rule is now pure and in the decision header
(`cs2::features::hook_target`):

* `may_attempt_install(enabled, refused)` — the feature has to be on and the
  last attempt must not have been refused;
* `refusal_after_switch(enabled, refused)` — the latch follows the switch:
  switching the feature **off** clears it, so the next switch-on retries the
  install and reports the refusal again (which is what the log's own latch
  reset was always for);
* `must_report(reported, status)` — the log writes a line only when the
  state changes.

The counters are reset with it: a re-enable shows the new hook's own
numbers, not the previous session's.

### 9.3 The status line does not allocate

The status line is built on the game's render thread, once per frame, so it
is written **straight into the caller's buffer**
(`Chams::status_line(char*, std::size_t)`), the way the sibling providers
(`Service::rage_status`, `movement_status`) do it. The description is a
`string_view`, so it is printed with a precision (`%.*s`) instead of being
copied into a temporary `std::string`.

The state reads `hook=installed` when the hook is in place — not `hook=on`,
which was easy to confuse with the *feature* being on (`off` is the
feature's own idle state).

### 9.4 The module identity is checked strictly

The offset's module used to be checked only when it was **not empty**, so an
offset that named no module skipped the check altogether. The generated SDK
always fills the module (the dumper writes `off.module`), so this was not
exploitable — but the strict form is the same length and has no hole.

### 9.5 What is still open

1. **The live run.** No live run was possible: the agent session is session
   0, with no GPU and no interactive desktop. The detour has still never run
   against the real draw.
2. **The calibration dump** in the tree has no `scene_draw_primitive`
   entry, so the chams stay dormant — with the reason *no offset* — until
   the operator refreshes it with `scripts/calibrate-live.py`.
3. **The thread-pool overlap** is established from the module's imports and
   its job bodies, not from a live trace: the fix is the per-owner guard
   either way, but the number of threads that actually overlap in one frame
   has not been measured.

### 9.6 The evidence index for this section

| Where | What it proves |
|---|---|
| `payload/features/include/cs2/features/recursion_guard.hpp` | the shared, per-owner guard (taken from the third-person camera's task) |
| `payload/features/include/cs2/features/guard_scope.hpp` | the holder: entered in the constructor, released in the destructor |
| `payload/features/tests/test_chams_draw_hook.cpp` (the Meson test `features-chams-draw-hook`) | another thread's call is not refused; the scope releases on every path; the tree holds one implementation |
| `payload/features/tests/test_chams_target.cpp` | the refusal latch follows the switch, and the refusal is reported again after a re-enable |
| `payload/features/include/cs2/features/hook_target.hpp` | the pure rules: `may_attempt_install`, `refusal_after_switch`, `must_report` |

---

## 10. The desc cache is safe for two writers (T33)

§9.1 made the guard **per owner**, so two jobs of the tier0 thread pool can
be inside the draw detour at the same time. That is the guard's whole
purpose — it refuses a *recursion*, never another job's draw — but it means
the draw path has **two writers**, and everything it shares has to be safe
for two.

### 10.1 The protocol that was replaced, and why it was unsafe

The desc cache published its entry with **three separate atomics**, the desc
last:

```cpp
entry.entity.store(entity, std::memory_order_relaxed);
entry.view_model.store(view_model ? 1 : 0, std::memory_order_relaxed);
entry.desc.store(desc_address, std::memory_order_release);
```

and read it with an acquire on the desc and relaxed loads on the rest:

```cpp
if (entry.desc.load(std::memory_order_acquire) == desc_address) {
  entity = entry.entity.load(std::memory_order_relaxed);
  view_model = entry.view_model.load(std::memory_order_relaxed) != 0;
}
```

That is a protocol that is safe for **one** writer and not for two. Every
field has its own modification order, so:

* writer A stores `entity=A`;
* writer B stores `entity=B`;
* writer A stores `desc=A` (release).

A reader loads `desc` (acquire) and sees `A`; the acquire synchronises with
A's release, so A's `entity` store is visible. **But** the reader's `entity`
load is relaxed, and `entity`'s modification order now has B **after** A — so
the reader may legally observe `entity=B`.

**The result is desc A with entity B**: the chams are decided on the **wrong
player** (the CT's model chamsed, the T not). The slot is direct mapped
(`(desc >> 4) & 63`, 64 slots, 16-byte granularity, no probe and no tag), so
two descs in one slot are the routine case, not an exotic one.

The invalidation was a second race on its own: the cache's stamp was a plain
`std::uint64_t` written by both render threads, and the clear (64 relaxed
stores of `desc = 0`) could be **silently undone** by a publisher that was
mid-publish.

### 10.2 The protocol now: a per-slot seqlock, with the generation inside it

Every slot carries a **sequence word**: odd while a writer is inside the
slot, even when the entry is stable.

* the **writer** claims the slot by taking the sequence from even to odd (one
  compare-exchange), **fences**, writes the entry as one unit, fences again,
  and publishes the even sequence again;
* the **reader** takes the sequence (acquire), reads the entry, and takes the
  sequence again behind an **acquire fence**: if it changed, or if it was odd,
  the entry it read is not the entry it is looking at, and it retries (a
  bounded number of times — a writer never blocks a reader, and the reader
  never allocates).

The entry's own fields are **relaxed**: the sequence is what makes them
consistent, and the writer's release fence is what publishes them.

### 10.2.1 The two fences, and what each order actually buys

The protocol is the canonical seqlock, and the canonical seqlock has **two**
barriers — Linux's `write_seqcount_begin()` puts an `smp_wmb()` between "the
sequence goes odd" and "the data is written", and `write_seqcount_end()`
puts another one between "the data is written" and "the sequence goes even".
Both are here, as `std::atomic_thread_fence(std::memory_order_release)`.

The **first** fence is the one that makes the protocol correct on a weakly
ordered machine, and it is the one this change added. A release orders the
accesses that come **before** it, so a release fence between the claim and
the entry's writes is exactly the store-store barrier that keeps the **claim**
from being delayed past the entry's writes. The property the protocol needs
is therefore:

> a reader that observes the **new** entry also observes the **odd** marker —
> so it retries, instead of reading the old even sequence, the new entry and
> the old even sequence again, and accepting **desc A with entity B**.

The claim's compare-exchange is **acquire**, and that is the whole of it: the
acquire keeps the entry's writes **below** the claim (the compiler cannot
hoist them above it). It was `acq_rel` here, and the release half was
**credited with an ordering it does not have** — a release orders the
accesses that come *before* it, not the ones that come after, so it never
made the claim visible before the entry's writes. It bought nothing either:
the only reader of the odd sequence is a reader that **skips** it and
retries, so there is no value for a release to publish. The release half is
gone; the fence does the work the comment used to claim.

**On x86-64 this changes nothing observable**, and that is the point: the
claim is a `lock cmpxchg`, which is already a full barrier, and x86's store
buffer retires in order — so the protocol was safe on the supported target
**by the accident of the instruction**. The fence is what makes it safe *by
construction*. The fix is therefore a **correctness argument, not a
behavioural change**: removing the fence again leaves the suite green on this
platform, and no test here can show the difference (the review measured it:
five runs, zero mismatches, with and without the fence).

The **generation is part of the protocol**. Every entry records the frame
stamp it was filled for, and the reader only accepts an entry that belongs to
the current generation. That is what makes the invalidation safe: the
invalidation **is** the generation, so there is no clear to undo, and a
publisher that was mid-publish when the frame advanced publishes an entry of
the *older* generation, which the reader rejects. The cache's own stamp
member (`desc_cache_stamp_`) is gone — it was the plain, racy mirror of the
frame stamp, and the generation now travels **with** the entry.

The slot index is unchanged: the collision rate is a property of the scene
system's desc addresses and not of the protocol.

### 10.3 The probe, and its numbers

The probe is the reviewer's: **four threads, two descs colliding in one
slot**, driving the **shipped** cache (`cs2::features::DescCache`) — not a
copy of it. It reports the mismatches it sees (a reader that pairs desc A
with entity B).

| The protocol | Mismatches | The test |
|---|---|---|
| the pre-fix one (three atomics, the desc last) | **132** | **fails** (`the cache never pairs two writes`) |
| the seqlock | **0** | passes |

The old protocol is also reproduced **inside the test** as the control, and
the number it reports varies with the run (61, 63, 127, 202, 24 394 …): the
race is a race, and how often it bites depends on the machine. The number is
printed, not asserted — a race that does not manifest in one run must not
fail the suite. The proof that the probe bites is the run against the
pre-fix protocol in a scratch copy of the tree, whose number is in the
task's report.

Two more rounds were added:

* **the generation bump under a concurrent publisher** — a fifth thread
  advances the generation while the four writers hammer the slot. The
  invalidation is part of the protocol, and this is the only way it ever
  happens in the payload: a publisher that was mid-publish when the frame
  advanced publishes an entry of the *older* generation, and the reader
  rejects it. The round reports **zero** mismatches with ten to forty
  thousand advances per run;
* **the reader's give-up path** — eight writers hammer one slot while a
  reader looks it up. The reader gives up after four tries and resolves the
  desc itself (the uncached path), and that is the one path where the
  cache's promise ("once per model") does not hold: the draw path pays two
  engine reads per *primitive* instead of per *model* while it lasts.

The give-up path was correct and **unmeasured**; the cache counts the
give-ups now (`DescCache::give_ups()`), and the round reports them: on this
machine the reader gives up on **28.6 % to 93.9 %** of the lookups under that
contention (the review's runs measured 28.5 % to 39.3 %; the rate depends on
how the eight writers and the one reader are scheduled, so it is a
measurement and not a property), and the entry the cache hands out is still
the right one every time (zero mismatches).

The count is **reported**, not just measured: the cache counts it
(`DescCache::give_ups()`), the chams read it (`Chams::cache_give_ups()`) and
the status line shows it as `gu=` (§10.4.1). The counter exists to tell the
operator that the cache is giving up — while it climbs, the draw path pays
two engine reads per *primitive* instead of per *model*, which is the exact
cost the cache exists to remove — and a counter nobody can see is a counter
nobody reads.

### 10.4 The guard fails **closed** on the draw path

The shared guard's fail-open rationale is the **camera's**: refusing a call
that is not a recursion would drop a camera update. For the draw path the
arithmetic is the other way round.

Under fail-open the calling thread is **not recorded in any slot**. So when
the trampoline comes back on that thread, the guard's first pass does not
find it — the recursion is **not broken**, and it runs until the stack
overflows (`0xc00000fd`) and takes the renderer with it. A **refusal** drops
**one primitive**: a bounded, visible cost.

The policy is therefore **per instance** (`RecursionGuard::Policy`), and the
default is the camera's (`fail_open`), so the third-person camera's
behaviour does not change: `camera_guard_{}` is built with the default and
the chams' `draw_guard_` with `Policy::fail_closed`.

### 10.4.1 The pool's size, and why it is per instance

The policy was the easy half. The **pool** was the camera's: nine slots
(eight and a reserve), which is right for a camera update that runs on one or
two threads and wrong for the draw path.

The chams' guard holds its slot for the **whole draw** — the call into the
original included, because that is the call the recursion happens inside — so
the pool has to hold one slot per **concurrent render job**, not one per
thread. With the camera's nine slots, a **tenth** concurrent render job was
refused, and a refusal on the draw path **drops the primitive**: the model
was hole-ridden, every frame, where the old fail-open path dropped nothing.
Visible (`rec=`/`pool=`/`tdn=`) is not the same as harmless.

So the pool's size is **per instance** now (`RecursionGuard`'s constructor),
and the draw path asks for `DRAW_PATH_SLOT_COUNT` — **sixty-four** slots.

**The job count is unmeasured.** The scene system splits the draw list into
jobs on the tier0 thread pool and prints `"rendering in %i jobs"`, and
nothing in the tree has ever measured it: the detour has still never run
against the real draw (the live run needs a GPU and an interactive desktop,
§10.7.1). Sixty-four is therefore chosen with **headroom**: the tier0 pool
has about as many threads as the machine has hardware threads, so sixty-four
covers a machine with up to sixty-four hardware threads. It is **not** "well
above any plausible count": on a machine with more than sixty-four hardware
threads the pool can be exhausted again, and the draws are then dropped and
**counted** (`pool=`), which is the mitigation — the operator sees it.
Sixty-four is **7.1 times** the camera's nine-slot pool (not eight: eight is
the camera's `SLOT_COUNT` without its reserve).

The camera's **behaviour** is exactly what it was — its pool is untouched and
its per-call cost did not change (measured below) — but its guard **object**
grew: `sizeof(RecursionGuard)` is now **528 bytes** (it was 80), because the
slots array is sized for the largest pool any guard may ask for. The
camera's `ThirdPerson` grew by 448 bytes.

**The cost of the headroom** is the scan: the guard's first pass ("am I
already inside?") walks **every** slot, so the scan is O(pool) per draw. A
slot is one 64-bit word, so sixty-four slots are 512 bytes (eight cache
lines, resident in L1) and the scan is sixty-four loads. **Measured** on this
machine (clang-cl, `/O2`, two million enter+leave pairs, the shipped header):

| guard | per enter+leave | per frame at 2000 primitives |
|---|---|---|
| the camera's pool (9 slots) | **10.4–11.9 ns** | — (the camera is not per-primitive) |
| **the draw path's pool (64 slots)** | **48.3–54.6 ns** | **≈ 100 µs** |

The review measured the same thing on the same machine: **9.4–9.8 ns** and
**43.7–46.7 ns**, i.e. **≈ 90 µs** per frame. The comment this section
quotes said "about ten nanoseconds … roughly twenty microseconds per frame":
the ten nanoseconds was the **camera's** nine-slot pool, not this one, and
the draw path's scan is about **4.5 times** that.

The **conclusion does not change**: ≈ 100 µs against a sixteen millisecond
frame is 0.6 %, and the desc cache saves about **2 300 µs** per frame on the
same path (the four engine reads per primitive that the cache removes —
eight thousand reads at 284 ns each, at two thousand primitives), so the scan
is noise next to it. What changes is the number: the next person sizing a
pool from it would have been wrong in the same direction.

**The fail-open path is not back on the table.** The prologue is verified
before the hook is installed (`hook_target`), so the recursion the fail-open
path risked is already excluded for a **verified** target — but "the
recursion is unlikely" is not "the recursion is broken", and the fail-open
window is the one that ends in a stack overflow that takes the renderer
with it. The refusal is the bounded cost; the pool is sized so the refusal
does not happen.

A refusal is now **counted** and reported: the refusal path returned before
`on_draw`, so it moved no counter at all and a dropped primitive was
invisible.

The three **causes** are counted **separately**, because they need different
responses and the counters are not reset — a climbing number is the
operator's only signal. The desc cache's **give-ups** are counted with them,
because they are the same kind of thing: a number that climbs while the
feature quietly does less than it says.

```
hook=installed seen=1234 chams=56 skip=1178 rec=0 pool=0 tdn=0 gu=0
```

* `seen` — the draws that reached the feature's **decision**;
* `chams` — the draws that were re-drawn with the chams' material;
* `skip` — the draws that were forwarded untouched;
* `rec` — the draws the guard refused because this thread was **already
  inside** (the recursion the guard exists to break). Rare, and it means the
  hook's target is wrong;
* `pool` — the draws the guard refused because its **pool** was exhausted.
  This is the serious one: the primitive is **not drawn at all**, every
  frame, so a climbing `pool` means the chams are eating the scene (§10.4.1);
* `tdn` — the draws that arrived while the hook was being torn down. One per
  teardown;
* `gu` — the desc cache's **give-ups**: the lookups that gave up on a slot
  that never settled, so the draw path paid two engine reads per *primitive*
  instead of per *model* while it lasted (§10.3). A climbing `gu` is the
  cache losing the promise it exists to keep.

The counters count the drops that could be **attributed**: a draw that
arrives after the feature is gone (`g_active == nullptr`) is dropped too, but
there is no object left to count it on, so it is not in the numbers. The
window is tiny — the hook is un-patched before `g_active` is cleared.

A dropped draw never reaches the decision, so it is not in `seen` at all.

`seen == chams + skip` holds at every point in time **except** the two
instructions between `seen_++` and the paired `drawn_++`/`skipped_++`: the
draw's counter is moved **before** the call into the original, exactly like
the skip paths' counters, so the window is two instructions wide and not the
whole draw. (A reader that reads the three counters one after another can
still see a torn combination — the counters are not read as one snapshot —
but the relation holds at every point in time otherwise.)

The line has to fit the overlay's **192-byte** HUD buffer, and the counters
are what the operator reads, so the **state** is the field that gives room:
when the line does not fit, the state is cut (at its parenthesis, and at 24
characters at the most) and the line is written again. The line also has
**two shapes**: the full one, and a fallback that drops the **derived**
`skip` field (`skip` is exactly `seen - chams`, so the line still carries
the information) when even the cut state does not fit.

The bound, in **bytes** — the figure that matters is the length *with* the
NUL, against the 192-byte buffer:

| what the caller can receive | characters | with the NUL |
|---|---|---|
| the fallback (the state cut to 24 characters, `skip` dropped) | 181 | 182 |
| the longest line the hook's own states can produce (the `off` state with every counter at its maximum — the full shape still fits there) | 187 | **188** |

The **full** shape is 243 characters at its worst (the seven counters at
their 64-bit maximum and the longest state), and it is only written when it
fits, so the line never exceeds 191 characters (192 with the NUL): it fits
the buffer by construction. The overflow needs 10^19 draws, so in practice
the operator sees the full state.

The bound is a **test**, not a comment: the status line's formatting is a
pure function (`chams_status::format`, in the chams' header), so the host
test drives it with every counter at its 64-bit maximum, over every state
the hook can report, and checks that the line and its NUL fit the 192-byte
buffer (`payload/features/tests/test_chams_draw_hook.cpp`).

### 10.5 The counters are not reset

The reset was three separate relaxed stores on the payload's thread while the
render threads were `fetch_add`ing the same counters, so a `seen` that
landed between two of the stores left `seen == chams + skip` broken (an
off-by-one that never healed) for the width of the reset window. The reset
is **gone**: the operator reads the difference between two `[state]` lines
(`docs/known-issues.md` §11.1, item 3). A reset that is atomic with respect
to the increments would need the counters to carry their own epoch and be
re-based with a compare-exchange on the draw path — a lot of machinery for a
number nobody reads absolutely.

§9.2's last sentence ("the counters are reset with it") describes the state
before this section and no longer holds.

### 10.6 The trampoline's lifetime

`InlineHook::uninstall()` used to **free** the trampoline and **then** null
the pointer:

```cpp
std::memcpy(target_, saved_, patched_size_);
free_executable(trampoline_, trampoline_size_);   // freed here
trampoline_ = nullptr;                            // nulled here
```

A detour that had already read `trampoline_` held a **dangling** pointer and
called **freed executable memory**. The order is now the mirror of the
install's:

1. `installed_` → `false` (release): no new caller proceeds;
2. `trampoline_` → `nullptr` (release): the null is published **before** the
   trampoline's pages are released, so a detour that enters from here on
   finds `original() == nullptr` and drops the call;
3. the target is un-patched: no new detour enters at all;
4. the trampoline is **retired**, not freed.

Step 4 is what closes the window for a detour that was **already inside**
when the uninstall happened: the retired trampoline's pages stay mapped, so
the call it makes is a call into live memory.

**The destructor was the other half of the property, and it was false.** The
pages were released when the `InlineHook` itself was destroyed, on the
assumption that "the feature is being torn down" means "no detour can be
inside it any more". That is an assumption, not a property: the review
proved it against the shipped code — a detour inside the hook, the hook
destroyed, the detour calls the trampoline → **0xC0000005**, a call into
unmapped executable memory.

There is no point in the hook's life at which the engine can know that no
detour is inside the hook: the detour is the **caller's** function, and the
engine never sees it enter or leave. So the pages are **never unmapped** —
the trampoline is retired at the uninstall (the pointer is published as null
and dropped) and its pages stay mapped, and executable, until the process
exits. The cost is a bounded leak: **one page per uninstall** (for the
chams: one per switch-off of the feature; for the camera, the present hook
and the animfix: one per state change they hook), and each page is reclaimed
by the operating system when the process exits.

The one sentence: **no thread can execute freed trampoline memory, because
the trampoline's pages are never unmapped — the null is published before the
teardown, and a retired trampoline stays mapped and executable for the life
of the process.**

The property is proved by a host test
(`payload/hooks/tests/test_inline.cpp`): the detour blocks inside the hook,
the hook is **destroyed**, and the detour then calls the trampoline it
captured. Against the code before this change the process died with
0xC0000005; it passes now.

### 10.7 What is still open

1. **The live run.** No live run was possible: the agent session is session
   0, with no GPU and no interactive desktop. The detour has still never run
   against the real draw, so the **number** of jobs that overlap in one frame
   is still not measured — which is exactly why the guard's pool is sized
   with headroom (§10.4.1).
2. **The animfix's comment** in `payload/control/src/input.cpp` said that the
   hook's publication "is a plain pointer store, which is atomic on x86". It
   was not any more: the trampoline is published with an **atomic release
   store** and read with an **acquire load**. The comment is **updated** (the
   file is outside this task's ownership, so the change is a single comment
   and was authorised for exactly that).
3. **The camera's own `g_active`** (`payload/features/thirdperson.cpp`) is a
   plain pointer with the same shape as the chams' was. It is outside this
   task's ownership (the camera's behaviour must not change), so it is
   reported, not fixed.
4. **The other `g_active`s** — the present hook's (`game_present_hook.cpp`)
   and the input's (`control/src/input.cpp`) — are the same shape. The
   present hook runs on the game's own present thread and the input's on the
   game's tick, so the exposure is smaller, but it is the same exposure.
5. **The release fence is not observable here.** The claim is a `lock
   cmpxchg` on x86-64, which is already a full barrier, so the protocol was
   safe on this target **by the accident of the instruction**. The fence
   (§10.2.1) is what makes it safe by construction; no test on this platform
   can show the difference, and none claims to.

### 10.8 The evidence index for this section

| Where | What it proves |
|---|---|
| `payload/features/include/cs2/features/chams.hpp` | the desc cache: the per-slot seqlock, the release fence between the claim and the entry's writes, the generation inside the entry, the fail-closed guard with the draw path's pool, the refusal counters by cause, the give-up counter, and the status line's pure formatting (`chams_status`) |
| `payload/features/src/chams.cpp` | the publish and read protocol, the atomic `g_active`, the refusals counted by cause, the status line |
| `payload/features/include/cs2/features/recursion_guard.hpp` | the per-instance policy (`fail_open` is the camera's default, `fail_closed` the draw path's) and the per-instance pool (`DRAW_PATH_SLOT_COUNT`) |
| `payload/hooks/include/cs2/hooks/inline.hpp` | the atomic `trampoline_` / `installed_`, and the rule that the trampoline's pages are never unmapped |
| `payload/hooks/src/inline.cpp` | the publication order on install and on uninstall, and the pages that are never released |
| `payload/features/tests/test_chams_desc_cache.cpp` | the concurrency probe: four threads, two descs in one slot, **zero** mismatches; the generation bump under a concurrent publisher; the reader's give-up path |
| `payload/features/tests/test_chams_draw_hook.cpp` | the latch clear's **value** (H3), the guard's scope **order** (H6), the fail-closed policy and the per-instance pool (H7), the refusals counted by cause, the cache's give-ups **reported**, and the status line's 192-byte bound |
| `payload/hooks/tests/test_inline.cpp` | the trampoline's lifetime: an uninstall **and a destruction** while the detour is inside do not free the memory it calls |

