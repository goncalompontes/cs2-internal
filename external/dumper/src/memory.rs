//! Process memory access.
//!
//! [`LinuxProcMemory`] reads a live Linux process (the Proton target) via
//! `/proc/<pid>/mem` + `/proc/<pid>/maps`. The [`ProcessMemory`] trait keeps the
//! analysis code backend-agnostic (a `memflow` backend can slot in later).

use std::collections::BTreeMap;
#[cfg(unix)]
use std::fs::File;
#[cfg(unix)]
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

#[cfg(unix)]
pub struct LinuxProcMemory {
    pid: u32,
    mem: File,
    modules: Vec<ModuleInfo>,
    regions: Vec<Region>,
}

#[cfg(unix)]
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

#[cfg(unix)]
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
        let parts: Vec<&str> = line.splitn(6, ' ').collect();
        if parts.len() < 5 {
            continue;
        }
        let Some((start_s, end_s)) = parts[0].split_once('-') else {
            continue;
        };
        let (Ok(start), Ok(end)) = (
            usize::from_str_radix(start_s, 16),
            usize::from_str_radix(end_s, 16),
        ) else {
            continue;
        };
        let perms = parts[1];
        let mut p = perms.chars();
        let read = p.next() == Some('r');
        let write = p.next() == Some('w');
        let execute = p.next() == Some('x');
        let path = parts
            .get(5)
            .map(|s| s.trim_start())
            .unwrap_or("")
            .to_string();
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
#[cfg_attr(not(unix), allow(dead_code))]
fn parse_maps(maps: &str) -> Vec<ModuleInfo> {
    // path -> (min_start, max_end)
    let mut ranges: BTreeMap<PathBuf, (usize, usize)> = BTreeMap::new();

    for line in maps.lines() {
        // e.g. 7f...-7f... r-xp 00000000 08:01 1234  /path/to/client.dll
        // Split on the first 5 spaces; the remainder is the path, which may
        // itself contain spaces (e.g. "Counter-Strike Global Offensive").
        let parts: Vec<&str> = line.splitn(6, ' ').collect();
        if parts.len() < 6 {
            continue;
        }
        let Some((start_s, end_s)) = parts[0].split_once('-') else {
            continue;
        };
        let path = parts[5].trim_start();
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

#[cfg_attr(not(unix), allow(dead_code))]
fn is_module_path(path: &str) -> bool {
    if !path.starts_with('/') {
        return false;
    }
    let lower = path.to_ascii_lowercase();
    lower.ends_with(".dll") || lower.ends_with(".exe") || lower.ends_with(".so")
}

/// Find a PID by process name: matches `/proc/<pid>/comm`, the `exe` basename,
/// or any command-line argument's basename (handles Wine processes, whose comm
/// is a thread name and whose argv uses Windows-style paths). When several
/// processes match (e.g. Steam's `reaper` wrapper), the one that actually maps
/// the named image is preferred.
#[cfg(unix)]
pub fn find_pid(name: &str) -> Option<u32> {
    let entries = std::fs::read_dir("/proc").ok()?;
    let mut fallback = None;
    for entry in entries.flatten() {
        let Ok(pid) = entry.file_name().to_string_lossy().parse::<u32>() else {
            continue;
        };
        let comm_match = std::fs::read_to_string(format!("/proc/{pid}/comm"))
            .map(|comm| comm.trim().eq_ignore_ascii_case(name))
            .unwrap_or(false);
        let exe_match = std::fs::read_link(format!("/proc/{pid}/exe"))
            .map(|exe| {
                exe.file_name()
                    .is_some_and(|f| f.to_string_lossy().eq_ignore_ascii_case(name))
            })
            .unwrap_or(false);
        let cmd_match = std::fs::read(format!("/proc/{pid}/cmdline"))
            .map(|cmdline| {
                String::from_utf8_lossy(&cmdline)
                    .split('\0')
                    .any(|arg| basename(arg).eq_ignore_ascii_case(name))
            })
            .unwrap_or(false);

        if !(comm_match || exe_match || cmd_match) {
            continue;
        }
        if maps_image(pid, name) {
            return Some(pid);
        }
        fallback.get_or_insert(pid);
    }
    fallback
}

/// Last path component, splitting on both Unix and Windows separators.
#[cfg_attr(not(unix), allow(dead_code))]
fn basename(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

/// Whether `/proc/<pid>/maps` maps a file whose basename is `name`.
#[cfg(unix)]
fn maps_image(pid: u32, name: &str) -> bool {
    let Ok(maps) = std::fs::read_to_string(format!("/proc/{pid}/maps")) else {
        return false;
    };
    maps.lines().any(|line| {
        line.splitn(6, ' ')
            .nth(5)
            .is_some_and(|path| basename(path.trim()).eq_ignore_ascii_case(name))
    })
}

#[cfg(windows)]
mod win {
    //! Minimal Win32 FFI (kernel32) for live process access. Hand-rolled so the
    //! dumper needs no extra crate; covers exactly the calls the backend uses.
    #![allow(non_snake_case, non_camel_case_types, dead_code)]

    pub type Handle = *mut core::ffi::c_void;
    pub type Bool = i32;
    pub type Dword = u32;
    pub type SizeT = usize;
    pub type Lpvoid = *mut core::ffi::c_void;
    pub type Lpcvoid = *const core::ffi::c_void;

    #[repr(C)]
    pub struct MemoryBasicInformation {
        pub BaseAddress: Lpvoid,
        pub AllocationBase: Lpvoid,
        pub AllocationProtect: Dword,
        pub PartitionId: u16,
        pub RegionSize: SizeT,
        pub State: Dword,
        pub Protect: Dword,
        pub Type: Dword,
    }

    #[repr(C)]
    pub struct ProcessEntry32W {
        pub dwSize: Dword,
        pub cntUsage: Dword,
        pub th32ProcessID: Dword,
        pub th32DefaultHeapID: usize,
        pub th32ModuleID: Dword,
        pub cntThreads: Dword,
        pub th32ParentProcessID: Dword,
        pub pcPriClassBase: i32,
        pub dwFlags: Dword,
        pub szExeFile: [u16; 260],
    }

    #[repr(C)]
    pub struct ModuleEntry32W {
        pub dwSize: Dword,
        pub th32ModuleID: Dword,
        pub th32ProcessID: Dword,
        pub GlblcntUsage: Dword,
        pub ProccntUsage: Dword,
        pub modBaseAddr: *mut u8,
        pub modBaseSize: Dword,
        pub hModule: Handle,
        pub szModule: [u16; 256],
        pub szExePath: [u16; 260],
    }

    pub const PROCESS_VM_READ: Dword = 0x0010;
    pub const PROCESS_QUERY_INFORMATION: Dword = 0x0400;
    pub const PROCESS_QUERY_LIMITED_INFORMATION: Dword = 0x1000;
    pub const TH32CS_SNAPPROCESS: Dword = 0x0000_0002;
    pub const TH32CS_SNAPMODULE: Dword = 0x0000_0008;
    pub const TH32CS_SNAPMODULE32: Dword = 0x0000_0010;
    pub const MEM_COMMIT: Dword = 0x1000;
    pub const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;

    // Page protection values (low byte of `Protect`).
    pub const PAGE_READONLY: Dword = 0x02;
    pub const PAGE_READWRITE: Dword = 0x04;
    pub const PAGE_WRITECOPY: Dword = 0x08;
    pub const PAGE_EXECUTE: Dword = 0x10;
    pub const PAGE_EXECUTE_READ: Dword = 0x20;
    pub const PAGE_EXECUTE_READWRITE: Dword = 0x40;
    pub const PAGE_EXECUTE_WRITECOPY: Dword = 0x80;

    #[link(name = "kernel32")]
    extern "system" {
        pub fn OpenProcess(dwDesiredAccess: Dword, bInheritHandle: Bool, dwProcessId: Dword)
            -> Handle;
        pub fn CloseHandle(hObject: Handle) -> Bool;
        pub fn ReadProcessMemory(
            hProcess: Handle,
            lpBaseAddress: Lpcvoid,
            lpBuffer: Lpvoid,
            nSize: SizeT,
            lpNumberOfBytesRead: *mut SizeT,
        ) -> Bool;
        pub fn VirtualQueryEx(
            hProcess: Handle,
            lpAddress: Lpcvoid,
            lpBuffer: *mut MemoryBasicInformation,
            dwLength: SizeT,
        ) -> SizeT;
        pub fn CreateToolhelp32Snapshot(dwFlags: Dword, th32ProcessID: Dword) -> Handle;
        pub fn Process32FirstW(hSnapshot: Handle, lppe: *mut ProcessEntry32W) -> Bool;
        pub fn Process32NextW(hSnapshot: Handle, lppe: *mut ProcessEntry32W) -> Bool;
        pub fn Module32FirstW(hSnapshot: Handle, lpme: *mut ModuleEntry32W) -> Bool;
        pub fn Module32NextW(hSnapshot: Handle, lpme: *mut ModuleEntry32W) -> Bool;
    }
}

/// A live Windows process, read through `ReadProcessMemory` /
/// `VirtualQueryEx` / the toolhelp snapshots. The Windows counterpart of
/// [`LinuxProcMemory`] (native Windows has no `/proc`).
#[cfg(windows)]
pub struct WindowsMemory {
    pid: u32,
    handle: win::Handle,
    modules: Vec<ModuleInfo>,
    regions: Vec<Region>,
}

#[cfg(windows)]
impl WindowsMemory {
    pub fn open(pid: u32) -> Result<Self, MemoryError> {
        let access = win::PROCESS_VM_READ
            | win::PROCESS_QUERY_INFORMATION
            | win::PROCESS_QUERY_LIMITED_INFORMATION;
        let handle = unsafe { win::OpenProcess(access, 0, pid) };
        if handle.is_null() {
            return Err(MemoryError::Io(std::io::Error::last_os_error()));
        }
        let modules = enumerate_modules(pid);
        let regions = enumerate_regions(handle);
        Ok(Self {
            pid,
            handle,
            modules,
            regions,
        })
    }

    pub fn pid(&self) -> u32 {
        self.pid
    }
}

#[cfg(windows)]
impl Drop for WindowsMemory {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                win::CloseHandle(self.handle);
            }
        }
    }
}

