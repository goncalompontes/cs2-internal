# Known issues — the open findings, and where each one is

> **This is the ledger of everything the reviews and the live run found that is
> not yet fixed.** It exists so that nothing is lost while the work moves on to
> new features: each entry says what the defect is, how it was found, where it
> lives, and — when there is one — the branch that already fixes it.
>
> **Rule for this file:** a finding is added when it is *reported*, and it is
> only removed when the fix is **merged and re-reviewed**. A finding that is
> known and not fixed is a known defect, not a surprise.

Last updated: 2026-10-21, after the first live run.

---

## 1. The live run (2026-10-21) — what the first live session found

The operator ran a 60-minute bot match on `de_dust2` with `bot_stop 1`; the
payload was injected once and the game was driven from the agent session.

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **L1** | **major** | **The animfix never captures the server's graph inputs.** The anim-tick hook fires (1542 client ticks) but the codec captured **nothing** (0 captures, 376 skips, all `no_capture`, `snapshot_interval_ms: 0`). The payload hooks **one** of the engine's mirrored snapshot codecs (`0x8d9740`); the RE lists a twin at `0x8daab0`, and the mirror's byte pattern has **three** byte-identical siblings on the live build (`0x8daab0`, `0x90eb20`, `0xd29b30`) — the same codec shape for three different classes. | `payload/control/src/input.cpp` (the capture), `external/dumper/src/analysis.rs` | **`feat/t32-animfix-codec`** (in flight) |
| **L2** | minor | **`strafe_ignored_during_window: -18`** — a counter that goes **negative**. | the strafe's status | — (not yet assigned) |
| **L3** | minor | The `bot-match` scenario **returns during the freeze/buy window** (`screen: "buy-time"`), so the operator has to wait by hand; and the team join happens late (as a side effect of driving to a live round). | `scripts/scenario/awaiting.py` | **`feat/t31-bot-match-live`** (in flight) |
| **L4** | nit | The payload's log has **no rotation** — 65 MB of accumulated history from three days. The growth rate is fine (113 B/s). | `%LOCALAPPDATA%\cs2-internal\cs2-payload.log` | — |

**What the live run confirmed as working:** the auto-strafe accelerates
(`speed_gain: 112.1`, `max_speed: 306.3`, `strafe_steered: True`, 1228 writes,
120 bhop presses); the 60-minute rounds and `bot_stop` take effect; the ESP
publishes and draws (`esp=110 drawn=6` at 1600x900); the payload's hooks
install; the new dumper signatures resolve (`animfix_graph_codec` →
`0x8d9740`, `animfix_anim_tick` → `0x35aeb0`); the animfix's **anim-tick**
hook fires; the offsets refresh offline reproduces the dump exactly.

---

## 2. The camera crash — the root cause, and the one that is still live

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **C1** | **blocker (live)** | **The third-person camera crashes the game.** `0xc0000005`, reading `0xffffffffffffffff`, at an address **inside the payload**, right after "[cs2-features] third person: the camera update is hooked". The cause is the **`thread_local` re-entrancy guard in the detour**: the payload is **manually mapped**, the loader never runs for it, the image's TLS index is **154** (above the 64 inline slots `cs2::tls::ensure_thread_tls` supports — it returns at `bootstrap.cpp:49`), so the guard's read dereferences uninitialised TEB memory. **The address is *not* stale**: `client.dll + 0xB62A20` is a valid 3789-byte function on the live build. | `payload/features/src/thirdperson.cpp:39` | **`feat/t27-thirdperson-crash`** (the fix round is in review; **until it is merged the camera must stay off**) |
| **C2** | major | The replacement guard (the per-owner one) had **two blockers** in review: the refusal depended on the slot scan order (a same-thread recursion could slip through) and a double claim leaked a slot, so a thread that was **not** recursing could be refused forever. The shipped 8-thread test was **flaky** (49 failures in 3000 runs). | `payload/features/include/cs2/features/recursion_guard.hpp` | **`feat/t27-thirdperson-crash`** (the fix round: a two-pass `try_enter`, `compare_exchange_strong`; 3000 rounds with 0 failures) |

---

