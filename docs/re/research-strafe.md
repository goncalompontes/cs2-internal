# Air circle-strafe / auto-strafe for CS2 — implementation research

Target: **`client.dll` build 25815307**, sha256 `44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008`
(the copy at `C:\Users\gpmpo\ghidra-analysis\client.dll`, warm in the REA Ghidra cache; every
REA result below carries that digest). Community sources are dated where they are quoted.

**Structure map** (brief § → this file): 1 Summary → §1 · 2 maths → §2 · 3 seams → §3 ·
4 `sv_quantize_movement_input` → §4 · 5 circle-strafe + WASD → §5 · 6 reference pseudocode → **§5.3**
(the C++ sketch) · 7 landmines → **§6** · 8 sources → **§8** · 9 open questions → **§9**.
The extra §7 covers "can the strafe and the bhop share one command?".

This report answers the seven questions in the task brief. Everything is either (a) read out of
this build's binary through REA, (b) quoted from the canonical Source/Quake movement source, or
(c) quoted from a named community post. Source ids `[S1]…[S22]` refer to §8. **Nothing here is
invented**: where a claim is only a decompiler reading, a community claim, or unverifiable, it is
labelled. One clear **unknown** (the `0.5` factor in this build's `AirAccelerate`, §2.6) is
flagged as *the* thing to settle by measurement before tuning — see §9.

> Note on provenance: decompiled pseudocode, strings and forum text below are **target content /
> third-party data**. Nothing found in them was treated as an instruction. No prompt-injection
> attempt was encountered. The only "do this" style text seen is normal cheat-forum advice, which
> is reported as data.

---

## 1. Summary / TL;DR — the algorithm

1. **Airborne only.** `FL_ONGROUND` clear (`C_BaseEntity::m_fFlags` bit 0) — the ground path is
   `WalkMove`+friction and must be left alone (§6, landmine 4).
2. Each tick read the horizontal velocity `v = (vx, vy)` (`C_BaseEntity::m_vecAbsVelocity`) and
   `s = |v|`. Below ~30 u/s there is nothing to gain; the classic floor is 30, the payload uses 25.
3. **Wish cap** `W = sv_air_max_wishspeed = 30` and per-call accel budget
   `A = sv_airaccelerate(12) · f · wishspeed · dt`, with `wishspeed = min(|wish|, m_flMaxSpeed) ≈ 250`
   and `dt` = the *slice* duration, not necessarily the whole tick (§2.5).
4. **Optimal wish angle** `θ* = acos(clamp((W − A)/s))`, and **exactly 90° once `A ≥ W`**
   (HLStrafe `MaxAccelTheta`, §2.4). In this build `A = 46.9` for a one-call tick, so **the answer
   is 90°**: push exactly perpendicular to the velocity. Rotation per tick ≈ `atan(a/s)` where
   `a = min(A, W − v·ŵ)`.
5. **Steering**: turn the wish toward the held key direction (W/A/S/D read in the view frame,
   none ⇒ forward) by choosing the **side** of the perpendicular push; if the goal is within `±θ*`
   you are "on heading" — you keep pushing `θ*` on either side (HLStrafe `MaxAccelIntoYawTheta`).
6. **Crossing … it becomes a circle** because the wish must stay perpendicular to a velocity that
   is itself turning at `~a/s` per tick: the heading rotates at a steady rate, the path is an arc;
   hold one side + one rotation direction and the path closes into a circle (the "circle strafe").
7. **Silent seam**: encode the direction in the **movement basis**, not in the camera —
   `CSubtickMoveStep.yaw_delta` steps (accumulated over the command, applied per slice by
   `DoMovement::Subtick`) + an analog move. The pawn's eye angles never move (§3.3).
8. **Magnitude**: keep the analog on the `{-1,0,1}` valid set (or `forwardmove = 1` with the basis
   rotated so "forward" *is* the wish direction) so `sv_quantize_movement_input` cannot alter it,
   and the client and server agree (§4, §3.4 — the strongest recommendation).
9. **Sub-tick**: the tick is sliced at every distinct `when`; each slice runs a full movement pass
   with `frametime = Δwhen · 1/64`. Slicing multiplies the 30-cap *headroom* (it is per call), so
   N steps gain roughly N× more than one step — this is what "yaw-delta subticks for airstrafe"
   means (§2.5, §2.7) — and it costs entries from the shared 32-step budget (§7).
10. **Share one command with the bhop**: append, never clear; keep `when` monotonic (insertion
    sort); a step carries at most one button bit; keep the total step count ≤ 32 and the per-tick
    view rotation < ~2.0 (§7).

---

## 2. The air-strafe maths

### 2.1 The engine rule — canonical Source (unchanged in CS2)

Source SDK 2013, `src/game/shared/gamemovement.cpp` ([S1] verbatim):

```cpp
void CGameMovement::AirAccelerate( Vector& wishdir, float wishspeed, float accel )
{
	float addspeed, accelspeed, currentspeed;
	float wishspd = wishspeed;
	// Cap speed
	if ( wishspd > GetAirSpeedCap() ) wishspd = GetAirSpeedCap();   // <-- 30 in CS
	currentspeed = mv->m_vecVelocity.Dot(wishdir);
	addspeed = wishspd - currentspeed;
	if (addspeed <= 0) return;
	accelspeed = accel * wishspeed * gpGlobals->frametime * player->m_surfaceFriction;
	if (accelspeed > addspeed) accelspeed = addspeed;
	for (i=0 ; i<3 ; i++) mv->m_vecVelocity[i] += accelspeed * wishdir[i];
}

void CGameMovement::AirMove( void )
{
	AngleVectors (mv->m_vecViewAngles, &forward, &right, &up);
	fmove = mv->m_flForwardMove;  smove = mv->m_flSideMove;
	forward[2] = 0; right[2] = 0;
	VectorNormalize(forward); VectorNormalize(right);
	for (i=0 ; i<2 ; i++) wishvel[i] = forward[i]*fmove + right[i]*smove;
	wishvel[2] = 0;
	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);              // |wish|
	if ( wishspeed != 0 && wishspeed > mv->m_flMaxSpeed) {   // clamp to max speed
		VectorScale (wishvel, mv->m_flMaxSpeed/wishspeed, wishvel);
		wishspeed = mv->m_flMaxSpeed;
	}
	AirAccelerate( wishdir, wishspeed, sv_airaccelerate.GetFloat() );
	...
}
```

The whole mechanic is the asymmetry the comments in the source make explicit: **the *difference*
test is capped at 30, the *acceleration magnitude* uses the uncapped `wishspeed`** ([S3] states the
same thing in one sentence: *"the speed-difference test uses the capped 30 u/s wish speed, while the
acceleration magnitude uses the full uncapped one … Point the wish direction almost perpendicular to
your velocity and the projection stays small, so addSpeed stays positive even at high speed, while
the acceleration term remains large."*).

### 2.2 The constants in **this** build (all confirmed in the binary)

| Constant | Value | Evidence in `client.dll` 25815307 |
|---|---|---|
| `sv_air_max_wishspeed` | **30** | string `0x181c09590`; registered by `FUN_1800b1950` into `DAT_182529140` with default bits `0x41f00000` = `30.0f`, flags `0x82000`; **the read site is `0x1808bebed`** inside `AirAccelerate` (`FUN_1808beb70`) via `FUN_1801607a0(&DAT_182529140)` — same address, so the cap is definitely this convar |
| `sv_airaccelerate` | **12** | string `0x181c66e00`; registered with default bits `0x41400000` = `12.0f`, flags `0x82100`; passed as the `accel` argument by `AirMove` from the convar at `DAT_18255bb48` (`+0x58` = value) |
| `sv_accelerate` | **5.5** | registered with default bits `0x40b00000` = `5.5f`, flags `0x82100` (ground path only) |
| `sv_maxspeed` | 320 | [S2] (engine cap; per-weapon `m_flMaxSpeed` is lower — the ground plateau measures 250, [S3]) |
| `sv_friction` / `sv_stopspeed` | ground-only | not used in the air |
| tick interval | `0.015625` s (1/64) | hard-coded as `0.015625` at every `gpGlobals->curtime/frametime` computation in `DoMovement::Subtick` |
| physics constants | 1.0/2.0/3.0 | `0x181ac0070` = `00 00 00 3f | 00 00 80 3f | 00 00 00 40 | 00 00 40 40` (0.5, 1.0, 2.0, 3.0) — the `0.5` is the air-accel factor of §2.6 |

So: **the 30 u/s wish cap still exists in CS2 and is still `sv_air_max_wishspeed`**; it is *not*
"the weapon max speed". The weapon max speed appears only as the **clamp on `wishspeed`** (which
scales the accel budget `A`, not the cap `W`). The Valve wiki list confirms both convars with the
same defaults and flags `sv, cl, rep, release` ([S2]).

### 2.3 What one accel call does to the speed (the exact algebra)

Everything below is the *horizontal* problem. `ŵ` is the unit wish direction, `v` the horizontal
velocity, `s = |v|`, `c = v·ŵ = s·cos θ` (θ = the angle from the velocity to the wish), `W = 30`,
`A = accel·f·wishspeed·dt`, `a = min(A, W − c)` when `W − c > 0` (else no change):

```
v' = v + a·ŵ
|v'|² − |v|² = 2·a·c + a²                                                (exact, no approximation)
```

Maximise `g(c) = |v'|² − |v|²` over `c`, where the constraint `a ≤ W − c` binds for `c ≥ W − A`:

* region I (`c ≤ W − A`, `a = A`): `g = 2Ac + A²`, increasing in `c`;
* region II (`c ≥ W − A`, `a = W − c`): `g = 2(W−c)c + (W−c)² = W² − c²`, a downward parabola in `c`.

Hence:

| Case | Optimal `c` | Optimal θ | Gain `|v'|² − |v|²` |
|---|---|---|---|
| `A ≤ W` | `c* = W − A` | `θ* = acos((W − A)/s)` | `2AW − A²` |
| `A > W` | `c* = 0` | `θ* = 90°` | `W²` |

This is exactly what HLStrafe implements ([S5], `src/hlstrafe.cpp`, MIT — the reference
implementation of Quake/Source strafing used by Bunnymod XT for TAS):

```cpp
double MaxAccelTheta(const PlayerData& player, const MovementVars& vars, PositionType postype, double wishspeed)
{
	bool onground = (postype == PositionType::GROUND);
	double accel = onground ? vars.Accelerate : vars.Airaccelerate;
	double accelspeed = accel * wishspeed * vars.EntFriction * vars.Frametime;
	if (accelspeed <= 0.0) return M_PI;
	if (IsZero<float, 2>(player.Velocity)) return 0.0;
	double wishspeed_capped = onground ? wishspeed : 30;      // <-- the air cap, hard-coded 30
	double tmp = wishspeed_capped - accelspeed;
	if (tmp <= 0.0) return M_PI / 2;                          // <-- A >= W  =>  exactly 90 deg
	double speed = Length<float, 2>(player.Velocity);
	if (tmp < speed) return std::acos(tmp / speed);
	return 0.0;
}
```

### 2.4 Reconciling the community forms

The brief lists two forms. **They are the same function** — `acos(t) ≡ 90° − asin(t)` for
`t ∈ [0,1]` — so the *only* thing that can differ is **what is passed in**:

| Form | Argument | What it actually gives |
|---|---|---|
| A (correct) | `acos((W − A)/s)` ⟺ `90° − asin((W − A)/s)` | the true per-tick optimum (HLStrafe/§2.3) |
| B (the common paste) | `90° − asin(W/s)` ⟺ `acos(W/s)` | `c = W` ⇒ `addspeed = W − c = 0` ⇒ **zero gain**. It is the *boundary at which the gain dies*, i.e. the `A → 0` limit. |
| C (Helvete the const-speed variant) | `acos(−W/s)` | the *speed-preserving* angle (`HLStrafe::ConstSpeedTheta`'s air branch when `γ ≥ 2W`): `c = −W`, `a = 2W` ⇒ `Δ|v|² = 0`. Used for turning without gaining. |

So: **A and B agree only when `A ≪ W`** (small `airaccelerate`, small `wishspeed`, or a short
slice). In CS2 with a full analog push and one accel call per tick, `A = 46.9 > W = 30` and the
correct answer is **exactly 90°**, where B gives ≈83° — a 7° error in the wrong direction, and it
loses *all* the gain (see the worked numbers below). The payload's `acos((W − A)/s)` form
(`Input::strafe_tick`, `cos_theta` branch) is form A and is the right one; it must additionally be
**clamped/`π/2`-short-circuited when `(W − A) ≤ 0`**, which HLStrafe does and the payload does not
(the payload keeps `acos` of a negative number and gets an obtuse push — see landmine 12).

### 2.5 Slicing: the tick is not one accel call

`CPlayer_MovementServices::DoMovement::Subtick` = `FUN_180a64c20` (RVA `0xa64c20`) is a loop over
the *decoded* step array (`count` at `CMoveData+0x60`, base at `+0x68`, stride `0x20`). Per
iteration, when the step's `when` **differs from the previous step's**, it writes the move/angles
into `CMoveData`, sets `gpGlobals->frametime`, and calls three movement vtable methods. The
frametime line, verbatim from the pseudocode:

```c
iVar16 = *(int *)(param_3 + 0xd4);                       // command tick base
iVar12 = *(int *)(param_3 + 0xd8) - iVar16;              // tick count spanned (usually 1)
fVar1  = *(float *)(param_3 + 0xe0);                     // slice end fraction
fVar2  = *(float *)(param_3 + 0xdc);                     // slice begin fraction
*(float *)(puVar6 + 0x34) = (float)iVar12 * 0.015625 * (fVar1 - fVar2);   // gpGlobals->frametime
```

so **`frametime` per slice = Δfraction × 1/64 s**. A single step at `when = 1` ⇒ one slice of a
full tick (`frametime = 1/64`); 12 steps at `i/12` ⇒ 12 slices of `1/768` each. This matches
independent external evidence: memorin.app's transcription of the current CS2 server binary states
*"CS2 applies acceleration per user command (subtick pipeline), not per tick. … AirAccelerate takes
frametime from the subtick interval"* ([S3]) — and the same page's *measured* ground one-tick
quantum `accelerate × maxspeed × dt = 5.5 × 250 / 64 = 21.48 u/s` is the same arithmetic with
`dt = 1/64`.

Consequence (important, and I believe under-appreciated in the current payload): **the 30 u/s cap is
per accel call, and the accel budget is per call, so splitting the tick into N slices gives N
times the headroom** (each slice has its own `addspeed = 30 − c_k`). The gain is not additive in
the naive way (the velocity and the angle change between slices), but it grows roughly linearly in
N. This is precisely what UC 769450's BN142 means by *"yaw-delta subticks are for splitting mouse
yaw across the tick (airstrafe etc.)"* ([S10]) — the split is not just cosmetic: it is where the
extra acceleration comes from. It also explains why the community movement recipes use
`when = (1/12)*i` ([S11], [S10]).

### 2.6 **The `0.5` factor in this build — the one unclear thing**

`AirAccelerate` here is `FUN_1808beb70` (RVA `0x8beb70`). The decompiled body (abridged to the
accel):

```c
local_res8[0] = FUN_1801607a0(&DAT_182529140);              // W  = sv_air_max_wishspeed = 30
fVar4 = FUN_180159ca0(&wishspeed, local_res8);              // wishspd = min(wishspeed, 30)
fVar5 = FUN_18015f0d0(param_2 + 0x38, &wishdir);            //  c = dot(velocity, wishdir)
fVar4 = fVar4 - fVar5;                                      // addspeed
if (0.0 < fVar4) {
  fVar9 = wishspeed * accel * *(float*)(param_1 + 0x26c) * frametime;   // friction, frametime
  fVar8 = fVar9 * 0.5;                                                 // <<< THE 0.5
  fVar5 = fVar4;                                    // default: add the whole addspeed
  if (fVar8 <= fVar4) {                             // addspeed >= accelspeed/2
     fVar5 = fVar8;                                 //   -> the velocity gets HALF
     if (fVar9 <= fVar4)  m_outWishVel += wishdir * fVar8;              // +0x114
     else               { fVar4 -= fVar8; m_outWishVel += wishdir * fVar4; }
  }
}
velocity(+0xe8) += wishdir * fVar5;      velocity(+0x38) += wishdir * fVar5;
```

Assembly confirms it (`0x181ac0070` = `0.5f`): `MULSS XMM7, [0x181ac0070]`, `COMISS XMM3, XMM6`,
and the two tails `ADDSS [RBX+0x114]` / `ADDSS [RBX+0xe8]` / `ADDSS [RBX+0x38]`.

Read literally, **`m_vecVelocity` (`CMoveData+0x38`, the vector that `AirMove` dots and then traces
with) receives at most `A/2`; the other half lands in `m_outWishVel` (`+0x114`)** and the sum is
the classic `min(A, addspeed)`. Two readings are possible and I could not settle them statically:

* **(i) split ledger** — `+0x38` is the *average* velocity used for the position integration
  (trapezoid), so the physical end velocity really does grow by the full `min(A, addspeed)`;
* **(ii) halved air accel** — `+0x38` is the velocity, so the effective air accel is `A/2`.

Evidence: (a) the ground `Accelerate` path is *not* obviously halved — memorin's independently
fitted one-step quantum is the un-halved `5.5·250/64 = 21.48` ([S3]), which supports (i) or an
air-only change; (b) `AirMove` moves a `base_velocity × frametime × 0.5` term between `+0x38` and
`+0x114`, i.e. the two vectors are used as the two halves of a displacement ledger. **Do not tune
this from the decompiler** — §9 gives the 3-tick measurement that decides it. Meanwhile the
difference in the *result* is small but real:

| | `a` at `s = 250`, perp. push | gain/tick | required θ |
|---|---|---|---|
| no 0.5 (Source1 semantics) | 30.000 | **+1.794 u/s** | **90.000°** |
| with 0.5 | 23.4375 | **+1.097 u/s** | 88.500° (`acos((30−23.4375)/250)`) |

### 2.7 Worked numeric example (both readings)

Setup: `v = (250, 0)`, `W = 30`, `airaccelerate = 12`, `f = 1`, `wishspeed = m_flMaxSpeed = 250`,
`dt = 1/64`. Everything below is the exact arithmetic from §2.3.

**(a) one step (`when = 1`), no-0.5:** `A = 12·250/64 = 46.875 > W` ⇒ `θ* = 90°`, `c = 0`,
`addspeed = 30`, `a = min(46.875, 30) = 30`.
`v' = (250, 30)` ⇒ `s' = 251.794` ⇒ **+1.794 u/s**, direction turned `atan2(30,250) = 6.843°`.
Cross-checks for the wrong forms: form B (`acos(30/250) = 83.108°`) ⇒ `c = 30` ⇒ `addspeed = 0` ⇒
**+0.000 u/s**; the "A-term included but halved" form (`acos(24.375/250) = 84.394°`) ⇒ `a = 5.625`
⇒ `Δ|v|² = 305.8` ⇒ **+0.611 u/s** — both well short of the optimum.

**(b) one step, with the 0.5:** `A_eff = 23.4375 < W` ⇒ `θ* = acos(6.5625/250) = 88.496°`,
`a = 23.4375` ⇒ `s' = 251.097` ⇒ **+1.097 u/s**, turn 5.355°.

**(c) 12 steps at `when = i/12` (the payload's default), no-0.5:** per slice
`A_s = 12·250/768 = 3.906 ≤ W` ⇒ `θ* = acos((30−3.906)/s) = acos(26.094/250) = 84.02°`,
`c = 26.094`, `a = 3.906` ⇒ `Δ|v|² = 2·3.906·26.094 + 3.906² = 219.1` per slice ⇒ **≈2629 per
tick** ⇒ `s' = 255.2` ⇒ **≈+5.2 u/s per tick** (subject to the velocity/angle changing between
slices, which reduces this slightly).

**(d) 12 steps with the 0.5:** `A_s = 1.953` ⇒ `θ* = acos(28.047/250) = 83.56°` ⇒ `Δ = 113.4`
per slice ⇒ **≈1361 per tick** ⇒ **≈+2.7 u/s per tick**.

So the falsifiable predictions are: *one step ≈ +1.1 … +1.8 u/s, twelve steps ≈ +2.7 … +5.2 u/s*
at 250 u/s, and the twelve-step path must be measurably non-linear in the step count. That is the
first thing to measure (§9.1) and it simultaneously settles the 0.5.

### 2.8 Why the community's `velocity_yaw ± (90° − asin(wishspeed/speed))` "works" anyway

Because in the *sliced* regime the argument `(W − A_s)` is close to `W` (with `A_s ≪ W`, e.g.
`A_s = 1.95` ⇒ `(30 − 1.95) = 28.05 ≈ 30`), so `acos((W−A_s)/s) ≈ acos(W/s)`: the two forms differ
by ~0.5–1° there. The error only becomes fatal in the *unsliced* regime (`A > W`), where the truth
is exactly 90° and the paste gives 83°.

---

## 3. The write seams and the recommendation

Recall the command chain (`docs/re/subtick.md`): `CUserCmd` (stride `0x98`) → `+0x10`
`CSGOUserCmdPB` → `+0x30` `base` (`CBaseUserCmdPB`), and the movement reads the command the
**slot-5** builder (`create_move_input`) produced. The three candidate seams:

### 3.1 (a) Analog move — `base->forwardmove/leftmove` (`+0x58/+0x5c`) + the raw axes

* **What it does**: `AirMove` builds `wish = forward·fmove + right·smove` with `fmove/smove` taken
  from `CMoveData+0x2c/+0x30`, and `AirMove` itself (in this build) reads `CMoveData+0x2c/+0x30`
  (confirmed: `fVar11 = fVar12 * fVar13 + local_108 * local_res8[0]` with `fVar13 = [param_2+0x2c]`,
  `local_res8[0] = [param_2+0x30]`).
* **Units**: the engine's own input writes `forwardmove = 1.0` in the command *and* `1.0` at
  `CCSGOInput+0x270/0x274` (repo measurement, `docs/re/subtick.md` §"The flow"); the classic ±450
  (`cl_forwardspeed`) scaling happens downstream and is clamped to `m_flMaxSpeed` by
  `CheckParameters` — which is why the wish speed in the accel formula is the *weapon* max speed
  (≈250) and why a `1.0` write already moves at full speed (204 u/s measured, 1.01× console).
* **Blocker**: `sv_quantize_movement_input` (§4). A fractional analog is honoured *client-side*
  (the payload measured `0.5 → 0.57×`) but the server's copy is quantized ⇒ client/server
  divergence ⇒ prediction-error corrections. On a local listen server you can either turn the
  convar off for probing or snap to the valid set.
* **Silence**: yes — the analog does not touch any view angle.

### 3.2 (b) The command's own `viewangles` — `base + 0x40` (field 4, `CMsgQAngle*`, has-bit `0x4`)

Steers (measured 90° with the walk turning 90°, 208 units) but the pawn's `m_angEyeAngles` follows
(90/90 measured) ⇒ **visible**, wrong for a silent strafe. It is also the basis *every* consumer
uses, so it changes the camera-relative meaning of the player's own keys.

### 3.3 (c) `CSubtickMoveStep.yaw_delta` — the silent movement-yaw channel

Measured by the repo: a yaw-only step (`has_bits = {when, pitch, yaw}`, `button = 0`, no analog),
`when = 1`, turns the walk by exactly its `yaw_delta` (89.9° for 90°) with eye angles 0 → 0. The
RE now explains *exactly* what it rotates and how it accumulates — `SetupMove`'s step copy
`FUN_180a73c70` (RVA `0xa73c70`):

```c
fVar21 = base_pitch;  fVar22 = base_yaw;        // CBaseUserCmdPB::viewangles (field 4), else the
                                               //   default CMsgQAngle instance PTR_PTR_1822002e8
for each protobuf step lVar18:
   fVar19 = (step->flWhen + 131072.0) - 131072.0;   // snap `when` to the 1/64 grid (mantissa trick)
   decoded[0] = fVar19;                             // when
   decoded[1] = step->nButton;                      // +0x08  (uint64)
   if (button == 0) { decoded[4] = flAnalogForwardDelta; decoded[5] = flAnalogLeftDelta; }  // +0x10/+0x14
   else             { decoded[4] = bPressed (byte); if (button & 0x801) ...zero the analog... }
   fVar21 += step->flPitchDelta;  decoded[6] = fVar21;     // +0x18  pitch  (ACCUMULATED)
   fVar22 += step->flYawDelta;    decoded[7] = fVar22;     // +0x1c  yaw    (ACCUMULATED)
```

and `DoMovement::Subtick` applies it per slice:

```c
if (cVar7 /* = (*(vtable+0x150))(...) */ != '\0') {
   *(float*)(CMoveData + 0x14) = step_pitch;     // the angles AirMove's AngleVectors uses
   *(float*)(CMoveData + 0x18) = step_yaw;
}
```

Answers to the brief's sub-questions:

| Question | Answer | Evidence |
|---|---|---|
| What does `yaw_delta` rotate? | the **movement basis** — `CMoveData`'s view angles (`+0x14/+0x18`), which are what `AirMove` feeds to `AngleVectors` to build `wishdir`. The command's/basis' base viewangles are untouched, hence silent. | `FUN_180a73c70` + `FUN_180a64c20` + `FUN_1808e48f0` |
| Absolute or accumulated? | **Accumulated per command**: each step's stored angle = base viewangles + Σ(deltas of that step and all earlier steps). So one step carrying Δ equals N steps carrying Δ/N *in total*, but the *first* slice uses base + Δ/N and the last uses base + Δ. | `fVar21 += flPitchDelta; fVar22 += flYawDelta` |
| What `when`? | `1.0` is the validated value for a single patch step ([S9] needed `set_when(1.f)`; the repo measured 89.9° with `when = 1`). With one step the *whole* tick is one slice, so the rotated basis covers the full tick. Beware: the engine snaps `when` to **1/64** in the copy, and dedupes steps that became identical. | `(flWhen + 131072.0) − 131072.0`; [S9] |
| Does `forwardmove = 1, leftmove = 0` then give the rotated direction? | **Yes.** `wish = forward(basis)·1 + right(basis)·0 = forward(basis)`. A yaw delta therefore rotates the *walk direction* by the same amount with `fmove = 1, smove = 0`. | `AirMove`'s wish construction; repo's 89.9° measurement |
| Is it gated? | Yes — the per-slice angle write is behind the vtable `+0x150` predicate (name not recovered; almost certainly the `sv_subtick_movement_view_angles` policy, [S12]/[S13]/[S14]). If that is off, the yaw steps do **not** steer and the strafe must fall back to the analog direction. | `FUN_180a64c20`; [S10], [S12] |

**The one gotcha in the accumulation:** the *analog* of step *k* is added to the running move
**after** slice *k* has been simulated (`fVar24 += step->fAnalogForwardDelta` sits after the
movement call). So the analog that drives slice *k* is `base + Σ_{j<k} Δ_j`: a delta carried by a
single last step (`when = 1`) lands in no slice at all. **Put the magnitude on the base
(`forwardmove`) or on the early steps**, and the rotation on the steps.

### 3.4 (d) Recommendation

1. **Direction in the basis, magnitude in a valid-set analog.** Each airborne tick, compute the
   wish yaw (§5), then emit
   * the base analog = `(forwardmove, leftmove)` of the **valid set** that best expresses the wish
     (in the un-rotated *camera* frame) — or simply `(1, 0)` if you rotate the basis so that
     "forward" *is* the wish, which is the cleanest and makes the quantizer a no-op;
   * N yaw-delta steps whose deltas sum to `wish_yaw − camera_yaw`.
   This is exactly the repo's `strafe_tick` delta (`target_yaw − view_yaw`) but written to the
   *yaw* channel instead of re-encoded as a rotated analog: it is lossless under any rounding and
   it cannot move the camera.
2. **Write the analog to both the command floats and the raw axes** (`CCSGOInput+0x270/0x274`)
   before/after slot 5 — the payload already does this, and it is the validated path.
3. **Never write the command's `viewangles`** (§3.2) and never the `input_history.view_angles`
   entries for movement (measured inert, `docs/re/subtick.md` §"The movement yaw channel").
4. Until the 0.5 is settled, use `θ* = 90°` with a guard: `θ* = (W − A ≤ 0) ? 90° : acos((W−A)/s)`.
5. Prefer `when` values on the **1/64** grid (`i/8`, `i/16`) over `i/12` (see landmine 8).

---

## 4. `sv_quantize_movement_input`

**What the binary says.** `client.dll` 25815307 contains the convar (string `0x181c09b08`,
registered by `FUN_1800b2590` into `DAT_1825291e0`, flags `0x82000`) with the description
**verbatim from the binary**:

> "Quantize movement input values. Enabling this restricts players from using analog input to move
> at fractional speeds normally impossible with digital button input."

**What it rounds, and where.** The community reading is that it rounds
`forwardmove`/`leftmove` so only the nine `{-1,0,1}²` states survive ([S6] Sumandora's post gives
the full model: *"rounding movement turned tx and ty from a 2D field … into a finite set … namely
S = { (0,0), (1,0), (-1,0), (0,1), (0,-1), (1,1), (1,-1), (-1,-1), (-1,1) } … S is derived by taking
the cartesian product of { -1, 0, 1 } with itself"*). The same post states the workaround
explicitly:

> "the new question becomes finding a yaw so that ForwardVector(wanted_yaw, dx, dy) is equal to
> ForwardVector(yaw, tx, ty) where yaw = [0;360) and tx, ty are one of S. Since there are only a
> finite set of forward- and sidemoves, we can no longer change them freely => We must adjust the
> yaw." … "I just brute forced the solution, by generating all yaws and taking the closest to the
> one that the server saw last".

**New evidence on *where* it is applied:** in this build the convar object `DAT_1825291e0` has
**only four xrefs** — two in its own registration function (`0x1800b259c`, `0x1800b2617`) and two in
its `atexit` destructor (`0x181a7b584`, `0x181a7b590`). There is **no other reader in `client.dll`**
(contrast `sv_air_max_wishspeed`, whose object has a code reader at `0x1808bebed` inside
`AirAccelerate`). So on this build the quantisation is **not applied in client.dll**; it is a
replicated `sv_` convar whose effect lives on the side that owns the authoritative movement
(the listen server / `server.dll`, which is not in this analysis set). Practical consequence for
the harness: **on the local listen server you can set it to 0 to isolate the analog channel, and
you must set it to 1 (default) to reproduce a Valve-like server.** Two further consequences:

* the client *predicts* with the un-quantized value it wrote, so with the convar on, any fractional
  analog you write is a source of client/server divergence (`m_vecPredictionError`);
* a *full-magnitude* analog (`±1`) survives quantisation untouched, and since `wishspeed` is then
  `m_flMaxSpeed`, the accel budget is the maximum one — which is the third argument for the §3.4
  recipe.

**Accepted workarounds, ranked:**

| # | Workaround | Cost / caveat |
|---|---|---|
| 1 | **Rotate the basis** (`yaw_delta`) so a *valid* analog points where you want. Direction stays exact, magnitude stays full, quantisation is a no-op, works with the camera fixed. | needs the silent-yaw channel (`sv_subtick_movement_view_angles` on — [S12],[S13]) and interacts with the step array budget (§7) |
| 2 | **Snap to the valid set** (`round(v)`, clamp to a unit step) — the payload's `valid_set()`; correct as a *fallback*, but it quantises your own wish direction to 90°-multiples or 45°-multiples (so the strafe becomes the digital 8-direction case and needs the basis rotation anyway to make progress). | with quantisation on, the *server* sees exactly what you snapped, so no divergence |
| 3 | **Change the yaw to one of the 8 discrete directions** (the sumandora solution, §above) — solve for the yaw such that a valid-state analog yields the desired world direction; equivalently "rotate the basis". | needs a *writable yaw* (camera ⇒ visible, or `yaw_delta` ⇒ silent) |
| 4 | Fractional analog (the current `move(forward=0.5)` path) | honoured client-side, **rounded server-side** ⇒ divergence/quantisation loss. Only usable with the convar off. |
| 5 | Sub-tick analog deltas (`analog_forward_delta`/`analog_left_delta`) to carry the residual | the step is applied *after* its slice and accumulates; and the residual is inside the same repeated field that the engine's own button steps use (§7) |

Reference: the community threads covering exactly this are UC 729264 ([S6], the maths), UC 753644
([S9], the movement fix and the `set_when(1.f)` correction), UC 759116 ([S8]), UC 761983 ([S11],
`AddAnalogSubtick`), UC 769450 ([S10], `CSubtickMoveStep` layout and the "don't stuff the array"
warning), plus the older "sv_quantize_movement_input fix" thread UC 725209 ([S15]) — the last one
**could not be retrieved** (empty response) and is listed as unverified.

---

## 5. Circle-strafe: side selection and direction input

### 5.1 Terminology, and where the speed actually comes from

* **auto-strafe**: a per-tick *wish direction* chooser that maximises the air-speed gain, usually
  implemented by rotating the view (the classic CS:GO internals) — the two published algorithms are
  "optimal angle every tick" and "constant yaw rate".
* **circle strafe**: the special case where the rotation direction is held constant, so the heading
  turns at a steady rate and the path closes to an arc/circle (surfers' "circle strafe"); in a
  bot it is just the auto-strafe with a fixed rotation sign.

**Why a *held* side stops working and why the cycle matters.** With a *fixed* basis and a held
strafe key, the wish is fixed at ±90° from the basis. Tick 1: `c = 0` ⇒ full gain. Tick 2: the wish
is unchanged but `c = v'·ŵ = a > 0` ⇒ `addspeed = 30 − a`, so the gain shrinks; by tick ~3 the gain
is zero and the velocity is aligned with the wish. **Speed requires re-perpendicularising the wish
every tick**, i.e. rotating the basis by ≈ the amount the velocity just turned. The *sign* of the
rotation is set by the side of the push (`right` ⇒ the velocity turns clockwise, `left` ⇒
counter-clockwise). Therefore:

* keep **one side + one rotation direction** ⇒ the heading turns at a constant rate ⇒ **a circle**
  (this is the "circle strafe", and it is what you want if the goal is speed, not heading);
* **alternate the side** while keeping the goal heading (rotate +θ, then −θ) ⇒ the heading stays
  roughly straight while the speed grows (the "left-right strafe") — this is the *steering* use of
  alternation, not a speed mechanism. (Digital-only implementations need it because they can only
  express 8 directions relative to the basis; an analog/yaw-delta bot can just set the exact wish
  and only alternates to hold a heading.)
* Required rotation rate at the optimum: `Δθ_tick = atan(a / s)` where `a = min(A, W − c)`.
  At `s = 250` with `a = 30`: **6.84°/tick ≈ 438°/s**. At `s = 300`: 5.71°/tick. This is the
  number a "constant yaw speed" strafer must track, and the number that makes the path a circle of
  radius `v/ω`.

**Published numbers.** HLStrafe exposes both policies as first-class options ([S5]):
`HLTAS::StrafingAlgorithm::YAW` with `MaxAccel`/`MaxAngle`/`ConstSpeed`/`MaxDeccel` handlers, plus
`curState.ConstantYawSpeed` with `yaw_delta = ConstantYawSpeedValue * vars.Frametime` — i.e. a
literal constant-degrees-per-second circle strafer — and `MaxAccelYawOffset` (a fixed user offset
added to the optimal angle). The repo's own single-hop measurement is the only CS2 number available
here: **+66.3 u/s gained in the air in one run with 0° camera drift** (`docs/re/subtick.md`), which
is consistent with a few u/s per tick over a hop (§2.7c/d).

**Which is better?** The optimal-angle rule is provably the per-tick maximum (§2.3); a constant
yaw rate is only optimal instantaneously (at the speed where `atan(a/s)·64` equals the chosen
rate), and it is what a human hand produces. For a bot, compute the angle from the velocity each
tick and clamp the *rate* if you want a human-looking turn.

### 5.2 Taking a *desired direction* from the keys (the spec: W/A/S/D, none ⇒ forward)

The published standard is **(i): use the key direction as the goal heading, and pick the strafe
side that makes progress toward it** — HLStrafe's `MaxAccelIntoYawTheta` ([S5]):

```cpp
double MaxAccelIntoYawTheta(player, vars, postype, wishspeed, vel_yaw, yaw) {
	if (!IsZero(player.Velocity)) vel_yaw = atan2(player.Velocity[1], player.Velocity[0]);
	double theta = MaxAccelTheta(player, vars, postype, wishspeed);
	if (theta == 0.0 || theta == M_PI) return NormalizeRad(yaw - vel_yaw + theta);
	return std::copysign(theta, NormalizeRad(yaw - vel_yaw));   // <-- full optimal angle, side of the goal
}
```

and the buttons are then chosen as the closest of the 8 digital directions (`GetBestButtons(theta,
right)`), with the *yaw* solved so the wish lands at `vel_yaw ± θ*` (`yaw = vel_yaw - phi + theta`).
Note it never alternates: the side is the sign of `goal − velocity`, always, and when `|goal −
vel|` is smaller than `θ*` the strafer still pushes the *full* `θ*` (so it oscillates around the
goal at maximum gain). For a purely speed-driven bot, `BestStrafeMaxAccel` evaluates both sides and
keeps whichever ends faster — for a perpendicular push they are symmetric, which is why the
"which side" question only matters for steering.

Domain recipe (all angles in the engine convention: yaw increases counter-clockwise, `forward(yaw) =
(cos, sin)`, `right(yaw) = (sin, −cos)`, so a **positive** yaw delta turns the wish left):

```
keys = engine input buttons (read BEFORE overwriting the mask), in the view frame
phi  = 0°            if W (or nothing) is held                  # goal = the view's forward
phi  = +90°          if only A          (left)
phi  = -90°          if only D          (right)
phi  = 180°          if only S          (behind)
phi  = ±45°          for the W+A / W+D diagonals (and ∓135° for S+A / S+D)
goal_yaw = camera_yaw + phi
```

Note the *silent* consequence: because the camera does not move, `camera_yaw` is whatever the human
is looking at; "no key ⇒ forward" therefore means *toward the camera's forward*, which is the
natural trainer semantics.

### 5.3 Reference pseudocode

```cpp
// Per tick, airborne only. `basis` = the command's view yaw (the camera); `vel` = horizontal velocity.
void strafe_tick(float camera_yaw, vec2 vel, uint64 held_keys, void* cmd, uint8* base) {
  constexpr float W = 30.f, ACCEL = 12.f, DT = 1.f / 64.f, F = 1.f /* surface friction */;
  const float s = length(vel);
  if (on_ground() || s < 30.f) return;                      // nothing to gain

  // 1. goal heading from the held keys (view-relative)
  float phi   = key_phi(held_keys);                          // 0/±45/±90/180 (none => 0)
  float goal  = normalize(camera_yaw + phi);
  float delta = normalize(goal - yaw_of(vel));                // signed shortest angle

  // 2. optimal wish angle (HLStrafe MaxAccelTheta / section 2.3)
  const int   N  = 8;                                        // slices this tick
  const float As = N * /*wishspeed=*/ 250.f * ACCEL * (DT / N) * F;   // per-slice budget
  float theta = (W - As) <= 0.f ? 90.f : to_deg(acos((W - As) / s));
  theta = std::max(theta, 1.f);                              // degenerate-speed guard

  // 3. side: toward the goal; when |delta| < theta this degenerates to "on heading" and the
  //    velocity oscillates about the goal while gaining speed.
  const float side      = (delta >= 0.f) ? +1.f : -1.f;
  const float wish_yaw  = yaw_of(vel) + side * theta;         // the direction to push
  const float basis_rot = normalize(wish_yaw - camera_yaw);   // what the basis must become

  // 4. write: magnitude in the analog, direction in the silent basis
  write_raw_move_and_base(base, /*fwd=*/1.f, /*side=*/0.f);   // (1,0) = "along the basis forward"
  for (int i = 1; i <= N; ++i)                                // deltas accumulate per command
    add_yaw_step(base, basis_rot / N, /*when=*/ (float)i / N);
  // -> OD  : basis slice i uses camera_yaw + i*(basis_rot/N); wish = forward(basis) * 1
  // -> camera and eye angles untouched (silent)
}
```

Two variants worth keeping in the harness:

* **`step=1`, `when=1`** (cheapest, best understood): one yaw step of the whole `basis_rot`;
  one slice; expect **+1.1…+1.8 u/s** per tick at 250 (§2.7a/b).
* **`N=8…16` splits**: expect the super-linear gain of §2.7c/d — the measurement that settles §2.6.

---

## 6. Landmine list

1. **The 30 cap is on `dot(velocity, wishdir)`, not on the gain.** `addspeed = 30 − v·ŵ > 0` is
   required, so a wish **along** the velocity stops accelerating entirely once `s ≥ 30`. This is
   why "speed only grows below the cap" — more precisely, *the projection of the velocity on the
   wish* must be under 30. Never aim the wish forward if you want speed.
2. **`A > W` changes the optimum discontinuously.** With `A = airaccelerate·wishspeed·dt ≥ 30` the
   optimum is *exactly* 90°, not `acos((30−A)/s)` (which would be obtuse). HLStrafe returns `π/2`;
   the payload's `std::fmax(acos(...), 1°)` does **not** and will produce an obtuse push that
   *loses* speed. Guard with `tmp = W − A; theta = tmp <= 0 ? 90° : acos(tmp/s)`.
3. **`A` depends on the weapon and on the slice length.** `wishspeed = min(|wish|, m_flMaxSpeed)`:
   a rifle (250) gives `A = 46.9`, a weapon with a lower max speed gives less; and slicing divides
   `dt`. So "90°" is a weapon- and step-count-dependent conclusion — compute it per tick.
4. **Air vs ground.** `sv_accelerate` (5.5) and ground friction are the ground path; the air uses
   `sv_airaccelerate` (12). Do not strafe on the ground (the payload already gates on
   `FL_ONGROUND`): a ground "strafe" fights friction and the `CheckParameters` speed clamp, and
   holding a movement key on landing can only *lose* speed.
5. **`m_surfaceFriction` is a multiplier on the air accel too** (`accel·friction·frametime`), and it
   is whatever the *last ground surface* set (CS scales it by 1.25 and clamps at 1.0). Air-strafing
   after a low-friction surface (ice, surf ramps) therefore gains *less*, not more. Do not assume
   `f = 1`.
6. **The subtick array is shared with the engine** (UC 769450, [S10], quoted in `docs/re/subtick.md`):
   *"scroll jump is real IN_JUMP press/release subticks. stuffing extra steps in the repeated field
   is why wheel jump died."* ⇒ **append only; never clear the field**; and the production recipe
   that *does* clear it (UC 761983/[S11] movement fix) is only safe in a build/no-bhop context.
7. **The admission gate rejects the *whole* array** (community reverse, UC 771389 Part II, quoted in
   `docs/re/research-bhop.md` §3.3): `count > 32`, `prev_when > when` (**monotonic non-decreasing**),
   `when > 1.0`, `popcount(button) != 1`, `(button & 0xE1F) == 0`, or `view_delta_accum > 2.0` ⇒
   *every* step in that command is discarded and only the implicit `{when=1, button=0}` trailer
   remains. For a strafe this means: **never write two button bits in one step; keep the total step
   count bounded; insertion-sort by `when`; and keep the accumulated per-tick view rotation under
   2.0** (a 90° turn = 1.571 rad, a 115° turn is over the limit — if the constant is radians, which
   the linear compare suggests; verify). The strafe was only validated with a *single* 90° yaw step,
   so **the 8–16-step split has not yet been checked against this gate** — a good reason to log the
   engine's accepted step count.
8. **`when` is snapped to the 1/64 grid by the engine's own copy** (`(x + 131072) − 131072`,
   `FUN_180a73c70`), and steps that become identical after snapping are **collapsed**
   (`FUN_180a73c70`'s trailing compression loop compares button, both analog deltas and both angles
   against the next step and removes the duplicate). `i/12` fractions are not on the grid (5.33/64,
   10.67/64, …); `i/8`, `i/16` are. Prefer grid-aligned fractions.
9. **A step's analog delta applies *after* its slice** (`DoMovement::Subtick` accumulates
   `fVar24/fVar23` after the movement call). A single step at `when = 1` carrying only analog
   deltas is consumed by nobody — put the magnitude on the base `forwardmove`/`leftmove`.
10. **`analog_*_delta` is a delta relative to the base, and it accumulates across steps** (the
    community recipe `delta = base − desired`, UC 761983/769450). Writing an absolute value on top
    of a base that already carries it double-counts.
11. **The per-slice view-angle write is behind a predicate** (`(*(vtable+0x150))()` in
    `DoMovement::Subtick`) — the `sv_subtick_movement_view_angles` policy. On a listen server you
    can toggle it: with it off, `yaw_delta` will *not* steer `wishdir` and the silent-yaw strafe
    silently stops working (the walk direction reverts to the camera frame). Always keep an analog
    fallback (direction in the analog) or assert the cvar.
12. **Silence must be *proved*, not assumed.** Read `m_angEyeAngles` (and the camera) before and
    after and require exact equality, and remember that the two *other* view channels are silent
    but inert for movement: `input_history[i].view_angles` (silent aim) and the raw
    `CCSGOInput+0xbd0` history. Only the command's own `viewangles` steers *and* moves the eyes.
13. **Units.** The command analog is `±1` in this build (not `±450`); the raw axes
    (`CCSGOInput+0x270/0x274`) are `±1` too. The ±450 convention in older public CS2/CS:GO
    autostrafe pastes ([S7]) does not apply here — a `450` write moves nothing.
14. **Do not write `CCSGOInput + 0xb58 + 0x18`** (the per-controller input entry): it crashed the
    game (input-path AV at `client.dll + 0xb64a80`) and reads denormals — the layout does not hold
    for this build (`docs/re/subtick.md`).
15. **The command is rebuilt every frame** and `CUserCmd::Reset` clears the sub-tick table, so the
    strafe's steps must be re-derived and re-appended **every frame**, idempotently by *array
    content* (the same lesson as the bhop, `docs/re/research-bhop.md` §6.2).
16. **`m_outWishVel` gets the other half of the accel** (§2.6) — do not read it as "the velocity".
    Also do not read the engine's `CMoveData` fields without re-deriving them: `+0x14/+0x18` are
    view angles, `+0x2c/+0x30/+0x34` are the move, `+0x38` is the velocity, `+0x108/+0x114` are the
    second velocity ledger.
17. **Client/server divergence from a fractional analog** (§4) — with
    `sv_quantize_movement_input` on (default), the client predicts a speed the server never applies.
18. **Do not strafe while `m_nActualMoveType` is not `MOVETYPE_WALK`** (noclip/ladder/observer) and
    not while dead/spectating; the bhop research records the same class of guard
    (`docs/re/prediction.md`).

---

## 7. Can the strafe and the bhop share one command?

Yes, with the rules above. The evidence:

* The engine's own steps **already live in the same `RepeatedPtrField`** (`base + 0x18`; the engine
  appends a real `IN_JUMP` press/release step at `when = 0` on the tick the key changes —
  measured, `docs/re/subtick.md` §"The flow"), so *both* features must be additive.
* The admission gate is **per command and wholesale** (§6.7): the strafe's yaw steps and the bhop's
  release+press pair together must satisfy `count ≤ 32`, monotonic `when`, one button bit per step,
  and the view-delta accumulator. The payload already insertion-sorts the bhop pair
  (`Input::write_bhop_pair`) — the strafe must do the same for its yaw steps, and both must share
  one budget check rather than each checking independently.
* Concretely, a workable per-tick allocation: 1 release + 1 press (bhop) + 1…N yaw steps (strafe)
  + the engine's own 1–2 steps ⇒ keep a hard ceiling well under 32 (the payload's
  `k_bhop_step_capacity = 32` is the gate's own limit, not a safe working budget). The repo's
  `docs/re/research-bhop.md` §9.9 flags exactly this interaction as **not yet re-checked**, and it
  remains open here too.
* `view_delta_accum` is the interaction that can silently kill *both* features: a strafe that
  rotates the view a lot on a landing tick would discard the bhop pair in the same array.

---

## 8. Sources

| # | Source | URL / id | Confidence |
|---|---|---|---|
| S1 | Valve, **Source SDK 2013** `src/game/shared/gamemovement.cpp` (`AirMove`, `AirAccelerate`, `CheckParameters`, `Accelerate`) — fetched in full | <https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/game/shared/gamemovement.cpp> | **High** — first-party source; CS2's function is structurally the same (S3, and the decompile in §2.6) |
| S2 | Valve Developer Wiki, *List of Counter-Strike 2 console commands and variables* — `sv_air_max_wishspeed 30`, `sv_airaccelerate 12`, `sv_accelerate 5.5`, `sv_maxspeed 320`, flags `sv, cl, nf, rep, release` | <https://developer.valvesoftware.com/wiki/List_of_Counter-Strike_2_console_commands_and_variables> | **High** for the defaults/flags (retrieved 2026-10; earlier attempts were Anubis-blocked, this one returned the table) |
| S3 | **memorin.app**, *CS2 air acceleration: sv_airaccelerate 12, capped by a 30 u/s wish speed* (verified 2026-07-27) — transcription of the algorithm, the capped/full asymmetry, "per user command (subtick pipeline)", the measured ground quantum `5.5·250/64 = 21.48` and plateau 250 | <https://memorin.app/mechanics/air-acceleration> | **Medium-High** — independent third-party RE/measurement, matches S1, S2 and this repo's measurements; the page's own tier claims "VERIFIED (algorithm RE'd)" for the air rule |
| S4 | Valve, **CS2 update notes** (2025-07-30 and 2025-09-26/10-30): sub-tick view angles affect movement; `sv_subtick_movement_view_angles` "will now only send subtick view angles to the server with other subtick events" | quoted via <https://key-drop.com/blog/ropz-discovers-cs2-movement-bug-that-boosts-jump-speed/> and <https://tradeit.gg/blog/cs2-updates/>; discussion: <https://www.reddit.com/r/GlobalOffensive/comments/1mfagzf/counterstrike_2_update_for_08012025/> | **Medium** — official text quoted by third parties; the Valve news page itself was not fetchable |
| S5 | **HLTAS/hlstrafe** — MIT library for HL movement optimisation (`MaxAccelTheta`, `ConstSpeedTheta`, `MaxAccelIntoYawTheta`, `SideStrafeGeneral`, `GetBestButtons`, `ConstantYawSpeed`, `MaxAccelYawOffset`), used by Bunnymod XT | <https://github.com/HLTAS/hlstrafe/blob/master/src/hlstrafe.cpp> | **High** for the maths (source read verbatim); it is the Half-Life/Quake family rules, which the air case shares with CS2 |
| S6 | UC **729264** *"[Discuss] Working around sv_quantize_movement_input for auto strafer"* (DylanDakota019, 2025-12-13); **Sumandora** post #5 (2025-12-14) — the full mathematical model, the 9-state set `S = {-1,0,1}²`, "solve for the yaw", brute-force closest-yaw | <https://www.unknowncheats.me/forum/counter-strike-2-a/729264-sv_quantize_movement_input-auto-strafer.html> | **High** — post retrieved verbatim |
| S7 | UC **657452** *"[Help] Auto Strafe (WASD)"* (zexolas, 2024-08-28) — a naive CS2 implementation: read the held WASD mask, write `flForwardMove/flSideMove = ±450` in the view frame; no strafe angle at all | <https://www.unknowncheats.me/forum/counter-strike-2-a/657452-auto-strafe-wasd.html> | **Medium** — retrieved verbatim; useful as the *negative* reference (no speed gain; ±450 units are wrong for build 25815307) |
| S8 | UC **759116** *"[Source] Subtick bhop"* — includes a `QueueForceSubtickMove(base)` helper (that build: `client.dll + 0x9CA720`) and the note that only slot 5 accepts sub-tick writes | <https://www.unknowncheats.me/forum/counter-strike-2-a/759116-subtick-bhop-source.html> | **Medium** — indexed/partially retrieved (printthread listing); details quoted from the repo's own earlier reading |
| S9 | UC **753644** *"[Help] movement fix + sv_quantize_movement_input"* (cacadura9090, 2026-05-21) — the fix only worked after `set_when(0.f)` → `set_when(1.f)` | <https://www.unknowncheats.me/forum/counter-strike-2-a/753644-movement-fix-sv_quantize_movement_input.html> | **Medium-High** — as recorded in the repo (direct fetch of the thread was not repeated here) |
| S10 | UC **769450** *"[Help] antiaim movement / subticks"* (gentpooy, 2026-08-28; reply **BN142**, 2026-08-31) — `CSubtickMoveStep` layout with the extra `metadata` field; *"yaw-delta subticks are for splitting mouse yaw across the tick (airstrafe etc). analog-delta subticks are for analog that did not already exist in the list."*; *"scroll jump is real IN_JUMP press/release subticks … stuffing extra steps in the repeated field is why wheel jump died"*; the OP's own finding that a yaw delta "works good until ~60 degrees offset … then it just stops moving" | <https://www.unknowncheats.me/forum/counter-strike-2-a/769450-antiaim-movement-subticks.html> | **High** — thread retrieved verbatim |
| S11 | UC **761983** `AddAnalogSubtick` recipe — reuse `elements[current_size++]` when `current_size < allocated`, else `CreateSubtickMoveStep(arena)` + `Add(field, step)`; delta relative to the base; `when = (1/12)*i` | as recorded in this repo (`docs/re/subtick.md`, `docs/re/research-bhop.md`) | **Medium-High** — the append path matches this build's `subtick_step_alloc`/`repeated_ptr_add`, verified in-process by the repo |
| S12 | UC UC 729264 post #4 (EzDiaoL): *"after the game update, `sv_subtick_movement_view_angles` controls whether the subtick angles affects movement"* + `CSubtickMoveStep { float pitch_delta_; float yaw_delta_; }` | same URL as S6 | **Medium** — forum claim that matches this build's gated per-slice angle write (§3.3) |
| S13 | UC **771389** / **774033** (InkCrow, sub-tick bhop) — the **admission gate** (`count > 32`, monotonic `when`, `popcount(button) != 1`, `view_delta_accum > 2.0` ⇒ DISCARD_ALL), "one `ProcessMovement` per distinct fraction", "a second step on the same fraction is free", the engine's own press/release steps in the same field | <https://www.unknowncheats.me/forum/counter-strike-2-a/771389-subtick-bhop.html>, <https://www.unknowncheats.me/forum/counter-strike-2-a/774033-subtick-bhop.html> (quoted at length in `docs/re/research-bhop.md`) | **High** for the mechanism per the repo's verbatim reading |
| S14 | SteamDatabase **GameTracking-CS2** `Protobufs/usercmd.proto` (`CSubtickMoveStep{button=1,pressed=2,when=3,analog_forward_delta=4,analog_left_delta=5,pitch_delta=8,yaw_delta=9}`, `CBaseUserCmdPB{…,forwardmove=5,leftmove=6,upmove=7,…,subtick_moves=18,…}`) | <https://github.com/SteamDatabase/GameTracking-CS2/blob/master/Protobufs/usercmd.proto> | **High** — authoritative field names/numbers, matches this build's wire parser |
| S15 | UC **725209** *"sv_quantize_movement_input 'fix'"* (Kamaro) | <https://www.unknowncheats.me/forum/counter-strike-2-a/725209-sv_quantize_movement_input-fix.html> | **Unavailable** — the fetch returned an empty body; referenced only as a thread title from UC's own "similar threads" index. **Unverified.** |
| S16 | UC **627344** *"How do i make my bhop auto strafe/gain speed?"* | <https://www.unknowncheats.me/forum/counter-strike-2-a/627344-bhop-auto-strafe-gain-speed.html> | **Unavailable** — HTTP 403 on both the thread and the print view. Not used. |
| S17 | This repo: `docs/re/subtick.md` (`CSubtickMoveStep`, the movement yaw channel table, the measured 89.9° silent yaw step, the analog unit measurement, the `+0xb58` crash), `docs/re/research-bhop.md` (the admission gate, the step-array rules), `docs/re/prediction.md`, `payload/control/src/input.cpp` (`Input::strafe_tick`) | local | **High** for the measurements; the local *interpretations* corrected here are listed in §9.4 |
| S18 | Private reference `movement/impl/airstrafe.cpp` + `movement/impl/bunnyhop.cpp` | — | **NOT FOUND.** Searched (GitHub/code search/forum) and not located; the repo's own earlier note agrees. Treated as **private/unverifiable**; not used as corroboration anywhere in this report. |
| S19 | `a2x/cs2-dumper` `client_dll.hpp`; `s2v.app` schema explorer | <https://github.com/a2x/cs2-dumper>, <https://s2v.app/SchemaExplorer/cs2/client/CCSPlayer_MovementServices> | **High** — used only for the schema field names this repo already cross-checks |
| S20 | REA / Ghidra 12.1.4 on `client.dll` 25815307 (sha `44af2aa0…`): `AirMove FUN_1808e48f0` (`"PreSource1AirMove"`), `AirAccelerate FUN_1808beb70`, `AddGravity FUN_1808be720`, `DoMovement::Subtick FUN_180a64c20`, the button-step loop `FUN_180a651b0`, the step copy `FUN_180a73c70`, the slice-bounds setter `FUN_180a53240`, the convar registrations `FUN_1800b1950`/`FUN_1800b2590`/`FUN_1800d8450`/`FUN_1800d8510`, `0x181ac0070` = {0.5,1,2,3} | local | **Highest** — first-hand decompilation/disassembly of this build; pseudocode is Ghidra output, not source |
| S21 | CS2 Docs convar list (`sv_jump_spam_penalty_time` etc.) | <https://cs2.poggu.me/dumped-data/convar-list/> | Medium (used for bhop cross-references only) |
| S22 | UC **standalone "strafe per subtick" / "How do I implement Autostrafe correctly?"** threads | titles seen in UC's similar-thread index; bodies **not retrieved** | **Unverified** — listed only because they exist |

---

## 9. Open questions that must be verified in-process / by measurement

Ordered by how much they change the implementation.

1. **The `0.5` in `AirAccelerate` (§2.6) — settle it first.** Experiment (in the existing
   `movement-probe` harness): from a pinned spot, jump, and at a known speed `s ≈ 150–200` write
   **one** `yaw_delta` step (`when = 1`) whose angle is exactly perpendicular to the velocity, with
   the base analog at `(1,0)`; read `|v|` before and after the tick.
   * Source1 semantics ⇒ `Δs = (sqrt(s² + W²) − s) = 450/s` ⇒ ≈ **+2.4 u/s** at 187.
   * Halved ⇒ `a = A/2 = 23.44` ⇒ `Δs ≈ 550/s` ⇒ ≈ **+1.5 u/s** at 187.
   Then repeat with `N = 4, 8, 16` steps and confirm the super-linear scaling of §2.7c/d. This also
   locates the operating regime (`A` vs `W`) and calibrates `f` (`m_surfaceFriction`).
2. **Does the movement really re-run `AirAccelerate` once per slice with `frametime = Δwhen/64`?**
   Static evidence says yes (§2.5) and the step-count scaling of (1) tests it directly. A
   cross-check that does not need a jump: the ground one-tick quantum (memorin's 21.48 u/s, [S3])
   should scale with the number of ground sub-tick steps in the same way.
3. **The `sv_quantize_movement_input` rounding rule and *where*.** Set it to 1 and 0 on the listen
   server and drive `move(forward = 0.25/0.5/0.75)`; log the movement actually applied (position
   delta) and `m_vecPredictionError`. Determine (a) per-axis round-half-to-even vs nearest-valid-
   state vs drop-to-zero, (b) whether a fractional write produces a prediction correction (which
   would prove the client honours it and the server does not). The static result "no reader in
   client.dll" (§4) predicts a correction.
4. **The `sv_subtick_movement_view_angles` predicate.** Confirm the vtable `+0x150` call in
   `DoMovement::Subtick` reads that convar (read the convar object referenced there), and then
   verify behaviour with the cvar 0: the `yaw_delta` step must stop steering. This decides whether
   the silent-yaw seam is usable as the primary channel.
5. **The admission gate on this build.** Locate the gate (the 2026-09-08 community address was
   `0x180A36170`; ours will differ) and read the constants: is the view-delta threshold really 2.0,
   in radians, accumulated per command? Then test a strafe that rotates 60°/tick and 100°/tick with
   and without a bhop pair in the same array, and watch whether the whole array is dropped.
6. **`when` grid + step collapse.** Verify `(when + 131072) − 131072` (1/64) against a live read of
   the decoded array, and check how many of 12 `i/12` steps survive (the collapse loop in
   `FUN_180a73c70`).
7. **Sign conventions.** Positive `yaw_delta` turned the walk 89.9° — in which world direction, and
   which sign of `leftmove` is "right"? (The engine's `right = (sin yaw, −cos yaw)` says positive
   side = right; the payload's comment agrees.) Confirm with one in-game test before trusting the
   side-selection sign in §5.2.
8. **`m_flMaxSpeed` per weapon** (the `A` input) — read it live for the weapons in the test loadout
   and store it, rather than assuming 250.
9. **Which command is read by the movement** — the slot-5 build vs the slot-25 copy
   (`docs/re/subtick.md` §"Open questions"); the strafe must write the one the movement consumes.
   (The bhop research's YouGame 336028 citation — "only slot 5" — is what the payload relies on.)
10. **Ground interaction** — confirm the strafe is inert when grounded and that a landing does not
    inherit a stale yaw step (the sub-tick table is rebuilt every frame, so a stale step should not
    survive; verify).
11. **Human-likeness / detection surface** — the per-tick rotation of ~7°/tick is large; consider
    clamping the rotation rate (HLStrafe's `ConstantYawSpeed`) and the admission-gate limit.

### 9.4 Corrections to this repo's current notes

* `docs/re/subtick.md` §"The movement yaw channel" is right that `yaw_delta` steers silently, but
  the *mechanism* is now known: the per-slice angles are the **movement basis** (`CMoveData
  +0x14/+0x18`), they are **absolute = base viewangles + cumulative deltas**, and the write is
  gated on a vtable predicate (`sv_subtick_movement_view_angles` candidates).
* The payload's optimal-angle expression (`Input::strafe_tick`) is form A of §2.4 but is **missing
  the `A ≥ W ⇒ 90°` short-circuit**, and it feeds `k_strafe_max_speed = 250` ×
  `k_strafe_airaccelerate = 12` × `tick/steps` as the per-step budget — consistent with §2.7 if the
  engine slices, but the 0.5 of §2.6 halves that budget on this build (i.e. re-verify `A` there).
* The payload's `strafe_tick` writes the analog in the *rotated frame* (`rotate_movement`) and only
  the command's base floats. Given §3.3/§3.4 the better construction is the opposite split:
  **keep the analog at a valid-set forward and put the rotation on `yaw_delta`** — which is also
  immune to `sv_quantize_movement_input`.
* `docs/re/subtick.md` states the strafe is "not working in-game" with the leading hypothesis about
  the raw axes being clobbered. The RE here adds a second, independent candidate: a single `when=1`
  yaw step puts the *whole* tick in one slice, and the analog written on the *base* is what that
  slice consumes — so the combination should work; if it does not, the sub-tick steps are likely
  being dropped by the admission gate (landmine 7) or the `+0x150` predicate is false (landmine 11).
  Both are cheap to test and should be logged.
