//! Schema system walker.
//!
//! Walks `CSchemaSystem` -> type scopes -> class hash maps -> fields using
//! [`SchemaLayout`], whose defaults are calibrated against the live client
//! (build 25815307, See docs/re/schema-system.md).
//!
//! Class lookup is a hash map, not a flat vector: each scope holds `0x100`
//! buckets at a fixed offset, each bucket a singly linked list of nodes that
//! point at `SchemaClassInfoData`.

use std::collections::BTreeSet;

use anyhow::Result;
use serde::{Deserialize, Serialize};

use crate::reader::MemoryRead;

/// Struct offsets for the schema system. All are relative to the indicated base.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SchemaLayout {
    // --- CSchemaSystem ---
    /// `m_TypeScopes` count (`u16`) inside the schema system.
    pub type_scopes_count_offset: usize,
    /// `m_TypeScopes` data pointer inside the schema system.
    pub type_scopes_data_offset: usize,

    // --- CSchemaTypeScope ---
    /// Inline NUL-terminated module name (`char[]`).
    pub scope_name_offset: usize,
    /// Class count (`i32`, informational).
    pub scope_class_count_offset: usize,
    /// First class-hash bucket.
    pub scope_buckets_offset: usize,
    /// Number of buckets.
    pub scope_bucket_count: usize,
    /// Stride between buckets.
    pub scope_bucket_stride: usize,
    /// Linked-list head pointer inside a bucket.
    pub scope_bucket_head_offset: usize,
    /// `next` pointer inside a bucket node.
    pub scope_node_next_offset: usize,
    /// `SchemaClassInfoData*` inside a bucket node.
    pub scope_node_class_offset: usize,

    // --- SchemaClassInfoData ---
    /// Class name (`const char*`).
    pub class_name_offset: usize,
    /// Module name (`const char*`).
    pub class_module_name_offset: usize,
    /// Class size (`u32`).
    pub class_size_offset: usize,
    /// Field count (`u32`).
    pub class_field_count_offset: usize,
    /// Fields array pointer.
    pub class_fields_offset: usize,

    // --- SchemaClassFieldData ---
    pub field_stride: usize,
    pub field_name_offset: usize,
    /// `CSchemaType*`.
    pub field_type_offset: usize,
    pub field_offset_offset: usize,

    // --- CSchemaType ---
    /// Type name (`const char*`).
    pub type_name_offset: usize,
}

