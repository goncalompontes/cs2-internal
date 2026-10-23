//! Analysis layer: signature registry and resolved offsets/interfaces.
//!
//! This is the seam between the raw scanner and the generated SDK consumed by
//! the C++ payload. It starts small and grows as analysis progresses.

use std::collections::BTreeMap;

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
        // ---- the local animfix (`docs/re/animfix.md`) ---------------------
        // The per-class netvar **codec** of `CCS2PawnGraphController`: it
        // decodes the **server's** replicated AG2 graph inputs into the
        // controller's slots (the destination is its first argument). Its
        // 14-byte prologue is *not* unique in the module, so the pattern
        // carries the two instructions that follow it (`lea rbp, [rsp-0x610]`
        // and `sub rsp, 0x710`), which are. RVA on 659940c1: `0x8d9740`.
        sig(
            "client.dll",
            "animfix_graph_codec",
            "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F0 F9 FF FF 48 81 EC 10 07 00 00",
            None,
            None,
        ),
        // The AG2 **parameter setter** — the one place a graph parameter's
        // value is written (`*(float *)(*(longlong *)(node + 0x10) + index * 8
        // + 0x18) = value`). The client's prediction pushes the local player's
        // values through it.
        //
        // It is **not hookable**: its third prologue instruction is a
        // RIP-relative load whose displacement cannot be relocated to the
        // trampoline (the module is mapped high, the trampoline low), so the
        // animfix re-applies the server's values in the anim tick instead.
        // The signature is kept because the host test (`control-animfix`) uses
        // it as the **counter-example**: the relocator rewrites this
        // prologue's displacement (so the hook only works when the module and
        // the trampoline live close together), while the anim tick's
        // prologue needs no relocation at all. The pattern is unique; the
        // RIP-relative displacement is relocated by the inline hook. RVA on
        // 659940c1: `0x8492a0`; on the 2026-10-23 build: `0x8494d0`.
        //
        // The displacement is **part of the pattern** (it is the RIP-relative
        // load's disp32, so it changes with any shift in the module's layout):
        // the first pattern hard-coded `A0 0F 3C 01` from `659940c1` and stopped
        // matching when the game updated. If it stops matching again, search for
        // the 12 bytes before the displacement (`F3 41 0F 10 08 4C 8B C9 F3 0F
        // 10 05`, unique) and read the four bytes that follow.
        sig(
            "client.dll",
            "animfix_param_setter",
            "F3 41 0F 10 08 4C 8B C9 F3 0F 10 05 10 21 3C 01",
            None,
            None,
        ),
        // `CAnimGraphGameSystem::AnimTickUpdate` — the animation driver, and
        // the place the AG2 graph is evaluated. It is **inferred** to run
        // after the client's prediction in the same tick (`docs/re/animfix.md`
        // §9.3 — inferred, not measured), so a correction written here is
        // the one the pose is computed from; the fix does not depend on the
        // inference being right, because the correction is written at the
        // **start** of the anim tick and the evaluation happens **inside**
        // it. Its 15-byte prologue is RIP-free (the relocator accepts it at
        // any address distance). The second argument is a **flag** (0 = the
        // server tick, otherwise the client tick), not a tick count. RVA on
        // 659940c1: `0x35aeb0`.
        sig(
            "client.dll",
            "animfix_anim_tick",
            "89 54 24 10 48 89 4C 24 08 55 57 41 54 41 55 41 57 48 8D AC 24",
            None,
            None,
        ),
        // ---- the camera split (`docs/re/camera-split.md`) ---------------------
        // `C_BasePlayerPawn`'s **eye-angle copy** — the virtual that takes the
        // view angles (the command's own, through the camera services at pawn
        // `+0x1328`) and stores them into the pawn's eye angles
        // (`m_angEyeAngles`, pawn `+0x3600`). The camera split hooks it: the
        // engine copies the command's angles into the eye angles every frame,
        // and the payload puts the camera back right after (`docs/re/strafing.md`
        // §"The split").
        //
        // The anchor is the prologue plus the **virtual call at vtable +0x570**
        // (the view-angle getter) — the function's entry, so the match start is
        // the RVA. Resolved RVA on build 659940c1: `0x8cb9b0`.
        sig(
            "client.dll",
            "eye_angles_copy",
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56 41 57 48 83 EC 30 48 8B 01 48 8D 54 24 20 4C 8B F1 FF 90 70 05 00 00",
            None,
            None,
        ),
        // `CCSGOInput`'s **view-angle setter** — `FUN_180b80e70(input, slot,
        // angles)`: it stores the angles the movement path hands it into the
        // local view angles (`input + slot*0x928 + 0x688`; slot 0 is the local
        // player, and when the per-slot history is empty the write lands
        // straight at `input + 0x688`). This is the write the camera reads and
        // the write the camera split replaces with the player's own angles.
        // Resolved RVA on build 659940c1: `0xb80e70`.
        sig(
            "client.dll",
            "view_angles_setter",
            "85 D2 75 3D 48 63 81 50 0B 00 00 F2 41 0F 10 00 85 C0 74 ?? 48 69 D0 40 04 00 00 48 03 91 58 0B 00 00",
            None,
            None,
        ),
        // ---- occlusion / physics query (docs/re/occlusion.md) -------------
        // `CGamePhysicsQueryInterface` singleton — `*(client_base + 0x2227058)`.
        // Anchor on a load in the query's own call path (a `MOV RCX, [singleton]`
        // in the trace caller the occlusion RE was pinned from). Resolved RVA on
        // build 659940c1: `0x2227058`.
        sig(
            "client.dll",
            "physics_query_interface",
            "48 8B 0D ?? ?? ?? ?? 0F 57 C0 48 89",
            Some(3),
            Some(7),
        ),
        // The static default trace filter (`client_base + 0x2227070`) — anchor on the
        // `LEA RAX, [filter]` in the function that builds the query filter inline
        // (`docs/re/occlusion.md` §"the singleton + call form"). RVA: `0x2227070`.
        sig(
            "client.dll",
            "physics_query_filter",
            "48 8D 05 ?? ?? ?? ?? 4D 8B CE 48 8D",
            Some(3),
            Some(7),
        ),
        // `CGamePhysicsQueryInterface::EntitiesInBox` entry (`client_base +
        // 0x9dbd20`) — resolve the match start (a function entry; the prologue is
        // distinctive enough to be unique). RVA: `0x9dbd20`.
        sig(
            "client.dll",
            "entities_in_box",
            "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 48 89 4C 24 08 55 41 54 41 55 41 56 41 57 48 8D AC 24 10 CC FF",
            None,
            None,
        ),
        // `CGamePhysicsQueryInterface::EntitiesAlongLinearSweep` entry
        // (`client_base + 0x9db9a0`) — the swept enumeration the occlusion layer
        // intends to use instead of a box (`docs/re/occlusion.md` §"the enumeration
        // primitive"). Resolve the match start (function-entry prologue).
        sig(
            "client.dll",
            "entities_along_linear_sweep",
            "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 41 55 41 56 41 57 48 8D AC 24 30",
            None,
            None,
        ),
        // `CGamePhysicsQueryInterface::TraceLineIntervals` — the call the real bullet
        // makes, and the probe's engine answer (`docs/re/bullet-trace.md` §"The probe's
        // call form"). It was **not** curated: the probe used the hard-coded
        // `0xA19E90` fallback, which went stale with the 2026-10-23 update and made
        // the probe fault *inside* the engine (the guard recovered the access
        // violation, but the process still died on the corrupted stack). The 32-byte
        // prologue is unique in client.dll (one match); RVA on this build: `0xa1acc0`.
        sig(
            "client.dll",
            "trace_line_intervals",
            "4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 49 89 7B 20 41 54 41 56 41 57 48 81 EC 90 00 00 00",
            None,
            None,
        ),
        // `SurfacePropertiesList` manager (`DAT_18254e100`, `client_base + 0x254e100`),
        // the game's surface-property table (`CGameSurfaceProperties`, docs/re/
        // penetration.md). Anchor on its resolver `FUN_1809e3560`
        // (`*(int*)(param+0x10)*0x20 + [list+0x28]`); the `MOV RCX,[list]` load is
        // unique. RVA on 659940c1: `0x254e100`.
        sig(
            "client.dll",
            "surface_properties_list",
            "48 8B 0D ?? ?? ?? ?? 48 C1 E0 05 48 03 41 28 C3",
            Some(3),
            Some(7),
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
        // ---- scenesystem.dll ----------------------------------------------
        // `CAnimatableSceneObjectDesc::DrawPrimitive` — the scene system's draw,
        // the hook target of the chams. Resolved as a function entry (the match
        // start): the prologue is `MOV [RSP+0x20], R9; MOV [RSP+0x10], RDX; PUSH
        // RBP; PUSH RBX; PUSH RSI; PUSH RDI; PUSH R12; PUSH R13; PUSH R15` — no
        // RIP-relative operand and no relative branch in the first 14 bytes, so
        // the repo's inline hook can displace them.
        //
        // The 20-byte prologue is unique in `scenesystem.dll` on the build the
        // RE measured (sha256 040eb851…, PE timestamp 0x6ACFCD4F); the 12-,
        // 14-, 16- and 20-byte prefixes are each unique as well.
        // `docs/re/chams.md` §8 records the measurement.
        sig(
            "scenesystem.dll",
            "scene_draw_primitive",
            "4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57",
            None,
            None,
        ),
    ]
}

