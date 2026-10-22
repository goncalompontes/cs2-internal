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
| **L1** | **major** | **The animfix never captures the server's graph inputs.** The anim-tick hook fires (1542 client ticks) but the codec captured **nothing** (0 captures, 376 skips, all `no_capture`, `snapshot_interval_ms: 0`). | `payload/control/src/input.cpp` (the capture), `external/dumper/src/analysis.rs` | **merged** (`166c19d`) — the counters made the cause visible: **the hooked codec never runs at all** (0 calls), for the local player and for everybody. The "twin" `0x8daab0` is **not** a second codec — it is the class's **schema registration** (it walks the field list and saves the names into the chainer's table; the class-name getter right before it returns `"CCS2PawnGraphController"`). The three byte-identical siblings are the registrations of three classes. So there is **one** codec and the payload already hooks it — the client's snapshot decode simply does not go through it |
| **L1a** | major | **Where the local player's graph inputs come from is still unknown.** Either the client decodes them through a path the payload does not hook, or **the local player is fully predicted** and the server's values for it are simply not on the client. The next step is a **measurement** (name the function that decodes them) — or re-basing the animfix on the interpolated `m_angEyeAngles` (`pawn + 0x3600`), which `docs/re/animfix.md` §5 (target 1) already names as the source of the server's aim yaw. | the animfix's seam | **not fixed — deferred** (it needs a new RE pass) |
| **L5** | minor | The dumper's own test `curated_signatures_resolve_offsets_offline` is **red in the merged tree**: it builds a synthetic buffer and asserts that *every* curated signature resolves, so **every signature added after it was written breaks it** — five are unresolved now (`animfix_graph_codec`, `animfix_param_setter`, `animfix_anim_tick`, `eye_angles_copy`, `view_angles_setter`). `cargo test` is therefore red in the tree (the plugin's verify loop does not run it, which is why the merges stayed green). | `external/dumper/tests/` | **not fixed — deferred** |
| **L6** | nit | **The dumper binary in `external/dumper/target/release/` is gitignored and goes stale.** A stale copy there makes the new `scenario-resolve-offsets-offline` suite fail (it runs the real binary and asks it for `--list-signature-modules`, which the old binary does not have) — with the *host* suite otherwise green. The cure: `cargo build --release` and copy the exe over. | the gitignored artefact | **not fixed — deferred** |
| **L7** | nit | **The cs2 MCP server loaded the scenario modules when it started**, so it still runs the pre-merge code: the new `bot-match` flow (the new keys `round`, `waited_s`, `team_joined`) and any new scenario (the hit log's) are invisible to `cs2_run_scenario` until the server is restarted. The repository's own runner picks them up at once: `python scripts\run-scenario.py <name>`. | the MCP server | **not fixed — deferred** |
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

## 3. The animfix's estimator — a fix round exists, but it is **not merged**

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **A1** | **blocker** | The sub-floor filter let a **single late sample** become the whole estimate, so the staleness bound inflated to **250 ms** and a **245 ms old** server value was written over the client's fresh prediction (the pre-fix code refused the same capture after 7 ms). The "the median discards the outliers — in both directions" claim in `docs/re/animfix.md` §10.1 was **false**. | `payload/control/include/cs2/control/animfix.hpp` | **`feat/t20b-animfix-estimator`** — the fix round closed the *specific* trigger (7 ms again ✓) but its re-review returned **DO NOT SHIP** with a **residual of the same class** (see A1a), so the branch is **not merged** |
| **A1a** | **blocker (residual)** | The trust rule is **relative to the window's fill** (`2 * filtered >= count`), so while the window holds one or two intervals a **single late interval is still trusted**: the estimate becomes that interval and the bound inflates to 200/250 ms. Measured end to end: the animfix is switched on during a hitch and writes a **245 ms old** capture. The exposure is **once per payload's lifetime** (the clock is only fed while the animfix is on and is never reset) and it is **unreachable today** because the animfix captures nothing at all (see L1). | `animfix.hpp:309-312` | **not fixed — deferred**; see the decision in §10 |
| **A2** | major | The sample window was **sticky** (`count` doubled as the ring index and was capped at the window size, so only slot 0 ever changed): the estimate never followed the server's rate, and the animfix went inert when the rate changed (duty cycle 99.8 % → 62.6 % at 20 Hz). It also made the round's "burst" scenario unreachable in the live payload. | `animfix.hpp` (`observe_decode`) | **`feat/t20b-animfix-estimator`** — **fixed** (the window slides ✓, the estimate follows 64 → 10 Hz ✓); not merged with the branch |
| **A3** | major | `docs/re/animfix.md` §10.1 stated the **opposite** of the code in three sentences. The file is append-only: the fix appends a dated §11 that corrects it. | `docs/re/animfix.md` | **`feat/t20b-animfix-estimator`** — §11 corrects §10.1 ✓, but §11 itself carries **two new false sentences** of the same class ("it is never an inflated one", "the 245 ms old capture is gone") — measured false by the re-review. Not merged with the branch |
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
| **H2** | major | The re-entrancy guard was **process-wide** and its early return **drops the draw**. The scene system renders the draw list on the **tier0 thread pool** (`g_pThreadPool`, `Thread_RenderSceneDrawList`, `"rendering in %i jobs"`, `sc_disableThreading`), so two jobs can be inside the detour at once. | `chams.cpp` | **fixed** by the per-owner guard (the branch's fix round) |
| **H2a** | **major** | **The per-owner guard exposes a desc-cache race.** The guard lets two jobs into the draw path *by design*, but the desc cache is published with three separate atomics (the desc last) — a protocol that is safe for **one** writer and not for two. Two descs that hash to the same slot (`(desc >> 4) & 63` — routine with hundreds of models) can leave a reader seeing **desc A with entity B**, so the chams are decided on the **wrong player** (the CT's model chamsed, the T not). The reviewer's probe of the exact protocol reproduced **965–3048** such mismatches per run. `desc_cache_stamp_` is a second, simpler race (a plain `std::uint64_t`). The branch's own comments now contradict each other (`chams.cpp:202-210` says two threads may be inside; `chams.hpp:256` says "the render thread owns the cache"). | `payload/features/include/cs2/features/chams.hpp` (the `DescEntry`), `payload/features/src/chams.cpp` (the publish protocol) | **fixed on `feat/t33-chams-race`** — the cache is a **per-slot seqlock** with the generation inside the entry (`chams.hpp`'s `DescCache`), and the probe is `payload/features/tests/test_chams_desc_cache.cpp` (four threads, two descs in one slot: **zero** mismatches; the pre-fix protocol is reproduced in the test as the control and reports hundreds). The writer's claim is ordered before the entry's writes by a release fence (`a4493ba`), the reader's give-up path is measured (`c239bb2`). **Not merged** — see the note below the table |
| **H3** | minor | After a refusal the status stuck at "installing" forever and the refusal was never re-reported (`install_failed_` was never cleared). | `chams.cpp` | **fixed on `feat/t33-chams-race`** — the switch-off clears the latch (`chams.cpp`'s `configure()`), and the test now pins **the value that is stored** — the switch's own answer — not merely that the rule is mentioned nearby (`fc5c8ba`); the neutered call now fails the suite |
| **H4** | minor | `status_line()` allocated a `std::string` **on the game's render thread, every frame**. | `chams.cpp` | **fixed** by the fix round (`snprintf` into the caller's buffer) |
| **H5** | minor | Two new public accessors were dead code; the module-identity check skipped an empty module string; `describe(installed)` returned `"on"`; the counters were not reset on toggle. | `chams.cpp` | **fixed** by the fix round |
| **H6** | minor | **Nothing pins that the guard is held *across* the trampoline call** — moving the scope after the original leaves the suite green, and that is the guard's entire purpose (the recursion it breaks happens *inside* the call to the original). | the test | **fixed on `feat/t33-chams-race`** — `test_chams_draw_hook.cpp` fails when the scope moves after the call (the branch's own test, `440fe4b`) |
| **H7** | minor | The shared guard's **fail-open** rationale is the camera's ("refusing would drop a camera update"); for the chams the arithmetic is the other way round: a refusal drops **one primitive**, while failing open lets the recursion run until the stack overflows (`0xc00000fd`) and takes the renderer with it. | `recursion_guard.hpp` | **fixed on `feat/t33-chams-race`** — the policy is **per instance** (`RecursionGuard::Policy`): the chams' guard fails closed, the camera's keeps failing open and `thirdperson.hpp` is untouched. The **pool's size** is per instance too: the draw path's pool holds 64 concurrent render jobs, so the tenth job's draw is not refused (`3ad442e`). The refusals are counted **by cause** (`rec=`, `pool=`, `tdn=`, `88eb291`) |
| **H8** | nit | `docs/re/chams.md` names `test_chams_guard.cpp`; the file is `test_chams_draw_hook.cpp`. The shared header cites `docs/re/thirdperson.md`, which arrives with T27. | the docs | **fixed on `feat/t33-chams-race`** — `docs/re/chams.md` names the file that exists, and the shared header's citation resolves |

The rows marked **fixed on `feat/t33-chams-race`** are fixed **on that branch**, and
the branch is **not merged**: this file's rule (§9) is that a row is deleted
only when the fix is merged **and re-reviewed**. So they are annotated here
and the deletion is left to the orchestrator after the merge. The branch's
own commits are listed in `docs/re/chams.md` §10.

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

---

## 10. Decisions taken by the orchestrator while the work moved on

**2026-10-21 — the animfix's trust rule: the *fixed* minimum wins.** The
re-review of the estimator fix found that the specification's two requirements
conflict: "the six §9.6 duty cycles stay identical to the digit" and "a
single late sample must never become the estimate". The fix round resolved it
in favour of the numbers (a rule relative to the window's fill). **The
decision: the validity rule wins** — the numbers move. The cost is measured:
with a **fixed** minimum of four samples at or above the floor, the "perfect"
duty cycle goes from **99.7717 % to 99.4292 %** (−0.34 %) and the fill's
oldest capture written stays at **7 ms** instead of 245 ms. A quarter of a
percent of a duty cycle is a rounding error; a quarter of a second of stale
pose is a correctness violation, and it is the one the animfix exists to
prevent. The branch's §11 must then say so, and the two sentences the
re-review falsified must be corrected.

**2026-10-21 — the third-person camera stays off until T27 is merged.** The
merged tree still carries the camera detour's `thread_local`; the camera's
hook is the **live** crash. T27's fix round exists (two-pass guard, 3000
rounds with 0 failures) and is in review; until it lands, the camera must
not be switched on.

**2026-10-21 — the bot match (T31) was merged without an adversarial
review**, because the operator stopped the agent budget to move on to new
features. It carries 12 unit tests (8 of them red against the pre-fix code),
a live run, and an orchestrator read of the diff. It is the only merge in the
tree without a review, and it is recorded here so that nobody assumes
otherwise.

**2026-10-21 — the protection workstream and `cpp-modernization` are out of
scope** (see §8); the findings in the tree that belong to them stay as they
are.

---

## 11. The next session's work (the operator's list, 2026-10-21)

Two items, in the operator's order. **Neither has been started** — this
section is the plan, not the work.

### 11.1 Fully fix the chams

> **Status (2026-10-21):** the whole recipe below is implemented on
> `feat/t33-chams-race`, which is **not merged**: the desc cache is a
> per-slot seqlock with the generation inside the entry (**H2a**), the
> latch's wiring is pinned — and the *value* it stores with it (**H3**), the
> guard's scope order is pinned (**H6**), the fail-closed policy and the
> per-instance pool are in (**H7**), and the doc's file names are right
> (**H8**). The rows in §5 stay until the merge and the re-review; see
> `docs/re/chams.md` §10.

The feature is in place (the target is resolved through a dumper signature and
verified against the prologue ✓) and the guard is per-owner ✓. What is left
is the **race the per-owner guard exposed** (**H2a**) and the test gaps around
it (**H3**, **H6**, **H7**, **H8**).

The recipe the review measured and the fix round's own review confirmed:

1. **The desc cache must be safe for two writers.** The options the review
   gave: publish the whole entry as one word (the desc pointer and the entity
   do not fit in 64 bits together, so a **seqlock** — a per-slot sequence that
   is odd while a writer is inside and even when stable — is the natural
   form), or give each slot a generation and re-check it after the fill. The
   reviewer's probe (the protocol copied verbatim, four threads, two descs
   colliding in one slot) is the **acceptance test**: it must report **zero**
   mismatches.
