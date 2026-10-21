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
| 8 | the movement services, inside `OnPawnProcessCommand` | `CPlayer_MovementServices::OnPawnProcessCommand` = `FUN_180a70de0` → `UpdateInputState` = **`FUN_180a77390`** → `DoMovement::Subtick` = `FUN_180a65ae0` | `0xa70de0` / **`0xa77390`** / `0xa65ae0` | `UpdateInputState` copies the **buttons** and the **command's move axes** (`base + 0x58/0x5c/0x60` → `ms + 0x1a0/0x1a4/0x1a8`) and the client tick (`base + 0x64` → `ms + 0x48`); the **angles** travel separately — `DoMovement::Subtick` copies the decoded per-step angles onto the `CMoveData`'s angles (`movedata + 0x14/+0x18`), gated by the movement-services vtable **+0x150** flag (`sv_subtick_movement_view_angles`) |
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

### 4.3 The admission rules of the silent channel — **not established**

The *client-side* construction is bounded and ordered:

* one `input_history` entry per **presented frame** (the frame history is a
  `CUtlVector` at `CCSGOInput + 0xbe0` whose count is `+0xbd8` and whose
  capacity is `+0xbe8`; entries are appended in frame order, so the `when`
  ordering is the frame order);
* the entry's timestamps are the render time pair (`entry + 0x00/+0x04`) and
  the player time pair (`entry + 0x08/+0x0c`);
* the shot reads the entry at `attack1_start_history_index` (`pb + 0x3c`),
  which is why a silent-aim write must cover **all** entries, not the newest.

The **server-side** admission rules (the per-command array length cap, the
per-entry angle-delta limit, the tolerance on the `when` values) were **not
derived** — `server.dll` on disk is a different build and was not imported
(see §8).

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

**Re-entrancy.** W1, W2, W6 and W7 all run **on the game thread inside the
input/tick path**; a detour that only writes to memory (no engine calls) is
re-entrant-safe. W7 (`FUN_180b07590`) is the pawn's post-think and runs once
per simulated command; hooking it is the natural place to *win the race*
against the engine's own write-back of the corrected angles.

**The camera and the movement must not be disturbed.** W1 changes the
command's own angles — the channel the **player** follows: measured, it steers
the movement by exactly the written delta and it moves the pawn's own
`m_angEyeAngles`. So a fake yaw written into W1 **does** rotate the movement
basis. If the anti-aim wants the movement to keep following the camera, the
fake angles must be written **after** the movement has consumed the command
(W1 is fine for the *body*, but the movement basis has to be restored
afterwards, or the per-step `yaw_delta` mechanism used instead).

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
   `m_angEyeAngles`. So a 180° fake rotates the walk basis. The movement
   must therefore be re-based (or the per-sub-tick `yaw_delta` used) to keep
   the player's controls on the camera.
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
   server→client) and the exact server-side derivation of the body yaw from
   the command's `viewangles`. The client-side facts are established (it is a
   `CWrappedInterpolatedVar<QAngle>` netvar on the `qangle_precise` channel
   at `+0x3600`, written locally by `FUN_1808cb9b0`); the netvar's
   direction flag was not located in the registration.
   **Experiment:** a live test — write `m_angEyeAngles` to a value 90° off
   and read what a *second* client (or the server's netvar dump) sees; or
   read the netvar's direction bit in the registration (`FUN_180c7fa90`,
   around `0x180c844e5`/`0x180c8454a`).
3. **The silent channel's server-side admission rules** (§4.3): the
   per-command array length cap, the per-entry angle-delta limit and the
   `when`-ordering tolerance. **Experiment:** the same server import; or a
   live probe that pushes N entries with a chosen delta and watches for the
   server's rejection (`CUserMessageLagCompensationError`-style messages, or
   the server's own log).
4. **The animation chain from `m_angEyeAngles` to the bone transforms** —
   which function writes `m_flAimYawAngle`/`m_flPreviousAimYaw` and where the
   `CTransform` bone array is updated. Only the *field names and the class*
   were pinned here (§6); the bone-transform half belongs to the sibling
   "animfix" task. **Experiment:** xref `0x181c0ee70`
   (`"m_flAimYawAngle"`) outside the schema registration and follow the
   writers.
5. **The exact tail arguments of `FUN_180a64570` (`CalcView`)** and of the
   pawn's vtable slot `+0x578` (the "aim angles" getter used by the
   post-think) — the decompilation shows them as `undefined8`/`float*`
   without types.

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
| the movement's **angle** basis | the vprof string `"CPlayer_MovementServices::DoMovement::Subtick"` (`0x181c3b528`) → `FUN_180a65ae0`; the *decoder* that turns the command's `subtick_moves` into the per-slice array (and accumulates `yaw_delta` onto the command's `viewangles`) is the movement services' vtable **+0x138** — **not re-derived here** |
| the movement's sub-tick yaw | the vprof string `"CPlayer_MovementServices::DoMovement::Subtick"` (`0x181c3b528`) → `FUN_180a65ae0` |
| the pawn's post-think | the assert file string `…\game\client\c_baseplayerpawn.cpp` → `FUN_180b07590` |
| the pawn's eye angles | the schema name `"m_angEyeAngles"` (`0x181c83610`) → the schema registration `FUN_180c56fe0` (the offset is the `MOV dword ptr [RAX+0x10], <offset>` next to the name `LEA`) |
| the server's view-angle corrections | the schema name `"m_ServerViewAngleChanges"` (`0x181b182c8`) → the netvar registration `FUN_180457c40` (the offset is the `MOV dword ptr [RSP+0x28], <offset>`) |
| the animation state | the schema class name `"CCSPlayerAnimationState"` (`0x181c0a968`) and the field names `"m_flAimYawAngle"` (`0x181c0ee70`) / `"m_flPreviousAimYaw"` (`0x181c0c2d0`) |
