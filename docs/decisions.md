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

Chosen over CMake. Host and payload are separate Meson
projects (payload uses a cross file).

## ADR-0004 — Ghidra MCP: ReVa
**Status:** accepted

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

