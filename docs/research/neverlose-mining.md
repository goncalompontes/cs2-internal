# Neverlose mining — what a reversed third-party CS2 cheat does, and what we steal from it

> Research note, 2026-10-20. The second source of truth for the feature wave
> (task T7 / item 6 of the wave brief).
> Scope: the **reversed Neverlose CS2 module** (`neverlose_cs2.dll`, a live memory
> dump of a third-party product), read as *reference material only*. Nothing from
> it is copied; every claim below is a **description** with an evidence address.
>
> **Ground truth.** The raw live image
> `re/cs2_212C3300000/cs2_212C3300000.bin`
> (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
> base `0x212C3300000`, size `0x5000000`, **`VA = base + file_offset`**), and the
> reconstructed PE `neverlose_seeded3.exe`
> (sha256 `21efa969cacf8e8f5c36066a0ac9de0bd11ae3184bcc1a562892839e31e16832`).
> The decompilation used is Ghidra's `all/funcs_00*.c` (47 MB, raw) plus the
> deobfuscated closures `ghout_deobf/closure_movement_v2.c`,
> `closure_entity_v2.c`, `closure_render.c` (project **gproj11**).
>
> **Method.** The prior session's notes (`SUBSYSTEM_*.md`, `FEATURES.md`, `DEOBF.md`)
> were read for orientation only; every claim intended for use was re-checked
> against the bytes. Where the notes and the bytes disagree, **the bytes win** and
> the disagreement is called out.
>
> **Confidence labels.** `verified` = I read the instruction/decompilation body;
> `inferred` = interpretation of what I read; `not found` = I looked and it is
> not there (which is *not* the same as "the product does not have it" — see §5.8).

---

## 0. Summary

| # | Question | Neverlose's answer | Our answer | Same / different | What we should change |
|---|---|---|---|---|---|
| 1.1 | Wish direction: held keys (WASD) or the camera? | **Both, and the keys win.** The wish is `forwardmove·F + leftmove·R` in world space; `forwardmove`/`leftmove` are the engine's own W/A/S/D axes, read at `0x212c37bff61/66`. The camera only supplies the basis `F`/`R` | The wish is the **W/A/S/D intent** in the view frame, converted to a world heading (`strafe_geometry.hpp` §`goal_for`) | **Same idea, different mechanism** | Read the **analog axes** instead of the button mask — one read, and it is the same value the engine uses for the wish. Small win. |
| 1.2 | How is the side picked? | **No dead zone, no hysteresis, no latch.** The side is the *sign* of three velocity-derived components vs `0` (`0x212c37c037f-0x212c37c0394`), combined with a common flip term | A **20° dead zone + a "last decisive side" latch** (`plan()`, `k_strafe_side_deadzone_deg`) | **Different** | Keep ours (it is strictly better than a pure sign). See §1.5 for the one idea worth borrowing. |
| 1.3 | Magnitude and channel? | **The channel is the camera.** It writes the computed (pitch, yaw) to `cmd+0x20` / `cmd+0xA8` (`0x212c37c035c-0x212c37c03a7`), clears the movement bits and re-sets a **diagonal** button pair (`0x212c37c03b9-0x212c37c03f5`), and sets the analog to **±1.0** on both axes (`0x212c37c0404-0x212c37c0437`) | A **silent wish vector**: analog floats + a sub-tick `yaw_delta` rotation. The camera never moves | **Different — and ours is the better design** | **Do not adopt** their camera-rotation channel: it is visible and it couples the strafe to the aim. |
| 1.4 | Buttons read before or after its own writes? | **Before.** The axes are read at `0x212c37bff61/66`; the first write is at `0x212c37c035c` — ~1 KB of code earlier | Same: `Input::strafe_tick` reads the mask before `write_strafe_wish` | **Same** | — |
| 2 | Camera / third person | **Not found.** No third-person / camera-distance / FOV feature in the image. The only camera writes are the two view-angle writes above | We drive the **engine's own** third person (`thirdperson.cpp`) | **Different** | Nothing to steal. Keep ours. |
| 3 | Chams | **There is no engine material and no material override.** Neverlose ships its **own D3D11 renderer with 28 embedded DXBC shaders**; its "chams" is a **world-space convex-hull polygon** drawn by its own shader, coloured by its own constant buffer (`hull_color` float4 + `global_alpha`) | Chams is **config only** (`ChamsMode {flat, glow, wireframe}`, `chams_color` RGBA) — **no implementation in the tree**; the plan is a material override | **Different approach** | Steal the **pass model** and the **per-point radius** (§7.3, §7.4); keep the material override as the primary. |
| 4 | ESP / entity classes | **27+ entity classes** resolved *by name* through the engine's schema lookup; the classifier maps a class to a small integer "kind" and walks the **schema class chain** for inheritance | Players, world (grenades / C4 / dropped weapons), radar, sound, hitboxes, skeleton, tracers, off-screen | **Same coverage, different mechanism** | Steal the **class-chain inheritance walk** (§7.6) if our snapshot only exposes the concrete class. |
| 5 | Protection | Opaque predicates (3,100 sites, two fingerprint families), a manual map, an **encrypted `.enc1` region** with no static decryptor, no `d3d11`/`materialsystem2` imports, and a **server-side CJSON feature list** | `payload/loader/` already written for this | informational | **Nothing to adopt** (by instruction, and because it is a "hide from a third party" design). |

---

## 1. Movement / autostrafe

**Where it lives.** `FUN_212c37beb50` (44,518 bytes) at `0x212c37beb50` — the
per-tick command builder. It bails when `state+0x70 == 7` (`0x212c37bebca`),
stores `cmd` at `ctx+0xD38` and `state` at `ctx+0xD30` (`0x212c37bebda/e1`), and
dispatches on `state+0x1C`: **2** = grounded pathing + "aim view" application,
**9** = the **air strafe + bhop** path, anything else = return
(`closure_movement_v2.c:5029-5031`). The air-strafe block is reached at
`0x212c37bff5a`. `verified`

### 1.1 The wish source — the held keys, *not* the camera alone

```
0x212c37bff5a  mov   rax, [rsi+0xd38]        ; cmd
0x212c37bff61  vmovss xmm8, [rax+0x2c]      ; xmm8 = cmd->forwardmove   (the W/S axis)
0x212c37bff66  vmovss xmm9, [rax+0x30]      ; xmm9 = cmd->leftmove      (the A/D axis)
0x212c37bff6b  vmovss xmm1, [rip+…]         ; 0.017453292  (DEG2RAD)
0x212c37bff73  vmulss xmm0, xmm1, [rax+0x20]; pitch  (rad)
0x212c37bff78  vmulss xmm6, xmm1, [rax+0x24]; yaw    (rad)
0x212c37bff7d  vmulss xmm7, xmm1, [rax+0x28]; roll  (rad)
0x212c37bff93  call  rdi                    ; SinCos(pitch) -> a0, a1
0x212c37bffa4  call  rdi                    ; SinCos(yaw)   -> b0, b1
0x212c37bffb5  call  rdi                    ; SinCos(roll)  -> c0, c1
```

