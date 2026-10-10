use anyhow::{anyhow, Context, Result};
use clap::Parser;

use cs2_dumper::memory::{find_pid, LinuxProcMemory, ProcessMemory};
use cs2_dumper::output::{self, DumpManifest};
use cs2_dumper::pattern::Pattern;
use cs2_dumper::pe;

/// External offset/interface dumper.
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

    /// Emit JSON instead of human-readable text.
    #[arg(long)]
    json: bool,
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    let pid = match cli.pid {
        Some(pid) => pid,
        None => find_pid(&cli.process).ok_or_else(|| {
            anyhow!(
                "process {:?} not found; is the client running?",
                cli.process
            )
        })?,
    };

    let mem = LinuxProcMemory::open(pid)
        .with_context(|| format!("opening memory of pid {pid}"))?;
    let modules = mem.modules();

    if cli.json && cli.pattern.is_none() && !cli.exports {
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
            let abs = module.base + hit;
            println!(
                "  0x{abs:012x}  {} + 0x{hit:x}",
                module.name
            );
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
