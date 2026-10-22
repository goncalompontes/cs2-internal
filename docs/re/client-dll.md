# `client.dll` (build 25815307)

Image base `0x180000000`. Analysis is now **complete** via Ghidra with the
default analyzer set (including `Decompiler Parameter ID`): 76,880 procedures
and 58,107 strings. On Windows a pre-imported project can be reused per run via
`REA_GHIDRA_EXISTING_PROJECT` / `REA_GHIDRA_EXISTING_PROGRAM` (PR #2358, commit
`9adad2c`); the global MCP entry otherwise re-imports per session.

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

## Game events (`IGameEventManager2`)

The payload captures game events by **hooking the client manager's dispatch**
(ADR-0020; the harness's `events`/`wait_for(event=…)`).

| What | VA | RVA | Notes |
|---|---|---|---|
| `CGameEventManager` (`IGameEventManager2`) **pointer global** | `0x182542908` | `0x2542908` | points at the static manager object `0x18221f280`; set by the static init `FUN_1800be760` |
| `CGameEventManager` vtable | `0x181c2b858` | — | COL `0x181c2b850`; RTTI `.?AVCGameEventManager@@` `0x182312af8` |
| ↳ vtable `+0x18` (slot 3) | `0x1809c8e70` | — | `AddListener(listener, name, bServer)` → returns the event index, `-1` if the name is unknown |
| ↳ vtable `+0x20` (slot 4) | `0x1809e0d30` | — | `RemoveListener(listener, name, bServer)` |
| ↳ vtable `+0x30` (slot 6) | `0x1809d3750` | — | `CreateEvent(name, …)` |
| ↳ vtable `+0x38` (slot 7) | `0x1809e1810` | — | **`FireEvent(event, bDontBroadcast)`** — dispatches to listeners |
| ↳ vtable `+0x40` (slot 8) | `0x1809e1aa0` | — | **`FireEventClientSide(event)`** — the client-side dispatch |
| `IGameEventListener2` RTTI | `0x1822ff4d8` | — | `.?AVIGameEventListener2@@` |
| ↳ vtable `+0x08` (slot 1) | — | — | **`FireGameEvent(IGameEvent*)`** |
| `IGameEvent` vtable (`CGameEvent`, `0x181c2b5a0`) | — | — | set by the event ctor `FUN_1809c0330` (objects are 0x68 bytes) |
| ↳ `+0x08` (slot 1) | `0x1809e6f90` | — | **`GetName()`** → `const char*` |
| ↳ `+0x30` (slot 6) | — | — | `GetBool(key, def)` |
| ↳ `+0x38` (slot 7) | `0x1809e6260` | — | **`GetInt(key, def)`** |
| ↳ `+0x40` (slot 8) | — | — | `GetUint64(key, def)` |
| ↳ `+0x48` (slot 9) | `0x1809e5e80` | — | **`GetFloat(key, def)`** |
| ↳ `+0x50` (slot 10) | `0x1809e8690` | — | **`GetString(key, def)`** |
| ↳ `+0x78` (slot 15) | `0x1809e7b90` | — | **`GetPlayerIndex(int* out, key)`** — for `userid`/`attacker`/`assister` (the `player_*` field types); returns a player slot |

The getters take a **16-byte key** by pointer:

```c
struct EventKey {          // CUtlStringToken / EventHash_t
  std::uint32_t hash;      // MurmurHash2(name, len, seed), case-insensitive
  std::uint32_t pad;
  const char* str;
};
```

`hash` is the engine's case-insensitive `MurmurHash2` (`V_StringToIntHash`,
`FUN_180165ce0`), called as `hash(str, len, len ^ 0x31415926)` (so the effective
seed is the `CUtlStringToken` constant `0x31415926`). The payload resolves that
function via the `event_key_hash` signature and hashes each field name itself —
a key with `hash = 0` does **not** resolve. Slots 6/7/9/10 match the manager
serializer's own switch (type 1 string → `+0x50`, 2 float → `+0x48`, 3..6/8/9 int
→ `+0x38`, 7 bool → `+0x40`) and the community reverse of `CGameEvent` on
UnknownCheats.

