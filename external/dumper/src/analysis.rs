//! Analysis layer: signature registry and resolved offsets/interfaces.
//!
//! This is the seam between the raw scanner and the generated SDK consumed by
//! the C++ payload. It starts small and grows as analysis progresses.

use crate::pattern::{Pattern, Signature};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum OffsetSource {
    Signature,
    Schema,
    Manual,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Offset {
    pub name: String,
    pub module: String,
    pub rva: usize,
    pub source: OffsetSource,
}

/// Curated signatures that re-resolve the target globals across builds.
///
/// Each entry is anchored on a distinctive instruction in the function that
/// produces the global and resolves the global through its RIP-relative
/// operand. `rip_offset` is the displacement's byte offset within the pattern
/// and `instr_len` is the distance from the pattern start to the end of the
/// referencing instruction (see [`Signature::resolve_rva`]).
///
/// These are *not* guessed: every one was read out of the build-25815307
/// analysis and live-verified. See `docs/re/client-dll.md` for the full
/// anchor → steps → address recipe behind each.
pub fn known_signatures() -> Vec<Signature> {
    let sig = |name: &str, pattern: &str, rip_offset: usize, instr_len: usize| Signature {
        name: name.to_string(),
        module: "client.dll".to_string(),
        pattern: Pattern::parse(pattern).expect("literal signature pattern is valid"),
        rip_offset: Some(rip_offset),
        instr_len: Some(instr_len),
    };
    vec![
        // `CGameEntitySystem*` global (InitEntity2NetworkClasses).
        sig(
            "game_entity_system",
            "48 8B C8 E8 ?? ?? ?? ?? 48 8B 1D ?? ?? ?? ??",
            11,
            15,
        ),
        // Entity identity list (pawn accessor: `mov r8d, [r8+0x6bc]` …).
        sig(
            "entity_list",
            "45 8B 80 BC 06 00 00 41 83 F8 FF 74 4D 4C 8B 0D ?? ?? ?? ??",
            16,
            20,
        ),
        // `CGameRules*` global (CGameRules::CGameRules store).
        sig(
            "game_rules",
            "8B 0D ?? ?? ?? ?? 48 89 1D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 84 C0",
            9,
            13,
        ),
        // Per-slot controller array (controller accessor tail).
        sig(
            "local_player_controllers",
            "8B 08 48 63 C1 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1",
            8,
            12,
        ),
        // World-to-screen (P x V) matrix array (view-matrix getter).
        sig(
            "view_matrix",
            "48 63 C1 48 8D 0D ?? ?? ?? ?? 48 C1 E0 06 48 03 C1 C3",
            6,
            10,
        ),
    ]
}

/// Resolve every curated signature that targets `module` against a mapped
/// module image. A signature that does not match is reported as absent so a
/// build that moved or dropped a global is visible rather than silent.
pub fn resolve_known(module: &str, bytes: &[u8], base: usize) -> (Vec<Offset>, Vec<String>) {
    let mut resolved = Vec::new();
    let mut missing = Vec::new();
    for signature in known_signatures() {
        if signature.module != module {
            continue;
        }
        match signature.pattern.find_first(bytes) {
            Some(hit) => match signature.resolve_rva(base, base + hit, bytes) {
                Some(rva) => resolved.push(Offset {
                    name: signature.name,
                    module: signature.module,
                    rva,
                    source: OffsetSource::Signature,
                }),
                None => missing.push(signature.name),
            },
            None => missing.push(signature.name),
        }
    }
    (resolved, missing)
}
