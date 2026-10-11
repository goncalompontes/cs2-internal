# Build & test

- **Meson + Ninja.** Host tools build natively.
- The payload is a **separate Meson project** cross-compiled with
  `clang-cl`/`lld` against msvc-wine headers/libs (`toolchain/`).
- Meson emits `compile_commands.json` → clangd (OpenCode built-in LSP).

## Host tools

```sh
meson setup build
meson compile -C build
meson test -C build
```

## Payload (cross, Windows target)

```sh
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload
```

## Component layout

- `external/dumper` — module enumeration + fingerprinting, pattern scanner
  (IDA-style sigs, wildcards, RIP-relative resolution), schema walker
  (`schemasystem.dll` / `SchemaSystem_001`), `CreateInterface` resolver.
  Emits `offsets.json`, `interfaces.hpp`, `schemas/*.hpp`, `manifest.json`.
  Memory access sits behind `ProcessMemory` / `MemoryRead` so a `memflow`
  backend can be added without touching analysis code. Header generation
  (`sdk`) is JSON-driven: `--gen [dir]` consumes `--interfaces-json` /
  `--schema-json` / `--offsets-json` and writes into `payload/sdk` by default.
- `payload/loader` — manual map: PE parse, sections, relocs, imports, TLS,
  DllMain.
- `payload/core` — file logging (`spdlog`), config (`glaze`), module cache,
  scoped memory access, control channel (IPC).
- `payload/scan` — in-process pattern engine (shares patterns with the dumper).
- `payload/sdk` — generated headers (schemas, interfaces, offsets) consumed by
  the payload and by the host `sdk-consumer` test. Rendered by
  `scripts/gen-sdk.sh` from the host build (and from the payload build itself,
  so standalone payload builds work).
- `payload/hooks` — hook engine: vtable + inline (prologue) hooks with
  executable-memory/protection helpers (host-tested).
- `payload/features` — `PresentHook` (`IDXGISwapChain::Present`, vtable index 8,
  swapchain vtable discovered via a throwaway D3D11 device) that derives the
  device/context/HWND from the swapchain and drives the UI backend.
- `payload/render` — `UiBackend` abstraction (ADR-0007) with an **ImGui** backend
  (vendored `payload/third_party/imgui` + dx11/win32 backends) and a frame-log
  stand-in; **RmlUi** pending.
- `injector` — manual-map injector (`injector.exe`, Windows x64) run inside the
  same Proton prefix: parse the payload, map sections + apply relocations into a
  local buffer, resolve imports by reading the target's module export tables,
  `WriteProcessMemory` the image, then call the entry point on a remote thread
  via a small x64 stub. Bypasses the target's `LoadLibrary`/signature checks.
  Includes a `victim.exe` + CRT-less `trivial.dll` for offline validation.
- `scripts/` — `gen-sdk.sh` (build-time header render), `calibrate-live.sh`
  (live dumps → `calibration/*.json`), `mcp-live.sh` (MCP wrapper).
- `external/dumper --mcp` — live-analysis **MCP server** (stdio), wired into
  OpenCode as `cs2-live`: attach, module enumeration, typed reads, pointer
  chains, pattern scans, and value scans with narrowing (a native,
  scriptable alternative to Cheat Engine for a Wine/Proton target).

## Live calibration & SDK generation

Two loops feed `payload/sdk/*.hpp`:

1. **Live calibration** (`scripts/calibrate-live.sh`) attaches to a running
   target and writes `calibration/interfaces.json` (and `schema.json` when an
   address is supplied). Interactive offset work is done through the
   `cs2-live` MCP tools.
2. **Build-time generation** — the `sdk-headers` Meson target runs
   `scripts/gen-sdk.sh`, rendering headers from whatever dumps are present.
   With `-Dlive_calibration=true` it refreshes the dumps first, so a release
   build can calibrate against a running target as part of the build.

`scripts/release.sh` ties this together: it builds the dumper, runs live
calibration, then configures the host build with `-Dlive_calibration=true` for
a release build (and cross-builds the payload when msvc-wine is set up). Note
that a `cargo build --release` replaces the dumper binary and **drops its file
capability**, so re-apply `setcap cap_sys_ptrace+ep` after each dumper build.

Reading another process's memory is subject to the ptrace policy; see the
ptrace section below.

## Testing & instrumentation (agent-drivable)

1. **Fixtures** — pattern scanner, PE loader, schema/offset resolution tested
   against committed fixtures; **no game required** (fast loop).
2. **Headless runs** — dedicated server (native, no GPU) for schema/interface
   dumps; client under Xvfb/gamescope for client-side tests.
3. **Control channel** — payload exposes a local socket with commands
   (`get_state`, `reload_offsets`, `run_scenario`, `dump_entity`, `quit`)
   returning JSON. Enables full scripted/interactive sessions.
4. **One-command e2e** — `scripts/e2e.sh`: virtual display → launch client →
   inject → run scenario → assert → exit code.
5. **Ghidra headless** (ReVa, fake backend) for automated static checks.

## Reading another process's memory (ptrace policy)

`/proc/<pid>/mem` reads are governed by `kernel.yama.ptrace_scope`. This
machine has `ptrace_scope=1`, so the dumper — which is *not* a descendant of
the target — will be denied. Options:

```sh
# run the dumper elevated
sudo external/dumper/target/release/cs2-dumper --process cs2.exe
# or relax the policy for this session
sudo sysctl kernel.yama.ptrace_scope=0
# or grant just this binary the capability
sudo setcap cap_sys_ptrace+ep external/dumper/target/release/cs2-dumper
```
