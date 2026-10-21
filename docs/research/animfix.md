# animfix and local animfix — the community's terms, the server's animation pipeline, and what we must reverse

> Research note, 2026-10-21. Web research only — no C++, no reversing.
> Scope is **technique**, not evasion: nothing here concerns anti-cheat, detection
> avoidance or VAC.
>
> **Access convention.** A row that carries `read` was fetched and parsed in
> full; `snippet-only` means the forum was unreachable by automated fetch and
> only the search-engine snippet was available (the same convention as
> [`cheat-dump-inventory.md`](cheat-dump-inventory.md)). No quote in this
> document is reconstructed: quoted text is verbatim from a fetched body or
> from a search snippet, and the snippet is quoted as such.

---

## 1. TL;DR

1. **"animfix" is at least five different things** in the community, and this
   report keeps them apart: (a) the **local animfix** — re-simulating the
   *local player's* animation on the client so it equals the server's; (b) the
   **other-player animfix** — the same for every *remote* player, so their
   client-side hitboxes match the server's; (c) the **"proper"/server animfix**
   — rebuilding the server's own `CCSGOPlayerAnimState::Update` and running it
   locally; (d) **desync** — the deliberate opposite, making the server's
   animation differ from the client's; (e) **resolver-related "animfix"** —
   feeding the *enemy's* animstate the wrong angles.
2. **The one this repository should build is (a), the local animfix, driven by
   the server's tick boundary and the server's own calculation order** — not
   the "paste a snippet and eyeball the model" style that dominates the forum.
   The reason is structural: the server re-simulates the animation **once per
   simulated tick** while the client re-simulates it **once per rendered
   frame**, and the client adds passes the server never runs (foot planting,
   cycle interpolation, LOD frame-skipping); therefore the client's local bone
   array — the array the studio hitbox capsules are built from — is *wrong*,
   and it is wrong in a way that no amount of "sync the pose parameters"
   patching fixes.
3. **The CS2 pipeline is not CS:GO's pipeline.** CS:GO had a 13-slot overlay
   layer system driven by `CCSGOPlayerAnimState`; CS2 replaced it with
   **AnimGraph2** (announced 2025-07-28, full release 2026-04-20), and the
   player-relevant state is now the 224-byte `CCSPlayerAnimationState` living
   inside `CCSPlayer_MovementServices` at **+0x310 on *both* client.dll and
   server.dll**. The handover in §6 therefore names `CCSPlayerAnimationState`,
   `CBaseAnimGraphController` and the AnimGraph2 node classes — **not**
   `CCSGOPlayerAnimState`.

---

## 2. The vocabulary

Every term the community uses for this, with the definition **the source
gives it**. Where the sources disagree, both readings are kept.

| Term | Definition as the source gives it | Source |
|---|---|---|
| **animfix** | "You match your client animations with the same ones that are on the server, by either rebuilding server animstate or fixing multiple things to achieve a good enough result. First step would be to updating at same time as server, then would come fixing layer delays and other fixes." | UC 459231 post 3173265 (Azewa) — `read` |
| **animfix** (the canonical 2018 snippet) | A function that: sets `m_bClientSideAnimation = true`; saves `gpGlobals->curtime` / `frametime`; sets `curtime = local_player->GetSimTime()` and `frametime = interval_per_tick`; forces the animstate's "model time" to `framecount - 1` if it equals `framecount`; calls `local_player->UpdateClientSideAnimation()`; restores the globals; then `SetAbsAngles(Vector(0.f, GetAnimState()->m_flGoalFeetYaw, 0.f))` — *"if u not doin dis it f\*cks up the model"*. | UC 292406 post 2191302 (NotSome) — `read`; the same code is `C_LagCompensation::fix_animation_data` in gucci `csgo/c_lagcompensation.cpp:182-215` — `read` |
| **local animfix** | The variant applied to the local player only. *"its meant for your local player"*; *"yes it is just for the visual"*; *"You for example need to fix local animations to get your accurate [hitboxes]"*. | UC 292406 posts 2191445 / 2191506 / 426383 — `read` (292406) / `snippet-only` (426383) |
| **proper animfix** / **server animfix** | *"The best way to fix animations for local is to just rebuild `CCSGOPlayerAnimState::Update` since basically animations on the server are of course accurate and the one from the client is [removed]"* — i.e. compile the server's animstate code and run it on the client, feeding it the real view angles. | UC 459231 post 3173680 (Comley) — `read` |
| **animfix** (other players) | The same re-simulation applied to remote players so their hitboxes match the server's: *"Updating every other player's animations only once on NET_POSTDATAUPDATE_END if they have simulated since the last update gets you really close-to-perfect hitboxes (except for desync ofc)."* | UC 459231 post 3173514 (KittenPopo, quoting jdl00) — `read` |
| **client-side animation** (`m_bClientSideAnimation`) | The netvar in `DT_BaseAnimating` that toggles whether the entity's animation is computed client-side. The 2018 animfix turns it **on** for the duration of the re-simulation and back **off** afterwards. | UC 292406 post 2191302 — `read`; gucci `csgo/c_lagcompensation.cpp:193,214` — `read` |
| **animstate** | The per-player animation state machine. In CS:GO it is `CCSGOPlayerAnimState` (13 overlay layers, foot/eye/aim yaw, velocity, lean). In CS2 it is `CCSPlayerAnimationState` — move type, ground move state, ground direction, air action, turn-on-spot angle, previous aim yaw, previous horizontal speed, foot IK offsets, weapon-drop smoothing. | UC 459231 post 3173680 — `read`; CS2OpenDev `CCSPlayerAnimationState` (build 25966348) — `read` |
| **anim layers** | CS:GO's 13 animation overlay slots, in the engine's own order: `AIMMATRIX, WEAPON_ACTION, WEAPON_ACTION_RECROUCH, ADJUST, MOVEMENT_JUMP_OR_FALL, MOVEMENT_LAND_OR_CLIMB, MOVEMENT_MOVE, MOVEMENT_STRAFECHANGE, WHOLE_BODY, FLASHED, FLINCH, ALIVELOOP, LEAN`. | weave `csgo/csgo_internal/sdk/animation_layer.hpp:4-19` — `read` |
| **SetupBones** | The function that turns the animated pose into the world-space bone array. The server's is `CBaseAnimating::SetupBones(matrix3x4a_t*, int boneMask)`; the client's is `C_BaseAnimating::SetupBones(matrix3x4a_t* out, int nMaxBones, int boneMask, float currentTime)`. **This is the function whose output the hitboxes are built from.** | leaked CS:GO source `game/server/baseanimating.cpp:1899` and `game/client/c_baseanimating.cpp:2983` — `read` |
| **UpdateClientSideAnimation** | The client's per-frame entry point into the animation state: `C_CSPlayer::UpdateClientSideAnimation()` → `m_PlayerAnimStateCSGO->Update(EyeAngles()[YAW], EyeAngles()[PITCH])`. Its **server** twin is `CCSPlayer::PostThink()` → `m_PlayerAnimStateCSGO->Update(m_angEyeAngles[YAW], m_angEyeAngles[PITCH])`. | leaked CS:GO source `game/client/cstrike15/c_cs_player.cpp:5950,5954` and `game/server/cstrike15/cs_player.cpp:4027,4201` — `read` |
| **p2c animations** | Premium-to-cheat animation quality. Used as a *comparison target* by the community, but the correct reference is the **server**: *"always, ALWAYS compare them to server"*. | UC 459231 post 3173265 — `read` |
| **desync** | The deliberate inverse: the server animates the player's angles differently from what the client shows. *"The current desync is exactly like the old desync angles in which the server animated the players angles differently to the clients"*. | UC 313369 "[Coding] explanation of desync" — `snippet-only` |
| **bypass** | Not a distinct animfix: in this community it means *making the server's animation of you differ from what others see* (the anti-aim/desync family), or *evading the resolver's animation-based check*. It is the **opposite** of an animfix and is out of scope here. | UC thread tag list `tags/desync.html`; UC 313369 — `snippet-only` |
| **resolver animfix** | Feeding an *enemy's* animstate the wrong angles so the resolver's model is wrong: *"make sure that you are feeding the animationstate the right angles"*. | UC 422413 "[Help] Animfix Shows desync instead of real" — `snippet-only` |
| **foot yaw / goal feet yaw** | The yaw the *lower body* is facing, owned by the animstate (`m_flGoalFeetYaw` in CS:GO; `m_flTurnOnSpotAngle` + `m_flPreviousAimYaw` in CS2). The server forces the entity's abs angles to it so the hitboxes line up: `m_pPlayer->SetAbsAngles(QAngle(0, m_flFootYaw, 0))` — *"force abs angles on client and server to line up hitboxes"*. | leaked CS:GO source `game/shared/cstrike15/csgo_playeranimstate.cpp:348` — `read` |
| **pose parameter** | The per-model float parameters that pose the skeleton (e.g. `DUCK_AMOUNT`, `speed`, `moveYaw`). The 2018 animfix saves/restores 24 of them; Fatality's snapshot saves 24 as well. | UC 443579 — `snippet-only`; fatality `features/animations.h:11,73` (`std::array<float, 24> poses`) — `read` |
| **AnimGraph2 / AG2** | CS2's replacement for the CS:GO overlay-layer animation system: *"The animation system in CS2 is in the process of being upgraded to AnimGraph2, which requires reauthoring existing game content."* Announced in the 2025-07-28 update; beta April 2026, full release 2026-04-20. | Steam/SteamDB patch note 19388062 (2025-07-28) — `snippet-only`; HLTV 44430 (2026-04-20) — `snippet-only`; UC 758612 *"This post proves CS2 animation is animgraph2/.vanim driven"* — `snippet-only` |
| **anim graph state** | The CS2 community's name for `c_csgo_player_anim_graph_state`, the CS2 counterpart of the CS:GO animstate: it keeps `last_update_increment`, `last_update_time`, `eye_yaw`, `foot_yaw`. | weave `cs2/counterstrike2/sdk/classes/c_csgo_player_anim_graph_state.hpp:7-18` — `read` |