impl Default for SchemaLayout {
    fn default() -> Self {
        // Calibrated against the live client, build 25815307. See
        // docs/re/schema-system.md.
        Self {
            type_scopes_count_offset: 0x190,
            type_scopes_data_offset: 0x198,

            scope_name_offset: 0x08,
            scope_class_count_offset: 0x56c,
            scope_buckets_offset: 0x5c0,
            scope_bucket_count: 0x100,
            scope_bucket_stride: 0x18,
            scope_bucket_head_offset: 0x10,
            scope_node_next_offset: 0x08,
            scope_node_class_offset: 0x10,

            class_name_offset: 0x08,
            class_module_name_offset: 0x10,
            class_size_offset: 0x20,
            class_field_count_offset: 0x24,
            class_fields_offset: 0x30,

            field_stride: 0x20,
            field_name_offset: 0x00,
            field_type_offset: 0x08,
            field_offset_offset: 0x10,

            type_name_offset: 0x08,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SchemaField {
    pub name: String,
    pub type_name: String,
    pub offset: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SchemaClass {
    pub module: String,
    pub name: String,
    pub size: u32,
    pub fields: Vec<SchemaField>,
}

const MAX_CLASSES: usize = 1 << 17;
const MAX_FIELDS: usize = 1 << 13;

/// Walk the schema system at `schema_system` and return all classes.
pub fn dump<R: MemoryRead>(
    reader: &R,
    schema_system: usize,
    layout: &SchemaLayout,
) -> Result<Vec<SchemaClass>> {
    let mut classes = Vec::new();
    let mut seen: BTreeSet<String> = BTreeSet::new();

    let scope_count = reader.read_u16(schema_system + layout.type_scopes_count_offset)? as usize;
    let scopes = reader.read_ptr(schema_system + layout.type_scopes_data_offset)?;

    for i in 0..scope_count {
        let scope = reader.read_ptr(scopes + i * 8)?;
        if scope == 0 {
            continue;
        }
        let scope_name = reader
            .read_cstring(scope + layout.scope_name_offset)
            .unwrap_or_default();

        for bucket in 0..layout.scope_bucket_count {
            let base = scope + layout.scope_buckets_offset + bucket * layout.scope_bucket_stride;
            let mut node = reader
                .read_ptr(base + layout.scope_bucket_head_offset)
                .unwrap_or(0);
            let mut guard = 0usize;
            while node != 0 && guard < MAX_CLASSES {
                if let Ok(class) = reader.read_ptr(node + layout.scope_node_class_offset) {
                    if class != 0 {
                        if let Some(c) = read_class(reader, class, &scope_name, layout) {
                            if seen.insert(c.name.clone()) {
                                classes.push(c);
                                if classes.len() >= MAX_CLASSES {
                                    return Ok(classes);
                                }
                            }
                        }
                    }
                }
                node = reader
                    .read_ptr(node + layout.scope_node_next_offset)
                    .unwrap_or(0);
                guard += 1;
            }
        }
    }

    Ok(classes)
}

fn read_class<R: MemoryRead>(
    reader: &R,
    class: usize,
    scope_name: &str,
    layout: &SchemaLayout,
) -> Option<SchemaClass> {
    let name = reader
        .read_cstring(reader.read_ptr(class + layout.class_name_offset).ok()?)
        .ok()?;
    if !plausible_symbol(&name) {
        return None;
    }

    let module = reader
        .read_cstring(
            reader
                .read_ptr(class + layout.class_module_name_offset)
                .unwrap_or(0),
        )
        .unwrap_or_default();
    let size = reader
        .read_u32(class + layout.class_size_offset)
        .unwrap_or(0);

    let field_count = reader
        .read_u32(class + layout.class_field_count_offset)
        .unwrap_or(0)
        .min(MAX_FIELDS as u32) as usize;
    let fields_ptr = reader
        .read_ptr(class + layout.class_fields_offset)
        .unwrap_or(0);

    let mut fields = Vec::new();
    for k in 0..field_count {
        let f = fields_ptr + k * layout.field_stride;
        let fname = reader
            .read_cstring(reader.read_ptr(f + layout.field_name_offset).unwrap_or(0))
            .unwrap_or_default();
        // Fields are contiguous from the start; the first implausible name means
        // we've overrun the real array (e.g. an over-large count), so stop.
        if !plausible_symbol(&fname) {
            break;
        }
        let type_ptr = reader.read_ptr(f + layout.field_type_offset).unwrap_or(0);
        let type_name = if type_ptr != 0 {
            reader
                .read_ptr(type_ptr + layout.type_name_offset)
                .ok()
                .and_then(|p| reader.read_cstring(p).ok())
                .filter(|t| plausible_symbol(t))
                .unwrap_or_default()
        } else {
            String::new()
        };
        let offset = reader.read_u32(f + layout.field_offset_offset).unwrap_or(0);
        fields.push(SchemaField {
            name: fname,
            type_name,
            offset,
        });
    }

    Some(SchemaClass {
        module: if scope_name.is_empty() {
            module
        } else {
            scope_name.to_string()
        },
        name,
        size,
        fields,
    })
}

/// A plausible C++/Source 2 symbol: printable ASCII, bounded length, at least
/// one alphanumeric. Rejects pointers misread as strings and overrun arrays.
fn plausible_symbol(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 200
        && s.chars().all(|c| c.is_ascii_graphic() || c == ' ')
        && s.chars().any(|c| c.is_ascii_alphanumeric())
}
