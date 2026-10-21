use anyhow::{anyhow, Context, Result};
use clap::Parser;

use cs2_dumper::analysis;
use cs2_dumper::interfaces;
use cs2_dumper::live;
use cs2_dumper::memory::{find_pid, ProcessMemory};
#[cfg(unix)]
use cs2_dumper::memory::LinuxProcMemory;
#[cfg(windows)]
use cs2_dumper::memory::WindowsMemory;
use cs2_dumper::output::{self, DumpManifest};
use cs2_dumper::pattern::Pattern;
use cs2_dumper::pe::{self, PeInfo};
use cs2_dumper::reader::BytesReader;
use cs2_dumper::rename;
use cs2_dumper::schema;
use cs2_dumper::schema::SchemaLayout;
use cs2_dumper::sdk;

/// The platform live-memory backend. Wine/Proton is read through
/// `/proc/<pid>/mem`; native Windows through `ReadProcessMemory`.
#[cfg(unix)]
type LiveMemory = LinuxProcMemory;
#[cfg(windows)]
type LiveMemory = WindowsMemory;

/// External offset/interface/schema dumper.
#[derive(Parser, Debug)]
#[command(name = "cs2-dumper", version, about)]
struct Cli {
    /// Analyze a PE file on disk (no process needed).
    #[arg(long)]
    file: Option<String>,

    /// Target process name (the Proton/Wine client).
    #[arg(long, default_value = "cs2.exe")]
    process: String,

    /// Explicit PID (overrides --process discovery).
    #[arg(long)]
    pid: Option<u32>,

    /// Scan a signature (IDA-style, e.g. "48 8B 05 ?? ?? ?? ??") in a module.
    #[arg(long)]
    pattern: Option<String>,

    /// Module to scan / inspect.
    ///
    /// In the file mode it names the module the curated signatures are
    /// looked up under (default: the file's own name); in the live mode it
    /// names the module in the process (default: client.dll).
    #[arg(long)]
    module: Option<String>,

    /// List the module's exported names (e.g. to find CreateInterface registrations).
    #[arg(long)]
    exports: bool,

    /// Enumerate `CreateInterface` registrations in the module.
    #[arg(long)]
    interfaces: bool,

    /// Extra interface-name seeds (repeatable) for --interfaces discovery.
    #[arg(long = "interface-seed")]
    interface_seeds: Vec<String>,

    /// Walk the CreateInterface registration list from a known head address (hex).
    #[arg(long)]
    interfaces_head: Option<String>,

    /// Walk from a module-local head *global* (hex); dereferences it first.
    #[arg(long)]
    interfaces_global: Option<String>,

    /// Dump the schema system. Requires --schema-system.
    #[arg(long)]
    schema: bool,

    /// Address of the CSchemaSystem (hex, e.g. 0x7ff6...).
    #[arg(long)]
    schema_system: Option<String>,

    /// Resolve the curated global offsets by scanning their signatures in the
    /// module (see docs/re/client-dll.md) and print the RVAs.
    #[arg(long)]
    resolve_offsets: bool,

    /// Print the modules that have curated signatures (a JSON array) and exit.
    ///
    /// The list comes from the signature registry itself, so a signature added
    /// for a new module shows up without an edit here. The offline offset
    /// resolver (scripts/resolve-offsets-offline.py) drives it.
    #[arg(long)]
    list_signature_modules: bool,

    /// Print how many curated signatures the registry holds per module (a
    /// JSON object) and exit.
    ///
    /// The offline offset resolver compares the number of signatures a module
    /// resolved against this count, so a module that was not really scanned
    /// (a wrong file, a module name that does not match) is a shortfall and
    /// not a silent success.
    #[arg(long)]
    list_signature_counts: bool,

    /// Read raw bytes at an address (hex) and print them as hex.
    #[arg(long)]
    read: Option<String>,

    /// Dereference a pointer at an address (hex) and print the pointed-to address.
    #[arg(long)]
    read_ptr: Option<String>,

    /// Number of bytes for --read (default 16).
    #[arg(long, default_value_t = 16)]
    len: usize,

    /// Emit JSON instead of human-readable text.
    #[arg(long)]
    json: bool,

    /// Generate C++ SDK headers into DIR (default: payload/sdk) from JSON dumps.
    #[arg(long, value_name = "DIR", num_args = 0..=1, default_missing_value = "payload/sdk")]
    gen: Option<String>,

