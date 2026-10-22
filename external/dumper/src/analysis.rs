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
/// referencing instruction (see [`Signature::resolve_rva`]). When `rip_offset`
/// is `None` the match start itself is the target (used to resolve a function
/// entry, e.g. `cbuf_add_text`).
///
/// Entries are grouped by owning module; `resolve_known` filters on the module
/// passed to the dumper's `--module`.
///
/// These are *not* guessed: every one was read out of the build-25815307
/// analysis and (for client.dll) live-verified. See `docs/re/client-dll.md`
/// and `docs/re/engine2.md` for the anchor → steps → address recipe behind
/// each.
pub fn known_signatures() -> Vec<Signature> {
    let sig =
        |module: &str, name: &str, pattern: &str, rip_offset: Option<usize>, instr_len: Option<usize>| {
            Signature {
                name: name.to_string(),
                module: module.to_string(),
                pattern: Pattern::parse(pattern).expect("literal signature pattern is valid"),
                rip_offset,
                instr_len,
            }
        };
    vec![
        // ---- client.dll ---------------------------------------------------
        // `CGameEntitySystem*` global (InitEntity2NetworkClasses).
        sig(
            "client.dll",
            "game_entity_system",
            "48 8B C8 E8 ?? ?? ?? ?? 48 8B 1D ?? ?? ?? ??",
            Some(11),
            Some(15),
        ),
        // Entity identity list (pawn accessor: `mov r8d, [r8+0x6bc]` …).
        sig(
            "client.dll",
            "entity_list",
            "45 8B 80 BC 06 00 00 41 83 F8 FF 74 4D 4C 8B 0D ?? ?? ?? ??",
            Some(16),
            Some(20),
        ),
        // `CGameRules*` global (CGameRules::CGameRules store).
        sig(
            "client.dll",
            "game_rules",
            "8B 0D ?? ?? ?? ?? 48 89 1D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 84 C0",
            Some(9),
            Some(13),
        ),
        // Per-slot controller array (controller accessor tail).
        sig(
            "client.dll",
            "local_player_controllers",
            "8B 08 48 63 C1 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1",
            Some(8),
            Some(12),
        ),
        // World-to-screen (P x V) matrix array (view-matrix getter).
        sig(
            "client.dll",
            "view_matrix",
            "48 63 C1 48 8D 0D ?? ?? ?? ?? 48 C1 E0 06 48 03 C1 C3",
            Some(6),
            Some(10),
        ),
        // `IGameEventManager2*` (the static `CGameEventManager`) — the manager
        // the game registers its `IGameEventListener2` listeners with
        // (`docs/re/client-dll.md` §"Game events"). Anchor: the static-init store
        // `DAT_182542908 = &static CGameEventManager` in `FUN_1800be760`.
        sig(
            "client.dll",
            "game_event_manager",
            "48 C7 05 ?? ?? ?? ?? 00 00 C8 00 75 07 48 89 0D ?? ?? ?? ?? E8",
            Some(16),
            Some(20),
        ),
        // The case-insensitive `MurmurHash2` used to hash game-event field names
        // (`FUN_180165ce0`); callers pass `seed = len ^ 0x31415926`. The payload
        // calls it to build the `{hash, pad, str}` key the `IGameEvent` getters
        // take (`docs/re/client-dll.md` §"Game events").
        sig(
            "client.dll",
            "event_key_hash",
            "48 83 EC 28 45 8B D0 4C 8B C9 48 83 FA 04 0F 82 ?? ?? ?? ?? 0F B6 09 48 89 5C 24 20",
            None,
            None,
        ),
        // `CCSGOInput::CreateMove` — the per-tick input step that receives the
        // `CUserCmd*` as its third argument (`rcx = CCSGOInput*`, `edx = int`,
        // `r8 = CUserCmd*`). `docs/re/client-dll.md` §"Input path (CUserCmd)".
        // Resolve the match start (function entry, no RIP-relative operand).
        sig(
            "client.dll",
            "create_move",
            "48 8B C4 4C 89 40 ?? 48 89 48 ?? 55 53 57",
            None,
            None,
        ),
        // `CCSGOInput` vtable slot 5 — the newer `CreateMove(CCSGOInput*, int slot,
        // bool active)`; fetches the current command via `GetUserCmd`. Resolve the
        // match start (function entry).
        sig(
            "client.dll",
            "create_move_input",
            "85 D2 0F 85 ?? ?? ?? ?? 48 8B C4 44 88 40",
            None,
            None,
        ),
        // Static `CCSGOInput` singleton object (`0x182572460`); its vtable
        // (`0x181c97b28`, slot 5 = `create_move_input`) is `*(ccsgo_input)`.
        // Anchor: the static init `FUN_1800ee570`, which does
        // `LEA RCX,[object]; …; LEA RAX,[vtable]; …; MOV [object], RAX`. The
        // singleton pointer used by the game is `dwCSGOInput` (RVA `0x2233d10`).
        sig(
            "client.dll",
            "ccsgo_input",
            "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C9 C7 05 ?? ?? ?? ?? FF FF FF FF 48 8D 05 ?? ?? ?? ?? 48 89 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??",
            Some(3),
            Some(7),
        ),
        // `CUserCmdArray*` global the game's `GetUserCmd` chain reads (anchor:
        // `MOV RCX,[global]; CALL get_user_cmd_tick; …` inside `create_move_input`).
        sig(
            "client.dll",
            "user_cmd_array",
            "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B CF 4C 8B F8",
            Some(3),
            Some(7),
        ),
        // The `GetUserCmd` chain (`docs/re/client-dll.md` §"Input path"):
        // `get_user_cmd_tick(controller, int* out)`, `get_user_cmd_array(array*,
        // tick)`, `get_user_cmd_by_sequence(controller, seq) -> CUserCmd*`.
        sig(
            "client.dll",
            "get_user_cmd_tick",
            "48 83 EC ?? 4C 8B 0D ?? ?? ?? ?? 4C 8B DA",
            None,
            None,
        ),
        sig(
            "client.dll",
            "get_user_cmd_array",
            "48 89 4C 24 ?? 41 56 41 57",
            None,
            None,
        ),
        sig(
            "client.dll",
            "get_user_cmd_by_sequence",
            "40 53 48 83 EC ?? 8B DA E8 ?? ?? ?? ?? 4C 8B C0",
            None,
            None,
        ),
        // Protobuf `RepeatedPtrField<T>::Add(field, element)` used to append a
        // `CSubtickMoveStep` to `CBaseUserCmdPB.subtick_moves` (and an
        // `CSGOInputHistoryEntryPB` to `input_history`). `field` is the field
        // base; the element must already be allocated in the command's arena
        // (see `docs/re/subtick.md` §"Open questions").
        sig(
            "client.dll",
            "repeated_ptr_add",
            "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 48 8B FA 48 8B 49 10 48 85 C9 74 07 8B 01 3B 43 0C 75 13",
            None,
            None,
        ),
        // `CSubtickMoveStep` allocator (`FUN_1804f7120`) — the "get or create"
        // the command's `add_subtick_moves()` uses. Its own code is identical to
        // every other `0x38`-message allocator, so anchor on the call site in
        // `create_move_input` (`E8` call, resolve the rel32). See
        // `docs/re/subtick.md`.
        sig(
            "client.dll",
            "subtick_step_alloc",
            "E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8 ?? ?? ?? ?? 48 8B C8",
            Some(1),
            Some(5),
        ),
        // ---- engine2.dll --------------------------------------------------
        // `CInputService` singleton (`DAT_1808dbf20`), anchor on
        // `LEA RCX, [g_InputService]; MOV EDX, 5; CALL Cbuf_AddText` inside
        // `CInputService::OnClientProcessInput` (`docs/re/engine2.md`).
        sig(
            "engine2.dll",
            "input_service",
            "38 00 0F 84 ?? ?? ?? ?? 45 33 C9 48 8D 0D ?? ?? ?? ?? BA 05 00 00 00 E8 ?? ?? ?? ??",
            Some(14),
            Some(18),
        ),
        // `Cbuf_AddText` wrapper `FUN_1801b8790` — resolve the match start
        // (no RIP-relative operand).
        sig(
            "engine2.dll",
            "cbuf_add_text",
            "48 83 EC 58 F2 0F 10 05 ?? ?? ?? ?? 33 C0 48 89 44 24 48 F2 0F 11 44 24 40 88 44 24 38 88 44 24 30 48 89 44 24 28 89 44 24 20 E8 ?? ?? ?? ?? 48 83 C4 58 C3",
            None,
            None,
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
