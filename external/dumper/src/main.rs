use anyhow::{anyhow, Context, Result};
use clap::Parser;

use cs2_dumper::interfaces;
use cs2_dumper::memory::{find_pid, LinuxProcMemory, ProcessMemory};
use cs2_dumper::output::{self, DumpManifest};
use cs2_dumper::pattern::Pattern;
use cs2_dumper::pe::{self, PeInfo};
use cs2_dumper::reader::BytesReader;
use cs2_dumper::schema::{self, SchemaLayout};
use cs2_dumper::{analysis, sdk};

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

    /// Module to scan / inspect (default client.dll).
    #[arg(long, default_value = "client.dll")]
    module: String,

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
}

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

    // SDK generation from JSON dumps — no process required.
    if let Some(dir) = &cli.gen {
        return generate_sdk(&cli, dir);
    }

    // File mode: analyze a PE on disk.
    if let Some(path) = &cli.file {
        let bytes = std::fs::read(path).with_context(|| format!("reading {path}"))?;
        let info = pe::parse_image(&bytes).with_context(|| format!("parsing PE {path}"))?;
        let base = info.image_base as usize;
        let name = std::path::Path::new(path)
            .file_name()
            .map(|f| f.to_string_lossy().to_string())
            .unwrap_or_else(|| path.clone());
        println!(
            "file {name} — image_base=0x{base:x} size_of_image=0x{:x} ({} exports)",
            info.size_of_image,
            info.exports.len()
        );
        // Map to RVA addressing so stored pointers match RVAs.
        let mapped = pe::map_file_to_rva(&bytes, &info);
        return analyze(&cli, &name, &mapped, base, Some(&info));
    }

    // Live process mode.
    let pid = match cli.pid {
        Some(pid) => pid,
        None => find_pid(&cli.process)
            .ok_or_else(|| anyhow!("process {:?} not found; is the client running?", cli.process))?,
    };
    let mem = LinuxProcMemory::open(pid).with_context(|| format!("opening memory of pid {pid}"))?;
    let modules = mem.modules();

    if cli.json && cli.pattern.is_none() && !cli.exports && !cli.interfaces && !cli.schema {
        let manifest = DumpManifest::new(&cli.process, pid, &modules);
        println!("{}", output::to_json(&manifest));
        return Ok(());
    }

    println!("process {:?} (pid {pid}) — {} modules", cli.process, modules.len());
    for m in &modules {
        println!("  {:<28} base=0x{:012x} size=0x{:x}", m.name, m.base, m.size);
    }

    if cli.pattern.is_some() || cli.exports || cli.interfaces {
        let module = mem
            .module_by_name(&cli.module)
            .ok_or_else(|| anyhow!("module {:?} not found", cli.module))?;
        let bytes = read_module(&mem, module.base, module.size);
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
        let classes = schema::dump(&mem, schema_system, &layout)?;
        if cli.json {
            println!("{}", serde_json::to_string_pretty(&classes)?);
            return Ok(());
        }
        println!("\nschema — {} class(es)", classes.len());
        for c in &classes {
            println!("  {:<24} {} ({} fields, size 0x{:x})", c.module, c.name, c.fields.len(), c.size);
            for f in &c.fields {
                println!("      +0x{:<4x} {:<32} {}", f.offset, f.name, f.type_name);
            }
        }
    }

    Ok(())
}

/// Shared analysis over an in-memory module image.
fn analyze(cli: &Cli, module_name: &str, bytes: &[u8], base: usize, info: Option<&PeInfo>) -> Result<()> {
    if let Some(pattern_text) = &cli.pattern {
        let pattern = Pattern::parse(pattern_text).context("parsing pattern")?;
        let matches = pattern.find_all(bytes);
        println!("\npattern {:?} in {module_name} — {} match(es)", pattern_text, matches.len());
        for hit in matches.iter().take(16) {
            println!("  0x{:012x}  {module_name} + 0x{hit:x}", base + hit);
        }
    }

    if cli.exports {
        match info {
            Some(info) => {
                println!("\n{module_name} — {} exports", info.exports.len());
                for e in info.exports.iter().filter(|e| e.name.contains("CreateInterface")) {
                    println!("  {:<32} rva=0x{:x}", e.name, e.rva);
                }
            }
            None => eprintln!("PE parse unavailable for {module_name}"),
        }
    }

    if cli.interfaces {
        let reader = BytesReader::new(base, bytes.to_vec());
        let found = if let Some(global_text) = &cli.interfaces_global {
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
        if cli.json {
            println!("{}", serde_json::to_string_pretty(&found)?);
        } else {
            println!("\n{module_name} — {} interface(s)", found.len());
            for it in &found {
                println!("  {:<40} rva=0x{:x} create=0x{:x}", it.name, it.rva, it.create_rva);
            }
        }
    }

    Ok(())
}

/// Generate C++ SDK headers from JSON dumps on disk.
fn generate_sdk(cli: &Cli, dir: &str) -> Result<()> {
    if cli.interfaces_json.is_none() && cli.schema_json.is_none() && cli.offsets_json.is_none() {
        return Err(anyhow!(
            "--gen needs at least one input: --interfaces-json / --schema-json / --offsets-json"
        ));
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

/// Read a module range tolerantly: unmapped gaps become zeros rather than errors.
fn read_module(mem: &impl ProcessMemory, base: usize, size: usize) -> Vec<u8> {
    const CHUNK: usize = 0x1000;
    let mut out = vec![0u8; size];
    let mut off = 0;
    while off < size {
        let n = CHUNK.min(size - off);
        let _ = mem.read(base + off, &mut out[off..off + n]);
        off += n;
    }
    out
}

fn parse_hex(text: &str) -> Result<usize> {
    let t = text.trim();
    let t = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")).unwrap_or(t);
    usize::from_str_radix(t, 16).with_context(|| format!("invalid hex address: {text:?}"))
}
