//! Thin PE helpers on top of `goblin`: image base, size, sections, exports.
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
pub struct SectionInfo {
    pub name: String,
    pub virtual_address: u32,
    pub virtual_size: u32,
    pub raw_offset: u32,
    pub raw_size: u32,
}

#[derive(Debug, Clone)]
pub struct PeInfo {
    pub image_base: u64,
    pub size_of_image: u32,
    pub size_of_headers: u32,
    pub entry_point: u64,
    pub sections: Vec<SectionInfo>,
    pub exports: Vec<Export>,
}

/// Parse a PE image from a byte buffer (a full image or at least headers+sections).
pub fn parse_image(bytes: &[u8]) -> Result<PeInfo> {
    let pe = PE::parse(bytes)?;

    let opt = pe
        .header
        .optional_header
        .ok_or_else(|| anyhow!("PE has no optional header"))?;

    let sections = pe
        .sections
        .iter()
        .map(|s| {
            let name = String::from_utf8_lossy(&s.name)
                .trim_end_matches('\0')
                .to_string();
            SectionInfo {
                name,
                virtual_address: s.virtual_address,
                virtual_size: s.virtual_size,
                raw_offset: s.pointer_to_raw_data,
                raw_size: s.size_of_raw_data,
            }
        })
        .collect();

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
        size_of_headers: opt.windows_fields.size_of_headers,
        entry_point: opt.standard_fields.address_of_entry_point,
        sections,
        exports,
    })
}

/// Map a file image into an RVA-addressed buffer (`buffer[rva] == data`), so
/// pointer values stored in the image can be matched directly against RVAs.
pub fn map_file_to_rva(bytes: &[u8], info: &PeInfo) -> Vec<u8> {
    let mut out = vec![0u8; info.size_of_image as usize];

    let headers = (info.size_of_headers as usize)
        .min(bytes.len())
        .min(out.len());
    out[..headers].copy_from_slice(&bytes[..headers]);

    for section in &info.sections {
        let raw_offset = section.raw_offset as usize;
        let raw_size = section.raw_size as usize;
        let virtual_address = section.virtual_address as usize;
        if raw_size == 0 {
            continue;
        }
        if raw_offset + raw_size <= bytes.len() && virtual_address + raw_size <= out.len() {
            out[virtual_address..virtual_address + raw_size]
                .copy_from_slice(&bytes[raw_offset..raw_offset + raw_size]);
        }
    }
    out
}
