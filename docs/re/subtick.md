# Subtick (`client.dll`, build 25815307) — first pass

CS2 records input at **sub-tick** resolution: each command carries a history of
timestamped **view angles** and a list of **sub-tick move/button steps**, so the
server can reconstruct the exact input within a tick. This is the layer an aim /
autostrafe feature must write — the plain `CUserCmd` button mask
(`docs/re/client-dll.md` §"Input path") drives *discrete* buttons but not angles
or analog movement.

Everything below is from the Hex-Rays/Ghidra decompilation of the client's own
protobuf (`ByteSizeLong`/`MergePartialFromCodedStream` walk every field, so the
offsets are exact), the authoritative field names from the public
`GameTracking-CS2` proto, and the community structs. Addresses are VAs for the
image at `0x180000000`; RTTI recipe in `README.md` §3.

## Classes (RTTI anchors)

| Class | `_TypeDescriptor` name VA | COL VA | vtable VA | size |
|---|---|---|---|---|
| `CSGOUserCmdPB` | `0x1822e1c80` | `0x181eecf88` | `0x181bc6dd0` | `0x48` |
| `CBaseUserCmdPB` | `0x1822d19c0` | `0x181ec66f0` | `0x181b27638` | (see below) |
| `CSGOInputHistoryEntryPB` | `0x1822e1c50` | `0x181eecdf0` | `0x181bc6d30` | `0x78` |
| `CSubtickMoveStep` | `0x1822d1960` | `0x181ec6800` | `0x181b274f8` | `0x38` |
| `CInButtonStatePB` | `0x1822d1938` | `0x181ec6778` | `0x181b27458` | `0x30` |
| `CFixedSizeCircularBuffer<CSGOInputHistoryEntryPB,16>` | `0x18230e600` | — | — | — |

The `CUserCmd` (`stride 0x98`) embeds `CSGOUserCmdPB` at `+0x10`
(`docs/re/client-dll.md` §"Input path"); so `CUserCmd + 0x10` == `CSGOUserCmdPB*`.

### `CSGOUserCmdPB` (`cmd + 0x10`)

Confirmed from its `ByteSizeLong` (`FUN_1807afce0`, vtable slot 7) and parser
(`FUN_1807afe20`):

| Offset | Type | Field |
|---|---|---|
| `+0x08` | — | internal metadata |
| `+0x10` | `uint32` | `_has_bits_` (bit 0 = `base`, …) |
| `+0x14` | `uint32` | cached size |
| `+0x18` | `RepeatedPtrField` | **`input_history`** — `current_size@+0x20`, `total_size@+0x24`, **`rep@+0x28`** (array of `CSGOInputHistoryEntryPB*`) |
| `+0x30` | ptr | **`base`** — `CBaseUserCmdPB*` (has_bits bit 0) |
| `+0x3c` | `int32` | `attack1_start_history_index` |
| `+0x40` | `int32` | `attack2_start_history_index` |

`base` is the inner message that actually holds the buttons/moves (field 1).
`input_history` is the per-sub-tick angle list (field 2).

### `CSGOInputHistoryEntryPB` — the angle history

A sub-tick angle sample. From its `ByteSizeLong` (`FUN_1807aec00`) and the ctor
`FUN_1807b1240` (zeroes `+0x08..+0x74`, sets `+0x74 = -1`):

| Offset | Type | Field |
|---|---|---|
| `+0x10` | `uint32` | `_has_bits_` |
| `+0x18` | ptr | **`view_angles`** — a `CMsgQAngle` (alloc `0x28`): `x@+0x18`, `y@+0x1c`, `z@+0x20` (3× `float`, has_bits `+0x10` bits 1/2/4) |
| `+0x20`/`+0x28`/`+0x30`/`+0x38` | ptr | further sub-messages (`view_angles` siblings: target/shoot positions, timing) |
| `+0x40` | ptr | another 3-float message (`shoot_position`-ish) |
| `+0x54` | `int32` | input-history index |
| `+0x60`/`+0x64`, `+0x68`/`+0x6c` | `{int tick, float frac}` | time pairs (render / player) |
| `+0x70` | `uint32` | 0 at ctor |
| `+0x74` | `uint32` | `-1` at ctor |

`view_angles` is the **aim seam**: `cmd->pb->input_history[i]->view_angles`
(`{pitch, yaw, roll}` floats, per the proto `CMsgQAngle`). A shot reads the entry
at `attack1_start_history_index` (`pb+0x3c`), so a silent-aim write must cover
**all** entries, not just the newest. The default (empty) instance is
`PTR_PTR_1822002e8` (`x/y/z @ +0x18/+0x1c/+0x20`).

### `CBaseUserCmdPB` — buttons and movement

