# cs2-dumper

External offset/interface/schema dumper.

Runs as a normal Linux process and reads the Proton/Wine target process (a
Linux process at the OS level) via `/proc/<pid>/mem` + `/proc/<pid>/maps`.

## Build & test

```sh
cargo test --manifest-path external/dumper/Cargo.toml
cargo build --release --manifest-path external/dumper/Cargo.toml
```

## Layout

| Module | Responsibility |
|---|---|
| `pattern` | IDA-style pattern parse/scan + RIP-relative resolution (pure, tested) |
| `memory` | `ProcessMemory` trait + `LinuxProcMemory` (Linux `/proc`) + PID discovery |
| `reader` | `MemoryRead` trait + helpers (`read_ptr`/`read_cstring`/…) + `BytesReader` for fixtures |
| `pe` | PE image helpers (image base, size, exports) via `goblin` |
| `interfaces` | `CreateInterface` registration-list resolver (seed-based) |
| `schema` | schema walker (`SchemaLayout`, configurable offsets) |
| `sdk` | C++ header generation (`interfaces.hpp`, `offsets.hpp`, `schemas/*.hpp`) |
| `analysis` | signature registry + resolved offsets/interfaces (grows over time) |
| `output` | JSON serialization |

Memory access is behind `ProcessMemory` / `MemoryRead`, so a `memflow` backend
can be added later without touching analysis code.

## Usage

```sh
# list modules of a running offline process
cs2-dumper --process cs2.exe

# JSON manifest
cs2-dumper --json

# scan a signature in a module
cs2-dumper --pattern "48 8B 05 ?? ?? ?? ??" --module client.dll

# list CreateInterface-related exports
cs2-dumper --exports --module client.dll

# enumerate interfaces (seed-based discovery)
cs2-dumper --interfaces --module client.dll
cs2-dumper --interfaces --interface-seed MyNewInterface001

# dump the schema system (needs the CSchemaSystem address)
cs2-dumper --schema --schema-system 0x7ff6XXXXXXXX

# generate C++ SDK headers from JSON dumps (default out dir: payload/sdk)
cs2-dumper --interfaces --json --module client.dll > build/interfaces.json
cs2-dumper --schema --json --schema-system 0x7ff6XXXXXXXX > build/schema.json
cs2-dumper --gen --interfaces-json build/interfaces.json --schema-json build/schema.json
```

`--gen` takes an optional output directory (`--gen out/sdk`, or bare `--gen`
for `payload/sdk`). See `payload/sdk/README.md` for the regenerated layout.

## Tests

```sh
cargo test --manifest-path external/dumper/Cargo.toml
```

Fixtures cover the pattern engine, the `InterfaceReg` list resolver, the
schema walker and SDK header generation — all without a running game.

## Schema layout calibration

`schema::SchemaLayout` offsets are **version-specific placeholders**. They must
be calibrated against the live build (via ReVa/Ghidra + a schema dump) before
the schema output is trustworthy. The traversal/emit logic is fixture-tested
against the same layout, so only the numbers need updating.

## Live analysis (MCP server)

`cs2-dumper --mcp` runs a [Model Context Protocol](https://modelcontextprotocol.io)
server over stdio for live memory analysis of a running target. It is wired
into OpenCode as the `cs2-live` server (see `opencode.jsonc` and
`scripts/mcp-live.sh`). Tools:

| Tool | Purpose |
|---|---|
| `list_processes` / `attach` / `detach` | target selection (by name or pid) |
| `modules` | loaded modules with base/size |
| `read` / `read_cstring` | typed or raw reads |
| `resolve_chain` | follow a pointer chain |
| `scan_pattern` | IDA-style byte scan in a module/range |
| `scan_value` / `scan_next` | Cheat-Engine-style value narrowing |
| `interfaces` | enumerate `CreateInterface` registrations |

All tools reuse the same memory/pattern/schema code as the CLI. Values are
handled byte-exactly, so 64-bit pointers are matched precisely.

## Notes

- Reading another process's `/proc/<pid>/mem` is subject to the kernel's ptrace
  policy (`/proc/sys/kernel/yama/ptrace_scope`). If reads fail, see the ptrace
  section in `docs/architecture.md`.
- Prior art to learn from: `a2x/cs2-dumper`, `ValveResourceFormat/DumpSource2`,
  `scros22/cs2-universal-offsets`.
