//! A small read abstraction used by the interface and schema walkers.
//!
//! `MemoryRead` is implemented by [`LinuxProcMemory`] for live targets and by
//! [`BytesReader`] for fixtures, so all traversal logic is unit-testable with
//! no game process.

use anyhow::{anyhow, Result};

use crate::memory::ProcessMemory;
#[cfg(unix)]
use crate::memory::LinuxProcMemory;
#[cfg(windows)]
use crate::memory::WindowsMemory;

pub trait MemoryRead {
    fn read_bytes(&self, addr: usize, len: usize) -> Result<Vec<u8>>;

    fn read_u8(&self, addr: usize) -> Result<u8> {
        Ok(self.read_bytes(addr, 1)?[0])
    }

    fn read_u16(&self, addr: usize) -> Result<u16> {
        let b = self.read_bytes(addr, 2)?;
        Ok(u16::from_le_bytes([b[0], b[1]]))
    }

    fn read_i16(&self, addr: usize) -> Result<i16> {
        let b = self.read_bytes(addr, 2)?;
        Ok(i16::from_le_bytes([b[0], b[1]]))
    }

    fn read_u32(&self, addr: usize) -> Result<u32> {
        let b = self.read_bytes(addr, 4)?;
        Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }

    fn read_i32(&self, addr: usize) -> Result<i32> {
        Ok(self.read_u32(addr)? as i32)
    }

    fn read_u64(&self, addr: usize) -> Result<u64> {
        let b = self.read_bytes(addr, 8)?;
        Ok(u64::from_le_bytes(b[..8].try_into().unwrap()))
    }

    fn read_ptr(&self, addr: usize) -> Result<usize> {
        Ok(self.read_u64(addr)? as usize)
    }

    /// Read a NUL-terminated string, capped to avoid runaway reads.
    fn read_cstring(&self, addr: usize) -> Result<String> {
        const MAX: usize = 4096;
        let mut out = Vec::new();
        let mut cur = addr;
        while out.len() < MAX {
            let chunk = self.read_bytes(cur, 256).unwrap_or_default();
            if chunk.is_empty() {
                break;
            }
            if let Some(nul) = chunk.iter().position(|&b| b == 0) {
                out.extend_from_slice(&chunk[..nul]);
                break;
            }
            out.extend_from_slice(&chunk);
            cur += chunk.len();
        }
        Ok(String::from_utf8_lossy(&out).to_string())
    }
}

#[cfg(unix)]
impl MemoryRead for LinuxProcMemory {
    fn read_bytes(&self, addr: usize, len: usize) -> Result<Vec<u8>> {
        ProcessMemory::read_vec(self, addr, len).map_err(|e| anyhow!(e.to_string()))
    }
}

#[cfg(windows)]
impl MemoryRead for WindowsMemory {
    fn read_bytes(&self, addr: usize, len: usize) -> Result<Vec<u8>> {
        ProcessMemory::read_vec(self, addr, len).map_err(|e| anyhow!(e.to_string()))
    }
}

/// In-memory reader for fixtures and analysis of recorded dumps.
pub struct BytesReader {
    pub base: usize,
    pub data: Vec<u8>,
}

impl BytesReader {
    pub fn new(base: usize, data: Vec<u8>) -> Self {
        Self { base, data }
    }
}

impl MemoryRead for BytesReader {
    fn read_bytes(&self, addr: usize, len: usize) -> Result<Vec<u8>> {
        let off = addr
            .checked_sub(self.base)
            .ok_or_else(|| anyhow!("address 0x{addr:x} below base 0x{:x}", self.base))?;
        self.data
            .get(off..off + len)
            .map(|s| s.to_vec())
            .ok_or_else(|| anyhow!("read out of range at 0x{addr:x} len {len}"))
    }
}
