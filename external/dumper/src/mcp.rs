//! Minimal MCP (Model Context Protocol) server over stdio for live analysis.
//!
//! Speaks newline-delimited JSON-RPC 2.0 (the MCP stdio transport) with the
//! subset OpenCode uses: `initialize`, `tools/list` and `tools/call`. Every
//! tool reuses the same memory / pattern / interface / schema code as the CLI,
//! so there is no duplicated analysis logic.
//!
//! This is the "live" counterpart to the static dumper: it lets an agent
//! inspect and narrow values in a running target (a native Linux process, i.e.
//! the Proton/Wine client at the OS level) — a native, scriptable alternative
//! to Cheat Engine.

use std::io::{BufRead, Write};
use std::sync::Arc;

use anyhow::{anyhow, Result};
use serde_json::{json, Value};

use crate::interfaces;
use crate::live::{self, ValueKind};
use crate::memory::{find_pid, LinuxProcMemory, ProcessMemory};
use crate::pattern::Pattern;
use crate::reader::{BytesReader, MemoryRead};

const PROTOCOL_VERSION: &str = "2024-11-05";
const SERVER_NAME: &str = "cs2-live";

/// One active narrowing scan.
struct ScanState {
    kind: ValueKind,
    candidates: Vec<(usize, Vec<u8>)>,
}

#[derive(Default)]
struct Session {
    mem: Option<Arc<LinuxProcMemory>>,
    scan: Option<ScanState>,
}

/// Run the MCP stdio loop until EOF.
pub fn serve() -> Result<()> {
    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    let mut session = Session::default();
    let mut out = stdout.lock();

    for line in stdin.lock().lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let message: Value = match serde_json::from_str(&line) {
            Ok(value) => value,
            Err(e) => {
                let response = err_response(Value::Null, -32700, &format!("parse error: {e}"));
                writeln!(out, "{}", serde_json::to_string(&response)?)?;
                out.flush()?;
                continue;
            }
        };
        if let Some(response) = handle(&mut session, &message) {
            writeln!(out, "{}", serde_json::to_string(&response)?)?;
            out.flush()?;
        }
    }
    Ok(())
}

/// Handle one JSON-RPC message. Returns `None` for notifications.
fn handle(session: &mut Session, message: &Value) -> Option<Value> {
    let method = message.get("method").and_then(Value::as_str).unwrap_or("");

    // Notifications carry no id and expect no response.
    let id = message.get("id").cloned()?;

    let result = match method {
        "initialize" => {
            let requested = message
                .get("params")
                .and_then(|p| p.get("protocolVersion"))
                .and_then(Value::as_str)
                .unwrap_or(PROTOCOL_VERSION);
            Ok(json!({
                "protocolVersion": requested,
                "capabilities": { "tools": {} },
                "serverInfo": { "name": SERVER_NAME, "version": env!("CARGO_PKG_VERSION") },
            }))
        }
        "ping" => Ok(json!({})),
        "tools/list" => Ok(json!({ "tools": tools() })),
        "tools/call" => match call_tool(session, message) {
            Ok(value) => Ok(value),
            Err(e) => Ok(tool_error(&e.to_string())),
        },
        other => Err(format!("method not found: {other}")),
    };

    Some(match result {
        Ok(value) => json!({ "jsonrpc": "2.0", "id": id, "result": value }),
        Err(msg) => err_response(id, -32601, &msg),
    })
}

fn err_response(id: Value, code: i64, message: &str) -> Value {
    json!({ "jsonrpc": "2.0", "id": id, "error": { "code": code, "message": message } })
}

fn tool_result(text: String) -> Value {
    json!({
        "content": [ { "type": "text", "text": text } ],
        "isError": false,
    })
}

fn tool_error(text: &str) -> Value {
    json!({
        "content": [ { "type": "text", "text": text } ],
        "isError": true,
    })
}

fn call_tool(session: &mut Session, message: &Value) -> Result<Value> {
    let params = message
        .get("params")
        .ok_or_else(|| anyhow!("missing params"))?;
    let name = params
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("missing tool name"))?;
    let args = params.get("arguments").cloned().unwrap_or(json!({}));

    let text = dispatch(session, name, &args)?;
    Ok(tool_result(text))
}

