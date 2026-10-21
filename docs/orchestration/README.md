# The orchestration plugin

The plugin that runs a task through
**plan → verify → review → fix → merge**, with a git worktree per agent.
The review is adversarial by construction. This file is the **index**: what
the plugin is, where it lives, what its eight tools do, and where the
details are. The details live in the plugin's own repository - see
[The plugin's own documentation](#the-plugins-own-documentation) at the end.

## Where it lives

| what | where |
|---|---|
| the source repository | `C:\Users\gpmpo\orchestration-plugin` |
| the installed plugin | `C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts` |
| the installed implementation | `C:\Users\gpmpo\.config\opencode\orchestration\` |
| the configuration **the plugin reads** | `C:\Users\gpmpo\.config\opencode\orchestration.json` |
| the source of that configuration | `C:\Users\gpmpo\orchestration-plugin\config\orchestration.json` - the two projects on this machine, `cs2` and `rea`, are described in it |

The last two rows are two different files, and the difference matters: the
plugin reads the **installed**
`C:\Users\gpmpo\.config\opencode\orchestration.json`. The plugin repository's
`config\orchestration.json` is only the **source** of it -
`node scripts/install.mjs --config` copies it there. Editing the source alone
changes nothing until the install runs again.

The installed copy is a **build of the source repository**, not the source
itself: `node scripts/install.mjs` copies `orchestration\` to
`<config>\orchestration\` and `plugins\orchestration.ts` to
`<config>\plugins\orchestration.ts`; `--config` installs the project
configuration as well. The layout is the same in the repository and in the
installed copy - the entry point in `plugins\`, the implementation in
`orchestration\` - so the entry point's relative import resolves in both, and
the installer refuses to install a layout that would not.
**Edit the source repository, install, and the change is live.**

It is plain user configuration - no core patch and no SDK import - so an
OpenCode update does not break it, and it is discovered automatically from the
global plugins directory.

## The loop

```
plan → verify → review → fix → merge
```

Each step is one of the eight tools the orchestrator calls; the agent between
them is a sub-agent. The tools are callable from the Code Mode `execute`
runtime as `tools.orchestrate.*`.

| tool | what it does |
|---|---|
| `plan({name, spec \| spec_file, base?, project?, sparse?, no_seed?})` | creates the task's worktree on `<prefix><name>` off the integration tip, seeds it (shared assets, shared dependencies, build directories **outside** the worktree), writes the spec into it, and returns the **implementer's prompt** |
| `verify({task?, groups?, skip?, project?})` | runs the project's verify loop in a worktree - what the implementer and the fixer use to check their own work before anyone reviews it |
| `review({commit, task?, base?, spec_file?, focus?, shared?, project?})` | points a review worktree at the commit and returns the **reviewer's prompt** (hostile by construction); the report is written to a fixed path, so the verdict can be read back before the merge |
| `fix({task? \| branch?, findings?, report?, project?})` | takes the review's findings back to the implementer's worktree and returns the **fixer's prompt** |
| `merge({branch, message?, task?, skip?, allow_unreviewed?, project?})` | merges the task branch into the integration branch - but only after the verify loop passes on the **merged tree** |
| `status({project?})` | the task board: ahead, behind, dirty, merged, reviewed, disk usage |
| `size({project?, task?})` | what makes the worktrees big, and what to do about it; one task, or all of them |
| `cleanup({task?, merged_only?, apply?, prune_branches?, remove_builds?, project?})` | removes the worktrees that are done with - safely (see [the junction guard](#the-junction-hazard-and-the-guard)) |

## The project configuration

A project is described by a configuration file:

1. `~/.config/opencode/orchestration.json` - the global configuration,
2. `<repository>/.orchestration.json` (or `.orchestration.jsonc`, or
   `.opencode/orchestration.json`, or `.opencode/orchestration.jsonc`) - the
   repository's own, which **wins**.

A repository with **no configuration at all is inferred** from what it
contains: its stack, its build system, its test command, its integration
branch, its house rules (`AGENTS.md`, `docs/style-guide.md`) and its reviewer
role (`docs/orchestration/reviewer-role.md`). The generic hostile-reviewer role
is used when there is none. The tools therefore work in an unknown repository
without being set up first - which is what the plugin's own test suite proves,
in a scratch repository it has never seen.

Every field is documented in the plugin's `docs\configuration.md`.

## The verify loop

For this project the loop is:

| group | steps |
|---|---|
| `fast` | the host build (`meson compile`), the payload, the overlay, the injector |
| `tests` | the host tests (`meson test`), the Python suite |
| `slow` | the type gate |

The three Windows targets need the MSVC environment, so they run inside
`vcvars64`. The quoting rule that keeps them working - and the symptom of
getting it wrong - is in the plugin's `docs\configuration.md` §"The Windows
quoting rule".

The loop runs in the **task's worktree**, and a worktree is only as good as
its inputs. The `calibration\` dumps are gitignored and the SDK generation
reads them: without them the generation writes **empty** SDK headers and the
payload builds against an empty SDK - a green build against nothing. The
plugin's configuration lists `calibration` as a shared asset, next to the
vendored Vulkan headers, but this repository tracks
`calibration/.gitignore` and `calibration/README.md`, so the directory is
already in every worktree and the seed leaves a directory that is already
there alone instead of linking it. **Check that the worktree's `calibration\`
is the shared copy before trusting a green build.**

**The merge verifies before it commits.** It does not commit first and verify
afterwards: the merge is staged with `--no-ff --no-commit`, the verify loop
runs against the **merged tree**, and only a tree that passes is committed. A
conflict or a red verify loop aborts the merge and leaves the integration tree
exactly as it was.

The merge also **refuses** a branch that was never reviewed, or one whose
review said **DO NOT SHIP**, unless `allow_unreviewed: true` is passed.

## The worktrees stay small

A worktree of a big project does not have to be big. Five levers, all in the
project configuration:

| lever | what it does |
|---|---|
| `build_root` | the build directories live **outside** the worktree, in `<build_root>/<task>/<name>` |
| `deps` | the dependency tree is installed **once**, keyed by the hash of the lockfile, and linked |
| `assets` | a vendored or generated tree is shared through a **junction** instead of being copied |
| `env` | a tool cache is pointed at another directory: the mypy cache is **shared**, the cargo target directory is **per task** (`{build_root}/{task}/cargo`), because cargo locks it |
| `sparse` | a large tracked checkout becomes a cone-mode sparse checkout |

The measured numbers for this project:

| what a cs2 worktree held, before the levers | size |
|---|---|
| `external/dumper/target` - the Rust build output | 117 MB |
| `build/` - the meson host build | 100 MB |
| `build-payload/` - the meson payload build | 70 MB |
| `external/Vulkan-Headers` - the vendored headers, copied per worktree | 45 MB |
| `.mypy_cache` - the same cache in every worktree | 43 MB |
| **the worktree, in total** | **374 MB** |
| of which the tracked sources | 8 MB |

The tracked sources are **part of** the 374 MB, not in addition to it: they
are the only part that has to be there. The other 366 MB is per-worktree
duplication of things that are identical in every worktree.

What the levers do with it:

- the **build output** lives in the build root, outside the worktree,
- the **vendored Vulkan headers** are shared through a junction instead of
  being copied - zero bytes in the worktree,
- the **mypy cache** is shared,
- the **cargo target directory** lives in the build root, per task.

So a worktree built **with the project's environment** holds about **8 MB**.

A worktree built **without** it does not: a build that skips the environment
puts the cargo target directory (117 MB) and the mypy cache (43 MB) straight
back into the worktree, and it is **170 MB** again. The environment is written
into the worktree when it is seeded - `<worktree>/.orchestration/env.ps1` and
`<worktree>/.orchestration/env.cmd` - and the implementer's and the fixer's
prompts tell the agent to take it before building:

```powershell
. <worktree>/.orchestration/env.ps1
```

The whole story is in the plugin's `docs\worktree-size.md`, which tells it the
same way.

## The junction hazard, and the guard

This is the rule that must not be lost.

**`git worktree remove --force` follows a directory junction and deletes the
contents of the target.** A junction inside a worktree points at a shared
tree, and removing the worktree takes the shared tree with it. That is how
the vendored Vulkan headers were destroyed on this machine once.

So the plugin:

1. **records** every link it makes, in
   `<worktrees>\.orchestration\links\<task>.json`,
2. **unlinks** them - the link, never what it points at - before it removes a
   worktree,
3. **scans** the worktree for any junction it did *not* make, and unlinks that
   too, and
4. never removes a worktree with `--force` before that scan.

`orchestrate.cleanup` is the tool that removes worktrees, and it unlinks
before it removes. A cleanup that only **reports** - `apply` not set - changes
nothing. `test/links.test.ts` in the plugin's repository is the test that
holds the line: it creates a junction, unlinks it, and asserts that the
target's content is still there.

## The plugin's own documentation

The details live in the plugin's repository, not here:

| document | what it holds |
|---|---|
| `C:\Users\gpmpo\orchestration-plugin\README.md` | the loop, the eight tools, the install |
| `C:\Users\gpmpo\orchestration-plugin\docs\configuration.md` | every configuration field, the verify step, the Windows quoting rule |
| `C:\Users\gpmpo\orchestration-plugin\docs\worktree-size.md` | the five levers, the measurements, the junction guard |

## This repository's own orchestration files

| file | what it is |
|---|---|
| `docs/orchestration/feature-brief.md` | the shared brief, given to the reviewer |
| `docs/orchestration/reviewer-role.md` | the reviewer's role - the hostile reviewer, by construction |
| `docs/orchestration/review-prompt-template.md` | the prompt the reviewer is given |
| `docs/orchestration/spec-*.md` | the written specification of a task - the same text goes to the implementer and the reviewer |

**Every task's spec is committed**, this one included. The spec is the
contract the implementer works to and the reviewer checks against, so it
belongs to the repository and not to a scratch directory:
`spec-t21-quality-pass.md` and the rest are committed, and
`spec-t22-plugin-docs.md` follows the same convention.
