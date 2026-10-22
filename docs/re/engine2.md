# `engine2.dll` (build 25815307)

Image base `0x180000000`. Addresses below are **absolute VAs** from the Ghidra
analysis; RVAs are `VA - 0x180000000`.

> Analysis on this host is REA + Ghidra 12.1.4 (see `docs/control-harness.md`
> "RE tooling"). The target analyzed is a local copy:
> `C:\Users\gpmpo\ghidra-analysis\engine2.dll`
> (sha256 `4b8fe29ecdef4f492a602d9013cbfe5226561c9c0ba4361ce82e6f46d4f9bc04`),
> with a pre-imported Ghidra project at
> `C:\Users\gpmpo\ghidra-analysis\rea-project` (program `engine2.dll`).

## Why this file exists

The programmatic test harness (`docs/control-harness.md`) needs to run **console
commands** inside a running CS2 and (optionally) drive them from outside the
process. This file records the RE of the engine's console-command path.

## TL;DR / decision

Two `exec` primitives exist. Both are now located and characterised:

| Primitive | Where | Needs injection? | Notes |
|---|---|---|---|
| **In-process `Cbuf_AddText`** | `FUN_1801b8790(&g_InputService, 5, line, 0)` | yes (payload) | **Recommended.** Same path the engine uses for its own queued/pipe input. Thread-safe. |
| **`-concommandpipe`** | launch flag `-insecure -concommandpipe <in>,<out>` | **no** | Built-in named-pipe console channel. Engine is the pipe *client*; **input-only** in this build (see below). Good fallback / no-injection debug channel. |

Either way the text ends up in **command-buffer slot 5**, which the engine
drains every frame. `-insecure` is not required for the in-process call (it *is*
required for `-concommandpipe`), but it also clears `m_bDefensiveConCommands`
(below), so the harness launches with it regardless.

## CInputService / the console command buffer ("Cbuf")

The console command machinery lives in the **`CInputService` singleton**
(`src/engine2/inputservice.cpp`), a large global object at **VA `0x1808dbf20`**
(RVA `0x8dbf20`). Ghidra prints the base as `DAT_1808dbf20`; the same pointer is
what the engine passes to the Cbuf wrapper on every owned path.

| What | VA | RVA | Notes |
|---|---|---|---|
| `g_InputService` (singleton object) | `0x1808dbf20` | `0x8dbf20` | base of all offsets below |
| **`Cbuf_AddText` wrapper** | `0x1801b8790` | `0x1b8790` | `(this, slot, text, flags)` → `CCommandBuffer::AddText` |
| `CCommandBuffer::AddText` | `0x1801c70e0` | `0x1c70e0` | locks the slot mutex, appends text |
| per-frame command-buffer drain | `0x1801c6300` | `0x1c6300` | processes slots 0..5 |
| ConCommand/ConVar dispatch | `0x1801c67a0` | `0x1c67a0` | resolves name, checks `FCVAR`, executes/sets |
| execute concommand | `0x1801c5910` | `0x1c5910` | called once flags pass |
| set convar | `0x1801c5e40` | `0x1c5e40` | convar set path |
| `CInputService::Init` | `0x1801bfe00` | `0x1bfe00` | parses `-concommandpipe`, stdin-cvar opts |
| `CInputService::OnClientProcessInput` | `0x1801c40c0` | `0x1c40c0` | per-frame; pumps the concommand pipe |
| shutdown / destroy | `0x1801c0b40` | `0x1c0b40` | frees the pipe channel |

`FUN_1801b8790` is a thin wrapper that zeroes the trailing stack args and tails
into `FUN_1801c70e0`:

```asm
FUN_1801b8790:                 ; (rcx=this, edx=slot, r8=text, r9d=flags)
  sub  rsp, 0x58
  movsd xmm0, [0x1805911c0]    ; default (0.0) for the float arg
  ...
  mov  [rsp+0x20], eax         ; 5th arg  = 0
  mov  [rsp+0x28], rax         ; 6th arg  = 0
  mov  [rsp+0x30], al          ; 7th arg  = false
  mov  [rsp+0x38], al          ; 8th arg  = false
  movsd[rsp+0x40], xmm0        ; 9th arg  = 0.0
  mov  [rsp+0x48], rax         ; 10th arg = 0
  call 0x1801c70e0
  add  rsp, 0x58
  ret
```