fn dispatch(session: &mut Session, name: &str, args: &Value) -> Result<String> {
    match name {
        "list_processes" => list_processes(args),
        "attach" => attach(session, args),
        "detach" => {
            session.mem = None;
            session.scan = None;
            Ok("detached".to_string())
        }
        "modules" => {
            let mem = require_mem(session)?;
            let filter = args.get("filter").and_then(Value::as_str).unwrap_or("");
            let modules: Vec<Value> = mem
                .modules()
                .into_iter()
                .filter(|m| filter.is_empty() || m.name.to_ascii_lowercase().contains(&filter.to_ascii_lowercase()))
                .map(|m| json!({ "name": m.name, "base": format!("0x{:x}", m.base), "size": m.size }))
                .collect();
            Ok(pretty(&modules))
        }
        "read" => read(session, args),
        "read_cstring" => read_cstring(session, args),
        "read_pointer" => resolve_chain(session, args),
        "resolve_chain" => resolve_chain(session, args),
        "scan_pattern" => scan_pattern(session, args),
        "scan_value" => scan_value(session, args),
        "scan_next" => scan_next(session, args),
        "interfaces" => interfaces_cmd(session, args),
        other => Err(anyhow!("unknown tool {other:?}")),
    }
}

fn require_mem(session: &Session) -> Result<Arc<LinuxProcMemory>> {
    session
        .mem
        .clone()
        .ok_or_else(|| anyhow!("no target attached; call `attach` first"))
}

fn list_processes(args: &Value) -> Result<String> {
    let filter = args.get("name").and_then(Value::as_str).unwrap_or("");
    let mut found = Vec::new();
    if let Ok(entries) = std::fs::read_dir("/proc") {
        for entry in entries.flatten() {
            let Ok(pid) = entry.file_name().to_string_lossy().parse::<u32>() else {
                continue;
            };
            let comm = std::fs::read_to_string(format!("/proc/{pid}/comm"))
                .unwrap_or_default()
                .trim()
                .to_string();
            if comm.is_empty() {
                continue;
            }
            if filter.is_empty()
                || comm
                    .to_ascii_lowercase()
                    .contains(&filter.to_ascii_lowercase())
            {
                found.push(json!({ "pid": pid, "name": comm }));
            }
        }
    }
    found.sort_by_key(|v| v.get("pid").and_then(Value::as_u64).unwrap_or(0));
    Ok(pretty(&found))
}

fn attach(session: &mut Session, args: &Value) -> Result<String> {
    let pid = if let Some(pid) = args.get("pid").and_then(Value::as_u64) {
        pid as u32
    } else if let Some(process) = args.get("process").and_then(Value::as_str) {
        find_pid(process).ok_or_else(|| anyhow!("process {process:?} not found"))?
    } else {
        // Default: the server's own process (useful for self-tests).
        std::process::id()
    };
    let mem = LinuxProcMemory::open(pid)?;
    let modules = mem.modules().len();
    session.mem = Some(Arc::new(mem));
    session.scan = None;
    Ok(pretty(&json!({ "pid": pid, "modules": modules })))
}

fn read(session: &Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let addr = live::parse_addr(
        args.get("address")
            .ok_or_else(|| anyhow!("missing address"))?,
    )?;
    let size = args.get("size").and_then(Value::as_u64).unwrap_or(16) as usize;
    let bytes = mem.read_vec(addr, size)?;
    if let Some(type_name) = args.get("type").and_then(Value::as_str) {
        let kind = ValueKind::parse(type_name)?;
        return Ok(pretty(&json!({
            "address": format!("0x{addr:x}"),
            "type": type_name,
            "value": kind.format(&bytes),
            "bytes": hex(&bytes),
        })));
    }
    Ok(pretty(
        &json!({ "address": format!("0x{addr:x}"), "bytes": hex(&bytes) }),
    ))
}

