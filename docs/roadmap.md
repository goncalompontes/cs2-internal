# Roadmap

## Phase 0 — Environment (in progress)
- [x] Repo scaffold, ADRs, agent conventions
- [x] Enable built-in LSP
- [ ] Install toolchain: msvc-wine, Ghidra + ReVa
- [ ] Add MCP servers (tree-sitter, ReVa)
- [ ] Steam + multilib + Proton; offline `-insecure` loop
- [ ] Install the Windows (Proton) build of the target

## Phase 1 — External dumper (no injection)
- [ ] Module enumeration + fingerprinting
- [ ] Pattern scanner + RIP-relative resolution
- [ ] schema walker → generated headers
- [ ] `CreateInterface` resolver
- [ ] Emit `offsets.json` / `interfaces.hpp` / `schemas/*.hpp`
- [ ] Fixture-based unit tests

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
