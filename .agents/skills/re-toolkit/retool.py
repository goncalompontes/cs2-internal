#!/usr/bin/env python3
"""RE Toolkit — IDA MCP bridge + Cheat Engine MCP bridge + pattern scanning + PE parsing.

Usage:
  python retool.py <command> [args...]

Commands (IDA MCP — requires IDA Pro with mcp-plugin loaded):
  decompile <addr>              Decompile function at address
  disasm <addr>                 Disassemble function at address
  fn <name>                     Find function by name substring
  fn_addr <addr>                Get function info at address
  xrefs <addr>                  Get cross-references to address
  callers <addr>                Get functions that call this function
  callees <addr>                Get functions this function calls
  list_fn [offset [count]]      List functions (offset=0, count=100)
  list_str [filter] [offset]    List strings matching filter
  globals [filter] [offset]     List globals matching filter
  structs [filter]              Search structures by name
  struct <name>                 Get structure layout
  read_mem <addr> <size>        Read bytes from memory (hex dump)
  write_asm <addr> <instrs>     Patch assembly at address (unsafe)
  rename <addr> <name>          Rename function at address
  comment <addr> <text>         Set comment at address
  meta                          Get loaded module metadata

Commands (Cheat Engine — requires CE running with MCP bridge loaded):
  ce_ping                       Test connectivity to CE bridge
  ce_info                       Get attached process info (name, PID, modules)
  ce_modules [offset [limit]]   List loaded modules with base addresses
  ce_read <addr> [size]         Hex dump of memory (default 256 bytes)
  ce_read_int <addr> [type]     Read typed value (dword, float, qword, ...)
  ce_read_str <addr> [len] [w]  Read null-terminated string
  ce_read_ptr <addr> [offsets]  Follow pointer chain
  ce_write <addr> <bytes...>    Write raw bytes (e.g. "90 90 90")
  ce_write_int <addr> <val> [t] Write typed value
  ce_write_str <addr> <text>    Write string to memory
  ce_aob <pattern...>           Array-of-bytes scan (e.g. "48 89 5C ?? 48")
  ce_str <filter>               Search for string in memory
  ce_disasm <addr> [count]      Disassemble instructions
  ce_xrefs <addr>               Find references to address
  ce_callers <addr> [limit]     Find functions calling this address
  ce_fn <symbol>                Resolve symbol to address
  ce_sym_info <addr>            Get symbolic name for address
  ce_regions [max]              List memory regions
  ce_ds <addr> [size]           Dissect memory structure (auto-guess)
  ce_lua <code>                 Execute Lua code in Cheat Engine
  ce_scan <val> [type] [opt]    First memory scan (value-based)
  ce_nscan <type> [val] [v2]    Next scan to narrow results
  ce_sresults [offset [limit]]  Get scan results
  ce_gen_sig <addr>             Generate unique AOB signature for address
  ce_bp <addr> [id]             Set hardware execution breakpoint
  ce_bplist                     List active breakpoints
  ce_bphits [id] [clear]        Get breakpoint hit logs
  ce_bpdel <id>                 Remove a breakpoint

Commands (local — no IDA or CE required):
  pattern <file> <pattern>      Scan binary file for byte pattern
                                 Pattern: "48 8B C4 ?? 48 89 58"
  pe_sections <file>            List PE sections with VA/raw offsets
  pe_map <file> <rva>           Convert RVA to file offset and vice versa
  pe_strings <file> [min_len]   Extract printable strings from binary
  pe_export <file> [filter]     List exported functions

Environment:
  IDA_HOST  — IDA MCP server host (default: 127.0.0.1)
  IDA_PORT  — IDA MCP server port (default: 13337)
  CE_PIPE   — Cheat Engine MCP pipe name (default: \\.\pipe\CE_MCP_Bridge_v99)
"""

import sys
import os
import json
import struct
import http.client
from typing import Any, Optional

IDA_HOST = os.environ.get("IDA_HOST", "127.0.0.1")
IDA_PORT = int(os.environ.get("IDA_PORT", "13337"))

CE_PIPE = os.environ.get("CE_PIPE", r"\\.\pipe\CE_MCP_Bridge_v99")

try:
    import win32file
    import pywintypes
    _HAS_WIN32 = True
except ImportError:
    win32file = None  # type: ignore
    pywintypes = None  # type: ignore
    _HAS_WIN32 = False

# -- IDA JSON-RPC ----------------------------------------------