`FUN_1801c70e0` (`CCommandBuffer::AddText`) behaviour:

- slot selector `slot` picks both a per-slot `CAtomicMutex` at
  `this + (slot + 0x43e2) * 0xc` and the `CCommandBuffer` at
  `this + 0x588 + slot * 0x86d8`;
- it locks the mutex, temporarily swaps the `CCommandBuffer`'s required `FCVAR`
  mask via `CCommandBuffer::SetRequiredFlags`, appends the text
  (`CCommandBuffer::AddText`), restores the mask, and optionally records the
  line in a `CUtlString` at `this + 0x32ee0`.

The engine's own console/pipe input uses **slot 5**:

```c
// CCommandBuffer drain, FUN_1801c6300:  for slot in 0..5 { ... execute ... }
FUN_1801b8790(&DAT_1808dbf20, 5, line, 0);   // used by FUN_1801c40c0 and FUN_1801c7310
```

So the in-process primitive is simply:

```c
using cbuf_add_text_t = void (*)(void* input_service, int slot,
                                 const char* text, int flags);
auto add_text = reinterpret_cast<cbuf_add_text_t>(module_base + 0x1b8790);
add_text(reinterpret_cast<void*>(module_base + 0x8dbf20), 5, "sv_cheats 1", 0);
```

The line is executed on the next frame that runs `FUN_1801c6300`.

### `FCVAR` gating (why the harness runs `-insecure`)

`CInputService::Init` sets `m_bDefensiveConCommands` at **`CInputService + 0x3ec60`**:

```c
bDefensive = GetGameInfoBool("Engine2/DefensiveConCommands", false)
             && !IsInsecureBuild();          // vtable[0xb0] on DAT_180696750
this->m_bDefensiveConCommands = bDefensive ? 1 : 0;
```

`FUN_1801c67a0` (dispatch) reads `+0x3ec60` (sites `0x1801c68ff`, `0x1801c6937`,
`0x1801c6b2d`, `0x1801c6ba1`) to reject commands when defensive mode is on.
`-insecure` turns it off.

## `-concommandpipe` — built-in external console channel

`CInputService::Init` (`FUN_1801bfe00`) handles a command-line option hashed
`0x6e6cda1d` (`-concommandpipe`):

```c
// requires -insecure (option hash 0x8f74045f); else logs the message below
if (CommandLine()->FindParm(..., 0x6e6cda1d) > 0) {
    if (CommandLine()->HasParm(0x8f74045f)) {
        const char* spec = CommandLine()->ParmValue(..., i + 1);
        this->m_pConCommandPipe = FUN_1801b9770(spec);   // CInputService + 0x3ece8
    } else {
        LoggingSystem_Log(..., "-concommandpipe requires -insecure");
    }
}
```

| What | VA | RVA |
|---|---|---|
| `-concommandpipe` handler (in `CInputService::Init`) | `0x1801bfe00` | `0x1bfe00` |
| `"-concommandpipe requires -insecure"` | `0x18057cb90` | `0x57cb90` (xref `0x1801c0379`) |
| `CConsoleCommandPipes::Create(spec)` | `0x1801b9770` | `0x1b9770` |
| read/dispatch loop | `0x1801c7310` | `0x1c7310` |
| pipe object field | `CInputService + 0x3ece8` | `0x3ece8` |
| "defensive concommands" flag | `CInputService + 0x3ec60` | `0x3ec60` |

### Spec grammar — `CConsoleCommandPipes::Create` (`FUN_1801b9770`)

`spec` is split on `,` (`V_SplitString`). Fields are classified numeric
(`V_isdigit_str` / leading `-`) or names; the object is a 0x50-byte allocation
holding an in/out pipe pair.

- **Named form** — `-concommandpipe "\\.\pipe\<in>,\\.\pipe\<out>"`:

  | field | connection | opened as |
  |---|---|---|
  | 0 (`<in>`)  | read pipe  | `GENERIC_READ`, `fopen` mode `"rb"` |
  | 1 (`<out>`) | write pipe | `GENERIC_WRITE`, `fopen` mode `"wb"`, `setvbuf` `_IONBF` |

