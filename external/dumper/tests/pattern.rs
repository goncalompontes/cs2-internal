//! Fixture-based tests for the pattern engine — no game process required.

use cs2_dumper::pattern::{read_i32, resolve_rip_relative, Pattern, PatternError};

#[test]
fn parse_rejects_empty_and_bad_tokens() {
    assert_eq!(Pattern::parse("").unwrap_err(), PatternError::Empty);
    assert_eq!(
        Pattern::parse("48 zz").unwrap_err(),
        PatternError::BadToken("zz".into())
    );
    assert_eq!(
        Pattern::parse("4").unwrap_err(),
        PatternError::BadToken("4".into())
    );
}

#[test]
fn finds_exact_and_wildcard_matches() {
    let hay = [0x11u8, 0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0x90];

    let exact = Pattern::parse("48 8B 05").unwrap();
    assert_eq!(exact.find_first(&hay), Some(1));
    assert_eq!(exact.find_all(&hay), vec![1]);

    let wild = Pattern::parse("48 8B 05 ?? ?? ?? ??").unwrap();
    assert_eq!(wild.find_all(&hay), vec![1]);
    assert_eq!(wild.len(), 7);
}

#[test]
fn match_at_bounds_are_checked() {
    let hay = [0x48u8, 0x8B];
    let p = Pattern::parse("48 8B 05").unwrap();
    assert!(!p.matches_at(&hay, 0));
    assert_eq!(p.find_first(&hay), None);
}

#[test]
fn rip_relative_resolution() {
    // instr at 0x1000, length 7, disp32 = 0x10 -> target 0x1017
    assert_eq!(resolve_rip_relative(0x1000, 7, 0x10), 0x1017);

    // decode a negative displacement from a buffer
    let buf = [0x48u8, 0x8B, 0x05, 0xF0, 0xFF, 0xFF, 0xFF];
    let disp = read_i32(&buf, 3).unwrap();
    assert_eq!(disp, -16);
    // instr at 0x2000, len 7 -> 0x2000 + 7 - 16 = 0x1FF7
    assert_eq!(resolve_rip_relative(0x2000, 7, disp), 0x1FF7);
}

#[test]
fn signature_resolves_rva_with_rip() {
    use cs2_dumper::Signature;

    // Module at 0x140000000, match at base+0x10, rip disp at offset 3.
    let base = 0x1_4000_0000usize;
    let mut bytes = vec![0u8; 0x40];
    let match_off = 0x10;
    // disp32 = target(0x140000100) - (match_addr + 7)
    let match_addr = base + match_off;
    let target = base + 0x100;
    let disp = (target as i64 - (match_addr as i64 + 7)) as i32;
    bytes[match_off + 3..match_off + 7].copy_from_slice(&disp.to_le_bytes());

    let sig = Signature {
        name: "test_offset".into(),
        module: "client.dll".into(),
        pattern: Pattern::parse("48 8B 05 ?? ?? ?? ??").unwrap(),
        rip_offset: Some(3),
        instr_len: Some(7),
    };

    let rva = sig.resolve_rva(base, match_addr, &bytes).unwrap();
    assert_eq!(rva, 0x100);
}

#[test]
fn curated_signatures_resolve_offsets_offline() {
    use cs2_dumper::analysis::{resolve_known, OffsetSource};

    // (name, pattern, rip_offset, instr_len, target_rva) — mirrors
    // `analysis::known_signatures()` and the build-25815307 RVAs.
    let cases: &[(&str, &str, usize, usize, usize)] = &[
        (
            "game_entity_system",
            "48 8B C8 E8 ?? ?? ?? ?? 48 8B 1D ?? ?? ?? ??",
            11,
            15,
            0x2711598,
        ),
        (
            "entity_list",
            "45 8B 80 BC 06 00 00 41 83 F8 FF 74 4D 4C 8B 0D ?? ?? ?? ??",
            16,
            20,
            0x2366770,
        ),
        (
            "game_rules",
            "8B 0D ?? ?? ?? ?? 48 89 1D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 84 C0",
            9,
            13,
            0x255be80,
        ),
        (
            "local_player_controllers",
            "8B 08 48 63 C1 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1",
            8,
            12,
            0x25338a8,
        ),
        (
            "view_matrix",
            "48 63 C1 48 8D 0D ?? ?? ?? ?? 48 C1 E0 06 48 03 C1 C3",
            6,
            10,
            0x2561cd0,
        ),
    ];

    let base = 0x180000000usize;
    let mut bytes = vec![0u8; 0x400];
    let mut expected = std::collections::BTreeMap::new();
    let mut off = 0x10;
    for (name, text, rip_offset, instr_len, target_rva) in cases {
        let toks: Vec<Option<u8>> = text
            .split_whitespace()
            .map(|t| match t {
                "?" | "??" => None,
                hex => Some(u8::from_str_radix(hex, 16).unwrap()),
            })
            .collect();
        for (i, tok) in toks.iter().enumerate() {
            bytes[off + i] = tok.unwrap_or(0);
        }
        let match_addr = base + off;
        let target_addr = base + target_rva;
        let disp = (target_addr as i64 - (match_addr as i64 + *instr_len as i64)) as i32;
        bytes[off + rip_offset..off + rip_offset + 4].copy_from_slice(&disp.to_le_bytes());
        expected.insert(name.to_string(), *target_rva);
        off += toks.len() + 8;
    }

    let (resolved, missing) = resolve_known("client.dll", &bytes, base);
    assert!(missing.is_empty(), "unresolved: {missing:?}");
    assert_eq!(resolved.len(), cases.len());
    for offset in &resolved {
        assert_eq!(offset.module, "client.dll");
        assert_eq!(offset.source, OffsetSource::Signature);
        assert_eq!(offset.rva, expected[&offset.name], "{}", offset.name);
    }
}
