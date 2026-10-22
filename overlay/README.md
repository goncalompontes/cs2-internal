# cs2-overlay — portable Vulkan present layer

A Vulkan **implicit layer** that loads into the target process and intercepts
`vkQueuePresentKHR` to draw the overlay model published by the payload over
shared memory. It sits below DXVK and outside the game's D3D11 present path, so
it cannot reproduce the ADR-0015 DXVK deadlock. See **ADR-0016**.

The same sources build as a Linux `.so` (loaded by the Vulkan loader under
DXVK/Proton) and a native Windows `.dll` (loaded by the Windows Vulkan loader
when the game runs `-vulkan`).

## Build

```sh
meson setup build-overlay overlay
meson compile -C build-overlay
```

- Linux: `build-overlay/libcs2_overlay_layer.so` + `build-overlay/cs2_overlay_layer.json`.
- Windows: `build-overlay/cs2_overlay_layer.dll` + `build-overlay/cs2_overlay_layer.json`.

`external/Vulkan-Headers` is required (vendored headers, git-ignored):

```sh
git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers external/Vulkan-Headers
```

## Enable

Native Windows now uses the Direct3D 11 renderer embedded in `payload.dll`
(ADR-0023). Remove `-vulkan` and Steam wrapper commands, restart CS2, then
use `Run Injector.cmd` once. No Vulkan registration or environment setup is
needed for the native renderer. Its Insert/Delete menu is available before
active-player detection succeeds.

The Vulkan layer remains the Proton renderer. Linux/Proton is opt-in with
`CS2_OVERLAY=1 %command%`. The legacy Windows Vulkan manifest now requires
`CS2_LEGACY_VULKAN_OVERLAY=1`, so old registry entries remain dormant during
native testing. Its diagnostic log can be overridden with `CS2_OVERLAY_LOG`.

### Windows: register the implicit layer

The Windows Vulkan loader discovers implicit layers through the registry. Add
the generated manifest path as a value under
`HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (data `0`). For example
(`register.reg`, run as administrator):

```
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\ImplicitLayers]
"C:\\Users\\gpmpo\\cs2-internal\\build-overlay\\cs2_overlay_layer.json"=dword:00000000
```

For the legacy Vulkan renderer only: registration, -vulkan, and CS2_LEGACY_VULKAN_OVERLAY=1 are required. Use the native payload renderer for the current Windows package.

## Shared-memory model

The payload writes its shared segment under its writable data dir —
`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm` on native Windows, or the prefix's
`drive_c` under Proton — and the layer maps the same segment. Override the path
with `CS2_OVERLAY_SHM`, or the directory with `CS2_PAYLOAD_DIR`.

## Diagnostic switches

`CS2_OVERLAY_NO_DRAW`, `CS2_OVERLAY_NO_WAIT`, `CS2_OVERLAY_NO_CHAIN`.
