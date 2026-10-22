# T12 — apply the chams colour (the material parameter setter)

**Goal:** make the 3D chams actually take the operator's colour. The chams hook
(`payload/features/src/chams.cpp`) substitutes the game's flat material, but the
material's **own** colour is used because the material system's
"set a parameter by name" entry point was never located.

**This is a research + implementation task.** The setter's location is the unknown;
everything else is plumbing that already exists.

**File ownership (you may ONLY touch these):**

- `payload/game/include/cs2/game/material.hpp`
- `payload/game/src/material.cpp`
- `payload/game/tests/test_material.cpp`
- `payload/features/src/chams.cpp`
- `payload/features/include/cs2/features/chams.hpp`
- `docs/re/chams.md` *(new)* — the findings

**Do NOT touch** `payload/src/payload_main.cpp` (the colour is already plumbed all
the way to `Chams::configure`), the wire struct, or anything else.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`payload/game/src/material.cpp` (the hash and the decision) and
`C:\Users\gpmpo\AppData\Local\Temp\opencode\re-chams.md` (the RE).

---

## 1. What is known

**Verified** (in the RE and in the merged code):

- The material system: `client.dll + 0x279CE48` → `CMaterialSystem2*`; its
  vtable is at `materialsystem2.dll + 0x12d920`; **`FindMaterial` is slot 14**
  (`(this, IMaterial2** out, const char* path)`); the returned handle points at
  the material slot and the material is `*handle`; its refcount is at `+0x20`.
- `CMaterial2`: object size `0x7E0`, vtable `materialsystem2.dll + 0x129ad0`,
  name at `+0x70`, flag byte at `+0x0B`, and a **hash map of named parameters at
  `+0x180`**.
- **The name → value hash**: MurmurHash2, **case-folded**, seed `0x31415926`,
  `h = len ^ seed`. It reproduces three engine constants exactly:
  `0x5D700411` = `"RepresentativeTexture"`, `0xF655FAAB` =
  `"PlayerVisibilityDataColor"`, `0x1C6E1DA6` = `"DataColor"`.
  **The implementation is in `payload/game/src/material.cpp` and is host-tested —
  use it, do not re-derive it.**
- `CMaterial2` slot 21 (`0x18000b660`) and slot 22 (`0x18000b700`) resolve
  **hashed named parameters** through that map. They are **getters**, and
  `0xCF382AB` / `0x28C67ADC` are the material's two **texture** parameters (not
  colour) — the RE corrected this.

**Unknown:** the **setter** — the entry point that writes a named parameter's
value — and the **name** of the colour parameter.

## 2. Find the setter — try these in order

**(a) The `.vmat` data (the most direct).** The material's own file names its
parameters. The game's VPK archives are under
`F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\`
(`pak01_dir.vpk` …). The target material is
`materials/physics/flat_basic.vmat`. **Write a small VPK reader** (the format is
documented and simple: a directory tree of file entries) and extract that
`.vmat`; it is a KeyValues text file listing the shader and its parameters —
**including the colour parameter's name**. Hash that name with the known hash
function and you have the hash the setter wants.

The RE agent already wrote a "VPK reader" that did not parse this file — check
what it did and fix it, or write a fresh one. A `flat_basic.vmat` is small; even
a partial parse that finds the parameter names is enough.

**(b) The `CMaterial2` vtable.** Read the whole vtable
(`materialsystem2.dll + 0x129ad0`, 30+ slots) and look for a slot that takes a
**hash** and a value (a `Vector4`/`float*`/pointer). The getters take a hash; the
setter is likely nearby or in a derived class. Decompile the candidates with the
**REA MCP server** (the material system's Ghidra project is warm; `open_binary`
on `materialsystem2.dll` is a cache hit) and identify the setter by its body — it
must **write** into the map at `+0x180` (or into the value the map holds).
Note `CMaterial2` may have a **derived** class for the concrete material type
(the `.vmat` is loaded into a subclass) — check the vtable's RTTI for the real
type of the object `FindMaterial` returns.

**(c) The engine's own colour path.** The game must be able to tint a material
somewhere (a preview, a team colour, a spectator mode). Search
`materialsystem2.dll` and `client.dll` for a call that sets a *known* colour
parameter and follow it back to the API. A strong lead: the engine's
`PlayerVisibilityDataColor` scene object writes a vec4 at the desc's
`+0x5D4`/`+0x5DC` — but that is a **scene** value, not a material parameter.
Another lead: the debug/collision-geometry materials are known to be tinted by
the engine (`show_collision_geometry_wire.vmat` is used with a colour), so find
**who tints them**.

**(d) The value map's shape.** If the setter cannot be found, consider whether the
value can be written **directly**: the map at `CMaterial2+0x180` is a hash map of
name → value. If the value's layout is known (a `Vector4`? a pointer to one?),
writing it directly is a legitimate last resort — **but** it is a *write into
engine memory*, so it must be done under the same fault-safety rules as
everything else, and it must be restored on unload. Say clearly in your report
which route you took and why.

## 3. Implement it

- Add the setter to `MaterialSystem` (in `payload/game/src/material.cpp`), with
  the **same fail-closed discipline** the chams already use:
  - the slot/function must be validated (inside the expected module),
  - every engine read goes through the fault-safe `game::read` helper,
  - a failure returns "not applied" rather than guessing.
- Apply the colour when the chams are **configured** (a colour change) and when
  the material is **resolved** (a mode change) — the flat material's colour must
  be set **after** the material is found, and the old colour restored when the
  chams are disabled or the material is swapped.
- The chams must still work with **no colour** (the material's own colour) when
  the setter is unavailable — the feature must not become a no-op because the
  colour could not be set.
- **Do not change the wire format.** `visuals_chams_color` already exists and is
  already plumbed.

## 4. The host test

- The hash function is already tested; add the **new** constants you discover
  (the colour parameter's name → its hash) as a test vector, so a future change
  to the hash is caught.
- The decision logic (when to apply, when to restore) must be testable **without
  the engine** — extract it as a pure function over a small interface, and test
  it.
- **Prove the tests can fail.**

## 5. `docs/re/chams.md`

A short note: the setter's location (or the reason it could not be found), the
colour parameter's name and its hash, the `.vmat`'s parameters, the route you
took, and what is still unknown.

## 6. Verification

- `meson compile -C build && meson test -C build` → green (24/24 at baseline).
- `meson compile -C build-payload` → clean.
- You cannot launch CS2 (it needs `schtasks /run /tn CS2Launch` and a GPU in the
  interactive session) — say clearly what is unverified.

## 7. Report

- The setter's location and signature, or the reason it could not be found.
- The colour parameter's **name** and its hash, and how you got the name.
- The route you took and what it costs at runtime.
- The restore path (how the old colour is put back).
- What is still unknown, and what the chams do without the colour applied.
