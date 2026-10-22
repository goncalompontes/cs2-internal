# Air movement / strafing (`client.dll`)

> **Status (2026-10-15, second pass): the auto-strafe feature is back in the
> payload** — a normal, **silent**, camera-following air-strafe
> (`Input::strafe_tick`, airborne only, holds your own A/D), wired into the overlay
> menu's `Movement` tab (`wire::Settings.auto_strafe`, ADR-0021), the control
> channel (`strafe`) and the `autostrafe` scenario. This document is the RE
> reference: the engine model below, the `AirMove`/`AirAccelerate` re-derivation,
> the `SetupMove` analog sanitizer, the silent sub-tick yaw channel and the
> analog-scale finding (§"Second RE pass", re-checked this session) all stand.
> Also see [`research-strafe.md`](research-strafe.md) (community sources + maths).
>
> Re-derived this session on the warm **25815307** copy (sha `44af2aa0…`): the
> decisive new finding is that `m_flCmdForwardMove` is `CBaseUserCmdPB::forwardmove`
> **unscaled** (`UpdateInputState`, `FUN_180a764d0`), so the engine's own unit
> analog (`1.0`) is what reaches `AirMove`; the movement then treats that as the
> full-speed request (`move(forward=1.0)` = 204 u/s, 1.01× the console `+forward`),
> so a unit analog gets the full `A = sv_airaccelerate · m_flMaxSpeed · dt` budget
> and `theta* = 90°`. The silent-yaw gate is the convar
> **`sv_subtick_movement_view_angles`** ("Whether or not subtick view angles are
> taken into account during movement"), read by `DoMovement::Subtick`'s
> `(*vtable+0x150)()` predicate.
>
> **Measured live (2026-10-16, de_dust2 bot match, listen server, 250 u/s ground
> speed, `sv_quantize_movement_input 1` — the Valve default):** the per-slice
> simulated basis rotation gains **+85.1 median / +104.9 max u/s per hop** at the
> default `strafe(subtick=8)`, and scales monotonically with the slice count —
> `subtick=1` +15.4, `4` +41.9, `8` +85.1 (− all vs an ≈ 0 control) — which is the
> signature of `AirAccelerate` really running once per slice (§2.5). An **even**
> split of the same rotation (an earlier revision) ramps the basis and only the last
> slice lands on `theta*`, so it gained ~5× less (+17.4); the fix is to *simulate the
> slices* and set each step's `yaw_delta` to the per-slice `theta*` (below). The
> camera-frame **analog** encoding (`subtick=0`) is nearly flat because the analog
> snaps to the `{-1,0,1}` valid set, which quantises the perpendicular push off its
> optimum. Requires `sv_subtick_movement_view_angles 1` (the `strafe-probe` scenario
> sets it; set it on the listen server for a manual test).

The engine side of the air-strafe: how a command's move axes become a `wishdir`,
how `AirAccelerate` turns that into the per-tick gain, and which write actually
steers the movement yaw **silently**. Re-derived on build **25815307**
(sha `44af2aa0…`) from the Ghidra project (`C:\Users\gpmpo\ghidra-analysis\client.dll`),
and **re-checked 2026-10-15** when the live game moved to **25947352**
(sha `659940c1…`) — the curated signatures re-resolve across both, so the payload
runs on the new build while the RVAs below are the older copy's (see the second RE
pass at the end of this doc). Cross-checked against the community threads (see
[`research-strafe.md`](research-strafe.md)). Addresses are RVAs (image base
`0x180000000`); the live build moves them, so the payload uses no hard-coded RVA.

Companion docs: [`subtick.md`](subtick.md) (the sub-tick structures and the
measured write channels), [`prediction.md`](prediction.md) (the jump chain).

## The functions

| Function | RVA | What it is |
|---|---|---|
| `AirMove` | `0x8e48f0` | vprof `PreSource1AirMove`. Builds `wishdir` from the move axes, calls `AirAccelerate`, does the Source-2 velocity split, and calls into the modern-jump bookkeeping. |
| `AirAccelerate` | `0x8beb70` | the gain rule. Caller: only `AirMove`. |
| angle basis | `0x173ccd0` | `FUN_18173ccd0(movedata + 0x14, &forward, &right, scratch)` — `AngleVectors` on the **CMoveData's** angles. |
| dot / min helpers | `0x15f0d0` / `0x159ca0` | vector dot; scalar min. |

