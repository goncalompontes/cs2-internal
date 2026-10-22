# Build & test

> **Cleanup note (2026-10-22).** This page predates the cleanup: the Vulkan
> layer, the named-pipe control channel and the per-feature diagnostics are
> gone. The current shape is in [`../AGENTS.md`](../AGENTS.md) and
> [`decisions.md`](decisions.md) ADR-0024. Build commands are in
> [`setup-windows.md`](setup-windows.md); the MCP tools wrap them.

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
meson test -C build-payload
```

## The tests, and which build runs them

Two builds, two copies of the same test, and **both are run**:

```sh
meson test -C build          # the host copy
meson test -C build-payload  # the payload's own clang-cl build
```

The payload's copy is the one that ships with the payload, and it is the one
built with the compiler the payload ships with (**clang-cl**). The host copy
is the same source in the host project; its **behavioural** half is only
meaningful in a **clang-cl** host build — with `cl.exe` the guard recovers
either way, so the half cannot see the defect (and this repository's host
suite *is* clang-cl: CI sets `CC=clang-cl`/`CXX=clang-cl`).

The payload's copy is registered whenever the build machine can run the
binaries (`meson.can_run_host_binaries()`), so the CI job — which configures
the payload as a cross build with `toolchain/windows-msvc.ini` **on a Windows
runner** — registers and runs it too. A cross build to a foreign system does
not.

`payload/diag/tests/test_diag_guard.cpp` is the fault guard's own test: ten
deliberate faults, ten recoveries, the process alive; plus a source check
that the tree holds exactly **one** recovery mechanism.

## Component layout

- `external/dumper` — module enumeration + fingerprinting, pattern scanner
  (IDA-style sigs, wildcards, RIP-relative resolution), schema walker
  (`schemasystem.dll` / `SchemaSystem_001`), `CreateInterface` resolver.
  Emits `offsets.json`, `interfaces.hpp`, `schemas/*.hpp`, `manifest.json`.
  The schema walker dedups per **(module, class)**, not per class name: client
  and server register the same schema names with different layouts (e.g.
  `CCSPlayerController`), and both must survive.
  Memory access sits behind `ProcessMemory` / `MemoryRead` so a `memflow`
  backend can be added without touching analysis code. Two live backends exist:
  `LinuxProcMemory` (`/proc/<pid>/{mem,maps}`, for Wine/Proton) and
  `WindowsMemory` (`ReadProcessMemory` + `VirtualQueryEx` + toolhelp snapshots,
  for native Windows). Header generation
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
- `payload/game` — state introspection: safe in-process reads
  (`ReadProcessMemory` on self, so an invalid pointer fails instead of faulting
  — and a read never faults at all, so it needs no guard), module lookup, the
  paged entity identity table (`EntitySystem`), and `StateReader` (local player
  + every player). Field offsets come from the generated schema (ADR-0001);
  players are enumerated by matching the local pawn's vtable rather than a
  hard-coded per-build RVA.
- `payload/features` — `Overlay` builds a `render::OverlayModel` from a
  `game::Snapshot` and serializes it to shared memory (`ModelPublisher`), and
  appends a compact dump to the payload log. Output paths are under the payload's
  writable data dir (`%LOCALAPPDATA%\cs2-internal` on Windows; ADR-0019) — not
  `C:\` root, which a Medium-integrity game process cannot write. `DxgiInterposer`
  (ADR-0015) and `GamePresentHook` (game-present inline hook) are retained but
  **not the default** — any payload work in the present/render path stalls DXVK
  under Wine/Proton (**ADR-0017**). The Vulkan layer does the rendering.
- `payload/render` — `UiBackend` abstraction (ADR-0007) with an **ImGui** DX11
  backend (retained but unused; Win32 platform backend/`WndProc` not used) and a
  frame-log stand-in. The UI-agnostic `render::HudModel`/`DrawList` (the
  `wire::Model` wire format) keep the backend free of game types.
- `overlay/` — **portable Vulkan present layer** (ADR-0016): intercepts
  `vkCreateSwapchainKHR`/`vkQueuePresentKHR` (a Linux `.so` under DXVK, and a
  native Windows `.dll` via the Vulkan loader), reads the shared-memory model,
  writes the presented extent back, and renders with ImGui+Vulkan
  (`imgui_impl_vulkan`). Validated in CS2 without a payload. Enabled with
  `CS2_OVERLAY=1`; see `overlay/README.md` for the Windows registry
  registration.
- `payload/tls` — per-thread TLS bootstrap for a manually-mapped image plus an
  `NtCreateThreadEx(SKIP_THREAD_ATTACH)` thread helper (the loader never
  initializes TLS for mapped modules; ADR-0016).
- `payload/diag` — the crash handler (a first-chance vectored handler and an
  unhandled filter) and a freeze watchdog (per-thread stacks) that captured the
  ADR-0015 freeze. The crash handler also owns the payload's **only** recovery
  mechanism: `cs2::diag::call_guarded` runs an engine call under a vectored
  handler plus `setjmp`/`longjmp`, so a fault inside the call is logged and the
  process survives instead of dying. The handler that owns the jump is **not**
  `noexcept`: a `noexcept` frame is not `nounwind` — clang-cl gives it a C++
  exception-handler unwind record whose cleanup action is `std::terminate`,
  and the `longjmp` out of it runs that action, so the process dies with
  0xC0000409 instead of recovering (clang-cl, the compiler the payload ships
  with; the plain handler's record has no handler at all, flags `0x0`).
  `payload/diag/tests/test_diag_guard.cpp` is built with clang-cl and proves
  it — ten deliberate faults, ten recoveries, the process alive. The guard's
  limits (one guarded call at a time, the faulting thread only, the
  recoverable codes, no resources in the guarded call) are in
  `payload/diag/include/cs2/diag/crash_handler.hpp`.
- `injector` — manual-map injector (`injector.exe`, Windows x64) run inside the
  same Proton prefix: parse the payload, map sections + apply relocations into a
  local buffer, resolve imports by reading the target's module export tables,
  register the image in the PEB loader list, register `.pdata`
  (`RtlAddFunctionTable` — load-bearing: the guard's `longjmp` unwinds the
  frames it leaves, so the payload's unwind tables have to be registered),
  `WriteProcessMemory` the image, then call the entry
  point on a remote thread via a small x64 stub. The import resolver maps API-set
  stubs (`api-ms-win-crt-*` → `ucrtbase.dll`, `api-ms-win-core-*` →
  `kernelbase.dll`; ADR-0019). Bypasses the target's
  `LoadLibrary`/signature checks. Includes `victim.exe` + CRT-less `trivial.dll`
  for offline validation.
- `scripts/` — `gen-sdk.sh` (build-time header render), `calibrate-live.sh`
  (live dumps → `calibration/*.json`), `mcp-live.sh` (MCP wrapper).
- `external/dumper --mcp` — live-analysis **MCP server** (stdio), wired into
  OpenCode as `cs2-live`: attach, module enumeration, typed reads, pointer
  chains, pattern scans, and value scans with narrowing (a native,
  scriptable alternative to Cheat Engine for a Wine/Proton target).

## Live calibration & SDK generation

Two loops feed `payload/sdk/*.hpp`:

1. **Live calibration** (`scripts/calibrate-live.py`, cross-platform; the
   `.sh` is a wrapper) attaches to a running target and writes
   `calibration/interfaces.json`, `schema.json`, `schema_system.json`,
   `offsets.json` and `_manifest.json`. `offsets.json` comes from
   `cs2-dumper --resolve-offsets` (the curated signatures in
   `external/dumper/src/analysis.rs`); a signature that stops matching is
   reported rather than silently skipped.
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
5. **Static RE** (REA + Ghidra) for automated static checks.

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
