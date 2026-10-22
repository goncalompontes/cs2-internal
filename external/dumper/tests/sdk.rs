//! Fixture tests for SDK header generation — no game required.

use std::path::PathBuf;

use cs2_dumper::analysis::{Offset, OffsetSource};
use cs2_dumper::interfaces::Interface;
use cs2_dumper::schema::{SchemaClass, SchemaField};
use cs2_dumper::sdk::{self, SdkInputs};

fn interface(name: &str, node: usize, create: usize) -> Interface {
    Interface {
        name: name.to_string(),
        target: String::new(),
        rva: node,
        create_rva: create,
    }
}

fn class(module: &str, name: &str, size: u32, fields: &[(&str, &str, u32)]) -> SchemaClass {
    SchemaClass {
        module: module.to_string(),
        name: name.to_string(),
        size,
        fields: fields
            .iter()
            .map(|(n, t, o)| SchemaField {
                name: n.to_string(),
                type_name: t.to_string(),
                offset: *o,
                target_name: String::new(),
            })
            .collect(),
        target_name: String::new(),
    }
}

#[test]
fn renders_interfaces_table_and_lookup() {
    let items = vec![
        interface("Source2Client002", 0x1000, 0x2000),
        interface("GameClientExports001", 0x1018, 0x2040),
    ];
    let text = sdk::render_interfaces(&items);

    assert!(text.contains("std::array<InterfaceInfo, 2> INTERFACES"));
    assert!(text.contains("\"Source2Client002\", \"\", 0x1000, 0x2000"));
    assert!(text.contains("\"GameClientExports001\", \"\", 0x1018, 0x2040"));
    assert!(text.contains("find_interface"));
    assert!(text.contains("find_interface_by_target"));
    assert!(text.contains("#pragma once"));
}

#[test]
fn renders_empty_interfaces_compiles_as_zero_array() {
    let text = sdk::render_interfaces(&[]);
    assert!(text.contains("std::array<InterfaceInfo, 0> INTERFACES"));
}

#[test]
fn renders_offsets_with_named_constants_and_source() {
    let items = vec![
        Offset {
            name: "entity_system".to_string(),
            module: "client.dll".to_string(),
            rva: 0x2541b20,
            source: OffsetSource::Manual,
        },
        Offset {
            name: "create_interface".to_string(),
            module: "client.dll".to_string(),
            rva: 0x19c6770,
            source: OffsetSource::Signature,
        },
    ];
    let text = sdk::render_offsets(&items);

    assert!(text.contains("inline constexpr std::uint32_t ENTITY_SYSTEM_RVA = 0x2541b20;"));
    assert!(text.contains("inline constexpr std::uint32_t CREATE_INTERFACE_RVA = 0x19c6770;"));
    assert!(text.contains("OffsetSource::manual"));
    assert!(text.contains("OffsetSource::signature"));
}

#[test]
fn duplicate_offset_names_are_skipped_with_a_comment() {
    let items = vec![
        Offset {
            name: "dup".to_string(),
            module: "a.dll".to_string(),
            rva: 1,
            source: OffsetSource::Manual,
        },
        Offset {
            name: "dup".to_string(),
            module: "b.dll".to_string(),
            rva: 2,
            source: OffsetSource::Manual,
        },
    ];
    let text = sdk::render_offsets(&items);

    assert_eq!(
        text.matches("inline constexpr std::uint32_t DUP_RVA")
            .count(),
        1
    );
    assert!(text.contains("skipped duplicate offset names: dup"));
}

#[test]
fn renders_schema_module_with_fields_and_empty_class() {
    let classes = [
        class(
            "client.dll",
            "CCSPlayerPawn",
            0x1234,
            &[
                ("m_iHealth", "int32", 0x340),
                ("m_lifeState", "uint8", 0x344),
            ],
        ),
        class("client.dll", "CEmpty", 0x8, &[]),
    ];
    let refs: Vec<&SchemaClass> = classes.iter().collect();
    let text = sdk::render_schema_module("client.dll", &refs);

    assert!(text.contains("namespace cs2::sdk::schemas::client_dll"));
    assert!(text.contains("inline constexpr std::string_view MODULE = \"client.dll\";"));
    assert!(text.contains("inline constexpr FieldInfo ccs_player_pawn_fields[] = {"));
    assert!(
        text.contains("{\"m_iHealth\", \"int32\", 0x340, \"\"}")
            || text.contains("\"m_iHealth\", \"int32\", 0x340, \"\"")
    );
    // The empty class uses a default-constructed span, not a zero-length array.
    assert!(text.contains("std::span<const FieldInfo>{}"));
    assert!(text.contains("field_offset"));
    assert!(text.contains("find_class_by_target"));
}

#[test]
fn writes_expected_files_and_aggregates_modules() {
    let dir = unique_temp_dir("cs2-sdk-test");
    let interfaces = vec![interface("Source2Client002", 0x1000, 0x2000)];
    let schema = vec![
        class(
            "client.dll",
            "CCSPlayerPawn",
            0x100,
            &[("m_iHealth", "int32", 0x10)],
        ),
        class("engine2.dll", "CEngine", 0x20, &[]),
    ];
    let offsets = vec![Offset {
        name: "entity_system".to_string(),
        module: "client.dll".to_string(),
        rva: 0x10,
        source: OffsetSource::Manual,
    }];
    let inputs = SdkInputs {
        interfaces: &interfaces,
        schema: &schema,
        offsets: &offsets,
    };

    let written = sdk::write_sdk(&dir, &inputs).expect("write_sdk");

    let names: Vec<String> = written
        .iter()
        .map(|p| {
            p.strip_prefix(&dir)
                .unwrap()
                .to_string_lossy()
                .replace('\\', "/")
        })
        .collect();
    for expected in ["interfaces.hpp", "offsets.hpp", "schemas.hpp"] {
        assert!(names.contains(&expected.to_string()), "missing {expected}");
    }
    assert!(names.iter().any(|n| n.starts_with("schemas/client")));
    assert!(names.iter().any(|n| n.starts_with("schemas/engine2")));

    let aggregator = std::fs::read_to_string(dir.join("schemas.hpp")).unwrap();
    assert!(aggregator.contains("#include \"schemas/client_dll.hpp\""));
    assert!(aggregator.contains("#include \"schemas/engine2_dll.hpp\""));

    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn dumper_json_round_trips_into_the_generator() {
    // The dumper emits these with serde; the generator reads them back. This
    // guards the Deserialize side of the contract.
    let interfaces = vec![interface("Source2Client002", 0x1000, 0x2000)];
    let json = serde_json::to_string(&interfaces).unwrap();
    let back: Vec<Interface> = serde_json::from_str(&json).unwrap();
    assert_eq!(back[0].name, "Source2Client002");
    assert!(sdk::render_interfaces(&back).contains("Source2Client002"));

    let offsets = vec![Offset {
        name: "entity_system".to_string(),
        module: "client.dll".to_string(),
        rva: 0x10,
        source: OffsetSource::Schema,
    }];
    let json = serde_json::to_string(&offsets).unwrap();
    assert!(json.contains("\"schema\""));
    let back: Vec<Offset> = serde_json::from_str(&json).unwrap();
    assert_eq!(back[0].source, OffsetSource::Schema);
}

fn unique_temp_dir(tag: &str) -> PathBuf {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    std::env::temp_dir().join(format!("{tag}-{}-{nanos}", std::process::id()))
}