// The handle is an opaque kernel object id; it carries no thread affinity.
#[cfg(windows)]
unsafe impl Send for WindowsMemory {}
#[cfg(windows)]
unsafe impl Sync for WindowsMemory {}

#[cfg(windows)]
impl ProcessMemory for WindowsMemory {
    fn read(&self, addr: usize, buf: &mut [u8]) -> Result<(), MemoryError> {
        let mut done = 0usize;
        while done < buf.len() {
            let mut got: win::SizeT = 0;
            let ok = unsafe {
                win::ReadProcessMemory(
                    self.handle,
                    (addr + done) as win::Lpcvoid,
                    buf[done..].as_mut_ptr() as win::Lpvoid,
                    buf.len() - done,
                    &mut got,
                )
            };
            if ok == 0 || got == 0 {
                return Err(MemoryError::Io(std::io::Error::last_os_error()));
            }
            done += got;
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

/// Decode a NUL-terminated UTF-16 buffer (as the toolhelp structs use).
#[cfg(windows)]
fn wide_to_string(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end])
}

/// Enumerate the target's modules via a toolhelp snapshot.
#[cfg(windows)]
fn enumerate_modules(pid: u32) -> Vec<ModuleInfo> {
    let mut out = Vec::new();
    let snapshot = unsafe {
        win::CreateToolhelp32Snapshot(win::TH32CS_SNAPMODULE | win::TH32CS_SNAPMODULE32, pid)
    };
    if snapshot == win::INVALID_HANDLE_VALUE {
        return out;
    }
    let mut entry: win::ModuleEntry32W = unsafe { std::mem::zeroed() };
    entry.dwSize = std::mem::size_of::<win::ModuleEntry32W>() as u32;
    let mut ok = unsafe { win::Module32FirstW(snapshot, &mut entry) } != 0;
    while ok {
        out.push(ModuleInfo {
            name: wide_to_string(&entry.szModule),
            path: PathBuf::from(wide_to_string(&entry.szExePath)),
            base: entry.modBaseAddr as usize,
            size: entry.modBaseSize as usize,
        });
        entry.dwSize = std::mem::size_of::<win::ModuleEntry32W>() as u32;
        ok = unsafe { win::Module32NextW(snapshot, &mut entry) } != 0;
    }
    unsafe {
        win::CloseHandle(snapshot);
    }
    out
}

/// Enumerate committed regions via `VirtualQueryEx`, decoding the protection
/// bits into read/write/execute. Paths are not available from `VirtualQueryEx`,
/// so `path` is left empty.
#[cfg(windows)]
fn enumerate_regions(handle: win::Handle) -> Vec<Region> {
    let mut out = Vec::new();
    let mut address: usize = 0;
    loop {
        let mut mbi: win::MemoryBasicInformation = unsafe { std::mem::zeroed() };
        let written = unsafe {
            win::VirtualQueryEx(
                handle,
                address as win::Lpcvoid,
                &mut mbi,
                std::mem::size_of::<win::MemoryBasicInformation>(),
            )
        };
        if written == 0 || mbi.RegionSize == 0 {
            break;
        }
        let start = mbi.BaseAddress as usize;
        let end = start.saturating_add(mbi.RegionSize);
        if mbi.State == win::MEM_COMMIT {
            let p = mbi.Protect & 0xff;
            let read = matches!(
                p,
                win::PAGE_READONLY
                    | win::PAGE_READWRITE
                    | win::PAGE_WRITECOPY
                    | win::PAGE_EXECUTE_READ
                    | win::PAGE_EXECUTE_READWRITE
                    | win::PAGE_EXECUTE_WRITECOPY
            );
            let write = matches!(
                p,
                win::PAGE_READWRITE
                    | win::PAGE_WRITECOPY
                    | win::PAGE_EXECUTE_READWRITE
                    | win::PAGE_EXECUTE_WRITECOPY
            );
            let execute = matches!(
                p,
                win::PAGE_EXECUTE
                    | win::PAGE_EXECUTE_READ
                    | win::PAGE_EXECUTE_READWRITE
                    | win::PAGE_EXECUTE_WRITECOPY
            );
            out.push(Region {
                start,
                end,
                read,
                write,
                execute,
                path: String::new(),
            });
        }
        if end <= address {
            break;
        }
        address = end;
    }
    out
}

/// Find a PID by image name (case-insensitive) via a toolhelp snapshot.
#[cfg(windows)]
pub fn find_pid(name: &str) -> Option<u32> {
    let snapshot = unsafe { win::CreateToolhelp32Snapshot(win::TH32CS_SNAPPROCESS, 0) };
    if snapshot == win::INVALID_HANDLE_VALUE {
        return None;
    }
    let mut entry: win::ProcessEntry32W = unsafe { std::mem::zeroed() };
    entry.dwSize = std::mem::size_of::<win::ProcessEntry32W>() as u32;
    let mut ok = unsafe { win::Process32FirstW(snapshot, &mut entry) } != 0;
    let mut found = None;
    while ok {
        if wide_to_string(&entry.szExeFile).eq_ignore_ascii_case(name) {
            found = Some(entry.th32ProcessID);
            break;
        }
        ok = unsafe { win::Process32NextW(snapshot, &mut entry) } != 0;
    }
    unsafe {
        win::CloseHandle(snapshot);
    }
    found
}

#[cfg(test)]
mod tests {
    use super::*;