    /// Interfaces JSON (input for --gen), as emitted by `--interfaces --json`.
    #[arg(long, value_name = "FILE")]
    interfaces_json: Option<String>,

    /// Schema JSON (input for --gen), as emitted by `--schema --json`.
    #[arg(long, value_name = "FILE")]
    schema_json: Option<String>,

    /// Offsets JSON (input for --gen).
    #[arg(long, value_name = "FILE")]
    offsets_json: Option<String>,

    /// Run as an MCP (Model Context Protocol) server over stdio for live analysis.
    #[arg(long)]
    mcp: bool,
}

/// The module the live mode looks at when --module is not given.
const DEFAULT_MODULE: &str = "client.dll";

/// Common interfaces used as discovery seeds.
const DEFAULT_INTERFACE_SEEDS: &[&str] = &[
    "SchemaSystem_001",
    "Source2Client002",
    "Source2ClientPrediction001",
    "Source2EngineToClient001",
    "GameClientExports001",
    "EngineServiceMgr001",
    "GameEventSystemClientV001",
    "Source2ClientUI001",
    "ClientToolsInfo_001",
    "Source2ClientConfig001",
];

fn main() -> Result<()> {
    let cli = Cli::parse();

    // Live-analysis MCP server over stdio (Unix hosts only; it reads /proc).
    if cli.mcp {
        #[cfg(unix)]
        return cs2_dumper::mcp::serve();
        #[cfg(not(unix))]
        return Err(anyhow!(
            "--mcp (live analysis) requires a Unix host; it reads /proc"
        ));
    }

    // The modules that have curated signatures: printed and exited (unlike
    // --exports, which is handled inside analyze()). It needs neither a
    // process nor a file — the list comes from the signature registry.
    if cli.list_signature_modules {
        println!(
            "{}",
            serde_json::to_string_pretty(&analysis::signature_modules())?
        );
        return Ok(());
    }

    // How many curated signatures the registry holds per module: the
    // cross-check the offline offset resolver does against a resolution.
    if cli.list_signature_counts {
        println!(
            "{}",
            serde_json::to_string_pretty(&analysis::signature_counts())?
        );
        return Ok(());
    }

    // SDK generation from JSON dumps — no process required.
    if let Some(dir) = &cli.gen {
        return generate_sdk(&cli, dir);
    }

    // File mode: analyze a PE on disk.
    if let Some(path) = &cli.file {
        let bytes = std::fs::read(path).with_context(|| format!("reading {path}"))?;
        let info = pe::parse_image(&bytes).with_context(|| format!("parsing PE {path}"))?;
        let base = info.image_base as usize;
        let file_name = std::path::Path::new(path)
            .file_name()
            .map(|f| f.to_string_lossy().to_string())
            .unwrap_or_else(|| path.clone());
        // The module the curated signatures are looked up under: the file's
        // own name, unless --module names one. A name that does not match
        // the file is an **error**: it would otherwise resolve nothing at
        // all and say so quietly. Without --module the file's name is
        // matched against the registry (case-insensitively), so a file that
        // is spelled "Client.dll" still resolves the registry's
        // "client.dll" signatures.
        let module = match cli.module.as_deref() {
            Some(name) if !name.eq_ignore_ascii_case(&file_name) => {
                return Err(anyhow!(
                    "--module {name:?} does not match the file's name {file_name:?}"
                ));
            }
            Some(name) => name.to_string(),
            None => analysis::signature_modules()
                .into_iter()
                .find(|known| known.eq_ignore_ascii_case(&file_name))
                .unwrap_or(file_name.clone()),
        };
        println!(
            "file {file_name} — image_base=0x{base:x} size_of_image=0x{:x} ({} exports)",
            info.size_of_image,
            info.exports.len()
        );
        // Map to RVA addressing so stored pointers match RVAs.
        let mapped = pe::map_file_to_rva(&bytes, &info);
        return analyze(&cli, &module, &mapped, base, Some(&info));
    }

    // Live process mode: attach to the target and read it in-process. Under
    // Wine/Proton this is `/proc/<pid>/mem`; on native Windows it is
    // `ReadProcessMemory` (see `memory::WindowsMemory`).
    let pid = match cli.pid {
        Some(pid) => pid,
        None => find_pid(&cli.process).ok_or_else(|| {
            anyhow!(
                "process {:?} not found; is the client running?",
                cli.process
            )
        })?,
    };
    let mem = LiveMemory::open(pid).with_context(|| format!("opening memory of pid {pid}"))?;
    let modules = mem.modules();

    if let Some(addr_text) = &cli.read_ptr {
        let addr = parse_hex(addr_text)?;
        let bytes = mem.read_vec(addr, 8).map_err(|e| anyhow!(e.to_string()))?;
        println!("0x{:x}", u64::from_le_bytes(bytes[..8].try_into().unwrap()));
        return Ok(());
    }
    if let Some(addr_text) = &cli.read {
        let addr = parse_hex(addr_text)?;
        let bytes = mem
            .read_vec(addr, cli.len)
            .map_err(|e| anyhow!(e.to_string()))?;
        let hex: Vec<String> = bytes.iter().map(|b| format!("{b:02x}")).collect();
        println!("{}", hex.join(" "));
        return Ok(());
    }

    if cli.json
        && cli.pattern.is_none()
        && !cli.exports
        && !cli.interfaces
        && !cli.schema
        && !cli.resolve_offsets
    {
        let manifest = DumpManifest::new(&cli.process, pid, &modules);
        println!("{}", output::to_json(&manifest));
        return Ok(());
    }

    if !cli.json {
        println!(
            "process {:?} (pid {pid}) — {} modules",
            cli.process,
            modules.len()
        );
        for m in &modules {
            println!(
                "  {:<28} base=0x{:012x} size=0x{:x}",
                m.name, m.base, m.size
            );
        }
    }

    if cli.pattern.is_some() || cli.exports || cli.interfaces || cli.resolve_offsets {
        let wanted = cli.module.as_deref().unwrap_or(DEFAULT_MODULE);
        let module = mem
            .module_by_name(wanted)
            .ok_or_else(|| anyhow!("module {:?} not found", wanted))?;
        let bytes = live::read_module_image(&mem, module.base, module.size);
        let info = pe::parse_image(&bytes).ok();
        return analyze(&cli, &module.name, &bytes, module.base, info.as_ref());
    }

    if cli.schema {
        let schema_system = parse_hex(
            cli.schema_system
                .as_deref()
                .ok_or_else(|| anyhow!("--schema requires --schema-system <hex>"))?,
        )?;
        let layout = SchemaLayout::default();
        let mut classes = schema::dump(&mem, schema_system, &layout)?;
        rename::apply_schema(&mut classes);
        if cli.json {
            println!("{}", serde_json::to_string_pretty(&classes)?);
            return Ok(());
        }
        println!("\nschema — {} class(es)", classes.len());
        for c in &classes {
            println!(
                "  {:<24} {} ({} fields, size 0x{:x})",
                c.module,
                c.name,
                c.fields.len(),
                c.size
            );
            for f in &c.fields {
                println!("      +0x{:<4x} {:<32} {}", f.offset, f.name, f.type_name);
            }
        }
    }

    Ok(())
}