---

## 3. The server's animation pipeline

### 3.1 CS:GO — the reference model (fully sourced)

The server's per-tick order, from the leaked CS:GO source:

| # | Where | What it computes | Client-side counterpart | Moves the hitboxes? |
|---|---|---|---|---|
| 1 | `CCSPlayer::PostThink()` — `game/server/cstrike15/cs_player.cpp:4027` | Copies `EyeAngles()` into the networked `m_angEyeAngles` (`:4192-4197`) and stores `m_flThirdpersonRecoil = GetAimPunchAngle()[PITCH]` (`:4199`) | none — `m_angEyeAngles` is a network var | **yes** (aim yaw drives the aim matrix) |
| 2 | `game/server/cstrike15/cs_player.cpp:4201` → `CCSGOPlayerAnimState::Update(eyeYaw, eyePitch)` | The whole animstate recomputation, once per **simulated tick** | `C_CSPlayer::UpdateClientSideAnimation()` (`game/client/cstrike15/c_cs_player.cpp:5950-5954`) — once per **rendered frame** | **yes** — everything below |
| 3 | `csgo_playeranimstate.cpp:261` | `eyePitch = AngleNormalize(eyePitch + m_flThirdpersonRecoil)` — the recoil is folded into the aim matrix | same code, client build | **yes** (pitch of the upper body) |
| 4 | `csgo_playeranimstate.cpp:266-268` | **The re-update gate**: `if (!bForce && (m_flLastUpdateTime == gpGlobals->curtime || m_nLastUpdateFrame == gpGlobals->framecount)) return;` then `m_flLastUpdateIncrement = Max(0, curtime - m_flLastUpdateTime)` | same code | **yes** — the *increment* is the animation dt |
| 5 | `csgo_playeranimstate.cpp:275-278` | `m_flEyeYaw/m_flEyePitch` normalised; `m_vecPositionCurrent = GetAbsOrigin()`; `m_pWeapon = GetActiveCSWeapon()` | same | **yes** |
| 6 | `csgo_playeranimstate.cpp:282-301` | On weapon change: purge the per-layer dispatch state (`m_pDispatchedStudioHdr`, `m_nDispatchedSrc/Dst`) | same | weakly (weapon pose) |
| 7 | `csgo_playeranimstate.cpp:315` | `m_flAnimDuckAmount = clamp(Approach(clamp(m_flDuckAmount + m_flDuckAdditional, 0, 1), m_flAnimDuckAmount, dt*6), 0, 1)` | same | **yes** (crouch changes the whole skeleton) |
| 8 | `csgo_playeranimstate.cpp:319-324` | Base layer reset: `SetSequence(0); SetPlaybackRate(0); SetCycle(0);` | same | yes |
| 9 | `csgo_playeranimstate.cpp:327-335` | **The ordered layer set-ups** — `SetUpVelocity(); SetUpAimMatrix(); SetUpWeaponAction(); SetUpMovement(); SetUpAliveloop(); SetUpWholeBodyAction(); SetUpFlashedReaction(); SetUpFlinch(); SetUpLean();` | same, client build | **yes** — `SetUpAimMatrix` (`:2064`), `SetUpMovement` (`:1351`) and `SetUpLean` (`:827`) all move bones; `SetUpAliveloop`/`SetUpFlashedReaction`/`SetUpFlinch`/`SetUpWholeBodyAction` move the weapon/arms only |
| 10 | `csgo_playeranimstate.cpp:348` | **`m_pPlayer->SetAbsAngles(QAngle(0, m_flFootYaw, 0))`** — *"force abs angles on client and server to line up hitboxes"* | the client build has the same line, but the *value* of `m_flFootYaw` differs once the animstate diverges | **yes** — the whole model rotates |
| 11 | `csgo_playeranimstate.cpp:358-359` | `m_flLastUpdateTime = curtime; m_nLastUpdateFrame = framecount;` | same | — |
| 12 | `CBaseAnimating::SetupBones(pBoneToWorld, boneMask)` — `game/server/baseanimating.cpp:1899` | `adjOrigin = GetAbsOrigin() + (0,0,m_flEstIkOffset)` (`:1925`); IK init/solve if `m_pIk` (`:1940-1945`); `GetSkeleton(pStudioHdr, pos, q, boneMask)` (`:1950`); `Studio_BuildMatrices(hdr, GetAbsAngles(), adjOrigin, pos, q, -1, scale, pBoneToWorld, boneMask)` (`:1997-2006`) | `C_BaseAnimating::SetupBones(out, nMaxBones, boneMask, currentTime)` — `game/client/c_baseanimating.cpp:2983` — **plus client-only passes** (see §4) | **yes** — this *is* the hitbox bone array |
| 13 | hitbox construction | The studio hitbox capsules are read out of the bone array: world capsule = `bone.pos + rotate(bone.quat, endpoint)`, radius `m_flShapeRadius` | same consumer | **yes** — this is the output |

### 3.2 CS2 — what the server does now

CS2 has no `CCSGOPlayerAnimState` and no 13-slot overlay. The chain is:

