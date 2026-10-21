# The third-person camera: the crash that killed the game

> **The one-line version:** the camera hook's address was **valid**; the crash
> was the **`thread_local` re-entrancy guard** in the detour. The payload is
> manually mapped, the game's camera thread has no TLS block for it, and the
> detour's very first instruction dereferenced that block pointer — so the
> read faulted *inside the payload* and the game died.

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
is the **instruction**.

### 2.1 The instruction that faults

A `thread_local` access on x64 is four instructions:

```
mov  eax, dword ptr [_tls_index]      ; the image's TLS index
mov  r8,  qword ptr gs:[58h]          ; TEB->ThreadLocalStoragePointer
mov  rax, qword ptr [r8+rax*8]         ; the per-thread block pointer
cmp  dword ptr [rax+offset],0         ; the variable inside the block
```

The **third** one reads the slot array (the TEB's own memory — always
mapped), so it cannot fault. The **fourth** one, which reads the *variable
inside* the block, dereferences the block pointer — and the block pointer is
garbage on a thread that never had a block.

So the question is: **which instruction in the image dereferences a TLS block
pointer?** In this tree's pre-fix build (`eca3605`, rebuilt from scratch for
this document; sha256
`a121da1aaf0667eb19de0ee73dfa4ebafa9bf31e0b33906750ceacad5befa612`) there are
exactly **three** `gs:0x58` reads:

```
18000dc09:  mov  eax,dword ptr [rip+0x1e8fa9]   ; _tls_index
18000dc0f:  mov  r8,qword ptr gs:[58h]            ; the slot array
18000dc18:  mov  rax,qword ptr [r8+rax*8]         ; the block pointer
18000dc1c:  cmp  dword ptr [rax+4],0              ; <-- the read of g_camera_depth

180034152:  mov  r12,qword ptr gs:[58h]           ; the TLS bootstrap
180034160:  cmp  qword ptr [r12+r15*8],0          ; the SLOT ARRAY, not a block

18005d183:  mov  rcx,qword ptr gs:[58h]           ; the prediction's svc_copy
18005d18c:  mov  rax,qword ptr [rcx+rax*8]        ; the slot array again
18005d190:  lea  rbx,[rax+8]                      ; the block pointer is only *passed on*
```

Only the **first** sequence dereferences the block: the bootstrap compares
the *slot* against zero, and the prediction hands the block pointer to the
fault-safe reader (`read_bytes` → `ReadProcessMemory`,
`payload/game/src/memory.cpp:19`), which **fails** on an invalid address
instead of faulting. The camera detour's guard is the one place in the image
where a garbage TLS block pointer is dereferenced directly — and it was the
detour's **very first action**.

### 2.2 The offset, and what it does and does not prove

The payload image is mapped at a 64 KiB-aligned base, so the faulting
instruction's offset inside the image is known only **modulo `0x10000`**:
`0x1832100dd9c - base ≡ 0xd9c (mod 0x10000)`.

That residue matches the camera detour's TLS read **in the layout the
operator was running**. The image with that layout on this machine is the
t28 one (`cs2-build/rev-t28-offline-offsets/build-payload/payload.dll`, sha256
`32a72a120cd7f28f15f4c3cb06ddd8835347ee9b54c6e6b579f87c08443a484b`):

```
18000dd89:  mov  eax,dword ptr [rip+0x241e39]   ; _tls_index
18000dd8f:  mov  r8,qword ptr gs:[58h]
18000dd98:  mov  rax,qword ptr [r8+rax*8]
18000dd9c:  cmp  dword ptr [rax+4],0              ; <-- the faulting instruction
```

With that layout the payload's base was `0x18321000000`.

**But the offset is not the proof, and this document does not present it as
one.** The camera detour's TLS access moves with the build: across the
payload images on this machine the `movq %gs:0x58` of the camera detour sits
at `0x18000dc0f`, `0x18000dc6f`, `0x18000dd6f`, `0x18000dd8f`,
`0x18000ddef`, `0x18000e89f`, `0x18000e98f`, `0x18000f63f`, `0x18002e81f`
and `0x18002e9af` — and the faulting instruction is thirteen bytes further
on, so its low sixteen bits are `0xdc1c`, `0xdc7c`, `0xdd7c`, `0xdd9c`,
`0xddfc`, `0xe8ac`, `0xe99c`, `0xf64c`, `0x2e82c` and `0x2e9bc`. The residue
`0xd9c` therefore identifies **which build** the operator was running, not
**what** faulted; and this tree's own pre-fix build puts the same
instruction at `0x18000dc1c` (§2.1), not at `0x18000dd9c`.

The conclusion rests on the **mechanism** and the **timing**, both of which
hold for every build:

