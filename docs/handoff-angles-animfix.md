# Handoff — the angle-split and animfix session (2026-10-21)

> **Start here if you are picking this thread up.** The user asked for two
> things: (1) research how the real ecosystem splits the **camera angles** from
> the **player/eye angles**, and build an **anti-aim** on that split; (2)
> research **animfix / local animfix**, reverse how the server applies
> animations, and apply them to our own player for accurate local hitboxes.
> A third task — a **project-wide quality pass** — was added mid-session.
>
> The one-line version: **the research and the reversal are done and merged**;
> the **anti-aim is merged, reviewed and pushed**; the **animfix** and the
> **quality pass** are in their **final review round** with every fix committed.
> **Nothing has been verified in the live game** — the agent runs in Windows
> session 0 with no GPU, and that is the one gap no amount of static review
> closes.

---

## 1. Where everything is

| what | where | state |
|---|---|---|
| the repository | `C:\Users\gpmpo\cs2-internal` | branch `feat/occlusion-penetration`, tip `9b46fc7` |
| the remote | `github` → `goncalompontes/cs2-internal` | **private**; `master` pushed to `9b46fc7` (`5078b83..9b46fc7`, a fast-forward) |
| the second remote | `origin` → `laptop:/home/pontes/git/cs2-internal.git` | **not pushed** — Tailscale SSH needs a browser check the agent cannot complete |
| the task worktrees | `C:\Users\gpmpo\cs2-wt\<task>` | one per task |
| the review worktrees | `C:\Users\gpmpo\cs2-wt\rev-<task>` | the plugin gives each review its own |
| the review reports | `C:\Users\gpmpo\cs2-wt\.orchestration\reviews\cs2\` | the merge gate reads the verdict from here |
| the plugin | `C:\Users\gpmpo\orchestration-plugin` (source) → `C:\Users\gpmpo\.config\opencode\orchestration` (installed) | **being refactored by another session concurrently** — see §5 |
| the game | Steam, `F:\SteamLibrary\…\Counter-Strike Global Offensive` | the live `client.dll` is sha256 `659940c1…` |

## 2. What is merged (and pushed)

| the deliverable | the file | the merge |
|---|---|---|
| the engine reversal of the angle channels | `docs/re/angle-split.md` | `642c2ae` |
| the community's angle-split shapes | `docs/research/angle-split-in-the-wild.md` | `2fc456d` |
| the engine's animation pipeline | `docs/re/animfix.md` | `d2062a5` |
| the community's animfix vocabulary | `docs/research/animfix.md` | `0dcaead` |
| **the anti-aim** (T19) | `payload/control/include/cs2/control/anti_aim.hpp`, its host test, `scripts/scenario/scenarios/anti_aim.py` | `f7cb46f` |
| the four task specs | `docs/orchestration/spec-*.md` | `71c50a4` … `e539bc6` |

The integration tree at `9b46fc7` is **green**: 45/45 host tests, 409 Python
tests, mypy clean (58 files), and the payload, overlay and injector all
compile.

## 3. What is in flight — and exactly how to finish it

Both branches have **every finding fixed and committed**. Only the final
review is outstanding.

### T20 — the local animfix

* branch `feat/t20-animfix`, worktree `C:\Users\gpmpo\cs2-wt\t20-animfix`
* tip **`9ab7c55`**; it **merges the integration tip** as `67bd9b7` (a merge
  commit, not a rebase — the no-rebase rule), so T19's anti-aim is already in
  the tree
* **five** fix rounds have run; **the final review has NOT**. The 4th review
  (`rev-t20-animfix-r4`, tip `64f3594`) returned **SHIP WITH FIXES** with
  three majors:
  * **M-A** — the **median** bound refused the capture in the second half of
    every gap, so the "the server's value stays in the graph" promise only
    held on a perfectly regular server. The reviewer **measured the duty
    cycle**: 100 % (perfect) → 97.0 % (jitter 16–19 ms) → 82.6 % (25 % of the
    snapshots delayed) → 63.1 % (50 % delayed) → 82.0 % (a 48 ms hitch).
  * **M-B** — `docs/re/animfix.md` §9.6 still documented the **round-3** rules
    (it said "exponential moving average", "at most once", and listed "already
    used" as a refusal reason — all three false) and `:1014` still asserted
    the frame ordering as fact. The document that exists to prevent drift was
    itself the drifted one.
  * **M-C** — the M2 fix was **not pinned by a test that can fail**; the
    commit message's mutation claim was false (the reviewer restored the
    consume-once machinery and the whole suite stayed green).
  * plus the minors m-1…m-7 and the nits n-1…n-4.
* **the 5th fix round has landed** (all committed):
  `f554738` the bound carries a margin and the duty cycle is reported ·
  `218bc55` the capture is pinned as not-consumed · `4fe159e` §9.6 as it is
  shipped · `c53ac68` the write-path failures are counted and the flag is
  named · `9ab7c55` the status has its own mutex and the scenario judges the
  duty cycle
* host suite **46/46**, Python **402**, mypy clean (55 files) — verified

**To finish (the next session's first job):** run the final review —

```powershell
orchestrate.review({commit:"feat/t20-animfix", project:"cs2",
                     spec_file:"docs/orchestration/spec-t20-animfix.md",
                     task:"t20-animfix-r5"})
