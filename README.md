# cs2-internal

A from-scratch internal toolkit for the offline/insecure CS2 client
(appid 730): an external dumper, a manual-map payload DLL, and a JSON-RPC
control/MCP harness that drives a real game for tests.

> **Offline only.** Everything here targets a `-insecure` (VAC-off) local game
> for research and testing. See the launch flags in `scripts/launch-cs2.cmd`.

## Start here

| You are… | Read |
|---|---|
| an **agent** working in this repo | [`AGENTS.md`](AGENTS.md) — conventions, how to launch the game, where to resume |
| setting up a **fresh Windows machine** | [`docs/setup-windows.md`](docs/setup-windows.md) — full install + first live run |
| doing **static RE** | [`docs/re/README.md`](docs/re/README.md) — the REA + Ghidra playbook and the current RE map |
| looking for the **current work** | [`AGENTS.md`](AGENTS.md), [`docs/operator-live-check.md`](docs/operator-live-check.md) |
| changing **C++ style** | [`docs/style-guide.md`](docs/style-guide.md) |
| the **control protocol** | [`docs/control-harness.md`](docs/control-harness.md) — JSON-RPC over an AF_UNIX socket |

## Layout

- `external/dumper/` — Rust: module/interface/schema/offset dumper, pattern
  scanner, live-memory backends (Linux `/proc` + Windows `ReadProcessMemory`),
  MCP server, C++ header generation.
- `payload/` — C++23 (MSVC ABI): manual-map `loader`, `hooks`, `game` state
  introspection and the **game state machine**, `control` (JSON-RPC over
  AF_UNIX) + input hooks, `features` (ESP/menu/chams), `render` (UI-agnostic
  model), `tls`, `diag`, `core` (the shared RAII handle).
- `injector/` — manual-map injector (`injector.exe`), no `LoadLibrary`.
- `scripts/` — SDK generation, live calibration, `preflight-windows.ps1`,
  `launch-cs2.cmd`; the runner/build/offsets entry points are MCP tools.
- `docs/` — design (`decisions.md`, `architecture.md`), RE notes (`docs/re/`),
  hand-off state, this project's ADRs.
- `calibration/`, `payload/sdk/` — **generated**, build-specific, git-ignored.

## Quick build (Windows)

```powershell
# host tools + tests
meson setup build && meson compile -C build && meson test -C build

# Windows targets (from a vcvars64 shell with CC=CXX=clang-cl)
meson setup build-payload payload   && meson compile -C build-payload
meson setup build-injector injector && meson compile -C build-injector
meson setup build-overlay overlay   && meson compile -C build-overlay
```

Full prerequisites, the interactive-session launch task, the Vulkan layer
registration and the first live calibration are in
[`docs/setup-windows.md`](docs/setup-windows.md).