* the faulting instruction is a **read** of a garbage pointer
  (`0xffffffffffffffff`), not a write to a bad address;
* the payload is **manually mapped**, so the game's threads have no TLS
  block (§3) — and the *only* instruction in the image that dereferences a
  block pointer is the camera detour's guard;
* the crash came **immediately after** the hook was installed, i.e. on the
  first camera update through the detour, whose first action was the guard's
  read;
* the address `0x1832100dd9c` is inside the payload, so the game's own code
  did not fault.

### 2.3 The fix, in the image

The tree that carries the fix (`build-payload/payload.dll`, built on
2026-10-21, sha256
`34a416290fd1ff06bb5ceffd4c94c56d4fa95b65253ffaa9ffa6c11c5b203b0f` — the
PE header carries a build timestamp, so a rebuild hashes differently; what
matters is the content) has **two** `gs:0x58` reads left, and neither is in
the camera detour:

```
180034ff2:  mov  r12,qword ptr gs:[58h]   ; the TLS bootstrap
18005e043:  mov  rcx,qword ptr gs:[58h]   ; the prediction's svc_copy
```

(The image also has three `gs:`-prefixed reads that are **not** TLS variable
accesses: `gs:0x30` twice and `gs:0x10` once — the unwinder's stack walk
and the exception registration. They are in every build, before and after.)

### 2.4 One detail about the `.tls` layout

The variable sits at TLS offset `+4` in this tree (`cmp dword ptr
[rax+4],0`), so the faulting address would be the block pointer `+4`; the
operator's build logged the block pointer itself, which is what a variable at
offset `0` would fault on. The order of the variables inside `.tls` is a
link-time detail, and it does not move any code **here** because the
instruction is `83 b8 04 00 00 00 00` — a `disp32`, whose length does not
depend on the value. (That is the reason the argument survives a relayout;
it is not true of every encoding.)

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
prepared.

### 3.1 The bootstrap could not repair it either

```cpp
const uint32_t index = *reinterpret_cast<const uint32_t*>(tls->AddressOfIndex);
if (index == 0xFFFFFFFFU || index >= k_inline_tls_slots) {
  return;                                    // <-- payload/tls/src/bootstrap.cpp:49
}
```

(`payload/tls/src/bootstrap.cpp:49-51`; `k_inline_tls_slots` is 64 — the
number of TLS slots the TEB holds inline.)

The manually mapped image was given a TLS slot by the injector's
`TlsAlloc` — the game was already running, so the loader's own slots were
taken. `docs/re/camera-split.md` measured the slot it got: **154**. That is
above the 64 inline slots, so:

* `ensure_thread_tls()` **returns at line 49** — it never installs a block,
  for any thread, for the life of the process. It is not the "already
  installed" check at line 53 that stops it; the index check stops it
  earlier;
* the read at `gs:[0x58][154]` is `154 * 8 = 1232` bytes past the start of
  the TEB's 64-entry inline array — **uninitialised TEB memory**.

### 3.2 The two accounts, reconciled

This repository holds two write-ups of "the crash that killed the game", and
they look like two different crashes. They are the **same mechanism, seen
twice**:

| | `docs/re/camera-split.md` §"Crash found on the way" | this document |
|---|---|---|
| where | the T18b branch (`feat/t18b-neverlose-camera-split`) | the operator's build (the t28 layout) |
| the faulting address | `0x4` | `0xffffffffffffffff` |
| the account | "the image got TLS slot **154**, above the 64 slots `ensure_thread_tls` supports, so the camera thread's TLS block was null and the guard read `[null+4]`" | the slot held garbage and the guard dereferenced it |

Both are the same thing: **the block was never installed** (§3.1), so the
slot the guard read held whatever was in that uninitialised TEB memory. In
the T18b build it happened to be zero — a null block — and the guard read
`[null + 4]`, i.e. `0x4`. In the operator's build the same slot held
`0xffffffffffffffff`, and the guard read `[0xffffffffffffffff + 0]`, i.e.
the logged address. The *value* is an accident of the build and the run;
the *cause* — a TLS block that was never installed, dereferenced by a
detour on a game's thread — is not.

So: **two events, on two branches, one mechanism.** This document is the
account for the operator's build; `docs/re/camera-split.md` keeps the one
for the T18b branch.

And the conclusion is unchanged either way: **adding `ensure_thread_tls()` to
the camera detour would not have fixed the crash** — the bootstrap refuses
the index outright — so the only correct fix is to have **no `thread_local`**
on that path at all.

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
  **depth** (one while the owner is inside). The state is one 64-bit word per
  slot (the owner in the high half, the depth in the low half), so a claim
  and a release are a single atomic compare-exchange: no lock, no
  allocation, **no `thread_local`**.
