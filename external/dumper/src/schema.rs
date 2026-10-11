//! Schema system walker.
//!
//! Walks `CSchemaSystem` -> type scopes -> class bindings -> fields, using a
//! configurable [`SchemaLayout`]. The layout offsets are **version-specific**:
//! the defaults are placeholders from public research and MUST be calibrated
//! against the running build (ReVa/Ghidra + a schema dump) before relying on
//! real output. The traversal and emit logic here are fixture-tested.

use anyhow::Result;
use serde::{Deserialize, Serialize};

use crate::reader::MemoryRead;

/// Struct offsets for the schema system. All are relative to the indicated base.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SchemaLayout {
    /// `CSchemaSystem::m_TypeScopes` (a `CUtlVector<CSchemaTypeScope*>`).
    pub type_scopes_offset: usize,
    /// Offset of `m_Size` inside a `CUtlVector`.
    pub vector_count_offset: usize,
    /// `CSchemaTypeScope::m_Name` (const char*).
    pub scope_name_offset: usize,
    /// `CSchemaTypeScope` class-binding vector.
    pub scope_classes_offset: usize,
    /// `SchemaClassInfoData::m_Name`.
    pub class_name_offset: usize,
    /// `SchemaClassInfoData::m_ModuleName`.
    pub class_module_name_offset: usize,
    /// `SchemaClassInfoData::m_nSize`.
    pub class_size_offset: usize,
    /// `SchemaClassInfoData` field vector.
    pub class_fields_offset: usize,
    /// Size of one `SchemaClassFieldData` entry.
    pub field_stride: usize,
    pub field_name_offset: usize,
    pub field_type_offset: usize,
    pub field_offset_offset: usize,
}

impl Default for SchemaLayout {
    fn default() -> Self {
        // PLACEHOLDERS — calibrate per build. See the module docs.
        Self {
            type_scopes_offset: 0x190,
            vector_count_offset: 0x10,
            scope_name_offset: 0x8,
            scope_classes_offset: 0x510,
            class_name_offset: 0x8,
            class_module_name_offset: 0x18,
            class_size_offset: 0x20,
            class_fields_offset: 0x30,
            field_stride: 0x20,
            field_name_offset: 0x0,
            field_type_offset: 0x8,
            field_offset_offset: 0x10,
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

/// Read a `CUtlVector<T>` at `addr`: returns `(data_ptr, count)`.
pub fn read_utlvector<R: MemoryRead>(
    reader: &R,
    addr: usize,
    count_offset: usize,
) -> Result<(usize, usize)> {
    let data = reader.read_ptr(addr)?;
    let count = reader.read_i32(addr + count_offset)?.max(0) as usize;
    Ok((data, count))
}

/// Walk the schema system at `schema_system` and return all classes.
pub fn dump<R: MemoryRead>(
    reader: &R,
    schema_system: usize,
    layout: &SchemaLayout,
) -> Result<Vec<SchemaClass>> {
    let mut classes = Vec::new();

    let (scopes, scope_count) =
        read_utlvector(reader, schema_system + layout.type_scopes_offset, layout.vector_count_offset)?;

    for i in 0..scope_count {
        let scope = reader.read_ptr(scopes + i * 8)?;
        if scope == 0 {
            continue;
        }

        let scope_name = reader
            .read_cstring(reader.read_ptr(scope + layout.scope_name_offset)?)
            .unwrap_or_default();

        let (class_ptrs, class_count) =
            read_utlvector(reader, scope + layout.scope_classes_offset, layout.vector_count_offset)?;

        for j in 0..class_count {
            let class = reader.read_ptr(class_ptrs + j * 8)?;
            if class == 0 {
                continue;
            }

            let name = reader
                .read_cstring(reader.read_ptr(class + layout.class_name_offset)?)
                .unwrap_or_default();
            if name.is_empty() {
                continue;
            }

            let module = reader
                .read_cstring(reader.read_ptr(class + layout.class_module_name_offset)?)
                .unwrap_or_else(|_| scope_name.clone());
            let size = reader.read_u32(class + layout.class_size_offset).unwrap_or(0);

            let (field_ptr, field_count) =
                read_utlvector(reader, class + layout.class_fields_offset, layout.vector_count_offset)?;

            let mut fields = Vec::new();
            for k in 0..field_count {
                let f = field_ptr + k * layout.field_stride;
                let fname = reader
                    .read_cstring(reader.read_ptr(f + layout.field_name_offset)?)
                    .unwrap_or_default();
                if fname.is_empty() {
                    continue;
                }
                let ftype = reader
                    .read_cstring(reader.read_ptr(f + layout.field_type_offset)?)
                    .unwrap_or_default();
                let foff = reader.read_u32(f + layout.field_offset_offset).unwrap_or(0);
                fields.push(SchemaField {
                    name: fname,
                    type_name: ftype,
                    offset: foff,
                });
            }

            classes.push(SchemaClass {
                module: if module.is_empty() { scope_name.clone() } else { module },
                name,
                size,
                fields,
            });
        }
    }

    Ok(classes)
}
