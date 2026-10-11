# Roadmap

## Phase 0 — Environment (in progress)
- [x] Repo scaffold, ADRs, agent conventions
- [x] Enable built-in LSP
- [x] msvc-wine toolchain + cross-compiled `payload.dll`
- [x] Ghidra + ReVa installed
- [x] MCP servers (treesitter + reva connected)
- [x] Offline run loop documented (docs/architecture.md)
- [x] Live-analysis MCP server (`cs2-live`) + build-time SDK generation
- [ ] Install the Windows (Proton) build of the target
- [ ] Verify offline `-insecure` run

## Phase 1 — External dumper (no injection)
- [x] Module enumeration + fingerprinting
- [x] Pattern scanner + RIP-relative resolution
- [x] Fixture-based unit tests (pattern, interfaces, schema)
- [x] Basic CLI (`--pattern`, `--exports`, `--interfaces`, `--schema`, `--json`)
- [x] schema walker (traversal + JSON emit; layout needs calibration)
- [x] Offline `--file` analysis mode (no process needed)
- [~] `CreateInterface` resolver — enumerates `{create, name}` registration
      entries (134 in client.dll, 116 in engine2.dll, build 25815307). The a2x
      reference list is stale for this build (e.g. `GameClientExports001` has no
      reference in the image), so cross-checking needs a current dump.
- [x] Header generation (`interfaces.hpp`, `schemas/*.hpp`, `offsets.hpp`) from JSON
- [x] Calibrate `SchemaLayout` + resolve schema-system address against the live build
- [ ] Optional `memflow` backend behind `ProcessMemory`

## Phase 2 — Manual-map loader
- [x] Portable PE parser (headers, sections, imports, relocations, exports) — fixture-tested
- [x] Manual-map core (section mapping + base relocations) — fixture-tested
- [x] Map the image into real remote memory (local map + `WriteProcessMemory`)
- [x] Import resolution against target modules (remote export-table walk, incl. forwarders)
- [x] Register the image in the target's PEB `Ldr` loader list
- [x] TLS — allocate a slot in the target and set the entry thread's slot via
      `gs:[0x58]` in the stub before `DllMain` (the payload has a TLS directory)
- [~] Exception handling — `.pdata` tables are not yet registered
      (`RtlAddFunctionTable`); fine for non-throwing code
- [x] Load a DLL (trivial + the real `payload.dll`) into a target process
- [x] Bypass `LoadLibrary` block / signature check via manual mapping

## Phase 3 — Hook engine + overlay
- [x] Hook engine — vtable + inline (both host-tested)
- [x] `IDXGISwapChain::Present` hook (vtable index 8; swapchain vtable via a
      throwaway D3D11 device; engages only when DXGI is loaded)
- [~] `UiBackend` abstraction + overlay — ImGui backend (vendored, dx11+win32)
      renders a window into the game's device; RmlUi + live validation pending

## Phase 4 — State introspection
- [ ] Local player fields via schema offsets
- [ ] Debug HUD (position/velocity/health/… values)
- [ ] Control channel (IPC) + scenario runner

## Phase 5 — Interfaces, events, deeper introspection
- [ ] `Source2Client002`, `GameEventSystemClientV001`
- [ ] Tick/frame hook
- [ ] Full scripted/interactive session instrumentation
