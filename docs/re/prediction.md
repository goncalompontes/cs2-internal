# Engine prediction (for sub-tick bunnyhop)

> ## ✅ Implemented (2026-10-15/16) — the record-based sub-tick bhop
>
> The payload now implements the authoritative design: `Input::bhop_tick` is a
> read-only `CreateMove` **post** hook that reads the engine's own landing record
> and appends a **release+press pair sharing one `when`** to the command's
> `subtick_moves`. Research report (UC 771389 / 774033 + the community threads):
> [`research-bhop.md`](research-bhop.md). RE below (§"RE findings").
>
> What changed vs the earlier payload bhop: the pair's `when` now comes from
> `m_ModernJump`'s landing record and the command's build tick (not the disabled
> `RunCommand` simulation); the write is gated on `build == landed_tick + 1`
> (placement A) with a B catch-up window, never on the player being airborne; the
> whole-tick `IN_JUMP` bit is no longer cleared (it is the held-key read, and the
> pair supplies the edge); the pair is insertion-sorted into `subtick_moves` so
> the admission gate's monotonicity clause cannot discard the array; and the write
> is idempotent by *array content*, so being called twice per frame is harmless.
>
> The manual `RunCommand` simulation (`simulate_landing`, behind `predict(on)`)
> is kept for reference only — it is **not** part of the bhop anymore. It needs
> the full `CPrediction` save/restore envelope and faults without it
> (`client+0xa51526 read addr=0x0`), and the authoritative design does not use it.
>
> Diagnostics on `input()`: `bhop_writes`, `bhop_presses`, `bhop_landed_tick`,
> `bhop_landed_frac`, `bhop_build`, `bhop_age` (= `build - landed_tick`; note it
> spans the whole airtime between landings, since `landed_tick` only moves on a new
> contact), `bhop_tick_base`, `bhop_client_tick`, `bhop_cmd_number`, `bhop_mode`
> (1 = A, 2 = B, 0 = nothing), `bhop_when`, and the decision counters
> `bhop_a`/`bhop_b`/`bhop_skipped`. **Placement A firing is the phase proof** —
> validated live 2026-10-15 (`bhop` scenario + operator), so no sampled judgement of
> `bhop_age` is needed.


**Status: RE in progress (2026-10-15).** Goal: a *proper* sub-tick bunnyhop. The
community conclusion (UC 754429) and the known-good reference
(`movement/impl/bunnyhop.cpp`) agree: **don't trace the landing — run the
engine's own movement prediction a few ticks ahead and read the landing the
modern-jump system records** (`CCSPlayerModernJump::m_nLastLandedTick` /
`m_flLastLandedFrac`), then place a single sub-tick `IN_JUMP pressed=true` step
at that fraction.

This document tracks the reverse: the movement / prediction functions and the
schema fields the simulation needs. Build **25815307** (the copy in
`C:\Users\gpmpo\ghidra-analysis\client.dll`).

## Why prediction

The engine needs a jump **press edge** at the moment of ground contact; a *held*
`IN_JUMP` never fires (that is why the old payload bhop did nothing). CS2 buffers
a press for a few ticks (`m_nLastUsableJumpPressTick`), but the press must land
close to the contact or it is discarded. Without prediction we only learn the
landing *after* it resolves (too late); the reference simulates up to 4 ticks
ahead and reads the recorded landing.

## Schema (client.dll, generated `payload/sdk/schemas/client_dll.hpp`)

| Class / field | Offset | Notes |
|---|---|---|
| `C_BasePlayerPawn::m_pMovementServices` | `+0x1330` | the `CCSPlayer_MovementServices*` |
| `CCSPlayer_MovementServices` | size `0xfe0` | the movement object to snapshot/restore |
| `CCSPlayer_MovementServices::m_ModernJump` | `+0x6c8` | `CCSPlayerModernJump` (size `0x38`) |
| `CCSPlayer_MovementServices::m_flDuckAmount` | `+0x40c` | duck blend for the accel base |
| `CCSPlayer_MovementServices::m_flStamina` | `+0x694` | stamina |
| `CCSPlayer_MovementServices::m_bHasEverProcessedCommand` | `+0xfd0` | movement has run |
| `CCSPlayerModernJump::m_nLastActualJumpPressTick` | `+0x10` | last real press tick |
| `CCSPlayerModernJump::m_flLastActualJumpPressFrac` | `+0x14` | its fraction |
| `CCSPlayerModernJump::m_nLastUsableJumpPressTick` | `+0x18` | buffered/usable press |
| `CCSPlayerModernJump::m_flLastUsableJumpPressFrac` | `+0x1c` | its fraction |
| `CCSPlayerModernJump::m_nLastLandedTick` | `+0x20` | **the landing tick** |
| `CCSPlayerModernJump::m_flLastLandedFrac` | `+0x24` | **the landing fraction (the press `when`)** |
| `CCSPlayerModernJump::m_flLastLandedVelocityZ` | `+0x30` | landing vz |
| `CBasePlayerController::m_nTickBase` | `+0x6b8` | tick base to compare the landing tick against |
| `C_BaseEntity::m_nActualMoveType` | `+0x526` | skip `noclip`/`ladder` |

