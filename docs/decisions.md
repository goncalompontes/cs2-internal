# Decisions (ADRs)

Short records of locked design decisions. Append new ones; don't edit history
silently.

## ADR-0001 — Hybrid model, schema-first
**Status:** accepted

External dumper + internal manual-mapped payload. Resolution prefers **internal
schema lookups / `CreateInterface`**; fall back to dumper output, RE'd offsets,
pattern scanning, RIP-relative resolution.

## ADR-0002 — Payload language: C++23 (MSVC ABI)
**Status:** accepted

The target is C++; MSVC ABI needed for vtable/class interop. Built with
`clang-cl`/`lld` against msvc-wine headers. Style follows Rust conventions
(`std::expected`, `std::variant`+`magic_enum`, concepts, newtypes).

## ADR-0003 — Build system: Meson + Ninja
**Status:** accepted

Chosen over CMake. Host and payload are separate Meson projects (payload uses a
cross file).

## ADR-0004 — Ghidra MCP: ReVa
**Status:** superseded by ADR-0012

`cgtudor/reverse-engineering-assistant` (context-rot-aware, assistant +
headless modes) for Ghidra-driven static RE.

## ADR-0005 — Code intelligence: built-in LSP + one tree-sitter MCP
**Status:** accepted

Rely on OpenCode's built-in clangd (`"lsp": true`) instead of a clangd MCP
server (avoids duplicate-clangd index corruption). Add a single lightweight
tree-sitter MCP for token-cheap structural views.

## ADR-0006 — Debuggers
**Status:** accepted

- **Interactive** PE/PDB debugging of the payload: **RAD Debugger**.
- **Agent-automated** dynamic analysis: **gdb + GEF/pwndbg** (native; the
  Wine process is a Linux process).
- Optional later: `x64dbg` + `x64dbg-mcp-server`.

## ADR-0007 — UI: RmlUi behind a swappable `UiBackend`
**Status:** accepted (revisit if integration cost is high)