## The convar defaults (read from the registration functions)

| Convar | ConVar object | Default | Evidence |
|---|---|---|---|
| `sv_air_max_wishspeed` | `DAT_182529140` | **30.0** (`0x41f00000`) | registrar `FUN_1800b1950` |
| `sv_airaccelerate` | `DAT_18255bb40` (`+0x8` = the mirrored value) | **12.0** (`0x41400000`) | registrar `FUN_1800d84f0` |
| `sv_quantize_movement_input` | `DAT_1825291e0` | **1 (on)** | registrar `FUN_1800b2590`: *"Quantize movement input values. Enabling this restricts players from using analog input to move at fractional speeds normally impossible with digital button input."* |

That last one is the decisive constraint: **with `sv_quantize_movement_input` on
(the default, and what a local listen server runs), `forwardmove`/`leftmove` are
rounded — fine (fractional) wish angles are impossible through the analog move
alone.** Steering has to come from rotating the *movement yaw*.

## `AirMove` — how the wish is built

`FUN_1808e48f0(svc, movedata)`:

1. `FUN_18173ccd0(movedata + 0x14, &fwd, &right, scratch)` — the basis comes from
   the **CMoveData's** angles at `+0x14` (`pitch`), `+0x18` (`yaw`), `+0x1c`
   (`roll`), not from the command's `viewangles` directly. That indirection is
   what makes the sub-tick yaw channel possible (below).
2. `wish = fwd * *(float *)(movedata + 0x2c)          // forwardmove
          + right * *(float *)(movedata + 0x30)`      // sidemove/leftmove
3. `wishspeed = min(|wish|, *(float *)(movedata + 0x120))` (the CMoveData's max
   speed bound), normalise `wish` → `wishdir`.
4. `AirAccelerate(svc, movedata, &wishdir, wishspeed, sv_airaccelerate)`.
5. transfer half of `movedata[0x108..0x110]` between `movedata[0x38..0x40]` and
   `movedata[0x114..0x11c]`, then add the pawn's contribution — the Source-2
   split-velocity bookkeeping (net effect: the classic rule).
6. `if (sv_legacy_jump == 0) FUN_1808e0b30(svc + 0x6c8, movedata)` — the modern
   jump bookkeeping runs from `AirMove` too.
7. jump-apex handling (`svc + 0x70c` = `m_bJumpApexPending`, `svc + 0x710` = a
   per-tick sub-tick accumulator clamped to 1.0).

## `AirAccelerate` — the gain rule

`FUN_1808beb70(svc, movedata, wishdir, wishspeed, accel)`:

```c
cap       = ConVar_get_float(&sv_air_max_wishspeed);      // 30.0
wishspd   = min(wishspeed, cap);
addspeed  = wishspd - dot(movedata->velocity /* +0x38 */, wishdir);
if (addspeed <= 0) return;                                // no gain
accelspeed = accel * wishspeed * svc[0x26c] * frametime;   // svc+0x26c = 1.0 (0.25 only on a
                                                           // ladder/noclip-ish branch)
applied    = min(addspeed, accelspeed);
velocity  += wishdir * applied;                            // (+ the split fields)
```

So per tick the **speed magnitude** changes by
`sqrt(|v|² + applied² + 2·|v|·applied·cos θ) - |v|`, where `θ` is the angle
between the velocity and the wish. With `u = |v|·cos θ` the gain factors as
`|v|² + cap² - u²` (for the uncapped case), so it is **maximised at `u = 0`**, i.e.
the wish perpendicular to the velocity — which is exactly why air-strafing gains
and why holding `W` (wish along the velocity → `u = |v|` → `addspeed < 0`) does
not. The practical optimum balances the rotation (you still have to steer) against
`applied` saturating at `addspeed`.