The offsets below are exact from the build's own wire parser `FUN_1804f5880`
(`MergePartialFromCodedStream`; each `switch` case is `wire_tag >> 3` = the field
number, and the store gives the offset) and its `ByteSizeLong`
(`FUN_1804f5430`, vtable slot 7); the field *names* are the authoritative ones
from
[`SteamDatabase/GameTracking-CS2 Protobufs/usercmd.proto`](https://github.com/SteamDatabase/GameTracking-CS2/blob/master/Protobufs/usercmd.proto)
(checked 2026-10). The build's field *numbers* match master; its `_has_bits_`
assignment does not (it is declaration-order based, see the bits column).

| Offset | Type | Field (number, name) | has-bit | Notes |
|---|---|---|---|---|
| `+0x10` | `uint32` | `_has_bits_` | — | |
| `+0x18` | `RepeatedPtrField` | **18 `subtick_moves`** | — | `current_size@+0x20`, `total_size@+0x24`, `rep@+0x28`; elements `CSubtickMoveStep` (`0x38`) |
| `+0x30` | ptr | 19 `move_crc` | `0x1` | `bytes` (arena string; length at `+0x10`) |
| `+0x38` | ptr | **3 `buttons_pb`** | `0x2` | `CInButtonStatePB*` — `buttonstate1/2/3` at `+0x18/+0x20/+0x28`, has-bits `1/2/4` |
| `+0x40` | ptr | **4 `viewangles`** | `0x4` | `CMsgQAngle*` (alloc `0x28`; `x/y/z@+0x18/+0x1c/+0x20`) |
| `+0x48` | ptr | 22 `execution_notes` | `0x8` | `CBaseUserCmdExecutionNotes*` (`ignored_reason` string) |
| `+0x50` | `int32` | 1 `legacy_command_number` | `0x10` | |
| `+0x54` | `int32` | 2 `client_tick` | `0x20` | |
| `+0x58` | `float` | **5 `forwardmove`** | `0x40` | analog move — **this build sets it from the raw input** |
| `+0x5c` | `float` | **6 `leftmove`** | `0x80` | |
| `+0x60` | `float` | **7 `upmove`** | `0x100` | |
| `+0x64` | `int32` | 8 `impulse` | `0x200` | |
| `+0x68` | `int32` | 9 `weaponselect` | `0x400` | |
| `+0x6c` | `int32` | 10 `random_seed` | `0x800` | |
| `+0x70`/`+0x74` | `int32` | 11/12 `mousedx`/`mousedy` | `0x1000`/`0x2000` | |
| `+0x84` | `int32` | 14 `pawn_entity_handle` | `0x20000` | |
| `+0x88` | `int32` | 17 `prediction_offset_ticks_x256` | `0x4000` | |

> **Correction (2026-10).** An earlier revision of this file listed `+0x30` as
> `buttons_pb` and `+0x38` as "field 3". The parser shows the reverse: `+0x30` is
> `move_crc` (a `bytes` field) and **`buttons_pb` is at `+0x38`**. The payload's
> `usercmd` backend writes the *legacy* `cmd + 0x60` mask but never
> `base + 0x38` — see "The command build" below.

The three `float` scalars at `+0x58/+0x5c/+0x60` (fields 5/6/7) are the legacy
`forwardmove`/`leftmove`/`upmove`. **The build *does* fill them** — `create_move_input`
copies `CCSGOInput + 0x270` → `forwardmove` and `+0x274` → `leftmove` every tick
(see "The command build"). The earlier live note "holding `+forward` sets no move
float in the command" therefore means the probe was reading the wrong command /
the wrong moment, not that the field is unused.

> The proto field *names* are not in the binary (Valve strips them), so they come
> from the public `GameTracking-CS2` proto above; the offsets, types and field
> *numbers* are exact from the build.

### `CSubtickMoveStep` — the movement step

`0x38` bytes (from its destructor `FUN_1804f4ae0`, which frees `0x38`). From its
`ByteSizeLong` (element loop of `FUN_1804f5430`) the element has 7 presence
fields: one `uint64` at `+0x18`, one bool (2-byte varint) at `+0x20`, and five
4-byte scalars at `+0x24/+0x28/+0x2c/+0x30/+0x34`. The authoritative proto names
are `button` (uint64), `pressed` (bool), `when` (float), `analog_forward_delta`
(float), `analog_left_delta` (float), `pitch_delta` (float), `yaw_delta` (float)
— the last two are wire fields **8/9** (not 6/7). The serializer
`FUN_1804f7530` walks a `RepeatedPtrField` of these.

> Appending one of these to `subtick_moves` with `analog_forward_delta` /
> `analog_left_delta` (and a `when` fraction in `[0,1)`) is the per-sub-tick
> movement channel. Per the community recipe the delta is *relative to*
> `base->forwardmove`/`leftmove` (`delta = base - desired`) and existing steps
> are cleared first — appending an absolute value on top of the base double-counts.

## `CCSGOInput` sub-tick history

`CCSGOInput` (object `+0x2572460`) keeps the raw per-tick history
(`docs/re/client-dll.md` §"Input path"):

| Offset | Type | Meaning |
|---|---|---|
| `+0xbc0` | `int32` | attack-1 start history index (`-1` = none) |
| `+0xbc4` | `int32` | attack-2 start history index |
| `+0xbc8` | `int32` | entry count |
| `+0xbd0` | ptr | array of entries, **stride `0x60`** |

Each entry (stride `0x60`); from `FUN_180cf9c30`:

| Offset | Type | Meaning |
|---|---|---|
| `+0x00`/`+0x04` | — | time/frac pair (copied to `CSGOInputHistoryEntryPB +0x60`) |
| `+0x08`/`+0x0c` | — | second time pair (`+0x68`) |
| `+0x10`/`+0x14`/`+0x18` | `float` | **view angles** (x/y/z) |
| `+0x54` | `int32` | index in the history |

## The flow

- **Units and has-bits (measured live 2026-10-15, `movement-probe`).** With a
  console `+forward` held, the command the engine builds carries
  **`base->forwardmove = 1.0`** and the raw axes `CCSGOInput + 0x270` read
  **`1.0`** — i.e. the engine's own input is in `±1` units (everything downstream
  scales by `cl_forwardspeed` = 450), and `sv_quantize_movement_input` rounds it
  to `{-1, 0, +1}`. The command's `_has_bits_` (read from `base + 0x10`) was
  `0x24867`: it contains **`0x40`** (our parser-derived `forwardmove` bit) and
  *not* `0x80`/`0x100`, so the engine sets the bit **only for the axes it uses**
  (our payload ORs all three — harmless but a deviation). `m_bFreezePeriod` etc.
  aside, this settles the two guesses the movement write depended on.