| # | Component | Fields / evidence | Moves the hitboxes? |
|---|---|---|---|
| 1 | **`CCSPlayerAnimationState`** (224 B) inside `CCSPlayer_MovementServices` at **+0x310** — **the same offset in client.dll and server.dll** (CS2OpenDev build 25966348; cs2-dumper `client_dll.hpp:3051` gives `m_AnimationState = 0x310`) | `m_currentMoveType` (+0x18, `MoveType_t{None,Ground,Air,Ladder}`), `m_groundMoveState` (+0x19, `GroundMoveState_t{None,Idle,Start,Move,TurnOnSpot,TurnOnSpotLoop,PlantAndTurn}`), `m_groundActionDirection` (+0x1A, `Direction_t{None,N,NE,E,SE,S,SW,W,NW}`), `m_airAction` (+0x1B, `AirAction_t{None,Jump,StartFall,Land}`), `m_bWasOnGroundLastUpdate` (+0x1C), `m_bWasStationaryLastUpdate` (+0x1D), `m_actionStartTick` (+0x20), `m_staticAimTimerStartTick` (+0x24), `m_plantAndTurnStartTick` (+0x28), `m_flTurnOnSpotAngle` (+0x2C), `m_flPreviousAimYaw` (+0x30), `m_flPreviousHorizontalSpeed` (+0x34), `m_flFootIKOffsetLeft` (+0x38), `m_flFootIKOffsetRight` (+0x3C), `m_flWeaponDropPercentageDueToMovement` (+0x40), `m_flWeaponDropSmoothDampVelocity` (+0x44) | **yes** — the move state, the aim yaw and the foot IK offsets all feed the pose |
| 2 | **The animstate update** — *"writes animgraph2 parameters (move dir, aim yaw/pitch, speed…)"* | Reached through the movement-services tree; *"not name-tagged in the IDB"*; the community's own method is a hardware write-breakpoint on `m_flPreviousAimYaw` at `ms + 0x340` (or `+0x33C` for `m_flTurnOnSpotAngle`) | **yes** |
| 3 | **`CBaseAnimGraphController`** — the AG2 controller. Client +1296 (0x510), server +1248 (0x4E0) inside `CBodyComponentBaseAnimGraph::m_animationController` | `m_nAnimationAlgorithm` (+0x18), `m_nPrevAnimUpdateTick` (client +0xD4 / server +0x80), `m_bLastUpdateSkipped` (client +0xCF / server +0x7B), `m_hSequence` (client +0xB0 / server +0x5C), `m_flPlaybackRate` (client +0xC0 / server +0x6C), `m_sAnimGraph2Identifier` (client +0x440 / server +0x400), `m_pGraphInstanceAG2` (client +0x448 / server +0x408) | **yes** |
| 4 | **AG2 evaluation** — `animationsystem.dll`. The named nodes: `CNmAimCSTask` (upper-body aim IK: spine→neck→head→weapon), `CAimMatrixUpdateNode` (clamps the aim to `m_flMaxYawAngle` and damps it), `CTurnHelperUpdateNode` (plays the turn animation when the limit is exceeded, rate-limited and damped by `CAnimInputDamping`) | `AimMatrixOpFixedSettings_t`: `m_damping` (+0x80, `CAnimInputDamping`), `m_flMaxYawAngle` (+0xC4), `m_flMaxPitchAngle` (+0xC8), `m_bUseBiasAndClamp` (+0xD5), `m_flBiasAndClampYawOffset` (+0xD8); `CAnimInputDamping`: `m_speedFunction` (+8), `m_fSpeedScale` (+12), `m_fFallingSpeedScale` (+16); `CTurnHelperInstanceData`: `m_turnAmount` (+0), `m_turnStartTime` (+4), `m_duration` (+8). *"The tuning numbers (max yaw, turn rate, damping) live in the compiled .vanim graph asset, not as constants in the DLL."* | **yes** — the aim matrix is what points the upper body (and therefore the head hitbox) |
| 5 | **Bone setup** → `CSkeletonInstance::m_modelState` (client +0x140, server +0x120) → `CModelState::m_pBoneArray` (client +0x80 = skeleton +0x1C0, stride 0x20) | see this repo's `docs/re/aim.md` | **yes** |
| 6 | **Hitboxes** — `CSkeletonInstance::m_nHitboxSet` (client +0x3FC, server +0x3BC); capsules from the bone array | see `docs/re/aim.md` | **yes** — the output |

> **The CS:GO → CS2 mapping that the community asserts** — *"eye yaw (instant, you control it) → client.dll → CCSPlayerAnimationState (embedded in CCSPlayer_MovementServices @ +0x310) → tracks move state, previous aim yaw, turn-on-spot angle → writes animgraph2 parameters (move dir, aim yaw/pitch, speed…)"* — is **independently confirmed by the schema**: `CCSPlayer_MovementServices::m_AnimationState` is at +784 (0x310) in *both* the client.dll and server.dll builds of `CCSPlayer_MovementServices` (CS2OpenDev build 25966348), and `m_flTurnOnSpotAngle`/`m_flPreviousAimYaw` are at +44/+48 inside that struct — i.e. `ms + 0x33C` and `ms + 0x340`, exactly the addresses the thread gives for its hardware breakpoints. `snippet-only` + `read`, cross-confirmed.

---

## 4. The divergence

What the client does differently, the symptom it produces, and the evidence.

| # | Divergence | Symptom | Evidence |
|---|---|---|---|
| D1 | **Tick vs frame.** *"The client updates player animations every frame; the server updates player animations for every simulated tick."* | The local model animates at the frame rate; at 144 fps it is 2.25× ahead of the server's 64 Hz. Any local hitbox drawn from it is wrong by up to a tick of motion. | UC 459231 post 3173373 (KittenPopo) — `read`; corroborated by UC 387519 *"the server animates players by ticks instead of frames"* — `snippet-only` |
| D2 | **Delayed animations.** *"Delayed animations are caused by the fact that csgo devs have literally 2 braincells and made the majority of the animation code just not run on the client."* | A landing plays ~one RTT late; the local model is in the past relative to the server. | UC 459231 post 3173373 — `read` |
| D3 | **`ShouldSkipAnimationFrame`** — client-only. *"if `gpGlobals->frametime < FPS_TO_FRAMETIME_SECS(300.0f)` … compute every third animation frame to floor animation at 100fps"*. | Above 100 fps the client's animation runs at 100 Hz while the server runs at 64 Hz — a systematic phase error that grows with framerate. | leaked CS:GO source `game/client/c_baseanimating.cpp:2938-2979` — `read` (no server counterpart: the server's `SetupBones` at `game/server/baseanimating.cpp:1899-2014` has no such path) |
| D4 | **`DoExtraBoneProcessing`** — client-only virtual, overridden by `C_CSPlayer` to call `DoProceduralFootPlant`. The server's `SetupBones` never calls it. | Foot-plant IK solves the feet onto the ground client-side only ⇒ the **foot/ankle hitboxes** differ between client and server. | leaked CS:GO source `game/client/c_baseanimating.h:204` (default), `game/client/cstrike15/c_cs_player.cpp:6536,6579` (override) vs `game/server/baseanimating.cpp:1899-2014` — `read`; the community's own instruction *"Also disable DoExtraBoneProcessing, the server doesn't do it"* — UC 459231 post 3173373, and *"you also need to either stop the game from calling DoExtraBoneProcessing or DoProceduralFootplant"* — post 3173680 — `read` |
| D5 | **The client interpolates the bone matrix between frames.** *"the client side bone setup code is just riddled with sequence/cycle interpolation, 'optimizations', and a bunch of other [removed] you need to 'patch' or 'hook' to 'fix'."* | The bones the client draws are a *rendered* pose; the server's are the *simulated* pose. | UC 524203 post 3584276 (Lynxaa) — `snippet-only`; the equivalent claim in UC 459231 post 3173373 — `read` |
| D6 | **The abs-angle alignment.** The server ends its animstate update with `SetAbsAngles(QAngle(0, m_flFootYaw, 0))` *"to line up hitboxes"*. A client that computes a different foot yaw (or never sets abs angles) rotates the whole model — and every capsule attached to it — by the difference. | The client's hitboxes are *rotated* relative to the server's. | leaked CS:GO source `csgo_playeranimstate.cpp:348` — `read`; the 2018 animfix's `SetAbsAngles(Vector(0.f, m_flGoalFeetYaw, 0.f))` — UC 292406 post 2191302 / gucci `csgo/c_lagcompensation.cpp` — `read` |
| D7 | **The update gate is frame-based on the client.** `m_nLastUpdateFrame == gpGlobals->framecount` short-circuits the client update; the server has no framecount. | Calling the client animstate update twice in a frame is a no-op; calling it once per tick requires defeating the gate (the `player_model_time = framecount - 1` trick, or setting `last_update`). | leaked CS:GO source `csgo_playeranimstate.cpp:266-267` — `read`; the trick — UC 292406 post 2191302 / gucci `csgo/c_lagcompensation.cpp:202-206` — `read`; Fatality does the same with `state->last_framecount` — `features/animations.cpp:1236-1237` — `read` |
| D8 | **The client's foot yaw lags the server's.** *"The main reason isn't for the user, it's for the cheat — think of all the calculations used. The thing is though if both animations are off you can needlessly miss shots."* | The local hitbox set is offset in yaw from the server's by the desync amount. | UC 459231 post 3173437 (jdl00) — `read` |
| D9 | **The animstate is fed the wrong angles.** Feeding the animstate stale/other angles produces a model that shows a *desync* instead of the real pose. | The client's local model displays the desynced body instead of the server's authoritative one. | UC 422413 "[Help] Animfix Shows desync instead of real" — `snippet-only` |
| D10 | **The number of times the animation is updated per frame.** *"Doing bcause animation Update Local can be accessed more than once on frame."* | Double-updating the animstate advances it twice → jitter. | UC 344993 "[Help] animation fix troubles" — `snippet-only` |
| D11 | **Where the fix is applied.** *"You should just do your local animation fix at the end of your createmove at that point rather than FRAME_NET_UPDATE_END"*; *"You should call local animfix at the end of createmove as under any case you shouldn't update your animations more than once a tick"*. | Applying the fix at the wrong hook point either double-updates (jitter) or uses a stale tick. | UC 394914, UC 459231 post 3173265 — `snippet-only` / `read` |