## The silent movement-yaw channel — `CSubtickMoveStep.yaw_delta`

This is the part the strafer uses, and it is confirmed end to end:

1. `FUN_180a73c70` (the `subtick_moves` → `CMoveData` copy) takes its **base**
   pitch/yaw from the command's own `viewangles`
   (`*(float *)(*(base + 0x40) + 0x18)` / `+0x1c`, i.e. `CBaseUserCmdPB::viewangles`
   at `base + 0x40`) and **accumulates** each step's `pitch_delta` / `yaw_delta`
   on top of it, storing the running absolute value into the CMoveData's step
   array entry.
2. `DoMovement::Subtick` (`FUN_180a64c20`), per distinct slice fraction, assigns
   that entry back onto the CMoveData's angles:
   `movedata[0x14] = entry.pitch`, `movedata[0x18] = entry.yaw`
   (behind `(*svc->vtable[42])()`, the "use sub-tick angles" predicate).
3. `AirMove` then builds `wishdir` from *those* angles.

So: **`movedata yaw = command viewangles.yaw + Σ(CSubtickMoveStep.yaw_delta)`**,
per slice — the command's `viewangles` are never touched, so the pawn's eye angles
(and the camera) do not move. Measured in this repo (`docs/re/subtick.md`,
`strafe-probe`): one `yaw_delta = 90°` step turns the walk **89.9°** with the eye
angles unchanged before *and* after.

### What that means for a strafer

- Write the wish as a **digital** move (`forwardmove = 1`, `leftmove = 0`) — which
  survives `sv_quantize_movement_input` byte-identically — and put the *angle* in
  the sub-tick `yaw_delta`: with `δ = target_wish_yaw − view_yaw` the resulting
  `wishdir` is exactly the target direction.
- Splitting `δ` across several steps (`δ/N` at `when = i/N`) makes the engine
  assign a *smoothly rotating* movement yaw per slice — that is the "sub-tick"
  strafer, and it is what a human's mouse rotation looks like to the engine.
