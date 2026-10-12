# `client.dll` (build 25815307)

Image base `0x180000000`. Analysis is now **complete** via the cached Ghidra
project (one cold run, then `-process -noanalysis` reuse): 76,880 procedures and
58,107 strings with the default analyzer set (including `Decompiler Parameter
ID`). See `docs/rea-project-cache.md`.

## Globals found

| What | Address | Notes |
|---|---|---|
| `CreateInterface` head global | `0x1827946c8` | `*global` = list head; NULL in file image |
| `Source2EntitySystem` registered node | `0x182541b20` | vtable `0x181c25200`; the live `CGameEntitySystem*` global is `0x182711598` (see below) |

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

### View matrix (resolved)

Three adjacent `float[4][4]` globals in `client.dll`'s `.data`, all populated
with an active in-map camera (verified live in a loaded map):

| What | Address | RVA |
|---|---|---|
| View matrix (world→view) | `0x182561c50` | `0x2561c50` |
| Projection matrix | `0x182561c90` | `0x2561c90` |
| **World-to-screen (P × V)** | `0x182561cd0` | `0x2561cd0` |

`0x182561cd0` is the `dwViewMatrix`-equivalent used for world-to-screen. It is
indexed per player slot (stride `0x40`):

- accessor `FUN_180c131c0(i) => &DAT_182561cd0 + i*0x40`;
- `FUN_180c18cb0` calls it with the local slot from
  `DAT_182557b80->vtable[0x310]()`;
- `FUN_180c13800` passes `&0x182561c50`, `&0x182561c90`, `&0x182561cd0` together.

Verified numerically live: `matrix@0x182561cd0 == matrix@0x182561c90 ×
matrix@0x182561c50` (max element error `1.3e-5`, float rounding).

Supporting pipeline:

- **`CViewRender` singleton** — static object `DAT_182570420` (RVA `0x2570420`),
  pointer global `DAT_182562698` (RVA `0x2562698`), vtable `0x181c771b8`
  (RTTI `. ?AVCViewRender@@`, contains `CViewRender::Render`).
- **View struct** — `CViewRender + 0x10` (stride `0x990` per slot);
  `CViewRender::Render` copies it to `+0x9a0` via `FUN_180a36810`.
- **Per-slot client mode** — `FUN_180cec8f0(slot)` → `(&DAT_182573478)[slot]`,
  class `ClientModeCSNormal` (vtable `0x181c99c20`).

## Re-derivation (after a game update)

Every absolute address above moves each build, so re-find them from stable
anchors rather than memorising the numbers. The curated signatures live in
`external/dumper/src/analysis.rs::known_signatures()` and resolve through their
RIP-relative operands:

```sh
cs2-dumper --resolve-offsets                       # live process
cs2-dumper --file client.dll --resolve-offsets     # on disk, no process
cs2-dumper --resolve-offsets --json > calibration/offsets.json
```

That prints one RVA per global (`source=signature`). The manual recipe behind
each signature — for when the instruction itself changes and the signature
must be re-cut — is:

### `game_entity_system` — `CGameEntitySystem*`
- **Anchor:** string `CGameEntitySystem::InitEntity2NetworkClasses`
  (also `CEntitySystem::AllocateEntityIdentity`).
- **Steps:** xref the string → the function; its head does
  `mov rbx, [rip+X]` — `X` is the global.
- **Invariant:** `*(global)` is a heap object whose vtable is in `client.dll`.

### `entity_list` — identity table (offset `+0x10` inside the entity system)
- **Anchor:** string `CEntitySystem::AllocateEntityIdentity`
  (`entitysystem.cpp:0x681`).
- **Steps:** decompile it; the paged lookup `*(this + 0x10 + (index>>9)*8)` +
  `(index & 0x1ff) * 0x70` is the entity list. The block-pointer global it reads
  is `entity_list` (also referenced by the pawn accessor's `mov r9, [rip+X]`).
- **Invariant:** 64 pages × 512 entries, stride `0x70`; `entry+0x00` = instance,
  `+0x08` = class, `+0x10` = handle.

### `game_rules` — `CGameRules*`
- **Anchor:** string `CGameRules::CGameRules constructed`.
- **Steps:** xref → the ctor; the store `mov [rip+X], rbx` of `this` is the
  global (the derived `C_CSGameRules` ctor then swaps in the derived vtable).
- **Invariant:** `*(global)` has a `client.dll` vtable.

### `local_player_controllers` — per-slot controller array
- **Anchor:** strings `GetLocalPlayerController` / `GetLocalPlayerPawn`.
- **Steps:** xref → the VScript binding function (`FUN_180396820`) → its
  `[7]`/`[8]` function-pointer table → the accessors; the accessor's
  `lea rcx, [rip+X]` is the array base (indexed by slot, `slot == -1` → local
  slot from `DAT_182557b80->vtable[0x310]()`).
- **Invariant:** `*(array + slot*8)` has a `client.dll` vtable; `+0x6bc` = pawn
  handle, resolved through `entity_list`.

### `view_matrix` — world-to-screen (P × V)
- **Anchor:** RTTI `.?AVCViewRender@@` and string `CViewRender::Render`.
- **Steps:** the `CViewRender` code passes three adjacent `0x40` matrices; the
  getter `return &global + i*0x40` is the world-to-screen array. The two matrices
  immediately *before* it are the view (world→view) and the projection.
- **Invariant:** `matrix@view_matrix == matrix@(view_matrix-0x40) ×
  matrix@(view_matrix-0x80)` (projection × view). Only populated with an active
  in-map camera.

### Regenerating the SDK offsets

```sh
cs2-dumper --resolve-offsets --json > calibration/offsets.json
cs2-dumper --gen --offsets-json calibration/offsets.json
```

A signature that stops matching is reported (`unresolved: <name>` / absent from
the JSON) rather than silently skipped, so a moved global is visible.
