# NEXT SESSION — the feature wave, session 2

> **Copy-paste this whole file as the opening prompt.**
>
> The full handoff is **[`docs/handoff-wave2.md`](docs/handoff-wave2.md)** — read
> it first; it says where everything is, what is done, what is open, and the
> orchestration playbook. The task specs for every task this wave ran are in
> **`docs/orchestration/`** (the brief, the reviewer's role, the review prompt
> template and all 26 specs) — **keep new specs there, not in the temp dir**,
> which is wiped between sessions.

---

Read `docs/handoff-wave2.md` and then work through it.

**The shape of this session, as the last one ran it:**

- a **written spec** per task, in `docs/orchestration/spec-<task>.md` — the
  implementer *and* the reviewer read the same one;
- **one git worktree per agent** (`C:\Users\gpmpo\cs2-wt\<name>`), never a
  shared tree; the vendored `external/Vulkan-Headers` is **copied** in (never a
  junction — a removable worktree with a junction inside it follows the link and
  deletes the target);
- an **implementer** sub-agent, then an **adversarial reviewer** sub-agent
  (hostile by construction — its brief is `docs/orchestration/reviewer-role.md`),
  then a **fix** round, then a **merge** with the full verify loop;
- the mechanics of that loop are now a **plugin**: `tools.orchestrate.plan`,
  `tools.orchestrate.review` and `tools.orchestrate.merge` (namespace
  `orchestrate`, in the Code Mode `execute` runtime) create the worktree, seed it,
  assemble the implementer's and the reviewer's prompts, and merge + verify.

**Do this first, before anything else:** the operator reports that the movement
(the auto-strafer or the bhop) **no longer accelerates**. `docs/handoff-wave2.md`
§5.1 has a specific, testable suspect (a default this wave changed, which moved
the strafe onto a path the server gates off) and the two-minute measurement that
settles it. Fix it, then continue.

Then, in order: the chams' hook target (§5.2), the neverlose follow-ups
(§5.3), and the item-5 feature candidates (§5.4).

**Two rules that earned their place last session:** the reviews caught four
blockers and eleven majors that would otherwise have shipped, and the two
live-only bugs (a hook that installed itself while the feature was off, and a
hook target taken from an RTTI *lambda* type) were found by *running* the game,
not by reading the code. So: **review everything, and then put it in the game.**
