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
