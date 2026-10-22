# T20b — the animfix's estimator: the bound must not inflate

> **The one-line version:** the round-6 review of the animfix returned **DO NOT
> SHIP**: the fix for the burst collapsed the bound but introduced a **worse**
> bug — the staleness bound can now inflate to **250 ms**, and the animfix
> writes a **245 ms old** server value over the client's fresh prediction. This
> task fixes that, fixes the **sticky sample window** that made the burst
> scenario unreachable in the live payload, and corrects the documentation
> that overstated both.

---

## 1. The findings (from the round-6 review)

The review is at
`C:\Users\gpmpo\cs2-wt\.orchestration\reviews\cs2\t20-animfix-r6-8a84d12641a3.md`
— **read it in full**. It is the contract; this spec is the short version and
the constraints.

### B1 (blocker) — the sub-floor filter lets one late sample become the whole estimate

`payload/control/include/cs2/control/animfix.hpp:257-286`. The filter drops
every sample below the 8 ms floor and takes the lower median of **what is
left**. When a burst has evicted the rest of the window, "what is left" is a
**single** sample — and the estimate becomes that sample however large it is.

Measured with the real code, pre-fix vs post-fix:

```
PRE-FIX  : interval=  2  bound=  8      POST-FIX : interval=500  bound=250
```

and end to end (a healthy 64 Hz stream, a 100 ms hitch, a catch-up burst of
seven back-to-back decodes, then a stall):

```
PRE-FIX  : writes=6  refusals=281  oldest capture WRITTEN =   7 ms (bound   8 ms)
POST-FIX : writes=40 refusals=247  oldest capture WRITTEN = 245 ms (bound 250 ms)
```

**The failure:** the animfix writes the server's **245 ms old** aim yaw into
the graph over the client's fresh prediction — the exact failure the bound
exists to prevent, and the one the specification singles out ("that is the bug
that makes an animfix make things worse").

**The requirement:** a capture must never be written over a fresh prediction
because the bound inflated. The estimate needs a **minimum number of samples**
before it is trusted (refuse to estimate from fewer than N samples above the
floor, and keep the bound at the floor until then), and a sample far above the
median must never become the whole estimate.

### M1 (major) — the sample window is sticky: the estimate is frozen at the first eight samples

`animfix.hpp:230-239` (`observe_decode`). `count` is both the **valid count**
and the **ring index**, and it is capped at the window size — so once the
window is full the write index is `8 % 8 == 0` **forever**. Every new sample
overwrites slot 0; slots 1..7 are frozen from the first eight decodes onwards.

Measured (the real header):

```
after 8 x 16 ms   : count=8 window=[16 16 16 16 16 16 16 16] estimate=16 bound=32
after 8 x 100 ms  : count=8 window=[100 16 16 16 16 16 16 16] estimate=16 bound=32
```

**The failure:** the server's rate changes (CS2 drops to 32/25/20 Hz under
load; a lossy link changes it constantly) — the bound never moves, the
captures are refused as stale, and **the animfix goes inert and never
recovers** (measured duty cycle: 99.77 % at 64 Hz, 82.65 % at 25 Hz, 62.59 %
at 20 Hz, 37.68 % at 10 Hz).

This also made the round's M3 fix unreachable in the live payload: after the
window is full, a burst overwrites slot 0 only, so the estimate does not move
— the "burst collapses the bound" scenario only exists in the **initial fill**.

**The requirement:** a separate total counter for the ring index
(`clock.total % k_snapshot_window`) and the valid count derived from it
(`min(total, k_snapshot_window)`), so the window really is a sliding window of
the last eight intervals.

### M2 (major) — `docs/re/animfix.md` §10.1 states the opposite of what the code does

`docs/re/animfix.md:1236-1250`. Three sentences are false against the code —
"a burst of fast decodes — of **any** size — cannot move the estimate", "a
**late** decode still cannot move it", and the argument that "an inflated
bound is the direction that writes a stale capture over the client's fresh
prediction" (which is now exactly what the code does).

**The requirement:** the document must say what the code does. The file is
**append only** — you may **not** rewrite §10.1; append a dated section that
corrects it, and make the code and the document agree. (If the estimate's
behaviour changes, the appended section is the one that describes it.)

### The minors and nits

* **m1** — the scenario's fallback to an older payload's counter is **silent**:
  the two denominators are not the same quantity and the result does not say
  which was used. Report it (`"duty_cycle_source": "enabled" | "client"`) and
  say it in the verdict — or refuse to measure on a payload that does not
  expose the counter.
* **m2** — the new comment in `input.cpp:1504-1507` puts the codec on the
  "network thread"; `docs/re/animfix.md:987-988` says the codec runs on the
  **main thread** (the network *phase*). Fix the comment (or drop the call
  from the per-tick path).