Slint **cannot** reuse the target's swapchain today (custom backends require
internal/unstable APIs; rendering into a backend texture is an open feature
request #4499). Therefore:
- **In-game overlay:** **RmlUi** (HTML/CSS, DX11 render interface reusing the
  the target's device), hidden behind a `UiBackend` interface. **ImGui** fallback.
- **Desktop control/tools app:** **Slint** (owns its own window).

## ADR-0008 — Tooling languages
**Status:** accepted

Python for glue/analysis, C++ for the payload, Rust only where performance
demands (e.g. the external dumper may be Rust + `memflow`).

## ADR-0010 — Naming & no globals (Rust-style)
**Status:** accepted

Naming follows Rust conventions: `snake_case` for functions/variables/
namespaces, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for constants. No
Hungarian/Windows prefixes (`g_`, `m_`, `p`, `dw`, `b`); `g_pSchemaSystem` is
wrong, `schema_system` is right. Use `enum class` instead of flag ints.

**No global mutable state.** State is passed explicitly or owned by an explicit
context object. Prefer nested namespaces/modules over name prefixes. The
target's own symbol names may appear in docs/comments only, never as our
identifiers.

## ADR-0011 — Symbol renaming pass + target mapping
**Status:** accepted

Everything the dumper emits is run through a **rename pass** (`rename` module)
that turns target symbols into our convention (ADR-0010): strip Source/Hungarian
tags (`C_`/`C`, `m_`/`m_p`/`m_i`/`m_vec`/…, `_t`) and normalize to `snake_case`
identifiers, `PascalCase` types and `SCREAMING_SNAKE_CASE` constants. Field type
displays are rewritten too (`CCSPlayer_BulletServices*` → `CsPlayerBulletServices*`).

The **original target name is always preserved** (`target` on interfaces,
`target_name` on classes/fields) and emitted as a **compile-time mapping**, so
lookups by the game's own name still work: `find_class_by_target`,
`field_offset_by_target`, `find_interface_by_target`. Collisions are resolved
deterministically (`_2`, `_3`, …).

## ADR-0012 — Static RE: REA + global Ghidra
**Status:** accepted

Static RE is driven by **REA** (`rea-agents`, MCP server `rea`,
`github.com/morluto/rea`) against a **global** Ghidra install
(`~/.local/share/ghidra/ghidra_12.1.4_PUBLIC`), registered in the global
OpenCode config with `GHIDRA_INSTALL_DIR` + `JAVA_HOME`.

The project no longer vendors Ghidra: ADR-0004's ReVa toolchain is retired and
the ~1.4 GB download/extraction is removed from the tree. This keeps checkouts
small and decouples RE tooling from the project tree.

REA is built from upstream PR #2358, which adds the existing-project launch
path (`REA_GHIDRA_EXISTING_PROJECT` / `REA_GHIDRA_EXISTING_PROGRAM`): a
pre-imported Ghidra project is reused with `-process -noanalysis` instead of
being re-imported on every open. Windows support for that path was added
upstream (PR #2358, commit `9adad2c`). The global MCP entry uses REA's normal
per-session import — the existing-project settings bind to one target, so they
are passed per run — and `REA_GHIDRA_STARTUP_TIMEOUT_MS` is raised for cold
imports. The former local `rea-agents` project-cache patch and its
`scripts/rea-*` / `docs/rea-project-cache.md` machinery were removed.

## ADR-0013 — Swapchain hooking: kiero + MinHook
**Status:** superseded by ADR-0015

`IDXGISwapChain::Present` is discovered with **kiero** and installed as a
**MinHook inline hook** (`kiero::bind(8, ...)`), following a known-working
reference overlay (Aspasia). Earlier revisions patched the shared `dxgi.dll`
vtable slot directly with the project's own `VtableHook`; that worked but the
overlay could stall the game on window/UI state changes. The inline-hook
approach is the tested one and is kept even though that stall is not yet fully
resolved (see `docs/roadmap.md`, Phase 3).

Vendored under `payload/third_party/kiero` (kiero 1.2.6 + MinHook). Notes for the
cross build: the payload project must declare `c` (MinHook is C), and MinHook's
`__movsb` needs `<intrin.h>` under clang-cl. The ImGui backend uses the Win32
platform backend plus a `WndProc` hook for input, and caches the back-buffer
render target.

## ADR-0014 — State introspection: schema offsets + vtable-matched players
**Status:** accepted

Player state is read in-process by `payload/game`:

- **Field offsets from the schema** (`payload/sdk`, ADR-0001), with literals as
  fallbacks. This depends on a dumper fix: the schema walker now dedups per
  **(module, class)** rather than class name. `client.dll` and `server.dll`
  register the same schema names with different layouts, so the old name-only
  dedup silently dropped the client player-controller hierarchy — including the
  only `m_bIsLocalPlayerController` field and the client-correct
  `m_hPlayerPawn` offset.
- **Safe reads.** Reads go through `ReadProcessMemory(GetCurrentProcess(), …)`.
  The payload is manually mapped without registered `.pdata`, so an invalid
  pointer must fail cleanly instead of faulting.
- **Players by vtable match.** Enemies are enumerated from the paged entity
  identity table by matching the local pawn's vtable, so no per-build vtable RVA
  is hard-coded. The local player is the controller with
  `m_bIsLocalPlayerController` (reached via the `local_player_controllers`
  global), and its `m_hPlayerPawn` yields the local pawn.
- **Presentation is decoupled.** The snapshot is rendered through the
  UI-agnostic `render::HudModel` and also appended to `C:\cs2-payload.log`, so
  the state is verifiable without the overlay.

## ADR-0015 — Swapchain interposition: DXGI method-table slots (retire kiero/Minhook)
**Status:** superseded by ADR-0016

The overlay stalled the game under Proton: `Present` stopped being reached on
window/UI state changes (resize, fullscreen toggle), with the render thread
blocked inside DXVK's `Present` and CS2's main thread waiting in
`CRenderContextBase::Present` / `CSwapChainBase::QueuePresentAndWait`. The stall
reproduced with the draw disabled and with a pure pass-through detour, so it was
never the ImGui draw (commit `e681c22`).

**Cause.** The stall reproduced with **both** interposition methods tried so far
— the shared-vtable slot patch (ADR-0013's earlier revision) and the
kiero/MinHook inline hook — and with the draw disabled. So the hook mechanism is
*not* the sole cause; the reproducible part is the swapchain **lifecycle**:

- A held back-buffer reference blocks `ResizeBuffers` ("swapchain cannot be
  resized unless all outstanding buffer references have been released"). The
  backend cached a back-buffer render target across frames.
- The Win32 platform backend's `WndProc`/window calls from the render thread
  deadlock under Wine (fixed in `8cd3314`, reintroduced in `e681c22`).

Inline-hooking DXVK is *additionally* fragile and unsupported, which is why we
also drop it:

- DXVK reserves a hot-patchable `Present`/`ResizeBuffers`/`CreateSwapChain*`
  entry only when the compiler provides `ms_hook_prologue`
  (`DXVK_HOTPATCHABLE` in `dxgi_swapchain.h`/`dxgi_factory.h`; added by
  [PR #5832](https://github.com/doitsujin/dxvk/pull/5832) for exactly this
  reason). Otherwise the macro is empty and hookability is left to chance.
- It has broken repeatedly across versions: 2.7.1 `Present` begins
  `lea rax, [rip - 7]` ("quite hard to hook instruction pointer relative code",
  [Nemirtingas/ingame_overlay#51](https://github.com/Nemirtingas/ingame_overlay/issues/51)),
  a COM-pointer change (`097c1a5`) broke overlays, and PR #5832 documents
  MSVC-style decoders failing on clang + frame pointers.
- DXVK chains `Present` -> `Present1`
  ([SpecialK#167](https://github.com/SpecialKO/SpecialK/issues/167)) and its
  swapchain methods live in `d3d11.dll`/`dxgi.dll` proxies
  ([dxvk#1996](https://github.com/doitsujin/dxvk/issues/1996)); upstream does not
  support overlay hooking. (Caveat: a Wine/Proton overlay framework — hudhook —
  does inline-hook via MinHook successfully, so this is a robustness argument,
  not proof of impossibility.)

**Decision.** Stop patching the `Present` function body. Interpose at the method
table instead — ReShade's `vtable_hook` model (`payload/features/dxgi_interposer`):

- Create a throwaway factory + D3D11 device + swapchain to read the **class**
  method tables (shared by every instance, so already-created and future
  swapchains are both covered), then `VirtualProtect` + overwrite the slots.
- `IDXGISwapChain::Present` (8) -> frame callback, before the original.
- `IDXGISwapChain::ResizeBuffers` (13) -> resize callback, before the original.
- `IDXGIFactory::CreateSwapChain` (10) and `IDXGIFactory2::CreateSwapChainForHwnd`
  (15) -> forwarded, to observe swaps and assert the method-table assumption.

No target prologue is ever decoded. `kiero` + `MinHook` are retired from the
build (`payload/third_party/kiero` is kept, unbuilt, for reference).

Rendering library is unchanged (ImGui, ADR-0007): it was never the cause. Two
regressions reverted to the `8cd3314` hardening: the Win32 platform backend and
its `WndProc` hook are dropped (window/mouse calls from the render thread
deadlock under Wine; the overlay is display-only), and the game's render target
is saved/restored around the ImGui draw. The backend now releases its cached
back-buffer view in `on_before_resize` (from the `ResizeBuffers` detour) and
rebuilds it on size change, so the game's resize is never blocked by a held
reference.

**Cross-check against upstream (2026-10).**

- *ReShade* (`source/dxgi/dxgi.cpp`) installs `IDXGIFactory::CreateSwapChain` at
  vtable index **10** and `IDXGIFactory2::CreateSwapChainForHwnd` at **15** via
  `vtable_from_instance` (writing the vtable entry), used as its fallback when a
  foreign overlay is present — index and mechanism match.
- *hudhook* (Rust; "runs on Windows and Wine/Proton") discovers the target by
  creating a dummy window + `D3D11CreateDeviceAndSwapChain` and reading
  `swap_chain.vtable().Present`, then hooks **Present and ResizeBuffers**. This
  validates (a) the dummy-device/swapchain discovery and (b) that ResizeBuffers
  must be intercepted. (It uses MinHook inline hooks, which is why the ADR treats
  the method-table rewrite as a robustness choice, not a strict necessity.)
- *DXVK* declares `DxgiSwapChain : DxgiObject<IDXGISwapChain4>` and
  `DxgiFactory : DxgiObject<IDXGIFactory7>`; DXVK's COM objects carry one vtable
  per class, so patching the class table covers every instance, before or after
  install.
- The community "pointer swap" / "data pointer swap" method hooks Present by
  rewriting the vtable pointer without patching any game code.
- `IDXGISwapChain::Present` (8) and `ResizeBuffers` (13) match the compiled
  interface order (IUnknown 0-2, `IDXGISwapChain` after `IDXGIObject`/
  `IDXGIDeviceSubObject`).

## ADR-0016 — Overlay rendering: portable Vulkan present layer + shared-memory model
**Status:** accepted; **superseded by ADR-0023** for the default renderer (in-process DXGI Present + the payload's own menu). Retained as the dialect/Vulkan fallback (`CS2_PAYLOAD_RENDER=layer`, `-vulkan`).

Live test of ADR-0015 (2026-10, Proton-CachyOS, `-insecure`): the method-table
interposer installed (`present hook installed (dxgi method-table interposer)`,
`ui backend initialized`) and presented ~225 frames, then `Present` stopped and
the watchdog reported **63 wedged threads**; the game never recovered. So the
freeze was **never the hook mechanism** — the DXGI inline-hook question
(ADR-0013) and the method-table question (ADR-0015) were both dead ends. The
common factor in every failing configuration is **issuing D3D11 commands from
inside the game's D3D11 `Present`**, while DXVK holds its swapchain locking
around `m_presenter->Present()`.

**Decision.** Move overlay rendering off the game's D3D present entirely, into a
**portable Vulkan implicit layer** that intercepts `vkQueuePresentKHR`, fed by
the injected Windows payload over **shared memory**:

- New top-level `overlay/` project (portable C++): the layer handshake, the
  `vkCreateSwapchainKHR`/`vkQueuePresentKHR` interception, the Vulkan renderer,
  and the shared-memory transport of `render::OverlayModel`.
- The **payload keeps computing state** (its schema/SDK reader is validated) and
  **stops drawing**: it publishes the `OverlayModel` (HUD + `DrawList`) to a
  file-backed shared segment (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm` on
  native Windows; the Proton layer derives the prefix's `drive_c` path from its
  environ — see ADR-0019).
- **Portable, not Linux-specific**: the same layer builds as a Linux `.so`
  (loaded via the Vulkan loader under DXVK/Proton) and a Windows `.dll` (loaded
  via the Windows Vulkan loader when the game runs Vulkan, i.e. CS2 `-vulkan`).
  Both consume the identical loader mechanism and renderer.

**Why this is the right seam.**

- The layer sits **below** DXVK: the game's D3D11 calls and DXVK's presenter stay
  untouched, so the ADR-0015 deadlock cannot occur.
- It is the proven mechanism on this exact host: the live module list of the
  game process contains `steamoverlayvulkanlayer.so` and `winevulkan.so`, and
  MangoHud/vkBasalt use the same approach.
- Swapchain recreation is handled by intercepting `vkCreateSwapchainKHR`/
  `vkDestroySwapchainKHR` rather than owning a DXGI lifecycle.

**Validated (2026-10-13, Proton-CachyOS, RTX 3050).** `overlay/` builds
`libcs2_overlay_layer.so` + a manifest enabled by `CS2_OVERLAY=1`. Loaded into
CS2 it logs the handshake and continuous presents
(`layer negotiated (v2)` → `vkCreateInstance` → `vkCreateDevice` →
`vkQueuePresentKHR #1 … #600`) with the game alive and no freeze.

The Vulkan **renderer** is implemented and validated too: the layer creates its
own render pass/framebuffers for the swapchain, transitions the presented image
`PRESENT_SRC → COLOR_ATTACHMENT_OPTIMAL`, draws ImGui (vendored
`imgui_impl_vulkan`, which embeds SPIR-V), submits on the present queue waiting
on the app's semaphores and signalling its own, and chains that semaphore into
the real present. In CS2 it drew `frame 0 … 5100` over `vkQueuePresentKHR #4800`
with the game alive and no freeze. Key detail: ImGui must resolve Vulkan entry
points through the next layer in the chain
(`IMGUI_IMPL_VULKAN_NO_PROTOTYPES` + `ImGui_ImplVulkan_LoadFunctions`), not the
loader's global exports, which are invalid/re-entrant from inside a layer.

**Consequences / open work.**

- Scope `CS2_OVERLAY=1` to the game (Steam launch option `CS2_OVERLAY=1
  %command%`); setting it on the Steam process loads the layer into every Vulkan
  child (observed).
- Remaining: (1) the Vulkan renderer (ImGui+Vulkan backend or a minimal
  `DrawList` renderer) drawing into the swapchain image in `vkQueuePresentKHR`;
  (2) the shared-memory model + payload publisher; (3) the Windows `.dll`
  target + registry/manifest. (1) and (2) are implemented (the renderer draws
  via `imgui_impl_vulkan`; the payload publishes `wire::Model` over the shared
  segment) and (3) landed with the native Windows bring-up (ADR-0018). The
  ADR-0015 D3D path stays in-tree but is not the default.
- The payload's `payload/diag/` watchdog (which captured the ADR-0015 freeze)
  remains useful.
- Anti-cheat note: this targets `-insecure` local testing only.

## ADR-0017 — Present-path injection deadlocks DXVK under Wine/Proton (validate on native Windows)
**Status:** accepted (investigation paused; resume on native Windows)

The overlay stalls (Present stops; 60+ threads park; the game never recovers)
**only when the injected payload does something in the game's render/present
path**, and always on a swapchain/display-mode change. The same trigger reproduced
across every interposition method we tried, which is the point:

- kiero + MinHook inline hook on `IDXGISwapChain::Present` (ADR-0013).
- DXGI method-table slot interposition for `Present`/`ResizeBuffers` (ADR-0015).
- A dedicated payload publish thread (`CreateThread`), then
  `NtCreateThreadEx` + `THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH` (see the ADR-0016
  thread findings), with and without the shared-memory write, at 16 ms and 1000 ms.
- An inline hook on the game's own `CRenderContextBase::Present`
  (`rendersystemdx11 + 0x16dd0`, found by RE).
- kiero/MinHook inline hook on `DxvkSwapChain::Present` — the same DXVK presenter
  path.

**Evidence this is a Wine/Proton/DXVK issue, not our code.**

- gdb on the wedged process shows every thread parked in Wine syscalls
  (`NtWaitForSingleObject`/`NtWaitForAlertByThreadId`/`linux_wait_objs`/`ioctl`)
  and DXVK's presenter idle — **no frame of ours is ever on any stack**.
- The freeze lands exactly on DXVK swapchain recreation (`vkDestroySwapchainKHR`
  → `vkCreateSwapchainKHR`), which is a known-fragile path under Wine.
- The portable Vulkan present layer (ADR-0016) **alone** (no payload) renders and
  survives the full resize/fullscreen stress; adding the injected payload is what
  flips it.
- On native Windows these exact hooks (MinHook/kiero overlays) are routine.

**Inline-hook caveat captured here.** `CRenderContextBase::Present`'s prologue is
`mov [rsp+0x10],rbx; mov [rsp+0x20],rsi; push rbp; push rdi; …` — the two stores
are home slots the **epilogue reads back**. A naive inline hook (ours, like
MinHook) runs the copied prologue on the trampoline's stack, so those stores land
8+ bytes off and corrupt RBX/RSI. Any in-payload present hook for this target
must be a **vtable-slot hook** (no prologue) or a stack-correct hook — not a
prologue-copying inline hook.

**Decision / resume plan (from a native Windows box).**

1. Keep the portable Vulkan present layer (ADR-0016) as the renderer; it is
   proven in isolation and is platform-neutral.
2. Stop injecting into the DXVK present path under Wine/Proton. If the payload is
   kept, it must not touch the present/render thread on Wine.
3. **Validate on native Windows**: build the same payload + `overlay/` layer, run
   CS2 (Windows), and confirm whether the present-path freeze disappears with
   native D3D11/DXGI (no DXVK). If it does, the freeze is Wine/DXVK-specific and
   the payload path is viable on Windows.
4. If pursuing Wine/Proton, the only path we have *not* been able to trip is
   **payload-free**: have the Vulkan layer read `client.dll` itself (same process,
   single render thread, no injection, no extra thread).

**In-tree state at hand-off.**

- `overlay/` — portable Vulkan present layer + renderer (builds; validated in CS2
  without a payload). `external/Vulkan-Headers` vendored.
- `payload/` — `payload/tls` (per-thread TLS bootstrap + `SkipThreadAttach`
  thread helper), injector `.pdata` (`RtlAddFunctionTable`) registration,
  `payload/diag` (crash handler + freeze watchdog), shm `ModelPublisher`
  (`map()` + synchronous `publish_now()`), ADR-0015 `DxgiInterposer`, and
  `GamePresentHook` (game-present inline hook — known-bad on Wine, per the
  caveat above). `PresentHook`/ImGui DX11 backend retained but unused.
- Diagnostic env switches added for the bisection: `CS2_PAYLOAD_NO_DIAG`,
  `CS2_PAYLOAD_NO_PUBLISH`, `CS2_PAYLOAD_PUB_THREAD`, `CS2_PAYLOAD_PUB_NOSTATE`,
  `CS2_PAYLOAD_PUB_NOWRITE`, `CS2_PAYLOAD_PUB_NOSHM`, `CS2_PAYLOAD_PUB_INTERVAL_MS`,
  and the layer's `CS2_OVERLAY_NO_DRAW`/`CS2_OVERLAY_NO_WAIT`/`CS2_OVERLAY_NO_CHAIN`.
- Anti-cheat note: `-insecure` local testing only.

## ADR-0018 — Native Windows build + live memory backend
**Status:** accepted

The project was developed and exercised only under Wine/Proton (ADR-0017). A
native Windows bring-up was needed so the payload, dumper, injector and overlay
build and run on a real Windows host. What changed:

- **C++23 dialect per compiler.** Meson's built-in `cpp_std` option does not list
  `c++23` for the MSVC-style drivers, so each Meson project selects the flag
  explicitly: GNU-like drivers get `--std=c++23` (GCC and Clang accept the long
  form), clang-cl/cl.exe get `-std:c++latest`. Both spellings avoid Meson's
  "use the built-in cpp_std option" warning.
- **Cross-platform SDK generation.** `scripts/gen-sdk.py` replaces the shell
  generator (which needed a POSIX `bash` and would not run on native Windows);
  `gen-sdk.sh` is now a thin wrapper. The Rust dumper's `/proc`-based code is
  `cfg(unix)`-gated so the crate builds on Windows (`--file`, `--pattern`,
  `--interfaces`, `--resolve-offsets`, `--gen` all work there).
- **`WindowsMemory`.** A second `ProcessMemory` backend for native Windows
  (`ReadProcessMemory` + `VirtualQueryEx` + toolhelp snapshots), selected by
  `cfg(windows)`; `LinuxProcMemory` stays for Wine/Proton. The dumper CLI's live
  mode is now cross-platform, so live calibration works on Windows.
- **Inline-hook engine fix.** The trampoline used `mov rax, imm64; jmp rax`,
  which clobbers RAX (corrupting the copied prologue's live registers and the
  MSVC x64 variadic AL). Replaced with a register-free `jmp qword ptr [rip+0]`
  (14-byte) jump. Exposed on Windows because clang-cl ignores the test's GCC-only
  `optimize("O0")` (now `optnone`).
- **Injector loader entry.** The synthetic PEB `LDR_DATA_TABLE_ENTRY` faults
  `LdrpInitializeThread` on native Windows when a thread attaches. The injector
  now skips it there (auto-detected via `wine_get_version`, overridable with
  `--register`/`--no-register`) and keeps it for Wine/Proton.
- **Overlay Windows `.dll`.** The layer builds as `cs2_overlay_layer.dll`
  (exporting `vkNegotiateLoaderLayerInterfaceVersion` +
  `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` via `__declspec(dllexport)`),
  maps the payload's shared segment (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm`;
  see ADR-0019) with the Win32 file-mapping API, and ships a manifest for
  registry-based implicit-layer registration.

**Verified:** host `meson test` (7/7), dumper `cargo test`, and the injector's
offline validation (`victim.exe` + `trivial.dll` writes its marker) all pass on
native Windows (clang-cl). The payload DLL and the overlay DLL link.

**Validated in-game (2026-10, build 25815307):** the payload was injected into
CS2 on Windows and read live state; the Vulkan layer rendered the HUD + ESP; a
windowed/fullscreen toggle did not freeze the game. The "native Windows —
untested" marker was removed; see `docs/windows-validation.md`.

## ADR-0019 — Native Windows runtime: API-set imports + user-writable output paths
**Status:** accepted

Two issues surfaced when the payload first ran inside CS2 on native Windows
(neither reproduces under Wine/Proton):

- **API-set imports.** `payload.dll` links the UCRT through the virtual
  `api-ms-win-crt-*.dll` API-set stubs, which the loader resolves to a host DLL
  but which never appear in the target's module list. The injector's import
  resolver found no such module and aborted injection. **Decision:** the
  injector rewrites API-set names to their host DLL before lookup
  (`api-ms-win-crt-*` → `ucrtbase.dll`, `api-ms-win-core-*` → `kernelbase.dll`)
  and resolves the symbols from the real export table.
- **Output paths.** The payload wrote its marker, log, and shared segment to
  `C:\` root. CS2 runs at **Medium integrity**, and a standard-user process
  cannot create files in `C:\` root, so every write failed silently (no marker,
  no log, no segment). **Decision:** the payload resolves its data directory to
  `%LOCALAPPDATA%\cs2-internal` (`CS2_PAYLOAD_DIR` overrides it) and the overlay
  layer maps the same path on Windows; under Proton the layer searches the
  prefix's `drive_c` (per-user `AppData\Local\cs2-internal`, a shared
  `cs2-internal`, then the legacy `cs2-overlay.shm`).

**Verified:** after both fixes, injection completes, the marker/log/segment are
written, and the overlay renders (see `docs/windows-validation.md`).

## ADR-0020 — Programmatic scenario/control harness

**Status:** accepted; control core implemented and validated live (2026-10).

To test the payload/overlay against real game situations, the project will
expose a **control channel** and a **scenario runner** instead of driving the
game by hand.

- **Transport:** a Win32 **named pipe** (default `\\.\pipe\cs2-internal-control`,
  env `CS2_CONTROL_PIPE`) carrying newline-delimited JSON. Chosen over TCP and
  shared memory: pure Win32, reachable cross-session by the same user, openable
  from the Python stdlib, no ports.
- **`exec` (console commands):** run **in-process in the payload** via the
  `Cbuf_AddText` wrapper — `FUN_1801b8790(&g_InputService, 5, line, 0)`
  (RVA `0x1b8790` / `0x8dbf20`, resolved by the dumper's `input_service` and
  `cbuf_add_text` signatures). The engine's built-in `-concommandpipe`
  (`-insecure`-gated, input-only in this build) is retained as a no-injection
  fallback. See `docs/re/engine2.md`.
- **`look` (aim) / `press` (keys):** engine-native, through the same `exec`
  primitive — `look` uses the `setang` command (direct view-angle control, needs
  `sv_cheats 1`) and `press` the button commands (`+forward`…`-forward`,
  `+attack`, `+jump`, …). Not OS `SendInput`: the runner may be in session 0
  while the game is in session 1, so synthetic input from the runner would never
  reach the game. No offsets or hooks. Validated live.
- **Game events:** the payload patches the client `CGameEventManager`'s dispatch
  vtable slots (`FireEvent` `+0x38`, `FireEventClientSide` `+0x40`; both call
  `listener->vtable[+0x8]` and read `IGameEvent::GetName` at `+0x8`) and records
  the fired names **and arguments** (read with the `IGameEvent` getters —
  `GetInt`/`GetBool`/`GetFloat`/`GetString`/`GetPlayerIndex` — keyed by the
  engine's own field-name hash). Scenarios read them with `events` / await them
  with `wait_for(event=…, event_args={…})`, so they can assert on events rather
  than only polling state. Names and fields are validated against the build's own
  list (`scripts/extract-gameevents.py` → `event_names.hpp`), so a typo is an
  error. See `docs/re/client-dll.md` §"Game events".
- **Runner:** a Python package under `scripts/` (`scenario/` + `run-scenario.py`)
  that launches via the `CS2Launch` task, injects, connects, and asserts.
- **First milestone:** control core — `ping`/`state`/`exec`/`wait_for`/`quit`,
  the console entry point, the Python client/launcher, and a join-bot-match
  scenario. **Implemented** in `payload/control/` + `scripts/scenario/`
  (host-tested via the `control-core` Meson test). `input`/`look`
  (engine `setang` + button commands) and game events
  (`IGameEventListener2` via the client's `CGameEventManager`) are implemented;
  console-output capture follows.

The control channel is a distinct seam from the render path (ADR-0016): it runs
on its own thread and never touches the present path. See
`docs/control-harness.md` for the full design, command surface, and next actions.

## ADR-0021 — Overlay UI: stock ImGui font (no AA), outlined ESP labels, in-game menu

**Status:** accepted; implemented.

The overlay's ESP labels used an embedded pixel font (Silkscreen, shipped as a
generated byte array) and the UI was display-only. Now:

- **One font.** The stock Dear ImGui default (`AddFontDefault`) is used for both
  the menu and the ESP labels; the vendored pixel font, its `gen-font.py`
  generator and the generated header are removed. The atlas coverage is
  thresholded to 0/255 alpha after `Build()` and glyph positions are floored, so
  text renders **without antialiasing**.
- **ESP outlining.** `render::Text` and `wire::Draw` gained an `outline` flag;
  the renderer draws the glyphs 8× at ±1 px in black, then the coloured pass.
  Every ESP element **except the player name** is outlined (the box and ammo bar
  get a 1 px black border too); the name keeps a plain drop shadow.
- **In-game menu.** `Insert` or `Delete` toggles a tabbed Dear ImGui window (ESP / Interface
  / About) with checkboxes for the ESP elements, the debug HUD and the
  enemy/dead filters. Input is captured on Windows by subclassing the game
  window's `WndProc` — the Win32 *platform backend* stays unused because its
  render-thread window/mouse calls are the ADR-0015 deadlock class. Mouse
  messages feed Dear ImGui's thread-safe input queue, `WM_INPUT` is drained and
  swallowed so the game's mouse-look does not move the camera behind the menu,
  and keyboard input is held by the menu while it is open. Off Windows there is
  no input path and the menu stays closed.
- **Settings round-trip.** The shared segment (ADR-0016) gained a seqlock-guarded
  `Settings` block and its version was bumped to 3: the payload publishes the ESP
  defaults with the segment, the menu edits them, and the payload applies them to
  `features::EspConfig`. `menu_open` is published as well so the payload can
  suppress input through the usercmd path (ADR-0020 follow-up) while the menu is
  open.
- **Movement features (2026-10-15, version 4).** The same block gained
  `auto_strafe` and `bhop` plus a `Movement` tab, so the payload's movement features
  (`docs/re/subtick.md` §"The movement yaw channel") are owned by the menu like the
  ESP toggles: the payload publishes their initial state as defaults (seeded from
  `CS2_PAYLOAD_MOVEMENT`), and a **settings change** is forwarded to
  `control::Service::set_movement_features` through a sink so the payload's `Input`
  flags follow. Because only *changes* are applied, an explicit `strafe`/`bhop`
  control command survives until the menu is touched again. Struct size is unchanged
  (12 one-byte toggles + 4 reserved), so the model layout is untouched.

## ADR-0022 — Ragebot aim: deterministic-spread damage field, not multipoint

**Status:** accepted (design); implementation pending. See `docs/ragebot.md`,
`docs/re/aim.md`, `docs/re/vphysics2.md`.

The ragebot chooses an aim by maximising a **damage field** `D(u)` over world
directions — not by multipoint + spread-cone sampling.

**Why.** The spread cone is **deterministic**: `FUN_180d23460` seeds the RNG from
`CBaseUserCmdPB::random_seed` (`MD5_PseudoRandom(sequence)`) and draws the pellet
offset from it (`docs/re/aim.md`). There is no random cone to sample; "hitchance
over 256 samples" integrates a delta. The only real random variable is the
target's lag-compensated pose.

**Direction.**
- Build `D` once (it is aim-independent); an aim is the pellet pattern looked up
  over `D`. Multipoint is the special case `K=1`, binary visibility, no
  penetration.
- The optima are exact and finite: a cell's nearest point (damage), a cell's
  Chebyshev centre (hitchance), the pose-blurred max (robust). Pose uncertainty
  enters as an analytic blur, not extra traces.
- **Tier 1 (proposed; oracle not yet validated):** few-trace — project hitboxes
  analytically, probe with `TraceShapeWithSortedResults` / `TraceLineIntervals`, then
  reconstruct occluder edges from planar faces. Current-build RE confirms the actual
  `FX_FireBullets` path uses `TraceLineIntervals` plus repeated penetration advance;
  neither API is yet proven to return the full material/mesh layer stack assumed here.
- **Tier 2 (target):** zero-trace — read collision hulls/meshes directly from
  `vphysics2` (shape type `+0x18`, hull pointer `+0xC0`, `RnHull_t` vertices
  `+0x70/+0x78`; `docs/re/vphysics2.md`), project, assemble the heat map
  analytically. Confirmed feasible and cheaper than tracing.
- **Rejected:** a per-pixel angular world raster (more expensive than targeted
  work); spread-cone sampling (nothing random); learned models (the inputs are
  computable).

Validation uses the control harness (silent sub-tick aim + fire + observed
health drop).

## ADR-0023 — Default renderer: in-process DXGI Present + the payload's own menu
**Status:** accepted (2026-10-19); supersedes ADR-0016 for the default. Validated
on native Windows.

**Context.** ADR-0016 moved rendering into a Vulkan layer to dodge the DXVK
`Present` deadlock (ADR-0017), which was specific to Wine/Proton + DXVK. On
**native Windows** the game runs D3D11/DXGI, and the historic freeze did not
reproduce; the DXGI method-table interposer (`PresentHook` + `DxgiInterposer`,
ADR-0015) and the ImGui D3D11 backend already existed but were not the default.

**Decision.** Render the ESP + HUD in-process from the game's own
`IDXGISwapChain::Present`, and move the menu into the payload:

- `PresentHook` (DXGI method-table interposer) is the default when the game is on
  D3D11/DXGI. `CS2_PAYLOAD_RENDER` (`present` | `layer` | `layer-thread`) overrides;
  otherwise a loaded `vulkan-1.dll` selects the layer. The game inherits its
  environment from **Steam**, so the decision is made from the loaded graphics
  modules, not the env.
- The menu lives in the payload (`features::Menu`): it owns the settings block
  (ADR-0021), subclasses the game window's WndProc, releases the cursor while
  open, and draws with the payload's ImGui context via a new `UiBackend` UI
  callback (`set_ui_callback`) invoked inside the ImGui frame.
- The `overlay/` Vulkan layer + shared-memory publisher remain the fallback for a
  Vulkan/DXVK stack (keep ADR-0016 for that case; the layer is unchanged).

**Gotcha (decisive).** The payload's threads are created with
`THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH` (ADR-0017) so they never run
`LdrpInitializeThread`. DXGI/D3D11 fault when their entry points are called from
such a thread (the probe device creation crashed inside `dxgi` reading `+0xa8`).
The in-process install therefore runs on a **normal** thread (safe on native
Windows D3D11); the layer paths keep the TLS-skip thread.

**Validated (2026-10-19, native Windows, de_dust2 bot match).** Present
interposed, ImGui D3D11 backend initialized, ESP (boxes/health/name/weapon/flags)
and the debug HUD drawn in-process, the menu rendered as its own ImGui window
(tabs + widgets), the game stable (`artifacts/game.png`).

**Menu input.** The menu subclasses the game window's WndProc, feeds Dear ImGui,
and swallows mouse/keyboard messages. That alone does **not** stop the game's
mouse-look: CS2's input goes through **SDL3**, which reads raw input with
`user32!GetRawInputBuffer` in its own loop (it does not import `GetRawInputData`),
so `WM_INPUT` never reaches the subclassed WndProc. While the menu is open the
payload therefore **patches SDL3's IAT entry for `GetRawInputBuffer`** (the export
is a `jmp`-thunk-like prologue too long for the inline hook) to return "no raw
input" — blocking mouse motion, buttons and keyboard to the game until the menu
closes. `GetCursorPos`-based look (`m_rawinput 0`) is not covered.

**Open.** Windowed/fullscreen + resize stress; the cursor release needs operator
confirmation; the raw-input block needs live confirmation; the ragebot follow-ups
in `docs/next-session-present-hook.md` remain.


---

## ADR-0024 — one render path, JSON-RPC control, one state machine (2026-10-22)

**Status: accepted (cleanup branch).** Supersedes the renderer half of ADR-0015
and the control-transport half of ADR-0020; ADR-0016 (the Vulkan layer) is
retired.

- **One renderer.** The external Vulkan implicit layer, its shared-memory model
  and its settings segment are removed. The payload draws in-process from the
  game's own DXGI/D3D11 `Present`; the menu owns the settings block.
- **JSON-RPC 2.0 control.** The named pipe is gone. The payload listens on an
  AF_UNIX socket (`%LOCALAPPDATA%\cs2-internal\control.sock`, loopback TCP as
  the fallback), frames with `Content-Length`, and requires the per-session
  token from `control.json` in `initialize`. Method names and results are the
  old command surface; a bad token closes that connection only.
- **One state machine.** `payload/game/include/cs2/game/game_state.hpp` reduces
  every snapshot to a `GameScreen` and a capability mask. Every feature — the
  ESP, chams, camera, movement, ragebot, trigger, anti-aim, animfix — asks it
  before acting. The `state` answer publishes `screen` and `capabilities`.
- **No diagnostics in the feature code.** `RagePerf`, the trace probe, the
  heatmap command, the HUD status sections and the per-frame state dump are
  removed; the crash guard and the event ring stay. The angular-chart math the
  solver uses is not diagnostics and stays.
- **The protection pass is a separate repository**, checked out at
  `tools/protect` as a git submodule
  (<https://github.com/goncalompontes/cs2-protect>). This tree loads the built
  DLL (`-Dprotect=true`; `protect_tools` and `protect_plugin` default to the
  submodule).
- **The MCP is the tool surface.** Build, test, offsets, preflight and
  calibration are `tools.cs2.*`; the one-time scripts and task specs are in
  `docs/archive/`.