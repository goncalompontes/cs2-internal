---
name: re-toolkit
description: Full reverse engineering toolkit — IDA MCP bridge (decompile, disassemble, xrefs, callers/callees, structs, read memory, rename), Cheat Engine MCP bridge (memory r/w, AOB scan, disassembly, breakpoints, pointer chains, value scanning), binary pattern scanning, PE section/export/string parsing. Use whenever the user asks about reverse engineering, IDA verification, signature checking, PE analysis, Cheat Engine memory operations, or pattern hunting.
---

# RE Toolkit

Use `python "<skill_dir>/retool.py" <command> [args...]` where `<skill_dir>` is the directory containing this SKILL.md.

## Quick Reference

### IDA Commands (require IDA Pro open with mcp-plugin loaded on port 13337)

| Command | Alias | What it does |
|---------|-------|-------------|
| `decompile <addr>` | `dec` | Decompile function to C |
| `disasm <addr>` | `d` | Disassemble function to assembly |
| `fn <name-substr>` | `f` | Search functions by name |
| `fn_addr <addr>` | `fa` | Get function info at address |
| `xrefs <addr>` | `x` | Cross-references to address |
| `callers <addr>` | `c` | Functions calling this function |
| `callees <addr>` | `cc` | Functions called by this function |
| `list_fn [off] [n]` | `lf` | Paginated function list |
| `list_str [filter] [off]` | `ls` | Strings matching filter |
| `globals [filter] [off]` | `g` | Globals matching filter |
| `structs [filter]` | `ss` | Search structures by name |
| `struct <name>` | `s` | Structure layout with offsets |
| `read_mem <addr> <n>` | `rm` | Hex dump n bytes at address |
| `write_asm <addr> <instrs>` | — | Patch assembly (unsafe!) |
| `rename <addr> <name>` | — | Rename function |
| `comment <addr> <text>` | — | Set comment |
| `meta` | — | Module metadata (path, base, md5, ...) |

### Cheat Engine Commands (require CE running with MCP bridge loaded)

| Command | Alias | What it does |
|---------|-------|-------------|
| `ce_ping` | — | Test connectivity to CE bridge |
| `ce_info` | — | Attached process name, PID, modules |
| `ce_modules [off] [n]` | — | List loaded modules with bases |
| `ce_read <addr> [size]` | `cr` | Hex dump of memory |
| `ce_read_int <addr> [type]` | — | Read typed value (dword, float, ...) |
| `ce_read_str <addr> [len] [w]` | — | Read null-terminated string |
| `ce_read_ptr <addr> [offs...]` | — | Follow pointer chain |
| `ce_write <addr> <bytes>` | `cw` | Write raw bytes |
| `ce_write_int <addr> <val> [t]` | — | Write typed value |
| `ce_write_str <addr> <text>` | — | Write string to memory |
| `ce_aob <pattern>` | `ca` | Array-of-bytes scan with `??` wildcards |
| `ce_str <filter>` | — | Search for string in memory |
| `ce_disasm <addr> [count]` | `cd` | Disassemble instructions |
| `ce_xrefs <addr>` | `cx` | Find code references to address |
| `ce_callers <addr> [n]` | — | Find functions calling this address |
| `ce_fn <symbol>` | `cf` | Resolve symbol to address |
| `ce_sym_info <addr>` | — | Get symbolic name for address |
| `ce_regions [max]` | — | List memory regions |
| `ce_ds <addr> [size]` | — | Dissect memory structure (auto-guess) |
| `ce_lua <code>` | `cl` | Execute Lua in Cheat Engine |
| `ce_scan <val> [type] [opt]` | — | First memory scan |
| `ce_nscan <type> [val] [v2]` | — | Next scan to narrow results |
| `ce_sresults [off] [n]` | — | Get scan results |
| `ce_gen_sig <addr>` | — | Generate unique AOB signature |
| `ce_bp <addr> [id]` | — | Set hardware execution breakpoint |
| `ce_bplist` | — | List active breakpoints |
| `ce_bphits [id] [clear]` | — | Get breakpoint hit logs |
| `ce_bpdel <id>` | — | Remove a breakpoint |

