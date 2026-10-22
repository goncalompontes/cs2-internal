//! Live-process helpers shared by the CLI and the MCP server.
//!
//! These wrap [`ProcessMemory`] / [`MemoryRead`] with the primitives a live
//! calibration session needs: typed reads, pointer-chain resolution and value
//! scanning across writable regions (a Cheat-Engine-style narrowing search).
//!
//! Values are handled **byte-exactly**: integer scans never round-trip through
//! `f64`, so 64-bit pointers and values are matched precisely.

use anyhow::{anyhow, Result};
use serde_json::Value;

use crate::memory::{ProcessMemory, Region};
use crate::reader::MemoryRead;

/// A scalar type understood by typed reads and value scans.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ValueKind {
    U8,
    I8,
    U16,
    I16,
    U32,
    I32,
    U64,
    I64,
    F32,
    F64,
}

impl ValueKind {
    pub fn parse(s: &str) -> Result<Self> {
        Ok(match s.to_ascii_lowercase().as_str() {
            "u8" | "uint8" | "byte" => Self::U8,
            "i8" | "int8" => Self::I8,
            "u16" | "uint16" | "short" => Self::U16,
            "i16" | "int16" => Self::I16,
            "u32" | "uint32" | "int" | "int32" => Self::U32,
            "i32" => Self::I32,
            "u64" | "uint64" | "ptr" | "pointer" => Self::U64,
            "i64" | "int64" => Self::I64,
            "f32" | "float" => Self::F32,
            "f64" | "double" => Self::F64,
            other => return Err(anyhow!("unknown value type {other:?}")),
        })
    }

    pub fn is_float(self) -> bool {
        matches!(self, Self::F32 | Self::F64)
    }

    pub fn size(self) -> usize {
        match self {
            Self::U8 | Self::I8 => 1,
            Self::U16 | Self::I16 => 2,
            Self::U32 | Self::I32 | Self::F32 => 4,
            Self::U64 | Self::I64 | Self::F64 => 8,
        }
    }

    /// Encode a JSON value (number, or hex/dec string) into little-endian bytes.
    pub fn encode(self, value: &Value) -> Result<Vec<u8>> {
        if self.is_float() {
            let f = as_f64(value)?;
            return Ok(match self {
                Self::F32 => (f as f32).to_le_bytes().to_vec(),
                _ => f.to_le_bytes().to_vec(),
            });
        }
        let n = as_i128(value)?;
        Ok(match self {
            Self::U8 | Self::I8 => vec![n as u8],
            Self::U16 | Self::I16 => (n as i16).to_le_bytes().to_vec(),
            Self::U32 | Self::I32 => (n as i32).to_le_bytes().to_vec(),
            Self::U64 | Self::I64 => (n as i64).to_le_bytes().to_vec(),
            _ => unreachable!(),
        })
    }

    /// Format raw bytes as the typed value, or a hex fallback if too short.
    pub fn format(self, bytes: &[u8]) -> String {
        if bytes.len() < self.size() {
            return format!("<{} bytes>", bytes.len());
        }
        match self {
            Self::U8 => format!("{}", bytes[0]),
            Self::I8 => format!("{}", bytes[0] as i8),
            Self::U16 => format!("{}", u16::from_le_bytes([bytes[0], bytes[1]])),
            Self::I16 => format!("{}", i16::from_le_bytes([bytes[0], bytes[1]])),
            Self::U32 => format!("{}", u32::from_le_bytes(bytes[..4].try_into().unwrap())),
            Self::I32 => format!("{}", i32::from_le_bytes(bytes[..4].try_into().unwrap())),
            Self::U64 => format!("0x{:x}", u64::from_le_bytes(bytes[..8].try_into().unwrap())),
            Self::I64 => format!("{}", i64::from_le_bytes(bytes[..8].try_into().unwrap())),
            Self::F32 => format!("{}", f32::from_le_bytes(bytes[..4].try_into().unwrap())),
            Self::F64 => format!("{}", f64::from_le_bytes(bytes[..8].try_into().unwrap())),
        }
    }
}

fn as_f64(value: &Value) -> Result<f64> {
    match value {
        Value::Number(n) => n.as_f64().ok_or_else(|| anyhow!("invalid number {n}")),
        Value::String(s) => s
            .trim()
            .parse::<f64>()
            .map_err(|e| anyhow!("invalid float {s:?}: {e}")),
        other => Err(anyhow!("expected number, got {other}")),
    }
}

