# T43 — the comparison becomes a test: replay the engine's trace against our occlusion machinery

> **The one-line version:** the probe already computes the difference between
> the engine's own answer (`TraceLineIntervals` — the call the real bullet
> makes) and our occlusion machinery's answer, and publishes **both** plus the
> verdict. What it does **not** do is *assert* anything: the verdict is a
> reading for a human to interpret, and the comparison cannot be re-run
> without the game. This task makes the comparison a **test**: one recorded
> answer becomes a committed **fixture**, a host test replays it through the
> *same* `diff_trace` the payload uses, and the verdict — including the claim
> that our machinery sees the world body — is pinned.
>
> It also closes T39's §3.2 obligation, **"prove the probe can fail"**.

---

## 1. Why this is a task and not a reading

The operator's report was: *"the main world mesh is not in the occluders"*.
The repo's own 2026-10-18 note records the same symptom and says the mesh fix
was written but **never live-validated** (`docs/handoff-wave3.md` §6). T39
built the **instrument**; the measurement is still owed, and nothing in the
tree would notice if the answer changed.

Two things are missing:

1. **Nothing asserts the agreement.** The probe publishes a verdict; a human
   reads it. A regression that drops the world mesh from our enumeration
   changes the verdict from `agrees` to `engine_only` and **nothing fails**.
2. **Nothing keeps the comparison honest between runs.** The difference rule
   (`trace_span_matches`) is a *position* rule with a tolerance; a change to it
   that makes the probe agree with itself would pass every review that only
   reads the code, and would only be caught by a live run weeks later.

A committed fixture closes both: the comparison is replayed on **every build**,
with no game, and the recorded engine answer is the **ground truth** it is
checked against.

---

## 2. The fixture — it is the probe's own output

The probe's published answer already *is* the fixture format. Per ray
(`scripts/scenario/protocol.py`):

```python
class TraceRayDict(TypedDict):
    start: Vec3Dict
    direction: Vec3Dict
    end: Vec3Dict
    depth: float
    engine: TraceRayEngineDict     # ok, faulted, interval_count, answer_missing,
                                   # surface_mapped, intervals[] (fraction, distance,
                                   # enter_exit, surface, raw)
    ours: list[TraceOccluderDict]  # index, kind, cluster, entry, exit
    difference: TraceDifferenceDict # verdict, engine_answer_usable,
                                    # engine_only[], ours_only[]
```

**Take one recorded ray, verbatim, and commit it as a fixture.** No new
serialisation format: the fixture is JSON, it is the probe's own answer, and
the reader is a JSON parser the host suite already has.

### 2.1 The fixture's metadata

A fixture without provenance is a stale file waiting to mislead. The file
carries, beside the rays:

| field | why |
|---|---|
| `recorded` | the date and time of the recording |
| `map` | the map the ray was recorded on — the world's geometry is per-map |
| `position` | the eye's position (the ray's origin) |
| `yaw`, `pitch` | the view angles the ray was built from |
| `depth` | the ray's length |
| `filter_mask`, `filter_group`, `filter_flags` | **the filter the engine was given** — the engine's answer is only the bullet's answer for this filter |
| `client_sha256` | the `client.dll` the answer came from (the repo already records one: `sha256 5a46a09b…`) |
| `verdict` | the **expected** verdict — the claim under test |

The `client_sha256` matters: the engine's answer is a property of the build.
A fixture recorded against a different `client.dll` is not evidence about this
one, and the metadata says so.

### 2.2 What is immutable and what is the claim

Two halves of the fixture with **different** rules:

* **The engine's half is immutable.** `engine.intervals[]` — including every
  `raw` hex string — is the **engine's ground truth**. It must never change.
  If a proposed fixture edit touches it, that edit is not a fixture refresh,
  it is a falsification, and it must be refused.
* **Our half is the claim.** `ours[]` and the expected `verdict` are what the
  test asserts. **Changing them is how the world-mesh fix lands** — the
  verdict flips from `engine_only` to `agrees` — and that change is a
  deliberate, reviewed edit, never a silent one.

The test enforces this asymmetry: it re-reads the engine's half from a
**second, immutable copy** and fails if the two disagree. See §4.3.

---

## 3. The host test

A new host test, `payload/control/tests/test_control_trace_comparison.cpp`,
registered in the root `meson.build` next to `control-trace-probe`.

It replays the fixture through the **same** functions the payload uses — not a
re-implementation:

```cpp
// The payload's own rule, the same header the payload compiles:
const std::vector<cs2::control::TraceSpan> spans =
    cs2::control::pair_trace_spans(intervals);
const cs2::control::TraceDiff diff =
    cs2::control::diff_trace(intervals, ours, tolerance, engine_answer_usable);
```

The types live in `payload/control/include/cs2/control/occlusion_query.hpp` as
header-only `inline` functions, and `payload/control/tests/test_control_trace_probe.cpp`
already links that header — so this is a host test in the ordinary sense.

### 3.1 The assertions

1. **The verdict reproduces.** `diff.verdict` equals the fixture's expected
   verdict. This is the headline.
2. **The lists reproduce.** `engine_only` and `ours_only` match the fixture's
   recorded lists (same count, same entries/exits within a tolerance).