fn read_cstring(session: &Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let addr = live::parse_addr(
        args.get("address")
            .ok_or_else(|| anyhow!("missing address"))?,
    )?;
    let s = mem.read_cstring(addr)?;
    Ok(pretty(
        &json!({ "address": format!("0x{addr:x}"), "string": s }),
    ))
}

fn resolve_chain(session: &Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let base = live::parse_addr(
        args.get("base")
            .or_else(|| args.get("address"))
            .ok_or_else(|| anyhow!("missing base"))?,
    )?;
    let offsets: Vec<usize> = args
        .get("offsets")
        .and_then(Value::as_array)
        .map(|arr| {
            arr.iter()
                .filter_map(|v| v.as_u64().map(|n| n as usize))
                .collect()
        })
        .unwrap_or_default();
    let deref_first = args
        .get("deref_first")
        .and_then(Value::as_bool)
        .unwrap_or(true);
    let steps = live::resolve_chain(&*mem, base, &offsets, deref_first)?;
    let formatted: Vec<String> = steps.iter().map(|a| format!("0x{a:x}")).collect();
    Ok(pretty(
        &json!({ "steps": formatted, "final": formatted.last() }),
    ))
}

fn scan_pattern(session: &Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let text = args
        .get("pattern")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("missing pattern"))?;
    let pattern = Pattern::parse(text)?;
    let limit = args.get("limit").and_then(Value::as_u64).unwrap_or(64) as usize;

    let (base, size) = match args.get("module").and_then(Value::as_str) {
        Some(name) => {
            let module = mem
                .module_by_name(name)
                .ok_or_else(|| anyhow!("module {name:?} not found"))?;
            (module.base, module.size)
        }
        None => {
            let base = live::parse_addr(
                args.get("address")
                    .ok_or_else(|| anyhow!("need module or address"))?,
            )?;
            let size = args.get("size").and_then(Value::as_u64).unwrap_or(0x1000) as usize;
            (base, size)
        }
    };

    let bytes = live::read_span(&*mem, base, size);
    let matches: Vec<String> = pattern
        .find_all(&bytes)
        .into_iter()
        .take(limit)
        .map(|off| format!("0x{:x}", base + off))
        .collect();
    Ok(pretty(
        &json!({ "count": matches.len(), "matches": matches }),
    ))
}

fn scan_value(session: &mut Session, args: &Value) -> Result<String> {
    let raw = args.get("value").ok_or_else(|| anyhow!("missing value"))?;
    let type_name = args.get("type").and_then(Value::as_str).unwrap_or("u32");
    let kind = ValueKind::parse(type_name)?;
    let needle = kind.encode(raw)?;
    let limit = args.get("limit").and_then(Value::as_u64).unwrap_or(64) as usize;
    let max_bytes = args
        .get("max_bytes")
        .and_then(Value::as_u64)
        .unwrap_or(256 << 20) as usize;
    let writable_only = args
        .get("writable_only")
        .and_then(Value::as_bool)
        .unwrap_or(true);
    let mem = require_mem(session)?;

    let regions = live::scan_regions(&*mem, writable_only);
    let hits = live::scan_bytes(&*mem, &needle, &regions, limit, max_bytes);
    let candidates: Vec<(usize, Vec<u8>)> = hits
        .iter()
        .filter_map(|&addr| mem.read_vec(addr, kind.size()).ok().map(|b| (addr, b)))
        .collect();

    let addrs: Vec<String> = candidates.iter().map(|(a, _)| format!("0x{a:x}")).collect();
    let count = addrs.len();
    session.scan = Some(ScanState { kind, candidates });
    Ok(pretty(
        &json!({ "type": type_name, "count": count, "addresses": addrs }),
    ))
}