fn as_i128(value: &Value) -> Result<i128> {
    match value {
        Value::Number(n) => n
            .as_i64()
            .map(i128::from)
            .or_else(|| n.as_u64().map(i128::from))
            .ok_or_else(|| anyhow!("invalid integer {n}")),
        Value::String(s) => {
            let t = s.trim();
            if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
                u128::from_str_radix(hex, 16)
                    .map(|v| v as i128)
                    .map_err(|e| anyhow!("invalid hex {s:?}: {e}"))
            } else {
                t.parse::<i128>()
                    .map_err(|e| anyhow!("invalid integer {s:?}: {e}"))
            }
        }
        other => Err(anyhow!("expected integer, got {other}")),
    }
}

/// Parse a hex (string) or integer (number) JSON value as an address.
pub fn parse_addr(value: &Value) -> Result<usize> {
    match value {
        Value::String(s) => parse_hex(s),
        Value::Number(n) => n
            .as_u64()
            .map(|v| v as usize)
            .ok_or_else(|| anyhow!("invalid address number {n}")),
        other => Err(anyhow!("invalid address {other}")),
    }
}

/// Parse `0x`-prefixed hex or plain decimal into a usize.
pub fn parse_hex(text: &str) -> Result<usize> {
    let t = text.trim();
    match t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        Some(hex) => {
            usize::from_str_radix(hex, 16).map_err(|e| anyhow!("invalid hex {text:?}: {e}"))
        }
        None => t
            .parse::<usize>()
            .map_err(|e| anyhow!("invalid number {text:?}: {e}")),
    }
}

/// Follow a pointer chain: `value = read_ptr(base)`, then
/// `value = read_ptr(value + offset)` for each offset. With `deref_first=false`
/// the first dereference is skipped (the base is treated as the object address).
pub fn resolve_chain<R: MemoryRead>(
    reader: &R,
    base: usize,
    offsets: &[usize],
    deref_first: bool,
) -> Result<Vec<usize>> {
    let mut steps = Vec::new();
    let mut current = if deref_first {
        reader.read_ptr(base)?
    } else {
        base
    };
    steps.push(current);
    for &offset in offsets {
        current = reader.read_ptr(current.wrapping_add(offset))?;
        steps.push(current);
    }
    Ok(steps)
}

/// Read `size` bytes from `base`, tolerantly zero-filling unmapped gaps.
pub fn read_span(mem: &dyn ProcessMemory, base: usize, size: usize) -> Vec<u8> {
    const CHUNK: usize = 0x1000;
    let mut out = vec![0u8; size];
    let mut off = 0usize;
    while off < size {
        let n = CHUNK.min(size - off);
        let _ = mem.read(base + off, &mut out[off..off + n]);
        off += n;
    }
    out
}

/// Read a module image, sizing it from the mapped range but preferring the PE
/// header's `SizeOfImage` when it is larger (Wine maps most of a PE image
/// anonymously, so `/proc/<pid>/maps` only reports the header page).
pub fn read_module_image(mem: &dyn ProcessMemory, base: usize, mapped_size: usize) -> Vec<u8> {
    const MAX: usize = 512 << 20;
    let head = read_span(mem, base, 0x1000);
    let mut size = mapped_size.max(0x1000);
    if let Some(size_of_image) = pe_size_of_image(&head) {
        if size_of_image > size {
            size = size_of_image;
        }
    }
    read_span(mem, base, size.min(MAX))
}

/// Decode `SizeOfImage` straight from PE headers. Deliberately does not use
/// `goblin`: full parsing eagerly walks data directories and fails on a
/// headers-only buffer.
fn pe_size_of_image(head: &[u8]) -> Option<usize> {
    if head.len() < 0x40 || &head[0..2] != b"MZ" {
        return None;
    }
    let e_lfanew = u32::from_le_bytes(head[0x3c..0x40].try_into().ok()?) as usize;
    if head.len() < e_lfanew + 0x54 || &head[e_lfanew..e_lfanew + 4] != b"PE\0\0" {
        return None;
    }
    let size = u32::from_le_bytes(head[e_lfanew + 0x50..e_lfanew + 0x54].try_into().ok()?);
    Some(size as usize)
}

/// Regions eligible for a value scan: readable, optionally writable-only.
pub fn scan_regions(mem: &dyn ProcessMemory, writable_only: bool) -> Vec<Region> {
    mem.regions()
        .into_iter()
        .filter(|r| r.read && (!writable_only || r.write))
        .collect()
}

