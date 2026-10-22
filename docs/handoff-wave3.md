# Handoff — session 3 (2026-10-22): the chams' race, the Monte-Carlo ragebot, and the guard

> **This is the entry point for the next session.** It says what landed, what is
> open, what is **wrong** with the two unmerged branches, and how to run the
> live test the operator has been asking for.
>
> The short copy-paste opening prompt is [`NEXT_SESSION_WAVE3.md`](../NEXT_SESSION_WAVE3.md).

---

## 1. Where things are

| what | where |
|---|---|
| the repo | `C:\Users\gpmpo\cs2-internal`, branch **`feat/occlusion-penetration`** |
| the task worktrees | `C:\Users\gpmpo\cs2-wt\<name>`, one per task, each on its own branch |
| the build directories | `C:\Users\gpmpo\cs2-build\<task>\...`, **outside** the worktrees |
| the review reports | `C:\Users\gpmpo\cs2-wt\.orchestration\reviews\cs2\<task>-<sha12>.md` |
| the task board | `orchestrate.status({project: "cs2"})` |
| the game | Steam, `F:\SteamLibrary\...\Counter-Strike Global Offensive` |
| the payload's log | `%LOCALAPPDATA%\cs2-internal\cs2-payload.log` (**70 MB**, append-only) |
| the orchestration config | `C:\Users\gpmpo\.config\opencode\orchestration.json` |
| the plugin's source | `C:\Users\gpmpo\orchestration-plugin` (the installed copy is a copy of it) |

**The integration tree is clean and green.** The verify loop has **ten** steps
now — the `payload tests` step was added this session, so the payload's
**clang-cl-built** tests actually *run* (before, they were only compiled).

---

## 2. What landed (eight tasks, each reviewed then fixed)

| task | what it delivered | review |
|---|---|---|
| **T33** | the chams' desc cache: a **per-slot seqlock** with the generation inside the entry, the release fence, `g_active` / `trampoline_` / `installed_` made atomic, the **trampoline's pages never unmapped** (an unconditional safety property), the draw path's guard **fail-closed** with a **64-slot** pool, the refusals counted **by cause** | re-reviewed, no blockers |
| **T34** | the **Monte-Carlo core**: the engine's own spread kernel, **nearest-hit** semantics (which removes the double-count the Gaussian had), the union over the pellets, two seed policies, the per-target can-kill | **SHIP** |
| **T35** | the **live wiring**: `AimObjective::monte_carlo`, the status's seed disclosure, the refusal, the fall back to `damage` | re-reviewed |
| **T36** | the **ledger** and the **operator's checklist**: the chams' findings closed, the settings-sink finding recorded, the checklist's false sentence corrected | re-reviewed |
| **T37** | the **fire path**: a real **release→re-press edge** for a semi-automatic weapon (the attack bit is *replaced* per command from its contributors, and an **unknown reading never removes** the bit), the gate on the clip / reload / deploy / fire interval, the engine's shots counted correctly | **SHIP** (third review) |
| **T38** | the **spread patterns**: the engine's **draw order** (the spread pair once per shot, the inaccuracy pair per pellet — the old model desynchronised the RNG from pellet 1), both radii transformed, the negev's `k = 3 − recoilIndex`, the shotgun's pattern table, and the **engine's** RNG proven bit-identical to tier0's | **SHIP** |
| **T40** | the **settings ownership**: the control channel's rage settings now **survive** the menu's, and the status names **who owns** each field | re-reviewed |
| **T41** | the **flaky desc-cache test**: 0 failures in 20 runs, 20/20 red against the old protocol, and the shipped cache is **byte-identical** | merged unreviewed (spot-checked by hand) |

---

## 3. What is NOT merged — two tasks, and the order matters

### 3.1 T42 — the guard's recovery (do this first)

* branch `feat/t42-guard-recovery`, head **`1695973`**
* **implemented and verified green — but the review NEVER RAN.** The report is
  a **430-byte empty stub** (`reviews/cs2/t42-guard-recovery-1695973e260c.md`),
  so the board says *do not ship*.
* **What it does**: drops the `noexcept` from the payload's vectored handler
  (`payload/diag/src/crash_handler.cpp`), which is what made the guard
  unusable; adds `payload/diag/tests/test_diag_guard.cpp` (ten deliberate
  faults, each must recover, built with **clang-cl** and **run** from the
  payload's own build); enforces **one guard** mechanically (a test fails if a
  second `setjmp`/`longjmp`/VEH appears outside `crash_handler.cpp`); and
  corrects the documents that claimed a wrong engine call kills the game.
