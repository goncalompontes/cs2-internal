# Native Windows build — validated

**Status: the native Windows build has been run on a real Windows host (2026-10,
CS2 build 25815307 / patch 1.41.9.0).** The historic "untested" marker (build
define message, boot banner, marker-file value, HUD suffix) has been removed and
this doc supersedes `docs/windows-untested.md`.

## What was verified

Launched from the interactive session via the `CS2Launch` scheduled task
(`scripts/launch-cs2.cmd`, `-insecure -novid -vulkan`), then manual-mapped the
payload with `injector.exe`:

- **Injection** — `payload.dll` maps, all imports resolve, `.pdata` registers
  (2043 entries), entry returns 0, the game stays alive.
- **Live calibration** — `scripts/calibrate-live.py` dumps interfaces
  (client 7 / engine2 34 / schemasystem 1), the schema (1492 classes), and the
  curated global offsets; `scripts/gen-sdk.py` renders `payload/sdk`.
- **State** — the payload reads the local player (team, health, armor, weapon,
  ammo, origin, eye origin, velocity, eye angles) and every other player, live,
  through the updated build.
- **Overlay** — the portable Vulkan layer (`cs2_overlay_layer.dll`) loads as an
  implicit layer, maps the shared segment, and renders the HUD + ESP into CS2's
  swapchain (`drew frame … draws=70`), visible on screen.
- **Display-mode change** — toggling windowed/fullscreen did **not** freeze the
  game (no `[freeze]` reports; the layer kept presenting). The ADR-0017 freeze
  did not reproduce on native Windows.
- **Control channel (2026-10)** — the payload's named-pipe control channel
  (`payload/control/`, ADR-0020) answered `ping`/`state`/`exec`/`wait_for` over
  `\\.\pipe\cs2-internal-control`, and the `scripts/run-scenario.py
  join-bot-match` scenario drove a full offline bot match end to end (launch via
  `CS2Launch` → inject → `map de_dust2` → add bots → assert state). See
  `docs/control-harness.md`.

## Issues found and fixed

- **API-set imports.** `payload.dll` links the UCRT through the virtual
  `api-ms-win-crt-*` API-set stubs, which never appear in the loader list, so
  the injector's module lookup failed. `injector/src/main.cpp` now maps
  `api-ms-win-crt-*` → `ucrtbase.dll` (and `api-ms-win-core-*` →
  `kernelbase.dll`).
- **Output paths.** The payload wrote its marker, log and shared segment to
  `C:\` root, which a **Medium-integrity** game process cannot write (only
  elevated processes can). They now live under the payload's writable data dir
  `%LOCALAPPDATA%\cs2-internal` (override `CS2_PAYLOAD_DIR`); the overlay layer
  resolves the same path on Windows and under Proton.
- **`IMM32.dll` import (2026-10).** Dear ImGui's default Win32 IME/clipboard/shell
  handlers pulled `IMM32.dll` (and clipboard `USER32`/`SHELL32` symbols) into the
  payload's import table; the game does not load `IMM32` at boot, so the injector
  could not resolve it. They are disabled in
  `payload/third_party/imgui/imconfig.h` (the overlay builds no Win32 platform
  backend). The injector also runs with `--load-missing` for robustness.
- **`injector.exe` quarantined.** Windows Defender quarantines the manual-map
  injector *after* it runs, so a second run fails with "injector not built".
  Rebuild it, or add a folder exclusion (environmental, not a build issue).

See `docs/handoff.md` for the current state and `docs/decisions.md` (ADR-0018,
ADR-0019).
