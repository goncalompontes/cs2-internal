//! Thin PE helpers on top of `goblin`: image base, size, exports.
//!
//! We parse a module image that has already been read out of process memory
//! (headers + sections), which is what the dumper and the payload both do.

use anyhow::{anyhow, Result};
use goblin::pe::PE;

#[derive(Debug, Clone)]
pub struct Export {
    pub name: String,
    pub rva: u32,
}

#[derive(Debug, Clone)]
pub struct PeInfo {
    pub image_base: u64,
    pub size_of_image: u32,
    pub entry_point: u64,
    pub exports: Vec<Export>,
}

/// Parse a PE image from a byte buffer (a full image or at least headers+sections).
pub fn parse_image(bytes: &[u8]) -> Result<PeInfo> {
    let pe = PE::parse(bytes)?;

    let opt = pe
        .header
        .optional_header
        .ok_or_else(|| anyhow!("PE has no optional header"))?;

    let exports = pe
        .exports
        .iter()
        .filter_map(|e| {
            e.name.map(|name| Export {
                name: name.to_string(),
                rva: e.rva as u32,
            })
        })
        .collect();

    Ok(PeInfo {
        image_base: opt.windows_fields.image_base,
        size_of_image: opt.windows_fields.size_of_image,
        entry_point: opt.standard_fields.address_of_entry_point,
        exports,
    })
}
