# Roadmap

## Phase 0 — Environment (in progress)
- [x] Repo scaffold, ADRs, agent conventions
- [x] Enable built-in LSP
- [x] msvc-wine toolchain + cross-compiled `payload.dll`
- [x] Ghidra + ReVa installed
- [x] MCP servers (treesitter + reva connected)
- [x] Offline run loop documented (docs/architecture.md)
- [ ] Install the Windows (Proton) build of the target
- [ ] Verify offline `-insecure` run

## Phase 1 — External dumper (no injection)
- [x] Module enumeration + fingerprinting
- [x] Pattern scanner + RIP-relative resolution
- [x] Fixture-based unit tests (pattern, interfaces, schema)
- [x] Basic CLI (`--pattern`, `--exports`, `--interfaces`, `--schema`, `--json`)
- [x] schema walker (traversal + JSON emit; layout needs calibration)
- [x] `CreateInterface` resolver (seed-based)
- [ ] Header generation (`interfaces.hpp`, `schemas/*.hpp`) from JSON
- [ ] Calibrate `SchemaLayout` + resolve schema-system address against the live build
- [ ] Optional `memflow` backend behind `ProcessMemory`

## Phase 2 — Manual-map loader
- [ ] PE map (sections, relocs, imports, TLS)
- [ ] Load a trivial logging DLL into the offline target process
- [ ] Bypass `LoadLibrary` block / signature check via manual mapping

## Phase 3 — Hook engine + overlay
- [ ] Hook engine (vtable + inline)
- [ ] `IDXGISwapChain::Present` hook (DX11 under DXVK)
- [ ] `UiBackend` abstraction + RmlUi (or ImGui) blank overlay

## Phase 4 — State introspection
- [ ] Local player fields via schema offsets
- [ ] Debug HUD (position/velocity/health/money/…)
- [ ] Control channel (IPC) + scenario runner

## Phase 5 — Interfaces, events, deeper introspection
- [ ] `Source2Client002`, `GameEventSystemClientV001`
- [ ] Tick/frame hook
- [ ] Full scripted/interactive session instrumentation
