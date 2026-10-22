# cs2-internal

A from-scratch internal toolkit for the offline/insecure CS2 client
(appid 730): an external dumper, a manual-map payload DLL, a portable Vulkan
overlay layer, and a programmatic scenario/control harness that drives a real
game for tests.

> **Offline only.** Everything here targets a `-insecure` (VAC-off) local game
> for research and testing. See the launch flags in `scripts/launch-cs2.cmd`.

## Start here

| You are… | Read |
|---|---|
| an **agent** working in this repo | [`AGENTS.md`](AGENTS.md) — conventions, how to launch the game, where to resume |
| setting up a **fresh Windows machine** | [`docs/setup-windows.md`](docs/setup-windows.md) — full install + first live run |
| doing **static RE** | [`docs/re/README.md`](docs/re/README.md) — the REA + Ghidra playbook and the current RE map |
| looking for the **current work** | [`docs/handoff.md`](docs/handoff.md), [`docs/control-harness.md`](docs/control-harness.md), [`docs/roadmap.md`](docs/roadmap.md) |
| changing **C++ style** | [`docs/style-guide.md`](docs/style-guide.md) |

## Layout

- `external/dumper/` — Rust: module/interface/schema/offset dumper, pattern
  scanner, live-memory backends (Linux `/proc` + Windows `ReadProcessMemory`),
  MCP server, C++ header generation.
- `payload/` — C++23 (MSVC ABI): manual-map `loader`, `hooks`, `game` state
  introspection, `control` channel (named pipe) + input hooks, `features`
  (overlay model publisher), `render` (UI-agnostic model), `tls`, `diag`.
- `injector/` — manual-map injector (`injector.exe`), no `LoadLibrary`.
- `overlay/` — portable Vulkan implicit layer (Linux `.so` / Windows `.dll`).
- `scripts/` — SDK generation, live calibration, the scenario runner,
  `preflight-windows.ps1`, `launch-cs2.cmd`.
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