- **Handle form** — `-concommandpipe "<readHandle>,<writeHandle>,<sourcePID>"`
  (all fields numeric): the engine `DuplicateHandle`s both handles out of
  `sourcePID` (`ThreadOpenProcess`/`DuplicateHandle`, `FUN_18041bf50`).

Both pipes are opened with `CreateFileA(name, access, ..., OPEN_EXISTING, ...)`
(`FUN_18041bdb0`, named; `FUN_18041bf50`, handles). **The engine is the pipe
client**, so the external tool must create the server end(s) *before* the engine
connects. Internally the read connection carries `PIPE_READMODE_MESSAGE` when a
flag requests it.

Pipe channel object layout (`FUN_1801c7310` / `FUN_1801c0b40`):

| offset | meaning |
|---|---|
| `0x00` | read `FILE*` (from `<in>`) |
| `0x18` | write `FILE*` (from `<out>`) |
| `0x30` | accumulate `CBufferString` length |
| `0x38` | accumulate `CBufferString` buffer ptr |
| `0x40` | accumulate `CBufferString` capacity |
| `0x44` | accumulate `CBufferString` flags |
| `0x48` | `int` pending-newline count |

### Read / dispatch loop (`FUN_1801c7310`)

Called every frame from `CInputService::OnClientProcessInput` (`FUN_1801c40c0`).
It is the classic `CConCommandPipe::ProcessCommands`:

1. if `m_pConCommandPipe == 0`, return;
2. `fgetc` from the read `FILE*` into a 1024-byte staging buffer, appending into
   the object's `CBufferString` and counting `\n` (0 and `\n` both count as line
   terminators);
3. while a complete line is pending, cut it out of the accumulate buffer and call
   `FUN_1801b8790(&DAT_1808dbf20, 5, line, 0)` — i.e. **the same Cbuf path the
   console uses**;
4. on EOF, close the read connection (`FUN_18041bd50`).

### Output direction — **not implemented in this build**

The write `FILE*` at `CInputService + 0x3ece8 -> 0x18` is only ever touched by
the destructor (`FUN_1801c0b40` closes it). No `fwrite`/`fprintf`/`WriteFile`
call site loads it (`V_fwrite`/`V_fprintf`/`WriteFile` xrefs were enumerated and
none reach it). Conclusion (inferred from the complete xref set): the `<out>`
pipe is opened but **never written** — treat `-concommandpipe` as an
**input-only** channel here.

Console *output* therefore has to come from another source for `wait_for`
predicates / assertions:

- `-condebug` (engine console log file), or
- the payload (in-process console/logging hook), or
- the logging system listener.

`CPipedCommandLogRedirector` (RTTI `.?AVCPipedCommandLogRedirector@@`,
vtable `0x18057dcb8`) exists but is **not** the output writer: it is a temporary
object built on the stack in `FUN_1801c5910` which captures the console output
lines produced while a command runs and **re-executes** each captured line as a
concommand/convar (`FUN_1801c54e0`). It does not touch the pipe.

## Game event system (`GameEventSystemClientV001`) — engine side (+ client API)

The harness wants a listener so scenarios can await/assert on game events
(`player_death`, `round_start`, …) instead of polling state. The interface is an
**engine2** interface (not client.dll) and is reachable from the generated SDK.

| What | VA | RVA |
|---|---|---|
| `GameEventSystemClientV001` create thunk (`create()`) | `0x180216a80` | `0x216a80` |
| `GameEventSystemServerV001` create thunk | `0x180216a90` | `0x216a90` |
| **`CGameEventSystem` singleton** (`create()` returns `&object`) | `0x18091c9e0` | `0x91c9e0` |
| `CGameEventSystem` constructor | `0x180216aa0` | `0x216aa0` |
| singleton vtable (RTTI `.?AVCGameEventSystem@@`) | `0x18058ae38` | `0x58ae38` |
| event **name → definition** registry global | `0x180696960` | `0x696960` |
| dispatch / service-events (`vtable + 0x90`) | `0x1802180c0` | `0x2180c0` |
| add-listener path (`vtable + 0x60`) | `0x1802170b0` | `0x2170b0` |
| remove-listener path (`vtable + 0x68`) | `0x1802171d0` | `0x2171d0` |

Verified from the code:

- `create()` is a 7-byte thunk: `LEA RAX, [singleton]; RET` — the singleton is a
  **static object** (its vtable pointer is written by `FUN_180216aa0` at runtime;
  the file image is zeroed).
