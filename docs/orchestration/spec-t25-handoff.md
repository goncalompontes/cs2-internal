# T25 - the handoff describes the orchestrator as it is now

## The problem

Two documents that a new session is *sent* to still describe the
orchestrator as it was:

- `NEXT_SESSION_WAVE2.md` - the file `AGENTS.md` tells the next session to
  open first ("Copy-paste this whole file as the opening prompt"). Lines
  27-30 say:

  > the mechanics of that loop are now a **plugin**: `tools.orchestrate.plan`,
  > `tools.orchestrate.review` and `tools.orchestrate.merge` (namespace
  > `orchestrate`, in the Code Mode `execute` runtime) create the worktree,
  > seed it, assemble the implementer's and the reviewer's prompts, and merge
  > + verify.

- `docs/handoff-wave2.md` - the wave's handoff. Section 2 ("The orchestration
  playbook (this is the part to copy)") lists the loop with three tools, and
  the table in section 1 says the plugin is
  `C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts` - a single file.

Both are wrong now, and the second is *actively* wrong: it gives the old
signatures.

- `orchestrate.review` "points the shared `cs2-wt\rev` worktree" - the plugin
  uses a **per-task** review worktree (`rev-<task>`) now.
- `orchestrate.merge({branch, message?, skip_tests?})` - the parameter is
  `skip`, and the schema has `additionalProperties: false`, so a call with
  `skip_tests` is **rejected**.

A session that reads the handoff and follows it writes a merge call the tool
refuses.

## What to do

Bring both documents in line with the plugin as it is now, and with the loop
the wave now runs.

**`NEXT_SESSION_WAVE2.md`** - the paragraph at lines 27-30: name the loop as
it runs (`plan -> verify -> review -> fix -> merge`) and the tools as they
are, and say that the plugin's source is its own repository
(`C:\Users\gpmpo\orchestration-plugin`) with the installed copy a build of
it. Keep it short - this file is the opening prompt of a session, not a
manual.

**`docs/handoff-wave2.md`** - section 2, the playbook:

- the seven steps become the five steps of the loop, with the two new ones
  (`verify` in the task's own worktree, `fix` taking the findings back);
- the three tool descriptions become the eight, with the **signatures that
  the plugin actually accepts** - a signature that the schema rejects is
  worse than no signature;
- say what the merge does that it did not do before: it verifies the
  **merged tree before it commits**, and it refuses a branch that was never
  reviewed;
- the table row in section 1 that says the plugin is one file becomes the
  repository.

Also check the rest of both documents for the same thing - every mention of
`orchestrate_*`, of the three tools, of the plugin's location, and of the
worktrees being heavy. The worktrees are **not** heavy any more: they are
about 8 MB of sources each, with the build directories outside the worktree
and the vendored trees shared. If either document says the worktrees cost
hundreds of megabytes, it is wrong now.

Do not rewrite the documents. They are a handoff of a wave that really
happened, and the wave's findings - what the reviews caught, the traps, the
measurements - are still true. Change what the plugin did, and leave the
history alone.

## File ownership

- `NEXT_SESSION_WAVE2.md`
- `docs/handoff-wave2.md`
- `docs/orchestration/README.md`

Nothing else.

The third file is the **index** the other two point at, and it has drifted:
its `cleanup` row does not mention `force: true` (a worktree with a modified
tracked file needs it, and without it the cleanup skips the worktree), and it
says the installed copy is a "build" of the source when the install is a
**copy** - `node scripts/install.mjs` is what makes an edit live. Fix those
two, and check the rest of that file for the same drift against the plugin's
current source.

## Verify

The project verify loop. A documentation change needs no new test.

## Report

What changed, per file. What you could not verify.
