# T26 — the chams' draw hook: the target, re-derived

> **The one-line version:** the chams are refused because the draw hook's target
> was taken from an MSVC RTTI **lambda** type. The real
> `CAnimatableSceneObjectDesc::DrawPrimitive` has been **re-derived from the
> class's own RTTI and vtable on the live build**, and its address and
> signature are pinned below. This task resolves that address the way
> ADR-0012 requires (a dumper signature), verifies it before it patches
> anything, and turns the chams back on.

---

## 1. What is wrong today

`payload/features/src/chams.cpp` refuses to install the draw hook behind
`k_draw_hook_target_verified = false` (line 149). The reason, in the file's own
words: the address came from the RTTI **lambda** type

```
.?AV<lambda_1>@?1??DrawPrimitive@CAnimatableSceneObjectDesc@@QEAAXPEAVIRenderContext@@PEAUCMeshDrawPrimitive_t@@PEBVIMaterial2@@PEAVIMaterialMode@@AEAUDrawValues_t@@_NAEAUSCeneSystemPerFrameStats_t@@PEAVISceneLayer@@@Z@
```

A lambda's `operator()` is **static** — it takes the eight arguments and has
**no `this`** — while the member function it mirrors takes the `this` plus the
same eight. The detour in `chams.cpp` assumes the member function's shape, so
patching the lambda's `operator()` shifts the whole argument list by one and
the callee dereferences the material pointer as a mesh pointer. That is the
access violation (`0xc0000005`) that killed the game.

The current guard is also **not a verification**: `scene.contains(target)`
(line 483) only asks whether the address is inside the module. A stale RVA
satisfies it.

---

## 2. What the RE established (this is the input; do not re-derive it)

Everything below was read out of the **live** module on this machine:

* `F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\scenesystem.dll`
* sha256 `040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5`
* PE timestamp `0x6ACFCD4F` (1792003407), image base `0x180000000`

(That is the same build the feature-wave note `docs/feature-wave.md` §3.2 used.
It is **not** the build the notes in `docs/re/` were derived from — but the
scene system's RE notes were, and every address below was re-read on the live
file.)

### 2.1 The class and its vtable

| what | address (VA) | RVA |
|---|---|---|
| the type descriptor's name string `.?AVCAnimatableSceneObjectDesc@@` | `0x18067C458` | `0x67C458` |
| the **type descriptor** (the name is at +0x10) | `0x18067C448` | `0x67C448` |
| the **complete object locator** | `0x180606DC0` | `0x606DC0` |
| the class hierarchy descriptor | `0x180606CF0` | `0x606CF0` |
| the pointer to the COL (the vtable's `[-1]` slot) — the **only** one in the module | `0x1805E5C88` | `0x5E5C88` |
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

**Slot 1 is not `DrawPrimitive`.** `FUN_180059FC0` is the *batch* entry point:
it takes a primitive, a **count**, and loops, calling the real draw once per
primitive. It has **no static callers** — it is reached only through the
vtable. Its own arguments are
`(this, IRenderContext*, CMeshDrawPrimitive_t*, int count, const IMaterial2*, const IMaterialMode*, CSceneSystemPerFrameStats_t*)`
— the material is the **fifth** argument there, *not* the fourth.

### 2.2 The real `DrawPrimitive`

`FUN_180059FC0` calls the real draw at `0x18005A465`, and the callee is:

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

**This is exactly the signature the payload already has.** The typedef in
`payload/features/src/chams.cpp:34-37` is

```c
using DrawPrimitiveFn = void(__fastcall*)(void* desc, void* render_context, void* mesh_primitive,
                                          const void* material, const void* material_mode,
                                          void* draw_values, bool deferred, void* stats,
                                          void* layer);
```

— nine arguments, the material at **index 3** counting `this`. **The payload's
detour was never wrong; only its target address was.** The detour and the
typedef stay as they are.

### 2.3 The prologue

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
  bytes.
* The **20-byte** prologue
  `4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57` occurs
  **exactly once** in `scenesystem.dll` (verified by scanning the live file;
  the 12-, 14-, 16- and 20-byte prefixes are each unique as well).

### 2.4 The lambda, for the record

The lambda's type descriptor is at VA `0x180678360` (its name string at
`0x180678370`), and it is the *lambda* type — not the member function. The
lambda's `operator()` is a **separate static function**; hooking it is what
broke the game. Nothing in this task hooks a lambda again.

---

## 3. What to build

### 3.1 The signature (ADR-0012)

`external/dumper/src/analysis.rs` — `known_signatures()` gains:

```rust
// `CAnimatableSceneObjectDesc::DrawPrimitive` — the scene system's draw,
// the hook target of the chams. Resolved as a function entry (the match
// start). The 20-byte prologue is unique in `scenesystem.dll` on the
// build the RE measured (sha256 040eb851…, PE timestamp 0x6ACFCD4F);
// `docs/re/chams.md` §7 records the measurement.
sig(
    "scenesystem.dll",
    "scene_draw_primitive",
    "4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57",
    None,
    None,
),
```

`scripts/calibrate-live.py` — the curated-offset loop (line 152) resolves the
offsets for `("client.dll", "engine2.dll")`; add `"scenesystem.dll"`, so the
live calibration writes the new offset into `offsets.json` and the generated
SDK carries it.

**Why the signature and not a constant:** ADR-0012 forbids a hard-coded RVA,
and a *hard-coded* RVA is exactly what killed the game twice (the chams and
the third-person camera). The signature is re-resolved on every build.

### 3.2 The payload's resolution and its verification