/// Shared analysis over an in-memory module image.
fn analyze(
    cli: &Cli,
    module_name: &str,
    bytes: &[u8],
    base: usize,
    info: Option<&PeInfo>,
) -> Result<()> {
    if let Some(pattern_text) = &cli.pattern {
        let pattern = Pattern::parse(pattern_text).context("parsing pattern")?;
        let matches = pattern.find_all(bytes);
        println!(
            "\npattern {:?} in {module_name} — {} match(es)",
            pattern_text,
            matches.len()
        );
        for hit in matches.iter().take(16) {
            println!("  0x{:012x}  {module_name} + 0x{hit:x}", base + hit);
        }
    }

    if cli.exports {
        match info {
            Some(info) => {
                println!("\n{module_name} — {} exports", info.exports.len());
                for e in info
                    .exports
                    .iter()
                    .filter(|e| e.name.contains("CreateInterface"))
                {
                    println!("  {:<32} rva=0x{:x}", e.name, e.rva);
                }
            }
            None => eprintln!("PE parse unavailable for {module_name}"),
        }
    }

    if cli.interfaces {
        let reader = BytesReader::new(base, bytes.to_vec());
        let mut found = if let Some(global_text) = &cli.interfaces_global {
            let global = parse_hex(global_text)?;
            interfaces::walk_from_global(&reader, base, bytes.len(), global)?
        } else if let Some(head_text) = &cli.interfaces_head {
            let head = parse_hex(head_text)?;
            interfaces::walk_list(&reader, base, bytes.len(), head)
        } else if cli.interface_seeds.is_empty() {
            interfaces::enumerate_all(&reader, base, bytes.len())?
        } else {
            let mut seeds: Vec<&str> = DEFAULT_INTERFACE_SEEDS.to_vec();
            seeds.extend(cli.interface_seeds.iter().map(String::as_str));
            interfaces::enumerate(&reader, base, bytes.len(), &seeds)?
        };
        rename::apply_interfaces(&mut found);
        if cli.json {
            println!("{}", serde_json::to_string_pretty(&found)?);
        } else {
            println!("\n{module_name} — {} interface(s)", found.len());
            for it in &found {
                println!(
                    "  {:<40} rva=0x{:x} create=0x{:x}",
                    it.name, it.rva, it.create_rva
                );
            }
        }
    }

    if cli.resolve_offsets {
        let (offsets, missing) = analysis::resolve_known(module_name, bytes, base);
        // What was looked at, and how much of it there is. The offline
        // resolver compares "resolved + unresolved" against the registry's
        // count, so a module that was not really scanned (a wrong file, a
        // name that does not match) is a shortfall and not a silent
        // success.
        eprintln!("  module: {module_name}");
        eprintln!("  signatures: {}", analysis::signature_count(module_name));
        if cli.json {
            println!("{}", serde_json::to_string_pretty(&offsets)?);
        } else {
            println!("\n{module_name} — {} offset(s)", offsets.len());
            for off in &offsets {
                println!("  {:<28} rva=0x{:x}", off.name, off.rva);
            }
        }
        // The signatures that did **not** match go to stderr in *both* modes:
        // the JSON mode is the one the calibration scripts use, and a
        // signature that stopped matching has to be visible there too (the
        // scripts report it as "unresolved: …").
        for name in &missing {
            eprintln!("  unresolved: {name}");
        }
    }

    Ok(())
}