## Functions (client.dll RVAs)

Found via the vprof strings (the functions the profiler names), then xrefs and
`resolve_containing_procedure`:

| Function | RVA | Evidence |
|---|---|---|
| `CPlayer_MovementServices::OnPawnProcessCommand` | `0xa6ff20` | vprof `OnPawnProcessCommand`; the per-command movement body (calls `DoMovement::Subtick`, `FinishMove`) |
| `CPlayer_MovementServices::DoMovement::Subtick` | `0xa64c20` | vprof `CPlayer_MovementServices::DoMovement::Subtick` |
| `CPlayer_MovementServices::SetupMove` | `0xa739d0` | vprof `CPlayer_MovementServices::SetupMove` |
| `CPlayer_MovementServices::UpdateInputState` | `0xa764d0` | vprof `CPlayer_MovementServices::UpdateInputState` |
| `CPlayer_MovementServices::RunCommand` | `0xa719a0` | the per-command entry: advances the tick base, then calls `OnPawnProcessCommand` (`0xa6ff20`) and the `+0x180` vtable method. Only caller of `OnPawnProcessCommand`. |
| `CPrediction::Update` / `PerformPrediction` | `0xbf2290` | vprof `CPrediction::Update`, `CPrediction::PerformPrediction executed too many commands`, and the `RunSimulation cmd %d -> slot %d` log |
| prediction per-command simulation | `0xbede70` | called per command from `0xbf2290` (the `RunSimulation` loop body) |

**The simulation seam.** `RunCommand(movement_services, cmd)` (`0xa719a0` on the
25815307 copy; **`0xa72860` on the live 25947352 build** — the curated
`movement_run_command` signature re-resolves it) is the one-tick entry: run it N
times with the command's tick advanced and read `m_ModernJump` after each tick.
It mutates the **real** player (origin, velocity, flags, the movement object), so
a simulation must snapshot and restore:

- the movement services object (`0xfe0` bytes) and the modern/legacy jump blob
  (inside it),
- the pawn's movement state (`m_vecAbsVelocity`, the origin via the game scene
  node, `m_fFlags`, `m_hGroundEntity`, `m_flSimulationTime` / `m_nSimulationTick`),
- the controller's `m_nTickBase`.

`CPrediction::PerformPrediction` (`0xbf2290`) is the engine's own version of this
(save → run commands → restore); it is the reference for the exact field set, but
it is entangled with the command array and the prediction globals.

## Superseded: the `RunCommand` simulation recipe (kept for reference)

> The authoritative design does **not** call `RunCommand` (see below). This
> section documents the community prediction recipe the payload keeps behind
> `predict(on)`; it is not used by the bhop.

The UC recipe (754429, 760037 — Bop32) works only with the full setup; **snapshot /
restore is correct**, and the side effects are handled by saving the right fields
around the call:

- `CPlayer_MovementServices` vtable: **`RunCommand` = slot 29**,
  **`SetPredictionCommand` = slot 43**, **`ResetPredictionCommand` = slot 44**.
  Verified live (25947352): the vtable's RTTI is `.?AVCCSPlayer_MovementServices@@`
  and slot 29/43/44 = RVAs `0x8e3760` / `0x8cb710` / `0x8cb7a0`. **The payload
  reads these from the object's own vtable at runtime** — the reliable source, since
  the slot indices can shift across builds (a curated signature cannot pin them:
  the function that calls `OnPawnProcessCommand` is *not* the vtable slot).