- The dispatch pump (`FUN_1802180c0`) walks the queued events, calls
  **`IGameEvent::GetName` at vtable offset `0x18`** on each event to identify it,
  resolves the definition through `DAT_180696960->vtable[0x60]`, then invokes the
  listeners registered in the `CEventDispatcher_Base` embedded at
  `singleton + 0x70`.
- Add/remove log on the game-event channel:
  `"GameEvent:  Event %s (id:%d group:'%s') added listener."` (`0x18058b3c8`) /
  `…removed listener.` (`0x18058b310`), and
  `"Error trying to register a handler for an unknown game event type!"`
  (`0x18058b380`).
- `gameevents_showeventlisteners` is a console command that prints the current
  listeners — a cheap live check.

### Interface vtable (`0x18058ae38`) and the dispatch pump

`create()` is a 7-byte thunk (`LEA RAX,[singleton]; RET`), so the
`GameEventSystemClientV001` interface pointer **is** the singleton and its vtable
is `0x18058ae38`. The confirmed slots (offset = index·8, from `read_bytes` +
`address_name`):

| off (slot) | function | role |
|---|---|---|
| `+0x58` (11) | `FUN_180217060` | `FireEvent(event)`: resolves the event definition and forwards to the engine broadcast |
| `+0x60` (12) | `FUN_1802170b0` | **add listener** — logs `GameEvent:  Event %s … added listener.` |
| `+0x68` (13) | `FUN_1802171d0` | **remove listener** — logs `… removed listener.` |
| `+0x90` (18) | `FUN_1802180c0` | dispatch pump (`ServiceEvents`): drains the queued events and calls the listeners |

(Slots 0–10 and 14–20 are the `CGameEventSystem` ctor/base machinery and the
event queue; slot 5 resolves to `_guard_check_icall`. The function names in the
Ghidra project are all analysis-generated `FUN_*` — there are no PDB symbols.)

### Listener shape — a plain `{context, callback}` pair (no vtable)

The listener registry is a **tier0** `CEventDispatcher_Base` embedded at
`singleton + 0x70`. Registration goes through the tier0 exports
`CEventDispatcher_Base::RegisterEventListener_Base` (ordinal `0xa4`) /
`UnregisterEventListener_Base` (ordinal `0xa3`), whose mangled names give the
exact signature:

```
RegisterEventListener_Base(CEventDispatcher_Base*, const CUtlAbstractDelegate&,
                           unsigned int, bool,
                           CUtlVector<EventListenerInfo_t>&, int, const char*);
```

In the dispatch pump each entry returned by `IterateListener` is invoked as a raw
`{context, callback}` pair — **there is no listener interface/vtable at the call
site**:

```asm
MOVZX EDX, byte ptr [RAX + 0x1c]   ; "wants the IGameEvent* argument" flag
MOV   R8,  qword ptr [RAX + 0x8]   ; callback
MOV   RCX, qword ptr [RAX]         ; context ("obj")
TEST  EDX, EDX
JZ    one_arg
MOV   RDX, RDI                     ; the IGameEvent* being dispatched
CALL  R8                           ; callback(obj, event)
```

So the effective listener is `void (*)(void* ctx, IGameEvent* event)` plus a byte
at entry `+0x1c` selecting the 1-arg vs 2-arg form; the engine wraps it in a
16-byte `CUtlAbstractDelegate` (`{obj, fn}`) before calling
`RegisterEventListener_Base`. `IGameEvent::GetName` is at event vtable `+0x18`.

### Event queue (the pump's input)

`FUN_1802180c0` drains the singleton's own event queue:

| offset | meaning |
|---|---|
| `+0x100` | element count (`int`) |
| `+0x104` | capacity (`uint`; high bits are allocation flags) |
| `+0x108` | data pointer |
| `+0x110` | current index (`int`; `-1` = end) |
| `+0x114` | "last index" |

Each element is `0x10` bytes: `[0]` = `IGameEvent*`, `[8]` = `uint32 id`.

> **This queue is unused in build 25815307.** A payload hook on the pump shows it
> running (~330×/s) with the queue always empty (`capacity 0`, `data null`,
> `first/last = -1`): CS2 client gameplay events do **not** flow through the
> engine's `CGameEventSystem`. They are dispatched by client.dll's
> `CGameEventManager` — see `docs/re/client-dll.md` §"Game events". (The layout
> is recorded in case a future build uses it.)