- **The engine queue its own sub-tick step for a button transition.** The same
  probe read one `CSubtickMoveStep` with `button = 0x8` (`IN_FORWARD`),
  `pressed = true`, `when = 0` on the tick the button changed, and none while the
  button stayed held (`steps = 0`) — consistent with UC 769450's warning that the
  repeated field carries the engine's own press/release steps.
- **Slot 5 (`FUN_180b64e60`, vtable `+0x28`, `create_move_input`) is the per-tick
  builder.** It runs the `GetUserCmd` chain, then fills the command's
  `CBaseUserCmdPB` (move axes, buttons, `random_seed`, `client_tick`) — see the
  next section. It then dispatches to the sub-tick history filler.
- **Slot 25 (`FUN_180d01610`, vtable `+0xc8`, `create_move`) fills the angle
  history.** It iterates the `+0xbd0` entries and, per entry, builds a
  `CSGOInputHistoryEntryPB`: `FUN_1807b1240` (ctor) → `FUN_180cf9c30` (copy
  fields, allocate `view_angles`) → `FUN_180cfd450` (read the angles back /
  apply). It also writes the attack-history indices (`pb+0x3c`/`+0x40`).
- `FUN_180cf9c30(entry, pb, …, when, …, ctx)` maps the `+0x60`-stride entry into
  the PB: `view_angles` (`pb+0x18`, xyz from `entry+0x10/+0x14/+0x18`), the
  `+0x38`/`+0x40` sub-messages, and the `+0x60`/`+0x68` time pairs.
- `FUN_180cde730` allocates the next `CSGOInputHistoryEntryPB` from the command's
  `input_history` arena (`cmd+0x10 → +0x18`) and records it.
- `FUN_1807b0990` / `FUN_1804f7530` are the wire serializers for the
  `input_history` / `subtick_moves` repeated fields.
- The consumer side is `CPlayer_MovementServices` (next sections).

## The command build (`create_move_input`) — where the move axes come from

`CCSGOInput::CreateMove` slot 5 (`FUN_180b64e60`, vtable `+0x28`) is the per-tick
builder: it runs the `GetUserCmd` chain, then fills the command's `CBaseUserCmdPB`
directly. The fields it *derives* (confirmed from the decompilation):

| Command field | Built from |
|---|---|
| `base->forwardmove` (`+0x58`) | `*(float*)(CCSGOInput + 0x270)` — 0 ⇒ the field is cleared |
| `base->leftmove` (`+0x5c`) | `*(float*)(CCSGOInput + 0x274)` |
| `base->buttons_pb` (`+0x38`) | the input button state (three `uint64`s → `+0x18/+0x20/+0x28`) |
| `base->random_seed` (`+0x6c`) | `MD5_PseudoRandom(sequence)` |
| `base->client_tick` (`+0x54`) | the controller tick |

