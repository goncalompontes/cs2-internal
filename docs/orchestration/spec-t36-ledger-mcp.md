# T36 — the ledger, the MCP schema, and the operator's checklist

> **The one-line version:** the two features are **merged and re-reviewed**, so
> the ledger may finally **close** their findings; the MCP tool's schema still
> refuses the new objective, which is the difference between the ragebot's new
> objective being **live-testable** and not; and the operator's checklist still
> warns about a race that no longer exists.

---

## 1. What this task is

This is the **closing task** of the chams and the ragebot work. Three things,
all of them small, all of them about the repository's **memory** and about the
operator being able to **drive** what was built.

It is **not** a feature task. There is no new behaviour.

---

## 2. What to do

### 2.1 The MCP tool's schema — **check it landed, do not redo it**

**This was fixed in T35's fix round** (the reviewer's finding **N1**):
`scripts/scenario/protocol.py:128`'s `AimObjective = Literal[...]` gained
`"monte_carlo"`, and `RageStatusDict.objective` (`:752`) was corrected from
`int` to the **string** the payload now returns, along with the new `mc_*`
keys.

→ **Verify it landed and that it is complete.** Do **not** redo it. Check:

* the `Literal` contains the new name;
* the MCP tool's **advertised schema** really includes it — read from the code
  where the schema is built (`scripts/scenario/mcp/tools.py`'s
  `self.input_schema(...)`), do not assume;
* `RageStatusDict` matches **what the payload actually returns** — compare it
  against the status block the payload publishes, field by field. A
  `TypedDict` that lies is worse than one that is missing a field, because the
  type gate will not catch it;
* a test pins it.

→ **Say what the fix does *not* fix.** The running MCP server imported
`protocol.py` **at start-up**, so its advertised schema is **stale** until the
server restarts — and the agent must not restart it (`opencode service
restart` kills the session). So:

* the **repository's own runner** picks the new schema up at once
  (`python scripts\run-scenario.py <name>` — a fresh process);
* the **MCP tools** keep refusing `"monte_carlo"` until the server is
  restarted by hand;
* **but** the feature is live-testable **today** through the scenario path:
  `cs2_run_scenario {"name":"rage","args":{"objective":"monte_carlo"}}` — the
  scenario's `args` are free-form, so they are not schema-validated.

All three facts belong in the operator's checklist (§2.3).

### 2.2 The ledger: close what is fixed, record what is not

`docs/known-issues.md` — the repository's ledger of every open finding. Its
own rule, at the top of the file:

> *"**Removing a finding:** only when the fix is **merged and re-reviewed** —
> then delete the row and note the merge in the commit."*

Both conditions are now met for the chams' findings: **T33 is merged**
(`1c80086`) and **re-reviewed** (`SHIP WITH FIXES`, no blockers, no majors).

→ **Delete the rows** for **H2a**, **H3**, **H6**, **H7** and **H8**, and note
the merge in the commit. Their content is not lost — it is in
`docs/re/chams.md` §10, which the branch wrote, and in the review reports.

→ The **note** that says the rows *"stay until the merge and the re-review"*
must go, and the annotation in **§11.1** (the orchestrator's recipe) must be
updated to say it is **merged**, with the merge commit.

→ **Add** the findings that are still open and that no one has recorded yet.
The chams' fix round reported these and they are **not** fixed:

* the **other `g_active`s** — `payload/features/src/thirdperson.cpp`,
  `payload/features/src/game_present_hook.cpp` and
  `payload/control/src/input.cpp` still hold **plain pointers** with the same
  shape the chams' had. The camera's behaviour must not change, so this is
  **not** a chams task, but it is a known defect and belongs in the ledger.
* the **trampoline's pages are never unmapped** — a deliberate, documented
  trade for an unconditional safety property: **one page per uninstall** is
  retained until the process exits. It belongs in the ledger so nobody
  re-discovers it.
* the **scene system's render-job count is unmeasured** — the draw path's
  guard pool holds 64 concurrent owners, which is headroom, **not** a
  measurement. If a machine has more than 64 concurrent render jobs the pool
  can be exhausted again, and the drops are counted (`pool=`).

Check what **T35's §12** already recorded before you add anything —
**do not duplicate a row**. If a finding is already there, leave it.

### 2.3 The operator's checklist must describe what is actually there

`docs/operator-live-check.md` — the operator's page. It is read by a person
who has never read the code.

→ **The chams' row is stale.** It still says:

> *"**Known defect:** the desc cache has a two-writer race (a reader can see
> one desc's address with another desc's entity) - the wrong player can be
> chamsed."*