---

## 5. The public implementations

### 5.1 gucci (CS:GO) — `notphage/gucci`

The tree the forum's canonical animfix was pasted from: *"this is obviously pasted from gucci"* (UC 292406 post 2191308), and the reply *"https://github.com/notphage/gucci/blob/master/csgo/c_lagcompensation.cpp#L182 — Improper, pasted, and already released."* (UC 292406 post 2191416). `read`

- `C_LagCompensation::fix_animation_data(C_BaseEntity*)` — `csgo/c_lagcompensation.cpp:182-215`:
  - `entity->client_side_animation() = true;` (`:193`)
  - save `globaldata->curtime` / `frametime` (`:195-196`), set
    `curtime = entity->get_simulation_time()` (`:199`) and
    `frametime = globaldata->interval_per_tick` (`:200`)
  - `player_animation_state = entity + 0x3894`; `player_model_time = *(int*)(player_animation_state + 112)`;
    if `*player_model_time == globaldata->framecount` then `*player_model_time = framecount - 1` (`:202-206`)
  - `entity->update_clientside_animations()` (`:209`)
  - restore the globals (`:211-212`), `client_side_animation() = false` (`:214`)
- Called from `C_LagCompensation::frame_net_update_end()` (`:15-39`) for **every other player** (`:33`), together with `set_interpolation_flags(entity, DISABLE_INTERPOLATION)` (`:34`) and `update_player_record_data(entity)` (`:35`).
- The record it stores (`:221-242`) is exactly the *pose-relevant* state: origin, abs origin, velocity, obb min/max, eye angles, abs eye angles, **sequence**, entity flags, simulation time, **lower body yaw**, **cycle**, **pose parameters**, ragdoll positions.

> **This is the reference "animfix" for the *other* players** and it is also, by accident, the reference list of what has to be saved and restored.

### 5.2 UC 292406 — "[Source] Sync Animations with Server (AnimFix)" (NotSome, 2018-07-28)

`read` (page 1 of 2). The post body is the gucci snippet re-formatted, plus:

- *"Guys don't use this………"* and the thread's own verdict from the threadstarter: *"yes it is just for the visual"*.
- The community's critique: *"proper animation fixes are useful this will just make you miss alot more than normal … its meant for your local player"*; *"WHATS THE POINT THEN there is no need to fix the animations on local player"*.

### 5.3 UC 459231 — "[Discuss] Best way to fix animations" (2022-09)

`read` (page 1 of 2). The most useful single thread, because it audits the public trees rather than pasting code:

- **What the public trees do** (Cride, post 3173251): *"Most of them are the same. The only difference between the forum and sources are the pose parameter and abs yaw (goal feet yaw) saves. The forum ones saved it when bsendpacket is false while the sources are local->simtime != local->oldsimtime … All of them running at frame_render_start (except weave what is frame_net_update_end). None of the animationfixes are close to p2c animations."*
- **What "proper" means** (Azewa, post 3173265): rebuild the server animstate; *"First step would be to updating at same time as server, then would come fixing layer delays and other fixes"*; *"you can use leaked csgo src + lasershark's reversed 2020 server animstate as reference"*; *"You should call local animfix at the end of createmove as under any case you shouldn't update your animations more than once a tick."*
- **The tick/frame rule and the two gates** (KittenPopo, post 3173373): *"Start by preventing the game from updating animations each frame, and do it manually for each tick (after createmove is my favorite)."* … *"Easiest way to match this is to only allow SetupBones on your local player when you send a packet, otherwise just use the previous bone cache. For other players, it's pretty much the same except I recommend NET_UPDATE_END since that's right after you receive their latest data."* … *"Make sure to restore networked animation layers after you update animation, too (they are overwritten by default)."* … *"IIRC you can just set the last update time to (curtime - interval_per_tick) to force a full tick of animation on each update."* … *"Also disable DoExtraBoneProcessing, the server doesn't do it."*
- **The community's ceiling** (KittenPopo, post 3173428): *"I don't see a valid reason for a cheater to need to see their hitboxes pixel perfect."* — the counter-argument (jdl00, post 3173437) is the one that matters for us: *"if both animations are off you can needlessly miss shots … if each angle is slightly off, as distance increase your shot angle will become increasingly inaccurate"*.
- **The real reference** (post 3173514): *"Updating every other player's animations only once on NET_POSTDATAUPDATE_END if they have simulated since the last update gets you really close-to-perfect hitboxes (except for desync ofc)."*
- **The endgame** (Comley, post 3173680): *"The best way to fix animations for local is to just rebuild `CCSGOPlayerAnimState::Update` … So you can just do your animations fix normally and update correctly along with calling rebuilt server `CCSGOPlayerAnimState::Update` and feeding it your real viewangle and etc."* — and the response (jdl00, post 3173990): *"There's very little difference between the server and client update, the main differences happen in setupmovement/velocity etc. You just can't call server functions for obvious reasons."*
- **The pointer to the reversed server animstate**: `https://github.com/click4dylan/CSGO_AnimationCode_Reversed` (post 3173694).

### 5.4 `click4dylan/CSGO_AnimationCode_Reversed` — the 2020 server animstate, reversed before the leak

`read` (file downloaded and inspected). `CCSGOPlayerAnimState_New.cpp` (2806 lines) is a pre-leak reverse of the server's animstate, published *"before the public leak was available"*. Useful as a **diff baseline** for the leaked `csgo_playeranimstate.cpp`: it is the same function with the 2018→2020 changes applied. `[inference]` — the two files are close enough in structure to be diffed, but the diff has not been run.

