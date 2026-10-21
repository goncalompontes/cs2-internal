# T22 - the orchestration README describes the plugin as it is now

## The problem

`docs/orchestration/README.md` describes the plugin as it was: one file at
`C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts`, three tools
(`plan`, `review`, `merge`), a hard-coded profile per repository, and the
build directories inside the worktree.

None of that is true any more:

- the plugin is a **repository of its own**, `C:\Users\gpmpo\orchestration-plugin`,
  and the installed copy is a build of it;
- it is **project-agnostic** - a project is described by a configuration file,
  and a repository with no configuration is inferred from what it contains;
- it has **eight tools**, and the loop has two steps it did not have: a
  verify step in the task's own worktree, and a fix step that takes the
  review's findings back to the implementer;
- the **merge verifies before it commits** - the merge is staged with
  `--no-commit`, the verify loop runs against the merged tree, and only a
  tree that passes is committed;
- a task worktree holds **a few megabytes** instead of a few hundred: the
  build directories live outside the worktree, the dependency tree is
  installed once and linked, and a vendored tree is shared through a junction
  instead of copied.

The README is the index a new session reads first. It has to be right.

## What to do

Rewrite `docs/orchestration/README.md` so that it describes the plugin as it
is now. It must cover:

1. **What it is and where it lives** - the source repository, the installed
   copy, and the fact that it is plain user configuration.
2. **The loop** - `plan -> verify -> review -> fix -> merge`, and the eight
   tools with one line each. A table.
3. **The project configuration** - where it lives, and that a repository with
   no configuration is inferred from its stack, its build system, its test
   command and its integration branch.
4. **The verify loop** - what it runs, and that the merge runs it against the
   **merged tree, before the commit**, and refuses a branch that was never
   reviewed or whose review said DO NOT SHIP.
5. **The worktree-size rules** - `build_root`, `deps`, `assets`, `env`,
   `sparse`, and the measured numbers for this project: a worktree was 374 MB
   and is now 8 MB; the vendored Vulkan headers are shared instead of copied.
6. **The junction hazard and the guard** - `git worktree remove --force`
   follows a directory junction and deletes the contents of the target, so
   the plugin records every link it makes and unlinks them before it removes
   a worktree. This is the rule that must not be lost.

Keep it an **index**, not a manual: the details live in the plugin's own
`README.md` and `docs/`. Link to them by path.

Do not describe anything that is not true. If you are unsure of a fact, read
the source: `C:\Users\gpmpo\orchestration-plugin\README.md`,
`C:\Users\gpmpo\orchestration-plugin\docs\configuration.md` and
`C:\Users\gpmpo\orchestration-plugin\docs\worktree-size.md` are the truth.

## File ownership

- `docs/orchestration/README.md`

Nothing else. Do not touch the plugin's own repository, and do not touch the
other files in `docs/orchestration/`.

## Verify

The project verify loop: `meson compile -C build`, `meson test -C build`, the
payload, the overlay, the injector, the Python suite and the type gate.

A documentation change needs no new test. Run the loop anyway - the point of
the task is to prove the loop runs in a worktree whose build directory lives
outside it.

## Report

What changed, per file. What you could not verify.
