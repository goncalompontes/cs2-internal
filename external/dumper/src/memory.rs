//! Process memory access.
//!
//! [`LinuxProcMemory`] reads a live Linux process (the Proton target) via
//! `/proc/<pid>/mem` + `/proc/<pid>/maps`. The [`ProcessMemory`] trait keeps the
//! analysis code backend-agnostic (a `memflow` backend can slot in later).

use std::collections::BTreeMap;
use std::fs::File;
use std::os::unix::fs::FileExt;
use std::path::PathBuf;

#[derive(Debug, Clone)]
pub struct ModuleInfo {
    pub name: String,
    pub path: PathBuf,
    pub base: usize,
    pub size: usize,
}

/// A `/proc/<pid>/maps` region.
#[derive(Debug, Clone)]
pub struct Region {
    pub start: usize,
    pub end: usize,
    pub read: bool,
    pub write: bool,
    pub execute: bool,
    pub path: String,
}

impl Region {
    pub fn size(&self) -> usize {
        self.end.saturating_sub(self.start)
    }
}

#[derive(Debug)]
pub enum MemoryError {
    Io(std::io::Error),
    NotOpen,
}

impl std::fmt::Display for MemoryError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            MemoryError::Io(e) => write!(f, "io error: {e}"),
            MemoryError::NotOpen => write!(f, "memory not open"),
        }
    }
}

impl std::error::Error for MemoryError {}

impl From<std::io::Error> for MemoryError {
    fn from(e: std::io::Error) -> Self {
        MemoryError::Io(e)
    }
}

pub trait ProcessMemory {
    fn read(&self, addr: usize, buf: &mut [u8]) -> Result<(), MemoryError>;

    fn modules(&self) -> Vec<ModuleInfo>;

    fn regions(&self) -> Vec<Region>;

    fn read_vec(&self, addr: usize, len: usize) -> Result<Vec<u8>, MemoryError> {
        let mut buf = vec![0u8; len];
        self.read(addr, &mut buf)?;
        Ok(buf)
    }

    fn module_by_name(&self, name: &str) -> Option<ModuleInfo> {
        self.modules()
            .into_iter()
            .find(|m| m.name.eq_ignore_ascii_case(name))
    }
}

pub struct LinuxProcMemory {
    pid: u32,
    mem: File,
    modules: Vec<ModuleInfo>,
    regions: Vec<Region>,
}

impl LinuxProcMemory {
    pub fn open(pid: u32) -> Result<Self, MemoryError> {
        let mem = File::open(format!("/proc/{pid}/mem"))?;
        let maps = std::fs::read_to_string(format!("/proc/{pid}/maps"))?;
        Ok(Self {
            pid,
            mem,
            modules: parse_maps(&maps),
            regions: parse_regions(&maps),
        })
    }

    pub fn pid(&self) -> u32 {
        self.pid
    }
}

impl ProcessMemory for LinuxProcMemory {
    fn read(&self, addr: usize, buf: &mut [u8]) -> Result<(), MemoryError> {
        let mut done = 0usize;
        while done < buf.len() {
            let at = (addr + done) as u64;
            let n = self.mem.read_at(&mut buf[done..], at)?;
            if n == 0 {
                break;
            }
            done += n;
        }
        Ok(())
    }

    fn modules(&self) -> Vec<ModuleInfo> {
        self.modules.clone()
    }

    fn regions(&self) -> Vec<Region> {
        self.regions.clone()
    }
}

/// Parse `/proc/<pid>/maps` into per-region entries.
pub fn parse_regions(maps: &str) -> Vec<Region> {
    let mut out = Vec::new();
    for line in maps.lines() {
        // e.g. 7f...-7f... rw-p 00000000 08:01 1234  /path  (path optional)
        let mut fields = line.split_whitespace();
        let Some(range) = fields.next() else { continue };
        let Some(perms) = fields.next() else { continue };
        let Some((start_s, end_s)) = range.split_once('-') else {
            continue;
        };
        let (Ok(start), Ok(end)) = (
            usize::from_str_radix(start_s, 16),
            usize::from_str_radix(end_s, 16),
        ) else {
            continue;
        };
        let mut p = perms.chars();
        let read = p.next() == Some('r');
        let write = p.next() == Some('w');
        let execute = p.next() == Some('x');
        // Remaining fields: offset, dev, inode, then an optional path.
        let path = fields.nth(2).unwrap_or("").to_string();
        out.push(Region {
            start,
            end,
            read,
            write,
            execute,
            path,
        });
    }
    out
}

/// Collapse `/proc/<pid>/maps` into one entry per file-backed module.
fn parse_maps(maps: &str) -> Vec<ModuleInfo> {
    // path -> (min_start, max_end)
    let mut ranges: BTreeMap<PathBuf, (usize, usize)> = BTreeMap::new();

    for line in maps.lines() {
        // e.g. 7f...-7f... r-xp 00000000 08:01 1234  /path/to/client.dll
        let Some((range, rest)) = line.split_once(' ') else {
            continue;
        };
        let Some((start_s, end_s)) = range.split_once('-') else {
            continue;
        };
        // path is the last whitespace-separated field
        let path = rest.split_whitespace().last().unwrap_or("");
        if !is_module_path(path) {
            continue;
        }
        let (Ok(start), Ok(end)) = (
            usize::from_str_radix(start_s, 16),
            usize::from_str_radix(end_s, 16),
        ) else {
            continue;
        };
        let entry = ranges.entry(PathBuf::from(path)).or_insert((start, end));
        entry.0 = entry.0.min(start);
        entry.1 = entry.1.max(end);
    }

    ranges
        .into_iter()
        .map(|(path, (start, end))| {
            let name = path
                .file_name()
                .map(|f| f.to_string_lossy().to_string())
                .unwrap_or_default();
            ModuleInfo {
                name,
                path,
                base: start,
                size: end - start,
            }
        })
        .collect()
}

fn is_module_path(path: &str) -> bool {
    if !path.starts_with('/') {
        return false;
    }
    let lower = path.to_ascii_lowercase();
    lower.ends_with(".dll") || lower.ends_with(".exe") || lower.ends_with(".so")
}

/// Find a PID by process name: matches `/proc/<pid>/comm`, the `exe` basename,
/// or the target's image name in the command line (handles Wine processes).
pub fn find_pid(name: &str) -> Option<u32> {
    let entries = std::fs::read_dir("/proc").ok()?;
    for entry in entries.flatten() {
        let Ok(pid) = entry.file_name().to_string_lossy().parse::<u32>() else {
            continue;
        };
        if let Ok(comm) = std::fs::read_to_string(format!("/proc/{pid}/comm")) {
            if comm.trim().eq_ignore_ascii_case(name) {
                return Some(pid);
            }
        }
        if let Ok(exe) = std::fs::read_link(format!("/proc/{pid}/exe")) {
            if exe
                .file_name()
                .map(|f| f.to_string_lossy().eq_ignore_ascii_case(name))
                .unwrap_or(false)
            {
                return Some(pid);
            }
        }
        if let Ok(cmdline) = std::fs::read(format!("/proc/{pid}/cmdline")) {
            let cmdline = String::from_utf8_lossy(&cmdline);
            if cmdline.split('\0').any(|a| a.eq_ignore_ascii_case(name)) {
                return Some(pid);
            }
        }
    }
    None
}