* **Why it is first**: the guard is the payload's **only** safety net for an
  engine call that faults, and it **killed the payload 4 times in 12**
  (see §5).

**To finish it:** `orchestrate.review({project:"cs2", commit:"feat/t42-guard-recovery", task:"t42-guard-recovery", spec_file:"C:\\Users\\gpmpo\\cs2-wt\\t42-guard-recovery\\docs\\orchestration\\spec-t42-guard-recovery.md"})`
→ an adversarial reviewer → a fix round → `orchestrate.merge`.

### 3.2 T39 — the engine trace probe (do this second)

* branch `feat/t39-trace-probe`, head **`fa98422`**
* **re-reviewed: SHIP WITH FIXES** — every finding of the first review (which
  was **DO NOT SHIP**) is fixed and proven. The blocker (the results
  container's layout: the trace's record pointer is at **`+0x08`**, not the
  overlap vector's `+0x10`, and the external-buffer bit is **`0x40000000`** at
  `+0x14`) is fixed, and "the engine never allocates" is now *established*.
* **Two minors remain**: **N-A** a comment promises a refusal the probe does
  not implement (it does not refuse to answer while the world's cache is cold
  — it pays the parse inside the input hook and says so only in the answer), and
  **N-B** the results buffer is **320 records on the stack** and the engine
  appends at `data[count]` with its growth skipped, so a ray crossing more than
  320 solids would write **past** it — stack corruption in the game's input
  path, which the guard **cannot** catch. The "bounded" claim rests on an
  unproven bound.

**It must land AFTER T42, and it must be reconciled.** T42's "one guard" test
**fails** on T39's branch, because T39 still carries its own
`payload/control/include/cs2/control/trace_guard.hpp`. The reconciliation (T42's
report gives it exactly):

1. delete `trace_guard.hpp`;
2. in `occlusion_query.cpp`, drop the include and call
   `cs2::diag::call_guarded(&invoke_trace, &call)` (the same signature);
3. delete `payload/control/tests/test_control_trace_probe_guard.cpp` and the
   root `meson.build` blocks that register it;
4. update `docs/re/bullet-trace.md` and `docs/re/occlusion.md`;
5. re-run the verify loop.

**Do that as a fix round on T39's branch, after T42 has merged.**

---

## 4. The live test — what the operator has been asking for

The operator's plan: stop the bots, look at a wall the ragebot fires through
that is **100 % solid** (so the ragebot did not see it), and compare the
engine's trace against our occluder logic for the same ray.

**The instrument is T39's probe**; it asks the engine its own question
(`TraceLineIntervals`, the call the real bullet makes) and publishes **both**
answers plus the difference.

**The sequence:**

```powershell
# 1. rebuild the merged tree
cd C:\Users\gpmpo\cs2-internal
meson compile -C build-payload        # the /MT configuration the injector needs
meson configure build-payload -Db_vscrt=mt   # if it is not already

# 2. restart the game - the running one has the OLD payload
schtasks /run /tn CS2Launch
#    (never inject twice into the same game)

# 3. inject once
build-injector\injector.exe cs2.exe build-payload\payload.dll --load-missing

# 4. the operator stops the bots and points at the solid wall:
#      game.exec("bot_stop 1"); game.exec("bot_place")
```