### 5.5 Fatality (CS:GO) — `cacamelio/fatality-source-fixed`

`read`. This is the most complete *engineering* treatment in the public trees, and it is not a "paste an animfix" — it is a per-command animation snapshot/restore system.

- **`hooks/setup_bones_calls.cpp`** — hooks `C_CSPlayer::SetupBones` and `C_BaseAnimating::SetupBones`:
  - `hook::animating_setup_bones` (`:4-21`): **during prediction, return the cached bone array instead of recomputing** — `memcpy(out, animating->get_bone_accessor()->m_pBones, …)` and return `true`. This is the community's *"only allow SetupBones on your local player when you send a packet, otherwise just use the previous bone cache"* implemented literally.
  - `hook::setup_bones` (`:23-123`): forces the LOD re-evaluation (`mask |= BONE_USED_BY_ANYTHING`, `flags = 0; frame = 0;`), then around the original call it **forces the abs angles to the animstate's foot yaw** — `player->set_abs_angles({0.f, state->foot_yaw, 0.f})` — and rolls back `view_offset`, `OBBMaxs`, `ground_entity`, `collision_viewheight`, `collision_bounds_change_time`, `eye_angles`, `active_weapon`.
  - `hook::update_interp_vars` (`:130-137`) and `hook::post_data_update` (`:182-199`): **clear `m_bClientSideAnimation` around the engine's own interpolation / data-update**, then restore it — i.e. *"Make sure to restore networked animation layers after you update animation, too (they are overwritten by default)"*.
  - `hook::on_latch_interp_var` (`:139-180`): for the local player, set `gpGlobals->curtime = ticks_to_time(cmd_number)` around the latch — the tick-accurate version of the 2018 `curtime = GetSimTime()` trick.
- **`features/animations.h`** — the snapshot record: `CCSGOPlayerAnimState state`, `std::array<C_AnimationLayer, 13> layers` + `client_layers`, `std::array<float, 24> poses`, `lower_body_yaw_target`, `is_strafing`, `eye_angles`, `sequence`, `checksum` (the command CRC). `read`
- **`features/animations.cpp`** — `animations::update_animations` (`:1225-1279`): backs up `frametime`/`curtime`; for **networked** players sets `frametime = interval_per_tick` and `curtime = record->m_simtime` and forces `state->last_update` (`:1240-1249`); then `player->get_anim_state()->update(angles)` (`:1256`). `animations::merge_local_animation` (`:1306-1358`) is the per-command local driver: it detects a *new* command (sequence + CRC), restores the previous snapshot, re-runs the update, and re-snapshots. `read`

### 5.6 Weave — `cacamelio/weave-v3-csgo-and-cs2`

`read` (the CS2 SDK headers). The only public CS2 tree with animation types:

- `cs2/counterstrike2/sdk/classes/c_csgo_player_anim_graph_state.hpp:7-18` — the CS2 animstate: `m_entity` (+0x8), `last_update_increment` (+0x94), `last_update_time` (+0x98), `eye_yaw`, `foot_yaw`. **Note:** these offsets do **not** match `CCSPlayerAnimationState` — they are a *different* struct (`c_csgo_player_anim_graph_state`), so the CS2 animstate family is not a single object. `[inference]`
- `cs2/counterstrike2/sdk/classes/c_body_component_base_anim_graph.hpp:8-11` — `get_anim_controller()` = `this + 0x460`.
- `csgo/csgo_internal/sdk/animation_layer.hpp:4-19` (the enum) and `:21-40` (the `animation_layer_t` layout, 0x38 B: `m_client_blend`, `m_blend_in`, `m_studio_hdr`, `m_dispatched_src/dst`, `m_order`, `m_sequence`, `m_prev_cycle`, `m_weight`, `m_weight_delta_rate`, `m_playback_rate`, `m_cycle`, `m_owner`).
- Weave's CS2 **lag compensation** (already documented in this repo's [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) §7) forces `calc_world_space_bones` at `frame_net_update_end` and `memcpy`s the whole bone cache per record — the same *shape* as gucci's `fix_animation_data`, but for CS2 and without the animstate re-simulation.

### 5.7 The CS2-specific reversal: UC 758612 "[Release] Animation system reversal"

`snippet-only` — UnknownCheats was in maintenance for the whole of the research window (§Appendix A). The search-engine excerpt of the post is unusually rich and is quoted here as a snippet:

> *"What's happening in the animation system (animationsystem.dll — the generic engine) 2. How CS2 drives it (client.dll — CCSPlayerAnimationState, the aim task, the pawn) 3. Appendix: raw IDA decompiles"*
>
> *"eye yaw (instant, you control it) → client.dll → CCSPlayerAnimationState (embedded in CCSPlayer_MovementServices @ +0x310) → tracks move state, previous aim yaw, turn-on-spot angle → writes animgraph2 parameters (move dir, aim yaw/pitch, speed…)"*
>
> *"animationsystem.dll (generic NetworkMotion / animgraph2 engine) → CNmAimCSTask → upper body aim IK (spine→neck→head→weapon) → CAimMatrixUpdateNode → clamps aim to ±m_flMaxYawAngle, damps it → CTurnHelper/locomotion → plays a turn anim when the limit is exceeded, rate-limited + damped via CAnimInputDamping"*
>
> *"The tuning numbers (max yaw, turn rate, damping) live in the compiled .vanim graph asset, not as constants in the DLL."*
>
> *"ms = *(uintptr_t*)(pawn + OFF_m_pMovementServices); // CCSPlayer_MovementServices\*  animSt = ms + 0x310; // CCSPlayerAnimationState  float prevAimYaw = \*(float\*)(animSt + 0x30); // m_flPreviousAimYaw (smoothed)  float turnOnSpot = \*(float\*)(animSt + 0x2C); // m_flTurnOnSpotAngle  uint8 moveType = \*(uint8_t\*)(animSt + 0x18); // CCSPlayerAnimationState::MoveType_t  uint8 moveState = \*(uint8_t\*)(animSt + 0x19); // GroundMoveState_t  float eyeYaw = \*(float\*)(pawn + 0x3320 + 0x4); // m_angEyeAngles.yaw"*
>
> *"The per-frame `CCSPlayerAnimationState::Update` (which actually computes m_flPreviousAimYaw and m_flTurnOnSpotAngle from eyeYaw/velocity) is reached through the movement-services tree and is not name-tagged in the IDB."*

Every offset in that snippet is confirmed by the schema (§3.2) except `m_angEyeAngles` (`pawn + 0x3320 + 0x4`), which is build-specific: CS2OpenDev build 25966348 puts `C_CSPlayerPawn::m_angEyeAngles` at **+13824 (0x3600)**. `[inference]` — the thread's build differs from 25966348.

### 5.8 Other threads (title + search snippet only, `snippet-only`)