So **the analog move the command carries is sourced from `CCSGOInput + 0x270`
(forward) / `+0x274` (left)** — the raw input cache — *not* from the command
itself. Writing `base->forwardmove` after slot 5 has run changes the field for
that one tick, but the next tick's `create_move_input` overwrites it from the
(unchanged) raw input. That is why the community movement *fix* only ever
*re-maps* a key that is actually held: the raw input is still the source of
truth.

> **`+0x270` is also written *inside* slot 5.** `create_move_input` calls
> `FUN_180b5f7b0(CCSGOInput, 0)` (the `slot` arg is 0 for the local player), which
> copies the latest per-controller input entry (array `CCSGOInput + 0xb58`,
> stride `0x440`; the move pair at entry `+0x18`) into `CCSGOInput + 0x270`.
> So a write made *before* the original is clobbered; the payload therefore
> writes the raw axes **both before and after** the original (the after-write
> survives to any post-`CreateMove` reader), plus the command fields. The fully
> faithful seam (writing the input entry itself) is the next refinement.

> This is the concrete "input side" seam the previous revision left open:
> `CCSGOInput + 0x270/0x274`. Writing it around slot 5 is how the payload now
> tries to synthesise movement the way the console `+forward` does.

## The movement consumer (`CPlayer_MovementServices`)