## 3. The animfix's estimator — fixed on a branch, not yet merged

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **A1** | **blocker** | The sub-floor filter let a **single late sample** become the whole estimate, so the staleness bound inflated to **250 ms** and a **245 ms old** server value was written over the client's fresh prediction (the pre-fix code refused the same capture after 7 ms). The "the median discards the outliers — in both directions" claim in `docs/re/animfix.md` §10.1 was **false**. | `payload/control/include/cs2/control/animfix.hpp` | **`feat/t20b-animfix-estimator`** (in review) |
| **A2** | major | The sample window was **sticky** (`count` doubled as the ring index and was capped at the window size, so only slot 0 ever changed): the estimate never followed the server's rate, and the animfix went inert when the rate changed (duty cycle 99.8 % → 62.6 % at 20 Hz). It also made the round's "burst" scenario unreachable in the live payload. | `animfix.hpp` (`observe_decode`) | **`feat/t20b-animfix-estimator`** (in review) |
| **A3** | major | `docs/re/animfix.md` §10.1 stated the **opposite** of the code in three sentences. The file is append-only: the fix appends a dated §11 that corrects it. | `docs/re/animfix.md` | **`feat/t20b-animfix-estimator`** (in review) |
| **A4** | minor | The duty cycle was computed from counters with **mismatched windows** (the denominator counted every tick since the payload started) → the scenario reported 0.0 % for a feature that was working perfectly. | `scripts/scenario/scenarios/animfix.py` | **merged** (`14a64e1`) |
| **A5** | minor | The install path **logged a line on every tick** when the calibration dump was stale (~500 MB/day). | `payload/control/src/input.cpp` | **merged** (`14a64e1`) |
| **A6** | minor | The scenario's fallback to an older payload's counter was **silent** (two different denominators, no marker). | `animfix.py` | **`feat/t20b-animfix-estimator`** (in review) |
| **A7** | minor | The refusal counters mixed per-tick and per-slot units; the status's comment asserted a unit the code did not keep. | `input.cpp`, `service.cpp` | **`feat/t20b-animfix-estimator`** (in review) |
| **A8** | nit | The reported snapshot interval was `0` for a server faster than the floor — ambiguous with "not observed yet". | `animfix.hpp` | **`feat/t20b-animfix-estimator`** (in review) |
| **A9** | nit | One of the three new burst tests was **green against the pre-fix code** (it only exercised the initial fill). | `test_control_animfix.cpp` | **`feat/t20b-animfix-estimator`** (in review) |

---

## 4. The prediction's copy — the defect was latent, and the fix is in review

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **P1** | minor | The prediction's copy buffer was a `thread_local` — on a manually mapped image that can never get a block (the TLS index is 154). **The defect was latent, not a live outage**: `Input::simulate_landing` has **no call site** in the tree, and `predict_` defaults to false. The buffer is now the caller's stack local. | `payload/control/src/input.cpp` | **`feat/t30-prediction-tls`** (in review) |
| **P2** | major | The source check that guards "no `thread_local` in the control module" has **three reproducible false negatives**: a C++ **digit separator** (`1'000`) makes the stripper discard the rest of the file, `__declspec(thread)` is not the word it looks for, and a **raw string** containing a `"` leaves it inside a string. | `payload/control/tests/test_control_prediction.cpp` | **not fixed** — deferred |
| **P3** | major | The property "a failed read leaves the engine's object alone" is asserted on a **model of the caller** written inside the test, not on the caller: a change in `input.cpp` that restores unconditionally would write 4064 zero bytes over the engine's `CCSPlayer_MovementServices` and the test would stay green. | `test_control_prediction.cpp` | **not fixed** — deferred |
| **P4** | minor | The restore writer is a raw `memcpy` although the payload has a fault-safe `cs2::game::write_bytes`; and `writable()` proves **readability**, not writability — the comment says otherwise. | `input.cpp` | **not fixed** — deferred |
| **P5** | nit | The check's scope is `payload/control` while the document states the rule as "no `thread_local` in the payload" (one module out of seven). | the test, `docs/re/prediction.md` | **not fixed** — deferred |
| **P6** | nit | `predict_snapshot_failed_` latches forever, so only the first failure in the payload's lifetime is ever logged. | `input.cpp` | **not fixed** — deferred |

---

