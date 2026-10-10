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
//! reconstructing the list heads, then walking each list.

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

/// Walk one linked list starting at `first` (absolute address).
pub fn walk_list<R: MemoryRead>(reader: &R, base: usize, first: usize) -> Result<Vec<Interface>> {
    let mut out = Vec::new();
    let mut node = first;
    let mut guard = 0usize;
    while node != 0 && guard < 8192 {
        let create = reader.read_ptr(node)?;
        let name_ptr = reader.read_ptr(node + 8)?;
        let next = reader.read_ptr(node + 16)?;

        if name_ptr != 0 {
            let name = reader.read_cstring(name_ptr)?;
            if looks_like_interface_name(&name) {
                out.push(Interface {
                    name,
                    rva: node.wrapping_sub(base),
                    create_rva: create.wrapping_sub(base),
                });
            }
        }
        node = next;
        guard += 1;
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

    // Collect candidate node addresses (name field at node+8).
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

    // Heads are candidates not referenced as `next` by another candidate.
    let mut referenced = std::collections::BTreeSet::new();
    for &c in &candidates {
        if let Ok(next) = reader.read_ptr(c + 16) {
            if next != 0 {
                referenced.insert(next);
            }
        }
    }
    let heads: Vec<usize> = candidates
        .iter()
        .copied()
        .filter(|c| !referenced.contains(c))
        .collect();

    let mut out = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    for head in heads {
        for it in walk_list(reader, base, head)? {
            if seen.insert(it.name.clone()) {
                out.push(it);
            }
        }
    }
    Ok(out)
}

/// Heuristic: interface names look like `Source2Client002`, `GameClientExports001`.
fn looks_like_interface_name(s: &str) -> bool {
    if s.len() < 4 || s.len() > 128 {
        return false;
    }
    s.chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_')
}

pub fn find_subslice(hay: &[u8], needle: &[u8]) -> Vec<usize> {
    if needle.is_empty() || needle.len() > hay.len() {
        return Vec::new();
    }
    (0..=hay.len() - needle.len())
        .filter(|&i| &hay[i..i + needle.len()] == needle)
        .collect()
}