```

— with a focus on: (a) reproduce the **duty-cycle** experiment (the implementer
claims 99.77 % / 99.75 % / 99.77 % / 99.54 % / 89.95 % for perfect / jitter /
25 % delayed / 50 % delayed / a 48 ms hitch) **and** check the margin did not
reopen M1 (one late decode, one fast decode and a total stall must all still
behave); (b) the reviewer's **mutant E** must now make the suite **red**;
(c) §9.6 read adversarially, sentence by sentence, against the code;
(d) the guard must still be **bit-exact** (the reviewer proved it exact with a
1 000 000-random-bit-pattern differential test against the engine's own
predicate — it must not have changed).

Then `orchestrate.merge({branch:"feat/t20-animfix", project:"cs2"})`. The
merge gate refuses only a **DO NOT SHIP** verdict, so a **SHIP WITH FIXES**
merges.

**The reviewer's proven facts, worth keeping:** the anim tick's flag polarity
is **proven from the live DLL** (`cmoveq %rdx,%rcx` at `0x35af26` ⇒ flag == 0
⇒ server tick, flag != 0 ⇒ client tick); the finite-value guard is **bit-exact**
against the engine's own predicate; the seam test catches **all six** signature
corruptions (the round-3 test was blind to every one); and the merge with the
integration tip is clean with T19 byte-identical.

### T21 — the project-wide quality pass

* branch `feat/t21-quality-pass`, worktree `C:\Users\gpmpo\cs2-wt\t21-quality-pass`
* tip `528fa59`; the round-3 fix commits are
  `48d6ff1` the policy contract says when `close` is asked · `187361d` the
  report names its build directory and the policy's requirements ·
  `8cdef05` §6.2 publishes the command its numbers come from · `b212124` the
  new test's local handle is const · `b824a9e` the clang-tidy table is
  re-measured at the tip · `528fa59` the second re-review is in the report ·
  `c4769f8` **M1 — the policy contract gains `usable()` and the wrapper asks
  it first, so a policy whose default state cannot answer is never called**
  (the reviewer's `std::bad_function_call` → `std::terminate` path is closed)
* **the third review round was in flight and was NOT finished** — the review
  worktree is `C:\Users\gpmpo\cs2-wt\rev-t21-quality-pass-r3` and the report
  stub is `…\.orchestration\reviews\cs2\t21-quality-pass-r3-*.md` (empty). The
  next session should re-run it.
* host suite **44/44**, Python **404**, mypy clean (52 files)

**To finish:** re-run the review —
`orchestrate.review({commit:"feat/t21-quality-pass", project:"cs2", spec_file:"docs/orchestration/spec-t21-quality-pass.md", task:"t21-quality-pass-r3"})`
— then `orchestrate.merge({branch:"feat/t21-quality-pass", project:"cs2"})`.

**The reviewer's own judgement, worth keeping:** the empty-SDK payload build
is worth keeping (it is the only compile gate on the payload, the overlay and
the injector), and the implementer **declined** the checked-in stub SDK
because a frozen stub would rot and quietly stop being a compile gate — the
generator stays, with its crates and dumper build cached. The whole workflow
was re-run on a fresh `git archive HEAD` checkout and every step was green.

## 4. What the reviews found — the argument for the loop

The reviews were worth every round. The ones that mattered:

| round | what it caught |
|---|---|
| T19, 1st | the anti-aim **silently destroyed** the auto-strafe, the ragebot's auto-stop and the `look` command; the re-based axes were written **unsnapped**, so the server's rounding made the operator walk up to **30°** off the camera; the `move_crc` gate's **premise was contradicted by the repo's own measurements**, so the feature shipped inert; the scenario **failed falsely** with the default configuration and its split check was vacuous |
| T20, 1st | the setter seam **cannot be hooked** — its third prologue instruction is a RIP-relative load the relocator cannot rewrite across the ~141 TB delta between the high-mapped module and the low-allocated trampoline. The animfix hooked **nothing** and failed silently |
| T20, 2nd | the hand-replicated write **bypassed the engine's finite-value guard**; the write target came from a **stale capture** and was never re-validated against the live controller |
| T20, 3rd | the staleness bound was an **EMA with no outlier rejection** (one late decode inflated it 8.5×); the once-per-capture gate consumed the correction on whichever anim tick ran first, and the server/client flag was ignored |
| T20, 4th | the **median** bound refuses the capture in the second half of every gap — the measured **duty cycle** falls from 100 % to 63 % under a 50 % snapshot loss, so the jitter the fix was meant to remove is still there; the RE's §9.6 still documented the **round-3** rules (three false statements); the M2 fix was **not pinned by a test that can fail** and its commit message's mutation claim was false |
| T21, 1st | the new CI workflow was **red on the first run** — `external/Vulkan-Headers` is gitignored and nothing fetched it; the CI built the payload `/MD` instead of the `/MT` that is actually injected |
| T21, 2nd | the wrapper still had a path that calls a **moved-from policy** (`reset()` → `std::bad_function_call` → `std::terminate` out of a `noexcept` destructor) |
| T21, 3rd | the wrapper's **default** policy could `std::terminate` the same way — fixed by the `usable()` contract |

**The pattern worth keeping:** the reviewer that breaks the maths and watches
the test fail is worth more than the reviewer that reads it. Several of these
findings were only reachable by *running* something.

## 5. The live-verification gap — the one thing static review cannot close

**Nothing has been run in CS2.** The agent runs in Windows session 0 with no
GPU; the game needs `schtasks /run /tn CS2Launch` (the interactive session),
and then
`build-injector\injector.exe cs2.exe <build-payload\payload.dll> --load-missing`.
**Never inject twice into the same game.**

The reviewer's own words for the animfix: *"M2 cannot be closed statically …
A live run with the animfix on and the player turning would settle it
immediately (the scenario's `max_abs_aim_yaw_delta` and the apply rate are
exactly the right probes) — that is the first thing the operator should
check."*

The payload is built to make that run **self-diagnosing**:

* the animfix reports `animfix_client_ticks` vs `animfix_anim_ticks` — if the
  correction lands in the evaluation the player sees, the server's value
  **stays** in the graph and `max_abs_aim_yaw_delta` stays small;
* `animfix_status` **explains itself** when a hook is missing or the
  calibration dump is stale, so an inert feature is never mistaken for a
  working one;
* the anti-aim reports the **camera yaw next to the fake yaw**, and the
  scenario **fails** if the camera follows — that is the split's proof.

**Before either feature can hook anything in a real build, the calibration
dump must be refreshed** — it is gitignored and build-specific:
`scripts/calibrate-live.py` (or
`cs2-dumper --file client.dll --resolve-offsets --json`). Both payloads say
so in their status rather than failing quietly.

## 6. The concurrent-session hazard

**Another session is working in the same repository at the same time.** During
this session it merged T22 (plugin docs), T23 (agent notes), T24 (dumper
path) and T25 (handoff), and it **refactored the orchestration plugin twice**
— the source moved to `C:\Users\gpmpo\orchestration-plugin` and the tool
signatures changed under us (`orchestrate.merge` now takes `project`, the
review worktree is per-task, `skip_tests` was renamed to `skip`, and a
`fix`/`status`/`verify`/`board`/`cleanup`/`size` tool set appeared).

**Check the current signatures before calling the plugin.** The installed
copy is `C:\Users\gpmpo\.config\opencode\orchestration\` and the source of
truth is `C:\Users\gpmpo\orchestration-plugin\`.

Also note: the plugin's `merge` **verifies the merged tree before it commits**
and **refuses a branch that was never reviewed** — a review report must exist
in `.orchestration\reviews\cs2\`.

## 7. The commands

```powershell
# the build
cd C:\Users\gpmpo\cs2-internal
meson compile -C build ; meson test -C build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-injector
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py