`verified` (instruction level, `all/funcs_005.c` + my own disassembly of the
raw image; the three `call rdi` sites are `0x212c42926f0`, a captured
`SinCos`-style helper).

The wish is then assembled in world space:

```
0x212c37c010c  vmulss xmm0, xmm8, [rbp+0x330]   ; forwardmove * (cos p · cos y)
0x212c37c0114  vmulss xmm1, xmm8, [rbp+0x228]   ; forwardmove * (cos p · sin y)
0x212c37c011c  vmulss xmm3, xmm9, xmm12        ; leftmove  * R.x
0x212c37c0121  vaddss xmm12, xmm0, xmm3        ; wish.x
0x212c37c0125  vmulss xmm0, xmm9, xmm15        ; leftmove  * R.y
0x212c37c012a  vaddss xmm0, xmm1, xmm0         ; wish.y
0x212c37c012e  vmulss xmm1, xmm9, xmm13        ; leftmove  * R.z
0x212c37c0133  vmulss xmm3, xmm8, xmm2         ; forwardmove * sin(pitch)
0x212c37c0137  vsubss xmm1, xmm1, xmm3         ; wish.z
```

`verified`. So: **`wish = forwardmove · F + leftmove · R`** where `F` is the
camera forward and `R` is the camera's lateral axis. The *magnitude* of the
player's key press is carried by the analog axes, exactly as the engine itself
carries it. The camera supplies the frame, the **keys supply the heading**.

> **This is the same decision as our "directional" mode** (`StrafeMode::directional`,
> `strafe_geometry.hpp` §`goal_for`): the goal heading is the player's W/A/S/D
> intent, not the camera. The difference is only in how the intent is *read*
> (their analog axes vs our button mask) and in how the wish is *delivered*
> (their camera angle vs our silent wish vector).

### 1.2 The velocity

```
0x212c37c0027  vmovss xmm5,  [rax+0x2dc]   ; vx
0x212c37c002f  vmovss xmm10, [rax+0x2e0]   ; vy
0x212c37c0037  vmovss xmm11, [rax+0x2e4]   ; vz
0x212c37c003f  vxorps xmm4, xmm4, xmm4    ; 0
0x212c37c0043  vmulss xmm0, xmm11, xmm4   ; vz * 0  -> the z is killed
0x212c37c0047  vsubss xmm3, xmm0, xmm10   ; -vy
0x212c37c004c  vsubss xmm14, xmm5, xmm0   ;  vx
0x212c37c0071  vsqrtss xmm7, xmm6, xmm6   ; |v_xy|  (the HORIZONTAL speed)
0x212c37c0095  vdivss xmm1, xmm1, xmm7    ; 1/|v|
```

`verified`. The strafe is a **2D (horizontal)** computation: the z component of
the velocity is multiplied by zero before the length is taken. Our repo's strafe
is also horizontal (`heading_of(vx, vy)` in `Input::strafe_tick`). `same`

### 1.3 The side — no dead zone, no hysteresis, no latch

The decision is three sign tests against zero:

```
0x212c37c0214  vxorps   xmm10, xmm10, xmm10   ; the threshold: 0
0x212c37c037f  vucomiss xmm12, xmm10 ; seta al   ; al = (xmm12 > 0)
0x212c37c0387  vucomiss xmm11, xmm10 ; seta cl   ; cl = (xmm11 > 0)
0x212c37c038f  vucomiss xmm9,  xmm10 ; seta dl   ; dl = (xmm9  > 0)
0x212c37c03c8  xor   cl, dl
0x212c37c03cd  lea   rcx, [rcx*8 + 8]          ; (cl^dl) ? 0x10 : 0x08
0x212c37c03dc  or    qword [r8+8], rcx        ; 0x08 = IN_FORWARD, 0x10 = IN_BACK
0x212c37c03e0  xor   al, dl
0x212c37c03e5  shl   eax, 9
0x212c37c03e8  add   rax, 0x200              ; (al^dl) ? 0x400 : 0x200
0x212c37c03f5  or    qword [rcx+8], rax       ; 0x200 = IN_MOVELEFT, 0x400 = IN_MOVERIGHT
```

`verified`. The structure is:

