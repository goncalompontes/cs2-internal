# T6 — true 3D chams

**Goal:** make the player models render with a flat, configurable material — the
real thing, not an overlay approximation.

**This task depends on a reverse-engineering result.** The RE report is at
`C:\Users\gpmpo\AppData\Local\Temp\opencode\re-chams.md` — **read it first**
(it has been updated with the second round). Treat its `verified` findings as
the ground truth; where it says `unknown`, **do not guess** — isolate it behind
a clearly-marked constant and say so.

**File ownership (you may ONLY touch these):**

- `payload/features/src/chams.cpp` *(new)*
- `payload/features/include/cs2/features/chams.hpp` *(new)*
- `payload/features/meson.build` (to add the source)
- `payload/game/include/cs2/game/material.hpp` *(new)* — the material-system access
- `payload/game/src/material.cpp` *(new)*
- `payload/game/meson.build` (to add the source)
- `payload/src/payload_main.cpp` (only the settings→chams plumbing)
- `payload/game/tests/test_material.cpp` *(new)* — if a host test is possible

**Do NOT touch** `payload/control/**`, `payload/features/src/esp_*.cpp`,
`payload/features/src/overlay.cpp` or the wire struct.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

---

## 0.1 What the RE established (do not re-derive)

The live `client.dll` is build **25947352**, sha `659940c1…`, base `0x180000000`.
**The repo's `docs/re/client-dll.md` addresses are from a different build and
are stale** — the report re-derived everything on the live build.

| Fact | Where |
|---|---|
| **The scene system is a separate module** | `scenesystem.dll` (sha `040eb851…`), exporting `SceneSystem_002`; `CSceneSystem`'s vtable is `0x1805f1710` |
| **client.dll keeps an interface-name → global table** | `client + 0x279CE48` = `CMaterialSystem2*`; `client + 0x279CF10` = `ISceneSystem*` — so the payload can reach the material system **without** `GetProcAddress` |
| The `ICvar` interface global | `client + 0x279cd58`; `FindVar` = vtable `+0x58`, `RegisterConCommand` = `+0x150` |
| **`CMaterialSystem2` vtable slot 14 = `FindMaterial(name)`** | proved from client.dll (`CPhysicsGameSystem::GameActivate` calls it with `materials/tools/show_collision_geometry_wire_nodepth.vmat`) |
| The `CMaterialSystem2` singleton | `materialsystem2.dll + 0x163530`, vtable `0x12d920`; `CreateMaterial` = slot 29, `CMaterial2` = 0x7E0 bytes, vtable `0x129ad0`, name at `+0x70`, flags at `+0x0B` |
| **The flat material** | **`materials/physics/flat_basic.vmat`** (and `…_wire_nodepth.vmat` for the see-through variant). `debug/DebugFlat` is **refuted** — absent from all four modules |
| **The draw signature** | `CAnimatableSceneObjectDesc::DrawPrimitive(IRenderContext*, CMeshDrawPrimitive_t*, const IMaterial2*, const IMaterialMode*, DrawValues_t&, bool, …)` — the material reaches the GPU as an `IMaterial2*`, the blend/depth state as a **separate** `IMaterialMode*` |
| **The name→value hash** | **MurmurHash2, case-folded, seed `0x31415926`, `h = len ^ seed`** — reproduced exactly: `0x5D700411` = `"RepresentativeTexture"` |
| **CS2 ships its own wallhack** | `CPlayerVisibilityDataColorSceneObjectDesc`; the convars `r_player_visibility_mode` / `_strength` / `_stencil`; the scene named value `"PlayerVisibilityDataColor"`; a colour vec4 written to the scene desc at `+0x5D4` / `+0x5DC`; the materials `materials/dev/player_visibility*.vmat` |
| `C_BaseEntity`'s vtable | `0x181c47bd0` — only **2 of its 96 slots** touch the scene system ⇒ the entity-side model submission is **not** a `C_BaseEntity` virtual. The client schema provably contains **no** model-material field |

**Still open:** the client.dll function that submits the player *model* to the
scene system (where the entity pointer is available); the exact vtable slot of
`DrawPrimitive`; how to identify an "enemy player" from inside the draw; the
parameter names behind `0xCF382AB` / `0x28C67ADC` (they are in the game data,
not in any module); and the "set material parameter" API.

## 0.2 The routes — try them in this order

### Route A — the game's **own** wallhack (`r_player_visibility_*`)

**Try this first.** If the `r_player_visibility_*` convars make enemy models
render through walls, then the "3D chams" is **already in the game** and the
payload only has to set the convars — no draw hook, no material override, no
crash risk from a render-path hook.

- Read the convars' **help strings** and their default values from the build to
  learn what `r_player_visibility_mode` actually does (the report found the
  convars; read what they say).
- Set them through the **`ICvar`** the client already holds
  (`client + 0x279cd58` → `FindVar` at vtable `+0x58` → the `ConVar`'s value at
  `ConVar* + 0x58`) — **or** through the payload's existing `Console::exec`
  (`payload/control/src/console.cpp`), which is the engine's own path and needs
  no offsets at all. **Prefer `exec`**: fewer offsets, fewer ways to be wrong.
- The colour: the game writes a vec4 into the scene desc at `+0x5D4` /
  `+0x5DC`. If the payload can reach that desc, it can drive the colour;
  if not, the game's own colour is the fallback.
- **Deliverable:** the convars, their meaning, whether they work, and — if they
  do — the feature is a handful of lines.

### Route B — the real material override

If route A does not do it, override the material in the draw path:

