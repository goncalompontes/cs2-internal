# Hand-off — overlay / payload state

> **Validated on native Windows (2026-10).** The payload was
> manual-mapped into CS2 on a real Windows host and read live player state; the
> Vulkan overlay layer rendered the HUD + ESP into the swapchain; a
> windowed/fullscreen toggle did not freeze the game. See
> [docs/windows-validation.md](windows-validation.md). The historic "native
> Windows — untested" marker has been removed. Originally validated on build
> **25815307**; re-validated on **25947352** (the game auto-updated — see
> [Running the scenario harness](#running-the-scenario-harness--operational-hand-off-2026-10-15),
> §2).

Read alongside `decisions.md` **ADR-0015/0016/0017/0018/0019/0020** and the RE
agent guide `docs/re/README.md`.

> **Always cross-check RE findings against community sources.** Before trusting a
> decompiled offset/slot/pattern, search the community reverse for the same
> symbol — UnknownCheats (esp. the CS2 *Reversal, Structs and Offsets* thread and
> the `CreateMove`/`CUserCmd`/`CCSGOInput` threads), GitHub CS2 internal/cheat
> sources, and `cs2-dumper`. Use it to predict, confirm and sanity-check, then
> re-derive on our binary (their build ≠ ours) and cite the sources (URL + build
> date) in `docs/re/<module>.md`. This is step 4 of the loop in
> `docs/re/README.md`.

> **Active feature:** the programmatic scenario/control harness. The RE is done
> and the **control core + input + game events are implemented and validated**
> (`payload/control/` + `scripts/scenario/`). The **`usercmd` input backend is
> implemented and validated live**: discrete buttons (`usercmd-move` drives jump +
> bhop) and the **sub-tick view angles** (`look(..., via="usercmd")` writes every
> `input_history` entry's `view_angles`; `subtick-aim` shows it is silent,
> `subtick-shot` proves the shot follows it). **Analog movement is validated
> live** — `move(forward=1.0)` walks 204 units/s (1.01× the engine's own console
> `+forward`) and `move(forward=0.5)` walks 115 (0.57×): the write needed the
> engine's units (`±1`, not 450) and a clear test spot (`movement-probe` pins one).
> Resume from the operational hand-off below,
> [`docs/control-harness.md`](control-harness.md) (design, decisions, next
> actions), [`docs/re/subtick.md`](re/subtick.md) (the sub-tick structures, the
> command build and the movement consumer) and
> [`docs/re/client-dll.md`](re/client-dll.md) (the input path + game events).

## ⏸ Wrap-up (2026-10-16, session 2) — weapon model, auto-fire fix, hitbox renderer

**State:** `payload.dll`, `cs2_overlay_layer.dll`, `injector.exe` all build; the 12
host tests pass. The ragebot is **working** (real studio hitboxes + keybind) and
the **auto-fire timing bug is fixed**. Two things landed on top:

- **Weapon model from the game.** The solve now reads the shooter's real
  `CCSWeaponBaseVData` (`weapon + 0x388`, `m_nDamage`/`m_flHeadshotMultiplier`/
  `m_flPenetration`/`m_flRangeModifier`); explicit `aim`/`rage` args still win.
  Live: `aim-probe` reports a Glock `weapon_damage 50` (was the hard-coded 100).
- **Auto-fire fix.** The attack is asserted **persistently** in `Input::apply()`
  step 1 (backend-independent) while a solve is active, so every simulated command
  carries the attack with the silent angle — the manual-fire path worked for the
  same reason. (Previously the press was only on the command `rage_tick` saw.)

**Hitbox renderer — open, off by default.** `EspConfig::hitboxes` draws each live
enemy's real studio capsules (stadium outline; short capsules collapse to one
circle) coloured by hitgroup with the rage target highlighted. It is **not right
yet** — the world geometry is verified correct (an external projection of the same
boxes lands on the enemy's body: head at the head, legs at the legs), but the
on-screen result is an unreadable tangle, and culling the off-screen cases (a
team-0 identity sitting on the camera smeared arcs across the screen) is a
*mitigation, not a fix*. The owner's read is that something is still genuinely
wrong. **Next step:** reproduce close-up and compare the external projection with
the drawn image pixel for pixel; decide whether it is the draw (edges, radius
scaling, no body occlusion) or a mismatch between the boxes read and the
skeleton pose actually rendered. `docs/ragebot.md` §10 item 1.

**Occlusion (item 3) — RE pinned, not implemented.** The trace call convention is
resolved from a live call site: `TraceShapeWithSortedResults` =
`(iface, <rdx descriptor>, start, end, filter, &results)` on
`*client_base + 0x221f058`, filter vtable `+0x1ace848`, mask `0x1c1003`, static
default filter at `+0x221f070` (`docs/re/aim.md` §"The call convention, pinned from
a live site"). It was **not** wired up: it needs a dumper signature for the
singleton + the function (ADR-0012) and live validation, and a wrong call aborts
the process (the payload has no exception tables), so it deserves its own pass.

**Tooling added:** `scripts/capture-screen.ps1` and `scripts/capture-game.ps1`
capture the interactive session (run them via a scheduled task —
`schtasks /create … /it`, like `CS2Launch`; session 0 has no visible desktop).
`capture-game.ps1` restores + foregrounds the game window and crops to its client
area.

---

## ⏸ Wrap-up (2026-10-16) — ragebot: real studio hitboxes + hold-key ragebot

**Resume here for the ragebot.** Design + direction: **ADR-0022** and
[`docs/ragebot.md`](ragebot.md) (which carries an implementation-status §0 and
the next actions). RE: [`docs/re/aim.md`](re/aim.md) (the shot model + the trace
recipe + **the runtime studio hitbox set**), [`docs/re/lagcomp.md`](re/lagcomp.md)
(backtracking), [`docs/re/vphysics2.md`](re/vphysics2.md) (collision geometry).

**Implemented + host-tested** (`payload/aim/`, 4 tests: `aim-geometry`,
`aim-field`, `aim-solver`, `aim-records`):

- `geometry` — the angular chart (gnomonic projection), OBB/sphere projection,
  convex hulls, half-plane clipping, and the two optima (nearest point,
  Chebyshev centre).
- `field` — the damage field (cells, the weapon value model, the pose-blurred
  maximum), `solver` — build the field and pick the aim.
- `hitbox` — bone/AABB → oriented box; `reader` — the snapshot → solver bridge;
  `records` — the backtrack ring + the rewind-tick formula.

**Live-validated** (`payload.dll` builds; `aim` + `rage` wired into the control
service; scenarios `aim-probe`, `aim-shot`, `rage`):

- `aim-probe` — the solve points at a bot's head within **0.02°**, using the real
  studio hitbox set (`hitcount` 19).
- `rage` — **the hold-key ragebot works**: with the feature enabled (the overlay
  menu's `Ragebot` tab, or the `rage` command) the payload solves each frame and
  the per-tick `Input::apply()` writes the silent sub-tick aim + `IN_ATTACK`;
  solves/writes/shots climb and the bot's health drops. **Firing manually while
  the ragebot holds the aim always hits.**

### The real studio hitboxes (done 2026-10-16)

`aim::read_model_hitboxes` now walks pawn → scene node → `CModelState` → (double
deref) `CModel` → render mesh → hitbox set → **19 capsules** (bone-local endpoints
`m_vMinBounds`/`m_vMaxBounds` + `m_flShapeRadius`), resolves each box's bone
through `CModel::GetBoneIndexForHitboxForMesh`, transforms the endpoints by the
bone (`{pos, quat}`, stride `0x20`) and reads the hitgroup from `m_nGroupId`
(head ⇒ `m_flHeadshotMultiplier`). Full layout + the live verification:
**`docs/re/aim.md` §"The runtime studio hitbox set"**. The collision-AABB head/body
proxy is now only a fallback. New geometry: `project_capsule` (a capsule is the
convex hull of two spheres, so its silhouette is the hull of the two circle
projections); `aim-solver` grew a capsule case.

**What is NOT done (the resume points, in priority order):**

1. **Ragebot auto-fire timing (minor).** With `fire` on, a shot is *sometimes*
   released on a command whose history the silent write did not reach that tick,
   so it follows the camera. Cause: `apply()` runs for both the slot-25
   `create_move` **copy** and the slot-5 simulated command, and the attack press is
   asserted per-`apply()`; manual fire works because the press is **persistent**
   across every command. Fix: assert the attack into `set_mask_` while the rage is
   active (or gate the press to the slot-5 command).
2. **Occlusion / wallbang** — the solver consumes occluders
   (`SolveInput::occluders` → `DamageField::clip`) but nothing produces them. The
   engine trace recipe (interface singleton `client_base+0x221f058`, the direct
   `FUN_180a1a0a0` call, the static filter at `+0x221f070`) is in
   `docs/re/aim.md` §"Calling the trace from the payload"; the struct layouts /
   arg order still need pinning from **assembly** (a wrong call faults the game).
3. **Backtracking** — `records` is host-tested but not sampled per frame.
4. **Pose blur** wiring; pellet-pattern registration (`K>1`); pulling the weapon's
   real damage/headshot/range from `CCSWeaponBaseVData` (the `aim`/`rage` commands
   still take a fixed profile).

## ⏸ Wrap-up (2026-10-15/16) — sub-tick bhop implemented from the RE

The menu-enabled movement features **engage** (`Input::apply()` runs them
regardless of the control backend and gates only the explicit `press`/`move`/
`look` commands).

- **Bunnyhop — re-implemented correctly (read-only, no engine calls).** The
  authoritative design is UC 771389 / 774033 (InkCrow), researched in
  [`docs/re/research-bhop.md`](re/research-bhop.md) and re-derived on
  `client.dll` 25815307 in [`docs/re/prediction.md`](re/prediction.md) §"RE
  findings". `Input::bhop_tick` is a `CreateMove` **post** hook that reads
  `m_ModernJump`'s landing record and the command's build tick, then appends a
  **release+press pair sharing one `when`** to `base->subtick_moves`:
  - `build = m_nTickBase + (fresh ? 1 : 0)`; the record is in `{build-1, f}`, so
    the pair goes into `build == landed_tick + 1` (placement A), with a B
    catch-up window for anything one to five ticks late;
  - `when = f == 0 ? 3/64 : clamp(q(f) + 3/64, q(f) + 1/64, 62/64)` — past the
    recorded contact and inside `sv_bhop_time_window * 0.5 = 16/64`;
  - a lone press is **not** a press edge while the key is held (the edge machine
    is `C || (A && B)`), so the pair is required;
  - the pair is re-derived and re-appended **every frame** (the command's table
    is rebuilt each frame), idempotent by *array content*, budget-checked (`32`)
    and insertion-sorted so the admission gate cannot discard the array;
  - the whole-tick `IN_JUMP` bit is **not** cleared — it is the held-key read;
  - the assist still only acts while the player actually holds jump.
  The `RunCommand` simulation the earlier session tried **crashes** without the
  full `CPrediction` envelope (`client+0xa51526 read addr=0x0`) and the
  authoritative design does not use it; it is kept behind `predict(on)` purely as
  a reference path.
  **Validated live (2026-10-15, `bhop` scenario, de_dust2 bot match, operator
  confirmed):** bhop enabled + jump held → 93 pair writes / 5+ hop cycles in 5 s,
  and placement **A fired** (`bhop_a` > 0), which is the phase proof — the pair
  lands in `build == landed_tick + 1`. `bhop_age` spans the whole airtime between
  landings (the record only moves on a new contact), so the phase is read from the
  `bhop_a`/`bhop_b` counters, not from sampling `bhop_age`.
- **Auto-strafe — re-implemented as a normal, silent air-strafe (2026-10-15).** The
  payload now computes the `AirAccelerate` optimum `theta*` every airborne tick and
  pushes the wish perpendicular to the *current* velocity on the side of the goal
  taken from the held keys in the view frame (`W`/nothing forward, `S` behind; a
  held `A`/`D` stands it down), so nothing writes a view angle and the camera/eye
  angles stay put. `Input::strafe_tick` / `write_strafe_wish`; wired into the menu
  (`wire::Settings.auto_strafe`), the `strafe` control command and the `autostrafe`
  scenario. Two encodings: `subtick=0` (default) camera-frame analog; `subtick=N>=1`
  keeps `(1, 0)` and rotates the movement basis with `N` `yaw_delta` steps. RE and
  concise re-derivation: [`docs/re/strafing.md`](re/strafing.md); community sources:
  [`docs/re/research-strafe.md`](re/research-strafe.md).
  - Decisive re-derivation this session: `m_flCmdForwardMove` is
    `CBaseUserCmdPB::forwardmove` **unscaled** (`UpdateInputState`, `FUN_180a764d0`),
    and the engine's own unit analog (`1.0`) reaches `AirMove` as the full-speed
    request (`move(forward=1.0)` = 204 u/s, 1.01x the console `+forward`), so a unit
    analog still gets the full `A` budget and `theta* = 90°`. The silent-yaw gate is
    the convar `sv_subtick_movement_view_angles`, read by `DoMovement::Subtick`'s
    `(*vtable+0x150)()` predicate.

**Resume here, in this order:**

0. **⚠️ Footgun fixed 2026-10-15:** a `move()` request with all-zero axes used to
   strip the movement bits out of every command (silently removing *all* movement —
   a scenario that ended with `move(forward=0, side=0)` left the operator unable to
   move). It now only overrides the bits when the wish is non-zero; `move(clear=True)`
   turns the override off. Also note the **overlay menu owns the settings block** (the
   layer re-writes it every frame), so a Python `settings.write()` can be clobbered —
   toggle the features in the menu when a write "does not stick".
1. **Bhop — validated live** (2026-10-15: `python scripts/run-scenario.py bhop`,
   de_dust2 bot match, operator-confirmed): hops, `bhop_writes`/`bhop_presses`
   climb, and placement **A fires** (`bhop_a`), i.e. the phase
   (`build == landed_tick + 1`) is right. Remaining polish: watch that `bhop_a`
   dominates `bhop_b` (B is the beta catch-up — a hop, but the ground-slice
   clamp keeps the speed loss), and consider the optional fresh-frame
   hull-trace guess.
2. **Strafer — validated live (2026-10-15); the silent sub-tick yaw rotation is the
   working recipe.** `Input::strafe_tick` + `write_strafe_wish`
   (`payload/control/src/input.cpp`) compute the `AirAccelerate` optimum every
   airborne tick and push the wish perpendicular to the *current* velocity on the
   side of the goal taken from the held keys in the view frame (`W`/nothing forward,
   `S` behind; a held `A`/`D` stands it down). The sub-tick rotation is **simulated
   per slice** (the community's `ref_air_accel_sim`): an *even* split ramps the basis
   (the engine accumulates the deltas) and only the last slice lands on `theta*`;
   setting each step's `yaw_delta` from the simulated velocity makes every slice gain.
   Measured on a de_dust2 bot match (listen server, 250 u/s ground speed,
   `sv_quantize_movement_input 1`, enabled purely through the menu's settings block):
   - `strafe(subtick=8)` (**default**): **+85.1 median / +104.9 max u/s per hop**, and
     the gain scales with the slice count — `subtick=1` +15.4, `4` +41.9, `8` +85.1
     (all vs an ≈ 0 control). The even split (an earlier revision) gained +17.4.
   - `strafe(subtick=0)` (camera-frame analog): nearly flat — the `{-1,0,1}` snap
     loses the exact perpendicular angle, so the rotation channel is the one to use.
   The `strafe` control command, the `wire::Settings.auto_strafe` byte and the menu
   toggle are wired back in; the `autostrafe` scenario observes it. Drive the settings
   block (`scripts/scenario/settings.py`) for a test — the menu owns the feature and
   reverts a control-side enable on the next published frame. RE:
   `docs/re/strafing.md`.
3. Re-check the bhop's `bhop_b` share (B is the beta catch-up; a high share means
   the phase slips) and consider the optional fresh-frame hull-trace guess.

Diagnostics on `input()`: the `bhop_*` block above plus `bhop_holding_ticks`,
`bhop_air_ticks`, `input_local_pawn`; `predict_ready`/`predict_runs`/
`predicted_landing_*` cover the retired simulation path, and a cyan **`PRED`** box
in the overlay draws its predicted origin. The local pawn is forwarded every frame
from the overlay refresh (`Overlay::set_local_pawn_sink`), so the features see it
fresh during menu-only play.

**Enabled-by-menu only** (unchanged): the payload re-applies `wire::Settings` every
published frame, so the menu owns the features; settings reset to defaults on
launch.

<details><summary>Earlier wrap-up (superseded)</summary>

### ⚠️ Wrap-up (2026-10-15, earlier session) — movement features were NOT working

**Operator-confirmed:** with the overlay menu's `Movement` toggles enabled (auto-strafe
and bunnyhop), **neither had an in-game effect**. That is now explained: `apply()`
early-returned on the default `console` backend, so the flags were never applied.

The original resume list (kept for context): (1) confirm the enable path live,
(2) check whether the held jump is visible in the command mask, (3) have the
strafe also write the raw axes, (4) re-verify with `scripts/scenario/autostrafe.py`.
Items 1 and 3 are done (see above); item 2 was answered by the diagnostics
(`bhop_holding_ticks` shows the mask *does* carry the held jump).

</details>

## One-line summary

The portable Vulkan present layer (`overlay/`) renders the overlay from shared
memory and is proven in isolation. On native Windows (D3D11/DXGI, no DXVK) the
full stack now works: the injected payload reads game state and publishes a
`wire::Model`, and the Vulkan layer renders it — **no freeze on the display-mode
change that wedged the game under Wine/Proton (ADR-0017)**.

## What is proven

- **`overlay/` (Vulkan present layer)** — intercepts `vkCreateSwapchainKHR` /
  `vkQueuePresentKHR`, renders the `wire::Model` (HUD + `DrawList`) with
  ImGui+Vulkan, and draws a real HUD + ESP in CS2. Validated on native Windows
  with the payload (`-vulkan`, implicit layer registered under
  `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers`, `CS2_OVERLAY=1`) and, earlier,
  without a payload under Proton.
- **State → shm model** — `ModelPublisher` serializes `render::OverlayModel`
  into the shared segment (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm` on
  native Windows; under Proton the prefix's `drive_c`); the layer writes the
  presented extent back so the payload can project world coordinates.
- **State introspection** — local player + every player (health, armor, team,
  weapon, ammo, origin/eye/velocity/angles), live-verified on the updated build.
- **Payload plumbing** — injector `.pdata` registration (`RtlAddFunctionTable`,
  ~2043 entries), per-thread TLS bootstrap for the manually-mapped image
  (`payload/tls`), `NtCreateThreadEx(SKIP_THREAD_ATTACH)` thread helper, crash
  handler + freeze watchdog (`payload/diag`).
- **RE** — the game's present is `CRenderContextBase::Present`
  (`rendersystemdx11.dll + 0x16dd0`, build 25815307), dispatching to
  `CRenderContextDx11::Present` at `*(this)+0x510`; the swapchain wrapper is
  `CSwapChainDx11`. **Do not inline-hook it** — its prologue has RSP-relative
  home stores the epilogue reads back; use a vtable-slot hook or a
  stack-correct hook (ADR-0017).

## Windows notes (learned 2026-10)

- **Session 0 has no GPU.** The agent/SSH shell cannot start the game; use the
  `CS2Launch` scheduled task (`scripts/launch-cs2.cmd`), which runs in the
  interactive session. See `AGENTS.md`.
- **Medium integrity.** The game runs as the standard user; it cannot create
  files in `C:\` root. All payload/overlay output paths must be under
  `%LOCALAPPDATA%` (or another user-writable dir).
- **API-set imports.** The injector maps `api-ms-win-crt-*` → `ucrtbase.dll`
  (and `api-ms-win-core-*` → `kernelbase.dll`) when resolving imports.

## Running the scenario harness — operational hand-off (2026-10-15)

The harness drives the game *programmatically* over a named pipe and **cannot see
the screen**. Every claim it makes is therefore a *state* claim; anything that is
really about the picture (did the camera turn? did the bullet hit? did the player
walk?) must be confirmed by the operator watching the game. Treat them as
complementary: the runner asserts numbers, the operator confirms the image.

### 1. Prerequisites

- Steam running in the interactive session (session 1); CS2 installed.
- The `CS2Launch` scheduled task exists (`schtasks /query /tn CS2Launch`).
- `build-injector/injector.exe` and `build-payload/payload.dll` are built.
- **The payload's SDK matches the *running* game build** (see §2).
- The runner may be in session 0 (no GPU) — fine: the named pipe is same-user and
  crosses sessions. The agent **cannot** launch `cs2.exe` directly (session 0 has
  no GPU); it goes through the scheduled task.

### 2. The game build is not pinned — re-calibrate after every update

CS2 auto-updates. On 2026-10-15 it went from **25815307 (1.41.9.0)** to
**25947352 (1.41.9.1)** mid-session and every client.dll feature silently broke
(`exec` faulted in the engine; `state`/`events`/`input` all failed) because the
RVA offsets were stale.

**Symptoms of a stale SDK:** `ping` shows `events_ready=false`,
`input_ready=false`, `state.valid=false`, and an `exec` **crashes the game**
(`[crash] … at engine+0x1c4b15 read addr=0x0`).

Fix — all curated signatures still match across patches, only the RVAs move:

```sh
schtasks /run /tn CS2Launch            # game running (payload not needed)
python scripts/calibrate-live.py       # rewrites calibration/*.json (needs a live process)
python scripts/gen-sdk.py calibration payload/sdk
meson compile -C build-payload         # rebuild against the fresh SDK
```

Then re-inject once and confirm with `ping` (`events_ready` / `input_hooked` /
`state_ready` true) and the `status` scenario.

### 3. Launch + inject — two modes

**Self-contained (launches the game):**

```sh
python scripts/run-scenario.py <scenario>
```

It kills a stale `cs2.exe`, runs the `CS2Launch` task (session 1), waits for
`client.dll`, injects `payload.dll`, connects, runs, and tears down (kills the
game) unless `--keep-running` is given.

**Attach to a session you are already in (do NOT relaunch or re-inject):**

```sh
python scripts/run-scenario.py <scenario> --no-launch --no-inject --keep-running
```

Use this once the game is up and injected. **Never inject twice into the same
process**: two payload instances install two pipe servers on the same name and
clients then reach a random one — this caused a long, confusing failure. To load a
new payload, kill + relaunch the game, then inject **once**.

Manual equivalent: `schtasks /run /tn CS2Launch` → wait for `cs2.exe` +
`client.dll` → `build-injector\injector.exe <pid> build-payload\payload.dll
--load-missing`. Logs: `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`,
`cs2-overlay-layer.log`.

### 4. The screens and states — input is ignored in most of them

The **screen state machine** (`scripts/scenario/screens.py`, documented in
`docs/control-harness.md` §"The screen state machine") is the authoritative map of
this. It reduces the payload state to one screen, says which operations that
screen allows, and drives the transitions — so scenarios no longer re-derive
"is it safe to send input?" from raw fields:

```python
awaiting.ensure(game, Screen.LIVE)   # drive: map -> jointeam -> end warmup -> wait freeze
awaiting.require(game, "move")       # "blocked: dead does not allow move"
```

The signals it reads (`state`): `in_game` — **the main-screen rule**, true only
when the level's `C_CSGameRulesProxy` entity exists (the `game_rules` global is
readable at the main menu too, so it cannot be the test) — the `rules` block
(`warmup`, `freeze`, `round_win_status`, `t_cant_buy`/`ct_cant_buy`,
`game_phase`, `round_time`, `bomb_planted`, …), plus `local.team` (0 =
team-select, 1 = spectator, 2/3 = T/CT) and `local.alive`.

| Screen / state | How it looks | What to do |
|---|---|---|
| **Engine not ready** | `valid=false` (right after injection, or mid level-teardown) | **wait** — `exec` faults inside the engine before its input service is up, and the game dies with it |
| **Main menu** | `in_game=false`, `in_map=true`, `origin=(0,0,0)`, `team=0` | load a map (`map de_dust2`); `exec` can fault if issued too early |
| **Loading** | `in_game=false` (once a level's rules are up) / `valid=false` right after the command | wait — never re-issue `map` while a load is in flight (it restarts the load; the 2026-10-15 livelock) |
| **Team-select** | `in_game=true`, `team=0`, `alive=false` | input is ignored — **join a team** (`jointeam 2`) |
| **Warmup** | `rules.warmup=true` | `mp_warmup_end` |
| **Freeze** | `rules.freeze=true` | wait for it to clear (input is ignored) |
| **Round over** | `rules.round_win_status != 0` | wait for the next `round_start` |
| **Dead** | `alive=false`, `life_state=2` | input is ignored — wait for respawn |
| **Bomb carrier** | `weapon="weapon_c4"`, `clip=-1` | `+attack` does **not** fire a gun — select a weapon (`slot2`) first |
| **Bots moving** | `player_hurt` / `player_death` events keep arriving | `bot_stop 1` freezes them (a round restart un-freezes them) |

`awaiting.ensure_live_round` (= `ensure(Screen.LIVE, verify_movable=True)`) gates
on the screen **plus** "a console `+forward` actually moves you" — **but it issues
`mp_restartgame 1`**, which respawns everyone, moves you to spawn and un-freezes
the bots. Do **not** call it when the operator has set up a specific position;
check `awaiting.require(game, "move")` (or `wait_for(playable=True)`) instead. A
round can also end *during* a test, so re-check right before acting.

The `playable` predicate (payload) is `alive && team ∈ {T, CT}` — false on the
team-select screen, as a spectator, or when dead. `status` prints it implicitly
via `team`/`alive`.

### 5. Validation — prefer direct state over events

- **`wait_for(event=…)` only matches events that fire *after* the call starts.**
  Calling it *after* firing a shot can never see the hit it just caused. Verify
  actions with a **direct state read** (target health before/after, origin
  distance, `clip`), not a `player_hurt` wait. (This produced a long false "silent
  aim doesn't work" conclusion.)
- `status` (scenario) prints `valid`, `in_map`, `alive`, `team`, `on_ground`,
  `weapon`, `clip`, `players`, and the tail of the event stream — the fastest way
  to see why a test did nothing.
- The sub-tick scenarios embed that state in their result/error, so a failure says
  whether it was a bad state or a real mechanism miss.
- **Operator confirmation is required** for anything visual: whether the camera
  actually turned, whether the player actually walked, whether the shot looked
  like it landed. The runner's numbers and your eyes should agree.

### 6. Scenarios

| scenario | what it does | needs |
|---|---|---|
| `status` | read-only state + recent events | — |
| `join-bot-match` | load a map, add bots | — |
| `move-and-aim` | `setang` + console `+forward`; asserts movement | live round |
| `game-events` | asserts game events fire | live round |
| `usercmd-move` | jump + bhop via the command button mask | live round, usercmd hook |
| `subtick-aim` | silent angle write lands, camera does not follow | live round, usercmd hook |
| `subtick-shot` | camera 45° off a bot, silent aim, target **health drops** | live round, a gun, an enemy |
| `subtick-move` | raw-input analog move, asserts displacement | live round, usercmd hook |
| `movement-probe` | dumps the engine's own move convention, then tries every movement write variant (quantize/step/raw_entry) and reports which moved | live round, usercmd hook |
| `screens` | prints the state machine's tables and walks `→ LIVE → kill → DEAD → LIVE`, asserting the capability gate | — |
| `strafe-probe` | decides which yaw channel steers movement and whether it is silent (console/usercmd, silent history, raw history, command angles, step yaw) | live round, usercmd hook |
| `autostrafe` | **observation-only** check of the payload's auto-strafe/bhop: reads the menu's settings (never writes), watches the payload's own `strafe_airborne`/`strafe_writes`/`strafe_delta` while the feature runs in a live round, and reports the speed gained and the eye-angle drift (silence) | live round, features enabled in the menu's `Movement` tab |
| `move-to` | walks to `(x, y)` with `move()`; logs pos/dist/writes; stops ≤ `tol` | live round, usercmd hook |

All in attach form:
`python scripts/run-scenario.py <name> --no-launch --no-inject --keep-running [--set k=v …]`.

### 7. What is validated vs open (2026-10-15)

Validated live on **25947352**:

- launch + inject + connect; `state`, `events`, `exec`;
- `usercmd` **discrete** buttons — jump + bhop (`usercmd-move`);
- **analog movement end-to-end** — `move(forward=1.0)` walks **204 units in 1 s**
  (1.01× the engine's own console `+forward`, 203) and `move(forward=0.5)` walks
  115 (0.57×), i.e. the analog magnitude is honoured; needs the engine's units
  (`1.0`, not 450) and an unobstructed spot (`movement-probe` pins one, sweeps for
  a clear heading and waits for the teleport *and* zero velocity before each
  measurement);
- **the silent movement yaw channel** (`strafe-probe`) — a `CSubtickMoveStep` carrying
  only a `yaw_delta` (one per tick, `when = 1`) turns the walk by exactly that delta
  (**89.9°** from a 90° step, 209 units) with the pawn's eye angles **unchanged before
  and after** — so this is the **silent** auto-strafe seam
  (`look(step_yaw=<desired yaw − camera yaw>)` + `move(forward/side)` per tick). The
  alternatives are dead ends: the command's own `viewangles` steers but turns the eye
  angles with it, and neither the silent-aim `input_history.view_angles` nor the raw
  per-sub-tick input history (`CCSGOInput + 0xbd0`) affect movement;
- **screen state machine** (`screens`): main screen → `not-ready`/`in-game` false
  (the rule: `C_CSGameRulesProxy`-style signals all fail — see
  [`docs/re/client-dll.md`](re/client-dll.md) §"Am I in a game?"), `load_map` out
  of the main screen, `team-select → join_team → freeze → live`, `kill → dead`
  with `move`/`attack`/`look` refused, `wait_respawn → live`;
- **silent aim end-to-end** — camera 45° off a bot (yaw 137.06), sub-tick aim at
  yaw 92.06, hits landed (hp 67→51, 51→34) with the camera unmoved
  (`subtick-aim`, `subtick-shot`).

**Analog movement — implemented, not yet validated live.**

- `create_move_input` *derives* `forwardmove`/`leftmove` from `CCSGOInput + 0x270`
  each tick, and with `sv_quantize_movement_input` on the server **rounds the
  analog floats to integers** (only the nine `{-1,0,1}²` states survive) —
  UC 729264 / 753644;
- our plain write lands (jump works; `move_writes` climbs) but the player does not
  move — and since console `+forward` **does** move, the local movement reads the
  **engine's own input state**, not the command copy;
- `+0x270` is re-derived *inside* slot 5 by `FUN_180b5f7b0` from the per-controller
  **input entry** (array `CCSGOInput + 0xb58`, stride `0x440`, move pair at
  `+0x18`), so writing `+0x270` before the original is clobbered.
- **Analog movement — validated live (2026-10-15).** `move(forward=1.0)` walks 204
  units in a second (1.01× the engine's console `+forward`, 203) and
  `move(forward=0.5)` walks 115 (0.57×): the analog magnitude is honoured. The
  missing ingredient was the **unit** — the engine writes `±1` (`cl_forwardspeed`
  is applied downstream), so the payload's old `450` moved nothing — plus a clear
  test spot. `movement-probe` pins the spot (teleport *and* zero velocity waited
  for before every measurement), sweeps for a clear heading, kicks the bots,
  asserts the screen is movement-capable, and reports a console baseline per run.
  **`raw_entry` remains unsafe** (writing `CCSGOInput + 0xb58 + 0x18` crashed the
  game with an input-path AV, `client.dll + 0xb64a80`, and reads denormals).
- Fallback that works today: the console `+forward` path (`move-and-aim`).

Also open: **console-output capture** (`exec` returns `{queued}` only;
`-concommandpipe` is input-only), and relating `dwViewAngles` to `view_angles`.

### 8. The `move-to` scenario

`move-to` exercises the **sub-tick `move()`** (not the console `+forward`): each
tick it takes the world direction to `(x, y)` and rotates it into the view frame
using the player's current yaw (`forward = u·(cos yaw, sin yaw)`,
`side = u·(-sin yaw, cos yaw)`), so it is independent of where the player looks —
no `setang`. It logs `dist`, `pos`, `yaw`, the computed `move fwd/side`, and the
payload's `raw_writes`/`move_writes`/`cmd+0x58` every 0.5 s, and stops within
`tol` (default 5): `--set x=… --set y=… --set tol=5`.

### 9. Build / run cheat-sheet

```sh
# Host build (native)
meson setup build && meson compile -C build && meson test -C build

# Native Windows (clang-cl; from a vcvars64 environment with LLVM on PATH)
set CC=clang-cl && set CXX=clang-cl
meson setup build-payload payload && meson compile -C build-payload
meson setup build-injector injector && meson compile -C build-injector
meson setup build-overlay overlay && meson compile -C build-overlay

# Payload (cross, Windows target)
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload

# Live calibration (game running) + SDK regeneration
python scripts/calibrate-live.py            # writes calibration/*.json
python scripts/gen-sdk.py calibration payload/sdk
meson compile -C build-payload              # rebuild against the fresh SDK
```

Run flow on Windows: `schtasks /run /tn CS2Launch` (or the AGENTS.md command) →
wait for `client.dll` to load → `build-injector\injector.exe <pid>
build-payload\payload.dll` → read `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`
and `cs2-overlay-layer.log`.

Scenario harness (see §1–§8 above):

```sh
python scripts/run-scenario.py --list
python scripts/run-scenario.py status       --no-launch --no-inject --keep-running
python scripts/run-scenario.py screens      --no-launch --no-inject --keep-running
python scripts/run-scenario.py subtick-shot --no-launch --no-inject --keep-running
python scripts/run-scenario.py movement-probe --no-launch --no-inject --keep-running
python scripts/run-scenario.py move-to      --no-launch --no-inject --keep-running --set x=… --set y=… --set tol=5
```

Diagnostic env switches: `CS2_PAYLOAD_NO_DIAG`, `CS2_PAYLOAD_NO_PUBLISH`,
`CS2_PAYLOAD_PUB_THREAD` (dedicated publish thread instead of the game-present
hook), `CS2_PAYLOAD_PUB_NOSTATE`, `CS2_PAYLOAD_PUB_NOWRITE`,
`CS2_PAYLOAD_PUB_NOSHM`, `CS2_PAYLOAD_PUB_INTERVAL_MS`, `CS2_PAYLOAD_DIR`,
`CS2_PAYLOAD_NO_CONTROL`, **`CS2_PAYLOAD_MOVEMENT`** (comma-separated `strafe`/`bhop`:
the payload-native movement features, seeded into the overlay menu's `Movement` tab —
read from the **game's** environment, which is Steam's, so setting it in
`launch-cs2.cmd` only applies when that script also starts Steam; a user env var +
Steam restart, the menu, or the control commands are the reliable paths);
and on the layer `CS2_OVERLAY`, `CS2_OVERLAY_SHM`, `CS2_OVERLAY_LOG`,
`CS2_OVERLAY_NO_DRAW`, `CS2_OVERLAY_NO_WAIT`, `CS2_OVERLAY_NO_CHAIN`.

## Open items

- The overlay layer is enabled via a machine-wide implicit-layer registry entry
  (HKLM) scoped by `CS2_OVERLAY=1`; a per-user/portable registration is still
  open.
- Decide whether the Windows default should be the dedicated publish thread or
  the game-present hook (the hook path was not exercised in the validated run).
- A strict swapchain-recreation stress (resolution change) was not captured on
  Windows (the windowed/fullscreen toggle did not recreate the swapchain); the
  layer's `vkCreateSwapchainKHR` handling is unchanged from the Proton-validated
  path.
