# C++ Style Guide

Practical house style for this repo. It documents what the tree actually does,
and calls out the few places where the code deviates from the normative ADR so
you know which is authoritative.

The cross-project baseline is the user-level `cpp-coding-standards` skill. This
guide records this repository's overrides and toolchain-specific rules; where
the project does not specify a choice, follow the shared baseline.

**Normative sources, in order:**

1. `.clang-format` — formatting is mechanical; don't argue about layout, run the
   formatter.
2. `.clang-tidy` — the check set the editor and the automated sweep use.
3. `docs/decisions.md` **ADR-0010 (Naming & no globals, Rust-style)** — naming
   and the "no global mutable state" rule.
4. This file — everything else (idioms, file layout, errors, tests, tooling).
5. The user-level `cpp-coding-standards` skill — shared defaults only where
   this repository's sources do not specify a rule.

When this file and an ADR disagree, the ADR wins; open a PR to fix this file.

---

## 1. Formatting (fully automated)

`.clang-format` is the single source of truth. Highlights:

- **2-space indent**, **100-column limit**, no tabs.
- **Attach braces** (K&R): `void f() {`, `if (x) {`.
- **Left-aligned pointers/references and qualifiers**: `const char* p`,
  `const T& v` — never `char *p` or `T const& v`.
- Namespace bodies are **not** indented; closers carry a comment:
  `}  // namespace cs2::hooks`.
- Access specifiers are indented one space (` public:`) — clang-format manages
  this; don't hand-tune.
- Includes are sorted **within each existing block** but blocks are **never
  regrouped** (`IncludeBlocks: Preserve`). This is deliberate: include *order* is
  load-bearing, e.g. `<windows.h>` must precede `<intrin.h>`.
- `Standard: Latest` (the bundled clang-format rejects the literal `c++23`).

Apply it with `clang-format -i <files>`, or let CLion do it (Settings ▸ Editor ▸
Code Style ▸ C/C++ ▸ Clang-Format). Do not reformat whole files as a drive-by; it
adds review noise.

---

## 2. Naming (ADR-0010)

Rust-style, **no Hungarian/Windows prefixes** (`C_`, `m_`, `g_`, `p`, `dw`, `b`).

| Kind | Convention | Example |
|---|---|---|
| Types / classes / structs | `PascalCase` | `StateReader`, `HudSection`, `InlineHook` |
| Functions / methods | `snake_case` | `read_player`, `rva_to_offset_inner`, `install` |
| Variables, params, namespaces | `snake_case` | `local_controller`, `cs2::hooks` |
| Public struct fields | `snake_case`, no suffix | `virtual_size`, `rip_disp` |
| Private class members | `snake_case` + trailing `_` | `installed_`, `trampoline_`, `overlay_` |
| Constants / `constexpr` | `SCREAMING_SNAKE_CASE` | `DOS_MAGIC`, `PAGE_COUNT`, `TEB_TLS_POINTER` |
| `enum class` values | `snake_case` | `InlineError::unsupported_instruction` |
| Macros | avoid; if unavoidable, `SCREAMING_SNAKE_CASE` | `CS2_NATIVE_WINDOWS_BUILD` |

Notes:

- **The target's own symbol names never become our identifiers** — they may
  appear in comments/docs only (ADR-0010/0011). The dumper's rename pass
  (`rename`) enforces this for generated SDK headers.
