# NEXT SESSION — wave 3 (2026-10-22): the chams' race, the Monte-Carlo ragebot, and the guard

> **Copy-paste this whole file as the opening prompt.**
>
> The full handoff is **[`docs/handoff-wave3.md`](docs/handoff-wave3.md)** —
> read it first; it says what landed, what is open, and how to run the live
> test.

---

Read `docs/handoff-wave3.md` and then work through it.

**The shape of this session, as the last one ran it:**

- a **written spec** per task, in `docs/orchestration/spec-<task>.md` — the
  implementer *and* the reviewer read the same one, and the **reviewer checks
  the code against the spec**;
- **one git worktree per task** (`C:\Users\gpmpo\cs2-wt\<name>`), never a
  shared tree;
- an **implementer** sub-agent → an **adversarial** reviewer sub-agent → a
  **fix round** → a **merge** with the full verify loop, over
  `tools.orchestrate.*`;
- the verify loop now has **ten** steps, and the new one — `payload tests` —
  **runs** the payload's **clang-cl**-built tests instead of only compiling
  them.

**Do this first, before anything else:**

1. **T42 — the guard's recovery** (`feat/t42-guard-recovery`, head `1695973`).
   It is **implemented and green but was never reviewed** — the report is an
   empty stub. Run the review, then the fix round, then the merge.
   **Why first:** the payload's fault guard **killed the payload 4 times in
   12** on the fault it was written for. It is the only safety net the payload
   has for an engine call that faults, and the probe (T39) calls the engine
   from the game's main thread.
2. **T39 — the engine trace probe** (`feat/t39-trace-probe`, head `fa98422`).
   Re-reviewed **SHIP WITH FIXES**; two minors are open. It **must land after
   T42**, and it must be **reconciled** with T42's one-guard rule: drop its
   own `trace_guard.hpp`, call `cs2::diag::call_guarded`, drop its own guard
   test. The handoff has the five steps.

**Then the live test**, which is what the operator has been asking for:
rebuild → restart the game → inject **once** → **the operator stops the bots
and points at a wall the ragebot fires through** → run the probe with those
view angles and compare the engine's trace against our occluder logic. The
procedure and the "what to check before you believe a result" list are in the
handoff's §4.

**Two rules that earned their place last session:**

- **A finding that is a hole in a *proof* counts.** The best reviews this
  session found not bugs but **vacuous tests** — a symmetric scene where all
  four objectives coincide, a substring search satisfied by a neighbouring
  occurrence, a control that was printed instead of asserted. A test that
  cannot fail is worse than no test.
- **Re-derive; do not trust the spec.** Three times, a careful implementer
  re-derived the engine instead of trusting the spec — and the engine was
  right every time (the pattern band's cycle, the trace container's layout, the
  guard's mechanism). **Write the specs from the live build, not from a
  summary.**

**The reviews continue after the fix.** Six of last session's tasks went
through a second, sometimes third, review; T37 needed three. The merge gate is
`SHIP`; a `DO NOT SHIP` needs `allow_unreviewed: true` **and** a note in the
report, and the **verify loop is not overridable** — the merged tree must
compile and pass.