    const MAPS: &str = "\
6a0000-6b0000 r-xp 00000000 08:01 1234       /home/u/Counter-Strike Global Offensive/game/csgo/bin/win64/client.dll
6b0000-6c0000 rw-p 00000000 00:00 0          [heap]
7f00-8000 rw-s 00000000 08:01 99             /files/lib/wine/x86_64-windows/d3d11.dll
";

    #[test]
    fn module_paths_may_contain_spaces() {
        let modules = parse_maps(MAPS);
        let names: Vec<&str> = modules.iter().map(|m| m.name.as_str()).collect();
        assert!(names.contains(&"client.dll"), "got {names:?}");
        assert!(names.contains(&"d3d11.dll"));
        // [heap] is not a module.
        assert_eq!(modules.len(), 2);
        let client = modules.iter().find(|m| m.name == "client.dll").unwrap();
        assert_eq!(client.base, 0x6a0000);
        assert!(client
            .path
            .to_string_lossy()
            .contains("Counter-Strike Global Offensive"));
    }

    #[test]
    fn regions_parse_perms_and_spaced_paths() {
        let regions = parse_regions(MAPS);
        assert_eq!(regions.len(), 3);
        assert!(regions[0].read && regions[0].execute && !regions[0].write);
        assert!(regions[0].path.ends_with("client.dll"));
        assert!(regions[1].write);
        assert_eq!(regions[1].path, "[heap]");
    }

    #[test]
    fn basename_handles_unix_and_windows_separators() {
        assert_eq!(basename("/a/b/cs2.exe"), "cs2.exe");
        assert_eq!(basename(r"S:\steamapps\game\bin\win64\cs2.exe"), "cs2.exe");
        assert_eq!(basename("client.dll"), "client.dll");
    }
}
