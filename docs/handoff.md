# Hand-off — resume from a native Windows box

> **Untested marker:** native Windows builds self-identify as untested (build
> define, boot banner, `C:\cs2-payload-boot.txt`, and the HUD title). See
> [docs/windows-untested.md](windows-untested.md). Remove the marker only after
> the payload has actually run on Windows.

This note records where the overlay work stands and what to try next from a
Windows machine. Read alongside `decisions.md` **ADR-0015/0016/0017**.

## One-line summary

The portable Vulkan present layer (`overlay/`) renders the overlay from shared
memory and is proven in isolation. Every attempt to **inject a payload into the
present/render path** freezes the game under Wine/Proton on a swapchain/display
change, with no frame of ours in the stuck stacks (ADR-0017). **We need to check
whether that freeze is Wine/DXVK-specific by testing the payload on native
Windows.**

## What is proven

- **`overlay/` (Vulkan present layer)** — intercepts `vkCreateSwapchainKHR` /
  `vkQueuePresentKHR`, renders the `wire::Model` (HUD + `DrawList`) with
  ImGui+Vulkan, and draws a real HUD in CS2 (`CS2_OVERLAY=1`), surviving the
  full resize/fullscreen stress **with no payload injected**.
- **State → shm model** — `ModelPublisher` serializes `render::OverlayModel`
  into `C:\cs2-overlay.shm` (`wire::Shared`); the layer writes the presented
  extent back.
- **Payload plumbing** — injector `.pdata` registration (`RtlAddFunctionTable`,
  ~4260 entries), per-thread TLS bootstrap for the manually-mapped image
  (`payload/tls`), `NtCreateThreadEx(SKIP_THREAD_ATTACH)` thread helper,
  crash handler + freeze watchdog (`payload/diag`).
- **RE** — the game's present is `CRenderContextBase::Present`
  (`rendersystemdx11.dll + 0x16dd0`, build 25815307), dispatching to
  `CRenderContextDx11::Present` at `*(this)+0x510`; the swapchain wrapper is
  `CSwapChainDx11`. **Do not inline-hook it** — its prologue has RSP-relative
  home stores the epilogue reads back; use a vtable-slot hook or a
  stack-correct hook (ADR-0017).

## What to do on Windows

1. **Build the payload** (MSVC or `clang-cl`; the project also cross-builds with
   `toolchain/msvc-wine`). `payload/` targets Windows already.
2. **Decide the injection path for Windows** — on native D3D11/DXGI (no DXVK),
   the ADR-0015 `DxgiInterposer` (method-table slots for `Present` /
   `ResizeBuffers`) is the intended path; `PresentHook` + the ImGui DX11 backend
   are still in-tree. Re-enable them for Windows.
3. **Reproduce/confirm** the mode-change (resize / fullscreen toggle) with the
   payload present:
   - If it is **stable on Windows**, the freeze is Wine/DXVK-specific; proceed
     with the internal payload + ImGui DX11 overlay on Windows.
   - If it **still freezes on Windows**, the bug is ours; the suspect is
     swapchain-lifecycle handling (held back-buffer references / D3D state),
     not the hook engine.
4. **Alternatively (payload-free)** — the only path not yet tripped is having
   the Vulkan layer read `client.dll` itself (same process, single render
   thread, no injection, no extra thread). This also runs the overlay on Windows
   if the game is launched `-vulkan`.

## Build / run cheat-sheet

```sh
# Host build (Linux)
meson setup build && meson compile -C build && meson test -C build

# Payload (cross, Windows target)
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload

# Injector
meson setup build-injector injector --cross-file toolchain/windows-msvc.ini
meson compile -C build-injector

# Overlay layer (native Linux .so; the Windows .dll target is TBD)
meson setup build-overlay overlay && meson compile -C build-overlay
```

Diagnostic env switches (added during the bisection): `CS2_PAYLOAD_NO_DIAG`,
`CS2_PAYLOAD_NO_PUBLISH`, `CS2_PAYLOAD_PUB_THREAD`, `CS2_PAYLOAD_PUB_NOSTATE`,
`CS2_PAYLOAD_PUB_NOWRITE`, `CS2_PAYLOAD_PUB_NOSHM`,
`CS2_PAYLOAD_PUB_INTERVAL_MS`; and on the layer `CS2_OVERLAY_NO_DRAW`,
`CS2_OVERLAY_NO_WAIT`, `CS2_OVERLAY_NO_CHAIN`.

## Open items

- Windows `.dll` build + manifest for the `overlay/` layer (Vulkan implicit
  layer via the Windows registry / loader).
- Decide: keep the rendered-in-payload path (Windows) or move state reading into
  the layer (payload-free) for Wine/Proton.
- Replace the `GamePresentHook` inline hook with a vtable-slot hook (or drop it)
  — the inline hook is known-bad on this target (ADR-0017).
