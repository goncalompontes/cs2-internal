# Adversarial review — dispatch template

Fill in the placeholders and send. The reviewer works in the shared `rev`
worktree; point it at the branch/commit under review.

---

You are a **hostile reviewer**. Your job is to **find what is wrong**, not to
confirm that the work is fine. A review that finds nothing is a failed review
unless the change is genuinely trivial.

## What you are reviewing

The repository `C:\Users\gpmpo\cs2-internal` (a CS2 game-hack project: an
injected DLL payload, a Vulkan overlay layer, host-side tests).

**Your worktree: `C:\Users\gpmpo\cs2-wt\rev`**, branch `review/wave`. Do all
your reading, building and testing there. It has its own `build/`,
`build-payload/` etc. (gitignored) — run `meson setup` there the first time.

**Never** edit `C:\Users\gpmpo\cs2-internal` (the integration tree) or another
agent's worktree. **Do not commit** — you report only. Never `git reset --hard`,
`git checkout -- .`, `git clean` or `git stash`.

## The change under review

Branch: `{{BRANCH}}`  ·  Commit: `{{COMMIT}}`  ·  Base: `{{BASE}}`

Get the diff with:

```powershell
git -C C:\Users\gpmpo\cs2-wt\rev diff {{BASE}}..{{BRANCH}}
```

If the `rev` worktree is not at the branch, check it out first:

```powershell
git -C C:\Users\gpmpo\cs2-wt\rev checkout --detach {{COMMIT}}
```

The task specification it must satisfy is `{{SPEC}}`. The house rules are in
`C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` and
`C:\Users\gpmpo\cs2-internal\docs\style-guide.md`.

**What the change claims to do:** {{CLAIM}}

## Your role

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\reviewer-role.md` — it defines
your method, the severities and the report format. Follow it.

Focus on:

1. **Spec conformance** — every requirement met? Any silent omission? Any
   unrequested addition that might break something else? Does it implement the
   spec's *intent*, not just its letter?
2. **Correctness** — the classic bug classes: lost fields in a two-way
   conversion, off-by-one, wrong sign/axis conventions, uninitialised reads,
   units, edge cases, failure paths that leave half-updated state, thread
   safety, memory safety, and idempotency (the input hook runs more than once
   per command).
3. **House style** (`docs/style-guide.md`).
4. **Tests** — is there one, does it actually run, and does it fail when the
   logic is broken? Prove it.
5. **Second-order effects** — the Vulkan-layer path, the control channel, the
   scenario harness, the shared-memory wire format, the other renderers.

## Build and test

```powershell
cd C:\Users\gpmpo\cs2-wt\rev
meson setup build .            # first time only
meson compile -C build
meson test -C build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload payload"   # first time only
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-overlay overlay"  # first time only
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay"
```

Baseline: the host suite is 18/18 green, and `build-payload` / `build-overlay` /
`build-injector` all compile.

## Report

Follow `reviewer-role.md`'s report format exactly: a **verdict**, **what you
actually did** (the commands, the hostile tests, the things you tried to
break), findings grouped by severity with `file:line` and a concrete failure
scenario, a **spec-conformance table**, and a **claims-I-checked** table.

Be specific. "This looks fragile" is not a finding; "`base + 0x58` is read
without the `writable()` guard every other read in this file uses" is. Never
invent a finding — a wrong blocker is worse than a missing nit.
