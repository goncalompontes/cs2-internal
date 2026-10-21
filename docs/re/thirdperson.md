# The third-person camera: the crash that killed the game

> **The one-line version:** the camera hook's address was **valid**; the crash
> was the **`thread_local` re-entrancy guard** in the detour. The payload is
> manually mapped, the game's camera thread has no TLS block for it, and the
> detour's very first instruction read that block — so the read faulted
> *inside the payload* and the game died.

---

## 1. The symptom

The operator turned the third-person camera on and the game died. The
payload's log, one line before the crash:

```
[cs2-features] third person: the camera update is hooked
[crash] first-chance code=0xc0000005 at 0x1832100dd9c read addr=0xffffffffffffffff
```

* the faulting instruction is **inside the payload** (the manually mapped
  image), not inside the game;
* it is a **read**, of `0xFFFFFFFFFFFFFFFF`;
* the crash came **immediately after** the hook was installed: the first
  camera update through the detour faulted.

The log line is the payload's own crash handler
(`payload/diag/src/crash_handler.cpp:106`): `at` is
`ExceptionRecord->ExceptionAddress` (the instruction) and `addr=` is
`ExceptionInformation[1]` (the address the instruction tried to read).

## 2. The faulting instruction is the `thread_local` read

The previous handoff (`docs/handoff-wave2.md` §5.1b) blamed a **stale hook
address**. That is wrong — but the proof is not the reverse engineering, it
is the **instruction offset**.

The payload image is mapped at a 64 KiB-aligned base, so the offset of the
faulting instruction inside the image is `0x1832100dd9c - base`, and its low
16 bits are always `0xd9c`. In the build of this tree
(`build-payload/payload.dll`, disassembled with `dumpbin /disasm`) the image
contains exactly **two** accesses to a `thread_local` variable:

```
000000018000DD89: mov  eax,dword ptr [_tls_index]      ; the image's TLS index
000000018000DD8F: mov  r8,qword ptr gs:[58h]            ; TEB->ThreadLocalStoragePointer
000000018000DD98: mov  rax,qword ptr [r8+rax*8]         ; the per-thread block pointer
000000018000DD9C: cmp  dword ptr [rax+4],0             ; <-- the read of g_camera_depth
```

```
000000018005D31D: mov  eax,dword ptr [_tls_index]      ; the prediction's svc_copy
000000018005D323: mov  rcx,qword ptr gs:[58h]
000000018005D32C: mov  rax,qword ptr [rcx+rax*8]
000000018005D330: lea  rbx,[rax+8]
```

The first is the camera detour's guard (`g_camera_depth`, the `thread_local`
the detour read **as its first action**); the second is the prediction's
`thread_local std::array<std::byte, 0xfe0> svc_copy`
(`payload/control/src/input.cpp:1268`).

Their offsets inside the image are `0xdd9c` and `0x5d330`. Only the first is
congruent to `0xd9c` modulo `0x10000` (`0x5d330 mod 0x10000 = 0xd330`), so
**the faulting instruction can only be the camera detour's `thread_local`
read**, and the payload base was `0x18321000000`.

The read is the *dereference of the per-thread block pointer*:
`rax = gs:[0x58][_tls_index]` is the block the loader would have allocated
for this thread, and the faulting address `0xFFFFFFFFFFFFFFFF` is that
pointer. (In this tree's build the variable sits at TLS offset `+4`, so the
faulting address would be `0xFFFFFFFFFFFFFFFB`; the operator's build had it
at offset `0` — the order of the variables inside `.tls` is a link-time
detail and does not move any code, so the instruction offset is the same
either way. The logged address is the block pointer itself.)

So the crash is **not** a bad address and **not** a bad write: it is the
detour reading a `thread_local` whose per-thread block was never installed.

## 3. Why the per-thread block was missing

The payload is **manually mapped**. The Windows loader never loads it, so it
never allocates the per-thread TLS blocks that a `thread_local` resolves
against, and it never runs the module's TLS callbacks.