3. **The engine's half is untouched.** Every `raw` hex string and every
   `distance`/`enter_exit` in the fixture matches what the engine produced —
   checked against the immutable copy (§2.2).
4. **The rule is not vacuous** — the mutation checks in §4.
5. **The world mesh claim.** The fixture records what our machinery saw along
   that ray. The test asserts the claim the fixture was recorded to prove:
   * if the expected verdict is `agrees`, then `ours` is **not** empty and
     `difference.engine_only` **is** empty — i.e. every solid the engine found,
     we found too;
   * and the fixture's `ours[].kind` records whether a **`mesh`** occluder was
     among them. The world body's six mesh shapes are what the operator's
     report is about, so a fixture recorded against a ray through the world
     must show it.

### 3.2 The tolerance

`diff_trace` takes a `tolerance` (world units). It must be **recorded in the
fixture** and used on replay — the same number the recording used. The
difference rule's own documentation says the tolerance is a **position**
tolerance and explicitly *not* a widening of the overlap test, so a fixture
replayed with a different tolerance is not the same test.

### 3.3 Where the fixture lives

`payload/control/tests/fixtures/trace-comparison-<name>.json`, and the test
is registered with Meson so it **runs** (the discipline T33/T41 taught: a
check that is not run is not a check).

---

## 4. The mutation checks — the test must be able to fail

A golden-file test that cannot fail is worse than no test. **Four** mutations,
each in a scratch copy, each expected to change the outcome:

| mutation | expected |
|---|---|
| move one of our hits **0.5 units** earlier along the ray | the verdict must **stop** being `agrees` (this is the case T39's re-review found: the engine's wall at 100–150 and our prop at 98–99.5 do not cover each other) |
| delete one of our hits | the verdict must become `ours_only` |
| delete one of the engine's intervals | the verdict must become `engine_only` |
| set `engine_answer_usable` to false | the verdict must become `unknown` and **both lists must be empty** — and it must **not** become `agrees` |

The last one is the re-review's **M2** finding, and the invariant it protects
is the one the handoff calls out by name: *"a verdict of 'agrees' with an
empty engine list is impossible by construction"*. It must be **pinned**, not
assumed.

---

## 5. The live half — the probe asserts, it does not only report

The fixture cannot prove the live world's geometry *today*. The live run stays
necessary, and this task gives it a **pass/fail** shape:

* the probe's answer is read by a **runner** (`scripts/trace-compare.py`, new)
  that connects to the control channel, sends `trace_probe`, and checks the
  result against the expectations in `docs/operator-live-check.md`:
  * `engine.answer_missing == false`
  * `difference.engine_answer_usable == true`
  * `engine.faulted == false`
  * `engine.interval_count > 0` **and** at least one record with
    `enter_exit == 1`
  * a verdict of `agrees` with an empty engine list is **impossible** — the
    runner fails if it sees it
* the runner **writes the fixture** — the same JSON the host test replays — so
  a live run *produces* the regression test's input. That is what makes the
  two halves one thing: the live measurement is recorded once, and every
  later build replays it.

**The runner's verdict must be an exit code**, so the whole thing can be run
from a script and can fail a gate.

---

## 6. File ownership

You own:

* `payload/control/tests/test_control_trace_comparison.cpp` — the new test
* `payload/control/tests/fixtures/**` — the fixtures
* the root `meson.build` — the test's registration
* `scripts/trace-compare.py` — the live runner
* `scripts/scenario/tests/test_trace_compare.py` — the runner's own tests
* `docs/re/bullet-trace.md` and `docs/re/occlusion.md` — the fixture's format,
  how to record one, and what the verdict means
* `docs/operator-live-check.md` — the live check's pass/fail shape

You do **not** own `payload/control/src/occlusion_query.cpp` (the difference
rule itself), `payload/control/include/cs2/control/occlusion_query.hpp`, the
probe's engine call, the dumper, or the injector. If the comparison rule
turns out to be **wrong** — not merely untested — **stop and report it**
instead of editing it: that is a separate task.

---

## 7. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and** definitions,
  fully qualified `std::`, no new global mutable state (ADR-0010),
  `[[nodiscard]]` / `noexcept`, value-init with `{}`,
  `static_cast`/`reinterpret_cast` only, 2-space indent, 100 columns, K&R
  braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no `thread_local` anywhere. (This test is a **host** test, so
  none of it applies to the test binary — but it does apply to anything you
  add to the payload.)
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.
* **Re-derive; do not trust this document.** Three times in this project a
  careful implementer re-derived the engine instead of trusting the spec — and
  the engine was right every time.

---

## 8. The verify loop

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

---

## 9. What the report must state

1. **The fixture** — where it came from (the live run that produced it: the
   map, the position, the view angles, the filter, the `client.dll`), what it
   records, and the **expected verdict** it pins.
2. **The replay** — that the host test reproduces the verdict through the
   payload's own `diff_trace`, with the commands and the output.
3. **The mutations** — all four, each shown to change the outcome.
4. **The live runner** — that it exits non-zero on a bad answer, and the
   output of a run that passed.
5. **The world mesh** — what the fixture's `ours[].kind` shows about the world
   body, and whether the verdict is `agrees`. **If it is not `agrees`, say so
   plainly and record it as the open item it is** — that is the measurement the
   operator asked for, and a red answer is a real answer.
6. What is still open, and what you could not verify.
