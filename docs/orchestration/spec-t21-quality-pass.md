# T21 — the project-wide quality pass

## What this task is

A **project-wide quality pass** over the C++ and Python trees: style and
convention consistency, structural refactors, RAII and modern C++ adoption,
tooling improvements, and other quality-of-life work that is worth doing.

This is a **refactoring** task, not a feature task. The measure of success is
that the tree is **easier to work in** afterwards, with **no behaviour
change** — every refactor must be provably equivalent, and the host suite,
the Python suite, the type gate and all four builds must stay green.

**This is a wide task.** It is deliberately scoped so that the work is
**reviewable** and **mergeable**: the commits must each be independently
verifiable, and the reviewer must be able to check one commit at a time.

## The rules that dominate everything

1. **No behaviour change.** This is a refactor. If a commit changes what the
   payload does, it does not belong in this task. A "drive-by bug fix" is a
   **separate commit at most**, and only if it is unambiguously a fix — and
   even then, say so loudly in the report and in the commit message.
2. **No reformatting drive-bys.** The tree is **not** clang-format-clean
   historically (`docs/style-guide.md` §7 rule 3 says so). A whole-file
   reformat of a 3000-line file produces a diff nobody can review and hides
   the real change. **Reformat only the lines you actually touch.**
3. **One concern per commit.** The commits must each be independently
   verifiable: `refactor(scope): what changed and why`. A commit that mixes
   a move, a rename, a formatting change and a behaviour fix is not
   reviewable and must be split.
4. **The verify loop after every commit**, not just at the end.
5. **Never** `git reset --hard`, `git checkout -- .`, `git clean`,
   `git stash`; never amend or rebase. Never edit the integration tree or
   another agent's worktree.

## The hard constraints of this codebase

These are the things that make a "modern C++" refactor dangerous here. Read
`docs/style-guide.md` in full — it is normative — and read
`docs/decisions.md` (the ADRs). The ones that will bite:

- **The payload is manually mapped** (`payload/loader/`). There is **no CRT
  static initialisation**, so a `static` with a non-trivial constructor is a
  **bug**, not a style issue. `docs/style-guide.md` §3 says exactly this.
- **No exceptions.** Errors are values: `std::expected` / `std::optional`.
  A refactor that introduces a throwing path (e.g. `std::vector::at`,
  `std::stoi`, `operator new` that can throw) breaks the payload's build
  contract. Check the `-fno-exceptions` build flags still hold.
- **No `thread_local` in a detour** on the mapped image: the module's TLS
  block is not installed on the game's threads (the T18b camera crash —
  `docs/re/camera-split.md`).
- **ADR-0010: no new global mutable state.** Where shared state is
  unavoidable, it goes behind a typed accessor with the right
  synchronisation, and the *initialisation* and the *later mutation* are
  separate concerns (the style guide spells this out).
- **The platform-independent core must not include `<windows.h>`**:
  `payload/loader` and `payload/game` are built both into the Windows
  payload and into the **host tests**. Guard with `#if defined(_WIN32)` and
  provide a host stub in the `#else` branch.
- **The hot paths are measured.** `payload/aim` is on the ragebot's tick
  budget (`docs/research/aim-solver-design-review.md` — the solver is
  ~5 ms at best, and the damage field is the measured cost driver). A
  refactor that allocates, copies or bounds-checks in a hot loop is a
  **regression**, not a cleanup. Use the host benchmark over a saved cell
  set for timing (`docs/handoff-wave2.md` §"Live measurement caveat") — a
  before/after across a game restart is **not** comparable.
- **The wire/ABI boundary** (`wire::Settings`, `overlay/include/cs2/overlay/wire.hpp`)
  is shared between two separately compiled binaries (the payload and the
  Vulkan layer). A field added on one side only, or a `k_version` bump not
  honoured, is **silent corruption**. If a refactor touches that struct at
  all, it must keep the layout, the `static_assert`s and the generated
  settings table (`scripts/gen-settings-table.py --check`) in step.

## Where the debt actually is

Measured on the current tree (2026-10-21). These are the **starting points**,
not a fixed list — find more, and drop anything that turns out to be fine.

