# The orchestration plugin

`C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts` — the plugin that
runs the **implement → adversarial review → fix → merge** loop with a git
worktree per agent. It is plain user configuration (not a core patch), so
OpenCode updates do not break it, and it discovers automatically from the
global plugins directory.

Its three tools are callable from the Code Mode `execute` runtime as
`tools.orchestrate.*`:

| tool | what it does |
|---|---|
| `plan({name, spec \| spec_file, base?})` | creates the task's worktree (`C:\Users\gpmpo\cs2-wt\<name>`) on `feat/<name>` off the integration tip, seeds it (copies the vendored Vulkan headers in), writes the spec into the worktree's `docs/orchestration/`, and returns the **implementer's prompt** |
| `review({commit, base?, spec_file?, focus?})` | points the shared review worktree (`cs2-wt\rev`) at the commit and returns the **reviewer's prompt** (with the hostile-reviewer role) |
| `merge({branch, message?, skip_tests?})` | merges the task branch into the integration branch (`feat/occlusion-penetration`) and runs the **whole verify loop**; it refuses to commit a tree that does not build, and on a conflict it reports and stops rather than committing something broken |

`repo: 'cs2'` (default) is this project; `repo: 'rea'` is the TypeScript REA
tool with its own npm/turbo verify loop.

## The verify loop it runs

`meson compile -C build` → `meson test -C build` → the payload → the overlay →
the injector → the Python suite → the mypy gate. **The Windows targets need
`vcvars64`**, and the plugin runs them through `cmd.exe /c call vcvars64.bat
...`.

## The `vcvars64` quoting bug (fixed 2026-10-21)

The plugin's Windows-target steps **reported exit 1 for every build** even
though the builds were fine. The cause was in `runInVcvars`: it passed the
whole command line as a **single** `/c` argument:

```ts
["/c", `call "${MSVC}" >nul 2>&1 && set CC=clang-cl && ... && ${args.join(" ")}`]
```

`execFile` with `shell: false` builds the Win32 command line by quoting each
argument, and a single argument that already contains `"` loses its inner
quotes to that quoting. `cmd.exe` therefore saw a **truncated path**, the
`call` failed, and the step exited 1 with **no output at all** — which is why
the failure looked like a build error and was not one.

The fix is to pass the tokens as **separate argv entries** (each a plain word
with no embedded quotes, so the quoting round-trips):

```ts
["/c", "call", MSVC, ">", "nul", "2>&1", "&&", "set", "CC=clang-cl", "&&", ...args]
```

**Symptom to recognise:** a Windows-target step fails with **no output** and
exit 1, while the same command run by hand succeeds. Always re-run the step
by hand before believing it.

## The rule about the vendored Vulkan headers

`external/Vulkan-Headers` is gitignored, so a fresh worktree has none and the
overlay build fails. **Copy them into each worktree; never junction or
symlink them** — `git worktree remove --force` follows a directory junction
and **deletes the contents of the target**. The plugin's `seed` does the copy.
