# Air movement / strafing (`client.dll`)

The engine side of the air-strafe: how a command's move axes become a `wishdir`,
how `AirAccelerate` turns that into the per-tick gain, and which write actually
steers the movement yaw **silently**. Re-derived on build **25815307**
(sha `44af2aa0…`) from the Ghidra project (`C:\Users\gpmpo\ghidra-analysis\client.dll`);
cross-checked against the community threads (see
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

## Open items

- `svc + 0x26c` (the `accel` multiplier `AirAccelerate` folds in) is `1.0`
  normally and `0.25` on one move-type branch of `FUN_1808c1950`; the branch was
  not chased further (it looks like ladder/noclip), so the strafer assumes `1.0`.
- The exact `· 0.5` split inside `AirAccelerate` (half of `accelspeed` applied to
  one velocity field, the rest to the other) is Source-2 bookkeeping; the net
  per-tick gain is the classic `min(addspeed, accelspeed)`.
- `when = 1` vs a split: measured silent either way; the split is the smoother
  option and the one the payload uses.