| what | where | why it is worth it |
|---|---|---|
| a 3274-line `.cpp` | `payload/control/src/service.cpp` | one `Service` class with ~40 `do_*` command handlers; the command dispatch, the ragebot glue, the snapshot/occlusion glue and the settings plumbing are all in one translation unit. **Split it into its own units.** |
| a 2029-line `.cpp` | `payload/control/src/input.cpp` | the `Input` class: the CreateMove hooks, the strafe, the bhop, the silent-aim writers, the eye-angle plumbing. Same story. |
| a 1418-line `.cpp` | `payload/control/src/occlusion_query.cpp` | the occlusion query — a self-contained subsystem that wants its own unit and its own header. |
| a 817-line header | `payload/control/include/cs2/control/input.hpp` | a class with a very large public surface; the members are grouped but the *interface* is not separable. |
| a 624-line header | `payload/control/include/cs2/control/service.hpp` | same. |
| manual file-handle management | `payload/control/src/service.cpp` (`do_log`: `CreateFileA` → three early returns → `CloseHandle`), `payload/control/src/rage_config.cpp` (the **same** `CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle` resolution block **twice**, in `rage_config_load` and `rage_config_save`), `payload/control/src/channel.cpp`, `payload/control/src/service.cpp` (the thread handle) | **the clearest RAII win in the tree.** A small `UniqueHandle` (or a `FileHandle` wrapper) in one header removes every manual `CloseHandle` and every "returned early without closing" hazard. |
| duplicated file-I/O plumbing | `payload/control/src/rage_config.cpp` resolves `CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle` **twice**, with the same type aliases and the same `kernel_fn` lookup | one shared helper. |
| the menu duplicated across two renderers | `payload/features/src/menu.cpp` (505 lines) and `overlay/src/menu.cpp` (334 lines) | already partly fixed — `overlay/include/cs2/overlay/menu_pages.hpp` (1171 lines) holds the shared pages — but the **renderers** still share a lot. Check whether more can move into the shared header. |
| the clang-tidy sweep is manual | `scripts/clang-tidy-fix.py` exists and works | **tooling**: make the sweep's *result* checkable (a "the tree is clean under these groups" mode) so a future change cannot silently regress the style. |
| no CI | there is no `.github/workflows` | **tooling**: a workflow that runs the host build, the host tests, the Python suite, the type gate and the format check. It cannot run the Windows payload build on a Linux runner, so scope it to what a runner can do, and say what it does not cover. |
| the preflight script is Windows-only | `scripts/preflight-windows.ps1` | **tooling**: is there a Linux/macOS equivalent needed? Only if the repo is meant to build there. Check `docs/decisions.md` ADR-0008 (the tooling languages) and decide deliberately. |

## What to look for beyond the list above

Read the code and find the rest. The categories that pay:

- **Duplicated logic that should be one function.** Look for the same five
  lines written three times.
- **Copy-paste between the payload's menu and the layer's menu.**
- **Long functions that are really several functions.**
- **Parameters that are always passed together** (a smell that a small
  struct would carry them).
- **Boolean parameters at a call site** (a smell that an `enum class` would
  carry the meaning).
- **`std::optional` / `std::expected` chains that could be flattened** — and
  the opposite: error paths that are handled by a magic return value.
- **Missing RAII**: anything that acquires a resource and can return early.
- **Modern C++ the tree is not using yet** where it is a clear win:
  `std::span`, `std::string_view` (already used), designated initialisers,
  `if constexpr`, concepts for the generic helpers, `std::erase_if`,
  `std::ranges`, `[[likely]]`/`[[unlikely]]` on the hot paths, `std::span`
  for the byte-buffer APIs, `constexpr` for the pure maths.
  **Only where it makes the code clearer** — the style guide warns that
  template complexity affects diagnostics and build times.
- **The host tests**: are there pure functions with **no** test? The style
  guide says new logic that can be expressed without the engine **must** get
  a host test. A refactor that moves logic into a pure header should
  **bring or extend its test**.
- **The Python side** (`scripts/`, `scripts/scenario/`): the mypy gate is
  strict and clean today; are there modules with no type annotations? Are
  the scenarios' shared helpers duplicated between scenario files?

## How to scope this task so it stays mergeable

**This is the most important instruction in the spec.**

Two other agents are working in `payload/control/` **concurrently**:

* **T19** — the anti-aim (`payload/control/src/input.cpp`,
  `payload/control/include/cs2/control/input.hpp`,
  `payload/control/src/service.cpp`, `payload/control/include/cs2/control/service.hpp`)
* **T20** — the local animfix (the same four files)

If this task refactors those four files, all three branches touch the same
lines and the merge becomes a conflict minefield.

**So:**

* **Do not refactor `payload/control/src/input.cpp`,
  `payload/control/src/service.cpp`,
  `payload/control/include/cs2/control/input.hpp` or
  `payload/control/include/cs2/control/service.hpp`.** They are owned by the
  other two agents right now.
* **You may** work on: `payload/control/src/occlusion_query.cpp` (and its
  header), `payload/control/src/rage_config.cpp`, `payload/control/src/channel.cpp`,
  `payload/aim/`, `payload/features/`, `payload/game/`, `payload/hooks/`,
  `payload/loader/`, `payload/render/`, `payload/tls/`, `payload/diag/`,
  `overlay/`, `injector/`, `tools/`, `scripts/`, and the build files.
* If a refactor you want to make **must** touch one of the four contended
  files, **do not do it** — describe it in the report as a **follow-up**,
  with the concrete plan, so it can be done after T19 and T20 land. That is a
  **good** outcome for this task: a precise, prioritised backlog.
* Splitting a **new** file out of a contended file is also out of scope for
  the same reason (it is an edit to the contended file). Splitting a
  **non-contended** file is exactly the kind of work this task wants.