The movement services consume the command; the schema field names (`.rdata`
block at `0x181c33800`, the build's own names) are:

| Field | Meaning |
|---|---|
| `m_nButtons` | the held button mask |
| `m_flCmdForwardMove` / `m_flCmdLeftMove` / `m_flCmdUpMove` | the *command's* move (copied in) |
| `m_flForwardMove` / `m_flLeftMove` / `m_flUpMove` | the current move |
| `m_flMaxspeed` | speed cap |
| `m_arrForceSubtickMoveWhen` | `CNetworkArrayBase<float,3>` — forces sub-tick move timing (the "de-subtick" knob) |

`CPlayer_MovementServices::OnPawnProcessCommand` (`FUN_180a6ff20`) →
`CPlayer_MovementServices::DoMovement::Subtick` (`FUN_180a64c20`, vprof name
string `0x181c35508`): it iterates a **decoded** sub-tick array (count `@+0x60`,
array `@+0x68`; element: `when@0`, button/pressed `@8`, forward/left delta
`@0x10/@0x14`, angles `@0x18/@0x1c`) and applies each step. The decoded struct is
*not* the protobuf — the protobuf `subtick_moves` are translated into it before
the movement runs, so an appended `CSubtickMoveStep` only matters if the command
is the one that gets decoded.

## Community cross-check (UnknownCheats)

Checked against the community reverse (2026 threads) and the authoritative
protobuf. The structures agree; the movement conclusion is now sharper.

- **Authoritative proto** (`GameTracking-CS2`, `usercmd.proto` / `cs_usercmd.proto`,
  checked 2026-10): `CSubtickMoveStep { button=1, pressed=2, when=3,
  analog_forward_delta=4, analog_left_delta=5, pitch_delta=8, yaw_delta=9 }`;
  `CBaseUserCmdPB { legacy_command_number=1, client_tick=2, buttons_pb=3,
  viewangles=4, forwardmove=5, leftmove=6, upmove=7, …, subtick_moves=18,
  move_crc=19, …, execution_notes=22 }`; `CSGOUserCmdPB { base=1,
  input_history=2, attack1_start_history_index=6, attack2_start_history_index=7,
  … }`; `CSGOInputHistoryEntryPB { view_angles=2, render_tick_count=4, …,
  shoot_position=66, target_*_check=67..69 }`. **Field numbers match this build**
  (the parser's switch cases); `pitch_delta`/`yaw_delta` are 8/9 (not 6/7).
- **`CSubtickMoveStep` layout confirmed** (UC 769450, "antiaim movement /
  subticks"): `vtable@0`, `metadata@8`, `has_bits@0x10`, `cached_size@0x14`,
  `nButton@0x18`, `bPressed@0x20`, `flWhen@0x24`, `flAnalogForwardDelta@0x28`,
  `flAnalogLeftDelta@0x2c`, `flPitchDelta@0x30`, `flYawDelta@0x34` — matches the
  offsets derived here. `has_bits` bits `0x1/0x2/0x4/0x8/0x10/0x20/0x40` for
  `button/pressed/when/forward/left/pitch/yaw` (confirmed again from the
  element loop of `CBaseUserCmdPB::ByteSizeLong`).
- **The append recipe** (UC 761983, `AddAnalogSubtick`): the field at
  `base+0x18` is a `RepeatedPtrField` (`arena@0`, `current_size@8`,
  `rep@0x10`; `rep = { allocated@0, elements@8 }`); reuse
  `elements[current_size++]` when `current_size < allocated`, else
  `CreateSubtickMoveStep(arena)` (`FUN_1804f7120`) + `Add(field, step)`
  (`FUN_18122d140`) — both now signatured (`subtick_step_alloc`,
  `repeated_ptr_add`). Confirmed against `FUN_18122d140`: `Add` writes
  `elements[current_size]` and increments `current_size` (it does **not**
  pre-increment), and `FUN_18122d2b0` shows the field's arena really is at
  `field+0`.
- **The movement fix** (UC 769450; UC 688985 "Protobuf & Subtick"): the working
  recipe **clears the existing `subtick_moves` first**, then per step sets
  `when = i/iterations` and `analog_forward_delta = base->forwardmove() - move.x`,
  `analog_left_delta = base->leftmove() - move.y` — i.e. the step is a *delta
  relative to the base move*, not an absolute value. It also rewrites
  `base->forwardmove`/`leftmove` and the command buttons. (Our payload's step
  writer was defined but never called until the second pass above; it now writes
  the residual relative to the base in `step` mode and never clears existing
  steps.)
- **Silent aim** (UC 772223 / 730511): "write your aim angle strictly to the
  subtick input history entries and leave `base->viewangles`" — confirms the
  `input_history.view_angles` seam this repo uses. Note the entries are plural
  and `attack1_start_history_index` (`pb+0x3c`) selects the entry a shot reads;
  writing only the *newest* entry may miss it.
- **Movement conclusion (revised, then settled 2026-10-15).** With the community
  recipe implemented on **both** the slot-25 and slot-5 commands the player did
  not move — the reason recorded here was that the engine reads its own input
  state. **That was wrong**: the write *does* move the player once the request is
  in the engine's own units (`±1`, not `±450`) and the measurement starts from an
  unobstructed spot (see §"Actionable seams" item 2 — `move(1.0)` = 1.01× the
  console path, `move(0.5)` = 0.57×). The 450-unit write, a blocked test
  direction and freeze-polluted runs each produced the same "does not move"
  symptom, which is what hid the working seam.

## Community cross-check, second pass (2026-10-15)

Re-read after the live "analog movement does not work" result, against the 2025–26
threads. Four things change the picture:

- **`sv_quantize_movement_input` (rounding) is a first-class blocker.** On (the
  default on Valve servers, UC 729264 "Working around sv_quantize_movement_input
  for auto strafer", 2025-12-13; UC 753644 "movement fix +
  sv_quantize_movement_input", 2026-05-21) the server **rounds
  `forwardmove`/`leftmove` to integers**, so only the nine `{-1, 0, 1}²` move
  states survive and fractional values are dropped. Every community fix is either
  "force the float onto that set" (`MovementToValidSet`, UC 759116), "change yaw
  to one of the 8 discrete directions", or the sub-tick `yaw_delta` step. Our
  `move()` never snapped to the set, so the write could be quantised away
  client-side *before* it ever reached the simulation. The payload now snaps in
  `quantize` mode (`valid_set()` in `payload/control/src/input.cpp`).
- **The sub-tick step is a *delta relative to the base move*, and the engine adds
  it.** UC 761983 / UC 769450 (`antiaim movement / subticks`, 2026-08-28):
  `analog_forward_delta = base->forwardmove() - move.x`, `when = (1/12)*i`, and the
  strafer accumulates locally as it goes. So a step is for analog the base does
  **not** already carry ("analog-delta subticks are for analog that did not already
  exist in the list") — appending a step equal to the base double-counts. The same
  reply warns that **stuffing or clearing the repeated field destroys the engine's
  own button steps** (scroll-wheel jump died): the field holds real
  `IN_JUMP` press/release steps, so append, do not clear.
- **`when = 1.f` is the working value for a single-step angle/move patch.**
  UC 753644's fix only worked after `set_when(0.f)` → `set_when(1.f)` (2026-05-24);
  UC 769450's "blank subticks" were a wrong struct/`has_bits` (its `metadata@8`,
  `has_bits@0x10`, `nButton@0x18`, `bPressed@0x20`, `flWhen@0x24`, forward/left
  `0x28/0x2c`, pitch/yaw `0x30/0x34` — matches our offsets).
- **The command that gets simulated is the one built in slot 5.** YouGame 336028
  ("subtick bhop", 2024-11-26): *"you can't do subtick stuff properly on another
  index, only 5 — subtick works proper"*; the same thread shows the `add` +
  `CSubtickMoveStep` helpers (`E8 ? ? ? ? 48 8B D0 48 8D 4F 18 …`), i.e. our
  `subtick_step_alloc` / `repeated_ptr_add`. UC 759116 (2026-06-24) instead calls a
  single `QueueForceSubtickMove(base)` helper (that build's `client.dll + 0x9CA720`)
  which returns a ready step — the engine's own "force sub-tick move" queue, the
  same knob as `m_arrForceSubtickMoveWhen` above.

- **`sv_subtick_movement_view_angles`** (new cvar, on by default since the
  2025-07-30 update) makes sub-tick view-angle changes **steer movement**, and
  only sends them alongside other sub-tick events (2026-09-26 update note). It is
  a server cvar, and our harness runs a local listen server, so a probe can turn it
  off to isolate the analog channel from the angle channel.

**What the payload now does about it** (`payload/control/src/input.cpp`):

| ingredient | `move(...)` flag | write |
|---|---|---|
| valid set | `quantize` | `resolve_axes()` snaps to the engine's valid set `{-1, 0, +1}` (`k_move_scale = 1.0` — the units the engine's own input uses, so the earlier `±450` was wrong) for the base floats, the raw axes and the input entry |
| sub-tick residual | `step` | `write_move_steps()` appends 12 `CSubtickMoveStep`s (`when = i/12`, `has_bits` assigned to exactly `{when, forward, left}`), delta = `requested - quantized` |
| input-entry seam | `raw_entry` | `write_entry_move()` writes `CCSGOInput + 0xb58 + 0x18` (entry 0 = local player) *before* slot 5 — **UNSAFE: this crashed the game (input-path AV at `client.dll + 0xb64a80`), and the same offset reads denormals, so the layout does not hold for this build. Do not enable it.** |

`quantize`/`step` default off, so the validated paths are unchanged (and both are
neutral for a full-speed request — see "Actionable seams"). The
`input` command also reports the decoded probes the probe scenario needs:
`base_has_bits`, `base_move`, `raw_move`, `entry_move`, `steps`/`step_dump`,
`entry_writes`, `step_writes`.

## The movement yaw channel (measured live 2026-10-15)

Auto-strafe needs a **yaw change inside the tick** as well as the wish direction.
Two candidate channels exist, and only one of them steers movement:

| write | `look(...)` | walk direction from a 90 deg write | eye angles (`m_angEyeAngles`) before / after |
|---|---|---|---|
| `input_history[i].view_angles` (the silent-aim seam) | `via="usercmd"` | **0 deg** — inert (191.9 units, same as the straight walk, writes landing: `angle_writes` 462-942) | 0 / 0 |
| the **raw** per-sub-tick input history (`CCSGOInput + 0xbd0`, the source the PB history is built from) | `via="usercmd", raw=True` | **0 deg** — inert | 0 / 0 |
| **`CSubtickMoveStep.yaw_delta`** (one step/tick, `when = 1`, no button/analog) | `via="usercmd", step_yaw=<deg>` | **90 deg** — steers | **0 / 0 — SILENT** |
| the command's **own** `viewangles` (`base + 0x40`, field 4, has-bit `0x4`) | `via="usercmd", command=True` | **90 deg** — steers (208.1 units); a *rotating* write curves the path | 90 / 90 — visible |

Consequences (2026-10-15):

- **The silent movement-yaw channel is `CSubtickMoveStep.yaw_delta`.** A yaw-only
  step (`has_bits` = `{when, pitch, yaw}`, `button`/`pressed`/analog left at 0) turns
  the walk exactly by its `yaw_delta` while the pawn's eye angles (and therefore the
  camera and what the server sees) stay put. That is what an auto-strafe needs: per
  tick, compute the wish yaw from the velocity (velocity yaw ± the airstrafe angle)
  and write the **delta relative to the camera**, alongside
  `move(forward=…, side=…)`.
- The command's own `viewangles` steers movement too, but it is a **visible** turn
  (the eye angles follow), so it is the wrong channel for a silent strafe (it is the
  classic "turn per tick" kind).
- Writing `input_history.view_angles` — the silent-aim seam — and the raw
  per-sub-tick input history (`CCSGOInput + 0xbd0`) do **not** steer movement at all,
  even with `sv_subtick_movement_view_angles 1`. Silent *aim* (shots/animation) and
  silent *movement yaw* are different channels.
- Measured by `strafe-probe` (one pinned spot, camera reset and verified per case,
  direction = `atan2(dy, dx)`, `eye_yaw_before`/`eye_yaw` as the silence proof).

**Implemented as a payload feature** (`payload/control` `strafe` command): while
airborne and above ~25 units/s the payload computes the community's optimal
air-strafe wish angle from the velocity (`velocity_yaw ± (90° − asin(wishspeed /
speed))`, side from the velocity's position relative to the camera), turns the
command's movement yaw onto it with `subtick` (default 12) `yaw_delta` steps split
across the tick, and holds the analog move at full forward in that rotated frame.
On the ground it writes nothing.

**Status (2026-10-15, end of session): the menu-enabled feature is NOT working
in-game** (operator-confirmed, both strafe and bhop), even though both ingredients
were measured working through the *harness-driven* path:

| measurement (harness driving the write) | result |
|---|---|
| `move(forward=1.0)` (raw axes + base floats) | 204.5 units/s = **1.01×** the engine's own walk |
| a `yaw_delta = 90°` step alone | turns the walk **89.9°** (eye angles 0 → 0, silent) |
| hop with the strafe enabled + `move()` driving (one run) | +66.3 units/s in the air, 0.0° drift |

Leading hypothesis for the in-game failure: `strafe_tick()` writes **only the
command's base floats** (`base + 0x58`), which `create_move_input` re-derives from the
**raw axes** (`CCSGOInput + 0x270/0x274`) inside slot 5 — the validated path wrote
those raw axes as well (`write_raw_move()`). Next probe: have `strafe_tick()` write
the raw axes / input entry with the strafe's `(1, 0)` in the rotated frame, and keep
the yaw-delta steps. For bhop, the assist gates on the player's held jump being
visible in `cmd + 0x60` — only ever verified for a *console* `+jump`, never a real key
press; if it is invisible there, read the engine's own input state instead. Full
steps in the wrap-up section at the top of `docs/handoff.md`. Both features are owned
by the overlay menu's `Movement` tab (`wire::Settings.auto_strafe`/`bhop`, ADR-0021 —
the same settings block
the ESP toggles use), the payload seeds their initial state from
`CS2_PAYLOAD_MOVEMENT`, and the `strafe`/`bhop` control commands override them for
tests.


1. **Aim / silent aim — implemented and validated live.** The payload writes
   **every** `input_history` entry's `view_angles` (`entry + 0x18`, 3 floats)
   every tick (`look(..., via="usercmd")`). Live: the write lands, the **camera
   does not follow**, and the **shot uses the angle** — with the camera 45° off a
   bot (yaw 137.06) a silent aim at yaw 92.06 landed hits (hp 67→51, 51→34).
   `subtick-shot` validates it by the target's **health** (a `wait_for(event=…)`
   started after the shot can never see the hit).
   The source of the copied angles is `CCSGOInput + 0xbd0` (stride `0x60`); write
   that (before the original runs) as the alternative if a future build reads the
   source instead.
2. **Autostrafe / analog movement — WORKS (validated live 2026-10-15).** The
   payload writes the raw axes `CCSGOInput + 0x270/0x274` before/after the original
   slot-5 call, the command's move floats and the move buttons on both
   `cmd + 0x60` and `buttons_pb`. The missing ingredient was the **units**: the
   engine's own input writes `±1` (a console `+forward` gives
   `forwardmove = 1.0` on the command *and* `1.0` on the raw axes; `cl_forwardspeed`
   = 450 is applied downstream), so writing `450` moved nothing —
   `k_move_scale` is now `1.0`. Measured from one fixed, unobstructed spot with a
   console baseline of **203 units** in 1 s:

   | write | moved | vs console |
   |---|---|---|
   | `move(forward=1.0)` | 204.5 | **1.01×** |
   | `move(forward=0.5)` | 115.0 | **0.57×** (the analog value is honoured) |
   | `move(forward=1.0, quantize=True)` | 208.0 | 1.02× |
   | `move(forward=1.0, quantize=True, step=True)` | 205.6 | 1.01× |
   | `move(forward=0.5, quantize=True, step=True)` | 205.1 | 1.01× (base snapped to 1 dominates) |

   So the command-side seam **does** synthesise movement (the earlier "it never
   moves" conclusion came from three confounds: the 450-unit guess, a spot
   obstructed in the test direction, and runs polluted by freeze/round cycling).
   `movement-probe` is the harness for this: it kicks the bots, sweeps for a
   **clear heading**, pins a **test spot** (teleporting back and waiting for the
   teleport to land *and* for zero velocity before every measurement), reports a
   console baseline, and asserts the screen is movement-capable around each
   measurement. **`raw_entry` is unsafe:** writing the per-controller input entry
   (`CCSGOInput + 0xb58 + 0x18`) took the game down with an access violation in the
   input path (`client.dll + 0xb64a80`) and the same offset *reads* denormals — the
   layout does not hold for this build; re-derive it before re-enabling. The
   console `+forward` path remains the fallback.
3. **Command buttons: `base->buttons_pb` (`base + 0x38`).** The payload sets/clears
   the move bits there (`CInButtonStatePB::buttonstate1` at `+0x18`) in addition to
   the legacy `cmd + 0x60` mask, since movement may read either.
4. **Sub-tick buttons** — append `CSubtickMoveStep`s (`button`, `pressed`) for
   exact press timing (the legacy `cmd + 0x60` mask is coarser). The engine already
   writes real `IN_JUMP` press/release steps for its own input, so **append**, and
   never clear the field (UC 769450). `write_move_steps()` uses the same allocation
   path for the analog channel.
5. **The silent movement yaw channel — found (2026-10-15).** A
   `CSubtickMoveStep` carrying **only a `yaw_delta`** (one per tick, `when = 1`, no
   button, no analog; `has_bits` `{when, pitch, yaw}`) turns the walk by exactly
   that delta — measured 89.9 deg from a 90 deg step at full speed (209.3 units) —
   while the pawn's eye angles stay at 0 **before and after**, i.e. the camera and
   what the server sees are untouched. That is the auto-strafe seam:
   `look(step_yaw=<desired yaw - camera yaw>)` + `move(forward=…, side=…)` per tick.
   The other candidates are dead: writing the command's own `viewangles`
   (`base + 0x40`) steers but moves the eye angles with it, and neither the
   `input_history.view_angles` (silent aim) nor the raw per-sub-tick input history
   (`CCSGOInput + 0xbd0`) affect movement at all. Measured by `strafe-probe`; see the
   section above.

## Open questions (next)

- **Settled by `movement-probe` (2026-10-15).** The engine's own console `+forward`
  writes **`forwardmove = 1.0`** into the command *and* `1.0` into the raw axes
  (`CCSGOInput + 0x270`), with `_has_bits_ = 0x24867` (so `0x40` = `forwardmove`,
  and the other axis bits stay clear: the engine marks only the axes it uses). The
  payload now uses those units (`k_move_scale = 1.0`); `450` was wrong. **Still
  open:** whether writing the axes *at all* can move the player — in a live round
  with the bots kicked, `1.0`/`0.5` on the raw axes and the command floats (with
  and without `quantize`/`step`) still produced no movement, which points at the
  engine's own **input state** (the community `CCSGOInput` struct puts
  `nKeyboardPressed` just above the move floats — `+0x250` with our `−0x28`
  mapping) rather than the command. Next probe: read the engine's input-state
  buttons while holding a console `+forward` (read-only, safe), and if the
  `IN_FORWARD` bit lights up there, write *that*.