* **m3** — the status's refusal counters mix per-tick and per-slot counts:
  `animfix_skip_controller` is counted **per slot** on three paths inside the
  per-slot loop and **per tick** on four others, and the new comment asserts
  a unit the code does not keep. Make the units true (or say what they are).
* **m4** — `animfix_snapshot_interval_ms` now reports **0** for a server
  faster than the floor, and 0 also means "nothing observed yet". Report the
  sub-floor rate (the unfiltered median) or say in the comment that 0 is
  ambiguous.
* **n1** — `test_a_burst_of_fast_decodes_does_not_collapse_the_bound` is
  **green against the pre-fix code** (it only exercises the initial fill).
  Rewrite it to exercise the case that actually changed.
* **n2** — two of the new tests are **source-text** checks; they pin the text,
  not the behaviour. A behavioural test is better where one is possible.
* **n3** — `animfix::Refusal::write_failed` has **no host test**.
* **n4** — `animfix::StatusLatch` is a "last status" latch, not a "distinct
  statuses" latch: an alternation A, B, A, B logs every time. Either make it
  a set, or make the name and the comment honest.
* **n5/n6** — the §10.1 wording about a faster-than-the-floor server; the
  per-tick cost of the `ensure_thread_tls()` call in the anim tick detour.

## 2. The rules that are not negotiable

- **The validity rule is the point of the feature:** a capture must not be
  written over a fresh prediction. Every change to the estimator is measured
  against that, not against the duty cycle alone.
- The estimator's decisions live in a **pure header**
  (`payload/control/include/cs2/control/animfix.hpp` — no platform headers,
  no engine access) and are pinned by a **host test that can fail**.
- **Prove it:** for every change, show the behaviour **before** and **after**,
  with the real code (the repository's own `duty_cycle()` helper and the
  reviewer's harnesses are the model). A test that is green against the
  pre-fix code proves nothing — say so in the report if you cannot make one
  fail.
- `docs/re/animfix.md` is **append only**. Never rewrite an existing section;
  append a dated section that says what the code now does.
- `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, trailing return types, fully qualified `std::`, errors as
  values, no exceptions, no `thread_local` in a detour, no new global mutable
  state (ADR-0010).
- No hard-coded RVAs (ADR-0012).

## 3. What to build

1. **The window** (M1): a real sliding window of the last eight intervals.
2. **The estimate** (B1): it must not be taken from fewer than N samples
   above the floor, and a single late sample must not become the estimate.
   Whatever you choose, the **bound must never exceed what is safe** — the
   reviewer's measurement (the oldest capture written) is the number to
   report.
3. **The documentation** (M2 + the nits): append a dated section to
   `docs/re/animfix.md` that says what the code now does, and fix the
   minors.
4. **The tests**: the burst test must fail against the pre-fix code; add the
   missing `write_failed` test; make the behavioural tests behavioural.

## 4. The measurements the report must contain

* the reviewer's harness, run against the **pre-fix** and the **new** code:
  the five §9.6 scenarios (99.7717 / 99.7500 / 99.7717 / 99.5434 / 89.9543 /
  22.0165 % — these must stay **identical to the digit**), the burst, the
  **seven-fast-then-one-late** case (the reviewer's B1 case), the three named
  cases (one late decode, one fast decode, a total stall), and the
  **oldest capture written** in each;
* the server-rate change (64 → 30 → 25 → 20 → 10 Hz): the duty cycle and the
  bound after each change (the reviewer's M1 case);
* the proof that each new test fails without the fix.

## 5. File ownership

You own `payload/control/include/cs2/control/animfix.hpp`,
`payload/control/include/cs2/control/input.hpp`,
`payload/control/src/input.cpp`, `payload/control/src/service.cpp`,
`payload/control/tests/test_control_animfix.cpp`,
`scripts/scenario/scenarios/animfix.py`,
`scripts/scenario/tests/test_animfix.py`, `scripts/scenario/protocol.py`, and
`docs/re/animfix.md` (**append only**).

You do **not** own `wire::Settings`, the ESP, the ragebot, the chams, the
anti-aim, the third-person camera, or the protection workstream. If you need
something outside this list, say so in your report instead of editing it.

## 6. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t20b-animfix-estimator\.orchestration\env.ps1
meson compile -C C:\Users\gpmpo\cs2-build\t20b-animfix-estimator\build
meson test   -C C:\Users\gpmpo\cs2-build\t20b-animfix-estimator\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t20b-animfix-estimator\build-payload"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

**You cannot launch CS2 from here** (session 0, no GPU). Say plainly what
stays unverified — in particular whether the engine really delivers a burst of
fast decodes, and how the server's rate behaves in a live round.
