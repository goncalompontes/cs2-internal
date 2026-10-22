# T39r — the trace probe's reconciliation with T42, and its two open minors

> **The one-line version:** T39's probe carries **its own** vectored handler and
> its own `setjmp`/`longjmp` (`payload/control/include/cs2/control/trace_guard.hpp`).
> T42 has just landed **one** guard — `cs2::diag::call_guarded` — fixed so it
> actually recovers under clang-cl, and mechanically **forbids** a second
> recovery mechanism anywhere in `payload/`. So T39's branch cannot merge as it
> stands: the one-guard test goes red on the merged tree.
>
> This task reconciles it, and closes the two minors the re-review left open.

---

## 0. Where this branch stands

* branch `feat/t39-trace-probe`, head `fa98422`
* merge-base with the integration branch: `5aa748da`
* the branch is **77 commits behind** the integration branch
* the re-review (`t39-trace-probe-fa98422f78ff.md`) says **SHIP WITH FIXES**:
  the blocker and all seven majors are fixed and proven, and **two minors
  remain** (N-A, N-B) plus two nits

**This task is a fix round on that branch, not a new task.** The probe's own
spec is `docs/orchestration/spec-t39-trace-probe.md`; this document only adds
what the re-review left open and the reconciliation T42 requires.

---

## 1. Bring the branch up to date **first**

The branch is 77 commits behind and T42 has landed in the integration branch
since. **Merge the integration branch into `feat/t39-trace-probe` before
anything else** — a merge, never a rebase and never an amend (the house rule).

```powershell
cd C:\Users\gpmpo\cs2-wt\t39-trace-probe
git merge --no-ff feat/occlusion-penetration -m "merge: the integration branch, for the guard"
```

The orchestrator has checked with `git merge-tree --write-tree`: the merge is
**clean** — no conflicts are expected. If a conflict appears anyway, **stop and
report it** rather than resolving it by guessing.

**Order matters.** T42 must be in the tree *before* the reconciliation lands,
because the reconciliation's whole point is that `cs2::diag::call_guarded` now
recovers. Merging T39 into the integration branch first would put the probe's
engine call back on the *broken* guard.

---

## 2. The reconciliation — five changes, no more

