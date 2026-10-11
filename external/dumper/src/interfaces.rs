//! `CreateInterface` resolver.
//!
//! Source modules register interfaces as a linked list of `InterfaceReg`:
//!
//! ```c
//! struct InterfaceReg {           // 24 bytes on x86-64
//!     void* (*create)();          // +0x00
//!     const char* name;           // +0x08
//!     InterfaceReg* next;         // +0x10
//! };
//! ```
//!
//! We locate the list by finding pointers to known interface-name strings and
//! reconstructing the list heads, then walking each list. All pointer fields are
//! validated to lie inside the module, so stray byte matches don't derail the
//! walk.

use anyhow::Result;
use serde::Serialize;

use crate::reader::MemoryRead;

pub const INTERFACE_REG_SIZE: usize = 24;

#[derive(Debug, Clone, Serialize)]
pub struct Interface {
    pub name: String,
    /// RVA of the `InterfaceReg` node.
    pub rva: usize,
    /// RVA of the create function.
    pub create_rva: usize,
}

/// Walk one linked list starting at `first` (absolute address). Stops at the
/// end of the list or when a pointer leaves `[base, base + size)`.
pub fn walk_list<R: MemoryRead>(
    reader: &R,
    base: usize,
    size: usize,
    first: usize,
) -> Vec<Interface> {
    let end = base + size;
    let mut out = Vec::new();
    let mut node = first;
    let mut guard = 0usize;

    while node != 0 && guard < 8192 {
        if node < base || node + INTERFACE_REG_SIZE > end {
            break;
        }
        let (Ok(create), Ok(name_ptr), Ok(next)) = (
            reader.read_ptr(node),
            reader.read_ptr(node + 8),
            reader.read_ptr(node + 16),
        ) else {
            break;
        };

        if name_ptr >= base && name_ptr < end {
            if let Ok(name) = reader.read_cstring(name_ptr) {
                if looks_like_interface_name(&name) {
                    out.push(Interface {
                        name,
                        rva: node - base,
                        create_rva: create.wrapping_sub(base),
                    });
                }
            }
        }

        if next != 0 && (next < base || next >= end) {
            break;
        }
        node = next;
        guard += 1;
    }
    out
}

/// Heuristic: interface names end in a 3-digit version, e.g. `Source2Client002`,
/// `GameClientExports001`, `AnimationSystem_001`.
fn looks_like_interface_name(s: &str) -> bool {
    if s.len() < 5 || s.len() > 128 {
        return false;
    }
    if !s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return false;
    }
    let bytes = s.as_bytes();
    let n = bytes.len();
    bytes[n - 1].is_ascii_digit() && bytes[n - 2].is_ascii_digit() && bytes[n - 3].is_ascii_digit()
}

/// Enumerate *all* interfaces by scanning the module for `{create, name}`
/// registration entries (16-byte stride in current builds).
pub fn enumerate_all<R: MemoryRead>(reader: &R, base: usize, size: usize) -> Result<Vec<Interface>> {
    let bytes = reader.read_bytes(base, size)?;
    let end = base + size;

    let mut out = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    let mut offset = 0usize;
    while offset + 16 <= bytes.len() {
        let create = read_u64_at(&bytes, offset);
        let name_ptr = read_u64_at(&bytes, offset + 8) as usize;
        if create >= base && create < end && name_ptr >= base && name_ptr < end {
            if let Some(name) = cstring_at(&bytes, name_ptr - base) {
                if looks_like_interface_name(&name) && seen.insert(name.clone()) {
                    out.push(Interface {
                        name,
                        rva: base + offset,
                        create_rva: create - base,
                    });
                }
            }
        }
        offset += 8;
    }
    Ok(out)
}

/// Enumerate interfaces by locating `InterfaceReg` nodes via pointers to the
/// provided seed name strings, then walking from the discovered list heads.
pub fn enumerate<R: MemoryRead>(
    reader: &R,
    base: usize,
    size: usize,
    seed_names: &[&str],
) -> Result<Vec<Interface>> {
    let bytes = reader.read_bytes(base, size)?;
    let end = base + size;

    let mut candidates: Vec<usize> = Vec::new();
    for seed in seed_names {
        for str_off in find_subslice(&bytes, seed.as_bytes()) {
            let str_addr = base + str_off;
            let needle = (str_addr as u64).to_le_bytes();
            for p in find_subslice(&bytes, &needle) {
                if p < 8 {
                    continue;
                }
                candidates.push(base + p - 8);
            }
        }
    }
    candidates.sort_unstable();
    candidates.dedup();

    candidates.retain(|&c| {
        let create = reader.read_ptr(c).ok();
        let next = reader.read_ptr(c + 16).ok();
        matches!((create, next), (Some(create), Some(next))
            if create >= base && create < end && (next == 0 || (next >= base && next < end)))
    });

    Ok(resolve_heads(reader, base, size, candidates))
}

/// Compute list heads (nodes not referenced as `next`) and walk them.
fn resolve_heads<R: MemoryRead>(
    reader: &R,
    base: usize,
    size: usize,
    mut candidates: Vec<usize>,
) -> Vec<Interface> {
    candidates.sort_unstable();
    candidates.dedup();

    let mut referenced = std::collections::BTreeSet::new();
    for &c in &candidates {
        if let Ok(next) = reader.read_ptr(c + 16) {
            if next != 0 {
                referenced.insert(next);
            }
        }
    }
    let heads: Vec<usize> = candidates
        .into_iter()
        .filter(|c| !referenced.contains(c))
        .collect();

    let mut out = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    for head in heads {
        for it in walk_list(reader, base, size, head) {
            if seen.insert(it.name.clone()) {
                out.push(it);
            }
        }
    }
    out
}

fn read_u64_at(bytes: &[u8], offset: usize) -> usize {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap()) as usize
}

fn cstring_at(bytes: &[u8], offset: usize) -> Option<String> {
    if offset >= bytes.len() {
        return None;
    }
    let mut out = String::new();
    let mut i = offset;
    while i < bytes.len() && bytes[i] != 0 {
        out.push(bytes[i] as char);
        i += 1;
        if out.len() > 128 {
            return None;
        }
    }
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}

pub fn find_subslice(hay: &[u8], needle: &[u8]) -> Vec<usize> {
    if needle.is_empty() || needle.len() > hay.len() {
        return Vec::new();
    }
    (0..=hay.len() - needle.len())
        .filter(|&i| &hay[i..i + needle.len()] == needle)
        .collect()
}