| Thread | Why it matters |
|---|---|
| UC 277614 "[Animation syncing]" | The canonical "how do I sync" question. |
| UC 338932 "[Discuss] Proper animfix" | Long-form discussion of client/server syncing. |
| UC 290864 "[Discuss] what does an animfix do?" | The definitional thread. |
| UC 348353 "[Source] accurate local player" | *"This will produce animations which don't jitter in unwanted ways and generally show a pretty accurate player model, without any manual fixing"*. |
| UC 377736 "[Information] On fixing animations and accurate bone" | *"Another important part of fixing the animations is the personal management of the setup or bone configuration of the setbones."* |
| UC 377094 "[Discuss] fixing local animations" | *"this is not even close to correct animation fix"*. |
| UC 443579 "[Help] Local Animation fix messed up" | The save/restore set: `static AnimationLayer saveanimlayer[13]; static std::array<float, 24> saveposeparameter; static float savegoalfeetyaw;`. |
| UC 378531 "[Source] local animation fix / hooking m_bClientSideAnimation / ragebot bug" | The local variant with the netvar hook. |
| UC 375366 "[Help] Local Animation Fix Problems" | `local->UpdateAnimationState(local->GetAnimState(), …)` — the client-side entry point name. |
| UC 394914 "[Help] jittering animations on local" | *"You should just do your local animation fix at the end of your createmove"*. |
| UC 415157 "[Help] Animations on local players updates twice" | The double-update failure mode. |
| UC 442832 "[Help] Animstate refuses to update when lagging" | The `last_update`/`last_framecount` gate. |
| UC 455165 "[Help] animation fix flicking" | The yaw-flick symptom. |
| UC 371924 "[Coding] proper lag fix and animation fix" | *"server does animate every tick. animating data belonging to ticks that you don't receive on the client."* |
| UC 387519 "[Help] animation fix works different on servers." | *"the server animates players by ticks instead of frames"*. |
| UC 352712 "[Source] Sync Animation with Server" | *"Its not an AnimationFix its just for Updating the Model with that of the Server."* |
| UC 409760 "[Help] local 'anim fix' causes yaw to flick when doing desync" | The desync/animfix interaction. |
| UC 422413 "[Help] Animfix Shows desync instead of real" | Feeding the animstate the wrong angles. |
| UC 459969 "[Source] '2 line animationfix' for local" | *"A funny way to show where is the server sided hitbox."* |
| UC 426383 "[Information] how to paste your favourite public animation fix" | *"this is mainly an local animfix … You for example need to fix local animations to get your accurate [hitboxes]"*. |
| UC 223459 / UC 292854 | "What is the proper way to synchronize animations with the server to get the most accurate hitbox?" |

---

## 6. What we must reverse — the handover to the RE agent

Written so that an RE agent can go straight at it. Every anchor below is a
**field name or a class name that is known to exist** (schema-verified unless
marked), so the agent never has to guess a structure.

### 6.0 The CS2 shape, in one diagram

```
server tick                                   client frame
────────────────────────────────────────      ──────────────────────────────────────
CPlayer_MovementServices::RunCommand
  └ CCSPlayerAnimationState::Update          ← A1  (the tick-vs-frame gate)
       (eye yaw, velocity, move type, …)          (currently: per frame)
  └ CBaseAnimGraphController (AG2)  ← A2
       └ CNmAimCSTask / CAimMatrixUpdateNode / CTurnHelperUpdateNode
  └ SetupBones → bone array  ← A3              (client has extra passes: D3, D4, D5)
  └ hitbox capsules from the bone array ← A4
```

### 6.1 A1 — the animstate update (the core)

| What | Exact name / offset | Notes |
|---|---|---|
| Container | `CCSPlayer_MovementServices` → field `m_AnimationState`, **+0x310**, type `CCSPlayerAnimationState`, size **224** | Same offset in `client.dll` and `server.dll` (CS2OpenDev build 25966348; cs2-dumper `client_dll.hpp:3051`). The pawn pointer: client `C_BasePlayerPawn::m_pMovementServices` **+0x1330**; server `CBasePlayerPawn::m_pMovementServices` **+0xB50**. |
| The struct's fields | `m_currentMoveType` +0x18, `m_groundMoveState` +0x19, `m_groundActionDirection` +0x1A, `m_airAction` +0x1B, `m_bWasOnGroundLastUpdate` +0x1C, `m_bWasStationaryLastUpdate` +0x1D, `m_actionStartTick` +0x20, `m_staticAimTimerStartTick` +0x24, `m_plantAndTurnStartTick` +0x28, `m_flTurnOnSpotAngle` +0x2C, `m_flPreviousAimYaw` +0x30, `m_flPreviousHorizontalSpeed` +0x34, `m_flFootIKOffsetLeft` +0x38, `m_flFootIKOffsetRight` +0x3C, `m_flWeaponDropPercentageDueToMovement` +0x40, `m_flWeaponDropSmoothDampVelocity` +0x44 | The **unmapped region is +0x00..+0x17 and +0x48..+0xDF** — the reverser must establish what is there (the CS:GO animstate's equivalent fields are `m_flLastUpdateTime`, `m_flLastUpdateIncrement`, `m_flEyeYaw`, `m_flEyePitch`, `m_flFootYaw`, `m_flMoveYaw`, `m_flAnimDuckAmount`, `m_flLastUpdateFrame` — the CS2 struct is a *different* shape, so do not assume). |
| The function to find | The per-tick/per-frame `CCSPlayerAnimationState::Update` that writes `m_flPreviousAimYaw` and `m_flTurnOnSpotAngle` | **Not name-tagged** (UC 758612). The community's method: hardware **write** breakpoint on `ms + 0x340` (`m_flPreviousAimYaw`) and `ms + 0x33C` (`m_flTurnOnSpotAngle`). For us: set a write-breakpoint on `ms + 0x340` and capture the writing function; then walk its callers. |
| What it must reproduce | The *ordered* computation, in the CS:GO order (§3.1 rows 3-11) but with CS2's fields: (1) the eye-yaw/eye-pitch intake, (2) the update gate (`last_update` / `last_framecount`), (3) `m_flLastUpdateIncrement`, (4) the move-type / ground-move-state / direction / air-action classification, (5) `m_flTurnOnSpotAngle` accumulation, (6) `m_flPreviousAimYaw` smoothing, (7) `m_flPreviousHorizontalSpeed`, (8) the foot-IK offsets, (9) the weapon-drop smoothing, (10) **the abs-angle alignment** — the CS2 equivalent of `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`. | Row 10 is the one that moves *every* hitbox. Find it in CS2 by watching which write changes the pawn's abs angles at the end of the animstate update. |
| Validation | The observable is `m_flPreviousAimYaw` and `m_flTurnOnSpotAngle` themselves: after the fix they must change **once per tick**, not once per frame. The symptom to kill is the **yaw flick** (UC 455165). |

### 6.2 A2 — the AnimGraph2 evaluation

