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
      kit, scoped, defusing, bomb, flash, ping, distance). All text uses the
      stock Dear ImGui font with antialiasing off; every ESP element except the
      player name is outlined.
- [x] In-game menu — `Insert` (or `Delete`, for keyboards where Insert needs Fn)
      opens a tabbed Dear ImGui window (ESP / Movement / Interface /
      About) with toggles for the ESP elements and the payload's movement features
      (auto-strafe, bunnyhop); the settings round-trip through
      the shared segment to the payload (**ADR-0021**).
- [~] Control channel (IPC) + scenario runner — **control core + input + game
      events implemented (events validated live)** (ADR-0020). `payload/control/`
      (`ping`/`state`/`exec`/`events`/`wait_for`/`quit` over a named pipe) +
      `scripts/scenario/` Python runner with `look`/`press`/`noclip`/`teleport`
      (engine `setang` + button commands through `exec`; no OS input, no hooks)
      and game events (dispatch vtable hooks on the client's
      `CGameEventManager`, with names *and arguments* read via the engine's own
      field-name hash, validated against the build's `resource/*.gameevents`).
      `exec` uses the in-process `Cbuf_AddText`
      (`FUN_1801b8790`, RVA `0x1b8790`; see `docs/re/engine2.md`); game events
      use the `game_event_manager` signature (`docs/re/client-dll.md`).
      Scenarios `join-bot-match`, `move-and-aim` and `game-events` run end to
      end; host test `control-core` is green. Remaining: console-output capture.
      See `docs/control-harness.md`.

## Phase 5 — Interfaces, events, deeper introspection
- [ ] `Source2Client002`, `GameEventSystemClientV001`
- [x] `CreateMove` / `CUserCmd` input path RE — `CCSGOInput` singleton + vtable,
      `CreateMove` (slot 5 `+0xb64e60`, slot 25 `+0xd01610`), `CUserCmd` layout
      (stride `0x98`, buttons `+0x60`), `GetUserCmd` chain; signatures in
      `analysis.rs` (see `docs/re/client-dll.md`).
- [~] `usercmd` input backend — `payload/control/input.{hpp,cpp}` hooks
      `CreateMove`; writes `cmd->buttons` (discrete: jump/bhop) and the sub-tick
      `view_angles` (`look(..., via="usercmd")`, silent); `press`/`release`/
      `bhop`; `on_ground` predicate; `awaiting.py` readiness helpers.
      **Validated live** (`usercmd-move`: jump + bhop; `subtick-aim`: silent angle
      write; `subtick-shot`: the **shot follows the silent angle** — camera 45°
      off a bot, silent aim, health drop). The sub-tick angle write covers every
      `input_history` entry. **Analog movement validated live** (2026-10-15):
      `move(forward=1.0)` walks 204 units/s (1.01× the console `+forward`) and
      `move(forward=0.5)` walks 115 (0.57×) — the engine's units are `±1`, not
      450; `movement-probe` pins a clear test spot and reports a console baseline.
- [~] Sub-tick RE (`docs/re/subtick.md`) — `CSGOUserCmdPB` / `CBaseUserCmdPB` /
      `CSGOInputHistoryEntryPB` / `CSubtickMoveStep` layouts (offsets + field
      numbers confirmed against the public proto and the build's parser), the
      `CCSGOInput` history, the command build (`create_move_input`) and the
      movement consumer (`CPlayer_MovementServices::DoMovement::Subtick`).
      `view_angles` implemented (all history entries); analog movement validated
      live (`move(1.0)` = 204 units/s = 1.01× the console path, `move(0.5)` = 115 =
      0.57×; the engine's units are `±1`) — `subtick-move`, `movement-probe`.
- [~] **Auto-strafe / silent sub-tick movement** (`payload/control` `strafe`,
      `docs/re/subtick.md` §"The movement yaw channel") — implemented (airborne-only:
      community air-strafe wish angle applied as **silent** sub-tick `yaw_delta` steps
      + full-forward analog in the rotated frame) and the **mechanism is measured**
      (analog write = 1.01× the engine's walk; a 90° yaw step turns the walk 89.9°
      with the eye angles untouched). **But the menu-enabled feature does not work
      in-game yet** (operator-confirmed 2026-10-15) — next probe: also write the raw
      axes (`CCSGOInput + 0x270/0x274`) from the strafe, which `create_move_input`
      re-derives the base floats from. See the wrap-up at the top of `handoff.md`.
- [~] **Payload-native movement controls** — `auto_strafe`/`bhop` live in
      `wire::Settings` (overlay menu's `Movement` tab, version 4) and the payload
      re-applies them every frame, so the menu is the single owner; the settings
      plumbing is validated (seqlock write → payload flags), the *effect* is not.
- [x] Screen/round state machine (`scripts/scenario/screens.py`) — the payload
      answers "is a level up?" with the **main-screen rule** (the level's
      `C_CSGameRulesProxy` entity exists, `state.level`/`state.in_game`; the
      `game_rules` global is readable at the main menu too, measured 2026-10-15)
      and reports the round phase from `C_CSGameRules` (`state.rules`). The
      harness reduces that to one screen (not-ready / not-in-game / team-select /
      spectator / warmup / freeze / live / dead / round-end) with per-screen
      capabilities and the transitions between them. Scenarios gate on
      `require("move")` / drive `ensure(Screen.LIVE)`; the `screens` scenario
      tests the main-screen rule, then walks `→ LIVE → kill → DEAD → LIVE`.
- [ ] Tick/frame hook
- [ ] Full scripted/interactive session instrumentation

## Phase 6 — Ragebot (aim)
- [x] Shot-model RE — the spread cone is **deterministic**
      (`RandomSeed(cmd_seed)`); inaccuracy terms, trace API, hitboxes and
      penetration parameters (`docs/re/aim.md`)
- [x] Collision-geometry RE — `vphysics2` Rubikon hulls/meshes are readable
      (shape type `+0x18`, hull pointer `+0xC0`; `docs/re/vphysics2.md`)
- [x] Design / direction locked — a deterministic-spread **damage field +
      heat map** (not multipoint): Tier 1 few-trace, Tier 2 zero-trace
      (**ADR-0022**, `docs/ragebot.md`)
- [x] Lag-comp / backtracking RE — rewind tick, per-entity lag records,
      interpolation history (`docs/re/lagcomp.md`)
- [~] Tier 1 — `payload/aim` implements the hitbox OBB projection + the damage
      field + the optima (host-tested: `aim-geometry`, `aim-field`, `aim-solver`);
      the engine trace probe (`TraceShapeWithSortedResults`) and harness
      validation remain
- [ ] Tier 2 — map the overlap record → shape pointer, cut build signatures
      (`RnHull_t` arrays resolved, `docs/re/vphysics2.md`)
- [ ] Backtracking — per-enemy record ring + lag-comp window/selection
      (`payload/aim/records.hpp` done + host-tested; wire it into the per-frame
      path)
- [ ] Occlusion — the engine trace probe (`docs/re/aim.md` §"Calling the trace
      from the payload"); the solver already consumes occluders
- [x] Studio hitbox set — the real per-hitbox **capsules** (bone-local endpoints +
      radius, resolved bone + hitgroup), live-validated (`docs/re/aim.md`
      §"The runtime studio hitbox set"); the collision-AABB proxy is now only a
      fallback
- [x] `aim` control command — the solve, live-validated (`aim-probe`, 19 boxes,
      **0.02°** to the head); `write: true` applies it to the silent sub-tick view
      angles
- [x] Ragebot keybind — a hold-key ragebot (`rage` command + the overlay menu's
      **Ragebot** tab): render-thread solve, per-tick silent angle write +
      `IN_ATTACK` (asserted **persistently**, so auto fire is reliable); live-
      validated (`rage` scenario)
- [x] Weapon model from the game — the solve reads the shooter's real
      `CCSWeaponBaseVData` (damage / headshot / penetration / range modifier)
      instead of a fixed profile (`docs/re/aim.md`)
- [~] Hitbox renderer — draws the real studio capsules on screen, but the result
      is an unreadable tangle; **off by default**, open problem (`docs/ragebot.md`
      §0/§10)
- [ ] Occlusion — the engine trace probe: the call convention is pinned from a
      live site (`docs/re/aim.md` §"The call convention"), but it still needs
      signatures + live validation; the solver already consumes occluders
- [ ] Pose-uncertainty model + analytic polygon blur; pellet-pattern
      registration (`K>1`)