What the payload does about it:

* the **injector** reserves a TLS slot in the target and points the image's
  `_tls_index` at it (`injector/src/main.cpp:449-483`: a remote `TlsAlloc`,
  then `write_u32(remote_base + (address_of_index - image_base), index)`);
* the **entry stub** writes the block pointer into the **entry thread's**
  slot array (`injector/src/main.cpp:585-592`:
  `mov rax, gs:[0x58]; mov [rax+rcx*8], rdx`);
* `cs2::tls::ensure_thread_tls()` installs a block **by hand** for any thread
  that calls it (`payload/tls/src/bootstrap.cpp:27`).

`ensure_thread_tls()` is called by the payload's own threads, by
`create_move_detour` (`payload/control/src/input.cpp:1400,1418`) and by the
game-present hook's render-thread callback
(`payload/features/src/game_present_hook.cpp:100`). **The camera detour never
called it** — the game's camera thread is not one of the threads the payload
prepared. (It is also not the thread the `CreateMove` detour prepares: that
thread's slot holds a block, so a `thread_local` read there works, and this
one crashed.)

Where the garbage comes from is the loader's bookkeeping: the TLS slot was
reserved by `TlsAlloc` **in the target, after the game's threads already
existed**, and a thread that was already running was never given a block for
it — the slot entry is the loader's "no block" marker rather than a pointer.
That is consistent with the observed value: `0xFFFFFFFFFFFFFFFF`, and with
the fact that the crash is a **read of that pointer**, not of a low address
(which is what a *null* slot would have produced).

And the bootstrap could not have repaired it afterwards:

```cpp
auto* tls_array = reinterpret_cast<std::uint64_t*>(__readgsqword(k_teb_tls_pointer));
if (tls_array == nullptr || tls_array[index] != 0) {
  return;  // already installed
}
```

(`payload/tls/src/bootstrap.cpp:53`) — a slot that holds **garbage** (here
`0xFFFFFFFFFFFFFFFF`) is taken for "already installed", so the bootstrap
installs nothing and the read keeps faulting. **Adding
`ensure_thread_tls()` to the camera detour would not have fixed the crash**;
the only correct fix is to have no `thread_local` on that path at all.

The rule was already in this repository: `docs/orchestration/spec-t20-animfix.md`
— *"No `thread_local` in a detour on the manually mapped image (the TLS block
is not installed on the game's threads)."* The third-person detour broke it.

## 4. The address was valid

For the record (the orchestrator's check, reproduced here so the next reader
does not have to redo it): on the live `client.dll` (sha256
`659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`, PE
timestamp `0x6AD01314` — the build the payload gates on,
`k_client_timestamp` in `payload/game/src/camera_detail.hpp:64`),
`client.dll + 0xB62A20` is a real function: `FUN_180b62a20`, 3789 bytes,
with the prologue

```
40 55                     push rbp
53                        push rbx
56                        push rsi
57                        push rdi
41 57                     push r15
48 8D AC 24 70 FE FF FF   lea rbp, [rsp - 0x190]
48 81 EC 90 02 00 00      sub rsp, 0x290
48 63 DA                  movsxd rbx, edx
48 8B F9                  mov edi, ecx
48 69 F3 28 09 00 00      imul rsi, rbx, 0x928
4C 8B FB                  mov r15, rbx
48 83 BC 0E A0 06 00 00 00 cmp qword ptr [rsi + rcx + 0x6A0], 0
```

It takes `(rcx = the camera object, edx = the slot)` and indexes
`slot * 0x928`, which is the calling shape of the payload's typedef
`std::uint64_t(__fastcall*)(void* input, int slot)`. Patching it is not the
bug. (The crash log agrees: the detour *was* reached and *did* execute.)

## 5. The old guard had a second defect

The old guard was not only in the wrong place (a `thread_local`), it was also
released too early:

```cpp
  g_camera_depth = 1;
  if (active != nullptr) { active->on_camera_update(input, slot); }
  g_camera_depth = 0;                     // <-- released here
  if (g_original != nullptr) {
    return g_original(input, slot);       // <-- the trampoline is called here
  }
```

