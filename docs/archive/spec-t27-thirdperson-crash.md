# T27 — the third-person camera: the crash that killed the game

> **The one-line version:** the operator turned the third-person camera on and
> the game died with `0xc0000005`, reading `0xFFFFFFFFFFFFFFFF` inside the
> payload. The handoff blamed a stale hook address; **that is wrong** — the
> camera update's address is valid on the live build. The real cause is the
> **`thread_local` re-entrancy guard in the detour**: the payload is manually
> mapped, and the per-thread TLS state a `thread_local` needs is not
> initialized on the game's threads. This task removes that hazard, keeps the
> camera working, and records what was actually wrong.

---

## 1. The evidence

The payload's log, one line before the crash:

```
[cs2-features] third person: the camera update is hooked
[crash] first-chance code=0xc0000005 at 0x1832100dd9c read addr=0xffffffffffffffff
```

* `0x1832100dd9c` is **inside the payload** (the manually mapped image), not
  inside the game — the faulting instruction is the payload's own code.
* the access is a **read** of `0xFFFFFFFFFFFFFFFF`.
* the crash came **immediately after** the hook was installed: the first
  camera update through the detour faulted.

The feature is documented in `docs/handoff-wave2.md` §5.1b.

## 2. What the RE says now (the handoff's hypothesis was wrong)

`docs/handoff-wave2.md` §5.1b says:

> "the same class of problem as the chams' hook target (§5.2): **the address
> was taken from a build that no longer matches**."

**That is not what the live build says.** I checked the live module with
REA/Ghidra:

* `F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`
* sha256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`
* PE timestamp `0x6AD01314` — **the same** timestamp the payload gates on
  (`k_client_timestamp` in `payload/game/src/camera_detail.hpp`), so the
  build gate passes and the hook is installed.
* `client.dll + 0xB62A20` is a **real function** in that image:
  `FUN_180b62a20`, 3789 bytes (`0x180B62A20` … `0x180B638EC`), with the
  prologue

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
  `slot * 0x928`. The payload's typedef
  `std::uint64_t(__fastcall*)(void* input, int slot)`
  (`payload/features/src/thirdperson.cpp:23`) matches the calling shape.

**So the address is right.** Patching it is not the bug.

## 3. The real cause: a `thread_local` in a detour

`payload/features/src/thirdperson.cpp:39`:

```cpp
thread_local int g_camera_depth = 0;