**How the game registers.** `FUN_180ba9a90` is a typical listener's setup: after
obtaining the manager it registers many events through the same call —

```c
manager = *reinterpret_cast<void**>(client_base + 0x2542908);   // IGameEventManager2*
vtable  = *reinterpret_cast<void***>(manager);
// vtable[3](manager, listener, name, bServer=0)
vtable[3](manager, this_listener, "player_death", 0);
```

`CGameEventManager::AddListener` (`0x1809c8e70`) hashes the name, looks the event
up in the manager's own table (a case-insensitive string BST) and stores the
listener in that event's bucket (logging `CGameEventManager::AddListener: event
'%s' unknown.` for a name the game does not know). The table is populated **when
a level loads** — it is empty right after injection.

> **Do not call `AddListener` from a non-game thread.** Doing so from the
> harness's control thread faults inside `tier0` (the game's allocator/logging)
> on the *success* path; the observed fault was `tier0+0x1ffce9`, `read
> addr=0x40`. That is why the harness hooks the dispatch below instead of
> registering a listener.

**How a listener is called.** `FireEvent` resolves the event's definition and
iterates its bucket, calling `(**(code**)(*listener + 8))(listener, event)` —
i.e. `FireGameEvent` at listener vtable `+0x8`. Both dispatch functions read the
event name with `(**(code**)(*event + 8))(event)` (`GetName` at `+0x8`),
confirmed in the manager's own code and in two real listener implementations,
`C_GameInstructor::FireGameEvent` (`FUN_180b27540`, vtable `0x181c4e040`, COL
`0x181c4e038`) and `C_CSGameRules`' handler (`FUN_180df8e80`).

**How the harness observes.** It patches the two dispatch vtable slots (7 and 8)
to its own detour, which reads the event name (validated against the build's own
event list) and then calls the original — no listener is registered and no game
state is mutated. The prologues are too long for the inline hook's trampoline, so
this is a vtable patch (the DXGI method-table technique, ADR-0015).

**How the harness reads arguments.** For each captured event it looks the field
list up in the generated `event_names.hpp` (the union of what `core`/`game`/`mod`
declare, since `mod` *extends* `core`), hashes each field name with the engine's
own `event_key_hash` (`MurmurHash2_casefolded(name, len, 0x31415926 ^ len)`), and
calls the getter for the declared type — `GetInt`/`GetFloat`/`GetString`, bools
via `GetBool`, and `player_*` fields via `GetPlayerIndex` — formatting the value
(`%d` / `%g` / `true|false` / string / player slot). So `events()` reports
per-event `args` and `wait_for(event=…, event_args={…})` can match on them.

> Cross-checked against the community reverse of `CGameEvent` on UnknownCheats
> (the `EventHash_t` key and the `GetInt`/`GetFloat`/`GetString` =
> 7/9/10 slots agree with the serializer above).

> The engine also has its own `CGameEventSystem` (`GameEventSystemClientV001`,
> engine2 — see `docs/re/engine2.md`); client.dll caches a pointer to it at
> `0x2557b48`. In this build its event queue is never populated, so it is **not**
> the path client gameplay events take.

## Input path (`CUserCmd`)

The per-tick client command that bhop / autostrafe / aim / silent-aim write.
Everything below was derived from build 25815307 **and cross-checked against the
community reverse** (see step 4, *"Cross-check against the community reverse"*,
in [`README.md`](README.md) — do that before trusting any of it):

- UnknownCheats, *"Help with cs2 internals (createmove)"* (thread 687361):
  `CreateMove` pattern + the `buttons` field / `IN_JUMP` bhop idiom.
- UnknownCheats, *"Correct CCSGOInput::GetUserCmd logic/offsets for Index 5
  Hook?"* (thread 730511): **`CCSGOInput` vtable index 5 = `CreateMove`**, the
  `getUserCmd` chain, and `CUserCmdArray`, `CUserCmdBase`/`CUserCmdBaseHost`
  structure.
- UnknownCheats, *"CUserCmd structure & help"* (thread 702186):
  `CUserCmdBase`/`CUserCmdBaseHost<CSGOUserCmdPB>`/`CCSGOUserCmd`/`CUserCmd`
  inheritance and `CInButtonState InButtonState;`.

> The older notes were **strings, not functions**: `0x1c9c334` is `+4` of the log
> string `"cl: CreateMove - Invalid player history …"`; `0x1c338a8` is the schema
> field-name string `m_nButtons`; `0x2320304` is the `.?AVCCSGOInput@@`
> `_TypeDescriptor` (`0x1823202f0`, name at `0x182320300`).

### `CCSGOInput` (the input service; the `dwCSGOInput` global)

| What | VA | RVA | Notes |
|---|---|---|---|
| `CCSGOInput` RTTI | `0x1823202f0` | `0x23202f0` | `.?AVCCSGOInput@@` (name at `+0x10`) |
| `CCSGOInput` singleton object | `0x182572460` | `0x2572460` | static object; vtable installed by `FUN_1800ee570` |
| `CCSGOInput` vtable | `0x181c97b28` | `0x1c97b28` | COL `0x181f1d598` |
| `CCSGOInput*` global (`dwCSGOInput`) | `0x182233d10` | `0x2233d10` | holds `0x182572460`; the handle the game reads everywhere |
| ↳ vtable `+0x28` (slot 5) | `0x180b64e60` | `0xb64e60` | **`CreateMove(CCSGOInput*, int slot, bool active)`** — fetches the cmd via `GetUserCmd`; the community's hook slot |
| ↳ vtable `+0xc8` (slot 25) | `0x180d01610` | `0xd01610` | **`CreateMove(CCSGOInput*, int, CUserCmd* cmd)`** — receives the `CUserCmd*` directly in `r8`; the attack-history/sub-tick processor |

`FUN_180d01610` (`create_move`) is the one that already has the command in hand:
its third argument (`r8`) is the `CUserCmd*`, and param usage confirms it
(`[cmd+0x08]` = command number — logged as `"cmd %d"`; `[cmd+0x10]` = the
protobuf whose `ByteSize` is logged as `"Usr cmd size = %d B"`). Hook it inline
(or patch slot 25) to write the command with no extra plumbing.
`FUN_180b64e60` (`create_move_input`) is slot 5; it resolves the local
controller (`FUN_1809699f0(0)`), fetches the current `CUserCmd` through the
chain below, and processes it. Hooking slot 5 is the community-standard seam and
is where a `usercmd` backend should live; hooking slot 25 is the shortcut when
you only need the command.

### `CUserCmd` layout

The command is `CUserCmd : CCSGOUserCmd : CUserCmdBaseHost<CSGOUserCmdPB> :
CUserCmdBase`. `stride = 0x98`, 150 per `CUserCmdArray` (confirmed by
`get_user_cmd_by_sequence`: `base + (seq % 0x96) * 0x98`):

| Offset | Size | What | Evidence |
|---|---|---|---|
| `+0x00` | 8 | vtable (`CUserCmdBase` `0x181c14ec8` → … → final `0x181c14fe8`) | `get_user_cmd_array` init loop |
| `+0x08` | 4 | `nSequenceNumber` (uint32) | `get_user_cmd_by_sequence` compares it |
| `+0x10` | `0x48` | **`CSGOUserCmdPB`** (protobuf message object; vtable `0x181bc6dd0`; its `New()` = `FUN_1807b08f0` allocs `0x48`) | `FUN_1807b1310` ctor; `FUN_1807afce0(cmd+0x10)` = `ByteSize` |
| `+0x58` | `0x20` | **`CInButtonState`** (RTTI `.?AVCInButtonState@@` `0x182306758`, vtable `0x181b23590` + 3× `uint64`) | `FUN_1804b4e90` ctor |
| `+0x60` | 8 | button mask #1 — **`buttons`** (`IN_JUMP = 1<<1`) | community `cmd->buttons` @ `0x60`; 1st `uint64` after the vtable |
| `+0x68` | 8 | button mask #2 | |
| `+0x70` | 8 | button mask #3 | |
| `+0x78` | 4 | `-1` at construction | |
| `+0x88`/`+0x8c`/`+0x90` | 4/1/8 | misc (`bHasBeenPredicted`-ish) | community `CUserCmd` tail |

> The `CSGOUserCmdPB` object is protobuf (`_has_bits_`/fields) so its *internal*
> field offsets are not stable; use the generated accessors or the cached
> `InButtonState` at `+0x58`. View angles are subtick (`CSubtickMoveStep`) — a
> "silent aim" writes the subtick angle history, not a plain `viewangles` field.
>
> **Live-verified (build 25815307):** the button mask is `cmd + 0x60` — a console
> `+forward` sets bit 3 and `+jump` bit 1 (read from a running game). Writing that
> bit from the payload's `CreateMove` hook leaves the player grounded for `jump`
> and hops for `bhop`, but does **not** produce `forward` movement:
> `create_move_input` derives `CBaseUserCmdPB::forwardmove`/`leftmove`
> (`base+0x58`/`+0x5c`) from the raw input at `CCSGOInput + 0x270/0x274` each
> tick, so the movement source of truth is the raw input, not the command. See
> [`subtick.md`](subtick.md) for the sub-tick structures (view-angle history,
> move steps, the command build and the movement consumer),
> [`prediction.md`](prediction.md) §"RE findings" for the sub-tick **jump chain**
> (`DECIDE`/`DoJump`, `m_ModernJump`, the press-edge machine, the controller tick
> base) and `docs/control-harness.md` §"`usercmd` backend".

### Reading the current command without hooking

`CUserCmdArray*` global `0x18221bd10` (RVA `0x221bd10`). `CUserCmdArray` is
`0x5970` bytes, 150 commands, `m_nSequenceNumber` at `+0x5910`. The chain the
game (and the community) uses:

```c
controller = FUN_1809699f0(0);                       // local controller, slot 0
get_user_cmd_tick(controller, &tick);                // 0x15d9b20
array = get_user_cmd_array(*CUserCmdArray_global, tick - 1);   // 0x943800
seq   = *(uint32_t*)(array + 0x5910);
cmd   = get_user_cmd_by_sequence(controller, seq);   // 0x9435e0
```

Signatures for `create_move`, `create_move_input`, `ccsgo_input`,
`user_cmd_array`, `get_user_cmd_tick`, `get_user_cmd_array`
and `get_user_cmd_by_sequence` live in
`external/dumper/src/analysis.rs::known_signatures()` (all resolve offline: see
the re-derivation section). `cl_showusercmd` / `cl_usercmd_showsize` remain handy
live checks via the harness `exec` channel.

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
- **Consumer:** the payload reads it every snapshot for the authoritative round
  phase (`payload/game/src/state_reader.cpp` `read_rules`, `C_CSGameRules`
  fields from the generated schema: `m_bFreezePeriod`, `m_bWarmupPeriod`,
  `m_gamePhase`, `m_iRoundTime`, `m_iRoundWinStatus`, `m_bTCantBuy`/
  `m_bCTCantBuy`, `m_bBombPlanted`, …). Exposed as `state`'s `rules` and consumed
  by the scenario screen state machine (`scripts/scenario/screens.py`,
  `docs/control-harness.md` §"The screen state machine").
- **What is *not* the main-screen test (all measured live 2026-10-15):** the
  `game_rules` global is **readable at the main menu** (a default, unstarted
  `C_CSGameRules`: `warmup = freeze = true`, `match_started = false`,
  `total_rounds_played = 0`, `round_start_time = 0`, but note
  `match_start_time = warmup_period_start = 1`); the main menu **is a level world**
  (258 entities in the identity list, and a `C_CSGameRulesProxy` entity whose
  `m_pGameRules` is the global). So neither "the rules exist" nor "a rules proxy
  exists" separates the menu from a game. The harness's interim rule is the
  observable consequence — *the local player is spawned in the level* (the menu
  placeholder sits at `origin = (0,0,0)`, no team, no weapon, `life_state = 2`) —
  see `docs/control-harness.md` §"The screen state machine".

## "Am I in a game?" — the main-menu rule (community)

The canonical community answer is **`CNetworkGameClient`**: `dwNetworkGameClient`
(engine2) holds the client, and two fields decide:

```c
CNetworkGameClient* ngc = *(CNetworkGameClient**)(engine2 + dwNetworkGameClient);
uint8_t  is_background_map = *(uint8_t*)(ngc + m_bIsBackgroundMap);  // vtable slot 17
uint32_t signon_state      = *(uint32_t*)(ngc + m_nSignonState);
bool is_connected = signon_state >= 2;                            // SIGNONSTATE_CONNECTED
bool is_in_game   = is_background_map == 0 && signon_state >= 6;  // SIGNONSTATE_FULL
```

- **Sources:** `cs2-dumper` issue #111 (2024-04-05) — the recipe above, with the
  signature for the global `48 89 3D ? ? ? ? 48 8D 15`; `UC "Signonstate value?"`
  (2023-12-01) — *"even in the main menu you are on a map, so ur connected"*, i.e.
  the signon state alone is 6 in the menu, which is why `is_background_map` is
  needed (the same thread also suggests the current map name — *reported
  unreliable* — or the game phase); `sezzyaep/CS2-OFFSETS` —
  `dwNetworkGameClient`, `dwNetworkGameClient_signOnState`,
  `dwNetworkGameClient_isBackgroundMap` ("boolean flag indicating if the current
  map is a UI background"); `SignonState_t` (s2v schema) — `NONE=0, CHALLENGE=1,
  CONNECTED=2, NEW=3, PRESPAWN=4, SPAWN=5, FULL=6, CHANGELEVEL=7`. The same
  question via engine vfuncs: `IsInGame` = vtable index 32, `IsConnected` = 33.
- **Cross-check against our build (measured live 2026-10-15)** — it matches: the
  menu is a background map, which is exactly why the menu has entities, a rules
  proxy and a readable rules object.
- **Not the rule** (measured on our build, same date): `rules.loaded` (true at the
  menu), the rules-proxy entity (present at the menu),
  `CBasePlayerController::m_iConnected` (`+0x6f4`; **0 even in a game** — the older
  community offset `0x63c` for `m_iConnected` does not behave as "connected"
  here), `rules.match_start_time`/`m_fWarmupPeriodStart` (1 at the menu, not 0),
  and the world entity count (258/382 at the menu vs 726 in a level — magnitude
  only).
- **Follow-up (not implemented):** resolve `dwNetworkGameClient` with the
  signature above and read `m_bIsBackgroundMap`/`m_nSignonState`, so the harness's
  `in_game` is the canonical rule instead of the spawn heuristic. The two field
  offsets are build-specific (cs2-dumper resolves them separately), so they need
  their own anchor or a live re-derivation.

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
- **Convention:** the matrix is row-major and applied to a **column vector**
  (`clip = M · world`, `clip.i = Σ_j m[i*4+j]·world.j`). Verified live:
  `matrix@view_matrix == matrix@projection · matrix@view` (row-major multiply,
  max element error `6e-5`), the camera eye maps to `clip ≈ (0, 0, 0)`, and a
  point `d` units ahead maps to `clip.w ≈ d`. Screen mapping:
  `sx = (ndc.x·0.5 + 0.5)·W`, `sy = (0.5 − ndc.y·0.5)·H`.
  (A row-vector form, `clip = world · M`, does **not** satisfy the invariant.)

### `game_event_manager` — `IGameEventManager2*`
- **Anchor:** string `CGameEventManager::AddListener: event '%s' unknown.`
  (`0x181c2bca0`) → xref `0x1809c8f3c` → `FUN_1809c8e70` (`AddListener`); or RTTI
  `.?AVCGameEventManager@@` → COL → vtable `0x181c2b858`, where slot 3 (`+0x18`)
  is `AddListener`.
- **Steps:** the manager is a **static** object; the init `FUN_1800be760` does
  `if (DAT_182542908 == 0) DAT_182542908 = &<static CGameEventManager>` — the
  store `MOV [rip+X], RCX` right after a `MOV qword [rip+X], 0xc80000` /
  `JNZ +7` is the signature. Decompile `FUN_1800be760` if the pattern moves.
- **Invariant:** `*(client.dll + game_event_manager)` is an object whose vtable
  (`*(*(client.dll + game_event_manager))`) points inside `client.dll`; the
  dispatch slots (`+0x38`/`+0x40`) iterate listener buckets and call
  `listener->vtable[+0x8]`, and `IGameEvent::GetName` is at event vtable `+0x8`.
- **Event names:** the manager's definitions come from
  `resource/{core,game,mod}.gameevents` (VPK-packed, loaded per level);
  `scripts/extract-gameevents.py` extracts the authoritative name list and emits
  `payload/control/include/cs2/control/event_names.hpp`.

### `create_move` / `create_move_input` — the per-tick input step
- **`create_move` (`FUN_180d01610`, receives the `CUserCmd*`)** — anchor: the log
  strings `"cl: CreateMove clamped invalid attack history %d index in cmd …"`
  (`0x181c9c580`) and `"cl: CreateMove - Frame %d, cmd %d …"` (`0x181c9c5e0`);
  xref them into the function. **Or** scan the community prologue
  `48 8B C4 4C 89 40 ?? 48 89 48 ?? 55 53 57` (unique; `mov rax,rsp; mov
  [rax+18h],r8; mov [rax+8],rcx; push rbp/rbx/rdi`). Resolve the match start.
- **`create_move_input` (`FUN_180b64e60`, vtable slot 5)** — scan
  `85 D2 0F 85 ?? ?? ?? ?? 48 8B C4 44 88 40` (unique). Resolve the match start.
- **Invariant:** both are in the `CCSGOInput` vtable (`0x181c97b28`); the
  `create_move_input` body starts by resolving the local controller via
  `FUN_1809699f0(0)` and calling the `get_user_cmd_*` chain.

### `ccsgo_input` — the input singleton
- **Anchor:** the static init `FUN_1800ee570` does
  `LEA RCX,[object]; CALL …; …; LEA RAX,[vtable]; MOV [obj+0x…], RAX;
  MOV [object], RAX`.
- **Steps:** scan
  `48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C9 C7 05 ?? ?? ?? ?? FF FF FF FF 48 8D 05 ?? ?? ?? ?? 48 89 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??`
  (unique), rip-offset `3`, instruction length `7` → the **object**
  (`0x182572460`).
- **Invariant:** `*(object)` is the `CCSGOInput` vtable (`0x181c97b28`; read it
  at runtime rather than signature it — the LEA for it is not unique on its own).
  The game reaches the object through the `dwCSGOInput` pointer (RVA
  `0x2233d10`).

### `user_cmd_array` — the `CUserCmdArray*` global
- **Anchor:** inside `create_move_input`, the sequence
  `MOV RCX,[global]; CALL …; …` — scan
  `48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B CF 4C 8B F8` (unique),
  rip-offset `3`, instruction length `7`.
- **Invariant:** `*(global)` is the `0x5970`-byte `CUserCmdArray`
  (`m_nSequenceNumber` at `+0x5910`).

### `get_user_cmd_tick` / `get_user_cmd_array` / `get_user_cmd_by_sequence`
- **Anchor:** the `create_move_input` body calls them in order; scan the
  community patterns (all unique): `48 83 EC ?? 4C 8B 0D ?? ?? ?? ?? 4C 8B DA`,
  `48 89 4C 24 ?? 41 56 41 57`,
  `40 53 48 83 EC ?? 8B DA E8 ?? ?? ?? ?? 4C 8B C0`. Resolve the match starts.
- **Invariant:** `get_user_cmd_by_sequence` returns `base + (seq % 0x96) * 0x98`
  and validates `[cmd+0x8] == seq`.

### Regenerating the SDK offsets

```sh
cs2-dumper --resolve-offsets --json > calibration/offsets.json
cs2-dumper --gen --offsets-json calibration/offsets.json
```

A signature that stops matching is reported (`unresolved: <name>` / absent from
the JSON) rather than silently skipped, so a moved global is visible.
