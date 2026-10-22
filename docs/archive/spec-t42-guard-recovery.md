# T42 — the payload's fault guard does not work on the build it ships in

> **The one-line version:** `cs2::diag::call_guarded` is the payload's only
> safety net for an engine call that faults — and on **clang-cl**, the compiler
> the payload is actually built with, it **cannot recover**: the VEH handler is
> `noexcept`, so it is `nounwind`, so MSVC's `longjmp` refuses to unwind
> through it and **fast-fails with `0xC0000409`** instead of returning. The
> guard turns "a bad engine call logs a line" into "a bad engine call kills
> the game". It has **never** been exercised — but it is the net every engine
> call in the payload relies on.

---

## 1. What is wrong today

### 1.1 The mechanism

`payload/diag/src/crash_handler.cpp:131`:

```cpp
LONG CALLBACK vectored_handler(EXCEPTION_POINTERS* info) noexcept {
  ...
  if (g_guard_active != 0 && g_guard_thread == GetCurrentThreadId() && is_recoverable(code)) {
    g_guard_active = 0;
    log_exception("guarded", info);
    std::longjmp(g_guard_jmp, 1);     // <- line 138
  }
  ...
}
```

A `noexcept` function is **`nounwind`**. clang-cl therefore emits **no unwind
information** for `vectored_handler`, and MSVC's `longjmp` (implemented over
`RtlUnwindEx`) **refuses to unwind through a `nounwind` frame** — it raises
`STATUS_STACK_BUFFER_OVERRUN` (`0xC0000409`) via `__fastfail` instead of
performing the jump.