| What | Exact name / offset |
|---|---|
| Controller | `CBodyComponentBaseAnimGraph::m_animationController`, type `CBaseAnimGraphController` — **client +0x510**, **server +0x4E0**. `C_BaseEntity::m_CBodyComponent` = +0x30 (client) / +0x30 (server, `CBaseEntity::m_CBodyComponent` = +48 = 0x30). |
| Controller fields | `m_nAnimationAlgorithm` +0x18, `m_nPrevAnimUpdateTick` (client +0xD4, server +0x80), `m_bLastUpdateSkipped` (client +0xCF, server +0x7B), `m_bNetworkedAnimationInputsChanged` (client +0xCD, server +0x79), `m_hSequence` (client +0xB0, server +0x5C), `m_flPlaybackRate` (client +0xC0, server +0x6C), `m_sAnimGraph2Identifier` (client +0x440, server +0x400), `m_pGraphInstanceAG2` (client +0x448, server +0x408) |
| The graph | `CBaseAnimGraph` (client size 4712, server 2624): `m_graphControllerManager` (client +4248, server +2128), `m_pMainGraphController` (client +4400, server +2280), `m_bAnimGraphUpdateEnabled` (client +4480, server +2360), `m_bAnimationUpdateScheduled` (client +4481, server +2361), `m_bShouldUpdateTransformations` (client +4610, server +2442) |
| The AG2 nodes (in `animationsystem.dll`, not in client.dll) | `CNmAimCSTask` — upper-body aim IK (spine→neck→head→weapon). `CAimMatrixUpdateNode` — the aim clamp + damp, `m_opFixedSettings` at +112. `AimMatrixOpFixedSettings_t`: `m_damping` +0x80, `m_flMaxYawAngle` +0xC4, `m_flMaxPitchAngle` +0xC8, `m_bUseBiasAndClamp` +0xD5, `m_flBiasAndClampYawOffset` +0xD8. `CAnimInputDamping`: `m_speedFunction` +8, `m_fSpeedScale` +12, `m_fFallingSpeedScale` +16. `CTurnHelperUpdateNode` (+`CTurnHelperInstanceData`: `m_turnAmount`, `m_turnStartTime`, `m_duration`) — the turn-in-place animation. `CAimCameraUpdateNode`. `CAnimMovement` (the NetworkMotion record: `endframe`, `motionflags`, `v0`, `v1`, `angle`, `vector`, `position`). `CSequenceGroupData`, `CSeqPoseParamDesc`, `CSeqAutoLayer`, `CSeqTransition`, `CSeqIKLock` (the animationsystem module, 47 classes). |
| What the reverser must establish | (a) **where the animgraph2 parameters are written** — the "writes animgraph2 parameters (move dir, aim yaw/pitch, speed…)" step; (b) **the order** relative to the animstate update; (c) **which parameters the server sets that the client does not** — this is the *server-only* list the spec asks for; (d) whether the AG2 evaluation on the server is driven by the same `CBaseAnimGraphController` path or a separate one. |
| Where the tuning constants live | *"The tuning numbers (max yaw, turn rate, damping) live in the compiled .vanim graph asset, not as constants in the DLL."* ⇒ the reverser will have to read the **.vanim asset** (or the graph definition resource) for the actual max-yaw / turn-rate values, or find them copied into a runtime struct. `snippet-only` |
| Validation | The upper-body yaw: with a known eye yaw, the head hitbox must land where the server puts it. `sv_showplayerhitboxes` on the server vs the client's ESP is the ground truth. |

### 6.3 A3 — the bone setup (this is where the hitboxes come from)

