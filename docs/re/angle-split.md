# The angle channels, re-derived for the anti-aim (build `659940c1`)

Re-derived 2026-10-21 on the **live** `client.dll`
(`sha256 659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
39 205 528 B, image base `0x180000000`). Every address below is an **RVA**
(`VA − 0x180000000`) unless it is explicitly called a VA; every one was
re-derived on *this* build with the string → xref → decompile path recorded
in the "derivation" column.

**Tooling.** REA (Ghidra 12.1.4) through the integrated MCP tools, target
`client.dll`, `target_id` `b896e6c3-9686-4ba9-b764-bf5b551f8597`. The
signature offsets were cross-checked with the repo's own dumper:

```
external\dumper\target\release\cs2-dumper.exe --file <live client.dll> \
    --module client.dll --resolve-offsets
```

which on this build resolves `create_move → 0xd02130`,
`create_move_input → 0xb65de0`, `ccsgo_input → 0x257b640`,
`get_user_cmd_tick → 0x15df9f0`, `get_user_cmd_array → 0x9446c0`,
`get_user_cmd_by_sequence → 0x9444a0`.

> The previous pass (`docs/re/camera-split.md`, branch
> `feat/t18b-neverlose-camera-split`) was also derived on `659940c1`, so its
> addresses are directly comparable. **Four of its rows are corrected below**
> (§3, "Corrections to the T18b table").

### §0 — the companion web-research document

The community/protocol half of this task landed on the branch
**`feat/research-angle-split`**, file
**`docs/research/angle-split-in-the-wild.md`** (557 lines; it is *not* in this
worktree and is not edited here). It is the source of the community's
vocabulary (the "fake/real angles", the "movement fix", the `move_crc`), of the
two camera-pin shapes, and of the Valve patch notes (2023-06-06 … 2026-09-09).

What it contributed to *this* document:

| from the research | settled here |
|---|---|
| the community's `GetViewAngle` / `SetViewAngle` byte patterns | **matched exactly** onto our `FUN_180b71010` (RVA `0xb71010`) and `FUN_180b80e70` (RVA `0xb80e70`) — §3.1 row 3, §5.2 |
| "the wish direction is built from the *command's* view angles" | **confirmed**: `CPlayer_MovementServices::SetupMove` (`FUN_180a74890`, RVA `0xa74890`) copies `base->viewangles` into the `CMoveData`'s angles, and `AirMove` (`FUN_1808e57b0`, RVA `0x8e57b0`) builds the wish direction from them — §5.3 |
| "the sub-tick view angles are only sent with other sub-tick events" (2025-09-26) | **refuted for the `input_history` channel**, **confirmed for the `CSubtickMoveStep` channel** — §3.1 row 4, §4.4 |
| `CBaseUserCmdPB::move_crc` (proto field 19) | **not found in the client build** — §5.4 |
| the `cs_player_camera` → `CustomPlayerCamera` entity (2026-08-24) | **confirmed in the engine**: `FUN_180882a00` (RVA `0x882a00`) — §5.2 |

The two documents agree on the load-bearing conclusion: **the anti-aim writes
the command, never the input** (§7 step 1).


---

## 1. TL;DR

| consumer | which angle source |
|---|---|
| **the camera** | the **live local view angles**, `CCSGOInput + 0x688` (pitch/yaw/roll) |
| **the animation / hitboxes** | the pawn's **`m_angEyeAngles`**, `C_CSPlayerPawn + 0x3600` (a netvar, `CWrappedInterpolatedVar<QAngle>` on the `qangle_precise` channel) → `CCSPlayerAnimationState` |
| **the server's *player* angles** (and therefore the body yaw) | the **command's own `viewangles`** — `CBaseUserCmdPB::viewangles`, `cmd + 0x10 + 0x30 + 0x40` |
| **the server's *shot* direction** | the **silent** channel — `CSGOUserCmdPB::input_history[i].view_angles` (entry `+0x18`) |

**The one sentence that decides the feature:** *the command carries **two**
independent angle channels in the same tick — the command's own `viewangles`
(the body) and the silent `input_history[].view_angles` (the shot) — so an
anti-aim can desync the body from the shot by writing different angles into
the two, while the camera keeps reading the untouched live view angles.*

**The two consequences the implementer must design around** (both confirmed
against the engine in §5.3 and §5.4):

1. **the command's angles rotate the walk.** `SetupMove` copies
   `base->viewangles` into the `CMoveData`'s angles and `AirMove` builds the
   wish direction from them — so a fake yaw **must** be accompanied by a
   re-based analog (camera frame → anti-aim frame, Z flattened, the
   down-pitch inversion, clamped to ±1).
2. **`move_crc` is a blocker.** `CBaseUserCmdPB::move_crc` (`base + 0x30`)
   exists in the proto and in the build's PB, but **the client never writes
   it** and the engine's CRC routine was not found. Until it is, a
   re-based analog may be rejected by the server (§5.4, §8.6).

Two of the three candidates are therefore real and they are **not** the same
field: the **command's own `viewangles`** is what the shared movement code
turns into the player's angles (the body), and the **silent history** is what
the server resolves the shot against. The pawn's `m_angEyeAngles` is the
*client's* copy of the animation aim, not a channel the client sends.

> **Confidence.** The *shot* channel is backed by a live measurement (the
> repo's own `subtick-shot` probe, §4.2b). The *body* channel is backed by
> the **shared** `game/shared/player_movementservices.cpp` code in
> `client.dll` plus the client's own prediction; the server's own copy of
> that code was **not** read out of `server.dll` (§8.1).

---

## 2. The frame timeline

All in `client.dll`. "slot" = the `CCSGOInput` vtable slot.

| # | stage | function | RVA | what it writes |
|---|---|---|---|---|
| 1 | the mouse/accumulate step, inside `create_move_input` | `CCSGOInput` vtable **+0x100** (VA `0x180b78350`) | `0xb78350` | the **live view angles** `CCSGOInput + 0x688` (from the raw mouse deltas) |
| 2 | the command's own angles, right after (1) | `FUN_180b85d70(cmd, angles)` | `0xb85d70` | `cmd->pb->base->viewangles` (`base + 0x40` → the `CMsgQAngle*`; x/y/z at `+0x18/+0x1c/+0x20`; has-bit `0x4` in `base + 0x10`) |
| 3 | the move axes and buttons, still inside `create_move_input` (slot 5) | `FUN_180b65de0` | `0xb65de0` | `base->forwardmove/leftmove/upmove` (`+0x58/+0x5c/+0x60`), `base->buttons_pb` (`+0x38`), `base->random_seed` (`+0x6c`), `base->client_tick` (`+0x54`) |
| 4 | the raw axes → the command | (inside `create_move_input`) | — | `base->forwardmove` (`+0x58`) ← `CCSGOInput + 0x270`, `base->leftmove` (`+0x5c`) ← `CCSGOInput + 0x274` (the raw input cache). *The function that fills `+0x270/+0x274` was **not** re-derived on this build (the old `FUN_180b5f7b0` has moved — `0xb5f7b0` is now inside `FUN_180b5f770`, an unrelated `CUtlVector<CUtlString>` helper).* |
| 5 | the per-frame *presented* snapshot → the **frame history**; called at the head of `create_move` (slot 25) | `FUN_180d055c0` — log string `"cl: ReadFrameInput - Presented data has no mod info"` | `0xd055c0` | appends one **frame-history** entry to `CCSGOInput + 0xbe0` (stride `0x60`): the render time `+0x00/+0x04`, the player time `+0x08/+0x0c`, the **view angles `+0x10/+0x14/+0x18`**, the frame index `+0x50`, the attack flag `+0x58`; the count is `CCSGOInput + 0xbd8`, the capacity `+0xbe8` |
| 6 | the per-slot *presented* block (the source of stage 5) | `FUN_180b70640(input, slot)` | `0xb70640` | returns `input + 0x258 + slot * 0x928`; the **presented** angles are at block `+0x00` |
| 7 | the silent channel, inside `create_move` (slot 25) | `FUN_180d02130` → `FUN_180cde100` → **`FUN_180cfa6b0(entry, pb, …)`** | `0xd02130` / `0xcde100` / **`0xcfa6b0`** | one `CSGOInputHistoryEntryPB` per frame-history entry: `pb + 0x18` = the `view_angles` `CMsgQAngle*` (x/y/z from `entry + 0x10/+0x14/+0x18`), `pb + 0x60/+0x64` = the time pair, `pb + 0x68/+0x6c` = the second pair; also the attack indices `pb + 0x3c/+0x40` |
| 8 | the movement services, inside `OnPawnProcessCommand` | `CPlayer_MovementServices::OnPawnProcessCommand` = `FUN_180a70de0` → `SetupMove` = `FUN_180a74890` → `UpdateInputState` = **`FUN_180a77390`** → `DoMovement::Subtick` = `FUN_180a65ae0` → `AirMove` = `FUN_1808e57b0` | `0xa70de0` / **`0xa74890`** / **`0xa77390`** / `0xa65ae0` / **`0x8e57b0`** | `SetupMove` copies the **command's own `viewangles`** into the `CMoveData`'s angles (`+0x14/+0x18`, with the server's correction applied on top) — **that is the wish basis**; `AirMove` builds `wishdir = fwd·analogForward + right·analogSide` from it. `UpdateInputState` copies the **buttons** and the **command's move axes** (`base + 0x58/0x5c/0x60` → `ms + 0x1a0/0x1a4/0x1a8`) and the client tick (`base + 0x64` → `ms + 0x48`); `DoMovement::Subtick` copies the decoded per-step angles onto the `CMoveData`'s angles, gated by the movement-services vtable **+0x150** flag (`sv_subtick_movement_view_angles`) |
| 9 | the pawn's think → the pawn's angles | `FUN_180b07590(pawn, flags)` — file `…\game\client\c_baseplayerpawn.cpp:0x171` | `0xb07590` | reads the live view angles, applies the **server's** view-angle correction (§3, row 9), writes the corrected angles back into `CCSGOInput + 0x688` **and** into `ms + 0x240`; stores the pawn's aim angles at `pawn + 0x1388` (the previous ones at `pawn + 0x13b4`) |
| 10 | the eye-angle copy | `FUN_1808cb9b0(pawn)` | `0x8cb9b0` | `pawn + 0x3600` = **`m_angEyeAngles`** (from the pawn's `EyeAngles()` virtual, vtable **+0x570**) |
| 11 | the camera | `FUN_180a64570(services, origin, angles, …)` = `CPlayer_CameraServices::CalcView` | `0xa64570` | the rendered view setup (origin + angles); the angle source is the live view angles |
| 12 | the camera's own angle bookkeeping | `FUN_180a63b00` | `0xa63b00` | reads the live view angles with `FUN_180b71010`, adds the head/body offset, writes them back with `FUN_180b80e70` |

**The order of the two `CreateMove` slots.** Slot 5 (`create_move_input`,
`0xb65de0`) is the per-tick builder: it runs the `GetUserCmd` chain
(`get_user_cmd_tick` `0x15df9f0` → `get_user_cmd_array` `0x9446c0` →
`get_user_cmd_by_sequence` `0x9444a0`) and then fills the command's
`CBaseUserCmdPB`. Slot 25 (`create_move`, `0xd02130`) *receives the
`CUserCmd*` directly* (`(CCSGOInput*, int slot, CUserCmd*)`) and finalises
the angle history — so **slot 5 runs first and slot 25 second**. (The
strict engine-side order was not observed at runtime; it follows from the
signatures: slot 25's command must already exist.) The payload's existing
"write after the original" strategy covers both slots.

**Who else writes the live view angles (`CCSGOInput + 0x688`) in one frame**
(callers of `FUN_180b80e70`, `0xb80e70`):
`0x851f10`, `0x85b580`, `0x8e6d10`, `0xa63b00` (camera),
`0xa64250` (camera variant), `0xa68d80` (spectator console),
`0xb04c60` (spawn / observer switch — it pushes the pawn's saved angles
`pawn + 0x1388` back into the input), `0xb07590` (the post-think, when the
server corrected them), `0xb8f6e0`, `0xbb0830`, `0xc75890`, `0xc76830`.

---

## 3. The channels

| what | offset / RVA | derivation on this build |
|---|---|---|
| **the `CCSGOInput` object** | the static instance at **client + 0x257b640**; the global pointer that the engine uses is at **client + 0x223bd00** | the `ccsgo_input` signature (`external/dumper/src/analysis.rs`) resolves `0x257b640` on this build; `read_bytes(0x18223bd00)` = `40 b6 57 82 01 …` → `0x18257b640` |
| **the `CCSGOInput` vtable** | VA `0x181c9db20` (RVA `0x1c9db20`) | `FUN_1800ee570` (the static init): `LEA RCX,[0x18257b640]; CALL …; LEA RAX,[0x181c9db20]; MOV qword ptr [0x18257b640],RAX` |
| vtable slot **0x28** = `create_move_input` | `0xb65de0` | vtable read at `0x181c9db20 + 0x28`; matches the dumper's `create_move_input` |
| vtable slot **0xc8** = `create_move` | `0xd02130` | vtable read at `0x181c9db20 + 0xc8`; matches the dumper's `create_move` |
| **the per-slot input block** | `input + 0x258 + slot * 0x928` | `FUN_180b70640` in full: `MOVSXD RAX,EDX; IMUL RAX,RAX,0x928; ADD RAX,0x258; ADD RAX,RCX; RET` |
| **the live local view angles** | `CCSGOInput + 0x688` (12 B: pitch, yaw, roll) = per-slot block `+ 0x430` (slot 0) | `FUN_180b71010` returns `param_1 + 0x688` when the history is empty; `FUN_180b80e70` writes `*(u64*)(input+0x688)` and `*(u32*)(input+0x690)`; `FUN_180b65de0` reads `param_1[0xd1]` (= `+0x688`) and `param_1[0xd2]` (= `+0x690`) |
| **the presented (frame) angles** | per-slot block `+ 0x00` (pitch/yaw/roll) | `FUN_180d055c0` copies `local_60[0..2]` (where `local_60 = FUN_180b70640(...)`) into the frame-history entry `+0x10/+0x14/+0x18` |
| **the input's angle history** | count `input + 0xb50`, array `input + 0xb58`, **stride `0x440`**, the angles at **entry + 0x430** | `FUN_180b71010`: `if (*(int*)(input+0xb50) != 0) return input+0xb58 + 0x430 + (count-1)*0x440;` — i.e. the **last** entry. `FUN_180b80e70` writes the same place |
| **the view-angle setter** | `FUN_180b80e70(input, slot, angles)` — RVA `0xb80e70` | decompiled directly; for `slot == 0` it writes `input+0x688` **or**, when the history is non-empty, the *last* history entry; for any other slot it does **nothing** |
| **the per-tick copy** | `FUN_180b07590(pawn, flags)` — RVA `0xb07590` | the assert's file string `…\game\client\c_baseplayerpawn.cpp:0x171`; the callers are `FUN_180c900f0` / `FUN_180c901f0` (the pawn think) |
| **the pawn's `EyeAngles()`** | `FUN_180c74ec0` — RVA `0xc74ec0`, the pawn's vtable slot **+0x570** | `FUN_1808cb9b0` calls `(*(code**)(*pawn + 0x570))(pawn, &out)`; the slot's implementation is `FUN_180c74ec0` |
| **the local eye-angle source** | `FUN_18093c4f0` — RVA `0x93c4f0` | called from `FUN_180c74ec0`'s local branch; it reads `pawn + 0x1328` and then that object's vtable `+0xf0` |
| **the eye-angle copy** | `FUN_1808cb9b0(pawn)` — RVA `0x8cb9b0` | stores into `param_1 + 0x6c0 * 8 = pawn + 0x3600` |
| **the camera `CalcView`** | `FUN_180a64570(services, origin, angles, …)` — RVA `0xa64570` | resolves the view target (`*plVar1+0xf0`) and calls its vtable `+0x110` |
| **the command's own angles** | `cmd + 0x10` (`CSGOUserCmdPB`) `+ 0x30` (`base`) `+ 0x40` (`viewangles`, field 4) | `FUN_180b85d70`: `plVar9 = param_1 + 0x40; lVar4 = *plVar9; *(u32*)(lVar4+0x10) |= 4; lVar5 = *(u64*)(lVar4+0x40); x@+0x18, y@+0x1c, z@+0x20` |
| **the silent channel** | `cmd + 0x10 + 0x18` (`input_history`, rep `+0x28`, size `+0x20`); per entry `view_angles` at **`entry + 0x18`** | `FUN_180cfa6b0`: `lVar4 = *(u64*)(pb+0x18)` (allocated `FUN_1806723c0`), `x@+0x18, y@+0x1c, z@+0x20`; `pb+0x10 |= 1` (the field-2 has-bit) |
| **the frame history** | array `CCSGOInput + 0xbe0`, count `+0xbd8`, capacity `+0xbe8`, **stride `0x60`** | `FUN_180d055c0` (the appender) and `FUN_180d02130` (the reader: `iVar7 * 0x60 + *(i64*)(input+0xbe0)`) |
| **the pawn's `m_angEyeAngles`** | `C_CSPlayerPawn + 0x3600`, a netvar of type `CWrappedInterpolatedVar<QAngle>`, channel `qangle_precise` | the schema registration `FUN_180c56fe0`: `MOV dword ptr [RAX+0x10], 0x3600` next to `LEA RCX,[0x181c83610]` (`"m_angEyeAngles"`); the netvar registration `FUN_180c7fa90`: `MOV dword ptr [RSP+0x28], 0x3600`, `MOV dword ptr [RSP+0x20], 0x90`; the type string at `0x181c8d4b8` = `"CWrappedInterpolatedVar< QAngle >"`, the channel string at `0x181b29670` = `"qangle_precise"` |
| **the movement services' fields** | `CPlayer_MovementServices + 0x1a0/0x1a4/0x1a8` = the **command's move axes** (forward/left/up), `+0x240` = an angle triple, `+0x48` = the client tick | `FUN_180a77390` (`UpdateInputState`): `*(u32*)(ms+0x1a0) = base+0x58`, `*(u32*)(ms+0x1a4) = base+0x5c`, `*(u32*)(ms+0x1a8) = base+0x60` (the `CMsgQAngle` default instance `PTR_PTR_1821ffe90` is the fallback — the same `base + 0x40` test is used for the client tick at `base + 0x64` → `ms + 0x48`); `FUN_180b07590` writes `ms + 0x240` |
| **the server's view-angle corrections** | `C_BasePlayerPawn + 0x1340` (count) / `+0x1348` (array, **stride `0x48`**), a netvar `CUtlVectorEmbeddedNetworkVar<ViewAngleServerChange_t, NetworkVar_m_ServerViewAngleChanges>`; per entry: `+0x30` a "fix" byte, `+0x34` the `QAngle`, `+0x40` the tick/index | the netvar registration `FUN_180457c40`: `MOV dword ptr [RSP+0x28], 0x1340`, `MOV dword ptr [RSP+0x20], 0x68`, `LEA RDX,[0x181b182c8]` (`"m_ServerViewAngleChanges"`); the reader `FUN_180931130`: `iVar1 = *(int*)(pawn+0x1340)`, `lVar3 = *(u64*)(pawn+0x1348) + i*0x48`, `if (param_3 < *(u32*)(lVar3+0x40))` → copy `*(u64*)(lVar3+0x34)` / `*(u32*)(lVar3+0x3c)` |
| **the animation state** | `CCSPlayerAnimationState`: `m_flTurnOnSpotAngle +0x2c`, `m_flPreviousAimYaw +0x30`, `m_flPreviousHorizontalSpeed +0x34`; and `m_flAimPitchAngle` / `m_flAimYawAngle` (the offsets of the second registration are stack-relative and were **not** pinned) | the schema registration `FUN_18087d320` (`"CCSPlayerAnimationState"`): `MOV dword ptr [RAX+0x10], 0x30` next to `LEA RCX,[0x181c0c2d0]` (`"m_flPreviousAimYaw"`); the wider registration `FUN_1808d9740` lists `m_flAimPitchAngle` (`0x181c0ee58`), `m_flAimYawAngle` (`0x181c0ee70`), `m_flGroundTurnAngleOrVelocity`, `m_flLadderYaw`, `m_groundAction`, `m_airAction` |

### Corrections to the T18b table

| T18b row | T18b said | this build |
|---|---|---|
| the local view angles | "`CCSGOInput + 0x688` (per-slot block, stride `0x928`)" | the **stride `0x928` is right**, the **base is wrong**: the per-slot block is `input + 0x258 + slot*0x928` and the angles sit at block `+0x430` → `0x688` for slot 0 (`FUN_180b70640`). The *presented* angles are the same block's `+0x00`. |
| the per-slot angle history | "`input + 0xb50 / +0xb58`" | correct, but it is **not per-slot**: `FUN_180b71010`/`FUN_180b80e70` only ever use slot 0 and return a **constant zero** `QAngle` (`&DAT_181ac29d8`, RVA `0x1ac29d8`) for any other slot. The stride is `0x440` and the angles are at **entry `+0x430`** (the *last* entry). |
| the pawn's `EyeAngles()` | "for the local player it calls `FUN_18093c4f0`" | **corrected**: `FUN_180c74ec0` takes the `FUN_18093c4f0` branch only when the spectator/dead flags `pawn + 0x569` or `pawn + 0x14c5` are set (and three globals allow it). In the normal case `EyeAngles()` returns **`pawn + 0x3600` for every pawn**, local or remote. `FUN_18093c4f0` is the *spectator / remote-controlled* path. |
| the per-tick copy `FUN_180b07590` | "reads the movement services' angle history, writes the result into `CCSGOInput + 0x688` and `movement_services + 0x240`" | **corrected**: it reads the **live view angles** (`FUN_180b71010`), then asks the **server** for a correction (`FUN_180931130`, §3 row "the server's view-angle corrections") and, only if the server sent one, writes the corrected angles back into `CCSGOInput + 0x688` and `ms + 0x240`. The "angle history" it consults is the *server's* `m_ServerViewAngleChanges`, not a movement-services history. |

Also note a **build delta inside `CCSGOInput`** that invalidates the old
`docs/re/subtick.md` table (which was derived on build `25815307`):

| field | old build | **this build** |
|---|---|---|
| attack-1 start history index | `+0xbc0` | **`+0xbcc`** |
| attack-2 start history index | `+0xbc4` | **`+0xbd0`** |
| frame-history count | `+0xbc8` | **`+0xbd8`** |
| frame-history array | `+0xbd0` (stride `0x60`) | **`+0xbe0`** (stride `0x60`) |
| frame-history capacity / allocator | — | `+0xbe8` / `+0xbec` |

### 3.1 Reconciled with the web research (branch `feat/research-angle-split`)

The research's §2 maps the community's vocabulary onto our fields. Every row
was re-checked against the engine; the **verdict** column says what the engine
says.

| # | community word | our field | who reads it | when it is written | verdict |
|---|---|---|---|---|---|
| 1 | "the base command viewangles" | `CBaseUserCmdPB::viewangles` — proto field 4; `cmd + 0x10 + 0x30 + 0x40`; x/y/z at `+0x18/+0x1c/+0x20`; has-bit `0x4` | **the movement**: `SetupMove` (`0xa74890`) copies it into the `CMoveData`'s angles (`+0x14/+0x18`), and `AirMove` (`0x8e57b0`) builds the wish direction from them. **The server**: the player's angles → the body yaw | per tick, by `FUN_180b85d70` (`0xb85d70`) from the live view angles | **CONFIRMED** — and this is the *movement* basis, exactly as the research claims |
| 2 | "subtick angles" / "silent angles" / "the input history" | `CSGOUserCmdPB::input_history[i].view_angles` — entry `+0x18` | **the server**: the shot direction. **Not** the camera, **not** the movement | per tick, per presented frame, by `FUN_180cfa6b0` (`0xcfa6b0`) from the frame history | **CONFIRMED** as the shot seam; **the movement does not read it** (our live measurement) |
| 3 | "the real angles" / `dwViewAngles` / `GetViewAngle()` / `SetViewAngle()` | the live view angles `CCSGOInput + 0x688`; the accessor `FUN_180b71010` (`0xb71010`), the setter `FUN_180b80e70` (`0xb80e70`) | **the camera** (`FUN_180a63b00` reads them with `FUN_180b71010`); the command builder | by the mouse (the `CCSGOInput` vtable `+0x100`), then copied into the command and into `ms + 0x240` by the post-think | **CONFIRMED**, and the community's two byte patterns match ours **exactly**: `GetViewAngle()` = `4C 8B C1 85 D2 74 08` → `FUN_180b71010`; `SetViewAngle()` = `85 D2 75 3x 48 …` → `FUN_180b80e70` (our `JNZ` displacement is `0x3D`, theirs `0x3F` — a build difference). Their `dwViewAngles` (`client.dll + 0x2314F98`) is **not** `CCSGOInput + 0x688` — it is a separate global we did not locate (**unverified**) |
| 4 | "the sub-tick view angles" that Valve gated on 2025-09-26 | **two different things**: (a) the `input_history` entries' `view_angles`; (b) the `CSubtickMoveStep.pitch_delta` / `yaw_delta` (`step + 0x30` / `+0x34`) | (b) is the **movement's** angle channel: `DoMovement::Subtick` (`0xa65ae0`) copies the decoded per-step angles onto the `CMoveData`'s angles when the movement-services vtable `+0x150` flag is on | (a) `FUN_180cfa6b0`, unconditionally, one entry per presented frame. (b) `FUN_180b65de0` (`0xb65de0`), **only** when that flag is on | **the research's G2/F8 is REFUTED for (a)** and **CONFIRMED for (b)** — see §4.4 |
| 5 | "eye angles" | `C_CSPlayerPawn::m_angEyeAngles` (`pawn + 0x3600`) | the **animation** (and the hitbox aim) | the client's writer `FUN_1808cb9b0` (`0x8cb9b0`), from the pawn's `EyeAngles()` | **CORRECTED**: the research says "for the *local* pawn it is a **copy** of the camera angles". It is **not**: `FUN_180c74ec0` (the `EyeAngles()` implementation, vtable `+0x570`) returns `pawn + 0x3600` **itself** in the normal case — the camera path (`FUN_18093c4f0`) is taken only under the spectator/dead flags `pawn + 0x569` / `pawn + 0x14c5`. The local `m_angEyeAngles` is a *prediction* of the server's netvar, not a copy of the camera |
| 6 | "yaw_delta" | `CSubtickMoveStep.yaw_delta` — proto field 9; `step + 0x34` | the **movement** only | by the cheat (the strafe), or by the engine when the angles change | **CONFIRMED**, with the engine's own producer now identified: `FUN_180b65de0` attaches the deltas to the other sub-tick events' steps, and `FUN_180b5e410` (`0xb5e410`) flushes a delta-only step |
| 7 | "the move axes" / "analog" | `CBaseUserCmdPB::forwardmove/leftmove/upmove` (proto 5/6/7) at `base + 0x58/+0x5c/+0x60`; the raw axes `CCSGOInput + 0x270/0x274` | the **movement** | the slot-5 builder re-derives the command's floats from the raw axes | **CONFIRMED** |
| 8 | "the movement fix" / "AA walk fix" | the analog re-projected into the anti-aim frame | — | the cheat | **CONFIRMED as necessary** — the wish basis is the command's angles (§5.3), so a fake yaw *does* rotate the walk |
| 9 | "the server's own animation" | `CCSPlayerAnimationState` (`m_flAimYawAngle`, `m_flPreviousAimYaw`, `m_flTurnOnSpotAngle`) | the server's animation | — | **partly** — the class and its fields are confirmed (§6), the AnimGraph-2 rewrite (2025-07-28 → 2026-04-20) makes the *model* suspect (**unverified**) |

Two rows of the research's table are **corrected** by the engine (row 5, and
their `dwViewAngles` address in row 3); the rest are **confirmed**; one row
(their `dwViewAngles` global) stays **unverified**.

---

## 4. The server's view

### 4.1 What the client sends

Every tick the engine builds one `CUserCmd` (stride `0x98`) that carries
**two** angle channels:

1. **the command's own `viewangles`** — `CBaseUserCmdPB` field 4, at
   `cmd + 0x10 + 0x30 + 0x40`, has-bit `0x4` in `base + 0x10`.
   Filled by `FUN_180b85d70` (`0xb85d70`) from the **live view angles**
   (`CCSGOInput + 0x688`) at `create_move_input` time.
   *Its only writer is `FUN_180b85d70`, whose only caller is
   `create_move_input` (`0xb65de0`).*
2. **the silent channel** — `CSGOUserCmdPB::input_history[i].view_angles`
   (entry `+0x18`). Filled by `FUN_180cfa6b0` (`0xcfa6b0`) from the
   **per-frame presented** angles (the frame history at
   `CCSGOInput + 0xbe0`, entry `+0x10/+0x14/+0x18`).

They are **different fields of the same message**, filled from **different
sources at different times** (the live angles now vs. the angles as of the
last presented frame), so they can and do differ — and a payload can write
them independently. The repo's payload already does exactly that
(`payload/control/src/input.cpp`: the `command_angles_enabled_` path writes
`base + 0x40`, `write_silent_angles()` writes the history entries).

### 4.2 Which one the server uses for what

**(a) The player's angles / the body yaw → the command's own `viewangles`.**

Two independent pieces of evidence:

*Static.* The code that turns a command into the player's angles lives in
`CPlayer_MovementServices` (`CPlayer_MovementServices::UpdateInputState` =
`FUN_180a77390`, RVA `0xa77390`; `DoMovement::Subtick` = `FUN_180a65ae0`,
RVA `0xa65ae0`). Its source file is
`C:\buildworker\csgo_rel_win64\build\src\game\shared\player_movementservices.cpp`
— **`game/shared/`**, i.e. the *same* translation unit the server links, so
what the client predicts is what the server simulates. `UpdateInputState`
copies the **buttons** and the **command's move axes**
(`base + 0x58/0x5c/0x60` → `ms + 0x1a0/0x1a4/0x1a8`) and the client tick
(`base + 0x64` → `ms + 0x48`); the **angles** travel on their own path —
`DoMovement::Subtick` copies the decoded per-step angles onto the
`CMoveData`'s angles (`movedata + 0x14/+0x18`), and it is gated by the
movement-services vtable **+0x150** flag (`sv_subtick_movement_view_angles`):

```c
cVar7 = (*(code**)(*param_1 + 0x150))();          // the cvar gate
if (cVar7) { movedata->angles[0] = step[0x18]; movedata->angles[1] = step[0x1c]; }
```

The per-step `yaw_delta`/`pitch_delta` are *deltas on the command's own
`viewangles`*, so the movement's angle basis is the command's `viewangles`.

*Measured.* The repo's live probe (`docs/re/subtick.md`, 2026-10-15,
`strafe-probe`) established the two channels' behaviour side by side:

| write | steers the movement? | the local `m_angEyeAngles` |
|---|---|---|
| the command's own `viewangles` (`base + 0x40`) | **yes** (90° → the walk turned 90°) | **moves** (0 → 90) |
| `input_history[i].view_angles` | no | unchanged (0 → 0) |

So the command's own `viewangles` is the channel the **player** (body,
movement basis) follows, and it is visible in the pawn's own eye angles.

*Confidence:* the movement basis and the client's predicted eye angles are
established. The **server-side** body-yaw derivation was not read out of
`server.dll` (§8.1) — it is inferred from the shared code plus the
measurement.

**(b) The shot direction → the silent `input_history` channel.**

This is the repo's own **live measurement** (`docs/re/subtick.md`,
2026-10-15, `subtick-shot`): writing *only* the `input_history` entries'
`view_angles` made the shot land on a bot 45° off the camera (hp 67→51→34)
while **the camera did not follow** and `m_angEyeAngles` stayed at 0/0.
The server can only have taken the shot direction from the silent channel.

Combined with (a): the command's own `viewangles` is the *body*, the silent
history is the *shot*. **They are independent — which is exactly what an
anti-aim needs.**

**(c) The pawn's `m_angEyeAngles` is not a third, client→server channel.**

`m_angEyeAngles` is a **network variable** of type
`CWrappedInterpolatedVar<QAngle>` on the `qangle_precise` channel
(`C_CSPlayerPawn + 0x3600`). The client's writer is `FUN_1808cb9b0`
(`0x8cb9b0`), which copies the pawn's `EyeAngles()` into it — and, in the
normal case, `EyeAngles()` *is* that field (`FUN_180c74ec0`). The
interpolation of the netvar is `FUN_180c99ac0` (`0xc99ac0`), which blends
between the angle sets stored at `pawn + 0x36a0` (stride `0xc`) by the
render time. So on the client it is a **prediction / interpolation** of the
server's value, not an independent channel the client sends. (The netvar's
*direction* was not established — see §8.2.)

**(d) The server can also correct the client's view angles.**

`C_BasePlayerPawn::m_ServerViewAngleChanges` (`+0x1340`) is a list of
`(tick, QAngle, fix)` entries the **server** sends. `FUN_180931130`
(`0x931130`) picks the first entry with `tick > current` and returns its
angles; the pawn's post-think (`FUN_180b07590`) then **overwrites the
client's live view angles** with them:

```c
puVar12 = FUN_180b71010(input, slot);      // the live view angles
local_88 = *puVar12;                       // … possibly overridden below
if (FUN_180931130(pawn, &local_88, tick, &idx)) {
    FUN_180b80e70(input, slot, &local_88); // overwrite the client's angles
    *(u64*)(ms + 0x240) = local_88;        // and the movement services'
}
```

This is the server's authority over the player's view angles, and it is the
mechanism that would fight an anti-aim that changes the angles the server can
predict.

### 4.3 The admission rules of the silent channel

The *client-side* construction is bounded and ordered (and now fully
established — §4.4):

* one `input_history` entry per **presented frame** (the frame history is a
  `CUtlVector` at `CCSGOInput + 0xbe0` whose count is `+0xbd8` and whose
  capacity is `+0xbe8`; entries are appended in frame order, so the `when`
  ordering is the frame order);
* the entry's timestamps are the render time pair (`entry + 0x00/+0x04`) and
  the player time pair (`entry + 0x08/+0x0c`);
* the entries carry the **absolute** angles of that frame
  (`entry + 0x10/+0x14/+0x18`), **not** a delta — the deltas belong to the
  `CSubtickMoveStep` channel (§4.4b);
* the shot reads the entry at `attack1_start_history_index` (`pb + 0x3c`),
  which is why a silent-aim write must cover **all** entries, not the newest.

The **server-side** admission rules (the per-command array length cap, the
per-entry angle-delta limit, the tolerance on the `when` values) were **not
derived** — `server.dll` on disk is a different build and was not imported
(see §8).

### 4.4 Is the silent channel gated on "other sub-tick events"? — **no for the history, yes for the steps**

The research's G2/F8 quotes Valve's 2025-09-26 note: *"`sv_subtick_movement_view_angles`
will now only send subtick view angles to the server **with other subtick
events** instead of sending them for every frame they change"*. That is a
claim about **client behaviour**, so it can be checked in the engine. There
are two different things called "the sub-tick view angles", and the note
applies to only one of them.

**(a) The `input_history` entries' `view_angles` — NOT gated.**
`FUN_180cfa6b0` (`0xcfa6b0`) writes them unconditionally:

```c
lVar4 = *(u64*)(pb + 0x18);                       // the entry's view_angles CMsgQAngle*
if (lVar4 == 0) { lVar4 = FUN_1806723c0(...); *(u64*)(pb+0x18) = lVar4; }
*(u32*)(lVar4 + 0x10) |= 1;                      // the field-2 has-bit
*(u32*)(lVar4 + 0x18) = entry[4];                 // pitch
*(u32*)(lVar4 + 0x1c) = entry[5];                 // yaw
*(u32*)(lVar4 + 0x20) = entry[6];                 // roll
```

There is **no cvar test and no "are there other sub-tick events" test** in
it. Its caller `FUN_180cde100` (`0xcde100`) only allocates the entry, and
its caller `FUN_180d02130` (`create_move`, slot 25) emits **one entry per
frame-history entry** — i.e. one per presented frame. The client therefore
still ships the history's angles every frame. **The research's F8 ("the
server ignores the fake angles unless a co-travelling sub-tick event
exists") is refuted for the client side**; whether the *server* honours them
without a co-travelling event is still unverified (§8.3).

**(b) The `CSubtickMoveStep.pitch_delta` / `yaw_delta` — gated.**
These are the *movement's* angle channel, and they are produced only when the
movement services' vtable **+0x150** flag is on. In `create_move_input`
(`FUN_180b65de0`, RVA `0xb65de0`) the flag is read as

```c
plVar12 = FUN_18096a8f0(0);                                   // the local pawn
bVar5 = (plVar12 && plVar12[0x266]) &&
        (**(code **)(*(longlong *)plVar12[0x266] + 0x150))(); // ms->vtable[+0x150]
```

and the deltas are attached to the other events' steps only inside
`if (bVar5)`:

```c
if (bVar5) {
   if (fVar2 - local_1a0 != 0.0) { step->has |= 0x20; *(float*)(step + 0x30) = fVar2 - local_1a0; }  // pitch_delta
   if (fVar2 - local_19c != 0.0) { step->has |= 0x40; *(float*)(step + 0x34) = fVar2 - local_19c; }  // yaw_delta
}
```

The per-controller input entries live at `CCSGOInput + 0x288` (stride `0x20`,
count `+0x284`); their `+0x18` / `+0x1c` are the pitch and the yaw, and the
deltas are computed against the previously emitted ones. When the angles
change at a point where there is no other event, the pending delta is flushed
as its own step by `FUN_180b5e410` (RVA `0xb5e410`) — that function creates a
`CSubtickMoveStep` with `button = 0`, `pressed = 0`, `when` =
`CCSGOInput + 0xb40` and the two deltas.

**So the note describes (b), the movement's channel.** The community's
"movement fix" (the analog re-base) is therefore the *only* way to steer the
walk while the command's angles are fake; the `yaw_delta` channel is the
strafe's, and it is capped by the `subtick_moves` admission rules (§8.3).


---

## 5. The write points for the anti-aim

The repo's inline relocator (`payload/hooks/src/inline.cpp`) copies
instructions until it has **≥ 14 bytes** (`k_jmp_size`), refuses **any
relative branch** (`decoded->relative`), refuses a total **> 16 bytes**, and
patches `total` bytes with a 14-byte `jmp [rip+0]`. A branch-free prologue of
≤ 16 bytes is therefore hookable; a short `JNZ` in the first two bytes is not.
All the prologues below were read from `procedure_assembly`.

| # | what it writes | function | RVA | when in the frame | who else writes it in the same frame | hookable? | `__fastcall` signature (MSVC x64) |
|---|---|---|---|---|---|---|---|
| **W1** | **the command's own `viewangles`** (`base + 0x40`) — the *body* channel | `FUN_180b85d70` | `0xb85d70` | inside `create_move_input` (slot 5), immediately after the mouse accumulation and **before** the command is sent/simulated | nothing else — `FUN_180b85d70` is its only writer | **yes** — 15-byte branch-free prologue (`MOV [RSP+8],RBX; MOV [RSP+0x10],RBP; MOV [RSP+0x18],RSI`) | `void (__fastcall *)(CUserCmd* cmd, const float* angles)` — RCX = the command, RDX = `{pitch,yaw,roll}` |
| **W2** | **the silent channel** (`input_history[i].view_angles`) — the *shot* channel | `FUN_180cfa6b0` | `0xcfa6b0` | inside `create_move` (slot 25), once per frame-history entry | nothing else on the client | **yes** — 14-byte prologue (`MOV [RSP+0x18],RBX; PUSH RBP; PUSH RDI; PUSH R14; LEA RBP,[RSP-0x37]`) | `void (__fastcall *)(FrameInputEntry* entry, CSGOInputHistoryEntryPB* pb, char fix, uint render, uint tick, void* ctx)` — RCX = the frame entry (`+0x10/+0x14/+0x18` = the angles), RDX = the PB |
| **W3** | **the live view angles** `CCSGOInput + 0x688` (the camera) | `FUN_180b80e70` | `0xb80e70` | many: the camera, the post-think, the spawn handler, the spectator console | 12 callers (see §2) | **no** — the prologue is `TEST EDX,EDX; JNZ rel8` → a relative branch | `void (__fastcall *)(CCSGOInput* input, int slot, const float* angles)` |
| **W4** | the live view angles (the mouse) | `CCSGOInput` vtable **+0x100** | `0xb78350` | inside `create_move_input`, before W1 | — | **yes** as a **vtable** hook (the code prologue is behind a `JNZ`); the payload already hooks the vtable | `char (__fastcall *)(CCSGOInput* self, int slot, float dt, float* angles)` |
| **W5** | the pawn's **`m_angEyeAngles`** (`pawn + 0x3600`) — the *animation* channel | `FUN_1808cb9b0` | `0x8cb9b0` | the pawn's eye-angle update (post-think / model update) | the netvar update (server) | **yes** — 15-byte prologue (already hooked by the payload) | `void (__fastcall *)(C_CSPlayerPawn* pawn)` |
| **W6** | the movement services' **command move** + buttons (`ms + 0x1a0/0x1a4/0x1a8`, `ms + 0x48/0x70/0x78`) | `FUN_180a77390` (`UpdateInputState`) | `0xa77390` | inside `OnPawnProcessCommand` (the shared code) | — | **yes** — 15-byte prologue | `void (__fastcall *)(CPlayer_MovementServices* self, CUserCmd* cmd)` — RCX = the movement services, RDX = the command |
| **W7** | the live view angles + `ms + 0x240` + `pawn + 0x1388` | `FUN_180b07590` (the pawn's post-think) | `0xb07590` | the pawn's think, after the movement | — | **yes** — 15-byte prologue (`MOV RAX,RSP; PUSH RBP; PUSH RDI; PUSH R15; LEA RBP,[RAX-0x5f]`). *T18b called this an 18-byte prologue and therefore "refused"; the relocator stops as soon as it has 14 bytes and 15 ≤ 16, so it is accepted.* | `void (__fastcall *)(C_BasePlayerPawn* pawn, uint flags)` |
| **W8** | the **server's** view-angle correction (read) | `FUN_180931130` | `0x931130` | inside W7 | — | **yes** — 15-byte prologue | `uint64 (__fastcall *)(C_BasePlayerPawn* pawn, float* out_angles, uint tick, uint* out_idx)` |
| **W9** | the rendered camera angles | `FUN_180a64570` (`CalcView`) | `0xa64570` | the render path | — | **yes** — 15-byte prologue (T18b's hook) | `void (__fastcall *)(CPlayer_CameraServices* self, void* origin, float* angles, …)` — the exact tail arguments were not pinned |

### 5.1 The write points (the table above)

### 5.2 The camera-pin shapes — which one, and why

The research's §3.6 gives two community camera pins and says shape (b) has a
published failure. The engine settles the shape question.

**What the community's seams are, on this build.** Their byte patterns match
ours *exactly*:

| community seam (research §3.2, Axion's `ccsgoinput.h`) | pattern | **our function** | RVA |
|---|---|---|---|
| `GetViewAngle()` | `4C 8B C1 85 D2 74 08` | `FUN_180b71010(input, slot)` — the accessor: slot 0 → `input + 0x688`, slot ≠ 0 → a constant zero `QAngle` | `0xb71010` |
| `SetViewAngle(QAngle&)` | `85 D2 75 3x 48 …` | `FUN_180b80e70(input, slot, angles)` — the setter (our `JNZ` displacement is `0x3D`, theirs `0x3F` — a build difference) | `0xb80e70` |

**But their "vtable slot 8" is not slot 8 here.** The `CCSGOInput` vtable is
at RVA `0x1c9db20` (VA `0x181c9db20`); its slot 8 (offset `+0x40`) is
**`FUN_180cea280`** (RVA `0xcea280`), and that function is an **input-history
reset**, not a validate-input:

```c
void FUN_180cea280(longlong p) {
   FUN_180b6c980();
   *(u8 *)(p + 0xbbd) = 0;  *(u64*)(p + 0xbcc) = -1;  *(u32*)(p + 0xbd8) = 0;
   *(u32*)(p + 0xbf0) = 0;  *(u32*)(p + 0xbf4) = -1;
}
```

Neither `FUN_180b71010` nor `FUN_180b80e70` is a `CCSGOInput` vtable entry
(all 76 slots of that vtable were read). So the community's "slot 8" is a
**different build's layout** (or a different object) — **their slot number
does not transfer**, but their *functions* do, and they are ours.

**The verdict: for the command-only anti-aim, no pin is needed at all.**
The camera reads the input's live view angles (`FUN_180a63b00` →
`FUN_180b71010` → `CCSGOInput + 0x688`); the anti-aim writes the *command*
(`FUN_180b85d70` → `base + 0x40`). Two different fields, two different
owners. The research's §5.1 one-liner ("the camera needs no write at all,
because the camera never reads the command") is **confirmed**.

If a pin *is* needed (e.g. because the server's correction or a movement
re-base writes into `input + 0x688`), **use the validate-input swap (shape
a), not the view-setup pin (shape b)** — and the engine says why:

| | shape (a) — the validate-input swap | shape (b) — the view-setup pin |
|---|---|---|
| what it replaces | the **input's** view angles around one engine call | the **view setup**'s angles (+ `dwViewAngles`, + `m_angEyeAngles`) right before the draw |
| what it can fix | anything the engine reads through `FUN_180b71010`/`FUN_180b80e70` — the camera *and* the pawn's aim angles | only the rendered view setup |
| what it cannot fix | the model's *cached* transform, if the fake pose was already baked in | the local model's aim: `FUN_1808cb9b0` writes `m_angEyeAngles` from the pawn's `EyeAngles()` **downstream of the camera**, so a view-setup pin cannot reach it |
| the published failure | — | **the viewmodel / player model keep the cached world transform of the fake pose — the hands "spazz"** (the research's F3 / U1). Our timeline explains it: the model is animated from `m_angEyeAngles` (stage 10), the view setup is written in stage 11 — the pin fixes stage 11 and leaves stage 10 on the fake pose |

So shape (b) is **not** the shape to copy: the engine's stage order (10 →
11) makes it structurally unable to hold the model. Shape (a) substitutes
the *source* the engine reads, so it can.

**A third, engine-native camera exists** (the research's L6): Valve's
2026-08-24 `cs_player_camera` → `CustomPlayerCamera` entity. It is in the
build: `FUN_180882a00` (RVA `0x882a00`) is the `CalcView` caller, and it
takes the view from that entity when one exists:

```c
plVar1 = FUN_180cb2100();                       // the camera entity
if (plVar1 == 0) { FUN_180a64570(services, origin, angles, fov, ?, ?); }   // the normal path
else {                                            // the entity's view wins
   *(vec3*)origin = (*(code**)(*plVar1 + 0x568))(plVar1, tmp);
   *(vec3*)angles = (*(code**)(*plVar1 + 0x570))(plVar1, tmp);
   *fov = FUN_180c7cf10(plVar1); *a = FUN_180c7cf00(plVar1); *b = FUN_180c7b560(plVar1);
}
```

That is Valve's own "a view that is not the pawn's" — the same decoupling an
anti-aim needs, shipped as a scriptable entity.

### 5.3 The movement's wish basis — the command's angles, confirmed

**Yes: the command's `viewangles` rotate the wish basis.** The chain, all on
this build:

1. `CPlayer_MovementServices::SetupMove` = `FUN_180a74890` (RVA `0xa74890`,
   vprof string `"CPlayer_MovementServices::SetupMove"`, file
   `game\shared\player_movementservices.cpp` — **shared with the server**)
   copies the command's angles into the `CMoveData`:

```c
ppuVar10 = &PTR_PTR_1821ffe90;                                     // the default CBaseUserCmdPB
if (*(u64*)(cmd_base + 0x40)) ppuVar10 = *(u64*)(cmd_base + 0x40);   // base->viewangles
ppuVar8  = &PTR_PTR_1822082e8;                                     // the default CMsgQAngle
if (ppuVar10[8]) ppuVar8 = ppuVar8[8];                            // base+0x40 = the CMsgQAngle*
FUN_180180160(&local_88, q->x, q->y, q->z);                          // the QAngle
FUN_180931130(pawn, &local_88, *(u32*)((u64)ppuVar10 + 0x7c), &idx); // the SERVER's correction
*(u64*)(movedata + 8)  = local_88;                                 // the angles
*(u64*)(movedata + 0x14) = local_88;                               // <-- the wish basis
```

   Note the third line: the **server's** view-angle correction is applied to
   the movement basis too (the same `FUN_180931130` the post-think calls),
   keyed by `base + 0x7c`.

2. `AirMove` = `FUN_1808e57b0` (RVA `0x8e57b0`, the vprof label
   `"PreSource1AirMove"`) builds the wish direction from those angles:

```c
FUN_181742af0(movedata + 0x14, &fwd, &right, &up);      // AngleVectors(the CMoveData's angles)
wishdir = fwd * movedata->analogForward(+0x2c) + right * movedata->analogSide(+0x30);
```

3. `SetupMove`'s last step (`FUN_180a74b30`, RVA `0xa74b30`) is the analog
   sanitizer, and `FUN_180a780a0` (`0xa780a0`) turns the held buttons into
   the same axes.

**Consequences for the anti-aim** (this is the research's A3/A4, confirmed):

* a fake yaw of 180° in `base->viewangles` **rotates the walk basis by
  180°** — the player walks backwards relative to the camera;
* the **down-pitch inversion** is real: `AirMove` uses the *full* 3D basis
  (`fwd`, `right`, `up` from `AngleVectors`), so a pitch of 89° tips the
  forward axis into the ground and the forward key stops contributing to the
  walk — the research's F5 ("one key only works when looking forward, two
  keys go diagonal"). The fix is Axion's: build the intent in the **camera**
  frame, flatten Z, project it onto the **anti-aim** frame, and invert the
  forward axis when the camera pitch is past 89°;
* the **±1 clamp** is a *unit* clamp, not cosmetics: the engine's own input
  is in `±1` (`base->forwardmove` = `1.0` for a console `+forward`,
  `docs/re/subtick.md`), and `sv_quantize_movement_input` (default on)
  rounds the **server-side** value to `{-1,0,+1}` — a fractional analog write
  is quantised away server-side, so the re-based analog must be snapped to
  the same discrete set (`valid_set()` in `payload/control/src/input.cpp`).

### 5.4 `move_crc` — **not found in the client build**

The research's G7/A10/F10 (Weave's P2 §9): *"after the anti-aim changes the
movement, the command is re-serialised and `move_crc` recomputed, or the
server rejects the modified forward/sidemove"*.

The field exists in Valve's proto — `CBaseUserCmdPB.move_crc = 19`
(`bytes`) — and in the build's `CBaseUserCmdPB` it is the `bytes` field at
**`base + 0x30`** (`docs/re/subtick.md`; the has-bit `0x1`). What the engine
shows:

* **the client's command builder never writes it.** `FUN_180b65de0`
  (`create_move_input`, RVA `0xb65de0`) writes `base + 0x10` (the has-bits),
  `base + 0x54` (the client tick), `base + 0x58/0x5c/0x60` (the move axes),
  `base + 0x6c` (the random seed) and `base + 0x68` (the mouse deltas) — and
  **`base + 0x30` is never touched** (the `MOV RAX,[RDI+0x30]` instructions in
  that function read `pb + 0x30`, the *base pointer*, not `base + 0x30`);
* **there is no CRC in the input path.** The only CRC in the command builder
  is the random-seed MD5 (`CALL qword ptr [0x181aacb40]` → `MD5_PseudoRandom`,
  at `0xb661c2`). The import `TIER0.DLL::CRC32_ProcessBuffer` has 17 call
  sites in the whole module (`find_xrefs_to_name`), and **none** of them is
  in the input path: `0x1e4780` (a rendering path), `0x9aea50`/`0x9aeb50`,
  `0xcb2440` (a class registration), `0xcb4670`, `0xcb5620`, `0x1006aa0`,
  `0x1016d20`, `0x1763fb0` — all in the network/serialisation/other
  subsystems;
* the payload does **not** compute a CRC today: `payload/control/src/input.cpp`
  writes `base + 0x58/+0x5c/+0x60` (the analog axes) and `base + 0x40` (the
  angles) and never touches `base + 0x30`; `external/dumper/` has no CRC
  signature either.

**What this means for us — and what it does not.** Two readings are
possible and the client cannot decide between them:

1. the CRC is computed by the **server** over the received move values and
   compared against the `move_crc` the client sent — in which case the client
   *must* fill `base + 0x30` and a modified command without a correct CRC is
   rejected; or
2. the field is filled by a **later, send-time** stage that our RE did not
   reach (the command's network serialisation), in which case a cheat that
   edits the command *after* that stage leaves a stale CRC.

Either way the implementer must treat **`base + 0x30` as a required field**:
if the anti-aim re-bases the analog axes, it must recompute the CRC the same
way the engine does — **and we have not found the engine's CRC routine**, so
that is an open implementation blocker (§8.6), not a solved one.

### 5.5 Re-entrancy and the camera/movement invariant

**Re-entrancy.** W1, W2, W6 and W7 all run **on the game thread inside the
input/tick path**; a detour that only writes to memory (no engine calls) is
re-entrant-safe. W7 (`FUN_180b07590`) is the pawn's post-think and runs once
per simulated command; hooking it is the natural place to *win the race*
against the engine's own write-back of the corrected angles.

**The camera and the movement must not be disturbed.** W1 changes the
command's own angles — the channel the **player** follows: measured, it steers
the movement by exactly the written delta and it moves the pawn's own
`m_angEyeAngles`. So a fake yaw written into W1 **does** rotate the movement
basis (§5.3). If the anti-aim wants the movement to keep following the
camera, the fake angles must be written **after** the movement has consumed
the command (W1 is fine for the *body*, but the movement basis has to be
restored afterwards, or the per-step `yaw_delta` mechanism used instead).

---

## 6. The animation link

* The pawn's **eye angles** are `m_angEyeAngles` (`C_CSPlayerPawn + 0x3600`),
  a netvar of type `CWrappedInterpolatedVar<QAngle>` on the `qangle_precise`
  channel. The client's writer is `FUN_1808cb9b0` (`0x8cb9b0`).
* The **animation state** is `CCSPlayerAnimationState` (schema class name at
  `0x181c0a968`). Its fields include `m_flTurnOnSpotAngle` (`+0x2c`),
  **`m_flPreviousAimYaw` (`+0x30`)**, `m_flPreviousHorizontalSpeed` (`+0x34`),
  `m_flFootIKOffsetLeft/Right`, `m_flWeaponDropPercentageDueToMovement`, and
  (in the wider registration) **`m_flAimPitchAngle`**, **`m_flAimYawAngle`**,
  `m_groundAction`, `m_groundActionDirectionID`,
  `m_flGroundTurnAngleOrVelocity`, `m_flLadderYaw`,
  `m_flLadderYawBackwards`, `m_airAction`, `m_flAirHeightAboveGround`,
  `m_leftFootTarget`, `m_rightFootTarget`, `m_flFlashedAmount`,
  `m_flinchHead` / `m_flinchBody` (+ their restart flags).
  So the aim-yaw layer is `m_flAimYawAngle` / `m_flAimPitchAngle` and the
  *body*-yaw machinery is `m_flPreviousAimYaw` + `m_flTurnOnSpotAngle` +
  `m_flGroundTurnAngleOrVelocity` — i.e. CS2 keeps the **aim yaw** and the
  **body yaw** in separate fields of the animation state, which is what makes
  a body/aim split representable at all.
* **The local player's animation is computed locally**, not copied from the
  server. Evidence: the T18b 90° test wrote `m_angEyeAngles = view + 90°`
  (a client-side write of a *netvar*) and the **model** aimed 90° away from
  the camera while the camera and the movement stayed on the view. A copied
  animation could not have followed a client-side write. (The "animfix"
  sibling task owns the bone-transform half of this.)
* The exact function that turns `m_angEyeAngles` into `m_flAimYawAngle` /
  the bone transforms was **not** re-derived here — see §8.

---

## 7. What the implementer must do

1. **Write the fake body angles into the command's own `viewangles`** —
   `CBaseUserCmdPB::viewangles` at `cmd + 0x10 + 0x30 + 0x40`, has-bit `0x4`
   in `base + 0x10`, x/y/z at `+0x18/+0x1c/+0x20`. The cleanest seam is an
   inline hook on **`FUN_180b85d70` (RVA `0xb85d70`)** —
   `void __fastcall (CUserCmd* cmd, const float* angles)` — hooked *around*
   the original so the fake angles are what the engine writes.
   *Risk:* this is the channel the **player** follows — measured, writing it
   steers the movement by exactly the written delta and moves the pawn's own
   `m_angEyeAngles`. So a 180° fake rotates the walk basis (the *wish basis*
   is the command's angles — §5.3). The movement must therefore be re-based
   (step 1b) to keep the player's controls on the camera.
1b. **Re-base the movement into the anti-aim frame.** The wish direction is
   built from the command's angles: `SetupMove` (`0xa74890`) copies
   `base->viewangles` into the `CMoveData`'s angles (`+0x14/+0x18`) and
   `AirMove` (`0x8e57b0`) builds `wishdir = fwd·analogForward + right·analogSide`
   from them. So the anti-aim must:
   * capture the **camera** angles before it writes (`CCSGOInput + 0x688`,
     read with `FUN_180b71010` `0xb71010`) — the player's intent is
     camera-relative;
   * build the intent vector in the camera frame, **flatten Z** (the walk
     lives in the XY plane) and **project it onto the anti-aim frame**
     (the command's own angles);
   * apply the **down-pitch inversion**: a camera pitch past 89° flips the
     forward axis, otherwise the player walks backwards (the engine's
     `AirMove` uses the full 3D basis, so a pitch of 89° tips the forward
     axis into the ground and only the side axis survives);
   * **clamp to ±1** and snap to the engine's discrete set `{-1, 0, +1}`
     (`sv_quantize_movement_input` rounds the server-side value to
     integers; the engine's own analog is in unit values, `±1`);
   * write the result to **both** `base->forwardmove/leftmove`
     (`+0x58/+0x5c`) **and** the raw axes `CCSGOInput + 0x270/0x274`, because
     the slot-5 builder re-derives the command's floats from the raw axes
     (`docs/re/subtick.md`) — a command-only write is overwritten;
   * **recompute `move_crc`** (`base + 0x30`) — see §5.4: we did not find the
     engine's CRC routine, so this is an open blocker (§8.6), not a solved
     step. Until it is solved, the anti-aim should write the analog only
     when the CRC question is settled, or accept that the server may drop
     the modified movement.
2. **Leave the silent channel alone for the body**, and use it only for the
   shot: `input_history[i].view_angles` at entry `+0x18`
   (`CMsgQAngle` x/y/z `+0x18/+0x1c/+0x20`, has-bit `0x1` in `entry + 0x10`).
   An inline hook on **`FUN_180cfa6b0` (RVA `0xcfa6b0`)** is the seam; the
   payload's existing per-entry write works too. **All** entries must carry
   the shot angles (the shot reads the entry at `attack1_start_history_index`,
   `pb + 0x3c`).
   *Risk:* the engine rebuilds the history every tick from the frame history
   (`CCSGOInput + 0xbe0`), so the write has to happen after
   `create_move` (slot 25) has filled it and before the command is sent.
3. **Never touch the live view angles** `CCSGOInput + 0x688` (or the
   per-slot block `input + 0x258 + slot*0x928`, whose `+0x430` is the same
   field). They are the camera's source (`FUN_180a63b00` reads them with
   `FUN_180b71010` and writes them back with `FUN_180b80e70`).
   *Risk:* `FUN_180b80e70` is **not** inline-hookable (the prologue is
   `TEST EDX,EDX; JNZ rel8`); use the vtable slot `+0x100` (`0xb78350`) or
   write the field directly.
4. **Watch the server's correction** (`m_ServerViewAngleChanges`,
   `pawn + 0x1340/+0x1348`, entries of `0x48`): the post-think
   (`FUN_180b07590` `0xb07590`) calls `FUN_180931130` (`0x931130`) and, if
   the server sent a correction, **overwrites the client's live view angles**.
   An anti-aim that fights this must win the race *after* the post-think, or
   must not touch the live angles at all (which is why step 1 uses the
   command, not the input).
   *Risk:* hooking the post-think to suppress the correction changes the
   *camera* as well (the corrected angles are written back into
   `CCSGOInput + 0x688`).
5. **Do not rely on `m_angEyeAngles` for the body.** It is the client's
   prediction of the server's value and it is rewritten by `FUN_1808cb9b0`
   (`0x8cb9b0`, hookable, already hooked by the payload) — useful for the
   *animation* only, never for what the server sees.
6. **The jitter** has to be applied to the *same* channel as the fake yaw
   (step 1) and must be written **once per command**, not per frame: the
   command's own `viewangles` is written exactly once per tick, by
   `FUN_180b85d70`, from the live view angles.
   *Risk:* writing it per frame (a 1 kHz writer) fights the engine's own
   write-back in the post-think.
7. **Do not pin the camera — the command-only shape needs no pin.** The
   camera reads the input's live view angles (`CCSGOInput + 0x688`, through
   `FUN_180b71010` in `FUN_180a63b00`); the anti-aim writes the command's
   `base->viewangles`. They are different fields, so the camera never
   follows the fake angles. *If* a pin is ever needed (because something else
   writes the input's angles), use the **validate-input swap** (substitute
   the real angles around the engine call that reads them, then restore) and
   **not** the view-setup pin: the view setup is written in stage 11 of the
   timeline while the model's aim is taken from `m_angEyeAngles` in stage 10,
   so a view-setup pin leaves the model on the fake pose — the published
   "the hands spazz" failure (§5.2).
8. **Acknowledge the server's angle corrections.** The client tells the
   server how many of the server's view-angle changes it consumed, in
   `CBaseUserCmdPB::consumed_server_angle_changes` — **`base + 0x7c`**, the
   has-bit `0x8000` in `base + 0x10` — written by `FUN_18092e7f0`
   (RVA `0x92e7f0`) from the global `DAT_18255c044`. The anti-aim must not
   disturb that field: it is how the server knows its correction landed.

---

## 8. What is still unknown

1. **The server-side body-yaw path.** `server.dll` on disk
   (`C:\Users\gpmpo\ghidra-analysis\server.dll`, sha256 `09e8c04c…`) is a
   **different build** and has **no warm Ghidra project** on this host, so
   *no server-side address is reported here* — the server half of the claim
   in §4.2(a) rests on the *shared* `game/shared/player_movementservices.cpp`
   code in `client.dll` plus the repo's live measurements, not on a server
   reversal.
   **Experiment that would settle it:** import `server.dll` (a cold Ghidra
   import, ~20–40 min), find `CPlayer_MovementServices::UpdateInputState`
   and `CBasePlayerPawn::ProcessUsercmd` by the `game/shared/` file strings
   and the `m_angEyeAngles` schema name, and read which angles end up in
   the server's `m_angEyeAngles` and in the shot's `CBaseUserCmdPB`.
2. **The direction of the `m_angEyeAngles` netvar** (client→server or
   server→client). *Partly resolved.* What the engine now shows:
   * `m_angEyeAngles` (`C_CSPlayerPawn + 0x3600`) is a
     `CWrappedInterpolatedVar<QAngle>` netvar on the `qangle_precise`
     channel; the client writes it locally with `FUN_1808cb9b0`
     (`0x8cb9b0`) from `EyeAngles()`, and in the normal case `EyeAngles()`
     *is* that field — so the client's write is a **prediction**, not an
     authored value;
   * the netvar's **interpolation** is `FUN_180c99ac0` (`0xc99ac0`), which
     blends between the angle sets stored at `pawn + 0x36a0` by the render
     time — interpolation only makes sense for a **server→client** var;
   * the neighbouring `m_ServerViewAngleChanges` netvar (`pawn + 0x1340`) is
     **definitively server→client**: the client *acknowledges* it with the
     client→server field `CBaseUserCmdPB::consumed_server_angle_changes`
     (`base + 0x7c`, has-bit `0x8000`), written by `FUN_18092e7f0`
     (`0x92e7f0`) from `DAT_18255c044` — and the same value is the key the
     client uses to look up the next correction (`FUN_180931130`) and to
     correct the **movement basis** (`SetupMove`, §5.3).
   So: `m_ServerViewAngleChanges` = server→client (**confirmed**);
   `m_angEyeAngles` = almost certainly server→client (**strongly indicated**,
   not proven).
   **Experiment that would settle it:** read the netvar's direction flag in
   its registration (`FUN_180c7fa90`, around `0x180c844e5` / `0x180c8454a`),
   or a live test — write `m_angEyeAngles` 90° off and read what a second
   client sees.
3. **The silent channel's admission rules.** *Client side: resolved.*
   * the **array length**: one `input_history` entry per **presented frame**
     (`FUN_180d055c0` appends to the frame history at `CCSGOInput + 0xbe0`,
     stride `0x60`, count `+0xbd8`; `FUN_180d02130` emits one PB entry per
     frame-history entry);
   * the **per-entry delta**: there is none — each entry carries the
     **absolute** angles of that frame (`entry + 0x10/+0x14/+0x18`), not a
     delta; the *deltas* belong to the `CSubtickMoveStep` channel
     (`step + 0x30` / `+0x34`), which is the movement's and is gated on
     `sv_subtick_movement_view_angles` (§4.4);
   * the **`when` ordering**: the frame order; the entry's timestamps are the
     render time pair (`entry + 0x00/+0x04`) and the player time pair
     (`entry + 0x08/+0x0c`).
   *Server side: still open* — the per-command length cap, the per-entry
   delta limit and the `when` tolerance are in `server.dll`, which was not
   read (§8.1).
   **Experiment:** the same server import; or a live probe that pushes N
   entries with a chosen delta and watches for the server's rejection.
4. **The animation chain from `m_angEyeAngles` to the bone transforms** —
   which function writes `m_flAimYawAngle`/`m_flPreviousAimYaw` and where the
   `CTransform` bone array is updated. Only the *field names and the class*
   were pinned here (§6); the bone-transform half belongs to the sibling
   "animfix" task. **Experiment:** xref `0x181c0ee70`
   (`"m_flAimYawAngle"`) outside the schema registration and follow the
   writers.
5. **The pawn's vtable slot `+0x578`** (the "aim angles" getter the post-think
   calls: `(**(code **)(*param_1 + 0x578))(param_1, &local_78)`), whose result
   is stored at `pawn + 0x1388`. The `CalcView` half of this item is
   **resolved**: `FUN_180a64570`'s signature is
   `(CPlayer_CameraServices* services, vec3* origin, QAngle* angles, int* fov, int* a, int* b)`
   — read off its only caller, `FUN_180882a00` (RVA `0x882a00`), which
   passes the same six arguments and, when a `CustomPlayerCamera` entity
   exists, fills them from that entity instead (§5.2).
   **Experiment for the remaining half:** read the pawn's vtable (the
   `C_CSPlayerPawn` RTTI at `0x1822e8be0` → the COL → the vtable) and take
   the entry at `+0x578`.
6. **The `move_crc` routine.** `CBaseUserCmdPB::move_crc` (proto field 19,
   `base + 0x30`) is never written by the client (§5.4), so **the engine's
   CRC routine over the move values was not found**. Until it is, the
   movement re-base (step 1b) carries the risk that the server rejects the
   modified analog.
   **Experiment:** (a) the send-time path — find where the `CUserCmd` is
   serialised for the network (the `CBaseUserCmdPB` writer) and look for a
   CRC there; (b) a live test — modify `base->forwardmove` without touching
   `base + 0x30` on a listen server and see whether the movement is
   honoured; (c) compare with the community's implementation (Axion/Weave)
   to see which bytes they hash and in which order.
7. **The `dwViewAngles` global** (the research's `client.dll + 0x2314F98`).
   It is **not** `CCSGOInput + 0x688` (the community uses both in one
   function). We did not locate it in this build.
   **Experiment:** read `client.dll + 0x2314F98` at runtime and compare it
   with `CCSGOInput + 0x688`; or find the writer of that address.

---

## 9. Re-derivation recipe (for the next build)

| what | anchor |
|---|---|
| the `CCSGOInput` object and its vtable | the `ccsgo_input` signature in `external/dumper/src/analysis.rs`; the static init `FUN_1800ee570` (`LEA RCX,[obj]; …; LEA RAX,[vtable]; MOV [obj],RAX`) |
| `create_move` / `create_move_input` | the same signatures (`create_move`, `create_move_input`) — the vtable slots `+0xc8` / `+0x28` |
| the live view angles | `FUN_180b71010` (the accessor) and `FUN_180b80e70` (the setter) — both are trivially recognisable by the `+0x688` / `+0xb50` / `+0xb58` constants |
| the per-slot block and the stride `0x928` | `FUN_180b70640` — a 10-byte function: `MOVSXD RAX,EDX; IMUL RAX,RAX,0x928; ADD RAX,0x258; ADD RAX,RCX; RET` |
| the frame history | the log string `"cl: ReadFrameInput - Presented data has no mod info"` → `FUN_180d055c0` |
| the command's own angles | the writer `FUN_180b85d70` (the only caller of the `base + 0x40` store) |
| the silent channel | the writer `FUN_180cfa6b0` (called from `create_move` slot 25) |
| the movement's command-move / button source | the vprof string `"CPlayer_MovementServices::UpdateInputState"` (`0x181c3b3d0`) → `FUN_180a77390` |
| the movement's **wish basis** | the vprof string `"CPlayer_MovementServices::SetupMove"` → `FUN_180a74890` — it copies `base->viewangles` into the `CMoveData`'s angles (`+0x14/+0x18`) |
| the wish direction | the vprof label `"PreSource1AirMove"` (`0x181c11d90`) → `FUN_1808e57b0` — `wishdir = fwd·analogForward + right·analogSide` |
| the movement's **angle** basis | the vprof string `"CPlayer_MovementServices::DoMovement::Subtick"` (`0x181c3b528`) → `FUN_180a65ae0`; the *decoder* that turns the command's `subtick_moves` into the per-slice array (and accumulates `yaw_delta` onto the command's `viewangles`) is the movement services' vtable **+0x138** — **not re-derived here** |
| the movement's sub-tick yaw | the vprof string `"CPlayer_MovementServices::DoMovement::Subtick"` (`0x181c3b528`) → `FUN_180a65ae0`; the **gate** is the movement services' vtable **+0x150** (`sv_subtick_movement_view_angles`), read in `create_move_input` (`0xb65de0`) |
| the community's angle accessor / setter | the byte patterns `4C 8B C1 85 D2 74 08` → `FUN_180b71010` and `85 D2 75 3D` → `FUN_180b80e70` |
| the `CCSGOInput` vtable slots | the vtable at RVA `0x1c9db20` (the static init `FUN_1800ee570`); slot 8 = `FUN_180cea280` (an input-history **reset**), slot `+0x100` = `FUN_180b78350` (the mouse/accumulate step) |
| the camera entity (`CustomPlayerCamera`) | `FUN_180882a00` (the `CalcView` caller) → `FUN_180cb2100()` |
| the pawn's post-think | the assert file string `…\game\client\c_baseplayerpawn.cpp` → `FUN_180b07590` |
| the pawn's eye angles | the schema name `"m_angEyeAngles"` (`0x181c83610`) → the schema registration `FUN_180c56fe0` (the offset is the `MOV dword ptr [RAX+0x10], <offset>` next to the name `LEA`) |
| the server's view-angle corrections | the schema name `"m_ServerViewAngleChanges"` (`0x181b182c8`) → the netvar registration `FUN_180457c40` (the offset is the `MOV dword ptr [RSP+0x28], <offset>`) |
| the consumed-angle acknowledgement | `FUN_18092e7f0` (writes `base + 0x7c`, the has-bit `0x8000`) — the proto field is `CBaseUserCmdPB.consumed_server_angle_changes = 20` |
| the animation state | the schema class name `"CCSPlayerAnimationState"` (`0x181c0a968`) and the field names `"m_flAimYawAngle"` (`0x181c0ee70`) / `"m_flPreviousAimYaw"` (`0x181c0c2d0`) |