/// Search `regions` for the exact byte `needle`, returning match addresses.
///
/// Scans every byte offset (Cheat-Engine style) in chunks with a small overlap
/// so matches spanning a chunk boundary are still found. `max_bytes` caps total
/// scanned memory to keep a broad scan bounded.
pub fn scan_bytes(
    mem: &dyn ProcessMemory,
    needle: &[u8],
    regions: &[Region],
    limit: usize,
    max_bytes: usize,
) -> Vec<usize> {
    if needle.is_empty() {
        return Vec::new();
    }
    let mut out = Vec::new();
    let mut scanned = 0usize;
    const CHUNK: usize = 1 << 20;

    for region in regions {
        let mut base = region.start;
        while base < region.end && scanned < max_bytes && out.len() < limit {
            let len = CHUNK.min(region.end - base);
            let Ok(buf) = mem.read_vec(base, len) else {
                break;
            };
            let mut i = 0usize;
            while i + needle.len() <= buf.len() {
                if buf[i..i + needle.len()] == *needle {
                    out.push(base + i);
                    if out.len() >= limit {
                        break;
                    }
                }
                i += 1;
            }
            scanned += len;
            if len < CHUNK {
                break;
            }
            base += CHUNK - (needle.len() - 1);
        }
        if scanned >= max_bytes || out.len() >= limit {
            break;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::memory::{MemoryError, ModuleInfo};
    use crate::reader::BytesReader;
    use serde_json::json;

    /// A tiny in-memory ProcessMemory for deterministic scan tests.
    struct FakeMem {
        base: usize,
        data: Vec<u8>,
        writable: bool,
    }

    impl ProcessMemory for FakeMem {
        fn read(&self, addr: usize, buf: &mut [u8]) -> Result<(), MemoryError> {
            let off = addr.checked_sub(self.base).ok_or(MemoryError::NotOpen)?;
            let end = off + buf.len();
            if end > self.data.len() {
                return Err(MemoryError::NotOpen);
            }
            buf.copy_from_slice(&self.data[off..end]);
            Ok(())
        }
        fn modules(&self) -> Vec<ModuleInfo> {
            Vec::new()
        }
        fn regions(&self) -> Vec<Region> {
            vec![Region {
                start: self.base,
                end: self.base + self.data.len(),
                read: true,
                write: self.writable,
                execute: false,
                path: String::new(),
            }]
        }
    }

    #[test]
    fn encode_is_byte_exact_for_64_bit_values() {
        // The value must survive encode/format without f64 precision loss.
        let bytes = ValueKind::U64.encode(&json!("0x1122334455667788")).unwrap();
        assert_eq!(bytes, vec![0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11]);
        assert_eq!(ValueKind::U64.format(&bytes), "0x1122334455667788");

        let bytes = ValueKind::I32.encode(&json!(-7)).unwrap();
        assert_eq!(ValueKind::I32.format(&bytes), "-7");
    }

    #[test]
    fn scan_bytes_finds_every_byte_offset() {
        let base = 0x1000usize;
        let mut data = vec![0u8; 0x40];
        let needle = ValueKind::U32.encode(&json!(0x1122_3344u32)).unwrap();
        data[5..9].copy_from_slice(&needle);
        data[12..16].copy_from_slice(&needle);
        let mem = FakeMem {
            base,
            data,
            writable: true,
        };

        let regions = scan_regions(&mem, true);
        let hits = scan_bytes(&mem, &needle, &regions, 16, 1 << 20);
        assert_eq!(hits, vec![base + 5, base + 12]);
    }

    #[test]
    fn scan_regions_respects_writable_only() {
        let mem = FakeMem {
            base: 0x1000,
            data: vec![0u8; 8],
            writable: false,
        };
        assert!(scan_regions(&mem, true).is_empty());
        assert_eq!(scan_regions(&mem, false).len(), 1);
    }

    #[test]
    fn decodes_size_of_image_from_headers() {
        let mut head = vec![0u8; 0x200];
        head[0..2].copy_from_slice(b"MZ");
        head[0x3c..0x40].copy_from_slice(&0x40u32.to_le_bytes());
        head[0x40..0x44].copy_from_slice(b"PE\0\0");
        head[0x40 + 0x50..0x40 + 0x54].copy_from_slice(&0x2994000u32.to_le_bytes());
        assert_eq!(pe_size_of_image(&head), Some(0x2994000));
        // Not a PE.
        assert_eq!(pe_size_of_image(&[0u8; 8]), None);
    }

    #[test]
    fn resolves_pointer_chain_through_reader() {
        let base = 0x1000usize;
        let mut data = vec![0u8; 0x40];
        // base -> base+0x10 ; (base+0x10)+0x10 -> base+0x20 ; base+0x20 holds 0xdead
        data[0x00..0x08].copy_from_slice(&((base + 0x10) as u64).to_le_bytes());
        data[0x10..0x18].copy_from_slice(&((base + 0x20) as u64).to_le_bytes());
        data[0x20..0x28].copy_from_slice(&0xdeadu64.to_le_bytes());
        let reader = BytesReader::new(base, data);

        let steps = resolve_chain(&reader, base, &[0x10], true).unwrap();
        assert_eq!(steps, vec![base + 0x10, 0xdead]);
    }
}