def _rpc(method: str, params: list = None) -> Any:
    """Call an IDA MCP JSON-RPC method."""
    conn = http.client.HTTPConnection(IDA_HOST, IDA_PORT, timeout=30)
    req = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or [],
        "id": 1,
    }
    try:
        conn.request("POST", "/mcp", json.dumps(req),
                     {"Content-Type": "application/json"})
        resp = conn.getresponse()
        data = json.loads(resp.read().decode())
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        conn.close()

    if "error" in data:
        err = data["error"]
        print(f"JSON-RPC error {err['code']}: {err['message']}")
        if "data" in err:
            print(err["data"])
        sys.exit(1)
    return data.get("result")


def _parse_addr(s: str) -> str:
    """Normalize address string: support 0x prefix, decimal, hex."""
    s = s.strip()
    if s.startswith("0x") or s.startswith("0X"):
        return s
    try:
        return f"0x{int(s):X}"
    except ValueError:
        return s


# -- IDA Commands ----------------------------------------------

# -- CE Named Pipe JSON-RPC -----------------------------------

def _ce_rpc(method: str, params: dict = None) -> Any:
    """Call a Cheat Engine MCP bridge JSON-RPC method via named pipe."""
    if not _HAS_WIN32:
        print("Error: pywin32 not installed. Run: pip install pywin32")
        sys.exit(1)

    req = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": int(os.times().elapsed * 1000) if hasattr(os, "times") else 1,
    }
    req_json = json.dumps(req).encode("utf-8")

    handle = None
    try:
        handle = win32file.CreateFile(
            CE_PIPE,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0, None, win32file.OPEN_EXISTING, 0, None,
        )
    except pywintypes.error as e:
        print(f"Error: Cannot connect to Cheat Engine pipe '{CE_PIPE}'")
        print(f"  Is Cheat Engine running with the MCP bridge loaded?")
        print(f"  ({e})")
        sys.exit(1)

    try:
        header = struct.pack("<I", len(req_json))
        win32file.WriteFile(handle, header)
        win32file.WriteFile(handle, req_json)

        resp_header = _read_exact(handle, 4)
        resp_len = struct.unpack("<I", resp_header)[0]
        if resp_len > 32 * 1024 * 1024:
            print(f"Error: response too large ({resp_len} bytes)")
            sys.exit(1)
        resp_body = _read_exact(handle, resp_len)
        data = json.loads(resp_body.decode("utf-8"))
    except pywintypes.error as e:
        print(f"Error: pipe communication failed: {e}")
        sys.exit(1)
    except (json.JSONDecodeError, ConnectionError) as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if handle:
            try:
                win32file.CloseHandle(handle)
            except Exception:
                pass

    if "error" in data:
        err = data["error"]
        print(f"CE error: {err}")
        sys.exit(1)
    return data.get("result", data)