## What to deliver

### 1. A quality report

**`docs/quality-pass.md`** — the pass's own record, so the next agent knows
what was done and what was deliberately left:

* **the inventory**: every finding, with `file:line`, what is wrong, and the
  concrete cost (a bug risk, a review cost, a build cost, a future-change
  cost);
* **what you changed**: the commit → the finding it fixes mapping;
* **what you did not change, and why**: the follow-up backlog, **prioritised**,
  each with the concrete plan (which files, which move, which seam);
* **the measurements**: the before/after numbers where a claim is
  quantitative (file sizes, the hot-path timings, the build time), and the
  **method** you measured them with.

### 2. The refactors themselves

Each one a commit, each one independently verifiable, each one covered by the
verify loop. The **RAII work** (the manual `CloseHandle` sites) is the
highest-value item — do it even if it means touching more than one file.

### 3. The tooling improvements

Pick the ones that are **genuinely worth it** and that you can finish. The
candidates are listed above (the sweep's check mode, a CI workflow). Do not
add a CI workflow that is red on the first run — a red CI is worse than no
CI. If you add one, make sure it passes locally first, and say in the report
what it does **not** cover (the Windows payload build, the live game).

### 4. The tests

Any refactor that moves logic into a pure header must bring or extend its
host test. Any new pure function must have a host test that **can fail**:
break it on purpose, watch it fail, revert.

## File ownership

You own everything **except** the four contended `payload/control/` files
listed above, and except:

* `payload/sdk/**` — **generated**, never hand-edited
* `external/**` and `payload/third_party/**` — vendored
* `overlay/include/cs2/overlay/wire.hpp` and the generated
  `overlay/include/cs2/overlay/settings_table.hpp` — **only** if a change is
  genuinely needed, and then the `k_version` bump plus **all** of
  `payload/features/include/cs2/features/esp.hpp`,
  `payload/features/src/settings.cpp` and `overlay/src/renderer.cpp` must
  move with it. **Prefer not to touch it at all.**
* `docs/re/**` and `docs/research/**` — those are the RE agents' records; do
  not rewrite them. (`docs/quality-pass.md` is yours.)
* `docs/orchestration/**` — the orchestrator's.

If you need a file outside this list, **stop and report it** instead of
editing it.

## The house style

`docs/style-guide.md` is normative. Read it **in full** before you write
anything. The short version:

- C++23, 2-space indent, 100 columns, K&R braces, `clang-format` (`.clang-format`).
- `PascalCase` types, `snake_case` functions/variables, `SCREAMING_SNAKE_CASE`
  constants, trailing `_` for private members, `enum class` with `snake_case`
  values.
- **Trailing return types** on declarations *and* definitions.
- Fully qualified `std::` — no `using namespace std;`.
- Errors are values: `std::expected` / `std::optional`. **No exceptions.**
- `[[nodiscard]]` on result-carrying helpers, `noexcept` on non-throwing code.
- Value-init with `{}`; `static_cast` / `reinterpret_cast` only.
- `auto` where the type is evident; spell the type out when it carries meaning.
- No new global mutable state (ADR-0010).
- Prefer ranges/algorithms; composition over inheritance.
- The platform-independent core must not include `<windows.h>`.
- Comments explain **why**, cite ADRs, and use `// --- Section ---` dividers
  in long files.
- Do not reformat whole files as a drive-by.

## The verify loop

After **every** commit:

```powershell
meson compile -C build
meson test -C build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

The host suite is **42/42** at baseline and must stay green (plus whatever you
add). The Python suite is **387** and mypy is **0 errors** at baseline.

**If you touch the hot path** (`payload/aim`), benchmark it: the host
benchmark over a saved cell set, before and after. A "before/after" across a
game restart is **not** comparable (the bots respawn elsewhere and the cell
count dominates).

## The report

1. **The inventory** — the findings, and which you fixed.
2. **The commits** — one line each: the commit, what it changed, which
   finding it fixes, and how it was verified.
3. **The measurements** — before/after, with the method.
4. **The follow-up backlog** — what you deliberately did **not** do, why, and
   the concrete plan (this is a first-class deliverable, not an excuse).
5. **The contended files** — what you would have done in
   `input.cpp` / `service.cpp` / `input.hpp` / `service.hpp` if they had been
   free, and how risky it would have been.
6. **What you could not verify.**

## The review

The reviewer reads this **same spec** and is hostile. A review that finds
nothing is a failed review. The things it will look for hardest:

* **behaviour change** — did any refactor change what the payload does? The
  reviewer will look for it specifically, because that is the failure mode
  that matters most here.
* **the RAII change** — is every handle closed on **every** path, including
  the early returns and the exception-free error paths?
* **the hot path** — did a "cleanup" add an allocation or a copy to the
  solver's tick?
* **the contended files** — did the agent stay out of them?
* **the tooling** — does the new tooling actually work, and does it pass?