## 5. The chams — the feature and its guard

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **H1** | blocker (historical) | The draw hook's target came from an RTTI **lambda** type; a lambda's `operator()` is static, so the nine-argument detour shifted the whole argument list and the game crashed with `0xc0000005`. The real target is `scenesystem.dll + 0x580A0` — a nine-argument member function whose signature is **exactly** the payload's existing typedef (the material at index 3). | `payload/features/src/chams.cpp` | **merged** (the signature, the pure decision function, the prologue verification) |
| **H2** | major | The re-entrancy guard was **process-wide** and its early return **drops the draw**. The scene system renders the draw list on the **tier0 thread pool** (`g_pThreadPool`, `Thread_RenderSceneDrawList`, `"rendering in %i jobs"`, `sc_disableThreading`), so two jobs can be inside the detour at once. | `chams.cpp` | **`feat/t26-chams-draw-hook`** (the fix round: a per-owner guard, held across the trampoline call) |
| **H3** | minor | After a refusal the status stuck at "installing" forever and the refusal was never re-reported (`install_failed_` was never cleared). | `chams.cpp` | **`feat/t26-chams-draw-hook`** (the fix round) |
| **H4** | minor | `status_line()` allocated a `std::string` **on the game's render thread, every frame**. | `chams.cpp` | **`feat/t26-chams-draw-hook`** (the fix round) |
| **H5** | nit | Two new public accessors were dead code; the module-identity check skipped an empty module string; `describe(installed)` returned `"on"`; the counters were not reset on toggle. | `chams.cpp` | **`feat/t26-chams-draw-hook`** (the fix round) |

---

## 6. The offline offsets — fixed and merged

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **O1** | major | The write path **deleted** the entries of the signatures that stopped matching and **exited 0** — the exact failure the script exists to prevent. It now refuses (with an explicit `--allow-unresolved` escape hatch). | `scripts/resolve-offsets-offline.py` | **merged** (`780cc21`) |
| **O2** | major | The 19 new tests were not wired into `meson test`; the new script was not in the mypy gate; the merge test could not fail and `main()` was untested. | `meson.build`, `scripts/typecheck.py`, the test file | **merged** (`780cc21`) |
| **O3** | minor | A bad `--dumper` path produced a traceback; nothing cross-checked that the offsets came from the module they claim; `compare_offsets` ignored the `source` field; `--module` was a no-op in file mode. | the script, `main.rs` | **merged** (`780cc21`) |

---

## 7. The quality pass — fixed and merged

`docs/quality-pass.md`'s handle counts did not follow from the method printed
next to them (14 vs 15); the workflow ran on a moving label while documenting a
pinned image; the workflow header overstated what the payload step compiles;
the tables labelled their column "the tip" when the tip had moved; one line was
101 columns; the new test was not clang-format-clean. **All fixed and merged**
(`9d1f83d`).

---

## 8. What is deliberately out of scope

* **The protection workstream** (`docs/handoff-protect.md`) — the operator's
  decision (2026-10-21): not a task for this repository. The T18/T19 work
  that already landed stays.
* **`cpp-modernization`** — C++23 (so it is not "cpp2"), but **252 commits
  behind**, its **ADR numbering collides** with the integration tree's (its
  ADR-0023 "Enumerated domain types" against the integration tree's ADR-0023
  "Default renderer"), and it rewrites the same ~90 files. Kept as a **source
  of ideas**; merging it is its own project.
* **The anim-state divergence** and the **frame ordering** — open research
  questions, recorded in `docs/re/animfix.md` §9.
* The **camera split's code** (on `feat/t18b-neverlose-camera-split`, 114
  commits behind): the **RE and the two dumper signatures** are merged; the
  hooks are a separate task. Without them the anti-aim's camera split does not
  hold — expect the "camera follows the fake yaw" probe to fail.

---

## 9. How to use this file

* **Adding a finding:** when a review or a live run reports one, add a row
  with its severity, where it lives and the branch that fixes it (or "not
  fixed — deferred").
* **Removing a finding:** only when the fix is **merged and re-reviewed** —
  then delete the row and note the merge in the commit.
* **New work:** pick the findings that block a feature the operator wants, and
  write the task's specification into `docs/orchestration/` as the other tasks
  did.
