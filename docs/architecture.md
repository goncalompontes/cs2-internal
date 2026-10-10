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
  backend can be added without touching analysis code.
- `payload/loader` — manual map: PE parse, sections, relocs, imports, TLS,
  DllMain.
- `payload/core` — file logging (`spdlog`), config (`glaze`), module cache,
  scoped memory access, control channel (IPC).
- `payload/scan` — in-process pattern engine (shares patterns with the dumper).
- `payload/sdk` — generated headers (schemas, interfaces, offsets) + accessors.
- `payload/hooks` — hook engine (`SafetyHook`/`MinHook`) + targets: render
  (`IDXGISwapChain::Present`, DX11 under DXVK), tick/frame, game events.
- `payload/features` — read-only state queries, debug HUD data assembly.
- `payload/render` — `UiBackend` abstraction; **RmlUi** primary, **ImGui**
  fallback.
- `injector` — manual-map injector run inside the same Proton prefix.

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