**Then the probe.** The invocation is in `docs/re/occlusion.md`; it is a
`trace_probe` control command, and it needs the control channel (the MCP's
`cs2_rage`/`cs2_aim` tools' schema is stale, but `cs2_run_scenario` and the
repository's own runner reach it).

**What to check before believing a result** (T39's report, §"What the
orchestrator must do"):

* `engine.answer_missing == false` — the engine's answer was **read**;
* `difference.engine_answer_usable == true`;
* `engine.faulted == false`;
* `engine.interval_count > 0` **and** at least one record with
  `enter_exit == 1`;
* **a verdict of "agrees" with an empty engine list is impossible by
  construction** — if it ever appears with a non-empty `ours`, that is a probe
  bug, not a result.

**The reading table** (also in `docs/re/occlusion.md`) turns the answer into
the numbered answer to *"where is the world geometry lost?"*:

| the difference says | the loss is at |
|---|---|
| `engine_only` | **3** (shapes parsed but dropped) / **5** (projected, the ray misses them) / **6** (the engine sees something we have no shape for) |
| `ours_only` | the known **over-inclusion** (the enumeration is a body-box overlap), not a loss |
| `agrees` | the geometry is **not** lost on that ray |

**The live run is still owed** for: the chams' detour (it has never run against
the real draw), the Monte-Carlo objective, the animfix's graph capture, and the
**fixed** third-person camera (the *broken* one was observed, on 2026-10-21).

---

## 5. The guard — the most important thing this session found

**The payload's fault guard did not work on the build it ships in, and it
killed the game.**

* `payload/diag/src/crash_handler.cpp`'s vectored handler was `noexcept`. A
  `noexcept` function's frame carries a **C++ exception-handler record whose
  action is `std::terminate`**, so MSVC's `longjmp` — which unwinds the frames
  it leaves — ran that cleanup and the runtime **fast-failed with
  `0xC0000409`**. Because it is a fast fail it **bypasses the vectored
  handlers**, so the guard never recovered.
* **It has fired 12 times**, every one the **tier0 allocator fault**
  (`read addr=0x40` — the fault the guard was written for), and **4 of the 12
  killed the payload**, which then had to be re-injected.
* The fix is **one word** (drop the `noexcept`); T42 implements it, with ten
  deliberate faults recovered on **clang-cl** and ten `0xC0000409` against the
  pre-fix code.
* The **MSVC-built** copy passes on the broken code — which is why the host
  suite could never have caught it, and why the test must be built with
  **clang-cl** and **run**.

**A correction to the record:** three accounts of the log's split existed
(4 died / 3 died / 11 died). The measurement that settles it: the line
**immediately** after a `[crash] guarded` line may be a **stack-frame** line
(the diag handler logs the frames *before* it dies), so the scan must skip
those and then look for `[crash] handler installed`. That gives **4 died, 8
survived**.

---

## 6. The operator's four findings — where each landed

| the finding | the answer | the task |
|---|---|---|
| **"it doesn't compensate for spread patterns"** | it now does: the engine's **draw order** (the spread pair **once per shot**, the inaccuracy pair **per pellet** — the old model desynchronised the RNG from pellet 1 on), both radii transformed, the negev's `k = 3 − recoilIndex`, and the shotgun's pattern table | T38 |
| **"it doesn't take into account if we can actually shoot"** | it now does: the gate reads the clip, the reload flag, the deploy tick and the engine's own next-attack tick, and it **refuses** rather than firing into nothing | T37 |
| **"pistols need an attack on each shot"** | it now does: the attack bit is **replaced** per command from its contributors, so a semi-automatic weapon gets a real **release→re-press edge**, and the **player's own press is never taken away** | T37 |
| **"the main world mesh is not in the occluders"** | **the symptom is real and reproduced**; the **hypothesis was wrong**: the world body *is* in the `EntitiesInBox` results and the payload *does* read its 6 mesh shapes. The repo's own 2026-10-18 note recorded the same symptom and the mesh fix was written but **never live-validated** | T39 (the instrument) — **the fix is still owed** |

**On the fourth:** the repo's own "Still open" list already names the fix —
*"the enumeration is still a body-box overlap; swapping it for the engine's own
`TraceLineIntervals` segment trace (exact same solids, no over-inclusion) is the
**Tier-1 step**"* (`docs/re/bullet-trace.md`). The **measurement** comes first;
the **Tier-1 replacement** is the next task after it.

---

## 7. The orchestrator's decisions (and the corrections)

**Decisions taken this session:**

1. **2026-10-22 — a task may merge over a `DO NOT SHIP` review.** The
   operator asked for the ability to live-test features that still have
   findings: `orchestrate.merge({..., allow_unreviewed: true})` overrides the
   review gate. The **verify loop is not** overridable — the merged tree must
   compile and pass, which is the right line.
2. **2026-10-22 — the ledger's rule is the rule.** A finding is removed only
   when its fix is **merged and re-reviewed**; a fix round **annotates** the
   row, and the orchestrator deletes it after the merge. (T41's row was
   annotated and the delete is still owed — see §8.)
3. **2026-10-22 — the verify loop runs the payload's tests.** The `payload
   tests` step was added to the cs2 project's verify list, because the
   payload's **clang-cl** copy of a test was the only one that could catch a
   clang-cl-only defect.
4. **2026-10-22 — the protection workstream is in the integration branch
   anyway.** A **parallel session** merged it (T20a, T23a, T23b, T24) while
   this session worked, although `AGENTS.md` still calls it out of scope. The
   **operator's call**, not the orchestrator's. It is in the tree; do not
   assume it is unwanted.

**Corrections the implementers forced on the orchestrator** — worth reading
before writing another spec:

* the **pattern band's** `k` **cycles** (`0…N−1,0…`), it is not
  `min(counter, N−1)`;
* the **trace's results container** is **not** the overlap query's: the record
  pointer is at `+0x08` and the external-buffer bit is `0x40000000` at `+0x14`;
* the payload's **log was not empty**: 12 guarded faults, 4 of them fatal;
* a `noexcept` frame is not "nounwind with no unwind info" — it carries a
  **terminate** cleanup, and the runtime's `longjmp` runs it.

**The lesson, stated once: in three separate tasks a careful implementer
re-derived the engine instead of trusting the spec, and the engine was right
every time.** Write the specs from the **live build**, not from a summary.

---

## 8. What is still open

**The two unmerged branches** (§3) — T42 first, then T39's reconciliation.

**The live run** (§4) — owed for the chams' detour, the Monte-Carlo
objective, the animfix's graph capture, and the fixed camera.

**Open findings** (the ledger's rows, and the ones this session added):

* **H12** (§15) — the desc-cache probe's flakiness: **fixed on
  `feat/t41-flaky-desc-cache-test`, merged**. The row can be deleted (the
  merge is `b28e805` and the re-review is `SHIP`).
* **§14's H9–H11** — the other plain-pointer `g_active`s, the never-unmapped
  pages, the unmeasured render-job count. **Not fixed, by decision.**
* **T38's N-A** — the status now reports **"standard"** for every weapon,
  because the revolver's transform is off while the pending attack type is
  unknown. The weapon's *class* is lost from the status. Reported by the
  re-review as a minor; **not fixed**.
* **T39's N-A / N-B** — the comment that promises a refusal the probe does
  not implement, and the **320-record stack buffer** with an unproven bound.
* **The convar signature** — reading `weapon_accuracy_shotgun_spread_patterns`
  needs a **new dumper signature** (ADR-0012). It was reported three times and
  is still owed; it is what keeps the shotgun's pattern refused.
* **The service's spread-pattern wiring** — T38's fix routed the live path
  through `applied_spread_pattern`, and the re-review confirmed it. **Done.**
* **`docs/re/occlusion.md:321`, `docs/ragebot.md:462`,
  `docs/re/chams.md:501`** — still carry the *"a wrong call aborts the
  process, the payload has no exception tables"* claim, which **T42 makes
  false**. The injector registers the `.pdata`, so the guard's `longjmp` works.
  **These three lines are still wrong.**

---

## 9. The playbook (unchanged, and it worked)

`orchestrate.plan` → an implementer sub-agent → `orchestrate.verify` → an
**adversarial** reviewer sub-agent → `orchestrate.fix` → `orchestrate.merge`.

**What this session added to it:**

* **the spec is the contract, and it is checked.** Every review this session
  began with *"does the code do what the spec said"* — and three times the
  **spec** was the thing that was wrong.
* **the reviewer must re-derive, not read.** The good reviews this session
  wrote their own PE readers and disassemblers and checked the claims against
  the **live** `client.dll` (`sha256 5a46a09b…`). That is what caught the
  container's layout, the pattern band's cycle and the guard's mechanism.
* **a finding that is a hole in a *proof* counts.** T38's and T39's first
  reviews found that the *tests* were vacuous (a symmetric scene where all four
  objectives coincide; substring searches satisfied by a neighbouring
  occurrence). **A test that cannot fail is worse than no test.**
* **the review continues after the fix.** Six of this session's tasks went
  through a second, sometimes third, review. **T37 needed three.** The
  merge gate is `SHIP`; a `DO NOT SHIP` needs `allow_unreviewed: true` and a
  note in the report.

---

## 10. Housekeeping

* 11 task worktrees, 1.9 GB, plus the integration tree at 2.0 GB.
  `orchestrate.cleanup({project:"cs2", merged_only:true, apply:true,
  remove_builds:true, prune_branches:true})` reclaims the merged ones.
* `C:\Users\gpmpo\AppData\Local\Temp\opencode` holds ~30 GB of scratch. The
  **REA Ghidra cache** (`rea/`) and the Ghidra projects under
  `C:\Users\gpmpo\ghidra-analysis` are **not** scratch — **do not delete
  them**. The extracted LLVM tree under `Temp\opencode` is a **duplicate** of
  `F:\llvm-sdk\...` (which is complete and is what the build uses); deleting it
  frees 3.9 GB, but a running agent may reference that path.
* The payload's log is **70 MB** and grows ~500 MB/day on a busy day; it has
  **no rotation** (ledger L4).
