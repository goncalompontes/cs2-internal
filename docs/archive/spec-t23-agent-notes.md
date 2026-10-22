# T23 - the agent notes name the tools the orchestrator actually has

## The problem

`AGENTS.md` tells the next session that the orchestration loop is a plugin
with three tools:

> The mechanics of that loop are a plugin
> (`tools.orchestrate.plan` / `.review` / `.merge`), and every task spec, the
> shared brief, the reviewer's role and the review prompt template live in
> [`docs/orchestration/`](docs/orchestration/).

That was true until 2026-10-21. The plugin is now a repository of its own
(`C:\Users\gpmpo\orchestration-plugin`), it has **eight** tools, and the loop
has two steps it did not have: a verify step in the task's own worktree, and
a fix step that takes the review's findings back to the implementer.

A session that reads `AGENTS.md` first is told about a plugin that no longer
exists in that shape, and reaches for `tools.orchestrate.review` when what it
wants is `tools.orchestrate.fix`.

## What to do

Fix the paragraph in `AGENTS.md` that names the tools, so that it names the
loop as it runs now:

```
plan -> verify -> review -> fix -> merge
```

and the eight tools: `plan`, `verify`, `review`, `fix`, `merge`, `status`,
`size`, `cleanup`. One clause each is enough - the index is
`docs/orchestration/README.md`, and the detail is the plugin's own
`README.md`.

Also say, in the same paragraph:

- that the plugin's **source** is its own repository
  (`C:\Users\gpmpo\orchestration-plugin`) and the installed copy is a build
  of it, so the thing to read and the thing to edit are the same shape;
- that the **merge verifies the merged tree before it commits**, and refuses
  a branch that was never reviewed - that is the behaviour a session must
  not be surprised by.

Keep it to the one paragraph. `AGENTS.md` is an index for the next session,
not a manual.

## File ownership

- `AGENTS.md`

Nothing else.

## Verify

The project verify loop. A documentation change needs no new test.

## Report

What changed, per file. What you could not verify.
