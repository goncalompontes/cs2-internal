# Decisions (ADRs)

Short records of locked design decisions. Append new ones; don't edit history
silently.

## ADR-0001 — Hybrid model, schema-first
**Status:** accepted

External dumper + internal manual-mapped payload. Resolution prefers **internal
schema lookups / `CreateInterface`**; fall back to dumper output, RE'd offsets,
pattern scanning, RIP-relative resolution.

## ADR-0002 — Payload language: C++23 (MSVC ABI)
**Status:** accepted

The target is C++; MSVC ABI needed for vtable/class interop. Built with
`clang-cl`/`lld` against msvc-wine headers. Style follows Rust conventions
(`std::expected`, `std::variant`+`magic_enum`, concepts, newtypes).

## ADR-0003 — Build system: Meson + Ninja
**Status:** accepted

Chosen over CMake. Host and payload are separate Meson projects (payload uses a
cross file).

## ADR-0004 — Ghidra MCP: ReVa
**Status:** superseded by ADR-0012

`cgtudor/reverse-engineering-assistant` (context-rot-aware, assistant +
headless modes) for Ghidra-driven static RE.

## ADR-0005 — Code intelligence: built-in LSP + one tree-sitter MCP
**Status:** accepted

Rely on OpenCode's built-in clangd (`"lsp": true`) instead of a clangd MCP
server (avoids duplicate-clangd index corruption). Add a single lightweight
tree-sitter MCP for token-cheap structural views.

## ADR-0006 — Debuggers
**Status:** accepted

- **Interactive** PE/PDB debugging of the payload: **RAD Debugger**.
- **Agent-automated** dynamic analysis: **gdb + GEF/pwndbg** (native; the
  Wine process is a Linux process).
- Optional later: `x64dbg` + `x64dbg-mcp-server`.

## ADR-0007 — UI: RmlUi behind a swappable `UiBackend`
**Status:** accepted (revisit if integration cost is high)

Slint **cannot** reuse the target's swapchain today (custom backends require
internal/unstable APIs; rendering into a backend texture is an open feature
request #4499). Therefore:
- **In-game overlay:** **RmlUi** (HTML/CSS, DX11 render interface reusing the
  the target's device), hidden behind a `UiBackend` interface. **ImGui** fallback.
- **Desktop control/tools app:** **Slint** (owns its own window).

## ADR-0008 — Tooling languages
**Status:** accepted

Python for glue/analysis, C++ for the payload, Rust only where performance
demands (e.g. the external dumper may be Rust + `memflow`).

## ADR-0010 — Naming & no globals (Rust-style)
**Status:** accepted

Naming follows Rust conventions: `snake_case` for functions/variables/
namespaces, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for constants. No
Hungarian/Windows prefixes (`g_`, `m_`, `p`, `dw`, `b`); `g_pSchemaSystem` is
wrong, `schema_system` is right. Use `enum class` instead of flag ints.

**No global mutable state.** State is passed explicitly or owned by an explicit
context object. Prefer nested namespaces/modules over name prefixes. The
target's own symbol names may appear in docs/comments only, never as our
identifiers.

## ADR-0011 — Symbol renaming pass + target mapping
**Status:** accepted

Everything the dumper emits is run through a **rename pass** (`rename` module)
that turns target symbols into our convention (ADR-0010): strip Source/Hungarian
tags (`C_`/`C`, `m_`/`m_p`/`m_i`/`m_vec`/…, `_t`) and normalize to `snake_case`
identifiers, `PascalCase` types and `SCREAMING_SNAKE_CASE` constants. Field type
displays are rewritten too (`CCSPlayer_BulletServices*` → `CsPlayerBulletServices*`).

The **original target name is always preserved** (`target` on interfaces,
`target_name` on classes/fields) and emitted as a **compile-time mapping**, so
lookups by the game's own name still work: `find_class_by_target`,
`field_offset_by_target`, `find_interface_by_target`. Collisions are resolved
deterministically (`_2`, `_3`, …).

## ADR-0012 — Static RE: REA + global Ghidra
**Status:** accepted

Static RE is driven by **REA** (`rea-agents`, MCP server `rea`,
`github.com/morluto/rea`) against a **global** Ghidra install
(`~/.local/share/ghidra/ghidra_12.1.4_PUBLIC`), registered in the global
OpenCode config with `GHIDRA_INSTALL_DIR` + `JAVA_HOME`.

The project no longer vendors Ghidra: ADR-0004's ReVa toolchain is retired and
the ~1.4 GB download/extraction is removed from the tree. This keeps checkouts
small and decouples RE tooling from the project tree.

A **local patch** to the installed `rea-agents` package adds a Ghidra project
cache (one project per target digest, reused with `-process -noanalysis`) so
large targets are not re-analyzed on every open; upstream REA only offers
exact-query snapshot replay. See `docs/rea-project-cache.md`.

## ADR-0013 — Swapchain hooking: kiero + MinHook
**Status:** accepted

`IDXGISwapChain::Present` is discovered with **kiero** and installed as a
**MinHook inline hook** (`kiero::bind(8, ...)`), following a known-working
reference overlay (Aspasia). Earlier revisions patched the shared `dxgi.dll`
vtable slot directly with the project's own `VtableHook`; that worked but the
overlay could stall the game on window/UI state changes. The inline-hook
approach is the tested one and is kept even though that stall is not yet fully
resolved (see `docs/roadmap.md`, Phase 3).

Vendored under `payload/third_party/kiero` (kiero 1.2.6 + MinHook). Notes for the
cross build: the payload project must declare `c` (MinHook is C), and MinHook's
`__movsb` needs `<intrin.h>` under clang-cl. The ImGui backend uses the Win32
platform backend plus a `WndProc` hook for input, and caches the back-buffer
render target.