2. **`desc_cache_stamp_` must be atomic**, and `g_active` and
   `hooks::InlineHook::trampoline_` (written by the payload's thread, read by
   the render threads) for the same reason.
3. **The counters' reset** must not break `seen == chams + skip`: three
   separate stores while the render thread increments them break the relation
   for the width of that window. The cheapest honest answer is to **not reset
   them** (the operator reads the difference between two `[state]` lines).
4. **The tests must pin what the fix is for**: the latch clear's *wiring*
   (deleting it must fail the suite) and the **order** of the guard's scope
   relative to the trampoline call (moving the scope after the call must fail
   the suite — that is the guard's whole purpose).
5. **The fail-open rationale must be revisited for the draw path** (H7): for
   the camera, refusing is worse than failing open; for the chams it is the
   other way round — a refusal drops one primitive, a fail-open recursion
   kills the renderer.

### 11.2 Remake the ragebot on the community's Monte-Carlo approach

**The existing implementation stays** — the new solver goes alongside it on a
**new branch** (the operator's instruction), so the current damage-field
solver remains available and comparable.

The repository has already done the research, and it is the right starting
point — `docs/research/aim-objective-math.md` §2 (what the community does)
and §3 (the engine's actual spread kernel):

* **velocity** (CS2, all rights reserved — *read it for ideas, do not copy
  code*) computes the hitchance by **Monte-Carlo over 64/128/256 seed
  indices**, calling the engine's own `weapon_calculate_spread(seed+1, …)` and
  a **ray-vs-capsule** test, with the engine's `trace_bullet` for the real
  penetration. The ordering is **lexicographic**: can-kill first, then the
  highest probability of any hit, then the hitgroup preference.
* The engine's own kernel is **assembly-verified** in `docs/re/aim.md`
  (`FX_FireBullets` → `FUN_180d23460`, RVA `0xd23460`):
  `RandomSeed(seed)`; per pellet `r1, a1` (spread) and `r2, a2` (inaccuracy);
  `off = (r1·cos a1 + r2·cos a2, r1·sin a1 + r2·sin a2)` in the (right, up)
  tangent frame. The support is exactly `|off| ≤ S + I` and the radial law is
  **uniform in radius** (rim-heavy) — the two facts a Gaussian blur gets
  wrong.
* The research's own caveat, which the new solver must respect: "the 256-sample
  loop is **not** a Monte-Carlo over the *actual* shot*" — it averages over
  hypothetical seeds, and the **seed is a function of the angles** (velocity's
  `find_spread_correction` searches for an angle whose seed produces a better
  offset). So the seed the sampler uses is the decision, and the repo's own
  RE on the spread cone is the authority for it.
* The repo's pieces that the new solver can reuse: `payload/aim/spread.hpp`
  (the spread model), `payload/aim/hitbox.hpp` (the capsule test),
  `payload/aim/occlusion.hpp` and `penetration.hpp` (the real trace), and the
  host benchmark over a saved cell set (`docs/ragebot-perf.md` §3.9) for the
  cost — the current cone objective is 20–35 ms for 717 cells, so a
  sample-based objective has to be counted carefully.

The first decision for the next session: **the seed**. Is the new solver's
hit chance the probability over the *shot that would be taken* (the seed the
engine would use for that pose) or the expectation over seeds? The community
does the first, and the research says that is what works in practice.

### 11.3 What the session closed with

**Merged into `feat/occlusion-penetration`** (each with its review):

| task | what landed |
|---|---|
| T20 (animfix) | the capture/re-apply seam, the duty cycle, the latched log |
| T21 (quality pass) | the RAII pass, the CI workflow, the report |
| T28 (offline offsets) | the offline resolver, the gates, the refusal to write a degraded dump |
| T30 (prediction copy) | the caller's buffer instead of the `thread_local` |
| T31 (bot match) | the team first, the live round last |
| T27 (camera guard) | the two-pass guard, no per-frame module lookup, `docs/re/thirdperson.md` |
| T26 (chams) | the draw hook's target resolved through a dumper signature and verified against the prologue; the per-owner guard; the allocation-free status line |

**Parked on their branches, not merged:**

| branch | why |
|---|---|
| `feat/t20b-animfix-estimator` | the re-review returned **DO NOT SHIP** (the residual in A1a) — the gate refuses a DO NOT SHIP verdict, and the branch's own documentation carries two false sentences (A3) |
| `feat/t29-hit-log` | the review **never finished** (the report is a 416-byte stub) — a large feature with no adversarial review is parked, not landed |
| `feat/t31-…` (merged, unreviewed) | see §10 — the only merge without a review |

**The live test is ready.** The offsets are refreshed (**29**: 26 `client.dll`,
2 `engine2.dll`, 1 `scenesystem.dll` — the chams' `scene_draw_primitive` at
`0x580A0` is in there, so the chams are no longer dormant), the payload is
built in the injected configuration (`/MT`, importing exactly `KERNEL32`,
`USER32`, `D3DCOMPILER_47`), and the injector is built. The game is up with a
bot match in it.

---

## 12. The Monte-Carlo objective in the live path (2026-10-21)

> Added with task T35 (`docs/orchestration/spec-t35-montecarlo-live.md`). The
> core of §11.2's plan is in the live path: `AimObjective::monte_carlo` is
> selectable from the control channel and from the per-weapon profile, so the
> operator can live-test it against the damage-field solver. **The live run is
> still the operator's** — nothing below has been exercised in the game yet.
>
> The four existing objectives (`damage`, `hitchance`, `expected`, `pellets`)
> are unchanged, and the host test proves it against the field's own optima.

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **M1** | minor | **The live path's seed sequence is a fallback, not the game's.** The command's sequence number — the thing `MD5_PseudoRandom(sequence)` is derived from (`docs/re/aim.md` §"The seed") — is **not reachable from where the service sits**: the `Input` module owns the `CUserCmd` pointer and exposes no accessor for the command's number, and the one that exists (`bhop_cmd_number`) is refreshed by the **bunnyhop assist** alone, so it is zero when the assist is off and stale when it is on. The live path uses the **prediction tick** (the local controller's `m_nTickBase`) as the documented fallback, and the status says so (`mc_seed_fallback: true`, `mc_seed_note` names the reason). | `payload/control/src/service.cpp` (`prediction_tick`) | **not fixed — deferred** (it needs an accessor in the `Input` module, which this task does not own) |
| **M2** | minor | **The `angle_grid` seed's hash is a stand-in.** The engine's `get_tick_view_angles` is not reverse-engineered, so the hash is *ours*: the numbers that policy produces are **indicative, not exact**. The status says so. | `payload/aim/src/montecarlo.cpp` (`angle_seed_hash`) | **not fixed — deferred** (it needs RE) |
| **M3** | minor | **The Monte-Carlo objective scores the hitbox geometry only.** The repo is Tier-2: the world is projected into the chart, not traced, so whether the shot is *visible* (or penetrates a wall) is still the existing machinery's job — the `only_visible` gate and the `penetrated` flag. | `payload/aim/src/solver.cpp` | **by design** (`docs/ragebot.md` §13.7) |
| **M4** | minor | **The `min_hitchance` and `min_damage` gates change meaning under the new objective, and not in the same direction.** The same `min_hitchance` passes **more** shots (the old Gaussian mass's σ is √6 too wide — it reports **0.221** where the truth is **0.777**), and the same `min_damage` blocks **more** shots (the Monte-Carlo's value is the **expected** damage: a 50 % hit chance on 100 damage reports **50**, not 100). A ragebot that fires under `hitchance` and does not fire under `monte_carlo` with the same thresholds is the semantics change, not a defect. | the gates (`payload/control/src/service.cpp`) | **by design** (`docs/ragebot.md` §14.2) |
| **M5** | minor | **The Monte-Carlo objective and the backtracking pass do not fit the frame budget together.** One Monte-Carlo solve is ~4.3 ms at a live-sized hitbox set, and the backtracking pass is a **full solve per pose**, so the two together exceed the 12 ms guard. | the backtracking pass | **not fixed — deferred** (the operator turns the backtracking off while live-testing; the sample count of **64** was chosen for the single-solve budget) |
| **M6** | nit | **The harness's `AimObjective` type does not know the new name.** `scripts/scenario/protocol.py`'s `Literal["damage", "hitchance", "expected", "pellets"]` becomes the MCP `cs2_rage` tool's JSON schema, so the tool refuses `"monte_carlo"` before the request is sent. The scenario library's `game.rage(objective="monte_carlo")` works (the annotation is not enforced at run time). | `scripts/scenario/protocol.py` | **not fixed — deferred** (one line: add the name to the literal) |
| **M7** | nit | **The `√6` σ correction is still not applied.** It is blocked on the `sqrt(r)` RE question and on the inaccuracy's units. It does not affect the Monte-Carlo objective (the kernel is sampled, not blurred), but it is why the *old* objective's numbers are not comparable with the new one's. | the field's blur | **not fixed — deferred** (see §11.2) |

**What is verified:** the host tests — `aim-solver` (the objective, its
fallback, and the four other objectives' unchanged behaviour), `aim-montecarlo`
(the core, including the two gaps T35 closed), `control-rage-objective` (the
names, the values, the seed policy, the weapon's pattern) and
`control-rage-config` (the profile's round trip and the garbage byte).

**What is not:** the live run. The operator's steps are in
`docs/operator-live-check.md` §6.

---

## 13. The T35 review round (2026-10-22)

> Added with the fix round of task T35
> (`docs/orchestration/spec-t35-montecarlo-live.md`). The adversarial review
> of T35 found **no blocker**, four majors and a dozen minors; this section
> records what each finding was and what the fix round did with it. **The
> live run is still the operator's** — nothing below has been exercised in the
> game.

| # | severity | finding | where | fix |
|---|---|---|---|---|
| **M1** | major | **The "the four other objectives are unchanged" regression test was vacuous.** Its scene was one symmetric sphere, so every objective's optimum was the same point: pointing `damage` at the hitchance search (or the reverse) left the whole suite green. | `payload/aim/tests/test_aim_solver.cpp` | **fixed** — the scene is two cells (a big body and a small, higher-multiplier head off-centre) chosen so both clear the hitchance objective's value floor; the block now asserts that the optima **differ** from each other, and the Monte-Carlo's fallback is pinned to the `damage` objective's aim (and explicitly not the hitchance objective's) |
| **M2** | major | **The seed-fallback disclosure had no test at all.** Inverting `seed_source_is_fallback` and replacing `seed_source_note`'s body with "the numbers are exact" left the suite green, so the operator could be told that a stand-in number is the engine's own seed. | `payload/control/include/cs2/control/rage_objective.hpp` | **fixed** — the test pins both flags, both notes (by the word the operator has to read) and that the two policies carry different notes |
| **M3** | major | **The `aim` command's Monte-Carlo path could never report a target or a kill.** The health list carried the entity index while the hitboxes kept `Hitbox::target`'s default -1, so the core's can-kill lookup never matched: `mc_target` was always -1 and `mc_can_kill` always false. | `payload/aim/src/reader.cpp`, `payload/control/src/service.cpp` | **fixed** — the reader stamps the hitboxes with the index of the target it chose, and the health entry is built from that same index by a pure rule the host test pins |
| **M4** | major | **The `aim` command's Monte-Carlo path built a degenerate kernel whenever the caller stated any weapon argument.** The weapon was read from the caller's profile alone, so the spread and the inaccuracy stayed 0 and the type stayed unknown: every sample landed on the aim (a "measured probability" of 1.0) and an unknown type counts as "modelled", so a shotgun would have been scored with the standard pattern. | `payload/control/src/service.cpp` | **fixed** — the weapon is always read from the game and the caller's damage model is overlaid on top; the `pellets` branch's spread hole is closed with it |
| **N1** | minor | **The MCP `cs2_aim`/`cs2_rage` tools refused `"monte_carlo"`.** The `Literal` becomes the tools' advertised schema and the server validates against it before the request is sent. | `scripts/scenario/protocol.py` | **fixed** — the literal carries the name, the test reads the schema from the tools themselves and checks that the validator accepts it. **The running MCP server has to be restarted** to advertise it |
| **N2** | minor | **The Monte-Carlo's live state was published as seven independent relaxed atomics**, so a reader could mix two ticks — and the inverse **hid the refusal entirely**. | `payload/control/src/service.cpp`, `service.hpp` | **fixed** — the verdict is published as **one value** (a mutex), so a reader never sees a half-published verdict |
| **N3** | minor | **The Monte-Carlo's state was never cleared on the early returns**, so the status could show a stale verdict under another objective's name. | `payload/control/src/service.cpp` | **fixed** — the verdict is cleared at the top of the tick, so every early return leaves nothing stale |
| **N4** | minor | **The hitbox filter drops malformed hitboxes silently**, so the measured probability is a silent lower bound. | `payload/aim/src/solver.cpp` | **fixed** — the count travels with the verdict (`MonteCarloVerdict::filtered_hitboxes`) and the status carries it as `mc_filtered`, so a dropped hitbox is visible |
| **N5** | minor | **The status named the global objective**, but with a per-weapon profile the solve runs the profile's objective. | `payload/control/src/service.cpp` | **fixed** — the status names the **effective** objective and reports the global one under `objective_global` |
| **N6** | minor | **The status lacked `mc_can_kill`/`mc_hitgroup`/`mc_hits`, and the `aim` payload lacked the seed's disclosure.** | `payload/control/src/service.cpp` | **fixed** |
| **N7** | minor | **The new code was not clang-format clean** (`service.cpp` 0 → 14 deviations, `solver.cpp` 3 → 10, …). | the touched files | **fixed** — the files this task touched are formatted; the count is **0** for every one of them |
| **N8** | minor | **The weapon type came only from `read_weapon_profile`**, which fails when the VData or the damage read fails; the type then stayed -1 and `pattern_supported` returned true, so a shotgun would be scored with the standard pattern. | `payload/control/src/service.cpp` | **fixed** — the snapshot's independently-read type is the fallback, in both the rage path and the `aim` path |
| **N9** | minor | **The backtracking budget was not surfaced**: the operator saw the backtrack counter stop incrementing with no explanation. | `payload/control/src/service.cpp` | **fixed** — `backtrack_over_budget` is in the status, and the operator's checklist says what to look for |
| **N10** | minor | **The HUD line carried the objective's name but none of the Monte-Carlo's numbers, the refusal or the seed disclosure.** | `payload/control/src/service.cpp` | **fixed** — the line carries the marker (`mc83%` / `mc-refused`); the record and the occluder count moved out of the line (they are in the JSON status), so the line still fits its 128-byte buffer |
| **N11** | minor | **`RageStatusDict.objective` was still declared as an `int`** and the ten new `mc_*` keys were missing. | `scripts/scenario/protocol.py` | **fixed** |
| **N12** | minor | **The `aim` command clamped an out-of-range sample count to 64 while the `rage` command ignored it**, and neither said so. | `payload/control/src/service.cpp` | **fixed** — both **ignore** an out-of-range count, and `mc_samples_note` says what is in force and whether it fits the tick |
| **N13** | nit | **A comment in `rage_config.cpp` invented a bug that was not there.** | `payload/control/src/rage_config.cpp` | **fixed** — the comment says what the code does |
| **N14** | nit | **The `aim` command's `seed` key means an integer seed for `pellets` and a policy name for `monte_carlo`.** | `payload/control/src/service.cpp` | **fixed** — the two are told apart by their type, and both call sites say so |
| **N15** | nit | **`scripts/perf-live.py` swept `objective in (0, 1)`**, which the payload ignores (it takes a name), so the live perf script could not measure the Monte-Carlo's cost. | `scripts/perf-live.py` | **fixed** — the sweep uses the names, and it includes `monte_carlo` |
| **N16** | nit | **The HUD line's buffer is 128 bytes** and the new line was 121 with realistic counters, so the last three `%s` truncated sooner. | `payload/control/src/service.cpp`, `payload/features/src/overlay.cpp` | **fixed** — the line is shorter than it was (the record and the occluder count moved to the JSON status) and the buffer is 192 bytes, so nothing truncates |
| **N17** | nit | **The source check greps `service.cpp` for literals**, so a comment containing one would fail it spuriously. | `payload/control/tests/test_control_rage_objective.cpp` | **fixed** — the check strips the comments first |
| **N18** | nit | **The header said the status is read on the control thread**; it is read on two (the control thread and the render thread). | `payload/control/include/cs2/control/service.hpp` | **fixed** — and that is why the verdict is published as one value |

**What is verified:** the host tests — `aim-solver` (the objective, its
fallback, and the four other objectives' unchanged behaviour **in an
asymmetric scene**), `aim-montecarlo` (the core, including the two gaps T35
closed), `control-rage-objective` (the names, the values, the seed policy and
its disclosure, the weapon's pattern, the target rule and the sample count)
and `control-rage-config` (the profile's round trip and the garbage byte) —
plus the scenario suite (the MCP tools' advertised schema).

**What is not:** the live run. The operator's steps are in
`docs/operator-live-check.md` §6.
