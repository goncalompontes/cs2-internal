# T41 — the desc-cache probe must test the invariant, not the scheduler

> **The one-line version:** `features-chams-desc-cache` **fails 1 run in 4** on a
> fast machine, and it has nothing to do with the cache. The **cache** reported
> **zero mismatches in every run** — the failures are the test's own
> assertions on **scheduler-dependent counts** ("the generation really advanced",
> "the reader still got the entry when it could"). A test that fails because
> the OS did not interleave the threads the way the test hoped is a broken
> test, and it is now in the merged tree.

---

## 1. What is wrong today

`payload/features/tests/test_chams_desc_cache.cpp` — the acceptance test for
T33's per-slot seqlock. Reproduced on the merged tree (`1c80086` and later), the
**same binary**, 8 consecutive runs:

| run | result | the failing check |
|---|---|---|
| 1 | **FAIL** | *"the reader still got the entry when it could"* |
| 2 | **FAIL** | *"the generation really advanced"* |
| 3–8 | ok | — |

and the **shipped cache reported `0` mismatches in every single run**,
including the two that failed. The control (the old three-atomic protocol
reproduced inside the test) reported between 0 and 3727 mismatches — and is
correctly **printed, not asserted**, so it never fails the suite.

The problem is the four assertions on **counts that depend on how the OS
scheduled the threads**:

| line | assertion | why it is flaky |
|---|---|---|
| `:328` | `expect(advances.load() > 0, "the generation really advanced")` | the advancing thread's count is 0 when the scheduler never let it advance before the readers finished — run 2 printed `0 advances` |
| `:329` | `expect(probe.hits.load() > 0, "the probe still read the cache across the bumps")` | under heavy contention the reader can legitimately give up on **every** lookup — run 1 printed `0 hits` with 160 000 give-ups |
| `:388` | `expect(cache.give_ups() > 0, "the reader gave up at least once under the contention")` | a run with no contention would never give up |
| `:390` | `expect(probe.hits.load() > 0, "the reader still got the entry when it could")` | as above |

**The invariant that actually matters — `mismatches == 0` — is asserted
everywhere and has never failed.** That is the test's real content; the four
`> 0` assertions are the test's *reachability* checks, and they are guesses
about the scheduler.

### 1.1 Why this matters beyond the noise

* A **flaky test in the merged tree** fails CI and trains everyone to ignore
  red. The repo's verify loop runs the host suite on every merge.
* It **undermines the review's central claim.** T33's re-review said *"the probe
  bites"* — and it does — but a test that fails 1 run in 4 for the wrong reason
  makes "the probe bites" indistinguishable from "the test is noisy".
* The **ledger's rule** is that a finding is closed only when the fix is
  merged **and re-reviewed**. T33's fix is merged and re-reviewed, but with a
  test that is unreliable, that claim is weaker than it should be.

---

## 2. What to build

### 2.1 The invariant stays; the reachability becomes **deterministic**

**The requirement:** the test must fail **only** when the cache is wrong, and
must never fail because of how the threads were scheduled.

**Every assertion must be one of:**

* **an invariant** — true for every correct execution, checked on every run;
  or
* a **guaranteed** precondition — the test **arranges** for it rather than
  hoping for it.

**The four `> 0` assertions must become one of those two.** The options, with
their costs — pick, and say which:

* **Arrange it.** For the generation bump: the **main thread** advances the
  generation (or a thread is **signalled** once it has advanced), so the
  "the generation really advanced" is a fact, not a hope. For the give-up
  path: the test needs the reader to *observe* an odd sequence four times in a
  row — which is a **scheduling** outcome today. Can it be **forced**? The
  cache's writer claims the slot and holds it for the duration of a few
  stores; if the test cannot hold it, the give-up path may need a **test-only
  hook** (say so, and say where it would live — the spec's §4 lets you touch
  this test file and `chams.hpp`; **a test-only hook in the shipped header
  must be justified**).
* **Condition it.** Assert the consequence **only when** the precondition
  actually happened, and **say so** — but then the path may go untested, so
  the test must also **report** (not assert) which paths were exercised, so a
  silent loss of coverage is visible.
* **Do both**: arrange what can be arranged, and condition what cannot.

### 2.2 The test must still **bite**

**The requirement is unchanged and is the point of the task:** the test must
still fail when the cache is wrong.

**The control is the proof.** The test already reproduces the **old**
three-atomic protocol inside itself and counts its mismatches. That control
must be made to **fail the suite** — deterministically — when the protocol is
wrong, and it must **not** be a printed number.

**The control's own mismatch count is currently 0–3727 across runs** — it is
the same scheduler dependence. So the control needs the same treatment: it
must be **asserted** in a way that is **deterministic**. (How is your call —
but a control that is *printed* proves nothing, and the reviewer will say so.)

**Prove it:** the test must still fail when the shipped cache is replaced by
the old protocol — and it must fail **every** time, not 1 time in 4.

### 2.3 The stress must still be a stress

The test's value is that it hammers the cache with four threads, two descs
colliding in one slot, and a generation bump, and shows **zero** mismatches.
**Do not reduce the contention to make the test stable.** If the fix makes the
test's rounds shorter, say so and justify it.

**A stable test is worth more than a thorough one only when the thorough one
is unreliable** — and the right answer is to keep the contention and make the
**assertions** right.

---

## 3. What is out of scope

* The **cache's** implementation (`payload/features/include/cs2/features/chams.hpp`'s
  `DescCache`, `payload/features/src/chams.cpp`). **The cache is correct** — it
  reported zero mismatches in every run, including the failing ones. **Do not
  change the cache's behaviour.** (A test-only hook is a possibility; see §2.1,
  and it must be justified.)
* The other chams tests (`test_chams_draw_hook.cpp`, `test_chams_target.cpp`).
* The **T33 task's** other findings — they were closed by T33's fix round and
  re-reviewed.
* Any other feature's tests. If you find a flaky test elsewhere, **report it**.

---

## 4. File ownership

You own:

* `payload/features/tests/test_chams_desc_cache.cpp` — the test
* `payload/features/include/cs2/features/chams.hpp` — **only** if a test-only
  hook is genuinely needed, and then **only** the hook, with a comment saying
  it is for the test
* `docs/known-issues.md` — **append only**: the flakiness, the fix, and the
  merge

You do **not** own `payload/features/src/chams.cpp`,
`payload/hooks/**`, `payload/aim/**`, `payload/control/**`, `overlay/**`, or
any other test. If you need a file you do not own, **stop and report it**
instead of editing it.

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and** definitions,
  fully qualified `std::`, no new global mutable state, `[[nodiscard]]` /
  `noexcept`, value-init with `{}`, `static_cast`/`reinterpret_cast` only,
  2-space indent, 100 columns, K&R braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
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

**The evidence this task must produce:** the test run **twenty (20) times in a
row** in your worktree, with **zero** failures — and the **old protocol**
replaced in the shipped cache, run **twenty (20) times**, failing **every**
time. **Both numbers go in the report.** A test that is "usually fine" has not
been fixed.

---

## 7. What the report must state

1. **Which assertions were flaky**, and what each became.
2. **The control** — how it is now asserted, and how you made it deterministic.
3. **The contention** — what the test still does, and what (if anything) you
   reduced, and why.
4. **The evidence** — the twenty green runs and the twenty red ones, with the
   commands.
5. What is still open, and what you could not verify.
