# Roadmap

## Phase 0 — Environment (in progress)
- [x] Repo scaffold, ADRs, agent conventions
- [x] Enable built-in LSP
- [x] msvc-wine toolchain + cross-compiled `payload.dll`
- [x] Ghidra + REA installed (global; no project-local copy)
- [x] MCP servers (treesitter + rea connected)
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
- [x] `IDXGISwapChain::Present` hook — discovered via **kiero** and installed as
      a **MinHook inline hook** (`kiero::bind`, index 8), matching a known-working
      reference overlay rather than patching the shared vtable slot. Engages only
      when DXGI is loaded.
- [~] `UiBackend` abstraction + overlay — ImGui backend (DX11 + Win32 platform
      backends, cached back-buffer RTV, `WndProc` hook for input) renders a window
      into the game's device and is **live-validated**. **Known quirk (accepted
      for now):** a window/UI state change (resize, fullscreen toggle, opening
      some menu panels) can stall the game — `Present` stops being reached. It
      reproduces with the payload mapped and has been observed even with the
      draw disabled and with a pure pass-through detour, so it is not the ImGui
      draw itself; the render thread ends up blocked inside DXVK's Present while
      CS2's main thread waits in `CRenderContextBase::Present` /
      `CSwapChainBase::QueuePresentAndWait`. Next: keep working the swapchain
      lifecycle (recreation/`ResizeBuffers`) or move the overlay off the present
      path. RmlUi pending.

## Phase 4 — State introspection
- [x] Entity-system walk — paged identity table (`CGameEntitySystem + 0x10`),
      plus the local player (controller → pawn) via schema offsets
- [x] Local player fields — health/max, armor, team, life state, world origin,
      eye origin, velocity, eye angles, active weapon + clip/reserve ammo
- [x] Enemies — every player pawn (matched by the local pawn's vtable, so no
      per-build vtable RVA is hard-coded) with the same fields, plus name and
      distance from the local player
- [x] Debug HUD — ImGui window rendering the snapshot
      (`payload/game` → `render::HudModel` → `UiBackend`), refreshed at ~10 Hz;
      the snapshot is also appended to `C:\cs2-payload.log` for headless
      verification
- [ ] Control channel (IPC) + scenario runner

## Phase 5 — Interfaces, events, deeper introspection
- [ ] `Source2Client002`, `GameEventSystemClientV001`
- [ ] Tick/frame hook
- [ ] Full scripted/interactive session instrumentation