- Prefer `enum class` over flag `int`s.
- **Deviations you will see** (know them, don't copy the bad ones):
  - `host_probe.cpp` uses `PascalCase` enum values (`ParseError::Empty`). New
    code uses `snake_case`; `host_probe` predates the rule.
  - Some existing constants use the former `k_snake_case` spelling. Treat them
    as legacy: new constants use `SCREAMING_SNAKE_CASE`; do not mass-rename old
    identifiers as drive-by cleanup.
  - The **Vulkan layer** (`overlay/`) and a few tests keep `g_`-prefixed file
    statics and globals (`g_lock`, `g_devices`, `g_original`). These are C-ABI
    entry points / fixtures where an explicit context object is impractical;
    this is the accepted exception to "no global mutable state", not a licence to
    add globals elsewhere.

---

## 3. Language & idioms

- **C++23** (the shared cross-project baseline is C++20; this repo opts into
  C++23. `meson.build` selects `--std=c++23` for GCC/Clang and
  `-std:c++latest` for `clang-cl`/`cl.exe`).
- **Always fully qualify**: `std::uint32_t`, `std::span`, `std::size_t`. No
  `using namespace std;`.
- **Errors are values, not exceptions.** Fallible operations return
  `std::expected<T, E>` ("parse, don't validate"); operations that cannot fail
  are marked `noexcept`. Use `std::optional<E>` when the failure has no payload
  (see `InlineHook::install`).
- **Error enums** are `enum class` with a companion
  `[[nodiscard]] const char* to_string(Error) noexcept;`.
- **`[[nodiscard]]`** on anything whose result matters (accessors, factory-style
  helpers); `noexcept` on non-throwing code.
- **Value-initialise with `{}`**: `std::uint32_t virtual_size{};`,
  `unsigned char saved_[16]{};`.
- **Casts**: `static_cast` for numeric/enum conversions, `reinterpret_cast` for
  pointer punning. No C-style casts.
- **`auto` where the type is evident** from the initialiser
  (`const auto length = static_cast<std::uint16_t>(...)`); spell the type out
  when it carries meaning.
- **Trailing return types** are the house style for declarations/definitions
  (`auto parse_hex(std::string_view text) -> std::expected<...>`). This was
  applied across the tree; keep new signatures consistent.
- **Rule of zero**: prefer value types; when a class owns a resource, make it
  non-copyable/non-movable explicitly with `= delete` (see `InlineHook`).
- **No new global mutable state** (ADR-0010). Where a documented ABI or test
  constraint requires shared state, keep it behind a typed accessor/wrapper;
  use thread-safe one-time initialization (`static` local or `std::call_once`)
  and separately synchronize every later mutation. Initialization-once is not
  mutation safety. Since this repo returns errors as `std::expected` and does
  not use exceptions, a once initializer returning an error normally still
  counts as initialized; choose deliberately whether to cache or retry it. Avoid
  re-entering the same lazy accessor during initialization. Do not widen the
  existing C-ABI exceptions.
- **Prefer iterator/range protocols**: use standard algorithms, range-for, and
  C++20/23 ranges when they make the operation clearer. Keep views within the
  lifetime of their source when they borrow, and avoid repeated traversal of
  single-pass ranges. Straight loops remain appropriate for complex control
  flow and measured hot paths.
- **Prefer composition over implementation inheritance**. Use public
  inheritance for genuine substitutability or deliberate interfaces, not just
  to reuse code.
- **Use generic programming for real reuse**: constrain templates with concepts
  and range/iterator requirements. Prefer `constexpr` functions for value
  computation; keep complex type-level metaprogramming deliberate and reviewable
  because template complexity affects diagnostics and build times.
- **Platform-independent core must not include `<windows.h>`** — `payload/loader`
  and `payload/game` are built both into the Windows payload and into host tests,
  so they stay free of OS headers. Guard truly platform-specific code with
  `#if defined(_WIN32)` and provide a host stub in the `#else` branch.

---

## 4. File layout

```cpp
#pragma once

// One-line purpose, then "why" — the non-obvious constraints. Cite the ADR if
// this file implements one (e.g. "// See ADR-0016.").

#include <cstddef>   // angle includes

#include "cs2/hooks/inline.hpp"  // quoted project includes

namespace cs2::hooks {

enum class InlineError { too_short, unsupported_instruction };

class InlineHook {
 public:
  InlineHook() = default;
  ~InlineHook();
  // ...
 private:
  void* trampoline_ = nullptr;
};

}  // namespace cs2::hooks
```

- Headers are **`#pragma once`**.
- **Anonymous namespace** for internal linkage in `.cpp` files
  (`namespace { ... }`).
- Free helpers that are pure and platform-independent live in a named nested
  namespace, not at file scope.
- `// --- Section ---` divider comments are used for long files.

---

## 5. Comments

- Explain **why**, not what. The tree rewards a short paragraph at the top of a
  file describing the constraint the code works around (e.g. "a static here
  would use the CRT's thread-safe-init TLS, which is unreliable in a mapped
  image").
- Reference the ADR: `// See ADR-0015.` when behaviour is not obvious from code.
- Use `///` for API members that deserve it; don't restate the signature.
- Keep the `}  // namespace x` and aligned trailing comments — clang-format
  maintains them.

---

## 6. Tests

Host tests live next to the component (`payload/hooks/tests/`,
`payload/loader/tests/`) and are wired via Meson `test(...)`.

Style (see `test_inline.cpp`):

- `#include <cassert>`, then the component header.
- Fixtures and helpers in an `namespace { ... }`.
- `assert(...)` for checks; `auto main() -> int {` returning `0`/`1`.
- Print a single short status line on success: `std::puts("inline: ok");`.
- For tests that hook real machine code, pin codegen so bytes are stable:
  `__attribute__((noinline, optnone))` under clang, `optimize("O0")` under GCC.

---

## 7. Automated style fixes (`scripts/clang-tidy-fix.py`)

Mechanical style/modernisation changes are applied by the staged sweep rather
than by hand. It's a thin, cross-platform driver over `clang-tidy -fix`, so the
same result is reproducible on CI.

```bash
python scripts/clang-tidy-fix.py --list-groups          # what's available
python scripts/clang-tidy-fix.py                        # dry run (default)
python scripts/clang-tidy-fix.py --group const --apply  # write one group
python scripts/clang-tidy-fix.py --apply --format       # + whole-file format
python scripts/clang-tidy-fix.py --list-gate            # the gated groups
python scripts/clang-tidy-fix.py --check                # the gate
```

**Groups** (ordered cheapest/most-mechanical first): `modernize-safe`,
`modernize-types`, `const`, `nodiscard`, `performance`, `readability`,
`modernize-extra`, `readability-extra`, `misc-extra`, `trailing-return-type`.

**The check mode and the gate.** `--check` runs the selected groups **without
writing anything** and fails when a selected group reports a finding. Without
`--group`/`--checks` the selected groups are the **gated** ones
(`GATED_GROUPS` in the script; `--list-gate` prints them), so the command is
the regression gate: a change that introduces a finding under a gated group
fails it. `--gate NAME` replaces the gated set — widening (or narrowing) the
gate is a deliberate act, done in the same commit that clears the group. The
gate is what CI runs (`.github/workflows/ci.yml`), and the decision it turns
into an exit code has its own unit test
(`scripts/scenario/tests/test_clang_tidy_fix.py`).

A translation unit that does not compile is **not** a finding of a group: the
sweep reports it as a failed translation unit ("failed on N translation
unit(s)"), which is where a compile error belongs.

**Rules of engagement:**

1. **One group at a time**, rebuild, `git commit` between groups. The sweep is
   idempotent, so re-running is safe.
2. **Never trust a fix that changes semantics.** Always run the verify loop
   (below) before committing a group.
3. **`--format` normalises the whole touched file.** The tree is not fully
   clang-format-clean historically, so expect reflow churn; prefer the default
   (clang-tidy only reformats the lines it rewrites, via `FormatStyle: file`).
4. **`README` of exclusions** — checks deliberately *not* in any group:
   - `modernize-use-integer-sign-comparison`: rewrites
     `x != static_cast<std::size_t>(-1)` to `std::cmp_not_equal(x, -1)`, which is
     *always true* for unsigned `x`. This broke the inline-hook displacement
     sentinel (crashed `hooks-inline`). Run only with per-hunk review.
   - `modernize-use-std-print`: pulls `<print>` (CRT/iostream machinery) into the
     **mapped payload**, which the code deliberately avoids.
   - `readability-convert-member-functions-to-static`: its fix emits
     `static [[nodiscard]] auto f(...)`, and an attribute list may not appear
     there — the attribute must precede the decl-specifier-seq
     (`[[nodiscard]] static auto f(...)`). The result does not compile, so the
     check is excluded from the `readability` group.
   - `modernize-use-ranges`: its fix rewrites
     `erase(std::remove_if(b, e, p), e)` into
     `erase(std::ranges::remove_if(b, p))`, but `ranges::remove_if` returns a
     *subrange*, not an iterator, so the result does not compile. The intent is
     right and the correct form is `std::erase_if(b, p)` (C++20) — apply it by
     hand where it is wanted. Excluded from the `modernize-extra` group.
5. **Verify loop** after applying any group:

   ```sh
   # Host (native Windows builds need MSVC's lib.exe on PATH).
   meson compile -C build && meson test -C build
   # Payload / overlay / injector (cross or native).
   meson compile -C build-payload
   meson compile -C build-overlay
   meson compile -C build-injector
   ```

   On this machine a plain SSH/session-0 shell lacks `lib.exe`; prepend
   `...\VC\Tools\MSVC\<ver>\bin\Hostx64\x64` to `PATH` for the host build.

CLion users get the same checks live in the editor (`.clang-tidy` is preferred
over IDE settings) and can batch them via **Code ▸ Code Cleanup**.

---

## 8. Quick checklist

- [ ] Ran `clang-format` (or CLion) — layout is not a review topic.
- [ ] Names follow §2; no `g_`/`m_`/Hungarian; target names only in comments.
- [ ] New constants are `SCREAMING_SNAKE_CASE`; new types `PascalCase`; new members
      `snake_case_`.
- [ ] Fallible calls return `std::expected`/`std::optional`; non-throwing code is
      `noexcept`.
- [ ] No new global mutable state; approved shared state has typed access and
      synchronization for every mutation.
- [ ] Algorithms use ranges/iterators where they clarify intent; borrowed views
      do not outlive or become invalid with their source.
- [ ] Inheritance models real substitutability; code reuse uses composition or
      appropriate generic algorithms instead.
- [ ] `[[nodiscard]]` on result-carrying helpers; `{}` value-init; `static_cast`.
- [ ] Platform-specific code behind `#if defined(_WIN32)` with a host stub.
- [ ] Style bulk changes went through `scripts/clang-tidy-fix.py`, one group per
      commit, with the §7 verify loop green.