* A call is refused **only** when the *same* owner is already inside. That
  is the recursion (the trampoline landed back in the detour). Another
  thread's call is never refused: the camera update can run on more than one
  thread, and a shared counter would silently drop the other thread's
  camera update.

### 6.1 The decision, in two passes

`try_enter(owner)` asks two questions **in this order**:

1. **am I already inside?** — *every* slot is checked, so the answer does not
   depend on which slot this owner happens to hold;
2. **is there a free slot?** — the first free one is claimed with a
   compare-exchange, so exactly one of two racing threads wins it.

The order is load-bearing. A single pass that claims the first free slot and
only *then* asks the question gets it wrong: the owner of slot 1 is let back
in as soon as slot 0 has been freed, and the recursion it exists to break
runs until the stack overflows. The two passes together also mean a thread
holds **at most one** slot — so `leave` has exactly one slot to release. (A
thread that claimed two would leak one of them, and the leftover would
refuse that thread's **legitimate** calls forever: in the detour, a refused
entry drops the frame's camera update.)

`leave` releases the slot with a **strong** compare-exchange and re-scans
rather than abandoning the slot, so a failure can never leak it.

### 6.2 The pool, and the cost of failing open

Eight slots for the camera update (which runs on one or two threads) and one
**reserve** slot, which is only reached when all eight are taken — so that
owner is tracked too, and its recursion is broken as well. When **every**
slot is held by *other* owners the guard **fails open**: the call is allowed,
and it is not tracked.

The cost of that window, stated plainly: **a fail-open entry has no
recursion protection at all.** The mis-patch scenario the guard exists for
is exactly the scenario in which the pool is most likely to be full, so the
protection is weakest precisely where it is needed. What the two-pass
decision buys is that the pool being full no longer means "this thread's
recursion gets through": a thread that **is** inside is refused whatever the
pool looks like, because the first pass checks every slot. Only a thread
that is *not* inside can be let through untracked — and for that thread the
guard's job (not dropping a legitimate camera update) is done.

The pool is nine slots deep and the camera update runs on one or two
threads, so the window needs nine concurrent owners to be reached at all.

### 6.3 The limits

A slot is leaked when a thread dies **inside** the guarded region (the
detour's `leave` never runs). The leaked slot is not reclaimed: the guard is
pure and has no way to ask whether a thread is still alive. The consequence
is bounded — a thread that reuses a dead thread's id would be refused, so
its camera update would be dropped (not a crash), and the pool would be one
slot shorter.

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
5. **the regression**: the owner of a *higher* slot is refused too, even when
   a lower slot has been freed in the meantime (the scan-order hole);
6. **the double claim** is gone: a thread owns at most one slot, one `leave`
   releases it, and the thread's next legitimate call is allowed (the leaked
   slot that refused it forever);
7. **every slot** — the eight plus the reserve — can be held at once, and
   every one of those owners has its recursion broken;
8. the **fail-open** window: one owner more than the pool has slots is let
   in (not refused) and is not tracked — the documented cost, pinned down so
   it cannot change silently;
9. eight threads hammering the guard: every entry succeeds, no second entry
   ever succeeds;
10. a **source check** over the repository: the guard's header, the feature's
    header, the detour's source **and the files the detour's path includes**
    (`payload/game/src/camera_live.cpp`, `camera.cpp`, `camera_detail.hpp`,
    `module.cpp`, `memory.cpp`) must not contain a `thread_local` (the check
    strips the comments first, so the prose that states the rule does not
    trip it).

**It can fail** — each break was applied to a scratch copy of the tree, the
test was run against it, and the copy was discarded:

| the break | the failure |
|---|---|
| the single-pass scan order (the guard this task replaced) | `Assertion failed: !guard.try_enter(higher)` — the recursion was let through |
| one shared counter, no owner (the naive fix) | `Assertion failed: other_entered` — the second thread was refused |
| `leave` that never releases the slot | `Assertion failed: guard.depth(owner) == 0` — the slot leaked |
| `thread_local int g_camera_depth = 0;` put back into the detour | `Assertion failed: strip_comments(source).find("thread_local") == npos` |
| a `thread_local` put into `payload/game/src/camera_live.cpp` | the same assertion — the check covers the detour's whole path |

**The eight-thread case is not flaky.** It was run **3000 times** against
this guard (the reviewer's harness, `proto_test_shipped.cpp`, which drives
the shipped case in a loop): **0 failures**. The review measured the same
harness against the previous, single-pass guard: **49 failures in 3000** — a
thread's legitimate entry was refused, or a recursion got through. The whole
test binary was run 500 times on this tree: **0 failures**.

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
* the write, through an engine built from the cached facts.

The per-build facts (the client's base, the input object's address and the
client's PE timestamp) are resolved by `ThirdPerson::refresh_target()` in
**one** module lookup, on the **render thread**, at most **once a second**,
and again whenever the detour reports that the input object it saw is not
the cached one (the module changed under us). They are cached because they
cannot change while the client is up: the input object is a singleton.

Two things about the *thread* and the *rate*, which the first version of this
document got wrong:

* `update()` — and therefore `refresh_target()` — runs on the **render
  thread**: the overlay's per-frame callback (`on_snapshot` ←
  `PresentHook::present_callback` ← the game's D3D11 `Present`), which in the
  default in-process mode is the **game's** render thread, not "the payload's
  own thread". The camera detour's thread is a different one (the client's
  camera update), and nothing in the detour's path touches the module table.
* the rate limit is **hard**. An earlier version also refreshed whenever the
  target could not be resolved, which made the lookup run on **every** frame
  for as long as the client was not up — the one window in which the camera
  update does not run at all, and therefore the window in which the cost
  mattered least and the work was largest.

### 8.1 What the game module had to change

`game::write_third_person_state()` re-resolved the module and the engine
**inside itself** — two module lookups, two PE-header reads and two
`std::string` allocations per frame, on the game's camera thread. The write
now takes the engine as a **parameter**, like the parameterised reads above
it, and the feature hands it the engine it built from its cached facts:

```cpp
// payload/game/include/cs2/game/camera.hpp
[[nodiscard]] auto write_third_person_state(const CameraEngine& engine,
                                            const ThirdPersonParams& params, float pitch,
                                            float yaw) noexcept -> bool;

// payload/game/src/camera_live.cpp
[[nodiscard]] auto live_camera_engine(std::uintptr_t client_base, bool build_verified,
                                      std::uintptr_t input_object) noexcept -> CameraEngine;
```

`live_camera_engine` binds the live (fault-safe) reader to the caller's
facts, so building the engine per frame is a struct copy: no module lookup,
no allocation. The detour's path now touches **no** module lookup and
allocates **nothing**.

**What is still on the render thread:** the *reporting* path
(`ThirdPerson::report()`) resolves the engine again for each of its reads —
`read_published_view()`, `read_view_setup_fov()`, `read_third_person_state()`
and `read_ideal_distance()` are four separate module lookups — but only
**twice a second**, and on the render thread, not on the game's camera
thread. It is the natural next cleanup, and it is not the detour's path.

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
  That file is **T26's** (the chams) and was not touched here. The defect
  is currently **latent**: in this tree's image the draw detour is not
  linked at all (`k_draw_hook_target_verified == false` makes `install()`
  return before the detour's address is taken, and `/OPT:REF` drops the
  function and its `thread_local` with it) — the image has only the two
  `gs:0x58` reads of §2.3. The defect comes back the moment T26 re-enables
  the hook. Its detour runs on the game's **render** thread; the render
  thread happens to call `ensure_thread_tls()` in the game-present hook
  (`game_present_hook.cpp:100`), but that is the *other* hook's callback —
  with the Vulkan layer's render path, or with the present hook
  uninstalled, nothing prepares the thread. **T26 must replace it with the
  same guard** (`cs2::features::recursion_guard.hpp`).
* **The prediction's `thread_local`** — `payload/control/src/input.cpp:1268`,
  `thread_local std::array<std::byte, 0xfe0> svc_copy;`, in the prediction
  path. It is **inactive**, not "possibly inactive": the image's TLS index
  is 154, and `ensure_thread_tls()` refuses any index at or above the 64
  inline slots (§3.1), so the block is **never** installed for any thread —
  the prediction's read of `svc_copy` always fails (cleanly: the fault-safe
  reader fails instead of faulting), and the prediction never runs. That
  file is T30's, not this task's.
* **The report path's module lookups** — see §8.1.

## 10. The files

| File | What |
|---|---|
| `payload/features/include/cs2/features/recursion_guard.hpp` | the pure guard (new) |
| `payload/features/include/cs2/features/thirdperson.hpp` | the status, the cached facts, the guard member |
| `payload/features/src/thirdperson.cpp` | the detour, the guard, the cached target, the status |
| `payload/features/tests/test_thirdperson_guard.cpp` | the host test (new) |
| `payload/game/src/camera_live.cpp` | the engine-parameterised write, `live_camera_engine` |
| `payload/game/include/cs2/game/camera.hpp` | the two declarations above |
| `meson.build` | the test registration |
| `docs/re/thirdperson.md` | this document (new) |