- Save before the call (the recipe's list, to be completed from
  `ProcessMovement`'s own save block — IDA anchor
  `E8 ? ? ? ? ? ? 48 8B CE FF 90 ? ? ? ? 48 85 DB`):
  - globals `CurTime`, `RealTime`, `TickCount`, `FrameTime`, `IntervalPerTick`,
  - movement services `m_nButtonDoublePressed`, `m_nQueuedButtonChangeMask`,
    `m_nToggleButtonDownMask`, `m_nQueuedButtonDownMask`,
  - the pawn movement state + the controller tick base.
- `SetPredictionCommand(cmd)` → `RunCommand(cmd)` → read the post-prediction
  `m_ModernJump.m_nLastLandedTick` / `m_flLastLandedFrac` → restore.
- `CPrediction` flags (`m_bInPrediction` `+0x34`, `m_bFirstPrediction` `+0x80`)
  and the `CPrediction` object offsets are in the thread
  (`m_nReason` `+0x30`, `m_flRealTime` `+0x68`, `m_nMaxClients` `+0x78`,
  `m_dRealTime` `+0x88`, `m_flGameTime` `+0x98`, `m_nTickCount` `+0x10C`).

The landing fraction `m_flLastLandedFrac` (`m_ModernJump +0x24`) is what the
sub-tick press `when` uses; a landing inside the simulated window is the signal.

## RE findings (2026-10-15/16, `client.dll` 25815307, sha `44af2aa0…`)

Re-derived from the Ghidra project (`C:\Users\gpmpo\ghidra-analysis\client.dll`)
and cross-checked against the UC threads (see
[`research-bhop.md`](research-bhop.md)). Addresses are RVAs (image base
`0x180000000`). The live build (`client-25947352.dll`) moves them, so **derive from
the schema fields at runtime** — the bhop uses no hard-coded RVA.

### The jump chain

| Function | RVA | What it is |
|---|---|---|
| `DECIDE` | `0x8c2630` | the per-slice jump gate. Called as `FUN_1808c2630(svc + 0x6c8, movedata)` from `0x8de1d0` / `0x8de840` / `0x8df090`. |
| legacy-jump twin | `0x8c24c0` | called as `FUN_1808c24c0(svc + 0x6b0)` — the `sv_legacy_jump 1` path. |
| `DoJump` | `0x8e6fb0` | the takeoff (alpha/beta paths, the jump impulse, `usable = -1024`). |
| press-edge machine | `0x4c0f10` | `FUN_1804c0f10(&CInButtonState, bit)`. |
| `RunCommand` | `0xa719a0` | advances the controller's tick base, sets `gpGlobals`, calls `OnPawnProcessCommand` (`0xa6ff20`). |
| `OnPawnProcessCommand` | `0xa6ff20` | `SetupMove` → `DoMovement::Subtick` (`0xa64c20`) → `UpdateInputState`. |
| `SetupMove` | `0xa739d0` | fills `CMoveData`; `FUN_180a73c70` copies the command's steps. |
| step copy | `0xa73c70` | copies `subtick_moves` into `CMoveData`'s `0x20`-stride step array (`count` at `+0x60`, base at `+0x68`, terminal `when = 1.0f` at `+0x90`). |
| `sv_bhop_time_window` | `DAT_182529040` | registered by `FUN_1800b1aa0`, default `0x3c000000` = **0.0078125 s**. |
| `sv_jump_spam_penalty_time` | `DAT_182529130` | registered by `FUN_1800b2010`, default `0x3c800000` = **0.015625 s** (exactly one tick). |

The convar descriptions confirm the semantics, from the binary:

> `sv_bhop_time_window` — *"sv_legacy_jump disabled only: The time window (in
> seconds) around landing where a jump press is considered a bhop attempt."*
>
> `sv_jump_spam_penalty_time` — *"For subtick jumps, if this much time **or less**
> has elapsed since the last time the user has pressed the jump key, pretend they
> hadn't."*

### `DECIDE` — confirmed against the binary

- `NOW = { CMoveData + 0xd4 (tick), CMoveData + 0xdc (frac) }` — i.e. `NOW` is in
  **`{tickbase - 1, f}`** coordinates (the same field `DoMovement::Subtick` pulls
  `gpGlobals->tickcount` from per slice).
- the window half-width really is `hW = sv_bhop_time_window * 0.5` (`* 0.5` appears
  at four sites) and the comparison is `usable ∈ [landed - hW, landed + hW)`.