In `payload/features/src/chams.cpp`:

1. Resolve the target: `game::find_offset("scene_draw_primitive")` (the same
   helper the animfix uses — `payload/control/src/input.cpp`) and
   `game::find_module("scenesystem.dll")`. The target is
   `module.base + rva`.
2. **Verify before patching** (this is the part that is missing today):
   * the offset must exist (a missing offset means the calibration dump is
     stale — say so, once, loudly, and refuse);
   * the target must be inside `scenesystem.dll` (`module.contains(target)`);
   * **the first 20 bytes at the target must equal the expected prologue**
     (`4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57`). If
     they do not, the module is not the build the RE measured → refuse and
     log.
3. Only then install the hook, and only while the feature is **on** (the
   existing "a feature that is off must not patch the game's code" rule
   stands).
4. `k_draw_hook_target_verified` goes away, or becomes the *runtime* check
   above. **Do not** simply flip the constant to `true` and delete the
   refusal: the constant is the thing that made the crash possible, and the
   runtime check is what replaces it.

The prologue check needs the bytes. The payload already has a fault-safe
reader (`game::read` / `game::read_bytes`) — use it; **do not** dereference
the target directly.

### 3.3 The log must not spam

`game::append_log` opens, writes and closes a file **per call**. The
animfix's review found the same bug there (it wrote a line on *every* tick
when the calibration dump was stale — roughly 500 MB a day). The chams'
install path runs on the render thread and can be retried every frame, so:

* log the "the calibration dump has no `scene_draw_primitive` signature" and
  the "the prologue does not match" messages **once** (latch them), not once
  per attempt;
* make the status/overlay report the reason the chams are off.

### 3.4 The evidence

The payload's status block must say, when the chams are on:

* whether the draw hook is **installed**;
* how many draws were seen, how many were **re-drawn with the chams' material**
  and how many were skipped;
* the reason when the hook is **not** installed (no offset / no module /
  prologue mismatch / hook refused).

The overlay's chams line shows the same thing.

---

## 4. What is out of scope

* **The third-person camera's crash** (`docs/handoff-wave2.md` §5.1b) is a
  separate task with the same class of bug (a stale target). Do not touch
  `payload/features/src/thirdperson.cpp`. If you build a reusable
  "verify the prologue before patching" helper, put it where the third-person
  task can use it and **say so in your report** — but do not change the
  camera's code.
* The material system, the colour setter, the material's parameter names and
  the restore path are all **done** (`docs/re/chams.md` §1–§5). Do not change
  them.
* The **anim-state divergence**, the neverlose work and the protection
  workstream are not in this task.

---

## 5. File ownership

You own:

* `external/dumper/src/analysis.rs` (the new signature)
* `scripts/calibrate-live.py` (the module list)
* `payload/features/src/chams.cpp` and
  `payload/features/include/cs2/features/chams.hpp` (the resolution, the
  verification, the counters, the latched log)
* `payload/features/tests/**` (the new host test — see below)
* `payload/features/meson.build` and the root `meson.build` (registering the
  test)
* `docs/re/chams.md` — **append only**: a new section recording the
  re-derivation (the RTTI chain, the vftable, the argument identification,
  the signature's uniqueness and the measurement's build). **Do not rewrite
  the existing sections.**

You do **not** own `wire::Settings`, the control channel's protocol, the
ESP, the ragebot, the anti-aim, the animfix or the third-person camera. If
you need something outside this list, say so in your report instead of
editing it.

---

## 6. The test that must be able to fail

The resolution and the verification are the whole point, so they need a host
test that **can fail**:

* put the decision in a **pure** piece of logic (which prologue bytes are
  acceptable, whether the target is inside the module, whether the offset is
  present) that takes the bytes as an argument — no Windows headers, no
  engine access — and test it with:
  * the **right** prologue → accepted;
  * a **one-byte-changed** prologue → refused;
  * a **truncated** prologue → refused;
  * an **empty** read (the target is unreadable) → refused;
  * an offset of 0 / a missing offset → refused.
* **Prove it can fail**: break the rule in a scratch copy, watch the test
  fail, revert. Say so in the report.

The test must be registered with Meson so it actually runs, and it must be
**green** with the correct prologue — a test that only ever refuses is not a
test.

---

## 7. The rules

- `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010).
- The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  `thread_local` in a detour (the TLS block is not installed on the game's
  threads — `docs/re/camera-split.md`).
- Every engine read goes through the fault-safe `game::read` helpers.
- Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 8. The verify loop

```powershell
# the project's environment FIRST - it keeps the cargo target directory and
# the tool caches out of the worktree
. C:\Users\gpmpo\cs2-wt\t26-chams-draw-hook\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\t26-chams-draw-hook\build
meson test   -C C:\Users\gpmpo\cs2-build\t26-chams-draw-hook\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t26-chams-draw-hook\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t26-chams-draw-hook\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t26-chams-draw-hook\build-injector"
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
run it, say plainly what is unverified — and say which of the four
observations above you could not make.

## 9. What the report must state

1. **The signature** — the pattern, the module, and the evidence that it is
   unique (the reviewer will re-scan the live module).
2. **The resolution** — how the target is resolved and every check it passes
   before the patch, and what happens when each check fails.
3. **The test** — the hostile test, and the proof that it fails without the
   fix.
4. **The counters** — what the status reports and what each number means.
5. **What is still open** — the live run (the calibration dump has to be
   refreshed with `scripts/calibrate-live.py` for the new signature to exist,
   and the operator has to run the game), and anything you could not verify.
6. The file list and the commit hashes.
