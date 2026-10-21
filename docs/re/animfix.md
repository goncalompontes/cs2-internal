# The animation pipeline and the local animfix

**Target.** The live `client.dll`
(sha256 `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`,
39 205 528 B), image base `0x180000000`. Every address below is an **RVA** in
`client.dll` unless another module is named. The server build
(`C:\Users\gpmpo\ghidra-analysis\server.dll`, sha256 `09e8c04c…`) is a
**different build**; its addresses are labelled `server-build (indicative)`.

> Every row carries its derivation (string → xref → decompile). Reproduce with
> `tools.rea.search_strings` → `tools.rea.xrefs` →
> `tools.rea.resolve_containing_procedure` → `tools.rea.procedure_pseudo_code`.
> The REA target id for this build was `b896e6c3-9686-4ba9-b764-bf5b551f8597`.

> ### §0 The web-research companion document
>
> The community-facing half of this task is
> **`docs/research/animfix.md`** on branch **`feat/research-animfix`**
> (commit `33651af`, "docs(research): animfix and local animfix"). It is
> *web research only* — no reversing — and it hands the engine half over in
> its §6 ("What we must reverse": A1 the animstate update, A2 AnimGraph2,
> A3 bone setup, A4 the tick boundary, plus the "also required" note about
> the CS2 equivalent of CS:GO's `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`).
>
> What it contributed to *this* document, and what it did not:
>
> * **Confirmed by the engine** (§3.1): the `CCSPlayerAnimationState` layout
>   and every one of its 16 schema offsets; the absolute breakpoint addresses
>   `ms+0x340` / `ms+0x33C`; the `CBaseAnimGraphController` field offsets
>   (`m_hSequence +0xb0`, `m_flPlaybackRate +0xc0`, `m_nPrevAnimUpdateTick
>   +0xd4`, `m_bLastUpdateSkipped +0xcf`,
>   `m_bNetworkedAnimationInputsChanged +0xcd`, `m_sAnimGraph2Identifier
>   +0x440`, `m_pGraphInstanceAG2 +0x448`); the 28 graph inputs and
>   `m_flAimYawAngle` at `controller+0x4e8`; the pointer
>   `C_BasePlayerPawn::m_pMovementServices` at `pawn+0x1330`.
> * **Corrected by the engine** (§6, §7): its `CreateMove` slot addresses
>   `0xb64e60` / `0xd01610` are the **stale** build-`25815307` values copied
>   from `docs/re/client-dll.md`; on the live build the slots are
>   **`0xb65de0`** (slot 5) and **`0xd02130`** (slot 25). Its
>   `m_pVerifiedCommands[seq % 150].m_crc` mechanism does **not exist** in
>   CS2's client. Its AG2 node list (`CAimMatrixUpdateNode`,
>   `CTurnHelperUpdateNode`, `AimMatrixOpFixedSettings_t`, `CAnimInputDamping`,
>   …) is **not present in `client.dll`** and there is **no
>   `animationsystem.dll`** in the game directory.
> * **Not verifiable here**: the `.vanim` tuning constants (they live in the
>   asset), the *server's* offsets (a different build), and the "server-only"
>   AG2 parameter list.
>
> Its D1–D11 divergence table is cross-referenced in §5; only the rows this
> document's own RE supports are kept.

> **There is no `CAnimationLayer` / `m_animLayers` in CS2.** The old Source 1
> layer array was replaced by **AnimGraph 2 (AG2)**: a per-entity *graph* whose
> inputs are 28 typed parameters, driven by one replicated *sequence layer*
> (`CBaseAnimGraphController`). Everything below is the AG2 pipeline; the
> "animation layers" of the brief map onto `CBaseAnimGraphController` +
> `CCS2PawnGraphController`.

---

## 1. TL;DR

**The pipeline, in five lines.**

1. **The per-command boundary** `FUN_180a72860` (the `runcommand:%04d,tick:%u`
   logger) saves the engine globals (`client+0x2230090`: curtime `+0x30`,
   frametime `+0x34`, tick count `+0x44`), sets `curtime = tick × 0.015625`
   and `frametime = 0.015625`, and runs the command; inside it the movement
   services' `ProcessCommand` (`0xa70de0`, `player_movementservices.cpp`)
   → `DoMovement` (`0xa65ae0`) → **`CCSPlayerAnimationState::Update`
   (`0x8a5d70`)** computes the move state, the aim yaw and the turn angle and
   finally calls the **abs-angle writer `0x22b470`** — the CS2
   `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`.
2. Every frame `CAnimGraphGameSystem::AnimTickUpdate` (`0x35aeb0`) queues
   `UpdateAnimGraph2Animations` (`0x38a390`) → the job `0x3552f0`, which
   evaluates the graph (`0x12e4f90`, `dt = ticks × 0.015625`) using the
   **28 graph inputs** of `CCS2PawnGraphController` — written both by the
   server's codec (`0x8d9740`) and by the client's own prediction
   (`CCS2PawnGraphController::ReflectPawnState`, `0x8e3540`).
3. The sequence layer (`CBaseAnimGraphController::m_hSequence +0xb0`,
   `m_flSeqStartTime +0xb4`, `m_flSeqFixedCycle +0xb8`, `m_flPlaybackRate
   +0xc0`), `m_flAnimTime` (`C_BaseEntity+0x3b4`) and
   `m_networkAnimTiming` (`CCSPlayer_WeaponServices+0x15c0`) are the
   replicated parts of the animation.
4. The pose reaches the skeleton in `CSkeletonInstance::SetupBones`
   (`0xaa0f80`), which writes the bone array at `CSkeletonInstance + 0x1C0`
   (= `CModelState + 0x80`) as 0x20-byte `CTransform`s (position `+0x00`,
   scale `+0x0C`, **rotation `+0x10`**).
5. The hitbox transforms are derived from *that same array* on demand by
   `CSkeletonInstance::GetTransformsForHitboxList` (`0xab0b80`) — there is no
   second, separate hitbox-transform cache on the client.

**Can a local animfix work on this build?** *Yes* — the pose is computed
entirely on the client and the hitbox transforms are re-derived from the
client's own bone array, so overriding the anim state (`0x8a5d70`), the graph
inputs, or the bones themselves moves the local player's hitboxes; the
override points that matter (`0x8a5d70` — 14 bytes, `0x22b470` — 16 bytes,
`0xaa0f80` — 15 bytes, `0x35aeb0` — 15 bytes, `0x8d9740` — 14 bytes) are
all **hookable** with the repository's inline relocator, unlike the
semantically "ideal" one (`ReflectPawnState`, `0x8e3540`, a 20-byte
prologue).

---

## 2. The pipeline (client side)

Ordered from the network snapshot to the hitbox transform.

| # | Stage | Function | RVA | Signature | Reads | Writes |
|---|---|---|---|---|---|---|
| 0a | **The per-command boundary** | the client's `RunCommand` | `0xa72860` | `void RunCommand(CPlayer_MovementServices* this, CUserCmd* cmd)` | the command (`cmd+0x8` = the command number), the globals | the globals (`client+0x2230090`: `curtime +0x30`, `frametime +0x34`, `tickcount +0x44`) are set to `tick × 0.015625` / `0.015625` / `tick` and **restored** afterwards |
| 0b | **The movement processing** | `CPlayer_MovementServices::ProcessCommand` → `DoMovement` | `0xa70de0` → `0xa65ae0` | `void ProcessCommand(CPlayer_MovementServices*, CUserCmd*)` (the source string is `"OnPawnProcessCommand"`, `player_movementservices.cpp:1849`) | the command, the pawn | the pawn's movement state; it fakes the globals a second time (a **sub-tick** `frametime`) |
| 0c | **The anim state update** | **`CCSPlayerAnimationState::Update`** | **`0x8a5d70`** | `void CCSPlayerAnimationState::Update(CCSPlayerAnimationState* this)` | the entity (`this+0x10`), the eye angles, the velocity | the anim state's fields (see §3) |
| 0d | **The abs-angle write** (the CS2 `SetAbsAngles`) | the angle writer | **`0x22b470`** | `void SetAbsAngles(C_BaseEntity* entity, const QAngle* angles)` | the scene node (`entity+0x330`) | the scene node's angles; it also **invalidates the bone cache** (`0x9d09b0`) |
| 0 | Snapshot decode | the per-class netvar codec (`CNetworkVarChainer` lookups by name hash) | — (per class) | `void Codec(CClass* dst, CClass* src, CSchemaTable*)` | the entity snapshot | every replicated netvar (`m_flAnimTime`, the velocities, `m_angEyeAngles`, `m_networkAnimTiming`, the sequence layer …) |
| 1 | Anim tick driver | `CAnimGraphGameSystem::AnimTickUpdate` | `0x35aeb0` | `void CAnimGraphGameSystem::AnimTickUpdate(CAnimGraphGameSystem* this, int clientTick)` | the entity list, the anim time | the per-frame update list (`this+0x10110`), the frame clock (`this+0xe4`) |
| 2 | Build the AG2 work list | `CAnimGraphGameSystem::UpdateAnimGraph2Animations` | `0x38a390` | `void CAnimGraphGameSystem::UpdateAnimGraph2Animations(CAnimGraphGameSystem* this, int* list)` | the update list | the job list (`this+0x10110`, 0x60-byte entries) and the job named `"UpdateAnimGraph2Animations"` |
| 3 | Job executor | the AG2 evaluate job | `0x3552f0` | `void Job(CAnimGraphGameSystem** ctx)` | the job list | per entity: calls `0x12e4f90`, `0x12e52b0`, then the entity virtual `+0x898` |
| 4 | Graph evaluation | AG2 instance update | `0x12e4f90` | `void AG2Update(AG2Instance* this, …, float dt, …)` | the graph inputs, the sequence layer, `dt` | the evaluated pose (task outputs) |
| 4b | Pose commit | AG2 pose-recipe serialisation | `0x12e52b0` → `0x13046e0` | `void CommitPose(CBaseAnimGraphController* this, …)` / `void SerializePoseRecipe(AG2Instance* this, int, …)` | the evaluated pose | the serialized pose recipe on the graph instance (`controller+0x108` is the graph instance; the recipe lives at `instance+0x11d0`) |
| 5 | Pawn state → graph inputs (the **client's prediction**) | `CCS2PawnGraphController::ReflectPawnState` | `0x8e3540` | `void CCS2PawnGraphController::ReflectPawnState(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn)` | the pawn (`m_bIsDefusing` `+0x1e9a`, the scene-node origin, the eye angles) and the anim state | **the 28 graph-input slots** `this+0x2d8 … this+0x560` (stride `0x18`) |
| 5a | aim/angles → inputs | (helper of 5) | `0x8e2f70` | `void ReflectAim(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn, void* animState)` | the eye-angle pair at `animState+0x3d4/0x3d8`, the scene origin | `aim_angle_pitch` (`+0x4d0`), `aim_angle_yaw` (`+0x4e8`) (+2 slots) |
| 5b | move state → inputs | (helper of 5) | `0x8e3200` | `void ReflectMove(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn, void* animState)` | the anim-state enums at `animState+0x18/0x19/0x1a/0x1b`, the move speeds, the ladder/foot/flinch values | the remaining graph-input slots |
| 6 | Graph-input binding | `CCS2PawnGraphController` param binding | `0x8c8a80` | `void CCS2PawnGraphController::BindParams(CCS2PawnGraphController* this)` | the graph definition | 28 parameter bindings at `this+0x2d8 … +0x560` |
| 7 | Snapshot codec of the graph inputs | the controller's netvar codec (two mirrored functions) | `0x8d9740` / `0x8daab0` | `void Codec(CCS2PawnGraphController* dst, CClass* src, CSchemaTable*)` | the entity snapshot | **the same 28 graph-input slots** (the *server's* values) |
| 8 | The sequence layer (the CS2 "anim layer") | `CBaseAnimGraphController` (embedded, `+0x510` in `CBodyComponentBaseAnimGraph`) | — (data) | — | the netvars | the sequence/loop/playback state |
| 9 | Pose → bones | `CSkeletonInstance::SetupBones` | `0xaa0f80` | `void CSkeletonInstance::SetupBones(CSkeletonInstance* this, uint boneFlags)` | the model (`this+0x1e0`), the anim decode list (`this+0x208`), the bone flags (`this+0x450/0x458`) | **the bone array at `this+0x1c0`** (`bone*0x20`, quaternion at `+0x10`) |
| 10 | Bone-cache invalidation | `CSkeletonInstance::InvalidateBones` | `0x9d09b0` | `void CSkeletonInstance::InvalidateBones(CSkeletonInstance* this)` | the model state (`this+0xc4..0xf8`) | clears the dirty flag `this+0x110`, resets the cached origin/angles (`this+0x10..0x2c`) |
| 11 | Hitbox transforms | `CSkeletonInstance::GetTransformsForHitboxList` | `0xab0b80` | `void CSkeletonInstance::GetTransformsForHitboxList(CSkeletonInstance* this, CTransform* out, int* hitboxSet)` | **the bone array `this+0x1c0`**, the model `this+0x1e0`, the hitbox set `this+0x3fc` | the caller's transform array (`out+0x10`, stride `0x20`) |
| 12 | Post-networking decode | `CAnimGraphGameSystem::ProcessPostNetworkingAnimationDecodes` | `0x37eb50` | `void CAnimGraphGameSystem::ProcessPostNetworkingAnimationDecodes(CAnimGraphGameSystem* this)` | the decoded anim payloads | the per-entity anim decode lists |

