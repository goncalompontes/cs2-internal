# Roadmap

## Phase 0 — Environment (in progress)
- [x] Repo scaffold, ADRs, agent conventions
- [x] Enable built-in LSP
- [x] msvc-wine toolchain + cross-compiled `payload.dll`
- [x] Ghidra + REA installed (global; no project-local copy)
- [x] MCP servers (treesitter + rea connected)
- [x] Offline run loop documented (docs/architecture.md)
- [x] Live-analysis MCP server (`cs2-live`) + build-time SDK generation
- [x] Install the Windows (Proton) build of the target
- [x] Verify offline `-insecure` run

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
- [x] Native Windows live memory backend (`WindowsMemory`:
      `ReadProcessMemory` + `VirtualQueryEx` + toolhelp snapshots); the dumper
      builds and reads a live target on Windows
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
- [x] `IDXGISwapChain` interposition — the swapchain `Present`/`ResizeBuffers`
      method-table slots (and the factory `CreateSwapChain*` slots) are replaced
      after discovery from a throwaway device/swapchain (**ADR-0015**), in place
      of the kiero + MinHook inline hook (DXVK only conditionally reserves a
      hot-patchable `Present` entry).
- [x] Portable Vulkan present layer — `overlay/` (`libcs2_overlay_layer.so` +
      manifest, `CS2_OVERLAY=1`) intercepts `vkQueuePresentKHR` under DXVK and
      renders the shared-memory model with ImGui+Vulkan. Validated in CS2 **with
      no payload** (draws, survives resize stress). See **ADR-0016**. The same
      sources now build a native Windows `cs2_overlay_layer.dll` (registry
      implicit-layer registration; see `overlay/README.md`).
- [~] `UiBackend` abstraction + overlay — the ImGui DX11 backend is retained but
      no longer the default; rendering moved to the Vulkan layer. The Win32
      platform backend/`WndProc` hook are not used (deadlock under Wine).
- [x] **Present-path injection freeze (Wine/DXVK-specific)** — under
      Wine/Proton, any injected payload doing work in the render/present path
      stalled the game on a DXVK swapchain/display-mode change (no frame of ours
      on the stuck stacks); reproduced with DXGI/kiero inline hooks, method-table
      interposition, a dedicated publish thread, and a game-present hook. See
      **ADR-0017**. On **native Windows** (D3D11/DXGI, no DXVK) the payload +
      Vulkan layer ran cleanly and a windowed/fullscreen toggle did not freeze
      the game — see **docs/windows-validation.md**.

## Phase 4 — State introspection
- [x] Entity-system walk — paged identity table (`CGameEntitySystem + 0x10`),
      plus the local player (controller → pawn) via schema offsets
- [x] Local player fields — health/max, armor, team, life state, world origin,
      eye origin, velocity, eye angles, active weapon + clip ammo (reserve is
      read from `m_pReserveAmmo` but its client element is unverified)
- [x] Enemies — every player pawn (matched by the local pawn's vtable, so no
      per-build vtable RVA is hard-coded) with the same fields, plus name and
      distance from the local player
- [x] Debug HUD — `render::HudModel`/`DrawList`, refreshed at ~30 Hz and
      serialized to the shared-memory model (rendered by the Vulkan layer); the
      snapshot is also appended to `C:\cs2-payload.log` for headless verification
- [x] World-to-screen — per-slot P×V matrix (`client.dll + 0x2561cd0`), row-major
      with a **column-vector** multiply (`clip = M · world`); verified live
      (`matrix@view_matrix == matrix@projection · matrix@view`) and covered by
      the host `game-matrix` test
- [x] ESP overlay — UI-agnostic `render::DrawList` (boxes, filled rects, lines,
      text): bounding box, health bar (left), ammo bar (bottom), player name
      (top), weapon name (bottom) and right-side flags (money, armor, helmet,
      kit, scoped, defusing, bomb, flash, ping, distance). Flags/names/weapons
      use an embedded pixel font (Silkscreen, OFL), embedded at build time by
      `scripts/gen-font.py`.
- [~] Control channel (IPC) + scenario runner — **designed** (ADR-0020); see
      `docs/control-harness.md`. RE of the console `exec` primitive is **done**
      (`docs/re/engine2.md`): in-process `Cbuf_AddText` (`FUN_1801b8790`,
      RVA `0x1b8790`) is the primary path; `-concommandpipe` is the
      no-injection fallback (input-only). Implemented: none yet.

## Phase 5 — Interfaces, events, deeper introspection
- [ ] `Source2Client002`, `GameEventSystemClientV001`
- [ ] Tick/frame hook
- [ ] Full scripted/interactive session instrumentation
