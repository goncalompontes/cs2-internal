//! Fixture tests for the symbol renaming pass (ADR-0010).

use cs2_dumper::interfaces::Interface;
use cs2_dumper::rename;
use cs2_dumper::schema::{SchemaClass, SchemaField};

#[test]
fn apply_schema_renames_and_keeps_targets() {
    let mut classes = vec![SchemaClass {
        module: "client.dll".into(),
        name: "C_CSPlayerPawn".into(),
        size: 0x100,
        fields: vec![
            SchemaField {
                name: "m_iHealth".into(),
                type_name: "int32".into(),
                offset: 0x34c,
                target_name: String::new(),
            },
            SchemaField {
                name: "m_pBulletServices".into(),
                type_name: "CCSPlayer_BulletServices*".into(),
                offset: 0x1570,
                target_name: String::new(),
            },
        ],
        target_name: String::new(),
    }];

    rename::apply_schema(&mut classes);

    assert_eq!(classes[0].name, "CsPlayerPawn");
    assert_eq!(classes[0].target_name, "C_CSPlayerPawn");
    assert_eq!(classes[0].fields[0].name, "health");
    assert_eq!(classes[0].fields[0].target_name, "m_iHealth");
    assert_eq!(classes[0].fields[1].name, "bullet_services");
    assert_eq!(classes[0].fields[1].type_name, "CsPlayerBulletServices*");
}

#[test]
fn collision_is_uniquified_within_a_module() {
    let mut classes = vec![
        SchemaClass {
            module: "c".into(),
            name: "C_Foo".into(),
            size: 0,
            fields: vec![],
            target_name: String::new(),
        },
        SchemaClass {
            module: "c".into(),
            name: "C_Foo_t".into(),
            size: 0,
            fields: vec![],
            target_name: String::new(),
        },
    ];
    rename::apply_schema(&mut classes);
    assert_eq!(classes[0].name, "Foo");
    assert_eq!(classes[1].name, "Foo_2");
    assert_eq!(classes[1].target_name, "C_Foo_t");
}

#[test]
fn apply_interfaces_renames_and_keeps_targets() {
    let mut items = vec![Interface {
        name: "Source2Client002".into(),
        target: String::new(),
        rva: 1,
        create_rva: 2,
    }];
    rename::apply_interfaces(&mut items);
    assert_eq!(items[0].target, "Source2Client002");
    assert_ne!(items[0].name, "Source2Client002");
    assert!(items[0]
        .name
        .chars()
        .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_'));
}