- The alternative — encoding the direction in the **analog** axes
  (`forwardmove`/`leftmove`, the reference's `rotate_movement`) — works only when
  `sv_quantize_movement_input` is off; with it on, the rounded pair can only
  express 8 view-relative directions and the fine optimal angle is lost.

## Constraints inherited from the sub-tick array

The steps share one `RepeatedPtrField` with the engine's own steps and with the
bunnyhop's release+press pair, and the admission gate discards the **whole** array
if `count > 32`, `when` decreases or exceeds 1.0, `popcount(button) != 1`, or the
view-delta accumulator exceeds 2.0 (`docs/re/prediction.md` §"RE findings",
`docs/re/research-bhop.md` §3.3). A strafer that appends `N` yaw steps per tick
therefore has to

- keep `when` monotonic (insert in order, as the bhop pair does),
- stay inside the 32-entry budget together with the bhop's pair and the engine's
  own steps,
- watch `view_delta_accum > 2.0`: per-sub-tick view-angle change is capped, so a
  large `δ` split over few steps is what trips it — split it, do not dump it.

### The two perpendicular sides

For **speed** alone the two perpendicular pushes (`vel_yaw ± theta`) are symmetric —
the choice only matters for *steering* (which way the heading turns). The payload
picks the side toward the goal and pushes the **full** `theta*` even when the goal is
inside the cone, so the heading oscillates about the goal rather than decaying.

The **rotation cap** only applies to the optional sub-tick `yaw_delta` path
(`strafe(subtick=N)`), because the admission gate discards the whole array — the
bunnyhop's pair included — on a too-large view delta. The **analog** direction has no
such limit (it is a direction in the camera frame, not a rotation), so with the
default `subtick=0` `strafe_clamped` stays 0 by construction. (An earlier revision
mirrored the side instead of clamping on the yaw path; that went away with the switch
to the analog as the primary write.)

#### ⚠️ Measured: which seam actually moves the velocity (2026-10-15)

| Write | Silent? | Moves the velocity? |
|---|---|---|
| **analog move** (`forwardmove`/`leftmove` + the raw axes `CCSGOInput+0x270/0x274`) | yes (the camera is untouched) | the **direction** is honoured — it *does* take over the air movement — but the **speed gain is ~0** in the bot-match A/B (median +7 u/s/hop with negative values from wall impacts; not a clean measurement) |
| `CSubtickMoveStep.yaw_delta` basis rotation (8 steps) | yes | **no** — it lands on the CMoveData angles (silent) but did not move the velocity |

**Operator-reported symptom with the analog path enabled: "I can't move."** That is
the auto-strafe doing what it says (it owns the wish while airborne, pushing
perpendicular to the velocity, so the player's own keys no longer steer) combined
with the missing gain — i.e. a *pure* loss. **The feature is therefore off by
default and should stay off until the magnitude question below is settled.**

The suspected missing piece is the **magnitude** of the CMoveData's move axes.
`AirMove` takes `wishspeed = min(|wish|, movedata + 0x120)`, and `SetupMove` fills
`movedata + 0x2c / + 0x30` (forwardmove / sidemove) from `svc + 0x1a0 / + 0x1a4`
through `FUN_180a76da0`, which **zeroes any value whose |x| ≥ 10** — i.e. those fields
appear to be ±1, not weapon-speed units. If `wishspeed` is 1, then
`accelspeed = sv_airaccelerate(12) · 1 · dt ≈ 0.19` per tick, which is a
*negligible* gain and matches the observation; the direction is still honoured,
which matches "it steers but does not accelerate". The next RE step is to find the
**writer of `svc + 0x1a0/0x1a4/0x1a8`** and establish the real unit the movement
expects (the `move(forward=1.0) → 204 u/s` ground measurement says *some* path gives
full speed from a 1.0 write, so the air path and the ground path may not read the
same field).

## The gate reason (`strafe_reason`)

`strafe_reason` reports why the last tick did or did not act — the fastest way to
tell a bad gate from a bad seam:

| Value | Meaning |
|---|---|
| 0 | feature off (or no command) |
| 1 | on the ground (the ground path is `WalkMove` + friction) |
| 2 | no velocity, or below the 30 u/s minimum |
| 3 | the command's `viewangles` could not be read |
| 4 | acted (the wish was written) |
| 5 | no room in the sub-tick array (the ≤32 budget, with the bhop's pair in it) |
| 6 | a non-positive `scale` (nothing to write) |
| 7 | the player is holding `A`/`D` — their input wins |

## Report vs implementation — the gap analysis (2026-10-15)

Checked line by line against [`research-strafe.md`](research-strafe.md).

| Report requirement | Our implementation | Verdict |
|---|---|---|
| `theta* = acos((W−A)/s)`, **and 90° when `A ≥ W`** (§2.3/§2.4, landmine 2) | `theta = 90` when `accel_slice >= 30`, else `acos((30−accel_slice)/s)` | ✅ fixed this session (it used to keep `acos` of a negative number and push *obtusely*) |
| magnitude on the **base** `forwardmove`/`leftmove`, never on a late step (§3.3 gotcha, landmine 9) | the wish goes on the base floats + the raw axes; the steps (if any) carry only the rotation | ✅ |
| direction may ride the silent `yaw_delta` basis channel; **fall back to the analog** if the vtable-`+0x150` predicate is off (§3.3, landmine 11) | yaw rotation behind `strafe(subtick=N)` (default 0); the analog is the primary path | ✅ in shape, ❌ in effect: the rotation ran but **did not move the velocity** — consistent with that predicate being off on our listen server. Not yet asserted/toggled. |
| write the analog to the raw axes **before *and* after slot 5** (§3.4.2) | only *after* (inside `apply()`), and then `write_raw_move()` ran after that and could clobber it | ❌ **defect — fixed this session**: `Input::write_strafe_raw()` is now re-asserted after `write_raw_move()` in both phases of both detours, and the wish is cached for the pre-original phase (the only way it reaches the command `create_move_input` builds) |
| **slicing gives N× the 30-cap headroom** (§2.5) — the point of the community's `when = i/N` | `subtick` knob exists | ⚠️ not exploitable as written: with equal deltas the accumulation **ramps** the rotation across the slices, so most slices get a near-parallel wish (≈ no gain) and only the last is perpendicular — i.e. ≈ `1/N` of the gain, not `N×`. The N× behaviour needs the *full* rotation from the first slice (first step carries Δ, later steps 0) — but landmine 8's duplicate-step **collapse** then removes them. Not resolved. |
| `when` on the 1/64 grid (`i/8`, `i/16`) (landmine 8) | `i/steps` with steps ∈ {8, 16} when enabled | ✅ |
| monotonic array, ≤ 32, never two button bits in a step (landmine 7) | shared insertion sort with the bhop pair, budget-checked, one button bit | ✅ |
| don't act on the ground (§6.4) | `on_ground()` gate | ✅ |
| also guard non-`MOVETYPE_WALK` / dead (landmine 18) | not implemented | ⚠️ gap |
| `friction` multiplier on the air accel is not 1 on all surfaces (§6.5) | ignored | ⚠️ gap (minor, offline) |
| prove silence from `m_angEyeAngles` (landmine 12) | measured: `eye_drift` 0.0–0.9° | ✅ |
| unit analog (±1), not ±450 (landmine 13) | unit vector `(cos, −sin)` | ✅ |
| `sv_quantize_movement_input` (default 1) rounds the analog server-side (§4/§6.17) | at the optimum the wish is `(cos 84°, sin 84°) → (0, 1)` i.e. exactly perpendicular, so the rounding is a no-op *at that angle* | ✅ by construction (a *wrong* angle would be rounded and lost) |

### The two biggest suspects

1. **Ordering (fixed):** our raw-axes write happened only after `create_move_input`, so
   the command the movement simulates carried the *player's* axes, not the strafe's.
   The measured symptom (the wish "acts", `strafe_writes` climbs, no speed change) is
   exactly what that predicts.
2. **The `yaw_delta` gate (open):** the rotation is applied to the CMoveData angles
   (silent) yet did not steer the velocity, which is what landmine 11 predicts when
   the `(*vtable+0x150)()` policy is off. On a listen server that policy is the
   `sv_subtick_movement_view_angles` convar — **the decisive one-command experiment is
   to set it to `1` on our server and re-run the yaw-path A/B.** Until then the
   analog path is the right primary.

### Measurement method (a correction to this session's own attempts)

Sampling the *snapshot's* velocity as fast as the control pipe allows gives ~700 Hz
samples against a 64 Hz tick, so most consecutive deltas are 0 and the statistic is
meaningless; and the payload's own `strafe_speed` is only written while the strafe
acts, so it cannot serve as its own baseline. The gain A/B needs the
`movement-probe` pattern (pinned spot, `pick_clear_heading`, settled velocity) or a
tick-aligned delta from `input_on_ground` transitions — not ad-hoc sampling.

## What the payload implements

| Piece | Where |
|---|---|
| the gates, the goal heading, `theta*`, the side | `Input::strafe_tick` (`payload/control/src/input.cpp`) |
| the **per-slice simulation** that sets each step's `yaw_delta` (below) | `Input::strafe_tick` |
| the analog/buttons/raw write and the sub-tick basis rotation | `Input::write_strafe_wish` |
| idempotency for the rotation (the hook runs more than once per command) | `Input::strafe_steps_present` |
| keeping the shared step array monotonic | `Input::sort_last_step` |
| state / diagnostics | `input()` status (`service.cpp`), `Input::strafe_*` |

**The sub-tick rotation is simulated, not split evenly.** The engine **accumulates**
each step's `yaw_delta` onto the basis, so splitting the whole tick's rotation evenly
ramps the wish across the slices and only the *last* slice lands on `theta*` (an
offline replay of `AirAccelerate` gives `Δv² ≈ 34`/tick at 8 slices, i.e. ~1/75 of the
potential). Instead `strafe_tick` simulates the tick slice by slice (the community's
`ref_air_accel_sim` recipe): for each slice it computes `theta_k*` from the
**simulated** velocity, sets the step's `yaw_delta` so the accumulated basis is that
wish yaw, then steps the simulated velocity the way `AirAccelerate` will
(`applied = min(A_slice, addspeed)`). That puts the bulk of the rotation on the first
step, small deltas after, and every slice gains — `Δv² ≈ 2538`/tick at 8 slices
(`2675` at 16), ~2.8× a single full-tick call. Measured live: **+85.1 median /
+104.9 max u/s per hop** at `subtick=8`, scaling with the slice count (`1` +15.4,
`4` +41.9, `8` +85.1), versus an even split's +17.4 (only the last slice optimal).

Diagnostics: `strafe` (enabled), `strafe_airborne`, `strafe_speed` (the payload's
own horizontal speed read), `strafe_goal` (the goal heading), `strafe_wish` (the
chosen wish yaw), `strafe_delta` (the camera-relative wish offset == the basis
rotation asked for), `strafe_rot` (the rotation actually written, after the gate
clamp), `strafe_side` (+1 CCW / −1 CW of the velocity), `strafe_steps` (yaw steps
queued), `strafe_reason` (why the tick did or did not act), `strafe_clamped`,
`strafe_wishspeed`, `strafe_subticks`, `strafe_writes`, `strafe_raw_writes`.

`strafe_reason`:

| Value | Meaning |
|---|---|
| 0 | feature off (or no command) |
| 1 | on the ground (the ground path is `WalkMove` + friction) |
| 2 | no velocity, or below the ~25 u/s minimum |
| 3 | the command's `viewangles` could not be read |
| 4 | acted (the wish was written) |
| 5 | no room in the sub-tick array (the ≤32 budget, with the bhop's pair in it) |
| 6 | nothing to write (a zero wish) |
| 7 | the player is holding `A`/`D` — their input wins |

Not implemented: a constant-yaw-rate circle strafer (HLStrafe's `ConstantYawSpeed`
policy — a human-looking variant), and a `m_nActualMoveType == MOVETYPE_WALK`/alive
guard (the airborne test covers the normal case).

## Open items

- `svc + 0x26c` (the `accel` multiplier `AirAccelerate` folds in) is `1.0`
  normally and `0.25` on one move-type branch of `FUN_1808c1950`; the branch was
  not chased further (it looks like ladder/noclip), so the strafer assumes `1.0`.
- The exact `· 0.5` split inside `AirAccelerate` (half of `accelspeed` applied to
  one velocity field, the rest to the other) is Source-2 bookkeeping; the net
  per-tick gain is the classic `min(addspeed, accelspeed)`.
- `when = 1` vs a split: measured silent either way; the split is the smoother
  option and the one the payload uses.

## Second RE pass (2026-10-15) — the magnitude, and circle vs. seek

The live game updated to build **25947352** (`client.dll` sha `659940c1…`) while the
warm Ghidra copy is still 25815307 (`44af2aa0…`); the curated signatures re-resolve
(`payload/sdk/offsets.hpp`), so the payload runs on the new build, but every RVA in
this doc is the 25815307 copy's. The payload writes **input only** — it consumes no
air-movement RVA — so the addresses are documentation, not a runtime dependency.

### Where the analog magnitude comes from — the sanitizer

`CPlayer_MovementServices::SetupMove` (`0xa739d0`, vprof `SetupMove`,
`player_movementservices.cpp`) is the **only** writer of the `CMoveData` move axes
(`+0x2c`/`+0x30`/`+0x34`). It fills them from the movement services' command fields —

| schema field | offset | role |
|---|---|---|
| `m_flCmdForwardMove` | `+0x1a0` | forward axis |
| `m_flCmdSideMove` | `+0x1a4` | side axis |
| `m_flCmdUpMove` | `+0x1a8` | up axis |

— and **only for the axes whose button bit is held** (`CInButtonState` word 0 at
`svc+0x58`; `IN_FORWARD|IN_BACK` = `0x18`, `IN_MOVELEFT|IN_MOVERIGHT` = `0x600`).
Each value passes through `FUN_180a76da0`, which returns it only when it is finite
and `|x| < 10`, and otherwise `0`:

```c
float sanitize(float x) { return (isfinite(x) && fabsf(x) < 10.0f) ? x : 0.0f; }
```

So the analog is a **unit value (`±1`)**, not `cl_forwardspeed` (`450`) units, and a
large write is dropped silently. That is the mechanism behind the earlier
`docs/re/subtick.md` observation that `move(forward = 1.0)` walks full speed while a
`450` write moves nothing. `Input::strafe_write_wish` therefore writes a **unit**
vector, with `strafe(scale=n)` clamped below the sanitizer (`k_strafe_max_scale`).

### `AirMove` / `AirAccelerate` re-read

`AirMove` builds `wish = fwd · fmove + right · smove` from the CMoveData's angles
(`AngleVectors` at `+0x14`), with `wishspeed = min(|wish|, CMoveData+0x120)`.
`AirAccelerate` then computes `addspeed = min(wishspeed, 30) − dot(v, ŵ)` and
`accelspeed = sv_airaccelerate · wishspeed · friction · frametime`, applying **half**
to `m_vecVelocity` (`+0x38`) and the other half to the second ledger (`+0x114`) — the
`* 0.5` is real (`0x181ac0070` = `0.5f`) and the two sum to the classic
`min(accelspeed, addspeed)`.

**Consequence for the expected gain — and an open contradiction.** With a unit
analog `|wish| ≈ 1`, so the air accel budget is `12 · 1 · (1/64) ≈ 0.19` per tick
(≈ `0.09` on `+0x38`) — a few u/s per hop. That matches the earlier live A/B's median
**+7 u/s/hop** and the modest turn it implies.

But it *contradicts* the harness's own ground measurement (`move(forward = 1.0)` walks
204 u/s, 1.01× the console `+forward`): with `wishspeed = |wish| ≈ 1` the ground
`Accelerate` (`0x1808bdfd0` → `0x1808be0a0`, `addspeed = wishspeed − dot`) could not
sustain more than ~1 u/s. One of the two readings is wrong, and the difference is
decisive, so the strafe exposes **`strafe(scale = n)`** — the analog magnitude, clamped
below the sanitizer (`k_strafe_max_scale = 9`) — as the experiment:

* if `scale = 1` gains speed, the engine scales the unit analog itself (`wishspeed`
  becomes the weapon speed) and `scale` is a no-op;
* if `scale = 1` gains ~nothing and `scale = 9` gains, then `wishspeed = |wish|` and the
  analog magnitude is the missing ingredient (this is what the earlier session's
  "+66 u/s" run would have needed).

`docs/re/subtick.md`'s per-slice step channel is the third candidate (the movement is
re-run per sub-tick, so the analog a slice consumes comes from the step deltas). The
per-run telemetry (`strafe_gain`) makes the live test decisive.

### Follow the camera, and let the player's input win

The strafe implemented here is the ordinary **auto-strafe**: while airborne it presses
`A`/`D` for you, choosing the side that turns the velocity toward **where you are
looking** (camera CCW of the velocity ⇒ `A`, else `D`), and keeping the wish
perpendicular to the velocity so it re-perpendicularises every tick and the velocity
follows the view while it gains. It is **your input that wins**: if you already hold
`A` or `D` the strafe writes nothing (`strafe_reason = 7`), so manual air control is
never hijacked. (An earlier "circle strafer" variant — a *fixed* rotation side that
orbits regardless of the camera — was dropped in favour of this.)

The payload writes the wish as an **analog vector in the camera frame** plus the
`A`/`D` button bit; the analog is what makes the write silent — it changes the world
wish direction without touching any angle — so the sub-tick `yaw_delta` rotation
stays optional (`strafe(subtick=N)`, default 0).

### Still open

- The client build changed under the doc; re-derive the RVAs on `659940c1…` before
  relying on any address here.
- The `|x| < 10` sanitizer's *purpose* (a guard against the community `±450` paste,
  or a NaN/denormal guard) is inferred from the constant; the range it admits (`±1`
  used by the engine) is what matters and is observed.
- Whether `sv_subtick_movement_view_angles` gates the per-slice angle write is still
  not asserted (the analog path does not need it).