fn scan_next(session: &mut Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let state = session
        .scan
        .as_mut()
        .ok_or_else(|| anyhow!("no active scan; call scan_value first"))?;
    let kind = state.kind;

    let target = match args.get("value") {
        Some(value) => Some(kind.encode(value)?),
        None => None,
    };
    let changed = args.get("changed").and_then(Value::as_bool);

    let mut kept = Vec::new();
    for (addr, previous) in &state.candidates {
        let Ok(bytes) = mem.read_vec(*addr, kind.size()) else {
            continue;
        };
        if let Some(target) = &target {
            if &bytes != target {
                continue;
            }
        }
        if let Some(changed) = changed {
            if (&bytes != previous) != changed {
                continue;
            }
        }
        kept.push((*addr, bytes));
    }

    let addrs: Vec<String> = kept.iter().map(|(a, _)| format!("0x{a:x}")).collect();
    let count = addrs.len();
    state.candidates = kept;
    Ok(pretty(&json!({ "count": count, "addresses": addrs })))
}

fn interfaces_cmd(session: &Session, args: &Value) -> Result<String> {
    let mem = require_mem(session)?;
    let module = args
        .get("module")
        .and_then(Value::as_str)
        .unwrap_or("client.dll");
    let m = mem
        .module_by_name(module)
        .ok_or_else(|| anyhow!("module {module:?} not found"))?;
    let bytes = live::read_span(&*mem, m.base, m.size);
    let reader = BytesReader::new(m.base, bytes);

    let found = if let Some(global) = args.get("global").and_then(Value::as_str) {
        interfaces::walk_from_global(&reader, m.base, m.size, live::parse_hex(global)?)?
    } else {
        interfaces::enumerate_all(&reader, m.base, m.size)?
    };

    let list: Vec<Value> = found
        .iter()
        .map(|it| {
            json!({
                "name": it.name,
                "node_rva": format!("0x{:x}", it.rva),
                "create_rva": format!("0x{:x}", it.create_rva),
            })
        })
        .collect();
    Ok(pretty(
        &json!({ "module": module, "count": list.len(), "interfaces": list }),
    ))
}

fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 3);
    for (i, b) in bytes.iter().enumerate() {
        if i != 0 {
            s.push(' ');
        }
        s.push_str(&format!("{b:02x}"));
    }
    s
}

fn pretty(value: &impl serde::Serialize) -> String {
    serde_json::to_string_pretty(value).unwrap_or_else(|_| "null".to_string())
}

