# `client.dll` (build 25815307)

Image base `0x180000000`. Analysis is now **complete** via the cached Ghidra
project (one cold run, then `-process -noanalysis` reuse): 76,880 procedures and
58,107 strings with the default analyzer set (including `Decompiler Parameter
ID`). See `docs/rea-project-cache.md`.

## Globals found

| What | Address | Notes |
|---|---|---|
| `CreateInterface` head global | `0x1827946c8` | `*global` = list head; NULL in file image |
| `CGameEntitySystem` / `Source2EntitySystem` singleton | `0x182541b20` | vtable `0x181c25200` |

### Interface list
`CreateInterface` @ `0x1819c6770` reads head from `0x1827946c8` and walks
`InterfaceReg { create@0, name@8, next@0x10 }` (identical to the other modules).

### Entity system
Static initializer `FUN_1800be600`:

```c
FUN_1803516d0(&DAT_182541b20, "Source2EntitySystem");  // register singleton
_DAT_182541b38 = 0;
DAT_182541b20 = &PTR_LAB_181c25200;                    // vtable
```

So the entity system is a static C++ object at `0x182541b20` implementing the
`Source2EntitySystem` interface. `FUN_1803516d0` is a generic singleton/registry
linker (`node[0]=create/vtable, node[1]=prev head, node[2]=name`, head global
`DAT_1823b2538`).

## Useful anchors

Debug strings present in `.rdata` (good reference points for signatures):

- `Source2EntitySystem`, `Source2EntitySystemDeallocator`
- `CGameEntitySystem::CreateEntities`, `CGameEntitySystem::InitEntity2NetworkClasses`
- `CEntitySystem::RegisterEntityClass`, `CEntitySystem::AllocateEntityIdentity`
- `EntitySystem - Entity Networking - start/finish`, `EntitySystem - Class Tables`

## Live (runtime) findings

From `scripts/calibrate-live.sh` against the running client (Proton/Wine):

- **ASLR**: module image bases differ per run (e.g. `client.dll` at
  `0x6fffe57b0000`); the static VAs here are image-base-relative, so add the
  live base to the RVAs.
- **Interface counts (authoritative linked list):** `client.dll` 7,
  `engine2.dll` 34, `schemasystem.dll` 1 (`SchemaSystem_001`). The earlier
  16-byte heuristic scan latches onto an unrelated crypto-name table — do not
  use it.
- **Anonymous mappings:** Wine maps most of a PE image anonymously, so
  `/proc/<pid>/maps` only reports a module's header page (and sometimes one
  section). Module sizes and image extents must come from the PE
  `SizeOfImage`, not the maps range.
- **Spaced install path:** the depot lives under
  `Counter-Strike Global Offensive`, so maps paths contain spaces — parse the
  path as the remainder after the inode field.
- **`CSchemaSystem*`** resolves to the same object from both the `client.dll`
  and `schemasystem.dll` globals (e.g. `0x6ffff7696710`).

## Resolved accessors and globals

All addresses are image-base-relative VAs in the complete build-25815307
analysis. Nothing here is a guessed pattern: each entry cites the function that
produces it.

### Entity system and entity list

| What | Address | Notes |
|---|---|---|
| `CGameEntitySystem*` pointer global | `0x182711598` (RVA `0x2711598`) | Holds the entity-system pointer used by `CreateEntities`/`InitEntity2NetworkClasses`. |
| Entity identity block array | `0x182366770` (RVA `0x2366770`) | The entity list; consistent with object base `0x182366760`. |
| `Source2EntitySystem` registered node | `0x182541b20` | Registry node (vtable `0x181c25200`); distinct from the entity system above. |

**Entity-list offset inside `CGameEntitySystem`: `+0x10`.** It is a paged
identity table, not a flat pointer array:

```
page  = index >> 9          // 0..63   (64 pages)
slot  = index & 0x1ff       // 0..511  (512 per page)
entry = CEntityIdentity*    // stride 0x70
```

