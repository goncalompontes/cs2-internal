# cs2-internal — feature wave brief (shared by all implementation and review agents)

> Working dir: `C:\Users\gpmpo\cs2-internal` (git repo, branch `feat/occlusion-penetration`).
> Read `AGENTS.md` and `docs/style-guide.md` first. They are normative.

## 0. Hard rules

1. **Never** `git reset --hard`, `git checkout -- .`, `git clean`, or otherwise discard
   work in the tree. Other agents may be working in the same tree.
2. **Never** amend or rebase existing commits. New commits only.
3. Commit message style is `type(scope): summary` — e.g. `feat(esp): tracers, skeleton and off-screen arrows`.
4. Only touch the files listed in your task's **File ownership** section. If you need a
   file outside it, say so in your report instead of editing it.
5. `wire::Settings` (`overlay/include/cs2/overlay/wire.hpp`) is **shared**: it is the
   transport for the menu → payload settings. Any change to it must bump
   `wire::k_version` and must update **all** of:
   - `payload/features/include/cs2/features/esp.hpp` (`EspConfig`)
   - `payload/features/src/settings.cpp` (`to_wire_settings` / `from_wire_settings`)
   - `overlay/src/renderer.cpp` (`default_ui_settings`)
   If the task's file ownership does not include those, **do not** change `wire::Settings`.
6. Verify loop before you claim done (see §4).

## 1. House style (summary — `docs/style-guide.md` is authoritative)

- C++23, clang-format (`.clang-format`): 2-space indent, 100 columns, K&R braces,
  left-aligned pointers (`const char* p`), namespace bodies not indented, closers
  `}  // namespace cs2::x`.
- `PascalCase` types, `snake_case` functions/variables, `SCREAMING_SNAKE_CASE`
  constants, trailing `_` for private members, `enum class` with `snake_case` values.
- **Trailing return types** on declarations *and* definitions.
- Fully qualify `std::` — no `using namespace std;`.
- Errors are values: `std::expected<T, E>` / `std::optional<E>`. **No exceptions.**
- `[[nodiscard]]` on result-carrying helpers, `noexcept` on non-throwing code.
- Value-init with `{}`; `static_cast`/`reinterpret_cast` only (no C-style casts).
- `auto` where the type is evident; spell the type out when it carries meaning.
- No new global mutable state (ADR-0010). Thread-safe one-time init where needed.
- Prefer ranges/algorithms; composition over inheritance.
- Platform-independent core must not include `<windows.h>`.
- Comments explain **why**, cite ADRs, and use `// --- Section ---` dividers in long files.
- Do not reformat whole files as a drive-by.

## 2. Layout

```
payload/sdk/       generated engine metadata (interfaces, offsets, schemas) — DO NOT EDIT
payload/game/      read-only game introspection (memory, entity system, snapshot, matrix)
payload/aim/       ragebot aim model (pure geometry, host-testable)
payload/control/   CUserCmd input backend + named-pipe control channel
payload/features/  ESP + in-process menu + present hooks  <- the live UI
payload/render/    UI-agnostic draw list + ImGui backend
payload/hooks/     vtable + inline hook mechanisms
overlay/           Vulkan implicit layer (the *fallback* renderer) + the shared wire format
```

**The live menu is `payload/features/src/menu.cpp`.** `overlay/src/menu.cpp` is the
Vulkan-layer fallback and is currently a near-duplicate of it.

## 3. Build / test (the verify loop)

```powershell
# host build + tests (no vcvars needed)
meson compile -C build
meson test -C build

# native Windows targets (need vcvars64 in the environment)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-injector"
```

The host suite is **17/17 green** at baseline — it must stay green.
New logic that can be expressed without the engine **must** get a host test under
`payload/<module>/tests/`, wired in `payload/<module>/meson.build` and the root
`meson.build` (see how `aim-*` / `control-core` are registered).

### 3.1 Worktrees — you work in YOUR OWN worktree

Every agent gets a **git worktree** — its own directory, its own branch, its own
build directories. You are told which one in your task prompt.

- **Do all your work inside that worktree directory** — every `edit`, every
  `write`, every build command. Use it as the working directory of your shell
  commands.
- **Never** edit files in `C:\Users\gpmpo\cs2-internal` (the integration tree) or
  in another agent's worktree. The orchestrator merges your branch.
- The worktree has its **own** `build/`, `build-payload/`, `build-overlay/` and
  `build-injector/` directories (they are gitignored). Run `meson setup` in the
  worktree the first time:

  ```powershell
  cd <your worktree>
  meson setup build .
  meson compile -C build
  meson test -C build
  $vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
  cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload payload"
  cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
  ```

- Commit on **your branch** in your worktree. The orchestrator merges it into the
  integration branch `feat/occlusion-penetration` in `C:\Users\gpmpo\cs2-internal`.

## 4. What "done" means

- `meson compile -C build` clean, `meson test -C build` 17/17 (or more).
- `build-payload`, `build-overlay`, `build-injector` all compile.
- New/changed public functions documented with a `///` comment that says *why*.
- A commit that touches only your owned files, message `type(scope): summary`.
- A report stating: what changed, file list, how it was verified, and every
  assumption you made.

## 5. Repo conventions worth knowing

- The payload is **manually mapped** into `cs2.exe`: no CRT static-init, no exceptions,
  no `<print>`. `payload/diag/crash_handler.cpp` provides `call_guarded` for probes that
  may fault (the image is mapped without exception tables, so `__try` is unavailable).
- The payload reads the engine through **resolved RVAs + raw pointer reads**
  (`payload/game/src/state_reader.cpp`), not through `CreateInterface` vtables.
- The game runs in **session 0** for the agent (no GPU). Never launch `cs2.exe`
  directly; use `schtasks /run /tn CS2Launch`.
- REA (Ghidra 12.1.4) is the static-analysis tool; the REA MCP tools are the
  `tools.rea.*` functions callable from the Code Mode `execute` runtime.
  See `docs/re/README.md`.