The cycle the guard exists to break is `detour -> trampoline -> detour -> …`,
i.e. it happens **inside** `g_original`. With the depth released before that
call, the re-entrant detour saw `g_camera_depth == 0` and was **not**
refused: the guard never broke the cycle it was written for, and a wrongly
patched prologue would still have overflowed the stack (`0xc00000fd`). The
new guard is held **across** the trampoline call.

## 6. The guard that replaces it

`payload/features/include/cs2/features/recursion_guard.hpp` — a **pure**
header (no Windows headers, no engine access), so the host test can drive
the whole decision.

* The guard tracks the **owner** of a slot — an opaque token that is unique
  per thread (the OS thread id, `GetCurrentThreadId()`) — and that owner's
  **depth**. The state is one 64-bit word per slot (the owner in the high
  half, the depth in the low half), so a claim and a release are a single
  atomic compare-exchange: no lock, no allocation, **no `thread_local`**.
* A call is refused **only** when the *same* owner is already inside. That
  is the recursion (the trampoline landed back in the detour). Another
  thread's call is never refused: the camera update can run on more than one
  thread, and a shared counter would silently drop the other thread's
  camera update.
* Eight slots are tracked; if they are all taken by *other* owners the guard
  **fails open** (the call is allowed). Refusing a call that is not a
  recursion would drop a camera update — the worse failure.

**The proof that it breaks a same-thread recursion without refusing another
thread.** `try_enter(owner)` scans the slots:

* a slot with `depth > 0` and `owner == mine` → **refused** (the recursion);
* a slot with `depth > 0` and `owner != mine` → skipped (another thread's
  slot; it is not claimable and it does not refuse me);
* a slot with `depth == 0` → claimed for me with a compare-exchange, and the
  exchange is the only way two threads can take the same slot, so exactly one
  of them wins.

The owner token is unique per thread, so "the slot is mine" is exactly "this
thread is already inside" — the refusal can only ever be a same-thread
refusal. The host test drives exactly this: the first call is allowed, the
second call **on the same thread** is refused, a **second thread** is
allowed while the first is inside, and the depth returns to zero after
`leave`.

## 7. The test

`payload/features/tests/test_thirdperson_guard.cpp`, registered with Meson
as `features-thirdperson-guard` (the root `meson.build`). It covers:

1. the first call is not refused;
2. the **same** thread's second call **is** refused (the recursion), and the
   refusal does not change the depth;
3. the guard is reusable: after `leave`, the next call is allowed again;
4. **another thread** is not refused while the first thread is inside — and
   that thread's own recursion *is* refused (the guard is not a per-thread
   bypass);
5. eight threads hammering the guard: every entry succeeds, no second entry
   ever succeeds;
6. a **source check** over the repository: the guard's header, the feature's
   header and the detour's source must not contain a `thread_local` (the
   check strips the comments first, so the prose that states the rule does
   not trip it).

**It can fail** — both breakages were run and reverted:

| the break | the failure |
|---|---|
| the depth shared by every thread (the naive fix: one counter, no owner) | `Assertion failed: other_entered` — the second thread was refused |
| `thread_local int g_camera_depth = 0;` put back into the detour | `Assertion failed: strip_comments(source).find("thread_local") == npos` |

## 8. What the detour no longer does

The detour runs on the **game's** camera thread, in the client's per-frame
camera update. Before this change it did, every frame:

* `game::input_object()` — a module lookup (`GetModuleHandleA` plus a
  `std::string` allocation) and a PE-header read, only to compare the
  identity of the input object;
* `game::client_build_verified()` — the same module walk again, and
  `game::camera_engine()` — a third one, both inside
  `game::write_third_person_state`;
* the write itself.

Now the detour does:

* an atomic load of the **cached** input object and a compare (the identity
  check — unchanged in meaning);
* an atomic load of the **cached** build gate;
* the write.

