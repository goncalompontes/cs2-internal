# cs2-dumper

External offset/interface/schema dumper.

Runs as a normal Linux process and reads the Proton/Wine target process (a
Linux process at the OS level) via `/proc/<pid>/mem` + `/proc/<pid>/maps`.

## Build & test

```sh
cargo test --manifest-path external/dumper/Cargo.toml
cargo build --release --manifest-path external/dumper/Cargo.toml
```

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
```

## Layout

| Module | Responsibility |
|---|---|
| `pattern` | IDA-style pattern parse/scan + RIP-relative resolution (pure, tested) |
| `memory` | `ProcessMemory` trait + `LinuxProcMemory` (Linux `/proc`) + PID discovery |
| `pe` | PE image helpers (image base, size, exports) via `goblin` |
| `analysis` | signature registry + resolved offsets/interfaces (grows over time) |
| `output` | JSON serialization |

Memory access is behind `ProcessMemory`, so a `memflow` backend can be added
later without touching analysis code.

## Notes

- Reading another process's `/proc/<pid>/mem` is subject to the kernel's ptrace
  policy (`/proc/sys/kernel/yama/ptrace_scope`). If reads fail, see the ptrace
  section in `docs/architecture.md`.
- Prior art to learn from: `a2x/cs2-dumper`, `ValveResourceFormat/DumpSource2`,
  `scros22/cs2-universal-offsets`.