- **The hook.** The draw entry is `CAnimatableSceneObjectDesc::DrawPrimitive`
  in `scenesystem.dll`. Two sub-routes:
  - hook it in `scenesystem.dll` (the module must be loaded — the payload can
    find it with `GetModuleHandleW`);
  - or hook the **client-side caller** that submits the player model — that is
    the one with the **entity pointer**, which is what tells you "this is an
    enemy". **Prefer this one** if you can find it; the RE did not, so this
    needs a bounded search: the `C_BaseEntity` vtable's two scene-touching
    slots, and the `CRenderComponent` path (`m_pRenderComponent` at `+0x338`).
- **The material.** `FindMaterial("materials/physics/flat_basic.vmat")` through
  `CMaterialSystem2` vtable slot 14, reached via the client's interface table
  (`client + 0x279CE48` → the `CMaterialSystem2*`; its vtable is at
  `materialsystem2.dll + 0x12d920`).
- **The colour.** The material is shader-parameter driven: the name→value hash
  is MurmurHash2 case-folded, seed `0x31415926`. Find the **colour parameter's
  name** by hashing the candidates (`"Color"`, `"Colour"`, `"Tint"`,
  `"BaseColor"`, `"Diffuse"`, `"MaterialColor"`, …) and matching against the
  hashes the material's parameter slots use (`0xCF382AB`, `0x28C67ADC`).
  **Write a tiny host-testable hash function and a test that reproduces
  `0x5D700411` = `"RepresentativeTexture"`** — that proves your hash is right
  before you rely on it.
- **The blend / depth state.** It is a **separate `IMaterialMode*`** argument,
  and the mode machinery lives on `CMaterialMode`
  (`CMaterialSystem2::SetRenderStateForMode`). The see-through variant
  (`…_wire_nodepth.vmat`) is the "no depth test" one. If a separate mode object
  is needed and its factory is not found, use the `_wire_nodepth` material
  instead and say so.

### Route C — a scene layer

High effort, high risk, and the RE says it looks *worse* than A/B for this
build (`CSceneSystem`'s RTTI is absent from client.dll). Only attempt it if A
and B both fail, and say why you tried.

## 1. The feature

`payload/features/{include/cs2/features,src}/chams.{hpp,cpp}`:

- the settings are **already there** (T1 added them) — use them, do not add
  new ones: `visuals_chams` (enable), `visuals_chams_mode` (0 = flat, 1 = glow,
  2 = wireframe), `visuals_chams_visible` (1 = only when the player is
  visible), `visuals_chams_local` (1 = also chams the local player), and
  `visuals_chams_color[4]`.
- the **visibility** question is the same one T9 is solving for the overlay ESP
  (a cached, low-rate per-player answer). **Reuse** whatever T9's provider
  exposes if it is available in your base; otherwise treat "visible only" as
  **not yet available** and say so.
- the **viewmodel** (the weapon in front of the camera) must **never** be
  chamsed.

## 2. Robustness — this is the riskiest change in the wave

A mistake in the draw path is a crash or a corrupted render, not a wrong pixel.

- **Fail closed.** If anything cannot be resolved (the material system, the
  material, the entity pointer, the vtable slot), the detour must call the
  original and return. **Never** guess a pointer.
- **Validate the hook target**: the vtable slot must point inside the expected
  module — the codebase already has this pattern for the movement-services
  slots (`payload/control/src/input.cpp`), copy it.
- **Never leave the material overridden.** The override is **global** state in
  the material system; a draw that returns without restoring it would chams
  the whole world. Set → draw → restore, with the restore on every path.
- **Thread safety.** The engine may draw from more than one thread. The
  material's colour is global state. Either restrict the chams to the **render
  thread** (and say so) or make the set/restore balanced under concurrency.
- The payload has a **crash handler** and a **watchdog**
  (`payload/diag/`); use them for the probe.
- **Install lazily**: the material system and the scene only exist once the
  game is up. Install from a frame callback that already runs (the `Present`
  hook) and retry until it succeeds.

## 3. Quality bar

- `docs/style-guide.md` is normative: trailing return types, `snake_case`,
  `SCREAMING_SNAKE_CASE`, `enum class`, `[[nodiscard]]`, `noexcept`,
  fully-qualified `std::`, **no exceptions**, no new global mutable state
  (ADR-0010).
- `payload/game` is compiled into the **host tests** too: keep it free of
  `<windows.h>` and of engine dependencies, or guard the Windows-only parts.
- Every engine read must go through the **fault-safe** helpers
  (`cs2::game::read` / `read_bytes`) — a direct `*reinterpret_cast` on an engine
  address crashes the game the first time an offset is wrong.
- The payload is **manually mapped**: no CRT static-init, no `<print>`.

## 4. Verification

- `meson compile -C build && meson test -C build` → green.
- `meson compile -C build-payload` → clean.
- A **host test** for the pure parts: the **hash function** (it must reproduce
  `0x5D700411` = `"RepresentativeTexture"`), and the "is this entity one of
  ours" decision as a pure function of `(team, life_state, is_local,
  is_viewmodel)`.
- **Prove the tests can fail.**
- **You cannot launch CS2** (it needs `schtasks /run /tn CS2Launch` and a GPU in
  the interactive session) — so say clearly what is unverified. In particular
  the *visual* result has not been seen.

## 5. Report

- **Which route you implemented and why** — and, if route A worked, that it is
  the game's own feature and what the payload had to do.
- Every offset, interface slot and vtable index, with its provenance.
- The hook's install/uninstall lifecycle and the fail-closed behaviour.
- The thread-safety argument (or the restriction you imposed).
- Which modes are real and which are approximations.
- What is still `unknown` from the RE and how the code behaves then.
- What is unverified without a live game.