The per-build facts (the input object's address and the client's PE
timestamp) are resolved by `ThirdPerson::refresh_target()` on the **payload's
own thread** (`update()`, at most once a second, and again whenever the
detour reports that the input object it saw is not the cached one — the
module changed under us). They are cached because they cannot change while
the client is up: the input object is a singleton.

**What is still on the game's thread:** `game::write_third_person_state()`
(`payload/game/src/camera_live.cpp:140`) re-resolves the module and the
engine **inside itself** — two module lookups, two PE-header reads and two
`std::string` allocations per frame, on the game's camera thread. That is
the game module's file, not this task's, and the fix belongs there:

> add `write_third_person_state(const CameraEngine& engine, …)` to
> `payload/game` (the engine-parameterised reads already exist) and let the
> feature keep a cached `game::camera_engine()`. Then the detour's path
> touches **no** module lookup and **no** allocation at all.

Until that lands, the detour path still allocates inside the game module —
the feature's own path (the guard, the identity check, the yaw) allocates
nothing and logs nothing.

The **reporting** stayed on the payload's own thread: `ThirdPerson::report()`
builds a `std::string` and calls `cs2::game::append_log`, which opens,
writes and closes the log file **per call**. It is reached from `update()`
only; the detour path never logs.

The status line now says what happened:

```
… hook=1 status=installed updates=1834 writes=1833 cmds=7
```

`status` is one of `off`, `waiting-for-client`, `unverified-build`,
`hook-refused`, `installed` — the reason the camera hook is **not**
installed, which is the question the operator actually asked. Each status is
logged **once** (the install path is retried every frame and the log opens a
file per call).

## 9. What is still open

* **The live run.** The agent runs in Windows session 0 with no GPU; the
  game needs `schtasks /run /tn CS2Launch` in the interactive session, and
  the payload has to be injected once
  (`build-injector\injector.exe cs2.exe <build-payload\payload.dll>
  --load-missing`). **Never inject twice into the same game.** What to look
  at in the log: the line `[cs2-features] third person: the camera update is
  hooked`, then the periodic status line with `status=installed` and a
  `updates=` counter that **grows** (the camera update is being seen) and a
  `writes=` counter that grows with it (the write lands). If the camera
  still does not move, the status says why (`unverified-build` means the
  client is not the build the offsets came from).
* **The chams' identical defect** — `payload/features/src/chams.cpp:122`,
  `thread_local int g_draw_depth = 0;`, the same guard in the draw detour.
  That file is **T26's** (the chams) and was not touched here. Its detour
  runs on the game's **render** thread; the render thread happens to call
  `ensure_thread_tls()` in the game-present hook (`game_present_hook.cpp:100`),
  but that is the *other* hook's callback — with the Vulkan layer's render
  path, or with the present hook uninstalled, nothing prepares the thread and
  the draw detour would fault exactly like this one. **T26 must replace it
  with the same guard** (`cs2::features::recursion_guard.hpp`).
* **The prediction's `thread_local`** — `payload/control/src/input.cpp:1268`,
  `thread_local std::array<std::byte, 0xfe0> svc_copy;`, in the prediction
  path. It runs where `ensure_thread_tls()` has been called, and the read
  goes through the fault-safe reader, so a garbage block fails *cleanly*
  (the prediction is skipped) instead of faulting — but a thread whose slot
  holds garbage can never get a block, so the prediction may be silently
  inactive on the game's main thread. Worth a look by the owner of that file.
* **The game module's per-frame work** — see §8.

## 10. The files

| File | What |
|---|---|
| `payload/features/include/cs2/features/recursion_guard.hpp` | the pure guard (new) |
| `payload/features/include/cs2/features/thirdperson.hpp` | the status, the cached facts, the guard member |
| `payload/features/src/thirdperson.cpp` | the detour, the guard, the cached target, the status |
| `payload/features/tests/test_thirdperson_guard.cpp` | the host test (new) |
| `meson.build` | the test registration |
| `docs/re/thirdperson.md` | this document (new) |