| What | Exact name / offset |
|---|---|
| The array | `CSkeletonInstance::m_modelState` (client +0x140, server +0x120) → `CModelState::m_pBoneArray` **+0x80** (= skeleton +0x1C0), stride **0x20**; each bone is a `CTransform {Vector pos; float scale; Quat xyzw}` with the rotation at **+0x10** (see this repo's `docs/re/aim.md`). `CModelState::m_hModel` +0xA0, `m_ModelName` +0xA8. |
| The hitbox set | `CSkeletonInstance::m_nHitboxSet` (client +0x3FC, server +0x3BC). The studio hitbox structs are in `docs/re/aim.md` (`m_vMinBounds`/`m_vMaxBounds`/`m_flShapeRadius`/`m_nGroupId`, capsule, per-mesh bone remap). |
| The client's bone-setup function | `C_BaseAnimating::SetupBones(matrix3x4a_t* out, int nMaxBones, int boneMask, float currentTime)` — the client build's version. The server's CS:GO twin was `CBaseAnimating::SetupBones(matrix3x4a_t*, int boneMask)`; in CS2 the server equivalent lives in `server.dll` (the `CBaseAnimGraph`/`CSkeletonInstance` path). **The reverser must find the CS2 server's bone-setup and diff it against the client's.** |
| The client-only passes to identify and neutralise | (i) the equivalent of `ShouldSkipAnimationFrame` (CS:GO: `game/client/c_baseanimating.cpp:2938`); (ii) the equivalent of `DoExtraBoneProcessing` → `DoProceduralFootPlant` (CS:GO: `game/client/cstrike15/c_cs_player.cpp:6536,6579`); (iii) the client's sequence/cycle interpolation and its LOD/`m_bLastUpdateSkipped` handling (`CBaseAnimGraphController::m_bLastUpdateSkipped`); (iv) the bone-cache invalidation that the client does per frame. |
| The known-good CS:GO reference | leaked `game/server/baseanimating.cpp:1899-2014` (the server's `SetupBones`) and `game/client/c_baseanimating.cpp:2983-3414` (the client's) — the *shape* of the diff to reproduce for CS2. |
| Validation | The **head** hitbox's world position must equal the server's to within the capsule radius. `sv_showplayerhitboxes` / `sv_showhitregistration` are the ground truth (the repo already has `sv_showplayerhitboxes` at `0x181bd0b40` per `docs/re/aim.md`). |

### 6.4 A4 — the tick boundary (where the fix has to be driven from)

| What | Why |
|---|---|
| The **command** is the tick. `CBaseUserCmdPB::client_tick` (`+0x54`), `legacy_command_number` (`+0x50`), and the controller's `m_nTickBase` (`+0x6B8`) — all documented in this repo's `docs/re/subtick.md` and `docs/re/prediction.md`. | The server animates once per **simulated tick**, i.e. once per command it processes. The client must re-simulate on the same boundary. |
| The hook point | The community's answer, three times over: *"do it manually for each tick (after createmove is my favorite)"*; *"call local animfix at the end of createmove"*; *"rather than FRAME_NET_UPDATE_END"*. Our `CreateMove` seam (slot 5 `0xb64e60`, slot 25 `0xd01610`, per `docs/re/client-dll.md`) is the equivalent. |
| The gate to defeat | The animstate's own re-update gate (`m_flLastUpdateTime == curtime || m_nLastUpdateFrame == framecount` in CS:GO; in CS2 the equivalent is whatever fields sit in the unmapped `+0x00..+0x17` region — **the reverser must identify them**). The community's workaround is `last_update = curtime - interval_per_tick` (UC 459231 post 3173373) or `last_framecount = framecount - 1` (Fatality `features/animations.cpp:1236-1237`; gucci `c_lagcompensation.cpp:202-206`). |
| The globals to fake | `gpGlobals->curtime` → the tick's time (`ticks_to_time(cmd_number)` in Fatality; `entity->get_simulation_time()` in gucci); `gpGlobals->frametime` → `interval_per_tick`; `gpGlobals->framecount` → force a re-update. Fatality also shows the *third* global: the command CRC (`m_pVerifiedCommands[seq % 150].m_crc`) is used to detect a *new* command. |
| The "send packet" refinement | *"only allow SetupBones on your local player when you send a packet, otherwise just use the previous bone cache"* — for a choking / fake-lag build, the tick boundary is the **packet** boundary, not the command boundary. Fatality implements the first half literally (`hooks/setup_bones_calls.cpp:4-21`, returns the cached `m_pBones` during prediction). |

### 6.5 The minimum viable sequence (what the implementation would do)

1. **At the tick boundary** (end of `CreateMove`, per the community consensus): snapshot the local pawn's animation state — the animstate struct, the AG2 controller's tick-dependent fields, the bone cache, the abs angles, the eye angles, the pose parameters.
2. **Re-run the animstate update** with the tick's eye yaw and the *server's* inputs, at `curtime = tick time`, `frametime = interval_per_tick`, with the update gate defeated.
3. **Re-run the AG2 evaluation** and the bone setup, with the client-only passes neutralised (frame-skip, foot plant, interpolation).
4. **Set the abs angles** to the animstate's foot yaw (CS2 equivalent of `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`).
5. **Restore** everything that the engine owns (the networked anim layers, the interpolation vars, the abs angles if the fix is only for the ESP/triggerbot).
6. **Consume** the resulting bone array exactly as the repo's `docs/re/aim.md` already does — `CSkeletonInstance::GetTransformsForHitboxList` (`0xaafca0`) and `CModel::GetBoneIndexForHitboxForMesh` (`0x18176cb40`).

> **Order matters and is the whole point.** The CS:GO order in §3.1 (rows 1→13) is the *server's* order; reproducing it in the client's order will produce a subtly different pose, because the server's `SetAbsAngles` at row 10 depends on the foot yaw produced at row 9.

---

## 7. Open questions

1. **The unmapped 176 bytes of `CCSPlayerAnimationState`** (+0x00..+0x17 and +0x48..+0xDF). Without them we cannot find the CS2 update gate (`last_update` / `last_framecount`) or the foot yaw. `read` gap.
2. **Does the server send the animstate, or do both sides compute it independently?** The schema gives the same struct at the same offset on both sides but carries no network metadata; the CS2 reversal thread describes the *client* computing it. If the server **sends** it, the entire "local animfix" collapses to "read the networked animstate" and the problem becomes an interpolation problem instead. **This is the single highest-value question in the document.** `[inference]` — the offsets and the thread's description point to independent computation, but the schema's silence is not proof.
3. **What are the "server-only" animgraph2 parameters?** The thread names the parameter *kinds* (move dir, aim yaw/pitch, speed) but not the parameter handles. Until the AG2 parameter list is enumerated, we cannot say which of them the client already fills correctly.
4. **The `.vanim` tuning constants** (max yaw, turn rate, damping) live in the asset, not the DLL. Reproducing the server's turn behaviour needs the asset parsed, or the runtime values located in a live struct.
5. **The CS2 equivalents of `DoExtraBoneProcessing` / `ShouldSkipAnimationFrame` / the client's cycle interpolation** — named only by analogy with CS:GO. The CS2 reverser must find the actual client-only bone passes.
6. **Does CS2 still have a `m_bClientSideAnimation`-style netvar?** The 2018 animfix depends on it; the CS2 reversal thread does not mention it. CS2OpenDev's `C_BaseEntity` has `m_bAnimatedEveryTick` (+0x548) and `m_bAnimTimeChanged` (+0x5E1), which may be the modern equivalents — unverified.
7. **The exact hitbox-affecting subset.** §3 marks the CS:GO layers that "move the hitboxes", but the mapping from animstate field → animgraph parameter → bone → hitbox is not established. We need the per-bone dependency: which bones does the aim matrix move (head, neck, spine_1..3, clavicle), and which the foot IK moves (`ankle_L`, `ankle_R`).
8. **Whether the AG2 evaluation is per-tick on the server at all.** *"The server animates players by ticks"* is a CS:GO statement; under AG2 the server may evaluate the graph per entity-tick with its own iteration counter (`m_nServerGraphInstanceIteration` / `m_nServerSerializationContextIteration` on the controller, client +1024/+1028, server +960/+964). Unverified.
9. **UC access.** UnknownCheats was under maintenance for the entire research window; the threads listed in §5.8 are known only by title and by search snippet. Re-running this note after the forum is back would likely change §4 and §5 substantially.

---

## Appendix A — Source inventory and access status

| # | Source | Access | Used for |
|---|---|---|---|
| S1 | UC 459231 "[Discuss] Best way to fix animations" — https://www.unknowncheats.me/forum/counterstrike-global-offensive/459231-fix-animations.html | **`read` (page 1 of 2)** | §2, §3.1, §4, §5.3 |
| S2 | UC 292406 "[Source] Sync Animations with Server (AnimFix)" — https://www.unknowncheats.me/forum/counterstrike-global-offensive/292406-sync-animations-server-animfix.html | **`read` (page 1 of 2)** | §2, §4, §5.2 |
| S3 | UC 758612 "[Release] Animation system reversal" (CS2) | `snippet-only` | §2, §3.2, §5.7, §6 |
| S4 | UC 524203 "[Question] delayed local animations" | `snippet-only` | §4 D5 |
| S5 | UC 422413 / 409760 / 443579 / 378531 / 375366 / 394914 / 415157 / 442832 / 455165 / 387519 / 371924 / 352712 / 313369 / 338932 / 290864 / 277614 / 348353 / 377736 / 377094 / 426383 / 459969 / 223459 / 292854 | `snippet-only` | §2, §4, §5.8 |
| S6 | Leaked CS:GO source — https://github.com/perilouswithadollarsign/cstrike15_src (`game/shared/cstrike15/csgo_playeranimstate.cpp`, `game/server/cstrike15/cs_player.cpp`, `game/client/cstrike15/c_cs_player.cpp`, `game/server/baseanimating.cpp`, `game/client/c_baseanimating.cpp`, `game/client/c_baseanimating.h`) | **`read`** | §3.1, §4, §6 |
| S7 | `click4dylan/CSGO_AnimationCode_Reversed` — `CCSGOPlayerAnimState_New.cpp` | **`read`** | §5.4 |
| S8 | `notphage/gucci` — `csgo/c_lagcompensation.cpp` | **`read`** | §2, §5.1 |
| S9 | `cacamelio/fatality-source-fixed` — `hooks/setup_bones_calls.cpp`, `features/animations.cpp`, `features/animations.h` | **`read`** | §2, §4, §5.5, §6.4 |
| S10 | `cacamelio/weave-v3-csgo-and-cs2` — `cs2/counterstrike2/sdk/classes/c_csgo_player_anim_graph_state.hpp`, `c_body_component_base_anim_graph.hpp`, `csgo/csgo_internal/sdk/animation_layer.hpp` | **`read`** | §2, §5.6 |
| S11 | CS2OpenDev schema — https://cs2opendev.github.io/CS2OpenDev-Docs/ (`cs2_schema.json`, build 25966348, 2026-10-15) | **`read`** | §3.2, §6 |
| S12 | `a2x/cs2-dumper` — `output/client_dll.hpp` (2026-10-15) | **`read`** | §3.2, §6.1 |
| S13 | Valve update note "[ ANIMATION ] … upgraded to AnimGraph2" (2025-07-28, SteamDB patch note 19388062 / Steam announcement) | `snippet-only` | §2, §3.2 |
| S14 | HLTV 44430 "AnimGraph2 goes live in latest CS2 update" (2026-04-20) | `snippet-only` | §2, §3.2 |
| S15 | This repository's own notes — `docs/re/aim.md`, `docs/re/lagcomp.md`, `docs/re/prediction.md`, `docs/re/subtick.md`, `docs/re/client-dll.md`, `docs/research/weave-cs2-ragebot.md`, `docs/research/fatality-csgo-algorithms.md` | **`read`** | §3.2, §6 (the hitbox/bone model, the tick model, the command model) |

### Sources that could not be read

| Source | Why |
|---|---|
| UnknownCheats (all of it) | The forum served a Cloudflare **"We'll be back shortly"** maintenance page (1176-byte body, HTTP 403 for the webfetch path) for the whole research window, from ~07:55 to the end of the session. S1 and S2 were fetched **before** the maintenance window and are complete for page 1; S3-S5 are `snippet-only`. The Wayback Machine refused the same URLs ("flagged it as suspected abusive bot traffic"). |
| Reddit `r/cs2` "Animgraph2 beta is better than an operation" (2026) | The post body is served by JavaScript; the readable excerpt used in §3.2 is a search snippet only. The snippet *"Until now, the server had no idea how …"* is **not** used as a claim here because it is truncated. |
| SteamDB patch note 19388062 | *"SteamDB is a community website that does not have an API"* — the page is behind a JS challenge. |
| `T1GxR/Axion-CS2-RAGE-CHEAT` (`cstrike/features/lagcomp/lagcomp.cpp`) | The file exists in the tree but is **0 bytes** — an empty placeholder. |
| `web.archive.org` | Rate-limited (HTTP 429) and then bot-flagged. |

### What was deliberately not done

- **No third-party binary was downloaded or executed.** Only public source text and public schema dumps were read.
- **No anti-cheat, detection-avoidance or VAC material** was collected or is present in this note. The scope is the animation *technique*.
