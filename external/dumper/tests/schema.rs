//! Fixture test for the schema walker — no game required.
//!
//! Builds a synthetic schema graph (CSchemaSystem -> type scope -> class hash
//! map -> class -> fields) using the same [`SchemaLayout`] the walker uses, so
//! the traversal/emit logic is exercised end to end.

use cs2_dumper::reader::BytesReader;
use cs2_dumper::schema::{self, SchemaLayout};

const BASE: usize = 0x2000_0000;

fn w64(data: &mut [u8], off: usize, v: u64) {
    data[off..off + 8].copy_from_slice(&v.to_le_bytes());
}

fn w32(data: &mut [u8], off: usize, v: u32) {
    data[off..off + 4].copy_from_slice(&v.to_le_bytes());
}

fn w16(data: &mut [u8], off: usize, v: u16) {
    data[off..off + 2].copy_from_slice(&v.to_le_bytes());
}

fn wstr(data: &mut [u8], off: usize, s: &str) {
    data[off..off + s.len()].copy_from_slice(s.as_bytes());
}

#[test]
fn dumps_synthetic_schema() {
    let layout = SchemaLayout::default();
    let mut data = vec![0u8; 0x10000];

    // CSchemaSystem @0x0: type scopes count=1, data ptr -> scopes array @0x1000.
    w16(&mut data, layout.type_scopes_count_offset, 1);
    w64(
        &mut data,
        layout.type_scopes_data_offset,
        (BASE + 0x1000) as u64,
    );
    w64(&mut data, 0x1000, (BASE + 0x2000) as u64); // scopes[0]

    // scope @0x2000: inline name "client.dll".
    wstr(&mut data, 0x2000 + layout.scope_name_offset, "client.dll");

    // One class in bucket 0: bucket head -> node @0x3000.
    let bucket0 = 0x2000 + layout.scope_buckets_offset;
    w64(
        &mut data,
        bucket0 + layout.scope_bucket_head_offset,
        (BASE + 0x3000) as u64,
    );

    // node @0x3000: next=0, class -> 0x4000.
    w64(&mut data, 0x3000 + layout.scope_node_next_offset, 0);
    w64(
        &mut data,
        0x3000 + layout.scope_node_class_offset,
        (BASE + 0x4000) as u64,
    );

    // class @0x4000
    wstr(&mut data, 0x5000, "C_BaseEntity");
    wstr(&mut data, 0x5100, "client");
    wstr(&mut data, 0x7000, "m_iHealth");
    wstr(&mut data, 0x7100, "m_iTeamNum");
    wstr(&mut data, 0x8000, "int32");

    w64(
        &mut data,
        0x4000 + layout.class_name_offset,
        (BASE + 0x5000) as u64,
    );
    w64(
        &mut data,
        0x4000 + layout.class_module_name_offset,
        (BASE + 0x5100) as u64,
    );
    w32(&mut data, 0x4000 + layout.class_size_offset, 0x1234);
    w32(&mut data, 0x4000 + layout.class_field_count_offset, 2);
    w64(
        &mut data,
        0x4000 + layout.class_fields_offset,
        (BASE + 0x6000) as u64,
    );

    // fields @0x6000 (stride 0x20)
    let f0 = 0x6000;
    let f1 = 0x6000 + layout.field_stride;
    w64(
        &mut data,
        f0 + layout.field_name_offset,
        (BASE + 0x7000) as u64,
    );
    w64(
        &mut data,
        f0 + layout.field_type_offset,
        (BASE + 0x8000) as u64,
    );
    w32(&mut data, f0 + layout.field_offset_offset, 0x34c);

    w64(
        &mut data,
        f1 + layout.field_name_offset,
        (BASE + 0x7100) as u64,
    );
    w64(
        &mut data,
        f1 + layout.field_type_offset,
        (BASE + 0x8000) as u64,
    );
    w32(&mut data, f1 + layout.field_offset_offset, 0x3c8);

    // CSchemaType "int32" @0x8000: m_Name at type_name_offset -> 0x8100.
    wstr(&mut data, 0x8100, "int32");
    w64(
        &mut data,
        0x8000 + layout.type_name_offset,
        (BASE + 0x8100) as u64,
    );

    let reader = BytesReader::new(BASE, data);
    let classes = schema::dump(&reader, BASE, &layout).unwrap();

    assert_eq!(classes.len(), 1);
    let c = &classes[0];
    assert_eq!(c.name, "C_BaseEntity");
    assert_eq!(c.module, "client.dll");
    assert_eq!(c.size, 0x1234);
    assert_eq!(c.fields.len(), 2);
    assert_eq!(c.fields[0].name, "m_iHealth");
    assert_eq!(c.fields[0].type_name, "int32");
    assert_eq!(c.fields[0].offset, 0x34c);
    assert_eq!(c.fields[1].name, "m_iTeamNum");
    assert_eq!(c.fields[1].offset, 0x3c8);
}