std::uint64_t __fastcall camera_update_detour(void* input, int slot) {
  if (g_camera_depth > 0) {   // <-- the FIRST thing the detour does
    return 0;
  }
  ...
```

The payload is a **manually mapped** image. `payload/tls/include/cs2/tls/bootstrap.hpp`
says it plainly:

> "The Windows loader allocates a per-thread TLS block and runs TLS callbacks …
> A manually-mapped DLL is bypassed by that machinery, so **only the injector's
> entry thread gets a TLS block** (set by the entry stub). Any *new* thread the
> payload creates would run with an uninitialized per-thread CRT block, which
> **crashes intermittently**."

`cs2::tls::ensure_thread_tls()` exists to install that block on a thread —
and it is called by the payload's own threads and by `create_move_detour`
(`payload/control/src/input.cpp`). **The game's own camera thread is not one
of them.** A `thread_local` access on that thread therefore resolves against
an uninitialized per-thread block, and the read faults — **inside the
payload**, at an address the payload never meant to touch.

That is exactly the logged shape: the faulting instruction is the detour's
first one, the access is a **read**, and the address is garbage
(`0xFFFFFFFFFFFFFFFF`).

**The rule is already in this repository.** `docs/orchestration/spec-t20-animfix.md`:

> "**No `thread_local` in a detour** on the manually mapped image (the TLS
> block is not installed on the game's threads)."

`payload/features/src/chams.cpp:122` has the same defect
(`thread_local int g_draw_depth = 0;`) — that is **another task's** file (the
chams are T26); do not edit it, but **report it**.

## 4. What to build

### 4.1 The guard, without `thread_local`

The detour's guard exists to break one cycle: if the trampoline is patched
wrongly, the chain *detour → original → detour → …* recurses until the stack
overflows, and the guard breaks it at depth one.

Replace the `thread_local` with a guard that is correct on a manually mapped
image. The requirements:

1. **No `thread_local`** anywhere in the detour or in the code it calls.
2. The guard breaks a **same-thread** recursion.
3. The guard must **not** refuse another thread's call: the camera update can
   run on more than one thread, and a shared counter that makes one thread's
   call return early would silently drop that thread's camera update.
   A per-owner design (the thread id that owns the depth, plus an atomic
   depth) satisfies both.
4. It must be **lock-free** and must not allocate: the detour is in the
   game's per-frame path.

Put the decision logic in a **pure header** (no Windows headers, no engine
access) so it can be host-tested, and keep the detour a thin wrapper around
it. `payload/features/include/cs2/features/thirdperson.hpp` and a new
pure header are yours.

### 4.2 The detour must not do the payload's work

Today `camera_update_detour` → `on_camera_update` does, **on the game's
camera thread, every frame**:

* `game::input_object()` → `find_module("client.dll")` (a **Toolhelp module
  walk**) plus a PE-header read, to compare the identity;
* `client_build_verified()` → the same module walk again, inside
  `write_third_person_state`;
* the write itself.

Three module walks per frame on the game's thread is work the game does not
need. Resolve the input object **once** and cache it (it is a singleton; the
payload already caches other per-build facts), and re-resolve only when the
resolution fails or the module changes. The identity check and the build gate
stay — they are what keeps the write off the wrong object.

### 4.3 The report must not run on the game's thread

`ThirdPerson::report()` builds a `std::string` and calls
`cs2::game::append_log` — which **opens, writes and closes a file per call**
(`payload/game/src/log.cpp`). It is called from `update()` (the payload's own
thread, ~30 Hz) — **keep it there**. Verify that nothing in the detour path
(`on_camera_update` → the write) does file I/O or allocates; if the reporting
is reachable from the detour, move it.

### 4.4 The status must say what happened

The feature's status/overlay line must show:

* whether the camera hook is **installed**;
* how many camera updates were seen, and how many **writes** landed;
* the reason when the hook is **not** installed (the build is not the
  verified one / the hook refused).

`writes_` and `commands_` exist; add what is missing and keep the names
stable.

### 4.5 The log must not spam

Same rule as everywhere else in this repository: `append_log` opens and
closes a file **per call**. The install path can be retried every frame —
log a failure **once** (latch it), not once per attempt.

## 5. What is out of scope

* **`payload/features/src/chams.cpp`** — the chams are T26's task, and they
  carry the same `thread_local` defect. Do not edit that file; report it.
* The material system, the ESP, the ragebot, the animfix, the anti-aim.
* The **protection** workstream (`docs/handoff-protect.md`) — it is out of
  scope for this repository now.

## 6. File ownership

You own:

* `payload/features/src/thirdperson.cpp` and
  `payload/features/include/cs2/features/thirdperson.hpp`
* the new pure header for the guard (your own file, your own name)
* `payload/features/tests/**` (the new host test) and
  `payload/features/meson.build` + the root `meson.build` (registering it)
* `docs/re/thirdperson.md` (**new**) — the camera update's address on the
  live build, the crash analysis, and what was actually wrong. Keep it
  factual: the address was **valid**; the crash was the `thread_local`.

You do **not** own `wire::Settings`, the control channel's protocol, the
chams, the ESP or the ragebot. If you need something outside this list, say
so in your report instead of editing it.

## 7. The test that must be able to fail

The guard is the whole point, so it needs a host test that **can fail**:

* enter → the call is **not** refused;
* re-enter on the **same** thread → the call **is** refused (that is the
  recursion the guard exists for);
* a **different** thread → its call is **not** refused (this is the case a
  shared counter gets wrong);
* leave → the next call is not refused again;
* and a check that the guard's code path contains **no `thread_local`** —
  the repo's host tests may read the repository's own sources; a check that
  fails when the word reappears is worth more than a comment.

**Prove it can fail**: break the rule in a scratch copy, watch the test fail,
revert. Say so in the report.

The test must be registered with Meson so it actually runs.

## 8. The rules

- `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010) — an
  instance member is not global state.
- The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no `thread_local` in a detour.
- Every engine read goes through the fault-safe `game::read` helpers.
- Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

## 9. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t27-thirdperson-crash\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\t27-thirdperson-crash\build
meson test   -C C:\Users\gpmpo\cs2-build\t27-thirdperson-crash\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t27-thirdperson-crash\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t27-thirdperson-crash\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t27-thirdperson-crash\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

**You cannot launch CS2 from here** — the agent runs in Windows session 0 with
no GPU; the game needs `schtasks /run /tn CS2Launch` (the interactive
session) and then
`build-injector\injector.exe cs2.exe <your build-payload\payload.dll>
--load-missing`. **Never inject twice into the same game.** If you cannot
run it, say plainly what is unverified — and say what the operator should
look at in the log to confirm the fix.

## 10. What the report must state

1. **The root cause** — why the detour faulted, with the evidence (the
   `thread_local`, the manual map, the TLS bootstrap's own words). If your
   analysis says something else, say so with the evidence.
2. **The guard** — the design, and the proof that it breaks a same-thread
   recursion **without** refusing another thread.
3. **The test** — the hostile test and the proof that it fails without the
   fix.
4. **The per-frame work** — what the detour no longer does, and what it does
   now.
5. **What is still open** — the live run, and the chams' identical defect
   (T26's file, not yours).
6. The file list and the commit hashes.