### Resolved in client.dll

Neither `IGameEventListener2` nor a listener wrapper exists in engine2 (string /
RTTI search is empty). The client-facing API is **client.dll**'s
`CGameEventManager` (`IGameEventManager2`): `AddListener(IGameEventListener2*,
name, bServer)` at vtable `+0x18` and `FireGameEvent` at listener vtable `+0x8`
(`IGameEvent::GetName` at `+0x8`). The observed engine2 add path
`FUN_1802170b0` takes an already-built `CUtlAbstractDelegate`
(`(this, key, const CUtlAbstractDelegate&, name, bool)`) and has **no direct
callers** in engine2 (reached only through the vtable); client.dll registers a
single delegate with it (via its cached `CGameEventSystem*` at `0x2557b48`) and
re-dispatches to `IGameEventListener2` clients. See `docs/re/client-dll.md`
§"Game events".

## Still to recover

1. **Console-output capture** for the harness (which of `-condebug` / a payload
   logging hook / a ConCommand output hook to use). Needed for authoritative
   `exec` results and `wait_for(text=...)`.
2. **`look` (direct view angles)** and **`input` (usercmd injection)** — these
   live in `client.dll` (view angles / `CUserCmd`), so they need the client.dll
   analysis project, not this one. The runner is in Windows session 0 while the
   game is in session 1, so OS-level `SendInput` from the runner will not reach
   the game; input must be injected **in-process by the payload**.

## Curated signatures (for `external/dumper`, ADR-0012)

Both were read out of build 25815307 and verified unique against the local
image. `sig()` below is `analysis::known_signatures`' helper; add with
`module = "engine2.dll"`.

| name | pattern | rip_offset | instr_len |
|---|---|---|---|
| `input_service` | `38 00 0F 84 ?? ?? ?? ?? 45 33 C9 48 8D 0D ?? ?? ?? ?? BA 05 00 00 00 E8 ?? ?? ?? ??` | 14 | 18 |
| `cbuf_add_text` | `48 83 EC 58 F2 0F 10 05 ?? ?? ?? ?? 33 C0 48 89 44 24 48 F2 0F 11 44 24 40 88 44 24 38 88 44 24 30 48 89 44 24 28 89 44 24 20 E8 ?? ?? ?? ?? 48 83 C4 58 C3` | – (match start) | – |

- `input_service` anchors on `LEA RCX, [g_InputService]; MOV EDX, 5; CALL
  Cbuf_AddText` inside `CInputService::OnClientProcessInput` and resolves the
  global through the `LEA`'s RIP-relative displacement → `0x8dbf20`.
- `cbuf_add_text` anchors on the whole `FUN_1801b8790` wrapper → `0x1b8790`.
- (The bare `... 48 8D 0D ?? ?? ?? ?? BA 05 00 00 00 E8 ...` fragment also
  matches a second site that references the **same** global, so the extra
  `38 00 0F 84 ?? ?? ?? ??` prefix is used to make the match unique.)

## Reproduction

```powershell
$env:GHIDRA_INSTALL_DIR = "C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC"
$env:JAVA_HOME          = "C:\Program Files\Java\jdk-21"
$env:REA_ANALYSIS_PROVIDER     = "ghidra"
$env:REA_GHIDRA_EXISTING_PROJECT = "C:\Users\gpmpo\ghidra-analysis\rea-project"
$env:REA_GHIDRA_EXISTING_PROGRAM = "engine2.dll"

rea search   "C:\Users\gpmpo\ghidra-analysis\engine2.dll" "concommandpipe" --format json
rea decompile "C:\Users\gpmpo\ghidra-analysis\engine2.dll" FUN_1801bfe00 --format json
rea decompile "C:\Users\gpmpo\ghidra-analysis\engine2.dll" FUN_1801b9770 --format json
rea decompile "C:\Users\gpmpo\ghidra-analysis\engine2.dll" FUN_1801c7310 --format json
rea decompile "C:\Users\gpmpo\ghidra-analysis\engine2.dll" FUN_1801c70e0 --format json
```

Warm opens with the pre-imported project take ~13 s; a cold re-import takes
~4 min.