/// The tool catalogue advertised via `tools/list`.
fn tools() -> Vec<Value> {
    fn tool(name: &str, description: &str, schema: Value) -> Value {
        json!({ "name": name, "description": description, "inputSchema": schema })
    }
    fn obj(props: Value, required: Vec<&str>) -> Value {
        json!({ "type": "object", "properties": props, "required": required })
    }

    vec![
        tool(
            "list_processes",
            "List running processes (optionally filtered by name).",
            obj(json!({ "name": { "type": "string", "description": "substring to match against the process name" } }), vec![]),
        ),
        tool(
            "attach",
            "Attach to a target process by name or pid. Omit both to attach to the server's own process.",
            obj(json!({
                "process": { "type": "string" },
                "pid": { "type": "integer" }
            }), vec![]),
        ),
        tool("detach", "Release the current target.", obj(json!({}), vec![])),
        tool(
            "modules",
            "List modules (loaded PE/ELF images) of the attached process.",
            obj(json!({ "filter": { "type": "string" } }), vec![]),
        ),
        tool(
            "read",
            "Read memory at an address as hex, or decode a scalar with `type`.",
            obj(json!({
                "address": { "type": "string", "description": "hex (0x...) or decimal" },
                "size": { "type": "integer", "description": "byte count (default 16)" },
                "type": { "type": "string", "description": "u8/i8/u16/i16/u32/i32/u64/i64/f32/f64" }
            }), vec!["address"]),
        ),
        tool(
            "read_cstring",
            "Read a NUL-terminated ASCII string at an address.",
            obj(json!({ "address": { "type": "string" } }), vec!["address"]),
        ),
        tool(
            "resolve_chain",
            "Follow a pointer chain: read_ptr(base), then read_ptr(result+offset) per offset.",
            obj(json!({
                "base": { "type": "string" },
                "offsets": { "type": "array", "items": { "type": "integer" } },
                "deref_first": { "type": "boolean", "description": "dereference base first (default true)" }
            }), vec!["base"]),
        ),
        tool(
            "scan_pattern",
            "Scan for an IDA-style byte pattern inside a module or an address range.",
            obj(json!({
                "pattern": { "type": "string", "description": "e.g. \"48 8B 05 ?? ?? ?? ??\"" },
                "module": { "type": "string" },
                "address": { "type": "string" },
                "size": { "type": "integer" },
                "limit": { "type": "integer" }
            }), vec!["pattern"]),
        ),
        tool(
            "scan_value",
            "Scan writable memory for a scalar value; starts a narrowing session.",
            obj(json!({
                "value": { "type": "number" },
                "type": { "type": "string", "description": "default u32" },
                "writable_only": { "type": "boolean", "description": "default true" },
                "limit": { "type": "integer" },
                "max_bytes": { "type": "integer" }
            }), vec!["value"]),
        ),
        tool(
            "scan_next",
            "Narrow the active scan: keep candidates equal to `value` and/or changed/unchanged.",
            obj(json!({
                "value": { "type": "number" },
                "changed": { "type": "boolean" }
            }), vec![]),
        ),
        tool(
            "interfaces",
            "Enumerate CreateInterface registrations in a module.",
            obj(json!({
                "module": { "type": "string", "description": "default client.dll" },
                "global": { "type": "string", "description": "head global address (optional)" }
            }), vec![]),
        ),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn call(session: &mut Session, id: u64, name: &str, args: Value) -> Value {
        handle(
            session,
            &json!({ "jsonrpc": "2.0", "id": id, "method": "tools/call",
                     "params": { "name": name, "arguments": args } }),
        )
        .unwrap()
    }

    #[test]
    fn initialize_negotiates_protocol_version() {
        let mut session = Session::default();
        let resp = handle(
            &mut session,
            &json!({ "jsonrpc": "2.0", "id": 1, "method": "initialize",
                     "params": { "protocolVersion": "2025-06-18", "capabilities": {} } }),
        )
        .unwrap();
        assert_eq!(resp["result"]["protocolVersion"], "2025-06-18");
        assert!(resp["result"]["capabilities"]["tools"].is_object());
        assert_eq!(resp["result"]["serverInfo"]["name"], SERVER_NAME);
    }

    #[test]
    fn notifications_get_no_response() {
        let mut session = Session::default();
        assert!(handle(
            &mut session,
            &json!({ "jsonrpc": "2.0", "method": "notifications/initialized" })
        )
        .is_none());
    }

    #[test]
    fn tools_list_is_well_formed() {
        let mut session = Session::default();
        let resp = handle(
            &mut session,
            &json!({ "jsonrpc": "2.0", "id": 2, "method": "tools/list" }),
        )
        .unwrap();
        let tools = resp["result"]["tools"].as_array().unwrap();
        for name in [
            "attach",
            "read",
            "scan_value",
            "scan_next",
            "resolve_chain",
            "interfaces",
        ] {
            let tool = tools
                .iter()
                .find(|t| t["name"] == name)
                .unwrap_or_else(|| panic!("missing {name}"));
            assert_eq!(tool["inputSchema"]["type"], "object");
        }
    }

    #[test]
    fn unknown_tool_is_reported_as_error() {
        let mut session = Session::default();
        let resp = call(&mut session, 3, "nope", json!({}));
        assert_eq!(resp["result"]["isError"], true);
    }

    #[test]
    fn attach_self_then_typed_read() {
        static MAGIC: std::sync::atomic::AtomicU64 =
            std::sync::atomic::AtomicU64::new(0x1122_3344_5566_7788);
        let mut session = Session::default();

        let attached = call(&mut session, 1, "attach", json!({}));
        assert_eq!(attached["result"]["isError"], false);

        let addr = format!("0x{:x}", std::ptr::addr_of!(MAGIC) as usize);
        let read = call(
            &mut session,
            2,
            "read",
            json!({ "address": addr, "type": "u64" }),
        );
        let text = read["result"]["content"][0]["text"].as_str().unwrap();
        assert!(text.contains("0x1122334455667788"), "got: {text}");
    }
}