**The orchestrator reproduced this** with a standalone repro built by
**clang-cl 22.1.8** (the payload's compiler), the same shape as the payload's
handler:

```
exit = -1073740791     (0xC0000409)
```

and the program never printed its "RECOVERED" line — it **died**.

The same source built with **MSVC's `cl.exe`** recovers, and so does the same
source with the `noexcept` **removed**. The one word is the whole defect.

### 1.2 Why it matters even though it has never fired

* **The guard is the payload's safety net for every engine call it makes.**
  `call_guarded` is used at `payload/control/src/occlusion_query.cpp:467` (the
  transform) and `:917` (the `EntitiesInBox` query) — and the repo's entire
  reasoning about calling into the engine rests on it: *"a wrong call faults
  the game (the payload has no registered exception tables), so this is not
  guessable"* (`docs/re/aim.md`), and the injector registers `.pdata` so the
  SEH unwinder can work. **That safety net does not work on this build.**
* **It is latent, not recurring.** The orchestrator checked the payload's own
  log: **zero** `[crash]` lines across **199** injections (199
  `[crash] handler installed` lines, one per injection). So no guarded call has
  ever faulted. **Nothing has been lost to this yet** — but the next bad
  engine call (a stale offset, a game update, a wrong ABI) kills the game
  instead of logging a line.
* **The repo has no test for it.** The host suite is built with **MSVC**
  (`cl.exe`), where the guard *does* recover — so a host test would pass
  either way and would **never** have caught this. **The coverage gap and the
  defect are the same thing.**

### 1.3 The repo already has a second, working guard

T39 (the trace probe) needed a guard and found it could not use this one, so it
wrote its own:
`payload/control/include/cs2/control/trace_guard.hpp`. That one **works** — its
test `control-trace-probe-guard` makes a call that deliberately faults (a write
through a null pointer) and the process **keeps running**.

**So the payload now has two recovery mechanisms, one of which works and one of
which does not.** That is not a state to leave in.

---

## 2. What to build

### 2.1 One guard, and it must work on clang-cl

**The requirement:** `cs2::diag::call_guarded` must **recover** from a
recoverable fault when the payload is built with **clang-cl** — the compiler
the payload ships with.

**The fix is at least the removal of the `noexcept` from the VEH handler.** But
the implementer must establish, not assume, that the recovery then works on
clang-cl, and that the fix does not introduce a different failure:

* the handler's frame must be **unwindable** — no `noexcept`, and nothing in
  it that the unwinder cannot traverse;
* the `longjmp` must not cross a frame with a **non-trivial destructor**;
* the handler must still **log** the exception before it unwinds (the log is
  the only evidence a fault happened);
* the handler must still return `EXCEPTION_CONTINUE_SEARCH` for the
  **non-recoverable** codes, and the first-chance logging must keep working;
* `unhandled_filter` is also `noexcept` — it does not `longjmp`, so it is
  probably fine, but **say so**.

**And the two guards must be reconciled.** The repo should end up with **one**
recovery mechanism. Either:

* `call_guarded` is fixed and the probe uses it (and `trace_guard.hpp` is
  removed, or reduced to a thin wrapper over the fixed one); or
* the probe's guard is generalised into `cs2::diag` and `call_guarded` uses it.

**Say which, and why.** Two guards, one of which is broken, is how this bug
happened.

### 2.2 The test must be built with **clang-cl**

**This is the heart of the task.** A host-suite test built with MSVC **cannot
catch this class of defect** — and that is exactly why the defect survived.

**The requirement:** there must be a test that

1. is **compiled with clang-cl** (the payload's compiler), and
2. **deliberately faults** inside a guarded call, and
3. asserts the call **returned** (recovered) and the process **is still
   running**.

**And the test must be RUN, not merely compiled.** The repo's verify loop's
`payload` step is `meson compile` — it never runs anything from that build
directory. So the test needs a way to be executed: a `meson test` in the
payload's build that the verify loop runs, or the Python suite invoking the
executable. **Say how it is run, and make sure it runs.**

**The source check is not enough on its own** — but it is a cheap, always-on
regression guard against the `noexcept` coming back, and the repo uses that
pattern elsewhere (T35's fix round had one). **Do both**: the clang-cl test
for the behaviour, and a source check for the shape.

**Also:** the test must be **deterministic**. A fault is a fault — it must
recover every time, not 99 times in 100.

### 2.3 The repo's own claims must be corrected

The repo's documents state the safety argument in several places — *"a wrong
call faults the game (the payload has no registered exception tables), so this
is not guessable"*, and the injector registers `.pdata` "so the SEH unwinder
can work". **That argument is only as good as the guard.**

→ Find those claims and **make them true**: once the guard works, the payload
**can** survive a bad engine call, and the documents should say so — and say
what the guard's limits are (one in-flight guarded call per process, the
recoverable code set, and the fact that a fault on a **different** thread is
not guarded).

**A claim in T39's report is wrong and must not be repeated:** it states the
payload's log shows nine `[crash] guarded` lines, each followed by a fresh
handler install — i.e. that the process died and was re-injected. **The log has
zero `[crash]` lines across 199 injections.** The guard has never fired. The
defect is real; the "it has been happening" claim is not.

---

## 3. What is out of scope

* **Calling more of the engine.** The guard's repair is the task; adding new
  engine calls is not.
* **The trace probe's own call** (T39's) — the probe's *usage* is reviewed
  separately. This task is about the **guard**.
* **The injector.** The `.pdata` registration is already there and is not the
  problem.
* **The crash handler's logging** — the first-chance and unhandled paths are
  fine; do not redesign them.

---

## 4. File ownership

You own:

* `payload/diag/src/crash_handler.cpp` and
  `payload/diag/include/cs2/diag/**` (whatever the header is called) — the guard
* `payload/control/include/cs2/control/trace_guard.hpp` — **only** to reconcile
  it with the fixed `call_guarded` (§2.1)
* the **new** clang-cl-built test and wherever it is registered
* `payload/diag/meson.build`, `payload/control/meson.build` and the root
  `meson.build` — the test's registration
* `docs/re/aim.md`, `docs/re/bullet-trace.md`, `docs/handoff.md` and
  `docs/architecture.md` — **only** the claims that the guard makes true or
  false (§2.3)

You do **not** own `payload/aim/**`, `payload/control/src/service.cpp`,
`payload/control/src/input.cpp`, `payload/features/**`, `overlay/**`, the
injector, or the dumper. If you need a file you do not own, **stop and report
it** instead of editing it.

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and** definitions,
  fully qualified `std::`, no new global mutable state, `[[nodiscard]]` /
  `noexcept`, value-init with `{}`, `static_cast`/`reinterpret_cast` only,
  2-space indent, 100 columns, K&R braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no `thread_local` anywhere.
* **The payload is built with clang-cl 22.1.8** — the fix must be verified
  with **that** compiler, not only with MSVC.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 6. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\<task>\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\<task>\build
meson test   -C C:\Users\gpmpo\cs2-build\<task>\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories live **outside** the worktree; do not run `meson setup`
inside it.)

**The evidence this task must produce:**

1. The **clang-cl-built** test: a guarded call that **deliberately faults**
   returns, the process survives, and the log line is written — **ten times in
   a row**.
2. The **MSVC-built** host test (if the repo keeps one): the same.
3. A **before-and-after** demonstration: the same test built against the
   **pre-fix** handler **dies** with `0xC0000409` — i.e. the test is proven to
   **bite** on the defect it is for.

**Both numbers go in the report.** A test that passes on the fixed code and
was never seen to fail on the broken code has not been shown to work.

---

## 7. What the report must state

1. **The mechanism** — why the `noexcept` breaks the `longjmp`, in your own
   words, with the repro's exit code.
2. **The fix** — what you changed, and **why it is now unwindable**.
3. **The one guard** — how the two mechanisms were reconciled, and why.
4. **The test** — where it lives, **how it is run** (not merely compiled), and
   that it is built with **clang-cl**.
5. **The evidence** — the ten green runs on the fixed code and the
   `0xC0000409` on the pre-fix code, with the commands.
6. **The documents** — every claim in the repo that the guard makes true or
   false, and what you changed.
7. What is still open, and what you could not verify.