/// The modules that have curated signatures, sorted and de-duplicated.
///
/// Derived from [`known_signatures`] itself, so a signature added for a new
/// module (the chams' `scenesystem.dll`) shows up here with no edit anywhere
/// else: the offline offset resolver asks the dumper for this list and works
/// through it.
pub fn signature_modules() -> Vec<String> {
    let mut modules: Vec<String> = known_signatures()
        .iter()
        .map(|signature| signature.module.clone())
        .collect();
    modules.sort();
    modules.dedup();
    modules
}

/// How many curated signatures the registry holds, per module.
///
/// The offline offset resolver compares the number of signatures a module
/// resolved (plus the number that did **not**) against this count: a module
/// that was not really scanned - a wrong file, a module name that does not
/// match - shows up as a shortfall instead of a silent success.
pub fn signature_counts() -> BTreeMap<String, usize> {
    let mut counts: BTreeMap<String, usize> = BTreeMap::new();
    for signature in known_signatures() {
        *counts.entry(signature.module.clone()).or_insert(0) += 1;
    }
    counts
}

/// How many curated signatures the registry holds for one module.
pub fn signature_count(module: &str) -> usize {
    known_signatures()
        .iter()
        .filter(|signature| signature.module == module)
        .count()
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;

    /// The modules the curated signatures name. The offline offset resolver
    /// asks the dumper for this list and works through it, so the modules the
    /// payload's offsets live in have to be in it.
    #[test]
    fn signature_modules_name_the_curated_modules() {
        let modules = signature_modules();
        assert!(
            modules.contains(&"client.dll".to_string()),
            "client.dll has curated signatures: {modules:?}"
        );
        assert!(
            modules.contains(&"engine2.dll".to_string()),
            "engine2.dll has curated signatures: {modules:?}"
        );
    }

    /// The list is **derived** from the registry, so it cannot drift from it:
    /// a signature added for a new module (the chams' `scenesystem.dll`)
    /// shows up without an edit here.
    #[test]
    fn signature_modules_match_the_registry() {
        let from_registry: Vec<String> = known_signatures()
            .iter()
            .map(|signature| signature.module.clone())
            .collect::<BTreeSet<_>>()
            .into_iter()
            .collect();
        assert_eq!(signature_modules(), from_registry);
    }

    /// The counts agree with the registry: they add up to the number of
    /// signatures, and every module the list names has at least one.
    #[test]
    fn signature_counts_match_the_registry() {
        let counts = signature_counts();
        let total: usize = counts.values().sum();
        assert_eq!(total, known_signatures().len());
        assert_eq!(
            counts.keys().cloned().collect::<Vec<String>>(),
            signature_modules()
        );
        for (module, count) in &counts {
            assert!(*count > 0, "{module} has curated signatures");
            assert_eq!(*count, signature_count(module));
        }
    }
}