* `al` = the sign of one velocity-derived component (the projection of the
  normalised velocity on the camera's forward axis, `inferred`),
* `cl` = the sign of a second component,
* `dl` = the sign of a third (the lateral component),
* the forward/back button = `cl XOR dl`, the left/right button = `al XOR dl`.

So the side is a **pure function of the current velocity and the current wish**:
there is **no stored "last side"**, no dead zone, no hysteresis. The only
"memory" is the velocity's own inertia. `verified`

Two magnitude guards sit in front of the write:

```
0x212c37c016e  vmovss  xmm1, [0x212c41976c0]   ; 0.2
0x212c37c0176  vucomiss xmm1, xmm3             ; xmm3 = |lateral|
0x212c37c017a  jae    0x212c37c043c            ; skip the whole strafe if |lateral| <= 0.2
...
0x212c37c01d8  vucomiss xmm0, xmm3
0x212c37c01dc  ja     0x212c37c043c            ; skip if the new lateral exceeds the old
```

`verified`. The `0.2` is a **speed/activity gate**, not a side hysteresis: below
it, *nothing* is written (no camera move, no buttons).

> **Our answer is strictly better here.** Our `plan()` keeps the side of the
> last decisive tick inside a 20° dead zone, so holding `W` alone (the goal
> exactly on the velocity heading) continues the current curve instead of
> flipping. Neverlose's pure-sign rule would flip every tick in that case —
> which is precisely why they add the **±1° nudge** (§1.5).

### 1.4 Magnitude and channel

The **channel is the view angle**:

```
0x212c37c035c  mov   rax, [rsi+0xd30]            ; state
0x212c37c0363  cmp   byte [rax+0x54a], 0          ; the "silent" flag
0x212c37c036a  jne   0x212c37c037f
0x212c37c036c  mov   rax, [rsi+0xd38]            ; cmd
0x212c37c0373  vmovlps [rax+0x20], xmm0          ; cmd->viewangles  (pitch, yaw)
0x212c37c0378  mov   dword [rax+0x28], 0         ; roll = 0
0x212c37c039e  vmovlps [r8+0xa8], xmm0           ; cmd->angles_alt  (always)
0x212c37c03a7  mov   dword [r8+0xb0], 0          ; roll = 0
0x212c37c03b9  and   qword [r8+8], 0xfffffffffffff9e7   ; clear FWD|BACK|MOVELEFT|MOVERIGHT
0x212c37c03c1  and   word  [r8+0x18], 0xf9e7            ; the mirror word
```

`verified`. Note the `state+0x54A` flag: the write to `cmd+0x20` is **skipped
when the flag is set**, while the write to `cmd+0xA8` is **unconditional** —
i.e. the module has two angle channels and a per-tick switch between them
(prior session's `SUBSYSTEM_movement.md` calls `cmd+0xA8` the "silent" one;
which of the two the server consumes is `unknown` from the image alone). That
is the same two-channel split our repo uses for the movement yaw
(`docs/re/subtick.md` §"the silent movement-yaw channel"), applied to the
angles. `same idea`

The **analog** is ±1.0 on both axes:

```
0x212c37c0400  test   byte [rax+8], 8        ; IN_FORWARD?
0x212c37c0404  vmovss xmm0, [0x212c4106abc]  ; -1.0
0x212c37c040c  vmovss xmm1, [0x212c40ec898]  ; +1.0
0x212c37c0418  je     0x212c37c041e          ; IN_FORWARD clear -> -1.0
0x212c37c041a  vmovaps xmm2, xmm1           ; IN_FORWARD set   -> +1.0
0x212c37c041e  vmovss [rax+0x2c], xmm2       ; cmd->forwardmove
0x212c37c042a  test   dword [rax+8], 0x200   ; IN_MOVELEFT?
0x212c37c0431  je     0x212c37c0437          ; IN_MOVELEFT clear -> -1.0
0x212c37c0433  vmovaps xmm0, xmm1           ; IN_MOVELEFT set   -> +1.0
0x212c37c0437  vmovss [rax+0x30], xmm0       ; cmd->leftmove
```

`verified`.

> ### ⚠ Sign-convention discrepancy (flag, not a conclusion)
>
> Neverlose writes **`cmd+0x30 = +1.0` when `IN_MOVELEFT` (0x200, the `A` key)
> is set** — i.e. it treats the `cmd+0x30` axis as **positive = left**. Its own
> basis confirms this: the lateral vector it composes with has world heading
> `yaw + 90°`, which is the **negation** of our repo's `right(yaw) =
> (sin yaw, −cos yaw)` (`strafe_geometry.hpp`, `docs/re/strafing.md` §`AirMove`).
>
> Our repo treats `cmd+0x30` as **positive = right**. One of the two is
> inverted. Our own research already lists this as open
> (`docs/re/research-strafe.md` §9.3 item 7: *"which sign of `leftmove` is
> 'right'?"*).
>
> **A single live check settles it**: read `cmd+0x30` (and `CCSGOInput+0x274`)
> while the player holds `A` and nothing else. If the engine's own value is
> **negative** for `A`, then the engine's axis is positive-right and
> **Neverlose's write is mirrored** (their strafe would curve the wrong way);
> if it is **positive**, our repo's convention is the one that is wrong and
> `strafe_geometry.hpp`'s `goal_for` / `Input::write_strafe_wish` need flipping.
> Either way it is a **one-line** fix, and it is the single most consequential
> thing in this note.
>
> > **Resolved 2026-10-20 — Neverlose is right.** The engine's own code was
> > re-derived on the live build 25947352 (`client.dll` sha `659940c1…`): the
> > view-frame basis function (`client.dll` RVA `0x173cb90`) returns its
> > **second** vector as `(-sin yaw, +cos yaw)` — the forward vector rotated
> > **+90°**, the player's **left** — and the engine's own input forms the
> > lateral axis as `value("left") - value("right")` (RVA `0xb8f6e0`), so
> > `IN_MOVELEFT` writes a **positive** side axis, exactly as Neverlose does.
> > The field is the command's `leftmove` (the movement services' schema calls
> > it `m_flCmdLeftMove`). Full derivation, addresses and the live check:
> > **[`docs/re/strafe-sign.md`](../re/strafe-sign.md)**.
> >
> > Our repo's *goal* heading was never wrong (it had the basis and the sign
> > both mirrored, which cancelled); our repo's **analog write** was not — it
> > wrote the side axis with the old sign. **That write is now fixed**
> > (2026-10-20, T13): it goes through `strafe_geometry.hpp`'s
> > `engine_side_axis_for_wish()` (and the button bits through
> > `engine_move_buttons()`), so the convention lives in one constant,
> > `k_engine_side_axis_positive_is_left`, and the host test pins it. The same
> > pass found the repo's **console button names** wrong — CS2's strafe
> > commands are `+left` / `+right`, not `+moveleft` / `+moveright`
> > (`strafe-sign.md` §4.3).

### 1.5 The one genuinely stealable trick: the ±1° tie-break

```
0x212c37c0219  vucomiss xmm11, xmm10 ; seta al
0x212c37c0235  vucomiss xmm12, xmm10 ; seta cl
0x212c37c023d  xor   cl, al
0x212c37c023f  vmovd  xmm0, eax
0x212c37c0243  vpinsrb xmm0, xmm0, ecx, 4
0x212c37c0249  vpslld xmm0, xmm0, 0x1f        ; -> a per-axis sign mask
0x212c37c024e  vbroadcastss xmm1, [-1.0]
0x212c37c0257  vblendvps xmm13, xmm1, [1.0], xmm0   ; xmm13 = ±1 per axis
...
0x212c37c032b  vaddps xmm1, xmm13, xmm0        ; angle + sign
0x212c37c032f  vsubps xmm0, xmm0, xmm13        ; angle - sign
0x212c37c0334  vblendps xmm0, xmm1, xmm0, 2    ; lane 0 = +, lane 1 = -
```

`verified`. The final angle is **nudged by exactly ±1°** depending on two sign
bits, before the `[-180, 180]` wrap. It is a tie-breaker: when the geometry is
exactly symmetric, the two candidate angles differ by 2°, so the result is
deterministic and never lands exactly on the symmetry axis. `inferred`

Our dead-zone + latch already resolves the tie, so this is **not** worth
adopting as-is — but it is the right idea if the dead zone is ever removed.

The wrap itself is the same trick the engine community uses:

```
0x212c37c033a  vmulps xmm1, xmm0, [0.0027777778]   ; x / 360
0x212c37c0342  vaddps xmm1, xmm1, [0.5]
0x212c37c034a  vroundps xmm1, xmm1, 9              ; round-to-nearest
0x212c37c0350  vmulps xmm1, xmm1, [-360.0]
0x212c37c0358  vaddps xmm0, xmm0, xmm1            ; x + round * (-360)
```

`verified` — byte-for-byte the same formulation as our
`normalize_angle()` (`strafe_geometry.hpp`), except ours uses `fmod` and
Neverlose uses the round-trip. `same`

### 1.6 Read-before-write

The analog axes are read at `0x212c37bff61/66`. The first write is at
`0x212c37c035c`. So the read is ~1 KB of code before the write. `verified`

The *latch landmine* is therefore avoided in the classic sense: Neverlose never
reads its own output back. But note it *does* overwrite the buttons and the
axes, so a second invocation in the same tick would read its own output — it
guards with a configuration byte at `0x212c37bff51-0x212c37bff54`
(`cmp byte [rdi], 1 / jne skip`). `inferred`

### 1.7 The bhop (for completeness)

```
0x212c37c0443  test   byte [rcx+0x18], 2   ; the mirror word's IN_JUMP
0x212c37c0449  test   byte [rcx+8], 2      ; IN_JUMP
0x212c37c044d  je     0x212c37cab88        ; neither held -> nothing
0x212c37c0453  mov    rax, [rcx+0x60]      ; the subtick step array
0x212c37c0460  cmp    qword [rax+8], 2     ; a step with button == 2 already?
0x212c37c0493  mov    dword [rdx], 0       ; when  = 0
0x212c37c0499  mov    qword [rdx+8], 2     ; button = IN_JUMP
0x212c37c04a1  mov    byte [rdx+0x10], 1  ; pressed = 1
0x212c37c04b7  add    qword [rcx+0x68], 0x48
```

`verified`. The `CSubtickMoveStep` layout is `{when dword +0, button qword +8,
pressed byte +0x10}`, stride **0x48** — the same layout and stride our repo
uses (`docs/re/subtick.md`). `same`

### 1.8 The other movement path (mode 2)

Mode 2 is the grounded pathing + "aim view" path. It keeps a per-context
waypoint struct at `ctx+0x38..0x70`, walks it with an `atan2` direction, and
applies a computed view angle back into the command:

```
0x212c37caa86  call 0x212c37cd570     ; rcx = cmd+0x20 (the view angles),
                                       ; rdx = &angle, xmm3 = fov * 0.7 + 0.3
```

`verified` (the call and its `cmd+0x20` argument are at `0x212c37caa86`; the
`fov*0.7+0.3` blend factor is in `closure_movement_v2.c:11587`, i.e. `inferred`
from the decompilation). This is their **aim-view** smoothing
— the same "smooth the view toward the computed angle" idea as
`Input::strafe_tick`'s sub-tick ramp, but applied to the camera.

---

## 2. Camera / third person

**Answer: not found.** Neverlose has **no third-person, camera-distance or
FOV feature** in this module.

What I checked:

* a plaintext scan of the whole image for `third*`, `person`, `cam_`, `camera`,
  `FOV`, `viewoffset`, `eye_`, `m_fl*` finds **no** feature or convar name —
  only the protobuf/particle metadata (`QAlpha`, `PATTACH_WORLDORIGIN`,
  `strSnapshotSubset`, `m_bShouldHitboxesFallbackToRenderBounds`, …). `verified`
* the module's **import table** (391 entries; the whole DLL set is
  `advapi32, combase, CRYPT32, KERNEL32, KERNELBASE, ntdll, OLEAUT32, SHELL32,
  tier0, USER32, WS2_32`) has **no game DLL at all** — the engine is reached
  through absolute addresses baked into the image (e.g. `0x7ffcf04ba160` for
  the schema attribute lookup), so there is no camera interface to call.
  `verified`
* the only *writes* to the camera in the whole module are the two view-angle
  writes in §1.4 (`cmd+0x20` and `cmd+0xA8`) and the mode-2 aim-view write
  (`0x212c37caa86`). `verified`

**Caveat (`inferred`):** the feature list is **not in the binary** — it is
fetched from their server as CJSON (*"BUG: Unable to fetch CJSON
configuration"*, `0x212c424f69f`). So a config-driven camera feature cannot be
ruled out. The honest answer is *not found in the image*, not *does not exist*.

**Ours:** `payload/features/src/thirdperson.cpp` drives the engine's own third
person (`thirdperson` + `cam_idealdist` convars, plus a direct write into the
input object's camera block from the game's own per-frame camera update).

**What we should change: nothing.** Their one camera technique is the *strafe
channel* (§1.4), and adopting that would be a regression for us — see §7.9.

---

## 3. Chams / visuals — the highest-value answer

### 3.1 The headline: there is no engine material, and no material override

* The module's **import table has no `d3d11` and no `materialsystem2`** entry
  (391 imports; the grep for `d3d|material|render|present|swapchain` returns
  only `IsDebuggerPresent`, and the whole DLL set is the Win32/COM/crypto list
  quoted in §2). The third-party libraries it uses (OpenSSL, Crypto++,
  protobuf, websocketpp, asio, LuaJIT) are **statically linked** into the
  image, not imported. `verified`
* The module ships **28 embedded DXBC shaders** in `.rdata` and captures the
  game's D3D11 device at runtime (through a vtable) to run its **own** render
  pipeline. The blobs are in the clear — the string
  *"Microsoft (R) HLSL Shader Compiler 10.1"* occurs 28 times, at
  `0x212c41c9768` … `0x212c4243680`. `verified`
* There is **no** `FindMaterial`, no `IMaterialSystem`, no `CreateMaterial`, no
  `SetMaterialOverride`, no `IMeshRenderContext` / `CSceneRender`, and no
  `*.vmat` material path anywhere in the 83 MB image (the single `.vmat` hit is
  a `.vmat_c` suffix token in the metadata pool). The one occurrence of
  `materialsystem2.dll` (`0x212c424fc8f`) is a **string in the embedded CS2
  metadata pool** — no code references that address, and the module does not
  even import `GetModuleHandle`. `verified` (absence, over the whole image and
  the whole decompilation)

So the spec's four sub-questions have a surprising answer:

1. **How does it get a flat material?** — **It does not.** There is no engine
   material anywhere in the visual stack. What it has instead is its own
   shader pipeline, and the "flat" look is a **flat colour uniform** in its own
   constant buffer.
2. **How does it override the material for a player model?** — **It does not.**
   What it draws instead is the player's **hitbox hull as a world-space convex
   polygon**, in several passes.
3. **How does it get the colour and the alpha?** — From **its own constant
   buffer**: `hull_color` is a `float4` (RGBA) and `global_alpha` a `float`,
   alongside per-pass `shadow_opacity` / `outline_opacity` / `glow_opacity` /
   `gradient_opacity`. In its 2D-primitive path the float colour is converted
   to bytes with `× 255 + 0.5` (`closure_render.c`, `FUN_212c367c4b1`).
4. **What are the Tracer / Impact / Trajectory / Polygon shaders for?**
   Tracer = the **bullet tracer**; Impact = the **bullet-impact marker**;
   Trajectory = the **grenade trajectory prediction**; Polygon = the
   **player/entity hull** (their "chams"), with a glow variant and a batched
   2D-UI primitive renderer beside it. Detail in §3.2.

### 3.2 The five shader families (byte-verified from the DXBC reflection)

The reflection data is plaintext inside the blobs, so the layouts below are
**read out of the file**, not inferred.

**(a) Polygon / hull — 5 blobs**
`0x212c41c9050` (4768 B), `0x212c41ca2f0` (10 696), `0x212c41ccd20` (10 812),
`0x212c41cf760` (6400), `0x212c41d1080` (6992).

* Constant buffer **`PolygonConstants`**:
  `inverse_view_projection` (float4x4), `viewport_size` (float2),
  `point_count` (dword), `group_count` (dword), **`hull_color` (float4)**,
  `smooth_k`, `fade_offset`, **`global_alpha`**, `shadow_opacity`,
  `outline_width_px`, `glow_width_px`, `geometry_type` (dword),
  **`render_pass` (dword)**, `mask_bounds` (float4), `texture_origin` (float2),
  `texture_size` (float2), `gradient_opacity`, `outline_opacity`,
  `opacity_padding`.
* Structured buffer **`hull_records`**, element type **`HullPoint`**:
  **`position_radius` (float4)**, `parent_growth` (float), `appearance` (float).
* Blobs 2 and 3 add a second SRV **`group_records`** (hulls grouped per
  entity); blobs 4 and 5 add **`glow_sampler`** + **`hull_mask`** (and
  `horizontal_glow`) for the glow pass.
* Vertex signature `SV_POSITION, COLOR, TEXCOORD`; pixel output `SV_Target`.

⇒ **This is the chams recipe.** The hull points are **world-space** (the
shader applies `inverse_view_projection`), each carries a **radius**, and the
whole hull is filled with one `hull_color` at one `global_alpha`. The
`render_pass` value selects the pass (fill / shadow / outline / gradient / glow),
and each pass has its own opacity uniform.

**(b) Trajectory — 8 blobs**
`0x212c41d2c30` (2992), `0x212c41d37e0` (2452), `0x212c41d4180` (1664),
`0x212c41d4840` (452), `0x212c41d4a70` (21 748), `0x212c41d9fa0` (2464),
`0x212c41da940` (2116), `0x212c41db190` (4204).

* Constant buffer **`TrajectoryConstants`**: `view_projection` (float4x4),
  `viewport` (float4), **`line_color` (float4)**, **`endpoint_color` (float4)**.
* SRV **`records`** of **`Record`**; the big blob also has **`points`** of
  **`Point { float3 position; float velocity; float time; uint tick;
  bounce_record }`**.
* The vertex shaders take **only `SV_VertexID` / `SV_InstanceID`** — the
  polyline is **generated on the GPU from the structured buffer**; there is no
  vertex buffer.

⇒ This is the **grenade trajectory prediction**: a 3D polyline of simulated
points (position, velocity, time, tick) with **bounce records**, projected in
the shader, with a per-line and per-endpoint colour.

**(c) Impact — 7 blobs**
`0x212c41e2f40` (94 000), `0x212c41f9e70` (724), `0x212c41fa150` (9880),
`0x212c41fc7f0` (3072), `0x212c41fd3f0` (7116), `0x212c41ff040` (23 560),
`0x212c4204c70` (7604).

* Constant buffer **`ImpactConstants`**: `view_projection`,
  `inverse_view_projection`, `viewport`, `point_count`, `radius`, **`colors`**
  (float4), `glow_width_px`, `rounding`, `fill_opacity`, `outline_opacity`,
  `glow_opacity`, `tile_metadata_valid`, `tile_width`, `tile_count`.
* SRV **`projected`** of **`ProjectedImpact { position, alpha, custom_color,
  type, size_scale, padding }`** and SRV **`ImpactPoint { bounds, pixel_size,
  distance, index, duplicate_geometry }`**; a **`tiles`** SRV and the textures
  `color_texture` / `mask_texture` / `glow_texture` (a **sprite atlas**);
  `global_opacity_order` for the draw order.

⇒ This is the **bullet-impact marker**: impacts are projected to the screen and
drawn as round markers with a fill, an outline and a glow, each with its own
**alpha** and an optional **custom colour**, and a `type` that selects the
sprite (the `tiles` atlas).

**(d) Tracer — 2 blobs**
`0x212c4206ae0` (1372, vertex), `0x212c4207060` (3316, pixel).

* VS: CB **`TracerConstants { viewport, start_fade, end_fade }`**, SRV
  **`vertices`** of **`TracerVertex { dword record; float2 position; float
  padding }`**.
* PS: SRV **`records`** of **`TracerRecord { float4 endpoints; uint4
  perspective; float4 color; … shape, glow }`**.

⇒ The **bullet tracer**: a screen-space quad between two projected endpoints,
with a start/end fade and a glow.

**(e) 2D UI / primitives — 6 blobs**
`0x212c42265a0` (1024), `0x212c42269b0` (1728), `0x212c42274a0` (1200),
`0x212c423c840` (2384), `0x212c423d2c0` (17 224), `0x212c4243450` (984).

* `0x212c423d2c0` is a **batched 2D primitive renderer**: the vertex shader
  takes `SV_PrimitiveID` and an SRV **`Primitives`** of
  **`PrimData { prim_type, uv_rule, enable_aa, shadow_type, rect_size,
  texture_effect, texture_effect_param, shadow_color, shadow_offset,
  shadow_size, rect_col_tl/tr/br/bl, circle_color, circle_inside_color, radius,
  rect_thickness, rect_smoothing, circle_a0, circle_a1, circle_gradient,
  circle_thickness, multi_is_horizontal, multi_colors_cnt, multi_colors,
  multi_fracs }`** — per-corner rectangle colours, circles with a gradient,
  drop shadows, multi-stop gradients, and an SDF mode.
* `0x212c423c840` is a **separable blur / glow** (`resolution`, `radius`,
  `offsets`, `weights`).
* `0x212c42265a0` is a full-texture + cutout-texture UI element.

⇒ This is **their menu/overlay renderer**, and it is the most reusable part of
the whole thing (see §7.3).

### 3.3 What we should change

Our chams is a **configuration with no implementation** in the tree
(`esp.hpp` §`visuals.chams`, `ChamsMode {flat, glow, wireframe}`,
`chams_color` RGBA) — the plan is a **material override** on the player model.

Neverlose's hull approach is *not* a drop-in replacement, but three of its
ideas are worth taking, and one of its *non*-ideas is worth confirming:

1. **The pass model** (§7.3): one geometry, several `render_pass` values with
   per-pass opacities. Our `ChamsMode::glow` wants an outline *and* a glow
   around the same silhouette; Neverlose gets both by re-drawing the same hull
   with a different pass — no second geometry, no second silhouette to keep in
   sync.
2. **The per-point radius** (§7.4): `HullPoint.position_radius` lets one
   polygon carry a soft edge, which is how their outline looks round.
3. **The "no engine material" property**: a hull/chams drawn by *our* renderer
   cannot break when Valve changes the material system, and it works in the
   Vulkan fallback path too. Worth keeping as a **second** chams mode.
4. **What to keep:** the material override. It is what a player means by
   "chams" (the model itself changes colour), it is occluded correctly by
   walls, and it does not need the hitbox hull to be accurate. Neverlose's hull
   polygon is a *different feature* (an ESP silhouette), not a replacement.

---

## 4. ESP / visuals generally

### 4.1 Which entity classes they special-case

Neverlose resolves entity classes **by name through the engine's own schema
lookup**, e.g.

```
closure_visuals.c:3991
  iVar1 = VWSH__pH_l_pH_E_____L__A___("C_PlantedC4", *(undefined8 *)(lVar3 + 8 + uVar6 * 0x18));
```

`verified`. The literal class names in the raw decompilation
(`all/funcs_00*.c`) give the set (all `verified` as *strings present in the
schema lookups*; the per-class resolver addresses are in the prior session's
`FEATURES.md`):

| group | classes |
|---|---|
| players | `C_CSPlayerPawn`, `C_CSPlayerPawnBase`, `C_CSObserverPawn`, `CCSPlayerController`, `CBasePlayerController` |
| entities | `C_BaseEntity`, `C_BaseModelEntity`, `C_EconEntity`, `C_EconWearable`, `C_WorldModelGloves`, `C_BaseCombatCharacter` |
| weapons | `C_BasePlayerWeapon`, `C_CSWeaponBase`, `C_CS2WeaponModuleBase` |
| grenades | `C_BaseCSGrenade`, `C_BaseCSGrenadeProjectile`, `C_SmokeGrenadeProjectile`, `C_HEGrenadeProjectile`, `C_MolotovGrenade`, `C_MolotovProjectile`, `C_Inferno` |
| objectives | `C_PlantedC4`, `C_C4`, `C_Multimeter`, `C_WeaponTaser`, `C_Hostage`, `C_HostageCarriableProp` |
| props | `CBaseProp`, `C_BasePropDoor`, `C_Breakable`, `C_DynamicProp`, `C_PhysicsProp`, `C_PhysicsPropMultiplayer` |
| world | `C_World`, `C_EnvSky`, `C_EnvCubemapFog`, `C_GradientFog`, `C_BarnLight`, `C_LightEnvironmentEntity`, `C_SoundEventEntity`, `CMapInfo` |
| viewmodel / preview | `C_CS2HudModelArms`, `C_CS2HudModelWeapon`, `C_CS2HudModelAddon`, `C_CS2HudModelBase`, `C_CSGO_PreviewPlayer`, `C_CSGO_PreviewModel`, `C_CSGO_TeamPreviewCharacterPosition` |
| econ modules | `C_NametagModule`, `C_StattrakModule` |
| special | **`C_Chicken`** |

`verified` (the strings) / `inferred` (the grouping). The class names occur
either as **literal strings in the schema lookups** (the ones I grepped out of
`all/funcs_00*.c`: `C_CSPlayerPawn`, `CCSPlayerController`, `C_BaseEntity`,
`C_BaseModelEntity`, `C_BasePlayerWeapon`, `C_CSWeaponBase`, `C_BaseCSGrenade`,
`C_BaseCSGrenadeProjectile`, `C_MolotovGrenade`, `C_PlantedC4`, `C_Multimeter`,
`C_Hostage`, `C_HostageCarriableProp`, `C_Breakable`, `C_DynamicProp`,
`C_PhysicsProp`, `C_PhysicsPropMultiplayer`, `CBaseProp`, `C_Chicken`,
`C_CS2HudModelWeapon`, `C_EconWearable`, `C_WorldModelGloves`, `C_EnvSky`,
`C_EnvCubemapFog`, `C_GradientFog`, `C_NametagModule`, `C_StattrakModule`) or
in the module's **embedded CS2 metadata pool** (the rest), and the prior
session's `FEATURES.md` maps each of them to its resolver address.

Three things stand out as **features we do not have**:

* **fog** — they resolve `C_EnvSky` / `C_EnvCubemapFog` / `C_GradientFog`, and
  the module's embedded CS2 metadata carries the field names `fogcolor`,
  `fogstart`, `fogend`, `fogmaxopacity`, `fogverticalexponent`,
  `fogfalloffexponent`, `fogstartheight`, `fogendheight`. ⇒ a **fog-removal**
  feature. `inferred` (the field names are verified; the feature is inferred)
* **viewmodel lighting** — the string `EnableViewModelSunlight` is embedded
  (`0x212c424e160`). ⇒ they touch the viewmodel's lighting. `inferred`
* **the chicken** is special-cased as its own class. `verified`

### 4.2 The classifier

`FUN_212c366be70` (`0x212c366be70`, 3500 B) maps an entity to a **small
integer "kind"**:

```
closure_entity_v2.c:1427-1435
  lVar2 = FUN_212c366cc70();                  // the C_PlantedC4 schema handle
  if (entity->class == lVar2) return 0x31;
  lVar2 = FUN_212c366d1c0();                  // the C_Multimeter handle
  if (entity->class == lVar2) return 0x37;
```

and the **inheritance test** walks the schema class chain:

```
closure_entity_v2.c:1504-1505
  if (*(longlong *)(lVar4 + 0x60) == 0) break;
  lVar4 = *(longlong *)(*(longlong *)(lVar4 + 0x60) + 0x18);
```

`verified`. So: *"is this entity an instance of class X (or of something that
derives from X)?"* is answered by walking `node = *(node + 0x60) + 0x18` until
null and comparing at each step. That is cheaper and more general than
comparing a vtable or a class id.

### 4.3 Radar and off-screen

**Not found.** No radar widget, no off-screen/edge-arrow code, no
screen-edge constant was located. Their 2D primitive renderer (§3.2e) could
draw one, and their feature list is server-side, so the honest answer is
*not found in the image*. `not found`

**Ours:** radar (`esp_radar.cpp`), off-screen handling, sound ESP, hitboxes,
skeleton, tracers, world entities.

**What we should change:** nothing structural. The **class-chain walk** (§4.2,
§7.6) is the only transferable idea, and it is only worth it if our snapshot
exposes the concrete class rather than a family.

---

## 5. The protection layer (informational only)

**No evasion is implemented here and nothing whose purpose is to hide a payload
from a third party is ported.** This section exists so the project knows what
it is up against.

1. **Manual map into `cs2.exe`.** The loader stub at `0x212C82F0000`:
   entry `0x212C3300A00` → `FUN_212c82f0a00` → DllMain `FUN_212c82f1690` →
   `FUN_212c82f10b0` / `FUN_212c82f1450` (rebuild the **erased** PE header;
   `SizeOfImage = 0x5000000`) + `FUN_212c82f1000` (a wildcard scan in ntdll) +
   `FUN_212c82f1720` (registers the manual map's unwind info through an
   unexported ntdll routine). `verified` (from the prior session's
   `NEXT_SESSION_PROMPT.md` §6)
2. **Opaque predicates.** Every real block is guarded by
   `mov [slot],r ; cmp [slot],r ; je real` with an inline decoy gap. Two
   fingerprint families: a **PEB** fingerprint (`gs:[0x60]` → `== 0x5877`) and a
   **KUSER_SHARED_DATA** checksum (`0x7ffe0000…` → `== 0x92FB254D`).
   **3 100** such sites in the raw image, **all** forward `je`/`jz` — so the
   deobfuscation is unambiguous: rewrite the branch to an unconditional `jmp`
   to the target, because the skipped gap *is* the decoy (`DEOBF.md` §1).
   `verified` (I re-confirmed the idiom in the raw bytes at
   `0x212c37bec58`: `cmp [rbp+0x270], rdx ; je 0x212c37bece7`, with the decoy
   between.)
3. **Arithmetic constant obfuscation** — `movabs; not/rol/xor/sub` chains
   around the compared constant, plus a `0x13371337` "leet" magic used as a
   chained-list index. `verified`
4. **Junk decoy blocks** — `bswap` / `cmc` / `sti` / `mov sp,si` /
   `bndldx` filler between the guard and the target. `verified` (I had to teach
   my disassembler to skip the MPX/CLDEMOTE filler to sweep the function at
   all.)
5. **An encrypted region at rest** — file offsets `0x29c0000–0x3a40000`
   (`.enc1`), entropy ≈ 8.0, and **no code references its VA range**. The
   loader contains **no decryptor**, so the key is runtime-only ⇒ the ESP
   producer half of the module is not statically recoverable. `verified`
   (from `SUBSYSTEM_visuals.md` §3b) — and this is *why* §3 could not find the
   hull producer.
6. **No import table use** for the graphics stack: the 391 imports are the
   Win32/COM/crypto set only (`advapi32, combase, CRYPT32, KERNEL32,
   KERNELBASE, ntdll, OLEAUT32, SHELL32, tier0, USER32, WS2_32`); **no
   `d3d11`, no `materialsystem2`**, and no game DLL. The D3D11 device is taken
   from the game at runtime through a vtable, and the engine is reached through
   absolute addresses baked into the image. `verified`
7. **A CS2 build-version table** — `{0x3AD6=15062, 0x42EE=17134, 0x47BA=18362,
   0x585D=22621, 0x65F4=26100}` — the module targets several game builds.
   `verified` (from `NEXT_SESSION_PROMPT.md` §6)
8. **The feature list is not in the binary.** It is fetched as CJSON from their
   server (*"BUG: Unable to fetch CJSON configuration"*, `0x212c424f69f`).
   ⇒ **the absence of a feature in the image is not proof the product lacks
   it.** This caveat applies to §2 and §4.3. `verified` (the string) /
   `inferred` (the consequence)

**How it is detected** (static indicators, for our own threat model): the
`mov/cmp/je` guard idiom, the PEB and KUSER fingerprint constants
(`0x5877`, `0x92FB254D`), the high-entropy `.enc1` region, the erased PE
header, the runtime IAT reconstruction, the KUSER checksum family.

**Ours:** `payload/loader/` is already written for this. **Nothing to adopt.**

---

## 6. Answers we could **not** get

| # | Question | What I tried | Status |
|---|---|---|---|
| 1 | The closed form of Neverlose's strafe angle (the exact "optimal" wish) | A recursive-descent disassembly of the whole 44.5 KB function (10 469 instructions recovered, the opaque-predicate decoys identified and skipped) plus Ghidra's deobfuscated output `closure_movement_v2.c`. The block `0x212c37c0100-0x212c37c0358` mixes a 3D cross product with a 2D (horizontal) normalisation and is register-rotated across ~200 SSE instructions | **partial**: the inputs, the outputs, the gates, the button mapping and the magnitude are all verified; the algebra is not |
| 2 | What builds the `hull_records` (the chams/hull producer) | The 128-function render closure (`closure_render.c`) — it contains only the D3D11 device/shader plumbing, not the producer; the ESP-side closure was never decompiled, and the prior session's §7.6 root list was partly wrong (`SUBSYSTEM_visuals.md` §3) | **not found** |
| 3 | Whether Neverlose has a third-person / camera-distance / FOV feature | A plaintext scan of the whole 83 MB image for every camera-ish token; the import table; every write to a camera-ish struct in the decompilation | **not found** (and §5.8 says "not found" ≠ "absent") |
| 4 | Whether it has a radar, and how it handles off-screen targets | The same plaintext scan; the 2D-primitive shader family's parameter list | **not found** |
| 5 | The mapping from the classifier's "kind" codes (`0x31`, `0x37`, …) to features | The classifier returns small integers; the consumer that maps them to draw commands is not in the entity closure | **not found** |
| 6 | The chams question *as the spec framed it* (a flat material + a material override) | The whole image, the import table, the 28 shader blobs' reflection data | **there is nothing to find** — the module does not use engine materials at all |
| 7 | The exact `leftmove` sign convention of the engine (see the ⚠ in §1.4) | Neverlose's code gives one data point; it contradicts our repo. A **live** read of `cmd+0x30` / `CCSGOInput+0x274` while holding `A` settles it | **closed 2026-10-20** — the engine's basis is the **left** vector and its side axis is positive-**left**; Neverlose is right, our goal was right, our analog write was mirrored — **and it is now fixed** (T13). See [`docs/re/strafe-sign.md`](../re/strafe-sign.md) |

---

## 7. Ideas worth stealing

| # | Idea | From | Effort | Safe to adopt? |
|---|---|---|---|---|
| 7.1 | **Read the analog axes, not the button mask.** `cmd+0x2C` / `cmd+0x30` are the engine's own W/A/S/D axes — one read instead of four bit tests, and the same values the engine uses for the wish. Our `intent_from_buttons` reconstructs them from bits. | `0x212c37bff61/66` | 30 min | **Yes.** Pure read, no detection surface. Benefit is small (the axes are quantised to {-1,0,1} by `sv_quantize_movement_input`), but it removes a translation layer |
| 7.2 | **The ±1° tie-break nudge** on a computed strafe angle | `0x212c37c023f-0x212c37c0257`, `0x212c37c032b-0x212c37c0334` | 20 min | **Yes, but not needed** — our dead zone + latch already resolves the tie. Keep it in the back pocket for a future no-hysteresis mode |
| 7.3 | **One geometry, many passes.** A single silhouette re-drawn with a `render_pass` selector and per-pass opacities (fill / shadow / outline / gradient / glow) instead of one geometry per visual style | `PolygonConstants` (`0x212c41c9050`'s reflection) | 2–4 h (we have a draw list) | **Yes.** Directly serves `ChamsMode::glow` and the ESP's outline+glow |
| 7.4 | **A per-point radius in the hull record**, so one polygon carries a soft edge | `HullPoint.position_radius` | 1 h | **Yes.** We already have the hitbox hull in world space (`esp_hitboxes.cpp`) |
| 7.5 | **Resolve entity classes by *name* through the engine's schema lookup** instead of a hard-coded class-id table | `VWSH…("C_PlantedC4", class)` (`closure_visuals.c:3991`) | 1 h | **Already done** — `payload/sdk/` generates the schemas from the engine. Do not re-do |
| 7.6 | **Walk the schema class chain to test inheritance** (`node = *(node+0x60) + 0x18` until null) | `closure_entity_v2.c:1504` | 1–2 h | **Yes**, if our snapshot only exposes the concrete class. Low benefit today (we already special-case by class id) |
| 7.7 | **Generate the trajectory / tracer geometry on the GPU from a structured buffer** (the vertex shader takes only `SV_VertexID`/`SV_InstanceID`) | the Trajectory and Tracer blobs | high | **Not now** — our renderer is Vulkan/ImGui-shaped, not a D3D11 pipeline. Worth revisiting only if the in-game renderer grows |
| 7.8 | **A batched 2D primitive renderer** (per-corner rectangle colours, circles with a gradient, drop shadows, multi-stop gradients, SDF mode) in one draw call | `PrimData` (`0x212c423d2c0`) | 1–2 days | **Yes, conceptually** — it is the shape of a good overlay renderer. But we already have ImGui; the win is only for the in-game (non-overlay) path |
| 7.9 | ❌ **Their strafe channel: rotate the camera.** | `0x212c37c035c` | — | **Do not adopt.** It couples the strafe to the aim, and a per-tick camera swing is itself an observable. Our silent wish-vector design (`payload/control/`) is the better one |
| 7.10 | ❌ **The whole protection stack** (§5) | — | — | **Do not adopt.** Out of scope by instruction, and its entire purpose is to hide a payload from a third party |

---

## 8. Evidence index

**Ground truth**

* raw live image: `re/cs2_212C3300000/cs2_212C3300000.bin`
  (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
  base `0x212C3300000`, `VA = base + file_offset`)
* reconstructed PE: `neverlose_seeded3.exe`
  (sha256 `21efa969cacf8e8f5c36066a0ac9de0bd11ae3184bcc1a562892839e31e16832`)
* import table: `imports.txt` (391 entries)

**Corpora read**

* `all/funcs_00{0..6}.c` (47 MB, raw Ghidra) + `all/index.tsv` (46 184 functions)
* `ghout_deobf/closure_movement_v2.c`, `closure_entity_v2.c`,
  `closure_render.c`, `closure_visuals.c` (project **gproj11**, the corrected
  deobfuscation)
* the prior session's notes: `SUBSYSTEM_movement.md`, `SUBSYSTEM_visuals.md`,
  `SUBSYSTEM_entity.md`, `DEOBF.md`, `FEATURES.md`, `NEXT_SESSION_PROMPT.md`
  (pulled from the Linux box at `/var/tmp/rea-work/`)

**Address index for this note**

| address | what |
|---|---|
| `0x212c37beb50` | the master movement / command builder (44 518 B) |
| `0x212c37bff61` / `0x212c37bff66` | the wish axes are read (`cmd+0x2C`, `cmd+0x30`) |
| `0x212c37bff73-0x212c37bffb5` | the camera basis (3 × `SinCos`) |
| `0x212c37c0027-0x212c37c00a5` | the velocity, normalised horizontally |
| `0x212c37c010c-0x212c37c0137` | `wish = forwardmove·F + leftmove·R` |
| `0x212c37c016e-0x212c37c017a` | the `0.2` activity gate |
| `0x212c37c01d8-0x212c37c01dc` | the second (lateral growth) guard |
| `0x212c37c023f-0x212c37c0257` | the ±1° sign mask |
| `0x212c37c033a-0x212c37c0358` | the `[-180, 180]` wrap |
| `0x212c37c035c-0x212c37c03a7` | the view-angle writes (`cmd+0x20`, `cmd+0xA8`) |
| `0x212c37c03b9-0x212c37c03f5` | the button clear + diagonal set |
| `0x212c37c0404-0x212c37c0437` | the ±1.0 analog |
| `0x212c37c0443-0x212c37c04c1` | the bhop subtick step |
| `0x212c37caa86` | the mode-2 aim-view write (`fov*0.7+0.3`) |
| `0x212c366be70` | the entity classifier (kind codes + class-chain walk) |
| `0x212c366cc70` | the `C_PlantedC4` schema resolver (the name-based lookup) |
| `0x212c41c9050 … 0x212c4243450` | the 28 embedded DXBC blobs |
| `0x212c4106abc` / `0x212c40ec898` | the `-1.0` / `+1.0` constants |
| `0x212c41976c0` | the `0.2` gate constant |
| `0x212c424f69f` | *"BUG: Unable to fetch CJSON configuration"* |

**Licence / use note.** Neverlose is a closed-source third-party product with
no licence grant: **read for understanding, describe, do not copy**. No
Neverlose code, structure or constant table is reproduced in this repository;
this note is a description with addresses so the claims can be re-checked.
