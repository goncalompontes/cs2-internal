# Hand-off — overlay / payload state

> **Validated on native Windows (2026-10, CS2 build 25815307).** The payload was
> manual-mapped into CS2 on a real Windows host and read live player state; the
> Vulkan overlay layer rendered the HUD + ESP into the swapchain; a
> windowed/fullscreen toggle did not freeze the game. See
> [docs/windows-validation.md](windows-validation.md). The historic "native
> Windows — untested" marker has been removed.

Read alongside `decisions.md` **ADR-0015/0016/0017/0018/0019/0020**.

> **Active feature:** the programmatic scenario/control harness. The RE is done
> and the **control core is implemented and validated live** (`payload/control/`
> + `scripts/scenario/`; the `join-bot-match` scenario drove a real game).
> Resume from [`docs/control-harness.md`](control-harness.md) (design, decisions,
> validation notes, next actions) and [`docs/re/engine2.md`](re/engine2.md) (RE of
> the engine console-command path). Next: console-output capture, then
> `look`/`press`.

## One-line summary

The portable Vulkan present layer (`overlay/`) renders the overlay from shared
memory and is proven in isolation. On native Windows (D3D11/DXGI, no DXVK) the
full stack now works: the injected payload reads game state and publishes a
`wire::Model`, and the Vulkan layer renders it — **no freeze on the display-mode
change that wedged the game under Wine/Proton (ADR-0017)**.

## What is proven

- **`overlay/` (Vulkan present layer)** — intercepts `vkCreateSwapchainKHR` /
  `vkQueuePresentKHR`, renders the `wire::Model` (HUD + `DrawList`) with
  ImGui+Vulkan, and draws a real HUD + ESP in CS2. Validated on native Windows
  with the payload (`-vulkan`, implicit layer registered under
  `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers`, `CS2_OVERLAY=1`) and, earlier,
  without a payload under Proton.
- **State → shm model** — `ModelPublisher` serializes `render::OverlayModel`
  into the shared segment (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm` on
  native Windows; under Proton the prefix's `drive_c`); the layer writes the
  presented extent back so the payload can project world coordinates.
- **State introspection** — local player + every player (health, armor, team,
  weapon, ammo, origin/eye/velocity/angles), live-verified on the updated build.
- **Payload plumbing** — injector `.pdata` registration (`RtlAddFunctionTable`,
  ~2043 entries), per-thread TLS bootstrap for the manually-mapped image
  (`payload/tls`), `NtCreateThreadEx(SKIP_THREAD_ATTACH)` thread helper, crash
  handler + freeze watchdog (`payload/diag`).
- **RE** — the game's present is `CRenderContextBase::Present`
  (`rendersystemdx11.dll + 0x16dd0`, build 25815307), dispatching to
  `CRenderContextDx11::Present` at `*(this)+0x510`; the swapchain wrapper is
  `CSwapChainDx11`. **Do not inline-hook it** — its prologue has RSP-relative
  home stores the epilogue reads back; use a vtable-slot hook or a
  stack-correct hook (ADR-0017).

## Windows notes (learned 2026-10)

- **Session 0 has no GPU.** The agent/SSH shell cannot start the game; use the
  `CS2Launch` scheduled task (`scripts/launch-cs2.cmd`), which runs in the
  interactive session. See `AGENTS.md`.
- **Medium integrity.** The game runs as the standard user; it cannot create
  files in `C:\` root. All payload/overlay output paths must be under
  `%LOCALAPPDATA%` (or another user-writable dir).
- **API-set imports.** The injector maps `api-ms-win-crt-*` → `ucrtbase.dll`
  (and `api-ms-win-core-*` → `kernelbase.dll`) when resolving imports.

## Build / run cheat-sheet

```sh
# Host build (native)
meson setup build && meson compile -C build && meson test -C build

# Native Windows (clang-cl; from a vcvars64 environment with LLVM on PATH)
set CC=clang-cl && set CXX=clang-cl
meson setup build-payload payload && meson compile -C build-payload
meson setup build-injector injector && meson compile -C build-injector
meson setup build-overlay overlay && meson compile -C build-overlay

# Payload (cross, Windows target)
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload

# Live calibration (game running) + SDK regeneration
python scripts/calibrate-live.py            # writes calibration/*.json
python scripts/gen-sdk.py calibration payload/sdk
meson compile -C build-payload              # rebuild against the fresh SDK
```

Run flow on Windows: `schtasks /run /tn CS2Launch` (or the AGENTS.md command) →
wait for `client.dll` to load → `build-injector\injector.exe <pid>
build-payload\payload.dll` → read `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`
and `cs2-overlay-layer.log`.

Diagnostic env switches: `CS2_PAYLOAD_NO_DIAG`, `CS2_PAYLOAD_NO_PUBLISH`,
`CS2_PAYLOAD_PUB_THREAD` (dedicated publish thread instead of the game-present
hook), `CS2_PAYLOAD_PUB_NOSTATE`, `CS2_PAYLOAD_PUB_NOWRITE`,
`CS2_PAYLOAD_PUB_NOSHM`, `CS2_PAYLOAD_PUB_INTERVAL_MS`, `CS2_PAYLOAD_DIR`; and on
the layer `CS2_OVERLAY`, `CS2_OVERLAY_SHM`, `CS2_OVERLAY_LOG`,
`CS2_OVERLAY_NO_DRAW`, `CS2_OVERLAY_NO_WAIT`, `CS2_OVERLAY_NO_CHAIN`.

## Open items

- The overlay layer is enabled via a machine-wide implicit-layer registry entry
  (HKLM) scoped by `CS2_OVERLAY=1`; a per-user/portable registration is still
  open.
- Decide whether the Windows default should be the dedicated publish thread or
  the game-present hook (the hook path was not exercised in the validated run).
- A strict swapchain-recreation stress (resolution change) was not captured on
  Windows (the windowed/fullscreen toggle did not recreate the swapchain); the
  layer's `vkCreateSwapchainKHR` handling is unchanged from the Proton-validated
  path.