Evidence: `CEntitySystem::AllocateEntityIdentity` @ `0x1815aa050`
(`entitysystem.cpp:0x681`, string-confirmed), reached from
`CGameEntitySystem::CreateEntities` @ `0x1809d3530`. `CEntityIdentity`:
`+0x00` = `m_pInstance` (the entity), `+0x08` = class, `+0x10` = handle/serial.

### Game rules global

| What | Address | Notes |
|---|---|---|
| `CGameRules*` global | `0x18255be80` (RVA `0x255be80`) | Set by `CGameRules::CGameRules` @ `0x180ba13b0`; read by `CGameRulesGameSystem::GameInit` @ `0x180b9f2c0`. |

The schema field `C_CSGameRulesProxy::m_pGameRules` (`+0x600`) points at the same
object type.

### Local player (controller and pawn)

| What | Address | Notes |
|---|---|---|
| Controller array base | `0x1825338a8` (RVA `0x25338a8`) | Slot-indexed `CBasePlayerController*` array. |
| Controller accessor | `FUN_1809699f0` @ `0x1809699f0` | `slot == -1` resolves the local slot via `DAT_182557b80->vtable[0x310]()`. |
| Pawn accessor | `FUN_180969a30` @ `0x180969a30` | Reads the pawn handle at `controller + 0x6bc` and resolves it through the entity list (`0x182366770`). |

VScript bindings `GetLocalPlayerController` / `GetLocalPlayerPawn` live in
`FUN_180396820`; they call the accessors above.

## Live verification (Proton/Wine, build 25815307)

Confirmed against a running target via the dumper's `/proc` backend
(`cs2-dumper --process cs2.exe`, `cap_sys_ptrace`):

- **Entity system** — `*(client.dll + 0x2711598)` is a heap object; its vtable is
  RVA `0x1c288d0`. `*(client.dll + 0x2366770)` points to `entsys + 0x10`, and
  `*(entsys + 0x10)` is block 0 of the identity table. So the **entity-list
  offset `+0x10` and the paged layout are confirmed live**.
- **Distinct objects** — the entity system's vtable (`0x181c288d0`) differs from
  the `Source2EntitySystem` node's (`0x181c25200`), so `0x182541b20` is the
  registered interface node, not the `CGameEntitySystem` instance.
- **Game rules** — `*(client.dll + 0x255be80)` is a heap object with a
  `client.dll` vtable (RVA `0x1badc90`); the base ctor sets the pointer and the
  derived `C_CSGameRules` ctor swaps in the derived vtable.
- **Local player controller** — `*(client.dll + 0x25338a8)` is a controller
  object with a `client.dll` vtable (RVA `0x1c06520`).

## Still to recover

### View matrix

The view pipeline is mapped, but the matrix is not a simple standalone global
here and is only populated once a real 3D view is active (the menu camera is
static, so live diffing found no matrix):

- **`CViewRender` singleton** — the static object `DAT_182570420`
  (RVA `0x2570420`), pointer global `DAT_182562698` (RVA `0x2562698`), vtable
  `0x181c771b8` (RTTI `. ?AVCViewRender@@`, contains `CViewRender::Render`).
  `FUN_180cef2e0` returns `&DAT_182570420`.
- **View struct** — `CViewRender + 0x10` (stride `0x990` per player slot;
  `CViewRender::Render` copies it to `+0x9a0` via `FUN_180a36810`).
- **Per-slot client mode** — `FUN_180cec8f0(slot)` → `(&DAT_182573478)[slot]`,
  class `ClientModeCSNormal` (vtable `0x181c99c20`); world-to-screen bounds via
  `FUN_180b63080` → `FUN_180cec8f0()->vtable[0x88]`.
- **View vectors** — `FUN_180b9b9c0` caches the view origin/angles from
  `param_2+0x4a0` / `+0x4b8` into globals `0x1825589d0..0x182558a08`
  (not the matrix).

Pinning the matrix needs an active in-map camera (then a live diff of `.data`
isolates the 64-byte region); this is the next step.