### Derivations

* **Stage 0a** — `search_strings "runcommand:%04d,tick:%u"` → `0x181c3b4c8`;
  xref → `FUN_180a72860`. The body logs the command number (`param_2+8`) and
  the tick, then saves and restores the globals at `PTR_DAT_182230090`
  (= `client + 0x2230090`): `+0x30` = curtime (set to `tick × 0.015625`),
  `+0x34` = frametime (set to `0x3c800000` = 0.015625), `+0x44` = the tick
  count, `+0x50` / `+0x58` = two more saved words.
* **Stage 0b** — `FUN_180a70de0` carries the source string
  `"OnPawnProcessCommand"`; `FUN_180a65ae0` carries `"DoMovement"` and
  `game/shared/player_movementservices.cpp:1849`.
* **Stage 0c** — `FUN_1808cb710` (the movement services' per-command handler)
  contains the single call `FUN_1808a5d70(param_1 + 0x310)` — **the anim
  state is at `+0x310` and the update takes a pointer to it**. It was found
  by the byte pattern `48 8D 8B 10 03 00 00` (`lea rcx,[rbx+0x310]`), which
  has nine hits in the module; the one inside the player region is
  `0x1808cb767`.
* **Stage 0d** — `FUN_18022b470` is called at the **end** of
  `FUN_1808a5d70` with the angle built from the anim state's turn angle
  (`this+0xd8`); its body reads the scene node (`entity+0x330`) and calls
  `FUN_1809d09b0` (the bone-cache invalidation) when the scene node's
  dirty flag (`+0x110`) is set. **This is CS2's equivalent of CS:GO's
  `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`** — see §4.
* **Stage 1** — `search_strings "AnimTickUpdate"` → `0x181b039a8`; xrefs
  `0x18035af07/0x18035af10/0x18035af35` → `FUN_18035aeb0`. The same function
  references `"PreUpdateAnimGraphAnimations"` (`0x181b03a60`),
  `"CAnimGraphGameSystem::PreUpdateAnimGraphAnimations"` (`0x181b03a80`),
  `"AnimGraph Client Tick"` / `"AnimGraph Server Tick"` (`0x181b03a28` /
  `0x181b03a10`). Its body calls `0x38a390` (stage 2) and then the per-entity
  virtuals `+0x888` / `+0x890`.
* **Stage 2** — `search_strings "UpdateAnimGraph2Animations"` → `0x181b03dc8`;
  xref → `FUN_18038a390`. The function copies the literal
  `"UpdateAnimGraph2Animations"` into the job object.
* **Stage 3** — `search_strings "AG2 - EvaluateAnimList - Job Execute Func"` →
  `0x181b03d98`; xref → `FUN_1803552f0`. It computes `dt = iVar4 * 0.015625`
  and calls `0x12e4f90`, `0x12e52b0` and the entity virtual `+0x898`.
* **Stage 5** — `search_strings "ReflectPawnState"` → `0x181c12238`, and
  `"CCS2PawnGraphController::ReflectPawnState"` → `0x181c122b0`; both xrefs
  land in `FUN_1808e3540`, whose VProf scope names the source file
  `game/shared/cstrike15/cs_player_pawngraphcontroller.cpp:122`.
* **Stage 5b** — `FUN_1808e3540` ends with `FUN_1808e2f70(...)` and
  `FUN_1808e3200(...)`. `FUN_1808e3200` reads the byte at `animState+0x328`
  and indexes the enum table `DAT_182532910` with it — the table built by
  `FUN_1800b05e0` as `move_type_ground / move_type_air / move_type_ladder`;
  the same function reads `animState+0x329` (`ground_action_*`),
  `animState+0x32a`, `animState+0x32b` (`air_action_jump/start_fall/land`,
  table `DAT_182532968`).
* **Stage 6** — `search_strings "Bindings for CCS2PawnGraphController"` →
  `0x181c12528`; xref → `FUN_1808c8a80`, which calls the three binder helpers
  at `this+0x2d8, 0x2f0, 0x308, 0x320, 0x338, 0x350, 0x368, 0x380, 0x398, 0x3b0,
  0x3c8, 0x3e0, 0x3f8, 0x410, 0x428, 0x440, 0x458, 0x470, 0x488, 0x4a0, 0x4b8,
  0x4d0, 0x4e8, 0x500, 0x518, 0x530, 0x548, 0x560` — 28 slots, stride `0x18`.
* **Stage 7** — `search_strings "m_flAimYawAngle"` → `0x181c0ee70`; its only
  xrefs are `0x1808d9740` and `0x1808daab0` — the two controller codecs.
  Their field names (`m_moveType`, `m_flMoveSpeedHorizontal`, `m_flCrouchAmount`,
  `m_flAimPitchAngle`, `m_flAimYawAngle`, `m_flinchHead`, …) and their
  destination offsets (see the table in §3) are the controller's own slots →
  **the server replicates the graph inputs**.
* **Stage 9** — `search_strings "CSkeletonInstance::AddAnimationDecode"` →
  `0x181c42c08` and `"CalcAnimationState"` → `0x181c42c30`; both xrefs land in
  `FUN_180aa0690` (`skeletoninstance.cpp`, `CalcAnimationState` at line 0xd5b,
  `AddAnimationDecode` at line 0xbf2). The bone writer is the sibling
  `FUN_180aa0f80`; its body holds `local_4240 = *(longlong *)(param_1 + 0x1c0)`
  and writes `((longlong)iVar17 * 0x20 + local_4240)` and
  `(uVar35 + 0x10 + local_4240)` → the bone array and the quaternion slot.
* **Stage 10** — `FUN_1809d09b0` is called from `FUN_180aa0f80` and
  `FUN_180ab0b80` under `if (*(char *)(param_1 + 0x110) != '\0')`; it clears
  `param_1+0x110` at the end.
* **Stage 11** — `search_strings "CSkeletonInstance::GetTransformsForHitboxList"`
  → `0x181c42f68`; xrefs `0x180ab0c06/0x180ab0c0d` → `FUN_180ab0b80`. Its body
  reads `*(longlong *)(param_1 + 0x1c0)`, indexes `iVar6 * 0x20 + lVar1` and
  writes `lVar9 * 0x20 + *(longlong *)(param_2 + 0x10)`.
* **Stage 12** — `search_strings "CAnimGraphGameSystem::ProcessPostNetworkingAnimationDecodes()"`
  → `0x181b03cf0`; xref → `FUN_18037eb50`.

### The animation "layers" (there is no `m_animLayers`)

`search_strings "m_animLayers"` → **0 hits**; `"CAnimationLayer"` → **0 hits**.
The CS2 equivalent is the AG2 *sequence layer* inside
`CBaseAnimGraphController`:

| Field | Offset (inside the controller) | Networked? | Evidence |
|---|---|---|---|
| `m_nAnimationAlgorithm` | `+0x18` | yes | `CNetworkVarBase@…NetworkVar_m_nAnimationAlgorithm@CBaseAnimGraphController@@` (RTTI `0x1822d2d30`) |
| `m_vecSecondarySkeletons` | `+0x38` | yes | RTTI `0x1822d2f10` |
| `m_nSecondarySkeletonMasterCount` | `+0x50` | yes | RTTI `0x1822d3070` |
| **`m_hSequence`** | **`+0xb0`** | yes | RTTI `0x1822d30e0` |
| **`m_flSeqStartTime`** | **`+0xb4`** | yes | RTTI `0x1822d3170` |
| **`m_flSeqFixedCycle`** | **`+0xb8`** | yes | RTTI `0x1822d31e0` |
| **`m_nAnimLoopMode`** | **`+0xbc`** | yes | RTTI `0x1822d3250` |
| **`m_flPlaybackRate`** | **`+0xc0`** | yes | RTTI `0x1822d32f0` |
| `m_hGraphDefinitionAG2` | `+0x370` | yes | RTTI `0x1822d33a0` |
| `m_primaryGraphId` | `+0x408` | yes | RTTI `0x1822d3840` |
| `m_nPrevAnimUpdateTick` | `+0xd4` | client | the schema table `FUN_180432220` |
| `m_bNetworkedAnimationInputsChanged` | `+0xcd` | **net** | `FUN_180461b90`: `param_1 + 0xcd`; RTTI `m_bNetworkedAnimationInputsChanged @CBaseAnimGraph` |
| `m_bLastUpdateSkipped` | `+0xcf` | client | `FUN_180461b90`: `param_1 + 0xcf` |
| `m_sAnimGraph2Identifier` | `+0x440` | **net** | `FUN_180461b90`: `param_1 + 0x440`; RTTI `m_sAnimGraph2Identifier @CBaseAnimGraph` |
| `m_pGraphInstanceAG2` | `+0x448` | client | `FUN_180461b90`; in the client's usage the graph instance is reached through `controller+0x108` (`FUN_1812e52b0` passes `*(this+0x108)` to the pose-recipe serialiser) |
| `m_nNotifyState` / `m_bNetworkedSequenceChanged` | `+0xcc` / `+0xce` | net | `FUN_180461b90` (not in the research document) |

The controller itself is embedded in the pawn's body component:
`CBodyComponentBaseAnimGraph::m_animationController` at **`+0x510`**
(size `0x6a0`); the component's size is `0xbb0`.

Derivation: `FUN_18044da50` (the `CBaseAnimGraphController` schema
registration) prints `cls: CBaseAnimGraphController size=0x6a0` and the field
list above; `FUN_180450670` prints
`m_animationController | CBaseAnimGraphController | sz=0x6a0 | off=0x510` for
`CBodyComponentBaseAnimGraph` (size `0xbb0`).

### The 28 graph inputs (the AG2 "layer inputs")

The AG2 parameter names sit next to the controller class in the binary
(`0x181c122e0 … 0x181c12510`): `is_defusing`, `move_direction_id`,
`move_speed_x`, `move_speed_y`, `move_speed_horizontal`,
`move_speed_horizontal_previous`, `move_crouch_amount`, `move_is_walking`,
`weapon_drop_amount`, `ground_action`, `ground_action_direction_id`,
`ground_turn_angle_or_velocity`, `ladder_cycle`, `ladder_angle_yaw`,
`ladder_angle_yaw_backwards`, `air_action`, `air_height_above_ground`,
`ik_left_foot`, `ik_right_foot`, `flashed_amount`, `aim_angle_pitch`,
`aim_angle_yaw`, `flinch_head_type`, `flinch_head_restart`,
`flinch_body_type`, `flinch_body_restart`, `flinch_is_on_fire`.

The controller stores each of them in an **0x18-byte slot**; the slot offsets
(and the member names the codec uses) are:

| slot | member (codec name) | graph parameter |
|---|---|---|
| `+0x2d8` | `m_bIsDefusing` | `is_defusing` |
| `+0x2f0` | `m_moveType` | (derived) |
| `+0x308` | `m_moveDirectionID` | `move_direction_id` |
| `+0x320` | `m_flMoveSpeedX` | `move_speed_x` |
| `+0x338` | `m_flMoveSpeedY` | `move_speed_y` |
| `+0x350` | `m_flMoveSpeedHorizontal` | `move_speed_horizontal` |
| `+0x368` | `m_flPreviousMoveSpeedHorizontal` | `move_speed_horizontal_previous` |
| `+0x380` | `m_flCrouchAmount` | `move_crouch_amount` |
| `+0x398` | `m_bIsWalking` | `move_is_walking` |
| `+0x3b0` | `m_flWeaponDropAmount` | `weapon_drop_amount` |
| `+0x3c8` | `m_groundAction` | `ground_action` |
| `+0x3e0` | `m_groundActionDirectionID` | `ground_action_direction_id` |
| `+0x3f8` | `m_flGroundTurnAngleOrVelocity` | `ground_turn_angle_or_velocity` |
| `+0x410` | `m_flLadderCycle` | `ladder_cycle` |
| `+0x428` | `m_flLadderYaw` | `ladder_angle_yaw` |
| `+0x440` | `m_flLadderYawBackwards` | `ladder_angle_yaw_backwards` |
| `+0x458` | `m_airAction` | `air_action` |
| `+0x470` | `m_flAirHeightAboveGround` | `air_height_above_ground` |
| `+0x488` | `m_leftFootTarget` | `ik_left_foot` |
| `+0x4a0` | `m_rightFootTarget` | `ik_right_foot` |
| `+0x4b8` | `m_flFlashedAmount` | `flashed_amount` |
| `+0x4d0` | `m_flAimPitchAngle` | `aim_angle_pitch` |
| `+0x4e8` | `m_flAimYawAngle` | `aim_angle_yaw` |
| `+0x500` | `m_flinchHead` | `flinch_head_type` |
| `+0x518` | `m_flinchHeadRestart` | `flinch_head_restart` |
| `+0x530` | `m_flinchBody` | `flinch_body_type` |
| `+0x548` | `m_flinchBodyRestart` | `flinch_body_restart` |
| `+0x560` | `m_flinchIsOnFire` | `flinch_is_on_fire` |

The slot offsets come from the binder `0x8c8a80` (which creates all 28) and
the codec `0x8d9740` (which writes, among others, `m_flAimPitchAngle` →
`param_1+0x4d0` and `m_flAimYawAngle` → `param_1+0x4e8`). The member↔parameter
pairing is proven for the entries the codec names explicitly; the rest follow
the parameter-name order, which matches the slot order one-for-one.

---

## 3. The fields

`net` = the value is **replicated by the server**; `client` = **computed on
the client**. The marker used here is the presence of a
`CNetworkVarBase<…NetworkVar_<field>@<Class>…>` RTTI entry (there are 1579
such strings in this build) **or** the field's appearance in a per-class
snapshot codec. Both are strong markers; the RTTI inventory is *not* complete
for wrapped/interpolated network vars (e.g. `m_angEyeAngles` is a
`CWrappedInterpolatedVar<QAngle>` and has no `CNetworkVarBase` RTTI).

| Field | Class | Offset | net / client | Evidence |
|---|---|---|---|---|
| `m_flAnimTime` | `C_BaseEntity` | `+0x3b4` | **net** | RTTI `0x1822d4ae0`; schema registration `FUN_180451d30` (`m_flAnimTime \| float32 \| sz=4 \| off=0x3b4`); the codec `FUN_1804328e0` |
| `m_flSimulationTime` | `C_BaseEntity` | `+0x3b8` | **net** | RTTI `m_flSimulationTime @C_BaseEntity`; `FUN_180451d30` |
| `m_vecServerVelocity` | `C_BaseEntity` | `+0x404` (0x28 B) | **net** | `FUN_180451d30`: `m_vecServerVelocity \| CNetworkVelocityVector \| sz=0x28 \| off=0x404` |
| `m_vecAbsVelocity` / `m_vecVelocity` | `C_BaseEntity` | `+0x3f8` / `+0x430` | net | the `C_BaseEntity` field table `FUN_1804328e0` |
| `m_fFlags` | `C_BaseEntity` | `+0x3f4` | **net** | RTTI `m_fFlags @C_BaseEntity` |
| `m_MoveType` | `C_BaseEntity` | `+0x525` | **net** | RTTI `0x1822d4fe0`; `FUN_180451d30` |
| `m_flTimeScale` / `m_flGravityScale` | `C_BaseEntity` | `+0x544` / `+0x540` | **net** | RTTI `m_flTimeScale @C_BaseEntity`; `FUN_180451d30` |
| `m_bAnimatedEveryTick` | `C_BaseEntity` | — | **net** | RTTI `m_bAnimatedEveryTick @C_BaseEntity` |
| `m_angEyeAngles` | `C_CSPlayerPawn` | `+0x3600` (`CWrappedInterpolatedVar<QAngle>`, 0x90 B) | **net, interpolated** | the `C_CSPlayerPawn` field table `FUN_180c56fe0` (`m_angEyeAngles@0x3600`, `m_arrOldEyeAnglesTimes@0x3690`, `m_arrOldEyeAngles@0x36a0`, `m_angEyeAnglesVelocity@0x36d0`) |
| `m_qDeathEyeAngles` | `C_CSPlayerPawn` | `+0x1da8` | **net** | RTTI `m_qDeathEyeAngles @C_CSPlayerPawn`; `FUN_180c7fa90` |
| `m_ServerViewAngleChanges` | `C_BasePlayerPawn` | — | **net** | RTTI `m_ServerViewAngleChanges @C_BasePlayerPawn`; xrefs `0x180439880`, `0x180457c40`, `0x180468cd0` — the server's *view-angle deltas* |
| **`m_AnimationState`** | **`CCSPlayer_MovementServices`** | **`+0x310`** (an **embedded** struct, size `0xe0`) | **client** | the `CCSPlayer_MovementServices` field table `FUN_1808bbe30` (`m_AnimationState@0x310`, type code 10, size `0xe0`); the string is referenced **only** by that table (plus a data pointer at `0x1822217e0`) — no codec ever encodes it |
| `m_currentMoveType` | `CCSPlayerAnimationState` | `+0x18` (abs. `ms+0x328`) | **client** | the `CCSPlayerAnimationState` field table `FUN_18087d320`; no codec, no netvar RTTI |
| `m_groundMoveState` | `CCSPlayerAnimationState` | `+0x19` | **client** | idem |
| `m_groundActionDirection` | `CCSPlayerAnimationState` | `+0x1a` | **client** | idem |
| `m_airAction` | `CCSPlayerAnimationState` | `+0x1b` | **client** | idem |
| `m_bWasOnGroundLastUpdate` | `CCSPlayerAnimationState` | `+0x1c` | **client** | idem |
| `m_bWasStationaryLastUpdate` | `CCSPlayerAnimationState` | `+0x1d` | **client** | idem |
| *(two bytes with no schema field)* | `CCSPlayerAnimationState` | `+0x1e`, `+0x1f` | client | the gap between `m_bWasStationaryLastUpdate` (`+0x1d`) and `m_actionStartTick` (`+0x20`) in `FUN_18087d320`; the constructor `FUN_1804d9550` initialises `ms+0x328` = 0 and `ms+0x32c` = 1 |
| `m_actionStartTick` | `CCSPlayerAnimationState` | `+0x20` | **client** | `FUN_18087d320` |
| `m_staticAimTimerStartTick` | `CCSPlayerAnimationState` | `+0x24` | **client** | idem |
| `m_plantAndTurnStartTick` | `CCSPlayerAnimationState` | `+0x28` | **client** | idem |
| **`m_flTurnOnSpotAngle`** | `CCSPlayerAnimationState` | `+0x2c` | **client** | idem — the body-yaw accumulator |
| **`m_flPreviousAimYaw`** | `CCSPlayerAnimationState` | `+0x30` | **client** | idem — the aim-yaw accumulator |
| `m_flPreviousHorizontalSpeed` | `CCSPlayerAnimationState` | `+0x34` | **client** | idem |
| `m_flFootIKOffsetLeft` / `…Right` | `CCSPlayerAnimationState` | `+0x38` / `+0x3c` | **client** | idem |
| `m_flWeaponDropPercentageDueToMovement` | `CCSPlayerAnimationState` | `+0x40` | **client** | idem |
| `m_flWeaponDropSmoothDampVelocity` | `CCSPlayerAnimationState` | `+0x44` | **client** | idem |
| *(non-schema members)* | `CCSPlayerAnimationState` | `+0xb8` (the turn **rate**), `+0xc4` (the aim yaw), `+0xc8` (the aim pitch), `+0xcc` (the smoothed yaw), `+0xd0`, `+0xd4` (the yaw delta), `+0xd8` (the **turn-on-spot angle** the engine applies) | **client** | the class is `0xe0` bytes (`FUN_1808bbe30`: type code 10, size `0xe0`) but its schema fields stop at `+0x44`; `FUN_1808a5d70` writes `+0xc4` (the summed aim yaw), `+0xc8` (the pitch), `+0xcc`/`+0xd0`/`+0xd4` (the smoothed yaw and its delta) and `+0xd8` (the turn angle); `FUN_18088c730` accumulates the turn angle into `+0xd8` and writes the turn rate to `+0xb8`; `FUN_1808e2f70` reads the eye angles at `animState+0x3d4/0x3d8` (= `+0xc4/+0xc8`) and `FUN_1808e3200` reads `animState+0x3c8` (= `+0xb8`) |
| `m_AnimationState` (the pointer) | `C_CSPlayerPawn` | `+0x34f8` → the `CCS2PawnGraphController*` | client | `FUN_180c905e0`: `FUN_1808e3540(*(longlong *)(param_1 + 0x34f8), param_1)` |
| `m_flDuckAmount` | `CCSPlayer_MovementServices` | `+0x40c` | **net** | RTTI `0x1822eed80`; `FUN_1808d2110`; the codec `0x1808c6660` |
| `m_flDuckSpeed` | `CCSPlayer_MovementServices` | `+0x410` | **net** | RTTI `0x1822eedf0` |
| `m_flDuckRootOffset` / `m_flDuckViewOffset` | `CCSPlayer_MovementServices` | `+0x418` / `+0x41c` | **net** | RTTI `m_flDuckRootOffset @CCSPlayer_MovementServices` |
| `m_bDucked` / `m_bDucking` / `m_bDesiresDuck` / `m_bDuckOverride` | `CCSPlayer_MovementServices` | `+0x408` / `+0x416` / `+0x415` / `+0x414` | **net** | RTTI for each |
| `m_flStamina` | `CCSPlayer_MovementServices` | `+0x694` | **net** | RTTI `0x1822ef3b0` |
| `m_flFrictionStashedSpeed` | `CCSPlayer_MovementServices` | `+0x690` | **net** | RTTI `m_flFrictionStashedSpeed @CCSPlayer_MovementServices` |
| `m_bHasEverProcessedCommand` | `CCSPlayer_MovementServices` | `+0xfd0` | client | `FUN_1808bbe30`; no netvar RTTI |
| **`m_networkAnimTiming`** | **`CCSPlayer_WeaponServices`** | **`+0x15c0`** (`CNetworkUtlVectorBase<uint8>`, 0x18 B) | **net** | the schema registration `FUN_180909930`: `m_networkAnimTiming \| CNetworkUtlVectorBase< uint8, NetworkVar_m_networkAnimTiming , -1, int > \| sz=0x18 \| off=0x15c0`; RTTI `0x1822effe0` |
| `m_bBlockInspectUntilNextGraphUpdate` | `CCSPlayer_WeaponServices` | `+0x15d8` | **net** | RTTI `m_bBlockInspectUntilNextGraphUpdate @CCSPlayer_WeaponServices` |
| `m_nHitboxSet` | `CSkeletonInstance` | `+0x3fc` | **net** | RTTI `0x1822d97a0`; read by `FUN_180ab0b80` |
| `m_modelState` | `CSkeletonInstance` | `+0x140` | net | RTTI `m_modelState @CSkeletonInstance`; implied by `FUN_180ab0b80` (bone array `+0x1c0` = `modelState+0x80`, model handle `+0x1e0` = `modelState+0xa0`) |
| `m_nAnimStateNoInterpSerialNumber` | `CModelState` | `+0x200` | **net** | RTTI `0x1822d9460`; the `CModelState` field table `FUN_1804d73c0` |
| `m_flRootBoneOffset_x/y/z` | `CModelState` | `+0xe8/+0xec/+0xf0` | **net** | RTTI `m_flRootBoneOffset_x @CModelState` |
| **`m_pBoneArray`** | `CModelState` | **`+0x80`** (= `CSkeletonInstance+0x1c0`) | client | the bone array is *not* a netvar; the pointer is read by `FUN_180ab0b80` and written by `FUN_180aa0f80` |
| the 28 graph inputs | `CCS2PawnGraphController` | slots `+0x2d8 … +0x560` (stride `0x18`) | **net + client-predicted** | the codec `0x8d9740`/`0x8daab0` writes the *server's* values into them; `0x8e3540`/`0x8e2f70`/`0x8e3200` write the *client's prediction* into the same slots |
| the bone transforms themselves | `CModelState::m_pBoneArray` | stride `0x20`, quat `+0x10` | client | `FUN_180aa0f80` |
| the hitbox transforms | (transient) | `CTransform`, stride `0x20` | client | `FUN_180ab0b80` writes into the caller's buffer |

### §3.1 The research document's fields, checked against the engine

Every row of the research's §6.1 / §6.2 tables, re-derived on **this** build
(`client.dll` sha256 `659940c1…`). "confirmed" = the engine shows the same
value; "corrected" = the engine shows something else; "unverified" = the
engine does not show it.

| research claim | engine | verdict |
|---|---|---|
| `CCSPlayer_MovementServices::m_AnimationState` at `+0x310`, type `CCSPlayerAnimationState`, size **224** | `FUN_1808bbe30`: `m_AnimationState@0x310`, type code 10 (a class), **size `0xe0` = 224** | **confirmed** |
| `m_currentMoveType +0x18`, `m_groundMoveState +0x19`, `m_groundActionDirection +0x1A`, `m_airAction +0x1B`, `m_bWasOnGroundLastUpdate +0x1C`, `m_bWasStationaryLastUpdate +0x1D` | `FUN_18087d320` (the class's schema field table) | **confirmed** |
| `m_actionStartTick +0x20`, `m_staticAimTimerStartTick +0x24`, `m_plantAndTurnStartTick +0x28` | idem | **confirmed** |
| `m_flTurnOnSpotAngle +0x2C`, `m_flPreviousAimYaw +0x30`, `m_flPreviousHorizontalSpeed +0x34` | idem | **confirmed** (the offsets) — but see the note below |
| `m_flFootIKOffsetLeft/Right +0x38/+0x3C`, `m_flWeaponDropPercentageDueToMovement +0x40`, `m_flWeaponDropSmoothDampVelocity +0x44` | idem | **confirmed** |
| the write-breakpoint addresses `ms + 0x340` (m_flPreviousAimYaw) and `ms + 0x33C` (m_flTurnOnSpotAngle) | `0x310 + 0x30 = 0x340` and `0x310 + 0x2C = 0x33C`; and `FUN_1808e3200`/`FUN_1808e2f70` read exactly `ms+0x328`, `ms+0x329`, `ms+0x33c`, `ms+0x340`, `ms+0x3d4/0x3d8` | **confirmed** |
| the animstate update is "not name-tagged in the IDB" and is reached "through the movement-services tree" | correct — it has no name string; but it **is** findable: `FUN_1808a5d70`, called as `FUN_1808a5d70(ms + 0x310)` from `FUN_1808cb710` | **confirmed, and now located** (§2 stage 0c) |
| the unmapped region `+0x00..+0x17` and `+0x48..+0xDF` | `+0x08` = a `CUtlString` (the constructor purges it); `+0x18..+0x44` = the schema fields; `+0x58` = a flag; `+0xAC` = the move speeds; `+0xB8` = the turn rate; `+0xC0` = a smoothed speed; `+0xC4`/`+0xC8` = the aim yaw/pitch; `+0xCC`/`+0xD0`/`+0xD4` = the smoothed yaw and its delta; `+0xD8` = the turn-on-spot angle | **partly mapped** (§3) |
| `C_BasePlayerPawn::m_pMovementServices` client `+0x1330` | `FUN_180433600`: `m_pMovementServices` with the offset `0x1330` | **confirmed** |
| `C_BasePlayerPawn::m_pMovementServices` server `+0xB50` | the server build's field tables could not be parsed (different code shape) | **unverified** |
| `CBodyComponentBaseAnimGraph::m_animationController` client `+0x510` | `FUN_180450670`: `m_animationController \| CBaseAnimGraphController \| sz=0x6a0 \| off=0x510` | **confirmed** |
| … server `+0x4E0` | not established | **unverified** |
| `CBaseAnimGraphController::m_nAnimationAlgorithm +0x18` | `FUN_18044da50` | **confirmed** |
| `m_nPrevAnimUpdateTick` client `+0xD4` | `FUN_180432220` | **confirmed** |
| `m_bLastUpdateSkipped` client `+0xCF` | `FUN_180461b90`: `FUN_180330ee0(param_2,&local_48,local_res18,param_1 + 0xcf,param_3)` | **confirmed** |
| `m_bNetworkedAnimationInputsChanged` client `+0xCD` | idem: `param_1 + 0xcd` | **confirmed** |
| `m_hSequence` client `+0xB0` | `FUN_18044da50` / `FUN_180432220` | **confirmed** |
| `m_flPlaybackRate` client `+0xC0` | `FUN_180461b90`: `param_1 + 0xc0` | **confirmed** |
| `m_sAnimGraph2Identifier` client `+0x440` | `FUN_180461b90`: `param_1 + 0x440` | **confirmed** |
| `m_pGraphInstanceAG2` client `+0x448` | `FUN_180461b90` | **confirmed** |
| the **28 replicated graph inputs** of `CCS2PawnGraphController`, `m_flAimYawAngle` at `controller+0x4e8` | the codec `0x8d9740` writes `m_flAimPitchAngle` → `param_1+0x4d0` and `m_flAimYawAngle` → `param_1+0x4e8`; the binder `0x8c8a80` creates all 28 slots at `+0x2d8 … +0x560` | **confirmed**, and the full slot↔parameter map is now proven (§2) |
| the AG2 node classes live in `animationsystem.dll` | **there is no `animationsystem.dll`** in `game/csgo/bin/win64` (only `client.dll`, `host.dll`, `matchmaking.dll`, `server.dll`); `CNmAimCSTask` / `CNmAimCSNode` **and** `CNmFootIKTask` / `CNmFootIKNode` *are* in `client.dll`; `CAimMatrixUpdateNode`, `CTurnHelperUpdateNode`, `AimMatrixOpFixedSettings_t`, `CAnimInputDamping`, `CTurnHelperInstanceData`, `CAimCameraUpdateNode`, `CAnimMovement`, `CSequenceGroupData`, `CSeq*` are **absent** from `client.dll` | **corrected** — the node names the research lists are not in this build's client; the aim/turn nodes live in the compiled `.vanim` asset (or in a newer build) |
| `CBaseAnimGraph::m_graphControllerManager` (c +4248), `m_pMainGraphController` (c +4400), `m_bAnimGraphUpdateEnabled` (c +4480) | `CBaseAnimGraph`'s size is `0x1268` = 4712 ✓ (the research's client size is right); `m_bAnimGraphUpdateEnabled` and `m_bShouldUpdateTransformations` are referenced by the class's registration `0x18044c070`, but their offsets were not extracted; `m_graphControllerManager` / `m_pMainGraphController` have **no code xrefs** | **size confirmed, offsets unverified** |
| `m_nServerGraphInstanceIteration` (c +1024), `m_nServerSerializationContextIteration` (c +1028) | both are referenced by `0x180461b90` / `0x1804387f0`; the offsets were not extracted | **unverified** |
| `m_pVerifiedCommands[seq % 150].m_crc` | `search_strings "m_crc"` → **absent**; `"VerifiedCommands"` → **absent** (only VAC/map-CRC strings) | **corrected** — no such mechanism in CS2's client; the CS:GO command-CRC trick does not apply |
| `CreateMove` vtable slot 5 `0xb64e60`, slot 25 `0xd01610` | the live `CCSGOInput` vtable is at **`0x181c9db20`**; **slot 5 = `0x180b65de0`**, **slot 25 = `0x180d02130`** | **corrected** — the research's values are the stale build-`25815307` ones from `docs/re/client-dll.md` |

### The payload's own state reader vs. this list

`payload/game/src/state_reader.cpp` reads:

| payload field | class / offset | net / client | note |
|---|---|---|---|
| `m_vecAbsVelocity` | `C_BaseEntity +0x3f8` | net | the client copy is interpolated; the server's is authoritative |
| `m_fFlags` | `C_BaseEntity +0x3f4` | net | |
| `m_flSimulationTime` | `C_BaseEntity +0x3b8` | net | |
| `m_angEyeAngles` | `C_CSPlayerPawn +0x3600` | net, **interpolated** | the payload reads the *interpolated* value; the server used the value at the shot time |
| `m_flMaxspeed` | `CPlayer_MovementServices` | net | |
| `m_flFlashDuration` | `C_CSPlayerPawnBase` | net | |

The payload reads **none** of the anim-state / graph-input / bone fields —
that is exactly the gap an animfix has to close.

---

## 4. The hitbox link

**Which function turns the animated bones into the hitbox transforms?**
`CSkeletonInstance::GetTransformsForHitboxList` — **RVA `0xab0b80`**
(`client.dll`), reached from `0xaa77c0` and `0xaade10`.

Its body (decompiled):

```c
// this = CSkeletonInstance*
lVar1 = *(longlong *)(param_1 + 0x1c0);              // the bone array
… *(longlong **)(param_1 + 0x1e0) …                 // the model handle
FUN_180aa0f80(param_1, 0x100);                       // SetupBones (ensure the pose is current)
iVar6 = FUN_181772940(model, lVar9 * 0x10 + *(longlong *)(param_3 + 2));   // the hitbox set
uVar7 = FUN_1801ca2b0(local_a8, (longlong)iVar6 * 0x20 + lVar1);          // bone → world transform
FUN_180a9b9f0(lVar9 * 0x20 + *(longlong *)(param_2 + 0x10), uVar7);      // store the hitbox transform
```

**Are the client's hitbox transforms the animated bones?** **Yes.**
The transforms are composed from the *same* array the animation just wrote:

* the bone array pointer: `CSkeletonInstance + 0x1C0` (= `CModelState + 0x80`);
* the bone stride: `0x20`;
* the bone layout: `{Vector pos @+0x00; float scale @+0x0C; Quaternion xyzw @+0x10}`
  — confirmed on this build by `FUN_180aa0f80`, which writes
  `(uVar35 + 0x10 + local_4240)`, i.e. the quaternion at bone `+0x10`.

There is **no separate hitbox-transform cache** on the client: the two callers
of `0xab0b80` compute the transforms into a *transient* buffer
(`0xaa77c0` allocates a local `CUtlVector` and frees it with `0xa9b3a0`;
`0xaade10` uses a caller-provided buffer). The only per-skeleton caches are:

| cache | offset | meaning |
|---|---|---|
| the pose (bone transforms) | `+0x1c0` | the animated bones — **this is what an animfix must overwrite** |
| the hitbox-set pointer | `+0x440` | the resolved `CHitBoxSet` for `m_nHitboxSet` |
| the bone-state masks | `+0x450` / `+0x458` | which bones are up to date |
| the "bones dirty" flag | `+0x110` | set by the animation, cleared by `0x9d09b0` |

**Where the local player's hitboxes are cached:** nowhere but in that bone
array. Consequently an animfix that rewrites the bones (after `SetupBones`)
*immediately* moves the hitboxes of the next trace/ESP query — no extra
invalidation is needed for the hitbox path. What *does* need care is the
bone-state masks (`+0x450`/`+0x458`): if the engine believes the bones are
current it will not recompute them, and a later `SetupBones` will overwrite
the fix.

### The foot yaw — CS2's `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`

In CS:GO the anim state ends with `m_pPlayer->SetAbsAngles(QAngle(0,
m_flFootYaw, 0))` — one line that rotates the whole model and therefore
*every* hitbox. **CS2 has no `SetAbsAngles`, no `m_flFootYaw` and no
`m_flGoalFeetYaw`** (`search_strings "SetAbsAngles"`, `"FootYaw"`,
`"footYaw"`, `"GoalFeetYaw"` → all **absent** from `client.dll`). The
equivalent exists, but it is split in two:

1. **The entity's world yaw is a *networked* value, not a computed one.**
   `CGameSceneNode::m_angRotation` is a network var
   (`.?AV?$CNetworkVectorBase@VQAngle@@VNetworkVar_m_angRotation@CGameSceneNode@@`,
   `0x1822d8110`), i.e. the *server* decides the model's world rotation and
   the client receives it. So the CS:GO "the client and the server disagree
   about the entity's yaw" divergence does **not** exist in CS2 — the
   divergence has moved *inside* the animation.
2. **The lower-body yaw is a graph input.** The anim state's turn angle
   reaches the graph as the parameter `ground_turn_angle_or_velocity`
   (**`controller+0x3f8`**), written by `ReflectMove` (`0x8e3200`) from
   `animState+0x2c` (`m_flTurnOnSpotAngle`, when the ground move state is
   `TurnOnSpot`) or from `animState+0xb8` (the turn *rate*, when the state is
   `TurnOnSpotLoop`). The ladder variant is `ladder_angle_yaw`
   (`controller+0x428`). The AG2 locomotion/turn nodes then twist the lower
   body inside the pose.

3. **What CS2 does have is an abs-angle write at the end of the anim state
   update**: `FUN_1808a5d70` ends with

   ```c
   local_88 = CONCAT44(*(undefined4 *)(param_1 + 0xd8), (undefined4)local_88);
   FUN_18022b470(*(longlong *)(param_1 + 0x48), &local_88);
   ```

   i.e. it hands the entity (`animState+0x48`) a `QAngle` whose yaw is the
   anim state's **turn-on-spot angle (`+0xd8`)** — and `FUN_18022b470`
   (`0x22b470`) writes it to the scene node and **invalidates the bone cache**
   (`FUN_1809d09b0`) so the pose is rebuilt with the new orientation.

   **So the CS2 equivalent of the CS:GO line is**
   `FUN_18022b470(entity, &QAngle(pitch, animState+0xD8, 0))` at the end of
   `CCSPlayerAnimationState::Update` — and it is **hookable** (a 16-byte
   prologue). The value it applies (`animState+0xd8`) is *computed*, never
   replicated, and it is the angle that rotates the whole model: this is the
   single most important field for the animfix.

---

## 5. The divergence (the animfix target list)

The server and the client run **the same** animation code
(`game/shared/…` is linked into both `client.dll` and `server.dll`), but they
feed it different inputs and they replicate only part of the result.

* **Replicated:** `m_flAnimTime`, the velocities, the flags, the eye angles,
  the duck amount, the stamina, the AG2 *sequence layer*
  (`m_hSequence`/`m_flSeqStartTime`/`m_flSeqFixedCycle`/`m_flPlaybackRate`),
  `m_networkAnimTiming` — **and the 28 graph inputs** of
  `CCS2PawnGraphController` (stage 7 above).
* **Not replicated:** the whole `CCSPlayerAnimationState`
  (`CCSPlayer_MovementServices+0x310`) — the string `m_AnimationState` is
  referenced only by the schema table, never by a codec — **and of course the
  pose** (the bones).

Two different mechanisms therefore make the client's pose differ from the
server's:

* **(A) the never-replicated anim state** — the client derives
  `m_currentMoveType`, `m_groundMoveState`, `m_airAction`,
  `m_groundActionDirection`, `m_flPreviousAimYaw`, the turn angle
  (`+0xd8`) and the foot-IK offsets from *its own* velocity and angles, in
  **`CCSPlayerAnimationState::Update` (`0x8a5d70`)** — which runs on the
  **command** path (`0xa72860` → `0xa70de0` → `DoMovement` → `0x8a5d70`),
  i.e. once per tick, but with the *client's* (predicted) inputs. The server
  derives the same values from the authoritative state, in the same function
  (the code is `game/shared`). Whenever the two differ, the graph receives
  different inputs — and the turn angle rotates the whole model (§4).
* **(B) the locally predicted graph inputs** — `ReflectPawnState`
  (`0x8e3540`) writes the client's own values into the very slots the
  server's codec (`0x8d9740`) fills. Whichever runs last wins; the animfix
  must make the **server's** values the ones that reach the graph.

Ordered by **how much each one moves the hitboxes**:

| # | Target | Where | Why it moves the hitboxes | Risk / difficulty |
|---|---|---|---|---|
| 1 | **The aim/body yaw the graph uses** (`aim_angle_yaw` at `controller+0x4e8`, `aim_angle_pitch` at `+0x4d0`; the accumulators `m_flPreviousAimYaw` `animstate+0x30`, `m_flTurnOnSpotAngle` `animstate+0x2c`) | `CCS2PawnGraphController` + `CCSPlayerAnimationState` | The upper body is rotated by the body yaw. A 5–10° body-yaw error swings the head and chest capsules sideways by several units — **the single biggest hitbox mover**. | The slot is **replicated**, so the fix is (B): make sure the server's value is not overwritten by the local prediction. The *source* of the server's value is the interpolated `m_angEyeAngles` (`pawn+0x3600`), which lags the shot time — an animfix can at best remove the *prediction* error, not the interpolation lag. |
| 2 | **The move/air state** (`m_currentMoveType`, `m_groundMoveState`, `m_airAction`, `m_groundActionDirection`) | `animstate+0x18…0x1b` | Decides walk / run / land / ladder — i.e. the leg pose. A "land" vs "run" mismatch moves the knee/foot capsules. | Mechanism (A): **never replicated**. The client's state comes from the *predicted* velocity; after a server correction it is one tick stale. The fix must re-derive the state from the server's velocity (`m_vecServerVelocity`, `C_BaseEntity+0x404`). |
| 3 | **The move speed** (`move_speed_horizontal`, `move_speed_horizontal_previous`) | the graph-input slots `+0x350` / `+0x368` | The walk↔run blend changes the stride length and the arm swing → the thigh/foot capsules move. | Replicated *and* locally predicted (B); the client's copy comes from the predicted velocity. |
| 4 | **The animation phase** (`m_flAnimTime`, `m_flSeqStartTime`/`m_flSeqFixedCycle`, `m_networkAnimTiming`) | `C_BaseEntity+0x3b4`, `CBaseAnimGraphController+0xb4/0xb8`, `CCSPlayer_WeaponServices+0x15c0` | A phase error of a few frames visibly slides the whole pose (the arms, the legs) — small but continuous. | `m_flAnimTime` is replicated but interpolated; `m_networkAnimTiming` is the server's own timing list and is the intended channel for exactly this. |
| 5 | **The crouch amount** (`m_flDuckAmount +0x40c`, `m_flDuckRootOffset +0x418`, `m_flDuckViewOffset +0x41c`) | `CCSPlayer_MovementServices` | Ducking lowers the whole model by up to ~18 units → every hitbox moves down. | Replicated, so the client agrees — but with one snapshot of latency; a local fix must not fight the replication. |
| 6 | **The foot IK** (`m_flFootIKOffsetLeft/Right +0x38/0x3c`, `ik_left_foot` `+0x488`, `ik_right_foot` `+0x4a0`) | anim state + graph-input slots | The feet are IK'd to the ground; an error moves the foot capsules (a low hitgroup, but the server does test them). | `ik_*` is replicated, the IK offsets are not (A). |
| 7 | **The flinch state** (`flinch_head_type +0x500`, `flinch_body_type +0x530`, …) | the graph-input slots | A hit reaction rotates the head/chest → the head capsule moves. | Replicated and locally predicted; transient. |
| 8 | **The ladder/ground-topology offsets** (`m_bUsingGroundTopologyOffset +0x3f0`, `m_vecLadderNormal +0x3f8`, `m_nLadderSurfacePropIndex`) | `CCSPlayer_MovementServices` | Tilts the model on a slope → every capsule shifts. | Replicated (`m_bUsingGroundTopologyOffset` is a netvar). |
| 9 | **The ragdoll pose** (`m_RagdollPose +0x11b8`, `m_bRagdollEnabled`) | `CBaseAnimGraph` | The ragdoll body replaces the animation entirely. | Replicated (netvars) — the client-side ragdoll is explicitly a separate flag (`m_bRagdollClientSide`). |

### The research's D1–D11 rows, reconciled

The research document's §4 divergence table, checked against this document's
RE. Only the rows the engine supports are kept.

| research row | claim | verdict on this build |
|---|---|---|
| **D1** tick vs frame | the client animates every frame, the server every tick | **supported, with the split spelled out**: the *anim state* is updated on the **command** path (`0xa72860` → `0xa70de0` → `DoMovement` → `0x8a5d70`), i.e. once per command/tick, while the *pose* is evaluated per rendered frame by the anim tick (`0x35aeb0` → `0x3552f0` → `0x12e4f90`). The state is therefore tick-aged while the pose is frame-aged — up to a tick of motion. |
| **D2** delayed animations | "the majority of the animation code just not run on the client" | **partly**: the AG2 evaluation *does* run on the client (`0x12e4f90`), and the anim state *is* computed on the client — but it is computed on the **command** path, so the state the pose is built from is a tick old while the pose itself is current. |
| **D3** `ShouldSkipAnimationFrame` | client-only frame skipping | **not applicable**: there is no such function in CS2. The CS2 analogue is the controller's client-side `m_bLastUpdateSkipped` (**+0xcf**) — a real field, but its effect on the pose was not traced. |
| **D4** `DoExtraBoneProcessing` / `DoProceduralFootPlant` | the client foot-plants the feet, the server does not | **partly**: the CS2 foot handling is `CNmFootIKTask` / `CNmFootIKNode` (both **in `client.dll`**) plus the anim state's `m_flFootIKOffsetLeft/Right` (**+0x38/+0x3c**), written by `FUN_18088baf0` *inside* the anim state update — i.e. it runs on **both** sides. Whether the AG2 foot-IK task adds a client-only pass was not traced. |
| **D5** the client interpolates the bone matrix | the client's bones are a rendered pose, the server's a simulated one | **supported**: `SetupBones` (`0xaa0f80`) runs on the client per frame; the server's runs per tick. |
| **D6** the abs-angle alignment | the server forces `SetAbsAngles(QAngle(0, m_flFootYaw, 0))` "to line up hitboxes" | **corrected for CS2**: an abs-angle write exists (`0x22b470`, at the end of the anim state update, with the angle from `animState+0xd8`), but the *entity's* world rotation is a **networked** value (`CGameSceneNode::m_angRotation`), so the CS:GO "the two sides disagree about the entity's yaw" cannot happen. The CS2 divergence is one level down: the **graph input** `ground_turn_angle_or_velocity` (`controller+0x3f8`). |
| **D7** the update gate is frame-based | the client's anim state update is gated by `framecount` | **not verified / probably not applicable**: the CS2 anim state update (`0x8a5d70`) contains no frame-count gate in the part that was read — it is called once per command. The "update gate" the research expects in the unmapped `+0x00..+0x17` was not found; the only gate-like field observed is the flag at `animState+0x58` (cleared at the end of the update). |
| **D8** the client's foot yaw lags the server's | | **supported in mechanism**: the turn angle (`+0xd8`) is computed from the client's own state and is never replicated. |
| **D9** the anim state is fed the wrong angles | | **supported**: the update reads the eye angles from the entity and folds them into `animState+0xc4/+0xc8` (`FUN_1808a5d70`). |
| **D10** the number of updates per frame | updating more than once per tick jitters | **supported**: the anim state update runs once per **command**, the pose rebuild once per **frame** — the two rates differ by construction. |
| **D11** where the fix is applied | "call local animfix at the end of createmove" | **supported and refined for CS2**: the natural seam is the *command* path — `CPlayer_MovementServices::ProcessCommand` (`0xa70de0`) runs the anim state update and the abs-angle write inside one command, so a fix applied there is automatically once per tick. The `CreateMove` seam is `CCSGOInput` vtable slot 5 (`0xb65de0`). |

**The short version:** the animfix's target list is

1. the **aim/body yaw** the graph uses (the biggest mover),
2. the **anim-state enums and the move speed**, which the client derives from
   its predicted velocity and the server never sends,
3. the **animation phase** (`m_flAnimTime` / the sequence cycle /
   `m_networkAnimTiming`).

---

## 6. The hook points

### The relocator's actual rule (from `payload/hooks/src/inline.cpp`)

`InlineHook::install` decodes **whole instructions** from the target until the
copied window is at least `k_jmp_size = 14` bytes (the size of the
register-free absolute jump it writes). It **bails** with
`unsupported_instruction` if

* any decoded instruction in the window is a **relative branch**
  (`jmp`/`jcc`/`call` rel32/rel8, `loop`), or
* the window would exceed **16 bytes** (`total > 16` → bail), or
* the opcode is not understood by its focused decoder.

RIP-relative displacements are *relocated* (so they are allowed), and the
window is padded with `0x90` after the 14-byte jump.

> The brief says "a 12-byte, branch-free prologue"; the code in the repository
> actually needs **≥ 14 and ≤ 16 bytes**. The verdicts below use the code.

### The table

| Function | RVA | Hookable? | Prologue | Signature | Hook shape |
|---|---|---|---|---|---|
| **`CCSPlayerAnimationState::Update`** | **`0x8a5d70`** | **YES** (14 B) | `40 53 57 48 81 EC 98 00 00 00 48 8B 79 10 48 8B` → exactly 14 whole bytes | `void CCSPlayerAnimationState::Update(CCSPlayerAnimationState* this)` | **the core animfix seam.** The detour receives the anim state pointer; it can overwrite the fields *before* the original computes them (move type, ground state, the aim yaw at `+0xc4`, the turn angle at `+0xd8`) or after it (the turn angle, `m_flPreviousAimYaw` at `+0x30`). It is called once per command, so a fix here is automatically tick-rate. |
| **the abs-angle writer** (CS2's `SetAbsAngles`) | **`0x22b470`** | **YES** (16 B) | `40 53 48 83 EC 60 48 8B D9 48 8B 89 30 03 00 00` → 16 whole bytes | `void SetAbsAngles(C_BaseEntity* entity, const QAngle* angles)` | **the foot-yaw seam.** The detour sees the `QAngle` the engine is about to apply to the scene node — the yaw is the anim state's turn angle (`+0xd8`). Overriding it rotates the whole model (and every hitbox) exactly as the CS:GO line did. |
| the per-command boundary | `0xa72860` | NO (18 B: `48 8B C4 48 81 EC C8 …` + `48 89 58 …`) | `48 8B C4 48 81 EC C8 00 00 00 48 89 58 10 48 89` | `void RunCommand(CPlayer_MovementServices* this, CUserCmd* cmd)` | the "end of CreateMove" equivalent: it is where the globals are faked (`client+0x2230090`) and restored, and where the movement processing (→ the anim state update) runs. Not hookable inline — use the `CreateMove` detour instead. |
| `CPlayer_MovementServices::ProcessCommand` | `0xa70de0` | ? | — | `void ProcessCommand(CPlayer_MovementServices*, CUserCmd*)` | the frame inside which the anim state update and the abs-angle write happen |
| `CSkeletonInstance::SetupBones` | **`0xaa0f80`** | **YES** (15 B) | `48 89 4C 24 08 / 55 53 56 57 / 41 54 41 55 41 56` → 15 whole bytes, no branch, no RIP | `void CSkeletonInstance::SetupBones(CSkeletonInstance* this, uint boneFlags)` | **the post-animation hook.** Detour: call `original()`, then overwrite the bones in `this+0x1c0` (or the specific bones) and return. The hitbox path reads the same array, so the fix is visible immediately. |
| `CAnimGraphGameSystem::AnimTickUpdate` | **`0x35aeb0`** | **YES** (15 B) | `89 54 24 10 / 48 89 4C 24 08 / 55 57 / 41 54 41 55` → 15 bytes | `void CAnimGraphGameSystem::AnimTickUpdate(CAnimGraphGameSystem* this, int clientTick)` | **the pre-animation hook.** Detour: patch the local pawn's anim state / graph inputs *before* `original()` runs the graph. |
| AG2 instance update | **`0x12e4f90`** | **YES** (14 B) | `40 53 / 48 83 EC 30 / 48 8B 44 24 60 / 48 8B DA` → exactly 14 bytes | `void AG2Update(AG2Instance* this, …, float dt, …)` | per-entity graph evaluation; a detour can patch the graph inputs for the local pawn just before the evaluation. |
| the graph-input codec | **`0x8d9740`** | **YES** (14 B) | `48 89 5C 24 08 / 55 56 57 / 41 54 41 55 41 56` → exactly 14 bytes | `void Codec(CCS2PawnGraphController* dst, CClass* src, CSchemaTable*)` | **the "let the server win" hook**: the decoded *server* values pass through here; a detour can capture them (and stop the local prediction from overwriting them). |
| the mirrored codec | **`0x8daab0`** | **YES** (14 B) | `48 89 5C 24 18 / 48 89 7C 24 20 / 55 / 48 8B EC` → exactly 14 bytes | idem | the twin of the above |
| `CCSPlayer_MovementServices` ctor | `0x4d9550` | YES (15 B) | `48 89 5C 24 08 / 48 89 6C 24 10 / 48 89 74 24 18` | `void CCSPlayer_MovementServices::CCSPlayer_MovementServices(...)` | a signature anchor for the `+0x310` anim state (it constructs it) |
| `FUN_180a7f360` (active-weapon resolver) | `0xa7f360` | YES (15 B) | `48 89 5C 24 08 / 48 89 74 24 10 / 57 / 48 83 EC 20` | `void* ResolveActiveWeapon(...)` | a signature anchor |
| AG2 parameter setter | `0x8492a0` | YES (16 B; one RIP-relative displacement — relocated) | `F3 41 0F 10 08 / 4C 8B C9 / F3 0F 10 05 A0 0F 3C 01` | `void SetGraphParam(void* params, short index, const void* value)` | the exact place a parameter value is written — a detour could rewrite the value on the way in |
| the AG2 pose-recipe serialiser | `0x13046e0` | YES (16 B) | `40 53 55 56 57 / 48 83 EC 78 / 80 B9 F4 47 00 00 00` → 16 bytes | `void SerializePoseRecipe(AG2Instance* this, int slot, …)` | reads the evaluated pose, writes the serialized pose recipe (`instance+0x11d0`) — a hook *before* the bones are written, for a pose-level fix |
| **`CCS2PawnGraphController::ReflectPawnState`** | **`0x8e3540`** | **NO** | `48 89 5C 24 18 / 56 57 41 56 / 48 83 EC 60` = 13 B; the next instruction (`48 8D 05 …`, 7 B) makes the window **20 B > 16** | `void CCS2PawnGraphController::ReflectPawnState(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn)` | *the semantically correct place to override* (it is the pawn→graph mirror) — but the inline relocator refuses it. Alternatives: write the values from the `AnimTickUpdate` detour, or interpose the **vtable** slot of the controller. |
| `ReflectMove` (helper) | `0x8e3200` | NO (18 B) | `48 89 5C 24 10 / 48 89 74 24 18 / 57 / 48 81 EC 80 00 …` → 18 B | `void ReflectMove(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn, void* animState)` | |
| `ReflectAim` (helper) | `0x8e2f70` | NO (18 B) | `48 89 5C 24 08 / 57 / 48 81 EC 80 00 …` → 18 B | `void ReflectAim(CCS2PawnGraphController* this, C_CSPlayerPawn* pawn, void* animState)` | |
| `CCS2PawnGraphController` param binding | `0x8c8a80` | NO (18 B) | `40 53 / 48 83 EC 20 / 48 8B D9 / C7 44 24 30 …` → 18 B | `void BindParams(CCS2PawnGraphController* this)` | |
| `CSkeletonInstance::InvalidateBones` | `0x9d09b0` | NO (18 B) | `40 53 / 48 83 EC 40 / 48 89 7C 24 58 / 48 8D 99 …` → 18 B | `void CSkeletonInstance::InvalidateBones(CSkeletonInstance* this)` | **the invalidation hook** — see below |
| `CSkeletonInstance::GetTransformsForHitboxList` | `0xab0b80` | NO (19 B) | `48 89 5C 24 20 / 55 56 57 / 41 54 41 55` = 12 B + `48 81 EC D0 …` (7 B) = **19 B** | `void CSkeletonInstance::GetTransformsForHitboxList(CSkeletonInstance* this, CTransform* out, int* hitboxSet)` | the hitbox producer — not hookable, but **not needed**: it reads the bone array, so a bone fix is enough |
| `CAnimGraphGameSystem::UpdateAnimGraph2Animations` | `0x38a390` | NO | 12 whole bytes, then `0F 84` = a **relative `jz`** → the decoder bails | `void UpdateAnimGraph2Animations(CAnimGraphGameSystem* this, int* list)` | |
| the AG2 evaluate job | `0x3552f0` | NO (20 B) | `40 53 / 41 55 41 56 / 48 81 EC 00 01 00 00` = 13 B + `48 8D 05 …` = 20 B | `void Job(CAnimGraphGameSystem** ctx)` | |
| `CAnimGraphGameSystem::ProcessPostNetworkingAnimationDecodes` | `0x37eb50` | NO (19 B) | `40 55 53 56 57 / 41 55 41 56 41 57` = 11 B + `48 8D AC 24 30` (8 B) = 19 B | `void ProcessPostNetworkingAnimationDecodes(CAnimGraphGameSystem* this)` | |
| the pawn's per-frame reflect (`FUN_180c905e0`) | `0xc905e0` | NO (17 B) | `48 89 5C 24 10 / 56 / 48 83 EC 30` = 10 B + `4C 8B 89 F8 34 00 00` (7 B) = 17 B | `void C_CSPlayerPawn::…(C_CSPlayerPawn* this)` — it calls `ReflectPawnState(this+0x34f8, this)` | the graph-controller pointer is at **`pawn+0x34f8`** |

### The seams the implementer needs

0. **The tick boundary (settled).** The `CCSGOInput` vtable on the live
   build is at **`0x181c9db20`** (RVA `0x1c9db20`); it is found from the
   static-init anchor the dumper already signs (`FUN_1800ee570`, whose
   `LEA RAX,[vtable]` sits at `+0x18`; the pattern
   `48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C9 C7 05 ?? ?? ?? ?? FF FF FF FF 48 8D 05 ?? ?? ?? ?? 48 89 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??`
   matches at `0x1800ee574`).
   * **slot 5 = `create_move_input` = `0x180b65de0` (RVA `0xb65de0`)**
   * **slot 25 = `create_move` = `0x180d02130` (RVA `0xd02130`)**
   * slot 29 = `0x180b6e720`
   The research document's `0xb64e60` / `0xd01610` are the **stale**
   build-`25815307` values quoted from `docs/re/client-dll.md`; they do not
   resolve on this build. The *engine's* per-command boundary is deeper:
   `CPlayer_MovementServices::ProcessCommand` (`0xa70de0`), which runs the
   anim state update and the abs-angle write inside one command.
   The engine globals the animfix has to fake are at **`client + 0x2230090`**
   (`curtime +0x30`, `frametime +0x34`, tick count `+0x44`).
   There is **no** `m_pVerifiedCommands[..].m_crc` in CS2's client (the
   strings are absent) — a new command must be detected some other way (the
   command number at `cmd+0x8`, which is what `0xa72860` logs).
1. **Post-animation hook** → `CSkeletonInstance::SetupBones` (`0xaa0f80`,
   hookable). Detour: `original(this, flags);` then write the corrected
   `CTransform`s into `*(this+0x1c0)` (stride `0x20`, quaternion at `+0x10`).
   This is the only place where the pose is guaranteed to be complete and the
   only one that also moves the drawn model (the renderer reads the same
   array).
2. **Invalidation hook** → `CSkeletonInstance::InvalidateBones` (`0x9d09b0`,
   *not* hookable inline) and the bone-state masks at `+0x450`/`+0x458`.
   The implementer must keep the masks consistent after the fix, otherwise
   the next `SetupBones` recomputes the pose and drops the correction. The
   *hookable* alternative is to do the invalidation from the `SetupBones`
   detour itself (write the bones **after** `original()` returns, and bump
   the mask word so the engine believes the bones are current).
3. **Pre-animation hook** → `CAnimGraphGameSystem::AnimTickUpdate`
   (`0x35aeb0`, hookable) and the AG2 update (`0x12e4f90`, hookable), plus
   the graph-input codec (`0x8d9740`, hookable) if the fix is "let the
   server's values win" rather than "write our own".

### The server-side counterparts (`server-build (indicative)`)

The server build on disk (`C:\Users\gpmpo\ghidra-analysis\server.dll`,
sha256 `09e8c04c5f44040155d04fa79a3f43491d542a386f45807ed77785729e5925a9`,
image base `0x180000000`, 68 603 procedures / 51 392 strings) is a **different
build**; every address below is **indicative**. It is, however, the same
source (`game/shared/…`), and the **prologues are byte-identical** to the
client's for every function that has one — which is the proof that the same
code runs on both sides.

| Function | client RVA | server RVA (`server-build (indicative)`) | server prologue | hookable (server) | derivation (server) |
|---|---|---|---|---|---|
| `CAnimGraphGameSystem::AnimTickUpdate` | `0x35aeb0` | **`0x530100`** | `89 54 24 10 48 89 4C 24 08 55 57 41 54 41 55 41` (identical) | **YES** (15 B) | `search_strings "AnimGraph Server Tick"` → `0x1817f8730` → `FUN_180530100` |
| `CAnimGraphGameSystem::UpdateAnimGraph2Animations` | `0x38a390` | **`0x555840`** | `40 56 48 83 EC 50 83 3A 00 48 8B F1 0F 84 …` (identical) | NO (relative `jz` at +12) | `search_strings "UpdateAnimGraph2Animations"` → `0x1817f8a50` → `FUN_180555840` |
| `CCS2PawnGraphController::ReflectPawnState` | `0x8e3540` | **`0xad8c90`** | `48 89 5C 24 18 56 57 41 56 48 83 EC 60 48 8D 05` (identical) | NO (20 B) | `search_strings "ReflectPawnState"` / `"cs_player_pawngraphcontroller.cpp"` → `0x18191d168` / `0x18191d180` → `FUN_180ad8c90` |
| `CSkeletonInstance::SetupBones` (the bone writer) | `0xaa0f80` | **`0xc9ea30`** | `48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41` (identical) | **YES** (15 B) | the server's `GetTransformsForHitboxList` calls `FUN_180c9ea30(param_1, 0x100)` — the same role as the client's `FUN_180aa0f80` |
| `CSkeletonInstance::GetTransformsForHitboxList` | `0xab0b80` | **`0xca71a0`** | `48 89 5C 24 20 56 57 41 54 41 55 48 81 EC …` | NO (18 B) | `search_strings "CSkeletonInstance::GetTransformsForHitboxList"` → `0x181952218` → `FUN_180ca71a0`; the body reads the bone array at `param_1+0x1a0` (= `modelState+0x80` with the server's `m_modelState` at `+0x120`) and the dirty flag at `param_1+0xf4` |
| `CSkeletonInstance::AddAnimationDecode` | (`0xaa0690` — the client's `CalcAnimationState`/`AddAnimationDecode` function) | **`0xc9e120`** | `40 55 56 57 41 54 41 55 41 56 41 57 B8 F0 10 00` | YES (15 B) | `search_strings "CSkeletonInstance::AddAnimationDecode"` → `0x181951f18` → `FUN_180c9e120` |
| `CSkeletonInstance::SetAnimationController` | `0xac4860` | **`0xcb1bd0`** | — | — | `search_strings "CSkeletonInstance::SetAnimationController"` → `0x181952120` → `FUN_180cb1bd0` |
| `CSkeletonInstance::PostDataUpdate` | — | **`0xcadd90`** | — | — | `search_strings "CSkeletonInstance::PostDataUpdate"` → `0x181951e90` → `FUN_180cadd90` |

**What the server does that the client does not** (the difference the animfix
must reproduce):

* the server **computes** the `CCSPlayerAnimationState` from the
  authoritative velocity/angles and never replicates it (the field's name is
  referenced only by the schema table on the client, §3);
* the server **replicates** the 28 graph inputs (`aim_angle_yaw`, the move
  speeds, the crouch amount, the flinch …) and the sequence layer, so the
  client *has* the server's values — it also predicts its own and overwrites
  them;
* the server's skeleton keeps the authoritative bone array
  (`CSkeletonInstance+0x1a0` on the server versus `+0x1c0` on the client —
  the `+0x80` inside `CModelState` is the same on both sides).

---

## 7. What the implementer must do

1. **Resolve the anim state and the graph controller.**
   `C_CSPlayerPawn + 0x34f8` → the `CCS2PawnGraphController*` (from
   `FUN_180c905e0`: `FUN_1808e3540(*(longlong *)(param_1 + 0x34f8), param_1)`).
   `C_BasePlayerPawn + 0x1330` → `CCSPlayer_MovementServices*` (the schema
   field table `FUN_180433600`: `m_pMovementServices@0x1330`), whose `+0x310`
   is the embedded `CCSPlayerAnimationState` (size `0xe0`); the anim state's
   own schema fields are at `+0x18` (`m_currentMoveType`) … `+0x44`, and its
   plain (non-schema) members — the aim yaw/pitch at `+0xc4`/`+0xc8`, the
   turn angle at `+0xd8` — are the ones the fix must touch.
   *Risk:* re-verify the three offsets against a live pawn before writing
   them into the payload (the field-table parser recovers only part of some
   classes).

2. **Decide what "the server's animation" means for the local player.**
   The client has the server's *sequence layer* (`m_hSequence`,
   `m_flSeqStartTime`, `m_flSeqFixedCycle`, `m_flPlaybackRate` — all netvars),
   the server's *timing list* (`m_networkAnimTiming`,
   `CCSPlayer_WeaponServices+0x15c0`) **and the server's 28 graph inputs**
   (they arrive through the codec `0x8d9740`) — but **not** the server's
   `CCSPlayerAnimationState`. Reproducing the server's anim state means
   re-running the same velocity→state derivation on the client.
   *Risk:* the derivation function **is** now located
   (`CCSPlayerAnimationState::Update`, `0x8a5d70`) but its *inputs* (which
   of the many values it reads are the authoritative ones) still need a
   live diff against the server's values (§8, #3).

3. **Stop the local prediction from overwriting the server's graph inputs.**
   The cleanest fix is to make the *server's* values reach the graph: hook
   the codec (`0x8d9740`, 14-byte prologue → hookable) to capture the
   server's `aim_angle_yaw` (`+0x4e8`), `aim_angle_pitch` (`+0x4d0`),
   `move_speed_horizontal` (`+0x350`), `move_crouch_amount` (`+0x380`) and
   the rest, and re-apply them after `ReflectPawnState` has run (from the
   `AnimTickUpdate` detour, or by restoring them in the `0x12e4f90` detour).
   *Risk:* the order of the snapshot decode and `ReflectPawnState` inside one
   frame was **not** established statically (§8, #2) — the implementer must
   measure it before choosing the seam.

4. **Re-derive the anim state from the server's velocity.**
   Write the correct `m_currentMoveType` / `m_groundMoveState` /
   `m_airAction` / `m_groundActionDirection` (`animstate+0x18…0x1b`) and the
   move speeds (`animstate+0xac`) from `m_vecServerVelocity`
   (`C_BaseEntity+0x404`) instead of the client's predicted velocity. The
   cleanest way is to hook **`CCSPlayerAnimationState::Update`**
   (`0x8a5d70`, a 14-byte prologue → hookable) and let the original run,
   then overwrite the fields the server would have produced.
   *Risk:* the update is called once per **command**, so the fix is
   automatically tick-rate — but it must not run twice per tick (the
   community's "jitter" failure mode).

5. **Fix the foot yaw — the CS2 `SetAbsAngles`.** Hook the abs-angle writer
   `0x22b470` (16-byte prologue → hookable) and replace the yaw the engine
   is about to apply with the server's turn angle. The engine's value is the
   anim state's `+0xd8`; the graph input it feeds is
   `ground_turn_angle_or_velocity` (`controller+0x3f8`). This is the
   equivalent of CS:GO's `SetAbsAngles(QAngle(0, m_flFootYaw, 0))` and the
   only single write that rotates *every* hitbox.
   *Risk:* the writer also invalidates the bone cache, so a fix applied here
   forces a pose rebuild — that is intended, but it must not fight the
   `SetupBones` hook of step 6 (pick one seam).

6. **Install the post-animation hook** on `CSkeletonInstance::SetupBones`
   (`0xaa0f80`, 15-byte prologue → the repo's relocator accepts it) and, in
   the detour, write the corrected bone transforms into `*(this+0x1c0)`
   (stride `0x20`, quaternion at `+0x10`) **after** `original()` returns.
   *Risk:* `SetupBones` runs on a worker thread (the AG2 job) and takes the
   skeleton's mutex at `this+0x114`; the write must not race the render
   thread, and it must be a full `CTransform` (position **and** quaternion) —
   a naive `{Vector, Quat}` write produces non-unit quaternions and stretches
   the hitboxes (see `docs/re/aim.md`).

7. **Keep the bone cache consistent.** After the write, either bump the bone
   state mask at `this+0x450`/`this+0x458` (so the engine does not recompute
   and overwrite the fix) or clear the dirty flag at `this+0x110` through
   `CSkeletonInstance::InvalidateBones` (`0x9d09b0`).
   *Risk:* `0x9d09b0` is **not** inline-hookable (18-byte prologue) — call it
   directly rather than hooking it.

8. **Do not hook the hitbox path.** `GetTransformsForHitboxList` (`0xab0b80`)
   already reads the bone array; a bone fix is enough, and the function is
   not hookable anyway (19-byte prologue).
   *Risk:* low. (The ESP in `payload/aim/src/reader.cpp` reads the same
   `CModelState::m_pBoneArray` at `+0x80` — so the ESP follows the fix for
   free.)

9. **Drive it from the command, not from the frame.** The tick boundary is
   the `CCSGOInput` vtable **slot 5 = `0xb65de0`** (`create_move_input`) or
   slot 25 = `0xd02130` (`create_move`); the engine's own per-command seam
   is `CPlayer_MovementServices::ProcessCommand` (`0xa70de0`), which runs
   the anim state update and the abs-angle write. The globals to fake are at
   **`client+0x2230090`** (`curtime +0x30`, `frametime +0x34`, tick `+0x44`).
   Detect a new command by its number (`cmd+0x8`) — there is no
   `m_pVerifiedCommands[..].m_crc` in CS2.
   *Risk:* the anim state update must run **once per tick**; running it per
   frame is the classic animfix jitter.

10. **Add signatures** (ADR-0012: no hard-coded RVAs) for `SetupBones`
    (`0xaa0f80`), `AnimTickUpdate` (`0x35aeb0`), the codec (`0x8d9740`),
    the anim state update (`0x8a5d70`) and the abs-angle writer
    (`0x22b470`) to `external/dumper/src/analysis.rs::known_signatures()`;
    the prologues above are good signature material
    (`48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56`,
    `89 54 24 10 48 89 4C 24 08 55 57 41 54 41 55`,
    `48 89 5C 24 08 55 56 57 41 54 41 55 41 56`,
    `40 53 57 48 81 EC 98 00 00 00` and
    `40 53 48 83 EC 60 48 8B D9 48 8B 89 30 03 00 00`).
    *Risk:* all five must stay unique in the module.

---

## 8. What is still unknown

1. ~~**The client-side writer of `CCSPlayerAnimationState`.**~~ **RESOLVED
   in the second pass.** The update is
   **`CCSPlayerAnimationState::Update` = `0x8a5d70`**, called as
   `FUN_1808a5d70(ms + 0x310)` from `FUN_1808cb710` (the movement services'
   per-command handler). It was found with the byte pattern
   `48 8D 8B 10 03 00 00` (`lea rcx,[rbx+0x310]` — the anim-state address),
   which has nine hits in the module; the player-region one is `0x1808cb767`.
   It writes `+0x18/+0x19` (move type / ground state, one 16-bit store),
   `+0x1b` (air action), `+0x1c` (was-on-ground), `+0x30`
   (`m_flPreviousAimYaw` = the newly computed aim yaw at `+0xc4`), `+0x38` /
   `+0x3c` (the foot-IK offsets, via `FUN_18088baf0`), `+0x40` / `+0x44` (the
   weapon drop, via `FUN_180882b70`), `+0xac` (the move speeds, via
   `FUN_1808e72e0`), `+0xc4` / `+0xc8` (the aim yaw / pitch), `+0xcc` /
   `+0xd0` / `+0xd4` (the smoothed yaw and its delta) and `+0xd8` (the
   turn-on-spot angle). It ends with the abs-angle write (§4).
   **Still open:** the writer of the *schema* field `m_flTurnOnSpotAngle`
   (`+0x2c`) and of `m_flPreviousHorizontalSpeed` (`+0x34`) — the engine's
   *applied* turn angle is the plain member at `+0xd8` (accumulated by
   `FUN_18088c730`, the `TurnOnSpotLoop` handler), and `+0xb8` carries the
   turn *rate*. **Experiment:** a write-breakpoint on `ms+0x33c` and on
   `ms+0x344` in a live client (the research's method) will name the two
   remaining writers; the byte-pattern probe used here (`F3 0F 11 ?? 2C`
   over the whole module, 123 hits) did not isolate them.

2. **The server-side counterpart addresses.** The `server.dll` on disk is a
   different build, so the addresses in the table in §6 are **indicative**
   only. What *is* established: the prologues are byte-identical for every
   counterpart, so the *hookability verdicts transfer verbatim* (the server's
   `AnimTickUpdate` and `SetupBones` are hookable, the server's
   `ReflectPawnState` is not). The server's `CCSPlayerAnimationState` member
   offsets could not be extracted — the server build's field tables use a
   different code shape, and the schema-field parsers used on the client do
   not apply. Note also that a prologue match is **not** proof: the pattern
   `40 53 57 48 81 EC 98 00 00 00` (the client's anim state update) also
   matches `0x180edb200` on the server, and that function turned out to be a
   *different* function (it works on offsets `+0x4ac…+0x540`), so the
   server's anim state update is still **unverified**.
   **Experiment:** with the server build imported, re-derive the anim state's
   field table and the state-derivation function there (search for the
   `lea rcx,[reg+0x310]` pattern around the server's
   `CCSPlayer_MovementServices`), then diff the call graphs against the
   client.

3. **Which of the anim state's inputs the server uses differently.** The
   anim state update reads the entity's eye angles, its velocity and its
   flags; the *client* reads its own (predicted) values. The implementer
   needs to know which reads are authoritative.
   **Experiment:** hook `0x8a5d70` and dump the anim state before and after
   it runs, for the local player, while the server's snapshot is applied;
   compare `+0x18` (move type), `+0xc4` (the aim yaw) and `+0xd8` (the turn
   angle) against the values the server's own anim state would produce (the
   server's can be read from a *remote* player's replicated graph inputs).

4. **The order of the snapshot decode and `ReflectPawnState` inside one
   frame.** Both write the same 28 slots; which one wins is the whole
   question for mechanism (B) of §5. **Not settled statically**: the codec
   (`0x8d9740`) has no direct callers (it is invoked from the schema's
   netvar-dispatch table) and `ReflectPawnState` is reached through the
   pawn's virtual (`0xc905e0`, called from the anim tick's per-entity
   virtuals `+0x888`/`+0x890`), so the frame phase of the two writes cannot
   be ordered from the call graph.
   **Experiment:** hook `0x8d9740` (the codec, 14-byte prologue → hookable)
   and the `AnimTickUpdate` detour (15 bytes → hookable), log the order of
   the writes into `controller+0x4e8` (`m_flAimYawAngle`) for one frame, and
   see which value survives.

5. **The AG2 node implementations and the `.vanim` tuning constants.** The
   node classes the research names (`CAimMatrixUpdateNode`,
   `CTurnHelperUpdateNode`, `AimMatrixOpFixedSettings_t`, `CAnimInputDamping`,
   `CTurnHelperInstanceData`, `CAimCameraUpdateNode`) are **not in
   `client.dll`**, and there is no `animationsystem.dll` in the game
   directory; only `CNmAimCSTask` / `CNmAimCSNode` and `CNmFootIKTask` /
   `CNmFootIKNode` are present. The aim clamp (max yaw/pitch), the turn rate
   and the damping therefore live in the compiled `.vanim` graph asset, not
   in the DLL.
   **Experiment:** parse the player `.vanim` graph asset from
   `game/csgo/pak01_dir` (or read the live runtime values out of the AG2
   instance at `controller+0x448`) and read the aim-matrix settings from it.

6. **Whether the client's hitbox query path caches the transforms after
   all.** `0xab0b80`'s two callers build the transforms on the stack, but the
   physics system may keep its own copy per entity for the duration of a
   frame. **Experiment:** watch `CSkeletonInstance+0x1c0` while a trace runs
   and check whether the hitbox array is re-derived after the bones change.

---

## 9. The ordering measurement (2026-10-21, the T20 implementer)

> **Appended, not rewritten.** Everything above is the RE's own text and
> stands; this section records the measurement the T20 task required (the
> order of the snapshot decode and the client's prediction inside one frame)
> and the corrections the measurement produced. The **live** measurement the
> task asked for could **not** be run — the agent is in Windows session 0
> with no GPU, so CS2 cannot start here (`AGENTS.md` §"Launching the game").
> The measurement below is therefore **static**: it was taken on the same
> `client.dll` (sha256 `659940c1…`, the live build) with Ghidra 12.1.4
> through REA, plus a byte-level scan of the module.

### 9.1 What was measured, and how

The RE could not order the two writes because *both* go through pointers
into a member array and neither has a direct caller (§8, item 4). The
measurement therefore followed the **writes** instead of the calls:

| question | how it was answered |
|---|---|
| What does the **codec** write, and where? | decompile `0x8d9740` and read its call sites' register setup (the *assembly*, not the pseudocode — the pseudocode drops the destination argument) |
| What does the **prediction** write, and where? | decompile `0x8e3540` (`ReflectPawnState`) and its helpers `0x8e2f70` (`ReflectAim`) / `0x8e3200` (`ReflectMove`) / `0x8be340` |
| Where does the graph **read** the values? | decompile the setter `0x8492a0` and the AG2 evaluation `0x12e4f90`'s caller `0x38a070` |
| In which **frame phase** does each run? | the callers of the pawn's reflect (`0xc905e0`), the anim tick (`0x35aeb0`) and the AG2 job (`0x3552f0`) |

### 9.2 The result: the prediction wins, and the server's value never reaches the graph

**The codec** (`0x8d9740`, `void Codec(CCS2PawnGraphController* dst, CClass* src, CSchemaTable*)`)
decodes the **server's** replicated values into the **destination** controller.
The destination is its **first argument**: every call site sets
`LEA R9, [R13 + <slot>]` with `R13` = the first argument, e.g. for
`m_flAimYawAngle`:

```asm
0x1808da70c: LEA RAX, [0x181c0ee70]      ; the field-name string
0x1808da713: MOV qword ptr [RSP + 0x30], -0x7a11b6da   ; the name hash
0x1808da71c: LEA R9, [R13 + 0x4e8]        ; the destination: the aim-yaw slot
0x1808da723: MOV qword ptr [RSP + 0x38], RAX
0x1808da728: LEA R8, [RBP + 0x658]        ; the decoded value
0x1808da734: LEA RDX, [RSP + 0x30]        ; the name hash
0x1808da739: MOV RCX, RBX                ; the source object
0x1808da73c: CALL 0x1808baec0            ; the field writer
```

**The prediction** never writes those slots. `ReflectPawnState` (`0x8e3540`)
→ `ReflectAim` (`0x8e2f70`) computes the *client's* aim angles and pushes them
into the graph through the **parameter setter** `0x8492a0`:

```c
// FUN_1808e2f70 (ReflectAim), the aim-pitch half
if ((*(longlong *)(param_1 + 0x4d8) != 0) && (-1 < *(short *)(param_1 + 0x4e0))) {
  FUN_1808492a0(*(longlong *)(param_1 + 0x4d8), *(short *)(param_1 + 0x4e0), &local_68);
}
```

and the setter is the one place a graph parameter's value is written:

```c
// FUN_1808492a0 — SetGraphParam(node, index, value)
*(float *)(*(longlong *)(*(longlong *)(node + 0x10) + (longlong)index * 8) + 0x18) = *value;
```

**So the slot layout is uniform** — `{ value @+0x00, node* @+0x08, int16 index
@+0x10 }`, stride `0x18` — and the chain is:

```
the codec      -> controller + 0x4e8      (the server's replicated float)
the prediction -> setter(node, index)     (the client's predicted float)
the AG2 graph  <- the parameter at +0x18  (whatever the setter wrote last)
```

**The prediction is the only writer of the graph's parameters *through the
setter*.** (The enum/bool slots are pushed with direct stores — see §9.4.3 —
and the flinch slots by nobody at all; §9.4.3 has the full list.) The server's
replicated value is *stored* in the slot and then never used: the client's
prediction is what the graph is evaluated from. That is mechanism (B) of §5 in
its sharpest form — and it is **not** a question of which of the two runs
last: the server's value does not reach the graph at all.

### 9.3 The frame phase (the ordering the task asked for)

* The **snapshot decode** (and with it the codec) runs in the client's
  **network** phase, on the main thread.
* The **prediction** runs on the **command** path:
  `CPlayer_MovementServices::ProcessCommand` (`0xa70de0`) → the pawn's virtual
  `+0xa00` → `0xc905e0` → `ReflectPawnState` (`0x8e3540`). `0xc905e0` is the
  pawn's virtual at vtable offset `0xa00` (four vtables carry it:
  `0x1c8ab38`, `0x1c90878`, `0x1c91448`, `0x1c95fb8`).
* The **AG2 evaluation** runs in the **anim** phase:
  `CAnimGraphGameSystem::AnimTickUpdate` (`0x35aeb0`) → `FUN_18038a390` (which
  either runs the job inline or queues it to the thread pool) → the job
  `0x3552f0` → `FUN_18038a070` (per entity) → **`0x12e4f90`** (the graph
  evaluation) → `0x12e52b0` (the pose commit).

**The order inside a frame is _inferred_, not measured**: the three phases
are known (the network decode, the command path, the anim phase), and the
community's research (`docs/research/animfix.md` §3.1) puts the server's
animation update after the movement processing — the client's counterpart of
which is the command path. So **decode → predict → evaluate**, and the
prediction's write is the last one before the graph reads the parameters:
**the prediction wins.** What was *measured* is the write path itself
(§9.2): the prediction is the only writer of the graph's parameters through
the setter, so the server's value never reaches the graph at all. The
*ordering* of the two phases is a reading of the engine's frame structure
that a live run would confirm — and the animfix does not depend on it
being right (see below).

**The seam this measurement selects** is the **anim tick**
(`CAnimGraphGameSystem::AnimTickUpdate`, `0x35aeb0`): it runs *after* the
prediction in the same tick, and it *contains* the AG2 evaluation (the
per-entity `FUN_18038a070` → `0x12e4f90`), so a correction written there is
the one the pose is computed from.

**And it does not depend on the inferred order.** The correction is written
at the *start* of the anim tick and the evaluation happens *inside* it, so
whatever the prediction did before — or whatever it does afterwards, in
the next tick — the values the graph is evaluated from are the server's. (A
prediction that runs *after* the anim tick would overwrite the parameters,
but not before the evaluation: the correction still reaches the pose.) The correction is the setter's own write,
replicated by hand:
`*(float *)(*(node + 0x10)[index] + 0x18) = value`.

> **Why not the setter** (`0x8492a0`), which is where the prediction writes
> and would be the natural seam? It is **not hookable**: its third prologue
> instruction is `movss xmm0, [rip+0x13c0fa0]` — a RIP-relative load — and
> the repo's inline relocator rewrites such a displacement as
> `disp + (target − trampoline)`. `client.dll` is mapped high under ASLR and
> the trampoline comes from `VirtualAlloc(nullptr, …)` low, so the rewrite is
> ~141 TB and can never fit in an `int32`: the hook engine bails with
> `unsupported_instruction`. The anim tick's prologue is RIP-free, so the
> correction goes there instead. (The repo has hit this limit before — see
> `docs/control-harness.md` — and the workaround there was a vtable patch,
> not an inline hook.)

### 9.4 Corrections to the sections above

1. **The codec decodes 14 slots, not 28.** The full list of the fields it
   writes (each guarded by "is the field in the snapshot?"), with the field
   names the codec itself carries:

   | slot | field | type written |
   |---|---|---|
   | `+0x2d8` | `m_bIsDefusing` | bool |
   | `+0x2f0` | `m_moveType` | int |
   | `+0x308` | `m_moveDirectionID` | int |
   | `+0x470` | `m_flAirHeightAboveGround` | float |
   | `+0x488` | `m_leftFootTarget` | vector |
   | `+0x4a0` | `m_rightFootTarget` | vector |
   | `+0x4b8` | `m_flFlashedAmount` | float |
   | `+0x4d0` | `m_flAimPitchAngle` | float |
   | `+0x4e8` | `m_flAimYawAngle` | float |
   | `+0x500` | `m_flinchHead` | int |
   | `+0x518` | `m_flinchHeadRestart` | bool |
   | `+0x530` | `m_flinchBody` | int |
   | `+0x548` | `m_flinchBodyRestart` | bool |
   | `+0x560` | `m_flinchIsOnFire` | bool |

   The other 14 slots (`m_flMoveSpeedX/Y`, `m_flMoveSpeedHorizontal`,
   `m_flPreviousMoveSpeedHorizontal`, `m_flCrouchAmount`,
   `m_flWeaponDropAmount`, `m_flGroundTurnAngleOrVelocity`, `m_flLadder*`,
   `m_groundAction`, `m_groundActionDirection`, `m_airAction`,
   `m_moveIsWalking`) are **registered** as network vars by the schema
   registration (`0x8daab0`) but the codec never decodes them — the server
   does not send them. So §5's "the server replicates the 28 graph inputs"
   is **too strong**: it replicates **14**, and `move_speed_horizontal` /
   `move_crouch_amount` — the two the T20 specification named — are among the
   ones it does **not** replicate.
2. **`0x8daab0` is not a "mirrored codec"**: it is the **schema registration**
   of `CCS2PawnGraphController` (it carries the field-name strings
   `m_flMoveSpeedX` … `m_flinchIsOnFire` and registers them). There is only
   **one** codec, `0x8d9740`.
3. **The four *float* slots are exactly the ones the prediction rewrites
   through the setter**: `+0x470` (written by `FUN_1808be340`), `+0x4b8`
   (by `ReflectPawnState`), `+0x4d0` and `+0x4e8` (by `ReflectAim`). The
   replicated **enum/bool** slots are pushed into the graph with *direct*
   stores (`*(undefined8 *)(param_obj + 0x18) = <the enum descriptor>`), which
   no available seam intercepts — and the **flinch** slots (`+0x500` … `+0x560`)
   are pushed by **nobody** at all, so the flinch reaction never plays on the
   client.
4. The **slot layout** is now known: `{ value @+0x00, node* @+0x08, int16
   index @+0x10 }`, stride `0x18` — the parameter object the node and the
   index point at holds the value at `+0x18`.
5. The anim tick's **second argument is a flag, not a tick count**: the
   function picks its VProf scope by it (`param_2 == 0` ⇒ "AnimGraph Server
   Tick", otherwise "AnimGraph Client Tick"). §2's signature line said
   `int tickCount`; it is corrected to `int clientTick` here. Whether the
   driver runs once per **tick** or once per **frame** is still not
   established (its caller is reached through a 32-bit-RVA dispatch table
   that this pass did not resolve), and nothing in the animfix depends on
   it: the correction is written at the start of the anim tick and the
   evaluation happens inside it.

### 9.5 What is still open

* **The anim-state divergence** (§5, mechanism A) is **not** closed by this
  work and is not closed by the T20 task: the client's
  `CCSPlayerAnimationState` is never replicated, so its move type, ground/air
  action and turn-on-spot angle are the client's own. The experiment that
  would settle which of the anim state's inputs the server uses differently
  is still the RE's §8 item 3 (a write-breakpoint on `ms + 0x328` and a diff
  of the anim state against a remote player's replicated graph inputs).
* The **enum/bool** graph inputs (the move type, the flinch) are not covered
  by the animfix: pushing them needs either the engine's static enum
  descriptor tables (`DAT_182532910` and friends) or a hook on the reflect
  itself, which is a vtable slot (`+0xa00`) and not an inline-hookable
  function.
* The **bone-write seam** (`CSkeletonInstance::SetupBones`, `0xaa0f80`) was
  **not** used: it runs on the AG2 job's worker thread, it would have to
  rewrite a **full `CTransform`** (position *and* quaternion) or the hitboxes
  stretch (`docs/re/aim.md`), and the graph-input seam already fixes the
  values *before* the pose is computed — so a bone write would be a second,
  riskier copy of the same fix.

### 9.6 The animfix's validity rule (and why it is not the bone seam)

The implementation (`payload/control/include/cs2/control/animfix.hpp`) captures
the server's values at the codec and re-applies them **in the anim tick** -
the place the graph is evaluated (§9.3). The rule that decides whether a
captured value may be re-applied is the specification's own:

> A captured value may only be re-applied if it was captured **this frame**.
> A capture corrects a slot **at most once**; only a newer decode (a newer
> generation) can correct it again. A capture that has **aged out** - older
> than one client frame (16 ms, which is also one of the server's 64 Hz
> ticks) - is refused outright.

The first clause is what keeps a capture from being written over the
client's fresh prediction again and again; the second refuses a capture that
aged out (a stalled connection, a dead player, a paused game), where the
server's value describes a pose the server has long left behind: writing it
over the client's fresh prediction would move the local hitboxes
*backwards*, which is worse than not fixing them at all. The refusals are
counted (`animfix_skips`).

Because the correction lands in the **same tick** as the decode, the bound
can be tight: the capture is at most one tick old when it is used.

**The hooks are installed on the game's thread.** The inline hook patches its
target with a 14-byte write that is not atomic, and a patch that lands while
another thread is *executing* the target can tear. The anim tick is the
client's tick - it runs on the main thread - so the animfix installs its
hooks from the CreateMove detour (also the main thread, and not inside the
anim tick): no thread can be executing the patched function while it is
patched. (The residual: `InlineHook::original()` is a plain pointer read on
the detour's side; the hook engine publishes the trampoline *before* it
patches the target, precisely so the detour always finds it, and the store
itself is atomic on x86.)

**The bone seam was not used.** `CSkeletonInstance::SetupBones` (`0xaa0f80`)
would have been the wrong place for this fix: it runs on the AG2 job's
**worker thread**, it would have to rewrite a **full `CTransform`** (position
*and* quaternion) or the hitboxes stretch (`docs/re/aim.md`), and - the
decisive point - the graph-input seam already fixes the values **before** the
pose is computed, so a bone write would only be a second, riskier copy of the
same fix.

---

*Companion documents:* the community definition of "animfix" / "local animfix"
is being written by the research sibling task (`docs/research/animfix.md`; it
may not be in the tree yet). The hitbox/bone model this report re-derives is
in [`aim.md`](aim.md) §"Hitboxes / bones"; the shot model is in
[`aim.md`](aim.md) and [`bullet-trace.md`](bullet-trace.md).