The direction is **decided** (T42's review, M5, and the orchestrator): the
**shared** guard wins. `cs2::diag` owns the tree's one recovery mechanism.

### 2.1 Delete `payload/control/include/cs2/control/trace_guard.hpp`

The whole file. It carries its own `State`, its own `volatile long active`,
its own `jmp_buf` and its own `AddVectoredExceptionHandler` — and T42's
one-guard test (`payload/diag/tests/test_diag_guard.cpp`) fails on any of
those tokens outside `payload/diag/src/crash_handler.cpp`.

### 2.2 Call the shared guard

`payload/control/src/occlusion_query.cpp`:

* drop `#include "cs2/control/trace_guard.hpp"` (line 14);
* add `#include "cs2/diag/crash_handler.hpp"`;
* at line 1376, `*ok = trace_call_guarded(&invoke_trace, &call);` becomes
  `*ok = cs2::diag::call_guarded(&invoke_trace, &call);`

**The signature is identical** — `bool(void (*)(void*), void*)` — so the call
site does not otherwise change. Check that `payload/control` already links
against `payload/diag`; if it does not, add it (`payload/control/meson.build`).

**The link already exists** — `payload/control/meson.build` already lists
`diag_dep` in `control_lib`'s and `control_dep`'s `dependencies`
(`dependencies : [game_dep, aim_dep, diag_dep, tls_dep, sdk_dep, hooks_dep]`),
so `cs2::diag::call_guarded` is already available to the control module. **No
meson change is needed.** Check it in the merged tree anyway — T42 may have
touched `payload/diag/meson.build`.

### 2.3 Delete the probe's own guard test

* `payload/control/tests/test_control_trace_probe_guard.cpp` — delete the file;
* the **two** root `meson.build` blocks that register it (the
  `control_trace_probe_guard_test` executable and the
  `test('control-trace-probe-guard', ...)`) — delete both.

T42's `diag-guard` test covers the behaviour, and it covers it **better**: it
runs the fault ten times and is built with clang-cl.

### 2.4 Update the documents that name the probe's own guard

* `docs/re/bullet-trace.md:508-509` — "the probe's engine call runs under
  **its own** guard"
* `docs/re/occlusion.md:535` — the ownership note

Both must now say the probe's engine call runs under `cs2::diag::call_guarded`
— the tree's one guard.

### 2.5 Re-run the verify loop

See §5. In particular the **one-guard test must be green on the merged tree**:
that is the whole point of this round.

---

## 3. N-A — the comment promises a refusal the probe does not implement

`payload/control/src/service.cpp:1812-1816` says:

> "It does not parse the world body: the probe refuses to answer while the
> world's cache is cold (`occlusion_world_cache_ready`), because parsing 10k
> brush shapes inside the input hook is the one thing that could stall the game."

The probe does **not** refuse. `occlusion_query.cpp:1653` only *reads*
`occlusion_world_cache_ready()` and publishes it; the enumeration at
`:1657-1658` proceeds and pays the world's parse when the cache is cold.

### 3.1 Implement the refusal — this is the required fix

Not "fix the comment". The spec's §2.3 requires **bounded work that must not
stall the game**, and the parse of ~10 k brush shapes **inside the game's
CreateMove hook** is the one thing that can.

The reason the refusal is *correct* rather than merely cautious: the probe's
"our answer" is the solver's own `ray_span` over the world's occluders. If the
world's cache is cold, **"ours" is empty too** — so the comparison the probe
exists to make is meaningless, and the operator is told to run the heatmap once
first anyway (`docs/re/occlusion.md`).

→ When `occlusion_world_cache_ready()` is false, **return early** with a clear
reason: publish the same shape as the other refusals (`answer_missing` and a
reason naming the cold cache), publish **empty** lists for both sides, and do
**not** run the enumeration.

### 3.2 And keep the comment true

The comment must then describe what the code does. If you take a different
route — correcting the comment instead of implementing the refusal — you must
justify it in the report, because §2.3 of the probe's spec asks for the bounded
work and the comment is not the specification.

---

## 4. N-B — the results buffer's bound is a hope, not a check

`occlusion_query.cpp:1694-1698` puts the results buffer on the **game's main
thread's stack**:

```cpp
alignas(16) unsigned char interval_buffer[(TRACE_INTERVAL_CAPACITY + TRACE_INTERVAL_SLACK) *
                                          k_trace_interval_stride] = {};
```

and `:1757` checks the count **after** the engine call:

```cpp
const bool count_usable = count >= 0 && count <= TRACE_INTERVAL_CAPACITY + TRACE_INTERVAL_SLACK;
```

### 4.1 Why the engine can overrun it — re-derived, do not take this on faith

With bit 30 (`0x40000000`) set in the container's flags at `+0x14`, the
engine's writers **skip their growth path** — verified against the live build
by the re-reviewer:

* `vphysics2.dll +0x65113` → `movsxd r13, dword ptr [r15]` (count `+0x00`),
  `cmp r13d, dword ptr [r15 + 0x10]` (capacity `+0x10`),
  `test dword ptr [r15 + 0x14], 0x40000000` → `jne skip_growth`;
* `client.dll FUN_18084d1a0` → the same shape, and the append itself is
  `0x18084d278 inc dword ptr [rdi]` **then** the 0x38-stride store at
  `data[old_count]`.

So the engine **writes at `data[count]` unconditionally** and the capacity
field only gates the growth it has been told to skip. The count check comes
**after** the write, so it can only *report* an overrun — never prevent one.
320 records is 11 904 bytes of the game's input-path stack, and an overrun
there is a silent stack corruption that the fault guard **cannot** catch (it is
not an unmapped page).

### 4.2 The fix — three parts, all required

**(a) Move the buffer off the game's main-thread stack.** A file-scope or
function-scope `static` POD buffer in the payload, or a one-off heap
allocation. **The file-scope `static` is safe — the orchestrator has checked
the loader:** the injector allocates the **whole image** and lets the OS zero
it.

> `injector/src/main.cpp:780-781`:
> `VirtualAllocEx(process, nullptr, image.size_of_image, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)`

`MEM_COMMIT` pages are zero-filled by the OS, and the sections are then mapped
over them — so the payload's `.bss` (which is **not** in the file) is zero. A
file-scope `static` POD array in the payload is therefore zero-initialised with
**no CRT static-init**, which is what the payload's rules require.

Do **not** introduce CRT static-init, `<print>`, exceptions or `thread_local`.
Follow the precedent the payload already has for function-scope `static`
state — find it and use the same shape.

**(b) Make the overrun *detectable*, not merely unlikely.** Put a **canary**
after the buffer and check it immediately after the engine call, **before** the
count is trusted. A tripped canary must:
* publish `engine.faulted = true` (or a dedicated overrun flag), and
* refuse the answer — `answer_missing` / `engine_answer_usable = false` —
  exactly as a dropped answer is handled.

This is what makes the "bounded" claim true in the only sense that can be
made true: **the probe cannot publish an answer it cannot vouch for.** The
re-reviewer is explicit that the bound itself is unproven — the engine has no
hard limit — so the answer is not "prove the bound" but "make an overrun
visible and make it cost the answer".

**(c) Size it for a count the engine cannot plausibly exceed** — the engine's
own bullet path sizes its container for 64 entries and grows. State the chosen
size and the reasoning in the report. A ray crossing a few dozen solids is
realistic; hundreds is not, but the buffer must not be the thing that breaks.

### 4.3 And the document

`docs/re/bullet-trace.md` states the "bounded" property. After the fix it must
state what is actually true: the buffer is sized for N records, the canary
detects an overrun, and an overrun **refuses the answer** rather than
publishing a corrupt one.

---

## 5. The two nits

* **N-C** — `docs/re/bullet-trace.md:486-488` cites the guarded log lines as
  88135, 88387, 88409, 90369 and 533404 … 534203. The 1-based line numbers in
  the log are 88136, 88388, 88410, 90370 and 533405 … 534204. Off by one.
* **N-D** — a dispute the re-review already resolved: **the branch is right**,
  the figure is **four died / eight survived**, and the branch's text must not
  be changed back. The orchestrator has re-measured the log independently and
  confirms it, with a refinement the branch does not yet carry:

  > the **four deaths** are at log lines 88136, 88388, 88410 and 90370, each
  > followed immediately by `[crash] handler installed`; three of them fault at
  > `tier0+0x1ffe29 read addr=0x40` (the tier0 allocator) and one at
  > `read addr=0x0`. The **eight recoveries** are at 533405, 533479, 533602,
  > 533681, 533729, 533891, 533951 and 534204 — each one immediately after a
  > fresh `[crash] handler installed`, i.e. injections #190–#199, the ten most
  > recent in the log — and each faults at a **null pointer** with frames in
  > `ntdll`, a **different signature** from the four deaths.

  If `docs/re/bullet-trace.md` describes the fault signatures, add that
  distinction. It matters: it is the difference between "the guard killed the
  game" and "the guard recovered", and the log supports the second for eight
  of the twelve.

---

## 6. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and** definitions,
  fully qualified `std::`, no new global mutable state (ADR-0010),
  `[[nodiscard]]` / `noexcept`, value-init with `{}`,
  `static_cast`/`reinterpret_cast` only, 2-space indent, 100 columns, K&R
  braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no `thread_local` anywhere.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.
* **Re-derive; do not trust this document.** Three times in this project a
  careful implementer re-derived the engine instead of trusting the spec — and
  the engine was right every time. Write the specs from the live build, not
  from a summary.

---

## 7. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t39-trace-probe\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\t39-trace-probe\build
meson test   -C C:\Users\gpmpo\cs2-build\t39-trace-probe\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t39-trace-probe\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t39-trace-probe\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t39-trace-probe\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories live **outside** the worktree; do not run `meson setup`
inside it.)

---

## 8. What the report must state

1. **The merge** — that the integration branch was merged in, and that the tree
   is clean afterwards.
2. **The one guard** — that `trace_guard.hpp` is gone, that the probe calls
   `cs2::diag::call_guarded`, and that T42's one-guard test is **green** on
   the merged tree. Show the test's name in the `meson test` output.
3. **N-A** — the refusal, where it is, and what it publishes.
4. **N-B** — where the buffer lives now, its size, where the canary is, what a
   tripped canary publishes, and the size's reasoning.
5. **The evidence** — the full verify loop green, including the payload tests.
6. **What is still open** — and what you could not verify.

**The live run is not yours.** The measurement — the operator pointing at a
solid wall and the engine's answer being compared with ours — is the
orchestrator's, after this merges. Your job is to hand it over: the probe must
publish `engine.answer_missing == false`,
`difference.engine_answer_usable == true`, `engine.faulted == false`,
`interval_count > 0` with at least one `enter_exit == 1` record, and a verdict
of "agrees" with an empty engine list must remain **impossible by
construction**.
