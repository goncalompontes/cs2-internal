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
  shared tree; the vendored `external/Vulkan-Headers` is **shared** through a
  junction (a removable worktree with a junction inside it follows the link and
  deletes the target — the plugin unlinks it before it removes a worktree);
- an **implementer** sub-agent, then an **adversarial reviewer** sub-agent
  (hostile by construction — its brief is `docs/orchestration/reviewer-role.md`),
  then a **fix** round, then a **merge** with the full verify loop;
- the mechanics of that loop are now a **plugin**: the loop is
  **plan → verify → review → fix → merge**, and the plugin's eight tools
  (`tools.orchestrate.*`, namespace `orchestrate`, in the Code Mode `execute`
  runtime) create the worktree, seed it, run the verify loop in it, assemble the
  implementer's, the reviewer's and the fixer's prompts, and merge only a tree
  that passes. Its source is its own repository
  (`C:\Users\gpmpo\orchestration-plugin`); the installed copy is a **copy** of
  it, and `node scripts/install.mjs` is what makes an edit live. The index of
  the tools is [`docs/orchestration/README.md`](docs/orchestration/README.md).

**Do this first, before anything else:** the movement question is **settled**
(2026-10-20). The auto-strafe's silent encoding is the only one that gains
speed, it needs the server to honour the sub-tick view angles (the default), and
the payload now reports the outcome and the gate
(`docs/handoff-wave2.md` §5.1, `docs/re/strafing.md` §"The gate"). The default
`movement_strafe_subticks` stays **8** — the measurement disproved the suspect.

Then, in order: the chams' hook target (§5.2), the neverlose follow-ups
(§5.3), and the item-5 feature candidates (§5.4).

**Two rules that earned their place last session:** the reviews caught four
blockers and eleven majors that would otherwise have shipped, and the two
live-only bugs (a hook that installed itself while the feature was off, and a
hook target taken from an RTTI *lambda* type) were found by *running* the game,
not by reading the code. So: **review everything, and then put it in the game.**
