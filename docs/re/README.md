# Reverse-engineering notes

Static analysis of build `25815307` (Windows depot) done with **Ghidra** (via
REA, headless). Findings are recorded per subsystem and are a work in progress.

- [`interface-system.md`](interface-system.md) — `CreateInterface` registration
- [`schema-system.md`](schema-system.md) — schema system
- [`client-dll.md`](client-dll.md) — entity system, local player, view matrix (WIP)
- [`engine2.md`](engine2.md) — console-command path, `-concommandpipe` (WIP)

> Addresses are absolute VAs for the module as loaded at its PE image base
> (`client.dll`/`engine2.dll`/`schemasystem.dll` → `0x180000000`).