/// Generate C++ SDK headers from JSON dumps on disk.
fn generate_sdk(cli: &Cli, dir: &str) -> Result<()> {
    if cli.interfaces_json.is_none() && cli.schema_json.is_none() && cli.offsets_json.is_none() {
        eprintln!(
            "warning: --gen called with no JSON inputs; writing empty SDK headers \
             (pass --interfaces-json/--schema-json/--offsets-json)"
        );
    }

    let interfaces: Vec<interfaces::Interface> = read_json(cli.interfaces_json.as_deref())?;
    let schema: Vec<schema::SchemaClass> = read_json(cli.schema_json.as_deref())?;
    let offsets: Vec<analysis::Offset> = read_json(cli.offsets_json.as_deref())?;

    let inputs = sdk::SdkInputs {
        interfaces: &interfaces,
        schema: &schema,
        offsets: &offsets,
    };
    let written = sdk::write_sdk(std::path::Path::new(dir), &inputs)?;
    println!(
        "wrote {} SDK header(s) to {dir} ({} interfaces, {} classes, {} offsets)",
        written.len(),
        interfaces.len(),
        schema.len(),
        offsets.len()
    );
    for path in &written {
        println!("  {}", path.display());
    }
    Ok(())
}

/// Read and deserialize a JSON file, defaulting to empty when no path is given.
fn read_json<T: serde::de::DeserializeOwned>(path: Option<&str>) -> Result<Vec<T>> {
    match path {
        None => Ok(Vec::new()),
        Some(path) => {
            let text = std::fs::read_to_string(path).with_context(|| format!("reading {path}"))?;
            serde_json::from_str(&text).with_context(|| format!("parsing {path}"))
        }
    }
}

/// Parse `0x`-prefixed hex (or plain decimal) into a usize.
fn parse_hex(text: &str) -> Result<usize> {
    let t = text.trim();
    let t = t
        .strip_prefix("0x")
        .or_else(|| t.strip_prefix("0X"))
        .unwrap_or(t);
    usize::from_str_radix(t, 16).with_context(|| format!("invalid hex address: {text:?}"))
}