def _read_exact(handle, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = win32file.ReadFile(handle, remaining)[1]
        if not chunk:
            raise ConnectionError("Connection closed while reading from CE pipe")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _ce_hex_dump(data_bytes: bytes, base_addr: int = 0) -> str:
    """Format bytes as a hex+ASCII dump, returning a string."""
    lines = []
    for i in range(0, len(data_bytes), 16):
        chunk = data_bytes[i : i + 16]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  0x{base_addr + i:016X}  {hex_part:<48s}  {ascii_part}")
    return "\n".join(lines)


# -- IDA Commands ----------------------------------------------

def cmd_decompile(args):
    addr = _parse_addr(args[0])
    code = _rpc("decompile_function", [addr])
    print(code)


def cmd_disasm(args):
    addr = _parse_addr(args[0])
    result = _rpc("disassemble_function", [addr])
    if isinstance(result, dict):
        print(f"; {result.get('name', '?')} @ {result.get('start_ea', addr)}")
        stack = result.get("stack_frame", [])
        if stack:
            print("; Stack frame:")
            for v in stack:
                if v["name"] != "__return_address":
                    print(f";   {v['name']}: +0x{int(v['offset'], 16):X} ({v['type']})")
        for instr in result.get("instructions", []):
            print(f"0x{instr.get('address', '?'):16s}  {instr.get('mnemonic', '')}  {instr.get('op_str', '')}")
    else:
        print(result)


def cmd_fn(args):
    name = args[0]
    result = _rpc("get_function_by_name", [name])
    if result is None:
        print(f"No function matching '{name}'")
    else:
        print(f"{result['name']} @ {result['address']}  size=0x{int(result['size'], 16):X}")


def cmd_fn_addr(args):
    addr = _parse_addr(args[0])
    result = _rpc("get_function_by_address", [addr])
    if result is None:
        print(f"No function at {addr}")
    else:
        print(f"{result['name']} @ {result['address']}  size=0x{int(result['size'], 16):X}")


def cmd_xrefs(args):
    addr = _parse_addr(args[0])
    result = _rpc("get_xrefs_to", [addr])
    if not result:
        print("No xrefs found")
    else:
        for x in result:
            fn = x.get("function", {})
            print(f"  {x.get('type', '?')} -> {fn.get('name', '?')} @ {fn.get('address', '?')}")


def cmd_callers(args):
    addr = _parse_addr(args[0])
    result = _rpc("get_callers", [addr])
    if not result:
        print("No callers found")
    else:
        for f in result:
            print(f"  {f['name']} @ {f['address']}")


def cmd_callees(args):
    addr = _parse_addr(args[0])
    result = _rpc("get_callees", [addr])
    if not result:
        print("No callees found")
    else:
        for c in result:
            print(f"  {c.get('name', '?')} -> {c.get('address', '?')}")


def cmd_list_fn(args):
    offset = int(args[0]) if len(args) > 0 else 0
    count = int(args[1]) if len(args) > 1 else 100
    result = _rpc("list_functions", [offset, count])
    items = result.get("items", [])
    for f in items:
        print(f"  {f['name']:50s} @ {f['address']}  size=0x{int(f['size'], 16):X}")
    if result.get("has_more"):
        next_off = offset + len(items)
        print(f"  ... more ({next_off} offset for next page)")


def cmd_list_str(args):
    filt = args[0] if len(args) > 0 else ""
    offset = int(args[1]) if len(args) > 1 else 0
    result = _rpc("list_strings_filter", [offset, 100, filt])
    items = result.get("items", [])
    for s in items:
        print(f"  {s.get('address', '?'):12s}  \"{s.get('value', '')}\"")
    if result.get("has_more"):
        print(f"  ... more ({offset + len(items)} offset for next page)")


def cmd_globals(args):
    filt = args[0] if len(args) > 0 else ""
    offset = int(args[1]) if len(args) > 1 else 0
    result = _rpc("list_globals_filter", [offset, 100, filt])
    items = result.get("items", [])
    for g in items:
        print(f"  {g.get('address', '?'):12s}  {g.get('name', '')}")
    if result.get("has_more"):
        print(f"  ... more ({offset + len(items)} offset for next page)")


def cmd_structs(args):
    filt = args[0] if len(args) > 0 else ""
    result = _rpc("search_structures", [filt])
    for s in result:
        print(f"  {s.get('name', '?')}")


def cmd_struct(args):
    name = args[0]
    result = _rpc("get_struct_info_simple", [name])
    if not result:
        print(f"Structure '{name}' not found")
        return
    print(f"struct {result.get('name', name)} {{  // size=0x{result.get('size', 0):X}")
    for m in result.get("members", []):
        off = int(m.get("offset", "0"), 16) if isinstance(m.get("offset"), str) else m.get("offset", 0)
        print(f"  +0x{off:04X}  {m.get('type', '?')} {m.get('name', '?')};")
    print("};")


def cmd_read_mem(args):
    addr = _parse_addr(args[0])
    size = int(args[1])
    result = _rpc("read_memory_bytes", [addr, size])
    # Parse "0xAB 0xCD ..." hex string
    hex_str = result.replace("0x", "").replace(" ", "")
    try:
        data = bytes.fromhex(hex_str)
    except ValueError:
        print(result)
        return
    va = int(addr, 16)
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  0x{va+i:016X}  {hex_part:<48s}  {ascii_part}")


def cmd_write_asm(args):
    addr = _parse_addr(args[0])
    instrs = " ".join(args[1:])
    result = _rpc("patch_address_assembles", [addr, instrs])
    print(f"Patched: {result}")


def cmd_rename(args):
    addr = _parse_addr(args[0])
    name = args[1]
    _rpc("rename_function", [addr, name])
    print(f"Renamed {addr} -> {name}")


def cmd_comment(args):
    addr = _parse_addr(args[0])
    text = " ".join(args[1:])
    _rpc("set_comment", [addr, text])
    print(f"Comment set at {addr}")


def cmd_meta(args):
    result = _rpc("get_metadata", [])
    for k, v in result.items():
        print(f"  {k}: {v}")


# -- Cheat Engine Commands (named pipe) -----------------------

def cmd_ce_ping(args):
    result = _ce_rpc("ping")
    print(f"CE Bridge: {result.get('version', '?')}  {result.get('message', '')}")


def cmd_ce_info(args):
    result = _ce_rpc("get_process_info")
    if result.get("success"):
        print(f"Process: {result.get('process_name', '?')}  PID: {result.get('process_id', '?')}")
        print(f"Modules: {result.get('module_count', 0)}")
        for m in result.get("modules", []):
            print(f"  {m['name']:30s}  {m['address']}  size=0x{int(m['size'], 16):X}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_modules(args):
    offset = int(args[0]) if len(args) > 0 else 0
    limit = int(args[1]) if len(args) > 1 else 100
    result = _ce_rpc("enum_modules", {"offset": offset, "limit": limit})
    for m in result.get("modules", []):
        sz = m['size']
        sz_val = int(sz, 16) if isinstance(sz, str) else int(sz)
        print(f"  {m['name']:30s}  {m['address']}  size=0x{sz_val:X}  {'x64' if m.get('is_64bit') else 'x86'}")
    if result.get("returned", 0) < result.get("total", 0):
        print(f"  ... more ({offset + result.get('returned', 0)} offset for next page)")


def cmd_ce_read(args):
    addr = _parse_addr(args[0])
    size = int(args[1]) if len(args) > 1 else 256
    result = _ce_rpc("read_memory", {"address": addr, "size": size})
    if not result.get("success"):
        print(f"Error: {result.get('error', 'unknown')}")
        return
    data_hex = result.get("data", "")
    try:
        data_bytes = bytes.fromhex(data_hex.replace(" ", ""))
    except ValueError:
        print(data_hex)
        return
    va = int(addr, 16)
    print(_ce_hex_dump(data_bytes, va))


def cmd_ce_read_int(args):
    addr = _parse_addr(args[0])
    vtype = args[1] if len(args) > 1 else "dword"
    result = _ce_rpc("read_integer", {"address": addr, "type": vtype})
    if result.get("success"):
        print(f"  {addr}: {result.get('value')}  ({result.get('hex', '?')})")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_read_str(args):
    addr = _parse_addr(args[0])
    max_len = int(args[1]) if len(args) > 1 else 256
    wide = len(args) > 2 and args[2].lower() in ("1", "true", "w", "wide")
    result = _ce_rpc("read_string", {"address": addr, "max_length": max_len, "wide": wide})
    if result.get("success"):
        print(f'  {addr}: "{result.get("value", "")}"')
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_read_ptr(args):
    addr = _parse_addr(args[0])
    if len(args) > 1:
        offsets = [int(a, 16) if str(a).startswith("0x") else int(a) for a in args[1:]]
    else:
        offsets = [0]
    result = _ce_rpc("read_pointer_chain", {"base": addr, "offsets": offsets})
    if result.get("success"):
        print(f"  Base: {result.get('base', addr)}")
        for step in result.get("chain", []):
            desc = step.get("description", "")
            off = step.get("hex_offset", "")
            ptr = step.get("pointer_value", "?")
            print(f"    [{step['step']}] {step['address']}{off} -> {ptr}  {desc}")
        print(f"  Final: {result.get('final_address', '?')} = {result.get('final_value', '?')}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_aob(args):
    pattern = " ".join(args)
    result = _ce_rpc("aob_scan", {"pattern": pattern, "limit": 100})
    if result.get("success"):
        print(f"Pattern: {pattern}")
        print(f"Matches: {result.get('count', 0)}")
        for a in result.get("addresses", []):
            print(f"  {a['address']}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_disasm(args):
    addr = _parse_addr(args[0])
    count = int(args[1]) if len(args) > 1 else 20
    result = _ce_rpc("disassemble", {"address": addr, "count": count})
    if result.get("success"):
        print(f"; {result.get('start_address', addr)} ({result.get('instruction_count', 0)} instructions)")
        for instr in result.get("instructions", []):
            print(f"  {instr['address']}  {instr['bytes']:<20s}  {instr['instruction']}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_xrefs(args):
    addr = _parse_addr(args[0])
    result = _ce_rpc("find_references", {"address": addr, "limit": 50})
    if result.get("success"):
        print(f"References to {addr}: {result.get('total', 0)} found")
        for r in result.get("references", []):
            print(f"  {r['address']}  {r['instruction']}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_callers(args):
    addr = _parse_addr(args[0])
    limit = int(args[1]) if len(args) > 1 else 50
    result = _ce_rpc("find_call_references", {"address": addr, "limit": limit})
    if result.get("success"):
        print(f"Callers of {addr}: {result.get('total', 0)} found")
        for c in result.get("callers", []):
            print(f"  {c['caller_address']}  {c['instruction']}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_fn(args):
    name = args[0]
    result = _ce_rpc("get_symbol_address", {"symbol": name})
    if result.get("success"):
        print(f"  {result['symbol']} -> {result['address']}")
    else:
        print(f"Not found: {name}")


def cmd_ce_sym_info(args):
    addr = _parse_addr(args[0])
    result = _ce_rpc("get_address_info", {"address": addr})
    if result.get("success"):
        print(f"  {addr} -> {result.get('symbolic_name', '?')}")
    else:
        print(f"No info for {addr}")


def cmd_ce_regions(args):
    max_r = int(args[0]) if len(args) > 0 else 100
    result = _ce_rpc("get_memory_regions", {"max": max_r})
    for r in result.get("regions", []):
        prot = r.get("protection", "?")
        flags = ""
        flags += "R" if r.get("readable") else "-"
        flags += "W" if r.get("writable") else "-"
        flags += "X" if r.get("executable") else "-"
        print(f"  {r['base']}  size=0x{int(r['size'], 16):X}  {prot}/{flags}")


def cmd_ce_write(args):
    addr = _parse_addr(args[0])
    bytes_str = " ".join(args[1:])
    byte_vals = [int(b, 16) for b in bytes_str.split()]
    result = _ce_rpc("write_memory", {"address": addr, "bytes": byte_vals})
    if result.get("success"):
        print(f"Wrote {result.get('bytes_written', 0)} bytes to {addr}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_write_int(args):
    addr = _parse_addr(args[0])
    value = int(args[1])
    vtype = args[2] if len(args) > 2 else "dword"
    result = _ce_rpc("write_integer", {"address": addr, "value": value, "type": vtype})
    if result.get("success"):
        print(f"Wrote {value} ({vtype}) to {addr}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_write_str(args):
    addr = _parse_addr(args[0])
    value = " ".join(args[1:])
    wide = False
    result = _ce_rpc("write_string", {"address": addr, "value": value, "wide": wide})
    if result.get("success"):
        print(f"Wrote string to {addr}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_ds(args):
    addr = _parse_addr(args[0])
    size = int(args[1]) if len(args) > 1 else 256
    result = _ce_rpc("dissect_structure", {"address": addr, "size": size})
    if result.get("success"):
        print(f"Structure at {addr} ({result.get('element_count', 0)} elements):")
        for e in result.get("elements", []):
            print(f"  +0x{e['offset']:04X}  {e['type']:12s}  {e.get('value', '')}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_lua(args):
    code = " ".join(args)
    result = _ce_rpc("evaluate_lua", {"code": code})
    if result.get("success"):
        print(result.get("result", ""))
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_scan(args):
    value = args[0] if len(args) > 0 else None
    vtype = args[1] if len(args) > 1 else "dword"
    option = args[2] if len(args) > 2 else "exact"
    value2 = args[3] if len(args) > 3 else None
    params = {"value": value, "type": vtype, "scan_option": option}
    if value2:
        params["value2"] = value2
    result = _ce_rpc("scan_all", params)
    if result.get("success"):
        print(f"First scan: {result.get('count', 0)} results")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_nscan(args):
    scan_type = args[0] if len(args) > 0 else "exact"
    value = args[1] if len(args) > 1 else None
    value2 = args[2] if len(args) > 2 else None
    params = {"scan_type": scan_type}
    if value is not None:
        params["value"] = value
    if value2 is not None:
        params["value2"] = value2
    result = _ce_rpc("next_scan", params)
    if result.get("success"):
        print(f"Next scan: {result.get('count', 0)} results")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_sresults(args):
    offset = int(args[0]) if len(args) > 0 else 0
    limit = int(args[1]) if len(args) > 1 else 100
    result = _ce_rpc("get_scan_results", {"offset": offset, "limit": limit})
    for r in result.get("results", []):
        print(f"  {r['address']}  {r.get('value', '')}")
    if result.get("returned", 0) < result.get("total", 0):
        print(f"  ... more ({offset + result.get('returned', 0)} offset for next page)")


def cmd_ce_str(args):
    filt = args[0] if len(args) > 0 else ""
    result = _ce_rpc("search_string", {"string": filt, "limit": 100})
    if result.get("success"):
        for a in result.get("addresses", []):
            print(f"  {a['address']}  \"{a.get('preview', '')}\"")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_bp(args):
    addr = _parse_addr(args[0])
    bpid = args[1] if len(args) > 1 else None
    result = _ce_rpc("set_breakpoint", {"address": addr, "id": bpid})
    if result.get("success"):
        print(f"Breakpoint {result.get('id', '?')} @ {addr}  slot={result.get('slot', '?')}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_bplist(args):
    result = _ce_rpc("list_breakpoints")
    for b in result.get("breakpoints", []):
        print(f"  {b['id']:20s}  {b['address']}  type={b.get('type', '?')}  slot={b.get('slot', '?')}")


def cmd_ce_bphits(args):
    bpid = args[0] if len(args) > 0 else None
    clear = len(args) > 1 and args[1].lower() in ("1", "true", "clear")
    result = _ce_rpc("get_breakpoint_hits", {"id": bpid, "clear": clear})
    for h in result.get("hits", []):
        print(f"  {h.get('id', '?')} @ {h.get('address', '?')}  {h.get('instruction', '')}")
        regs = h.get("registers", {})
        if regs:
            print(f"    {json.dumps(regs)}")


def cmd_ce_bpdel(args):
    bpid = args[0]
    result = _ce_rpc("remove_breakpoint", {"id": bpid})
    if result.get("success"):
        print(f"Removed breakpoint '{bpid}'")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


def cmd_ce_gen_sig(args):
    addr = _parse_addr(args[0])
    result = _ce_rpc("generate_signature", {"address": addr})
    if result.get("success"):
        print(f"Signature for {addr}:")
        print(f"  {result.get('signature', '')}")
        print(f"  Offset: {result.get('offset_from_start', 0)}")
    else:
        print(f"Error: {result.get('error', 'unknown')}")


# -- Local Commands (no IDA required) -------------------------

def cmd_pattern(args):
    """Scan a binary file for a byte pattern."""
    filepath = args[0]
    pattern = " ".join(args[1:]) if len(args) > 2 else args[1]

    parts = pattern.split()
    needle = []
    mask = []
    for p in parts:
        if p in ("?", "??"):
            needle.append(0)
            mask.append(False)
        else:
            needle.append(int(p, 16))
            mask.append(True)

    with open(filepath, "rb") as f:
        data = f.read()

    matches = []
    for i in range(len(data) - len(needle)):
        ok = True
        for j in range(len(needle)):
            if mask[j] and data[i + j] != needle[j]:
                ok = False
                break
        if ok:
            matches.append(i)
            if len(matches) >= 50:
                break

    if not matches:
        print("No matches found")
        return

    print(f"File: {filepath} ({len(data)} bytes)")
    print(f"Pattern: {pattern}")
    print(f"Matches: {len(matches)}{'(truncated)' if len(matches) >= 50 else ''}\n")

    for m in matches[:20]:
        end = min(len(data), m + len(needle) + 32)
        actual = " ".join(f"{data[k]:02X}" for k in range(m, end))
        print(f"  file+0x{m:X}: {actual}")


def cmd_pe_sections(args):
    """List PE sections with VA/raw offset mapping."""
    filepath = args[0]
    with open(filepath, "rb") as f:
        data = f.read()

    # DOS header
    if data[:2] != b"MZ":
        print("Not a valid PE file")
        return
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\x00\x00":
        print("Not a valid PE file")
        return

    # COFF header
    num_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, pe_offset + 20)[0]

    # Machine type and characteristics
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    img_base = struct.unpack_from("<Q", data, pe_offset + 24 + 24)[0]

    print(f"PE: {filepath}")
    print(f"  Machine: 0x{machine:04X}  Sections: {num_sections}")
    print(f"  Image base: 0x{img_base:X}")

    sec_start = pe_offset + 24 + opt_hdr_size
    print(f"\n  {'Name':8s}  {'VA':>10s}  {'VSize':>8s}  {'RawOff':>8s}  {'RawSz':>8s}  {'Flags':>8s}")
    print(f"  {'--------'}  {'----------'}  {'--------'}  {'--------'}  {'--------'}  {'--------'}")

    for i in range(num_sections):
        off = sec_start + i * 40
        name = data[off:off + 8].rstrip(b"\x00").decode("ascii", errors="replace")
        vsize = struct.unpack_from("<I", data, off + 8)[0]
        va = struct.unpack_from("<I", data, off + 12)[0]
        raw_size = struct.unpack_from("<I", data, off + 16)[0]
        raw_off = struct.unpack_from("<I", data, off + 20)[0]
        flags = struct.unpack_from("<I", data, off + 36)[0]
        print(f"  {name:8s}  0x{va:08X}  0x{vsize:06X}  0x{raw_off:06X}  0x{raw_size:06X}  0x{flags:06X}")


def cmd_pe_map(args):
    """Convert between RVA and file offset."""
    filepath = args[0]
    rva_str = args[1]

    rva = int(rva_str, 16) if rva_str.startswith("0x") else int(rva_str)

    with open(filepath, "rb") as f:
        data = f.read()

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    sec_start = pe_offset + 24 + opt_hdr_size

    for i in range(num_sections):
        off = sec_start + i * 40
        name = data[off:off + 8].rstrip(b"\x00").decode("ascii", errors="replace")
        va = struct.unpack_from("<I", data, off + 12)[0]
        vsize = struct.unpack_from("<I", data, off + 8)[0]
        raw_off = struct.unpack_from("<I", data, off + 20)[0]
        raw_size = struct.unpack_from("<I", data, off + 16)[0]

        if raw_off <= rva < raw_off + raw_size:
            map_va = va + (rva - raw_off)
            print(f"  file+0x{rva:X} -> {name} -> RVA 0x{map_va:X}")
            return
        if va <= rva < va + vsize and raw_off != 0:
            map_raw = raw_off + (rva - va)
            print(f"  RVA 0x{rva:X} -> {name} -> file+0x{map_raw:X}")
            return

    print(f"  offset 0x{rva:X} not found in any section")


def cmd_pe_strings(args):
    """Extract printable ASCII strings from a binary."""
    filepath = args[0]
    min_len = int(args[1]) if len(args) > 1 else 4

    with open(filepath, "rb") as f:
        data = f.read()

    current = b""
    for i, b in enumerate(data):
        if 32 <= b < 127:
            current += bytes([b])
        else:
            if len(current) >= min_len:
                off = i - len(current)
                print(f"  0x{off:X}: {current.decode('ascii')}")
            current = b""
    if len(current) >= min_len:
        print(f"  0x{len(data)-len(current):X}: {current.decode('ascii')}")


def cmd_pe_export(args):
    """List exported functions from a PE/PDB."""
    filepath = args[0]
    filt = args[1].lower() if len(args) > 1 else ""

    with open(filepath, "rb") as f:
        data = f.read()

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    # Get export directory RVA
    export_rva = struct.unpack_from("<I", data, pe_offset + 24 + 96 + 16)[0]
    export_size = struct.unpack_from("<I", data, pe_offset + 24 + 96 + 20)[0]
    if export_rva == 0:
        print("No export directory")
        return

    # Find which section contains the export directory
    num_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    sec_start = pe_offset + 24 + opt_hdr_size

    def rva_to_raw(rva_val):
        for j in range(num_sections):
            s_off = sec_start + j * 40
            s_va = struct.unpack_from("<I", data, s_off + 12)[0]
            s_vsize = struct.unpack_from("<I", data, s_off + 8)[0]
            s_raw = struct.unpack_from("<I", data, s_off + 20)[0]
            if s_va <= rva_val < s_va + s_vsize and s_raw != 0:
                return s_raw + (rva_val - s_va)
        return None

    export_raw = rva_to_raw(export_rva)
    if export_raw is None:
        print("Cannot resolve export directory")
        return

    num_names = struct.unpack_from("<I", data, export_raw + 24)[0]
    func_rva = struct.unpack_from("<I", data, export_raw + 28)[0]
    name_rva = struct.unpack_from("<I", data, export_raw + 32)[0]
    ordinal_base = struct.unpack_from("<I", data, export_raw + 16)[0]

    func_raw = rva_to_raw(func_rva)
    name_raw = rva_to_raw(name_rva)

    if func_raw is None or name_raw is None:
        print("Cannot resolve export tables")
        return

    print(f"Exports: {num_names} named functions")
    ordinal_rva = struct.unpack_from("<I", data, export_raw + 36)[0]
    ordinal_raw = rva_to_raw(ordinal_rva) if ordinal_rva else None

    count = 0
    for i in range(num_names):
        name_idx = struct.unpack_from("<I", data, name_raw + i * 4)[0]
        name_r = rva_to_raw(name_idx)
        if name_r is None:
            continue
        end = data.find(b"\x00", name_r)
        name = data[name_r:end].decode("ascii", errors="replace")
        if filt and filt not in name.lower():
            continue

        if ordinal_raw:
            ord_val = struct.unpack_from("<H", data, ordinal_raw + i * 2)[0]
        else:
            ord_val = i
        func_idx = struct.unpack_from("<I", data, func_raw + ord_val * 4)[0]
        print(f"  {name:60s}  RVA 0x{func_idx:08X}")
        count += 1
        if count >= 200:
            print(f"  ... truncated at 200")
            break


# -- Command dispatch ------------------------------------------

COMMANDS = {
    "decompile": cmd_decompile,
    "disasm": cmd_disasm,
    "fn": cmd_fn,
    "fn_addr": cmd_fn_addr,
    "xrefs": cmd_xrefs,
    "callers": cmd_callers,
    "callees": cmd_callees,
    "list_fn": cmd_list_fn,
    "list_str": cmd_list_str,
    "globals": cmd_globals,
    "structs": cmd_structs,
    "struct": cmd_struct,
    "read_mem": cmd_read_mem,
    "write_asm": cmd_write_asm,
    "rename": cmd_rename,
    "comment": cmd_comment,
    "meta": cmd_meta,
    "pattern": cmd_pattern,
    "pe_sections": cmd_pe_sections,
    "pe_map": cmd_pe_map,
    "pe_strings": cmd_pe_strings,
    "pe_export": cmd_pe_export,
    # CE commands
    "ce_ping": cmd_ce_ping,
    "ce_info": cmd_ce_info,
    "ce_modules": cmd_ce_modules,
    "ce_read": cmd_ce_read,
    "ce_read_int": cmd_ce_read_int,
    "ce_read_str": cmd_ce_read_str,
    "ce_read_ptr": cmd_ce_read_ptr,
    "ce_write": cmd_ce_write,
    "ce_write_int": cmd_ce_write_int,
    "ce_write_str": cmd_ce_write_str,
    "ce_aob": cmd_ce_aob,
    "ce_str": cmd_ce_str,
    "ce_disasm": cmd_ce_disasm,
    "ce_xrefs": cmd_ce_xrefs,
    "ce_callers": cmd_ce_callers,
    "ce_fn": cmd_ce_fn,
    "ce_sym_info": cmd_ce_sym_info,
    "ce_regions": cmd_ce_regions,
    "ce_ds": cmd_ce_ds,
    "ce_lua": cmd_ce_lua,
    "ce_scan": cmd_ce_scan,
    "ce_nscan": cmd_ce_nscan,
    "ce_sresults": cmd_ce_sresults,
    "ce_gen_sig": cmd_ce_gen_sig,
    "ce_bp": cmd_ce_bp,
    "ce_bplist": cmd_ce_bplist,
    "ce_bphits": cmd_ce_bphits,
    "ce_bpdel": cmd_ce_bpdel,
}

ALIASES = {
    "dec": "decompile",
    "d": "disasm",
    "f": "fn",
    "fa": "fn_addr",
    "x": "xrefs",
    "c": "callers",
    "cc": "callees",
    "lf": "list_fn",
    "ls": "list_str",
    "g": "globals",
    "ss": "structs",
    "s": "struct",
    "rm": "read_mem",
    "ps": "pe_sections",
    "pm": "pe_map",
    "pstr": "pe_strings",
    "pexp": "pe_export",
    "pat": "pattern",
    # CE aliases
    "cr": "ce_read",
    "cw": "ce_write",
    "ca": "ce_aob",
    "cd": "ce_disasm",
    "cx": "ce_xrefs",
    "cf": "ce_fn",
    "cl": "ce_lua",
}

def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1]
    if cmd in ALIASES:
        cmd = ALIASES[cmd]
    if cmd not in COMMANDS:
        print(f"Unknown command: {cmd}")
        print(f"Available: {', '.join(sorted(COMMANDS))}")
        sys.exit(1)

    args = sys.argv[2:]
    COMMANDS[cmd](args)


if __name__ == "__main__":
    main()