- **Does the engine *add* or *subtract* a step's `analog_*_delta`?** The community
  recipe (`delta = base - desired`) implies one sign; a live read of the engine's
  own steps for a known `+forward` decides it before the payload picks one.
- **Is `CCSGOInput + 0x270` overwritten every frame by the engine, or only on
  input change?** `FUN_180b5f7b0` writes it inside slot 5 (from the input entry),
  so the *after*-original write is the one that survives to a post-`CreateMove`
  reader. **`+0xb58` is disproven** — it reads denormals and writing it crashed the
  game (input-path AV, `client.dll + 0xb64a80`), so that entry layout needs a
  fresh derivation.
- **Which command does the movement consume?** Slot 25's `cmd` argument, the one
  slot 5 builds, or the `GetUserCmd` chain result the payload writes today
  (YouGame 336028: only the slot-5 command accepts sub-tick writes properly).
  Compare `last_cmd` (slot 25) with `current_cmd()` in `input()`.
- **Which button state gates movement?** `base->buttons_pb` vs `cmd + 0x60` vs
  `CPlayer_MovementServices::m_nButtons` — the "jump works but forward does not"
  split suggests they are not the same path.
- **Does the shot use `input_history.view_angles` or the `CCSGOInput` source
  entries (`+0xbd0`), and which entry?** Validate with `subtick-shot`.
- **`m_arrForceSubtickMoveWhen`** (`CPlayer_MovementServices`, `float[3]`): the
  network var the engine uses to force sub-tick move timing (the de-subtick
  knob) — the same queue as `QueueForceSubtickMove` (UC 759116).
- Relate `dwViewAngles` (the camera/view global) to `view_angles` for the
  "camera differing from view angles" feature.

## Re-derivation

Every address moves per build. Anchors that are stable and cheap:

- **RTTI → vtable** (README §3): `_TypeDescriptor` name VA → `(S-0x10) - base` →
  `search_bytes` the 4-byte RVA → COL → `search_bytes` the COL VA → vtable.
- **Field offsets**: read the class's vtable (slot 7 = `ByteSizeLong`) and
  decompile it — it walks every present field with its offset; the parser
  (slot 4 / `MergePartialFromCodedStream`) shows the wire tag → field number.
- The subtick classes have no globals of their own, so there is nothing to add to
  `external/dumper/src/analysis.rs` yet; the `CUserCmd`/`CCSGOInput` signatures
  (`docs/re/client-dll.md` §"Input path") locate the command and input.
