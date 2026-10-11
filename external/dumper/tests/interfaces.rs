//! Fixture tests for the `CreateInterface` resolver — no game required.

use cs2_dumper::interfaces;
use cs2_dumper::reader::BytesReader;

const BASE: usize = 0x1000_0000;

fn w64(data: &mut [u8], off: usize, v: u64) {
    data[off..off + 8].copy_from_slice(&v.to_le_bytes());
}

fn wstr(data: &mut [u8], off: usize, s: &str) {
    data[off..off + s.len()].copy_from_slice(s.as_bytes());
}

#[test]
fn enumerates_linked_interface_list() {
    let mut data = vec![0u8; 0x4000];

    // Interface name strings.
    wstr(&mut data, 0x100, "Source2Client002");
    wstr(&mut data, 0x200, "GameClientExports001");

    // Node A @0x1000 -> next B @0x2000
    w64(&mut data, 0x1000, (BASE + 0x3000) as u64); // create fn inside module
    w64(&mut data, 0x1008, (BASE + 0x100) as u64);
    w64(&mut data, 0x1010, (BASE + 0x2000) as u64);

    // Node B @0x2000 -> end
    w64(&mut data, 0x2000, (BASE + 0x3100) as u64);
    w64(&mut data, 0x2008, (BASE + 0x200) as u64);
    w64(&mut data, 0x2010, 0);

    let reader = BytesReader::new(BASE, data);
    let found = interfaces::enumerate(
        &reader,
        BASE,
        0x4000,
        &["Source2Client002", "GameClientExports001"],
    )
    .unwrap();

    assert_eq!(found.len(), 2);
    let names: Vec<&str> = found.iter().map(|i| i.name.as_str()).collect();
    assert!(names.contains(&"Source2Client002"));
    assert!(names.contains(&"GameClientExports001"));
}

#[test]
fn enumerate_all_finds_list() {
    let mut data = vec![0u8; 0x4000];
    wstr(&mut data, 0x100, "Source2Client002");
    wstr(&mut data, 0x200, "GameClientExports001");

    w64(&mut data, 0x1000, (BASE + 0x3000) as u64);
    w64(&mut data, 0x1008, (BASE + 0x100) as u64);
    w64(&mut data, 0x1010, (BASE + 0x2000) as u64);

    w64(&mut data, 0x2000, (BASE + 0x3100) as u64);
    w64(&mut data, 0x2008, (BASE + 0x200) as u64);
    w64(&mut data, 0x2010, 0);

    let reader = BytesReader::new(BASE, data);
    let found = interfaces::enumerate_all(&reader, BASE, 0x4000).unwrap();
    assert_eq!(found.len(), 2);
    let names: Vec<&str> = found.iter().map(|i| i.name.as_str()).collect();
    assert!(names.contains(&"Source2Client002"));
    assert!(names.contains(&"GameClientExports001"));
}

#[test]
fn walk_stops_on_invalid_head() {
    let data = vec![0u8; 0x1000];
    let reader = BytesReader::new(BASE, data);
    let list = interfaces::walk_list(&reader, BASE, 0x1000, 0);
    assert!(list.is_empty());
}