- the spam penalty is `if (penalty < elapsed) { window logic } else edge = 0` —
  i.e. penalised when `elapsed <= penalty`, **inclusive**, so a press one tick
  after the previous one is swallowed.
- `usable = 0xfffffc00` (`-1024`) is the discard sentinel.
- the edge is sampled as `edge = FUN_1804c0f10(svc + 0x50, IN_JUMP)`, and `DoJump`
  runs when `(sv_autobunnyhopping && (svc+0x58 & IN_JUMP)) || edge`.

`FUN_1804c0f10` decompiles to `edge = C || (A && B)` over the three words at
`state+8/+0x10/+0x18`: A = held now, B = changed since the command start,
C = pressed and released within it. Hence a **lone press while the key is held at
command start produces no edge** and the primitive is the release+press pair on
one `when`.

### `DoJump` — the alpha path

`FUN_1808e6fb0` overwrites the horizontal velocity from the record —
`*(movedata + 0x38) = *(m_ModernJump + 0x28)` and `+0x3c = +0x2c`, i.e. the
recorded `m_flLastLandedVelocityX/Y` — on the path taken when the press is inside
the window and the landed speed beats the current one. That is the
speed-preserving *alpha* takeoff; a whole tick late leaves the ground-slice clamp
(the *beta* path, the observed speed loss). The schema offsets are therefore
confirmed from code, not just the dump:

`m_ModernJump` (`svc + 0x6c8`): `+0x10/0x14` actual press (`{tick, frac}`),
`+0x18/0x1c` usable press, `+0x20/0x24` **landing record**, `+0x28/0x2c/0x30`
landed velocity X/Y/Z.

### The command's build tick (the phase)

`RunCommand` (`0xa719a0`) does, per simulated command:

```c
iVar10 = controller->m_nTickBase;                 // +0x6b8
controller->m_nTickBase = iVar10 + 1;
log("client advancing tick base to %u for command %d", iVar10 + 1, cmd->nSequenceNumber);
gpGlobals->tickcount = controller->m_nTickBase;    // = iVar10 + 1
gpGlobals->curtime   = controller->m_nTickBase * 0.015625;
```

so **after the engine simulates a command, `CBasePlayerController::m_nTickBase`
(+0x6b8) equals that command's tickbase**, and a command the engine has *not*
simulated yet reads one behind — the phase correction
`build = m_nTickBase + (fresh ? 1 : 0)`, `fresh = (command number changed since
the previous frame)`. The landing record lives in `{build - 1, f}`, so the pair
belongs in the command with `build == landed_tick + 1`.

`CBaseUserCmdPB::client_tick` (`base + 0x54`) is built in the slot-5 builder as
`max(gpGlobals->tickcount, previous command's client_tick)` — the same counter
sampled at build time. It is exposed as a diagnostic (`bhop_client_tick`) so the
phase can be cross-checked live without re-deriving anything: offline the two
must agree, i.e. `bhop_age == 1` on the frame the pair is placed.

### The write

`base->subtick_moves` (`base + 0x18`) is a `RepeatedPtrField` (`current_size@+0x20`,
`rep@+0x28`, elements `CSubtickMoveStep` `0x38`); `FUN_180a73c70` copies it into
`CMoveData` verbatim (no sentinel element — the terminal fraction is a field,
`+0x90 = 1.0f`), so appended steps are simulated. The engine's *own* steps live in
the same field (a real `IN_JUMP` press/release at `when = 0` on the tick the key
changes, none while it is held) and must be preserved. The admission gate rejects
the **whole** array when `count > 32`, `when` decreases or exceeds 1.0,
`popcount(button) != 1`, or the view-delta accumulator exceeds 2.0 — so the pair
is budget-checked and **insertion-sorted** into the field.

### What the payload implements

| Piece | Where |
|---|---|
| read the record, derive `build`, choose `when` (A/B), write the pair | `Input::bhop_tick` (`payload/control/src/input.cpp`) |
| budget + monotonicity + append | `Input::write_bhop_pair` |
| idempotency by array content | `Input::bhop_pair_present` |
| state / diagnostics | `input()` status (`service.cpp`), `Input::bhop_*` |

Not implemented (offline it is invisible, and it needs engine calls the payload
avoids): the UC 771389 Part IX **fresh-frame hull-trace guess** that lets an
in-tick landing on a command's *last* frame be answered before the command is
re-sent. On a listen server every frame re-runs the same command, so the
reuse-frame patch is sufficient.