**That defect is fixed and merged.** Remove the warning and replace it with
what the operator should now watch: the status line gained **`rec=`** (the
recursion refusals), **`pool=`** (the guard's pool exhausted), **`tdn=`** (the
teardown drops) and **`gu=`** (the desc cache's give-ups — the cache's health;
the operator should watch it stay low).

→ **The Monte-Carlo objective needs a row.** It is the newest thing in the
tree and the operator has no way to know it exists, what it reports, or what
its numbers mean. The row must say, in the checklist's own terse style:

* how to select it (the control channel; **not** the menu);
* **the MCP tool refuses it until the server is restarted** — and that the
  repository's own runner does not have that problem (§2.1);
* **turn the backtracking off** while live-testing it — the MC objective and
  the backtracking pass do **not** fit the frame budget together
  (`docs/ragebot.md` §14.5);
* that `min_hitchance` and `min_damage` **mean different things** under it
  (the two gates move in **opposite** directions) — `docs/ragebot.md` §14 has
  the numbers.

→ The checklist's chams' row must also say the **live run is still owed**:
the chams' detour has never run against the real draw, the tier0 job count is
unmeasured, and the fail-closed refusal's real-world frequency is unknown.

→ **Check the numbers the checklist quotes.** It says the host suite is
*"45/45 today"* (`docs/operator-live-check.md:17`) — the suite has grown a long
way since (the tree is at **58/58** host tests and **499** Python tests as of
T35's fix round). **Any number the checklist states about the tree must be
right** — an operator who reads "45/45" and sees a different count stops
trusting the page. Check the counts and the commands in §0 and correct them,
and say where the number came from.

---

## 3. What is out of scope

* **Any behaviour change.** Nothing in the payload, the overlay or the control
  channel's protocol changes. The one code change is the `Literal`.
* **The `wire::Settings` block** — not touched, not version-bumped.
* **The menu and the overlay** — the objective is not exposed there, by
  design.
* **The other `g_active`s** — **record** them in the ledger, do not fix them.
* **The protection workstream** — out of scope.
* **Re-reviewing the merged work.** T33 and T35 have been re-reviewed; this
  task does not re-open them.

---

## 4. File ownership

You own:

* `scripts/scenario/protocol.py` **only if** T35's fix round did not already
  complete §2.1 — and then **only** to close the gap it left
* `scripts/scenario/tests/**` (the test that pins the schema)
* `docs/known-issues.md` (the ledger)
* `docs/operator-live-check.md` (the checklist)

You do **not** own the payload, the overlay, `wire::Settings`, the aim module,
the control module or the MCP server's implementation. If you need a file you
do not own, **stop and report it** instead of editing it.

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative.
* Python: the repository's own conventions — the type-checking gate
  (`python scripts/typecheck.py`) must stay clean, and the code must type
  check under **mypy strict**.
* **The ledger's rule is the rule**: a finding is removed only when its fix is
  **merged and re-reviewed**, and the merge is noted in the commit.
* **Append-only** for the ledger's *new* sections; the H-rows are the
  exception — they are **deleted**, deliberately, and the commit says so.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 6. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\<task>\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\<task>\build
meson test   -C C:\Users\gpmpo\cs2-build\<task>\build
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories live **outside** the worktree; do not run `meson setup`
inside it.)

---

## 7. What the report must state

1. **The schema** — what you changed, and the evidence that the MCP tool's
   advertised schema now includes the new objective.
2. **The stale server** — what the fix does *not* fix, and how the operator
   drives the new objective **today**.
3. **The ledger** — every row you deleted (with the merge that justifies it),
   every row you added, and confirmation that you did not duplicate T35's
   entries.
4. **The checklist** — what the operator will now look at for each feature.
5. What is still open, and what you could not verify.
