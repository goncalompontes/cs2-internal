# The eye-angles / camera-angles split in the wild

Web research, 2026-10-21. **Scope: technique only.** Nothing here is about
anti-cheat evasion, detection avoidance, VAC or stream-proofing; the only
question asked of every source is *how the fake angles reach the server while
the camera keeps the real angles, and what breaks*.

> **Confidence labels.** `verified` = the page or the source file was read
> (quoted or paraphrased from the text); `snippet-only` = only the search
> result / forum metadata was reachable (the site blocked the fetch) — the
> quote is the snippet, not a re-read; `inference` = our reading, marked as
> such with the reasoning.
>
> **Evidence limits.** The UnknownCheats forum served ~8 pages in full early in
> the session and then returned HTTP 403 for every further request from this
> host (the same block hit `r.jina.ai` and `steamdb.info`), so most UC rows are
> `snippet-only`. Reddit is JS-gated (403). `counter-strike.net` is a React
> app, so the official patch notes were read through the **Steam Web API**
> (`ISteamNews/GetNewsForApp/v2/?appid=730`), which returns Valve's own update
> bodies verbatim. No third-party binary was downloaded or executed; the public
> source trees were read as text over HTTPS.

---

## 0. Sources

| id | source | kind | confidence |
|---|---|---|---|
| U1 | UC 767333 "[Help] CS2 desync keeps local viewmodel spazzing (cmd viewangles fake, camera pinned real)", 14 Aug 2026 | forum thread | `verified` (read in full) |
| U2 | UC 758612 "[Release] Animation system reversal" | forum thread | `verified` (read in full) |
| U3 | UC 769450 "[Help] antiaim movement / subticks", 28 Aug 2026 | forum thread | `verified` (read in full) |
| U4 | UC 699114 "[Help] desync aa", 2 May 2025 | forum thread | `verified` (read in full) |
| U5 | UC 727475 "[Help] Setting subtick angles?", 29 Nov 2025 | forum thread | `verified` (read in full) |
| U6 | UC 739851 "[Help] antiaim internal cs2", 23 Feb 2026 | forum thread | `verified` (read in full) |
| U7 | UC 418150 "[Question] Why do viewangles and fake angles differ when using desync?", 25 Sep 2020 (CS:GO) | forum thread | `verified` (read in full) |
| U8 | UC 645873 "[Tutorial] How to make antiaim" | forum thread | `snippet-only` |
| U9 | UC 771744 "[Help] Antiaim" | forum thread | `snippet-only` |
| U10 | UC 739930 "[Question] Internal silent aim — need help finding the right sub-function to hook (Feb 2026)" | forum thread | `snippet-only` |
| U11 | UC 772223 "[Help] how can i make silentaim/silentaimbot" | forum thread | `snippet-only` |
| U12 | UC 773170 "[Help] Issues with Anti-Aim and Subtick Strafer" | forum thread | `snippet-only` |
| U13 | UC 203501 "[Question] Anti Aim Camera" (CS:GO) | forum thread | `snippet-only` |
| U14 | UC 424542 "[Help] Camera shaking antiaim" (CS:GO) | forum thread | `snippet-only` |
| U15 | UC 632998 "[Help] AntiAim movement fix" | forum thread | `snippet-only` |
| U16 | UC 751102 "[Help] Anti Aim movement correction" | forum thread | `snippet-only` |
| U17 | UC 649799 "[Help] Cannot move camera while anti aim is on" | forum thread | `snippet-only` |
| U18 | UC 673511 "[Release] Easy CS2 Antiaim Jitter" | forum thread | `snippet-only` |
| U19 | UC 718536 "[Question] sv_subtick_movement_view_angles" | forum thread | `snippet-only` (title only) |
| U20 | UC 766410 "[Discuss] Is Resolver is a 'thing' in CS2?" | forum thread | `snippet-only` |
| U21 | UC 247182 "[Information] CSGO Fake angles demystified" (CS:GO) | forum thread | `snippet-only` |
| U22 | UC 459969 "[Source] '2 line animationfix' for local" (CS:GO) | forum thread | `snippet-only` |
| U23 | `csgohacks/master-guide`, `introduction/cheat-features.md` (CS:GO background) | public doc | `verified` (read in full) |
| P1 | `T1GxR/Axion-CS2-RAGE-CHEAT` (**MIT**), `features/antiaim/antiaim.cpp`, `features/misc/movement.cpp`, `sdk/datatypes/usercmd.h`, `sdk/interfaces/ccsgoinput.h` | source tree | `verified` (read) |
| P2 | `cacamelio/weave-v3-csgo-and-cs2` — our note [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) §9–§10 | source tree | `verified` (our note) |
| P3 | `cacamelio/fatality-source-fixed` — our note [`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) §8 | source tree | `verified` (our note) |
| P4 | Gamesense CS:GO module — our note [`gamesense-csgo-re.md`](gamesense-csgo-re.md) §1.1 | binary reversal | `verified` (our note) |
| P5 | Neverlose CS2 module — our note [`neverlose-mining.md`](neverlose-mining.md) | binary reversal | `verified` (our note) |
| V1 | `SteamDatabase/GameTracking-CS2`, `Protobufs/cs_usercmd.proto` | proto (Valve) | `verified` (read in full) |
| V2 | `SteamDatabase/GameTracking-CS2`, `Protobufs/usercmd.proto` | proto (Valve) | `verified` (read in full) |
| L1 | CS2 patch notes **2025-09-26** (Steam Web API, appid 730) | Valve | `verified` |
| L2 | CS2 patch notes **2025-07-30** | Valve | `verified` |
| L3 | CS2 patch notes **2024-11-20** | Valve | `verified` |
| L4 | CS2 patch notes **2023-06-06** | Valve | `verified` |
| L5 | CS2 patch notes **2025-07-28** → **2026-04-20** (AnimGraph 2) | Valve | `verified` |
| L6 | CS2 patch notes **2026-08-24** / **2026-09-09** (`cs_player_camera` → `CustomPlayerCamera`) | Valve | `verified` |
| L7 | CS2 patch notes **2025-08-26** | Valve | `verified` |
| L8 | `cscommands.com/command/sv_subtick_movement_view_angles` | convar reference | `verified` |
| N1 | Neverlose forum, "Everything YOU need yo know about your hvh cheat", Jan 2021 | product doc | `verified` (read in full) |
| N2 | Neverlose forum, "ANTIAIM IN CS 2", 7 Sep 2023 | forum thread | `verified` (read in full) |
| N3 | Neverlose on TikTok, "Does desync exist in CS2? Not really. And we will explain why", 8 Aug 2025 | product video | `snippet-only` (caption) |
| N4 | Neverlose on TikTok, "DESYNC IN CS GAMES EXPLAINED", 28 Jul 2025 | product video | `snippet-only` (caption) |
| R1 | r/GlobalOffensive, "Counter-Strike 2 Update for 07/29/2026" (comment) | forum comment | `snippet-only` |
| R2 | r/counterstrike2, "Totally normal. Fake angles are back." | forum thread | `snippet-only` |
| R3 | r/cs2, "There is a new hack called 'anti aim'…" (May 2025) | forum thread | `snippet-only` |
| D1 | `docs/re/camera-split.md` — exists only on the branch `feat/t18b-neverlose-camera-split` (read with `git show feat/t18b-neverlose-camera-split:docs/re/camera-split.md`) | our RE | `verified` |
| D2 | [`../re/subtick.md`](../re/subtick.md) | our RE | `verified` |
| D3 | [`../re/strafing.md`](../re/strafing.md) | our RE | `verified` |
| D4 | [`../re/research-strafe.md`](../re/research-strafe.md) §3 | our RE | `verified` |

---

## 1. TL;DR

**The mechanism, in ten lines.**

1. CS:GO's split was a **protocol** feature: the cheat wrote `cmd->viewangles`
   and the server got them, while the local view stayed on the mouse because
   the cheat returns **false** from `CreateMove` and the engine then does not
   apply the command's angles to the local view — "Set usercmd->viewangles in
   CreateMove. These angles will control your player without changing your
   view angles as long as you return false in …" (U14). The *real/fake* pair was
   therefore free, and the server derived the **body** yaw from the view yaw
   plus the animation state, which is what produced the 58° clamp, the LBY
   breaker and the "goal feet yaw" (U7, U23).
2. **CS2 has no such pair in the protocol.** The client cannot make the server
   see an angle the client did not itself render; the camera is not a network
   object, it is derived from the local view angles. The CS2 split is
   therefore **render-side**: put the fake angles in the *command*, pin the
   camera back to the real angles on the render path.
3. The community's camera pin has two shapes: (a) a **validate-input swap** on
   `CCSGOInput` vtable slot 8 — save the angles, set the real ones, call the
   original, restore the fake ones (U6, U8); (b) a **view-setup pin** in the
   view override — write the real angles back into the view setup, into
   `dwViewAngles` and into `m_angEyeAngles` right before the draw (U1).
4. Shape (b) has a published, reproducible failure: the first-person viewmodel
   and the player model keep the **cached world transform of the fake pose**,
   so the hands "spazz" even though every angle reads back as real — "looks
   like the render uses cached world transform from the fake pose instead of
   scene rotation" (U1).
5. The **movement** must be re-based, always: the CS2 wish direction is built
   from the *command's* view angles, so a fake pitch of 89° "dumps IN_FORWARD
   into the ground" and "you move relative to fake angles (looking down) not
   real angles" (U3, U6, U13). Every implementation we found rewrites the
   analog axes into the anti-aim frame.
6. The **sub-tick input history** is the only channel that is silent with no
   render-side work at all — but it is the channel the **shot** is taken from
   ("where weapon traces, seed calculation, and lag compensation actually pull
   from", U11), so it is the *silent-aim* seam, not the anti-aim's seam.
7. The **movement**'s own silent angle channel is `CSubtickMoveStep.yaw_delta`
   — the strafe's, not the anti-aim's ("yaw-delta subticks are for splitting
   mouse yaw across the tick (airstrafe etc) … neither is the AA walk fix",
   U3).
8. Valve **gated** the silent channel twice: the sub-tick view angles are now
   only sent to the server *together with other sub-tick events* (L1,
   2025-09-26), and the convar that turns the movement's use of them on/off
   is `sv_subtick_movement_view_angles` (L8, D3).
9. The CS2 *desync* in the CS:GO sense (hitbox away from the model) is, by
   the community's own verdict, dead: "and yes, desync is indeed patched"
   (U4, May 2025); "there will be no desync" (N2, Sep 2023); "Does desync
   exist in CS2? Not really." (N3, Aug 2025).
10. But the *input-history* channel is reported as exploited again in 2026 —
    "cheats where spoofing subtick input history with fake view angles.
    Someone released it on github a few weeks ago but the repo is deleted now"
    (R1) — i.e. the 2026 "fake angles" are the sub-tick history angles, not
    the CS:GO feet yaw.

**The three things the community established that we did not know.**

1. **The anti-aim writes the *command*, never the input.** Weave's CS2
   anti-aim writes `cmd->csgo_user_cmd.base->view->angles` and does **not**
   call `CCSGOInput::SetViewAngle`, so "the anti-aim is server-visible but the
   local camera does not follow it" (P2 §10). The camera/eye split we built
   for the auto-strafe (D1) is therefore *already* the right shape for an
   anti-aim: the fake goes into the command, the camera is untouched.
2. **The camera pin has a named, hookable seam: the validate-input swap.**
   The community restores the real angles *around* `CCSGOInput`'s validate
   call (vtable slot 8), not by fighting the render: "original =
   pInput->GetViewAngles(); … pInput->SetViewAngle(actual_angles); … o(pInput,
   unk); pInput->SetViewAngle(original);" (U8, U6). Our T18b RE found the
   same two functions by another name — the setter `FUN_180b80e70` and the
   per-tick copy `FUN_180b07590` (D1) — so the community's seam and ours are
   the same object.
3. **The movement re-base is a full 3D basis remap, and the pitch is the
   hard case.** Axion's `movment_fix` (P1, MIT) rebuilds forward/left/up from
   the *wish* basis into the *command* basis with
   `revers = wish_angle.x > 89.f ? -1 : 1` — the down-pitch inversion — and
   clamps to ±1; and the community's own correction is to "take the WASD
   intent from the camera angles, flatten Z, rebuild forward/left in the AA
   angle basis, write that back to analog" (U3). Our repo's analog is in
   **unit** (±1) values, so that clamp is not cosmetic.

---

## 2. The angle channels

The community's words, in the left column, mapped onto our fields. "Server?"
answers: does the **server** consume this value as the authoritative angle for
the local player.

| # | community word | our field | who reads it | written when | source |
|---|---|---|---|---|---|
| 1 | **"viewangles"** / "the base command viewangles" | `CBaseUserCmdPB::viewangles` (proto field 4, `CMsgQAngle*`; `base + 0x40` on the build we reversed) | **both** — the server takes the base yaw/pitch from it; the movement takes the wish basis from it | per tick, by the engine (`create_move_input`); overwritten by the cheat in the command hook | V2, U11, D2 |
| 2 | **"subtick angles"**, "input history", **"silent angles"** | `CSGOUserCmdPB::input_history[i].view_angles` (proto field 2 of the entry; `entry + 0x18`) | **server** — the shot direction, the trace/seed/lag-comp source; **not** the camera | per tick, per sub-tick sample, by the engine; overwritten by the cheat for silent aim | V1, U5, U11, D2 |
| 3 | **"real angles"**, "the view angles", `dwViewAngles` | `CCSGOInput + 0x688` (the per-slot view-angle block); the global `dwViewAngles` is reported at `client.dll + 0x2314F98` | **client only** — the camera's source | by the mouse, then copied into the command and into `movement_services + 0x240` by the post-think | D1, U1, U6 |
| 4 | **"eye angles"** | `C_CSPlayerPawn::m_angEyeAngles` (`pawn + 0x3600` on our build; `pawn + 0x3320` in U2's build) | **both** — the animation and the hitbox aim; for the *local* pawn it is a **copy** of the camera angles | by the engine's eye-angle copy, downstream of the camera | D1, U2, U1 |
| 5 | **"yaw_delta"** / "yaw-delta subtick" | `CSubtickMoveStep.yaw_delta` (proto field 9; `+0x34`) | **server**, movement only — the movement's basis rotation | per tick, per step, by the cheat (the strafe) | V2, U3, D3 |
| 6 | **"the move axes"**, "analog" | `CBaseUserCmdPB::forwardmove/leftmove/upmove` (proto fields 5/6/7) and the raw axes `CCSGOInput + 0x270/0x274` | **server**, movement only | per tick; the slot-5 builder re-derives them from the raw axes | V2, D2, D3 |

**The same table, in our own seam vocabulary** (D4 §3, our RE): the three
candidate write seams are (a) the **analog move** (silent, does not touch any
angle), (b) the **command's own `viewangles`** (steers the movement, and the
pawn's `m_angEyeAngles` follows ⇒ *visible*), (c) the **`yaw_delta` sub-tick
step** (silent, steers the movement, eye angles unchanged). The anti-aim
needs (b) for the server, (a)/(c) for the movement, and must undo (b)'s
visibility on the render path.

**What the server actually uses, per the community:**

- "In CS:GO, you just wrote to `cmd->viewangles` and fixed your movement/angles
  on return. In CS2, the engine splits angles into multiple protobuf surfaces:
  the base command viewangles (which dictate player movement direction and
  your base yaw/pitch) and the subtick `input_history` array (where weapon
  traces, seed calculation, and lag compensation actually pull from)."
  — U11 (`snippet-only`)
- "a shot reads the entry at `attack1_start_history_index`" → the silent-aim
  write must cover **all** the entries, not just the newest — D2 (our RE),
  and the field is proto `CSGOUserCmdPB.attack1_start_history_index = 6`
  (V1).

---

## 3. How the shipped products split the camera from the fake angles

### 3.1 Weave (CS2) — the reference shape

`feature/antiaim/antiaim.cpp` (P2 §10):

- output: `cmd->csgo_user_cmd.base->view->angles` + `BASE_BITS_VIEWANGLES`;
  "the **input history is not touched**, and `CCSGOInput::SetViewAngle` is
  **not** called — so the anti-aim is server-visible but the local camera does
  not follow it".
- "**There is no desync model**: no real/fake yaw pair, no `fl_flicker`, no
  `fixedyaw`, no per-tick side selection, and **no fake-lag or choking at all**".
- gates: not ladder/noclip, not frozen, not in the buy zone, not `IN_USE`, not
  mid-grenade-throw, and **not while the ragebot is firing**.
- movement: "movement is re-derived from the **pre-anti-aim** yaw
  (`store_angle` captured before the anti-aim runs)" (P2 §9), and "after the
  anti-aim changes the movement, the command is re-serialised and `move_crc`
  recomputed, or the server rejects the modified forward/sidemove".

> **This is the shape we want.** The camera does not follow the anti-aim
> because the camera is not the command. Our T18b split (D1) is already this
> shape for the strafe; the anti-aim needs the same "the fake lives in the
> command" invariant, plus the movement re-base and the CRC.

### 3.2 Axion (CS2, **MIT**) — the movement fix, in full

`features/antiaim/antiaim.cpp:65-70` (P1):

```cpp
pCmd->m_csgoUserCmd.m_pBaseCmd->m_pViewangles->m_angValue.x = flPitch;
pCmd->m_csgoUserCmd.m_pBaseCmd->m_pViewangles->m_angValue.y += flYaw;
```

— the command only, again; and no jitter/desync model.

`features/misc/movement.cpp:78-134`, `movment_fix()` (P1) — the **3D basis
re-projection** of the analog move:

| step | code | why |
|---|---|---|
| the wish (camera) basis | `AngleQangles(wish_angle, &view_fwd, &view_right, &view_up)` | the player's intent is camera-relative |
| the command (anti-aim) basis | `AngleQangles(viewangles, &cmd_fwd, &cmd_right, &cmd_up)` | the movement will be interpreted in the AA frame |
| flatten + normalise | `norm_view_fwd`, `norm_cmd_fwd` are built with `z = 0` (and the up vector reduced to `z`) | the XY plane is what the walk uses |
| project | `fwd = dot(cmd_fwd, intent)`, `side = dot(cmd_right, intent)` | the intent expressed in the AA frame |
| the pitch inversion | `int revers = wish_angle.x > 89.f ? -1 : 1;` and `fwd = revers * …` | **a down-pitch of 89° flips the forward axis**; without this the player walks backwards |
| clamp | `std::clamp(..., -1.f, 1.f)` | the analog is in **unit** values on our build too (D2: the engine writes ±1, and `SetupMove`'s sanitizer drops anything `|x| ≥ 10`) |

`sdk/datatypes/usercmd.h:225-238` (P1) — `SetSubTickAngle()`: the *same* angle
into **every** `input_history` entry's `m_pViewCmd->m_angValue`. That is the
silent channel; Axion uses it for the silent aim, **not** for the anti-aim.

`sdk/interfaces/ccsgoinput.h:58-77` (P1) — the two seams the community hooks:

| seam | signature | note |
|---|---|---|
| `GetViewAngle()` | `4C 8B C1 85 D2 74 08` | returns the input's view angles |
| `SetViewAngle(QAngle&)` | `85 D2 75 3F 48` | writes the input's view angles |

U10 reports the same `SetViewAngles` pattern (`85 D2 75 3F 48 63 CA`) working
in Feb 2026 on "the latest CS2 build", and says the hook is also the place
where "a1, a1, a1 are floats related to x,y,z for your viewangles
`CSGOInputHistoryEntryPB` you can modify … or get history input from
`cusercmd` and modify the angles inside this function" (`snippet-only`).

### 3.3 Fatality (CS:GO) — the CS1 model, and why it does not transfer

`features/antiaim.cpp` (P3 §8):

- the desync is the **body/feet** yaw, not the view yaw: "**body yaw =
  min_max · yaw_modifier · |delta|**" with `yaw_modifier` from a
  movement-speed model (`animations.cpp:3-20`);
- the delivery is a **send-time rewrite of already-created choked commands**:
  it "runs **only when the packet is sent** … rewrites the view angles of
  **already-created choked commands** in the input buffer, using the classic
  **'compensate angle'** — biasing the *view* yaw so that the server-derived
  *body* yaw lands exactly on the intended desync";
- it also re-projects the movement onto the new view basis and **patches the
  command CRC**.

P3 §8 lists what does **not** transfer to CS2, and the list is our gate:
the whole desync model (CS2 has no `CCSGOPlayerAnimState`), the compensate
angle, roll/lean (`CS2 has no roll angle`), the delivery mechanism, and
`sendpacket`/`chokedcommands`.

### 3.4 Gamesense (CS:GO) — the product's *vocabulary*

The config registry (P4 §1.1, 38 `Antiaim.*` vars) is the industry vocabulary
we should name our knobs after:

| var | meaning |
|---|---|
| `Antiaim.desync` | the fake-angle pair on/off |
| `Antiaim.desync_range`, `Antiaim.inverted_desync_range`, `Antiaim.desync_spin_range` | the size of the fake |
| `Antiaim.antiaim_type` | the yaw mode |
| `Antiaim.fake_lag`, `fake_lag_limit`, `fake_lag_type` | the choke |
| `Antiaim.lby_type`, `Antiaim.legit_lby_type` | the lower-body-yaw policy |
| `Antiaim.freestand` | which side to hide the real angle on |
| `Antiaim.base_angle`, `Antiaim.yaw_jit`, `Antiaim.spin_speed` | base yaw, jitter, spin |
| `Antiaim.at_targets` | the yaw target selection |
| `Antiaim.body_lean` | the roll/lean |

### 3.5 Neverlose — the shipped surface and the CS2 verdict

The product's anti-aim surface (N1, Jan 2021 — **CS:GO era**):

> "Fake Angles / Air mode / LBY mode (Lowerbody Yaw) / **Desync on shot** —
> Desyncs animations sent to the server on shot. / **Hide real angle
> (Freestanding)** / **Avoid overlap** — Avoid overlapping real and fake
> angles. / **Fake desync** — Standing anti-aim. / **Inverter** (key) — Flips
> the fake angle to the opposite direction."

Note the vocabulary: *real* vs *fake*, *avoid overlap*, *hide real*, *desync
on shot* — the same three nouns the anti-aim spec needs ("the fake/desync
yaw, the pitch, the jitter").

Its CS2 verdict (N2, Neverlose forum, 7 Sep 2023):

> "there will be no desync. at this time aimware's spin antiaim is the meta."

and (N3, Neverlose's own channel, 8 Aug 2025): "Does desync exist in CS2? Not
really. And we will explain why". Both are the **vendor's community**, not a
reversal: `snippet-only` for N3, a community member's post for N2. Neverlose's
earlier explainer (N4, 28 Jul 2025, "DESYNC IN CS GAMES EXPLAINED") covers the
CS:GO/CS CSS model, not CS2.

The community's own verdict on CS2 anti-aims (R3, r/cs2, May 2025): "The
modern 'anti aim' can't do any sort of desyncing or anything special at all.
Just moves yaw and pitch around and that's it really." — which matches what
the public trees do (P1, P2) and what the product surface shows (N1: the
CS:GO-era knobs, kept as names).

The Neverlose **CS2** module (P5) has no anti-aim in the reversal at all, and
its one angle technique is the wrong one for us: the strafe **rotates the
camera** by writing `cmd->viewangles` ("the channel is the camera"), which our
note marks "Do not adopt — it couples the strafe to the aim, and a per-tick
camera swing is itself an observable" (P5, §7.9).

### 3.6 The community's internal cheats — the two camera pins

**(a) The validate-input swap** (U8, `snippet-only`; the same shape in U6,
`verified`):

```cpp
original = pInput->GetViewAngles();                       // the fake AA angles
pInput->SetViewAngle(F::RAGEBOT::ANTIAIM::actual_angles); // the real angles
o(pInput, unk);                                           // the engine validates / reads the camera
pInput->SetViewAngle(original);                            // back to the fake AA angles
```

U6 spells the same idea out with its own variable names: "viewAngles =
actual_angles; // Real angles → camera free; oValidateInput(pThis, unk);
viewAngles = original; // Fake AA (pitch 89)". U6's poster also reports that
the `ValidateInput` hook "is hooked successfully but **NEVER CALLED by CS2
during gameplay**" in his build — which is exactly why the second shape
exists. (U6's thread is AI-assisted slop; the *pattern* is what matters and
U8 carries it independently.)

**(b) The view-setup pin** (U1, `verified`, read in full):

```cpp
vec3 get_real_view() { return *(vec3*)(g_input + 0x688); }

void on_create_move(CCSGOUserCmdPB* cmd) {
    auto base = (CBaseUserCmdPB*)cmd;
    auto va   = (QAngle*)base->m_viewangles;          // the fake goes here
    vec3 real = get_real_view();
    va->x = 89.0f;  va->y = real.y - 180.0f;
    *(vec3*)g_dwViewAngles                  = real;
    *(vec3*)(g_local_pawn + m_angEyeAngles) = real;
}

void on_override_view(void* view_setup) {
    vec3 real = get_real_view();
    *(vec3*)((uintptr_t)view_setup + 0x4B8) = real;    // the view angles
    *(vec3*)((uintptr_t)view_setup + 0x4AC) = real;    // the view origin/angles pair
    *(vec3*)((uintptr_t)view_setup + 0x0C)  = real;
    *(vec3*)g_dwViewAngles                  = real;
    *(vec3*)(g_local_pawn + m_angEyeAngles) = real;
}
```

Three things to take from it, and one to reject:

- `g_input + 0x688` is **our** camera field (`CCSGOInput + 0x688`, D1) — the
  community independently uses the same address for "the real view".
- the fake goes into `base->m_viewangles` — the command, not the input.
- the reply (U1, `verified`): "its not desync and remove
  `*(vec3*)(g_local_pawn + m_angEyeAngles) = real;`" — **writing
  `m_angEyeAngles` is not the way to hold the camera**; the local pawn's eye
  angles are a *copy* of the camera angles and writing them fights the
  engine (D1: `FUN_1808cb9b0` stores `EyeAngles()` into `m_angEyeAngles`
  downstream of the camera).
- the reported failure of (b): "the hands are spazzing/player model which is
  annoying, and it [removed]s up my movement a bit so it doesnt really support
  the bhop or whatever … looks like the render uses cached world transform from
  the fake pose instead of scene rotation".

---

## 4. The CS2 gates and the failure modes

### 4.1 The gates (what can stop the fake from reaching the server, or the camera from following)

| # | gate | what it actually does | how you observe it | source |
|---|---|---|---|---|
| G1 | `sv_subtick_movement_view_angles` | "Whether or not subtick view angles are taken into account during movement." (the reference page lists `Requires sv_cheats false`, `Client true`, `Server true`) | our own measurement: with it **off**, the silent yaw rotation steers nothing; with it **on**, it accelerates the player (195.8 vs 100.2 u/s over a 3 s window) | L8, U19, D3 |
| G2 | the 2025-09-26 change | "sv_subtick_movement_view_angles will now only send subtick view angles to the server **with other subtick events** instead of sending them for every frame they change" | a silent write that used to be honoured every frame now needs a co-travelling sub-tick event | L1 |
| G3 | the 2025-07-30 fix | "Fixed a case where movement button changes were ignored while adjusting view angles at very high frame rates" | a movement/angle combination that used to be dropped is now applied | L2 |
| G4 | the sub-tick array admission gate | the whole `subtick_moves` array is discarded if `count > 32`, `when` decreases or exceeds 1.0, `popcount(button) != 1`, or the **view-delta accumulator exceeds 2.0** | the rotation silently stops working; our `strafe_ignored` counter is exactly this | D3 |
| G5 | `sv_quantize_movement_input` (default **1**) | the server rounds `forwardmove`/`leftmove` to `{-1,0,+1}` | a fractional analog write is quantised away server-side | D3 |
| G6 | `attack1_start_history_index` | which `input_history` entry the shot is credited to | write only the newest entry ⇒ the shot misses | V1, D2 |
| G7 | `move_crc` | the server rejects a modified `forwardmove`/`sidemove` if the CRC is stale | the command is silently ignored | P2 §9 |
| G8 | `CBaseUserCmdPB::viewangles` absent | the movement falls back to the default `CMsgQAngle` instance (`PTR_PTR_1822002e8`) | the wish direction is wrong by 0°/whatever the default is | D3 |
| G9 | **AnimGraph 2** (2025-07-28 → live 2026-04-20) | the animation system was rewritten ("reduces the CPU and networking costs associated with animation") | the lower-body / aim-matrix model U2 reversed may no longer exist | L5 |
| G10 | the server's own animation | the server animates the player from the angles it receives; there is no separate "real" yaw to diverge from | the CS:GO-style desync has no protocol to live in | U7, U4, N2 |

### 4.2 The failure modes (the checklist an implementer can use)

| # | symptom | cause | how the community detects it | source |
|---|---|---|---|---|
| F1 | **the camera follows the fake angles** | you wrote the *input's* view angles instead of the command's | "camera is still looking down" | U9, U8, U17 |
| F2 | **the camera is stuck** | the input's view angles are written in a loop | "My camera is simply stuck and i cannot move it no matter what i do. I tried returning false in OnMove" | U17 |
| F3 | **the hands / viewmodel spazz** | the render uses the **cached world transform** of the fake pose; pinning the scene rotation is not enough | "the render uses cached world transform from the fake pose instead of scene rotation" | U1 |
| F4 | **the movement is disturbed** | the wish direction is built from the *command's* view angles | "you move relative to fake angles (looking down) not real angles" | U6, U13, D2 |
| F5 | **only one move key works; two keys go diagonal** | a fake pitch of 89° dumps `IN_FORWARD` into the ground; the side axis survives on XY | "CS2 wishdir uses the full cmd viewangles, so pitch 89 dumps IN_FORWARD into the ground … one key only works when looking forward, two keys go diagonal" | U3 |
| F6 | **the movement fix breaks on jitter / spin** | the correction is applied against an already-corrected frame, or the analog is corrected twice | "backwards obviously works fine but for jitter and spin" | U16, U12 |
| F7 | **the movement fix breaks below full analog** | a deadzone test of `x < 0.01` is true for `0`, so a single-axis key gets a phantom second button | "my movement fix isn't working properly when initial forwardmove and leftmove are < 1" | U15, U3 |
| F8 | **the server ignores the fake angles** | G2 — the sub-tick angles now only travel with other sub-tick events | the write lands, the server's view does not change | L1 |
| F9 | **the shot does not use the silent angle** | only the newest history entry was written | the shot goes to the old angle | D2, V1 |
| F10 | **the command is rejected** | the move CRC was not recomputed after the movement edit | the movement does not change | P2 §9 |
| F11 | **the wheel-jump dies** | the shared `subtick_moves` array was stuffed or cleared | "the only thing the subtick does is that i cant jump with my scrollwheel" | U3, D2 |
| F12 | **the yaw-delta rotation stops steering at ~60°** | the view-delta accumulator cap trips (our RE puts the cap at 2.0, unit not pinned) `[inference: the ~60° symptom is this gate; the numbers do not obviously match]` | "setting the yawDelta works good until ~60 degrees offset from my AA angles, then it just stops moving all together" | U3, D3 (G4) |
| F13 | **the animation is visibly wrong (the "desync" look)** | the local model is animated from the fake pose while the server's is not | the classic "legs turned away" tell | U2, U22, U7 |

**The anti-aim still works as a hitbox defence without any desync.** A CS2
ragebot author reports "my ragebot is struggling to hit correct hitboxes if
enemies using antiaim" (U20, `snippet-only`) — which is consistent with the
verdict that CS2's anti-aim is a *yaw/pitch* presentation, not a hitbox
displacement: the model's upper body turns away (U2), and the head sways with
the feet yaw (U7, the CS:GO model).

### 4.3 The terminology map (community → our fields)

| community word | our field | note |
|---|---|---|
| "real angles" / "view angles" / `dwViewAngles` | `CCSGOInput + 0x688` | the camera's source; the anti-aim's *input* |
| "fake angles" / "desync" / "the AA angles" | `CBaseUserCmdPB::viewangles` | what the server sees |
| "subtick angles" / "silent angles" / "history angles" | `input_history[i].view_angles` | what the shot uses |
| "eye angles" | `m_angEyeAngles` | the animation/hitbox aim — downstream of the camera |
| "the wish" / "the intent" | the analog axes in the camera frame | the movement's basis |
| "movement fix" / "movement correction" / "AA walk fix" | re-basing the analog into the AA frame | U3, U13 |
| "LBY" / "lower body yaw" | the *CS:GO* feet-yaw channel | **does not exist in CS2** (P3 §8) |
| "freestand" / "hide real angle" | the side selection of the AA | N1, P4 |
| "avoid overlap" | keeping the fake away from the real | N1 |
| "jitter" | a per-tick yaw flip | U18: `if (nTickBase % 2) y -= 30; else y += 30;` |
| "spin" | an accumulating yaw | P2 §10 (`remainderf(·,360)`), P3 §8 |
| "animfix" / "local animation fix" | re-syncing the *local* model to the server's animation | CS:GO (U22, U21); the CS2 equivalent is open |

### 4.4 Valve's own angle timeline (the official patch notes)

Read from the Steam Web API (`ISteamNews/GetNewsForApp/v2/?appid=730`), which
returns Valve's update bodies verbatim — the same text `counter-strike.net`
renders client-side.

| date | the line | what it means for the split | id |
|---|---|---|---|
| 2023-06-06 | "Fine-tuned weapon aim punch recovery to be latency-independent during sub-tick recovery on the client." | the client already models the *sub-tick* angle history for the aim punch | L4 |
| 2024-11-20 | "Fixed a case where damage prediction was incorrect due to view angle changes mid-spray." | the **server** honours the sub-tick view angles: a mid-spray angle change changes the predicted damage | L3 |
| 2025-07-28 → 2026-04-20 | AnimGraph 2 replaces the animation system ("reduces the CPU and networking costs associated with animation") | the lower-body / aim-matrix model (U2) is suspect; the animation is now a different machine | L5 |
| 2025-07-30 | "Fixed a case where movement button changes were ignored while adjusting view angles at very high frame rates" | the angle/movement coupling was a **bug**, and Valve fixed it — our repo notes the same date as the "sub-tick view angles" default change | L2 |
| 2025-08-26 | "View angle velocity now affects spectator target viewmodels the same way it affects local player viewmodels." | the eye-angle *velocity* is a first-class animated quantity (`m_angEyeAnglesVelocity`, U2) | L7 |
| 2025-09-26 | "sv_subtick_movement_view_angles will now only send subtick view angles to the server with other subtick events instead of sending them for every frame they change" | **the silent channel is now conditional** — this is the CS2 gate on the input-history angles | L1 |
| 2026-08-24 / 2026-09-09 | the `cs_player_camera` entity, later renamed `CustomPlayerCamera`: "Gives control of a player's view without moving their pawn." | Valve itself now ships a *view* that is decoupled from the pawn — the same decoupling our anti-aim needs, as a scripting entity | L6 |

The last line is worth a second look: the community's "desync" wish (a view
that is not the pawn's) is now a **supported engine concept**, used by map
scripts rather than by cheats.

---

## 5. What it means for our anti-aim

The research imposes the following requirements. They are stated as the
implementer's checklist, in the order the code has to do them.

| # | requirement | why (source) |
|---|---|---|
| A1 | The anti-aim must write the **command's** `base->viewangles` and **must not** write `CCSGOInput + 0x688`. | P2 §10; U1; F1, F2 |
| A2 | The anti-aim must capture the **real** angles *before* it writes, every tick, and keep them: they are the camera's angles and the movement's intent basis. | U8 ("store the viewangle so you can look around"); P2 §9 (`store_angle` before the AA runs) |
| A3 | The anti-aim must **re-base the movement**: take the WASD intent in the *camera* frame, flatten Z, rebuild forward/left in the *AA* frame, write it back to `base->forwardmove/leftmove` **and** to the raw axes `CCSGOInput + 0x270/0x274` (the slot-5 builder re-derives the command's floats from the raw axes, so a command-only write is overwritten — D2). | U3; P1 `movment_fix`; D2; F4, F5 |
| A4 | The re-base must handle the **down-pitch inversion** (`|pitch| > 89 ⇒ reverse the forward axis`) and clamp to **±1** (our analog is in unit values; `SetupMove`'s sanitizer drops `|x| ≥ 10`). | P1 `movment_fix`; D2, D3 |
| A5 | The anti-aim must **not** clear or stuff the shared `subtick_moves` array, and must not use the `yaw_delta` channel for itself — that channel is the strafe's and it is capped. | U3; D2; G4, F11, F12 |
| A6 | The camera must be pinned on the **render/validate** path, and the pin must not be `m_angEyeAngles` (that is a copy of the camera, downstream of it). | U1 (the reply); D1; F3 |
| A7 | The pin must be checked against the **viewmodel**: a pin that only fixes the view-setup angles leaves the hands on the fake pose. The T18b hooks (`calc_view` = `FUN_180a64570`, `local_eye_angles` = `FUN_18093c4f0`) are the candidates; the viewmodel's cached transform is the thing that has to be re-pinned. | U1; D1; F3 |
| A8 | The anti-aim must be able to set **pitch** and survive the pitch-89 case. | U3, U8; F5 |
| A9 | The anti-aim must **not** write the input history: that channel is the *shot* direction, and a fake there would make the anti-aim miss its own shots. | U5, U11; D2 |
| A10 | If the anti-aim edits the movement, the **move CRC** must be recomputed. | P2 §9; G7, F10 |
| A11 | The anti-aim's yaw must be **re-derived from the camera's yaw every tick** (never accumulated), so the operator's mouse stays authoritative; jitter/spin must alternate on a **tick** counter, not a frame counter. | U18 (`nTickBase % 2`); P2 §10; F6 |
| A12 | The anti-aim must have the community's gates: not ladder/noclip, not frozen, not in the buy zone, not `IN_USE`, not mid-grenade-throw, and **not while the ragebot fires**. | P2 §10; U6 |
| A13 | The anti-aim must expose the CS2-shaped surface (P4's names, minus the CS:GO-only ones): `enabled`, `at_targets`, `yaw`/`base_angle`, `jitter`/`yaw_jit`, `spin`/`spin_speed`, `pitch`, `freestand`, `fake_lag` + `fake_lag_limit` + `fake_lag_type`, `air_mode`, `inverter`. `desync_range`/`lby_type`/`legit_lby_type` are **CS:GO-only** and must not be copied into a CS2 schema. | P4 §1.1; P3 §8 |
| A14 | The anti-aim must **report** its own gate, the way the strafer reports `strafe_steered`/`strafe_ignored`: if the server is not honouring the angles, say so instead of moving nothing quietly. | D3 (the pattern we already set) |
| A15 | The local player must still see: the camera on the real angles, the world model turning the way the operator expects (the AA is a *presentation* to the server, not to the operator), the crosshair where the mouse points, and the movement exactly as if the AA were off. | A1–A4; U1, U13 |

### 5.1 The one-line version of the split we are building

> The anti-aim owns **one** write — the command's `base->viewangles` — and
> **one** correction — the analog re-based into the AA frame. The camera needs
> no write at all, because the camera never reads the command; it reads the
> input's view angles. What the camera *does* need is a pin if the anti-aim
> ever touches the input or the pawn's eye angles, and the pin has to cover
> the viewmodel's cached transform, not just the view setup.

---

## 6. Open questions

These are the questions this research could **not** settle. They need the
game, not the web.

| # | question | why it matters | how to settle it |
|---|---|---|---|
| Q1 | Does the CS2 server still have a CS:GO-style **feet / goal-feet** clamp (the 58° limit) after AnimGraph 2? | it decides whether any "desync"-flavoured feature is possible at all | reverse/observe the server's animation on a listen server; U2's anim-state model may be obsolete post-AnimGraph-2 (L5) |
| Q2 | Is the 2026 "fake angles" report (R1, R2) the **input-history** channel, and what is the server's acceptance rule for it? | it is the only desync-shaped thing left, and it is the channel our silent aim already uses | write the history angles on a listen server and watch the server's view of our own pawn (we already have the write seam: D2) |
| Q3 | Is the community's `ValidateInput` / validate-view-angles swap equivalent to our `FUN_180b80e70` (the setter) or `FUN_180b07590` (the per-tick copy)? | it is the only *named* camera seam the community uses, and our T18b RE found the same two functions under other names | RE the two functions against the vtable slot 8 the community hooks (`GetViewAngle`/`SetViewAngle` patterns, P1) |
| Q4 | Is the community's `dwViewAngles` (`client.dll + 0x2314F98`, U6) the same object as our `CCSGOInput + 0x688`? | U1 uses both, in the same function, as if they are the same | dump both and compare |
| Q5 | What is the "input alignment delta" that must be suppressed (U12) in our build? | it is the difference between "re-base the movement" and "inject the rotation as an extra impulse" | re-read U12's source on a live copy (currently `snippet-only`) and find the equivalent in our `strafe_tick` / anti-aim path |
| Q6 | Does the viewmodel's cached world transform come from a field we can re-pin, or is it only reachable through the animation system? | it is the one failure mode that survives the view-setup pin (F3) | RE the viewmodel's render path; if AnimGraph 2 owns it, the pin may have moved |
| Q7 | Is the CS2 "fake angles" of 2026 (R1) a *third* channel we have not catalogued (the deleted GitHub repo)? | a whole technique may be missing | watch for the fork; ask on the forum when UC is reachable again |
| Q8 | How far does the *server* honour a fake **pitch** (89°) in the hitbox, given the model animation is now AnimGraph 2? | it is the pitch half of the anti-aim | measure on a listen server with a controlled pawn |
| Q9 | Is there a CS2 equivalent of the CS:GO **LBY breaker**? | P3 §8 says the CS:GO desync model does not transfer, but the *inverter*/"avoid overlap" surface survives (N1) | reverse the server's lower-body policy, or settle it by the Q1 measurement |

---

## 7. What we take from each source (one line each)

| source | the one line |
|---|---|
| U1 | the fake goes in the command, the camera is pinned in the view override — and the hands still spazz because the render uses the fake pose's cached transform |
| U2 | the local model is a filtered function of the eye yaw (aim matrix limit + damped turn); `m_angEyeAngles` at `pawn + 0x3320` in that build |
| U3 | the AA walk fix is an XY remap of the camera intent into the AA basis; the yaw-delta subtick is not the AA's business; the sub-tick array must not be stuffed |
| U4 | "desync is indeed patched" — what the CS2 AA writes is jitter, not a fake/real pair |
| U5 | the silent channel is every `input_history` entry's `view_angles`; identical angles every frame make the aim "snappy" |
| U6 | `dwViewAngles` is a single source of truth in CS2; the camera follows whatever you write there; the validate hook is the community's camera escape |
| U7 | CS:GO's desync was the *feet* yaw, clamped by the server at the end of the choke cycle |
| U8 | the recipe: store the real angles, write the fake into the command, restore around the validate call, do the movement correction |
| U11 | CS2 splits the angles across the base command and the sub-tick history; the shot pulls from the history |
| U13 | "yes you do need a movement fix. Your movement will be as if you were looking at the angles your antiaim is settings, not the ones you see" |
| U23 | the CS:GO numbers: legit anti-aim ≤ 58°, an LBY breaker reaching 118° |
| P1 | Axion: the AA writes the command; the movement fix is a 3D basis re-projection with the pitch inversion and a ±1 clamp; `SetSubTickAngle` is the silent channel |
| P2 | Weave: no desync model at all; the movement is re-derived from the pre-AA yaw; the move CRC must be recomputed |
| P3 | Fatality: the CS:GO desync is the body yaw + a send-time rewrite of the choked commands — and ten things in it do not transfer |
| P4 | Gamesense: the vocabulary to name our knobs with (`desync`, `lby_type`, `freestand`, `at_targets`, `yaw_jit`, `spin_speed`) |
| P5 | Neverlose: their CS2 strafe rotates the camera — visible, and not to be copied |
| N1/N2/N3 | Neverlose: the AA surface (fake angles, LBY, hide real, avoid overlap, inverter); and "no desync" in CS2 |
| L1 | Valve gated the silent channel: the sub-tick view angles now only travel with other sub-tick events |
| L5 | AnimGraph 2 replaced the animation system in 2026 — every model of the lower body we have is suspect |
| D1 | our own split: the camera never reads `m_angEyeAngles`; the hookable seams are `calc_view` and `local_eye_angles` |
