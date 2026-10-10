use anyhow::{anyhow, Context, Result};
use clap::Parser;

use cs2_dumper::interfaces;
use cs2_dumper::memory::{find_pid, LinuxProcMemory, ProcessMemory};
use cs2_dumper::output::{self, DumpManifest};
use cs2_dumper::pattern::Pattern;
use cs2_dumper::pe;
use cs2_dumper::schema::{self, SchemaLayout};

/// External offset/interface/schema dumper.
#[derive(Parser, Debug)]
#[command(name = "cs2-dumper", version, about)]
struct Cli {
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

    /// Dump the schema system. Requires --schema-system.
    #[arg(long)]
    schema: bool,

    /// Address of the CSchemaSystem (hex, e.g. 0x7ff6...).
    #[arg(long)]
    schema_system: Option<String>,

    /// Emit JSON instead of human-readable text.
    #[arg(long)]
    json: bool,
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

    if let Some(pattern_text) = &cli.pattern {
        let pattern = Pattern::parse(pattern_text).context("parsing pattern")?;
        let module = mem
            .module_by_name(&cli.module)
            .ok_or_else(|| anyhow!("module {:?} not found", cli.module))?;
        let bytes = read_module(&mem, module.base, module.size);
        let matches = pattern.find_all(&bytes);
        println!(
            "\npattern {:?} in {} — {} match(es)",
            pattern_text,
            module.name,
            matches.len()
        );
        for hit in matches.iter().take(16) {
            println!("  0x{:012x}  {} + 0x{hit:x}", module.base + hit, module.name);
        }
    }

    if cli.exports {
        let module = mem
            .module_by_name(&cli.module)
            .ok_or_else(|| anyhow!("module {:?} not found", cli.module))?;
        let bytes = read_module(&mem, module.base, module.size);
        match pe::parse_image(&bytes) {
            Ok(info) => {
                println!(
                    "\n{} — image_base=0x{:x} size_of_image=0x{:x} ({} exports)",
                    module.name,
                    info.image_base,
                    info.size_of_image,
                    info.exports.len()
                );
                for e in info.exports.iter().filter(|e| e.name.contains("CreateInterface")) {
                    println!("  {:<32} rva=0x{:x}", e.name, e.rva);
                }
            }
            Err(err) => eprintln!("PE parse failed for {}: {err:#}", module.name),
        }
    }

    if cli.interfaces {
        let module = mem
            .module_by_name(&cli.module)
            .ok_or_else(|| anyhow!("module {:?} not found", cli.module))?;
        let mut seeds: Vec<&str> = DEFAULT_INTERFACE_SEEDS.to_vec();
        seeds.extend(cli.interface_seeds.iter().map(String::as_str));
        let found = interfaces::enumerate(&mem, module.base, module.size, &seeds)?;
        println!("\n{} — {} interface(s)", module.name, found.len());
        for it in &found {
            println!("  {:<40} rva=0x{:x} create=0x{:x}", it.name, it.rva, it.create_rva);
        }
    }

    if cli.schema {
        let schema_system = parse_hex(
            cli.schema_system
                .as_deref()
                .ok_or_else(|| anyhow!("--schema requires --schema-system <hex>"))?,
        )?;
        let layout = SchemaLayout::default();
        let classes = schema::dump(&mem, schema_system, &layout)?;
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