# the orchestration (the loop, with the plugin as it is NOW)
#   plan   -> a worktree + the implementer's prompt
#   review -> a per-task review worktree + the reviewer's prompt
#   fix    -> the findings back to the implementer's worktree
#   merge  -> verifies the merged tree, then commits
# see docs/orchestration/README.md

# the game (session 0 cannot do this; the interactive session must)
schtasks /run /tn CS2Launch
build-injector\injector.exe cs2.exe build-payload\payload.dll --load-missing
```

## 8. What is next, in order

1. **T20: run the final review** (`t20-animfix-r5`) — the focus list is in
   §3; the branch is at `9ab7c55` with every finding fixed and committed;
2. **T21: re-run the third review** (`t21-quality-pass-r3`) — the branch is
   at `528fa59` with every finding fixed and committed;
3. merge both (a **SHIP WITH FIXES** verdict merges; only **DO NOT SHIP**
   blocks);
4. **refresh the calibration dump** (`scripts/calibrate-live.py`) — without it
   neither feature can hook anything;
5. **run the game** and check the animfix's `animfix_duty_cycle` (it should be
   ~1.0) and `animfix_client_tick_seen` (if it is false, the flag's polarity is
   wrong for that build and the animfix cannot work), and the anti-aim's
   camera-vs-fake yaw;
6. push the two merges to `github/master` (a fast-forward each — `master` is
   an ancestor of the integration branch, so no force is needed);
7. the **anim-state divergence** remains open (the client's
   `CCSPlayerAnimationState` is never replicated) — the RE's §8 item 1
   experiment is a hardware watchpoint on `movementServices+0x328`;
8. the **other players** are out of scope for the animfix so far — the RE
   gives the same seams for them.

## 9. A note on the review loop

Both branches went through **four and three** review rounds. The loop earned
its keep — it caught an **unhookable seam** (a RIP-relative prologue the
relocator cannot relocate across the ~141 TB gap between the high-mapped
module and the low-allocated trampoline), a **CI that was red on the first
run**, two **`std::terminate` paths** in the ownership primitive, a **30°-off
walk direction**, a **false engine claim in the RE**, and a **test that could
not fail**.

It also caught the implementer's own **overstated claims** — the RE
documenting rules the code no longer had, and a commit message claiming a
mutation proof that did not exist. Both were fixed by making the **code** and
the **documents** agree.

**The lesson worth keeping for the next session:** the reviewer that *runs*
something — breaks the maths and watches the test fail, restores the old
machinery and watches the suite stay green, measures the duty cycle — finds
what reading does not. Several of the findings above were reachable **only**
by execution.

**And the honest limit:** every one of these findings is **static**. The
features have never run in CS2. The next session's most valuable act is not
another review round — it is a **live run**.