### Local Commands (no IDA needed)

| Command | Alias | What it does |
|---------|-------|-------------|
| `pattern <file> <pattern>` | `pat` | Scan binary for byte pattern with `??` wildcards |
| `pe_sections <file>` | `ps` | PE section headers with VA↔raw mapping |
| `pe_map <file> <rva>` | `pm` | Convert RVA ↔ file offset |
| `pe_strings <file> [min]` | `pstr` | Extract printable strings |
| `pe_export <file> [filter]` | `pexp` | List exported functions |

## Usage Patterns

### Verifying a signature
```bash
# 1. Check PE sections to understand file→RVA mapping
python retool.py pe_sections F:\...\scenesystem.dll

# 2. Scan the binary for the pattern
python retool.py pattern F:\...\scenesystem.dll "48 8B C4 48 89 58 ?? 48 89 50 ?? 55 56 57"

# 3. Map file offset to RVA
python retool.py pe_map F:\...\scenesystem.dll 0x6F920

# 4. Check if it's a function entry point (IDA must be open with that DLL loaded)
python retool.py fn_addr 0x180070520

# 5. Decompile to verify it's the right function
python retool.py dec 0x180070520
```

### Checking struct layout
```bash
python retool.py struct C_CSPlayerPawn
```

### Tracing call flow
```bash
# Who calls this function?
python retool.py callers 0x180070520

# What does it call?
python retool.py callees 0x180070520

# What references this global?
python retool.py xrefs client_base + 0x254FE70
```

### Pattern hunting across multiple DLLs
```bash
for dll in client.dll engine2.dll scenesystem.dll; do
    echo "=== $dll ==="
    python retool.py pattern "F:\...\$dll" "48 8B 0D ?? ?? ?? ?? 48 8D 55"
done
```

### Cheat Engine memory operations
```bash
# Check connectivity
python retool.py ce_ping

# Get attached process info
python retool.py ce_info

# Read memory at an address
python retool.py ce_read 0x7FF600000000 128
python retool.py ce_read client.dll+0x1000 64

# Read typed values
python retool.py ce_read_int client.dll+0x254FE70 dword
python retool.py ce_read_int 0x1A2B3C4D float

# Follow a pointer chain
python retool.py ce_read_ptr client.dll+0x1000 0x10 0x20 0x8

# AOB scan
python retool.py ce_aob "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??"

# Disassemble
python retool.py ce_disasm client.dll+0x1000 30

# Find who writes to an address
python retool.py ce_xrefs client.dll+0x254FE70

# Find callers of a function
python retool.py ce_callers client.dll+0x70000

# Value scanning workflow
python retool.py ce_scan 100 dword          # first scan for dword=100
python retool.py ce_nscan decreased          # value decreased
python retool.py ce_nscan exact 50           # now equals 50
python retool.py ce_sresults                  # show results
```

## Environment

Set `IDA_HOST` and `IDA_PORT` env vars if not using defaults (127.0.0.1:13337).

Set `CE_PIPE` env var to use a custom pipe name (default: `\\.\pipe\CE_MCP_Bridge_v99`).

## Setup

### Cheat Engine MCP Bridge
1. Clone `https://github.com/miscusi-peek/cheatengine-mcp-bridge`
2. Load `MCP_Server/ce_mcp_bridge.lua` in Cheat Engine (File → Execute Script)
3. CE console prints: `[MCP v12.0.0] MCP Server Listening on: CE_MCP_Bridge_v99`
4. Now retool.py CE commands will work

## Notes

- Addresses can be hex (`0x180070520`), decimal, or RVA if the IDA module base is known
- Pattern format: space-separated hex bytes, `??` or `?` for wildcards
- `decompile` output can be large — pipe through `head` or redirect to file
- `pe_sections` is the first command to run when working with a new DLL to understand the file→memory mapping
