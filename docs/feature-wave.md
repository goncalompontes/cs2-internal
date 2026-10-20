# Feature wave — plan of record (2026-10-20)

> This is the plan for the feature wave that follows the ragebot fail-closed work
> (commit `7906ce1`). It is the **plan of record**: the decisions below were taken
> with the operator, and each item names the task that implements it and the
> review that checks it.
>
> The wave is implemented by **sub-agents**, each with an explicit file
> ownership, and each change is checked by a separate **adversarial** reviewer
> agent before the next task starts on the same files.

## 0. Decisions taken with the operator

| Question | Decision |
|---|---|
| "Directional autostrafe: WASD dictates the wish direction" | The **WASD intent becomes the travel heading**; the wish is still the `AirAccelerate` optimum, pushed perpendicular to the velocity **on the side of that heading**. A/D no longer switches the feature off (it biases the heading); the legacy behaviour stays available behind a setting. |
| Chams | **True 3D material chams** (a scene/material hook in `client.dll` + `materialsystem2.dll`), not an overlay approximation. |
| Third person | **Force the game's camera** into third person (the `CViewRender` view struct), not merely "make the cheat work in 3rd person". |
| Neverlose | The Linux machine was reached over Tailscale SSH and the reversal artifacts were **pulled to this machine**; the mining report is in `docs/research/neverlose-mining.md`. |

## 1. The tasks

| Id | Task | Owns | State |
|---|---|---|---|
| **T1** | Settings & config foundation: extend `wire::Settings` (v11 → v12), replace the flat `features::EspConfig` with a sectioned, typed `features::Config`, add a persistent config store, wire the one-shot config op | `overlay/include/cs2/overlay/wire.hpp`, `payload/features/*`, `overlay/src/renderer.cpp` | in progress |
| **T2** | Menu overhaul: one shared menu builder for both renderers, a real keybind capture, the new sections, a config section | `overlay/include/cs2/overlay/menu_pages.hpp` (new), both `menu.cpp`s | pending |
| **T3** | Directional autostrafe: the WASD travel heading, the side dead-zone with hysteresis, the legacy mode, a pure host-testable geometry module | `payload/control/*` | pending |
| **T4** | ESP expansion: box styles, head box, skeleton, tracers, off-screen arrows, distance/scaling/filters, radar, sound ESP, grenade/C4/dropped-weapon ESP, per-element colours, **and the `filled_poly` rendering fix** | `payload/features/src/esp*`, `payload/render/*` | pending |
| **T5** | Third person: force the camera via the `CViewRender` view struct | `payload/features/src/thirdperson.cpp` (new), `payload/game/*` | blocked on the RE |
| **T6** | True 3D chams: a scene/material hook | `payload/features/src/chams.cpp` (new), `payload/game/*` | blocked on the RE |
| **T7** | Neverlose mining (item 6) | — | blocked on the machine |
| **T8** | The remaining clang-tidy sweep groups + the manual refactors | repo-wide | to run at the end of the wave |
| **T9** | The ESP's out-of-module data providers: the **sound** provider (the `player_footstep` / `player_sound` events are captured but *not* in the default subscription set, and the `userid` argument is a player *slot*, so the position must be resolved from the snapshot) and the **visibility** provider (cached, low-rate, reusing the ragebot's existing occlusion machinery — never a per-player-per-frame query) | `payload/control/**`, `payload/src/payload_main.cpp` | in progress |

### 1.1 The second workstream: the scenario / control API

Requested alongside the features: the scenario API and library "fleshed out
further so it is fully type safe", with a state machine that "covers more
possible game states and more possible interactions", and an integration so
"agents can directly probe and run scripts inline with the execute tool".

| Id | Task | Owns | State |
|---|---|---|---|
| **S1** | The control channel's **interface** commands: a `settings` command (get/set the 87-field feature settings block by name), a **generated** name→offset table, a `batch` command (the "run a script" primitive), a protocol version on `ping`, an `events` `since` cursor, a `log` command, and the fix for the `heatmap` `target` argument the docs promise and the code drops | `payload/control/**`, `scripts/gen-settings-table.py`, `overlay/include/cs2/overlay/settings_table.hpp` | in progress |
| **S3** | The **typed protocol layer**: a `protocol.py` with a `TypedDict` per command result, `Literal`s for the constrained strings, a recursive `WaitExpr` for the predicate DSL, a `ControlTransport` `Protocol`, `@overload`s on the request helper, and a **runtime validator** for payload/client version skew | `scripts/scenario/protocol.py`, `control.py` | in progress |
| **S4** | The **wider state machine**: more screens (`LOADING`, `DISCONNECTED`, `BUY_TIME`, `PLANTED`, `DEFUSING`, `OVERTIME`, `HALF_TIME`, `PAUSED`), more capabilities (`PLANT`, `DEFUSE`, `DROP`, `INSPECT`, `SWITCH_WEAPON`, `GRENADE`, `SCORE`, `RECONNECT`), the matching `Machine` actions, the six unannotated `Game` methods, and unit tests for the machine's pure logic | `scripts/scenario/screens.py`, `awaiting.py`, `session.py` | pending |
| **S5** | The **OpenCode integration**: a stdlib-only local MCP server exposing the API as `tools.cs2.*`, with every tool's JSON Schema **generated from the typed models**, plus the registration in the global OpenCode config | `scripts/scenario/mcp/**`, `~/.config/opencode/opencode.jsonc` | pending |
| **S6** | The **typing gate**: a `pyproject.toml` with a strict type-checker config, the Python tests wired into `meson test`, and the missing test coverage (`awaiting`, `session`, `heatmap`, the transport, the scenario registry) | `pyproject.toml`, the root `meson.build`, `scripts/scenario/tests/**` | pending |

**Why the `settings` command matters most.** The feature wave put **87
settings** into the settings block (every ESP element, the third-person knobs,
the chams switches, the movement modes) — and the control channel could not
reach **any** of them: a scenario could `exec` a console command and drive the
`CUserCmd`, but not turn on a single ESP element. `cs2_settings_set` closes
that, and it is what lets the MCP tools drive the whole feature set with no GUI
in the loop.

### 1.2 The order and the reasoning

- **S3 before S4 before S5**: the state machine consumes the typed snapshot
  (`StateSnapshotDict`), and the MCP tools' schemas are generated from the
  types. S1 is independent of all three and runs alongside them.
- **S6 last**: the typing gate must judge the *final* code, not a moving
  target.
- The **neverlose** mining (T7) is **done** - the report is in `docs/research/neverlose-mining.md` and the artifacts are local (`C:\Users\gpmpo\nea\rea-work\`, including the reconstructed loadable PE, so the analysis no longer needs the Linux box).


### Why the tasks are ordered this way

`wire::Settings` is the **transport** for the menu → payload settings and is
compiled into **two separately built binaries** (the payload and the Vulkan
layer). Letting several tasks extend it in parallel would produce silent
divergence, so exactly one task owns it and every other task consumes the
fields it defines.

## 2. The settings contract (T1)

T1 appends the fields for the whole wave so that no later task has to touch the
wire struct again:

- **ESP** — box thickness/style, head box, skeleton, tracer, off-screen arrows,
  glow, visible-only, distance, distance scaling, max distance, radar, sound,
  grenades, C4, dropped weapons, the local player, spectators, teammates, the
  maximum box scale, and the per-element colours.
- **Movement** — the strafe mode (`off` / `camera-relative` / `directional`),
  the "A/D stands down" switch, the wishspeed, the sub-tick count, and the third
  person (enable, distance, angle, pivot height, FOV, shoulder side).
- **Visuals** — chams (enable, mode, colour, visible-only, local player).
- **Menu / config** — the menu key, the accent colour, the transparency, the
  config op + token, and the config path.

## 3. The review process

Each task is followed by an **adversarial reviewer agent** whose brief is in
`C:\Users\gpmpo\AppData\Local\Temp\opencode\reviewer-role.md`. The reviewer:

- checks **spec conformance** (including silent omissions and unrequested
  additions),
- hunts **correctness** bugs (lost fields in a two-way conversion, off-by-one,
  sign/axis errors, uninitialised reads, units, edge cases, thread safety,
  idempotency of the input hook),
- checks the **house style** (`docs/style-guide.md`),
- **writes a hostile test** and proves the new logic can fail,
- checks the **second-order effects** (the Vulkan-layer path, the control
  channel, the scenario harness, the shared-memory wire format).

The reviewer reports findings with `file:line`, a severity and a concrete failure
scenario. It does **not** fix them; the implementer does, in a follow-up round.

## 3.1 The RE inputs (and a build-delta warning)

Two RE agents produced the inputs for T5 (third person) and T6 (chams). They ran
**serially** — the REA MCP server has one bound target per session and the two
agents were competing for the same Ghidra capacity, so one was stood down and
resumed after the other finished. **Use REA's MCP tools; do not start a private
`analyzeHeadless` project** — the REA project cache is warm for both targets.

**The game was updated and the repo's RE notes are stale.** The notes in
`docs/re/` were derived from `client.dll` `44af2aa0…` (build `25815307`); the
live one is `659940c1…`. Only `docs/re/occlusion.md` has been re-derived. See
the warning at the top of [`docs/re/README.md`](re/README.md) — **no address
from those notes may be used without re-verifying it on the current build.**

What the RE has already established for T5 (third person, `client.dll` build
25947352):

- the third-person switch is a **single byte**, `CCSGOInput + 0x229`; the camera
  state is pitch `+0x230`, yaw `+0x234`, distance `+0x238` (default `30.0f`),
  mode `+0x6a8`; `CCSGOInput` is `*(void**)(client + 0x223bd00)`;
- the engine **clears the flag on death / respawn**, so it must be re-asserted
  every frame from the `CreateMove` hook — the `Present` hook is too late;
- the **view struct must not be written** (it is rebuilt every frame); the
  durable state is the input object, and the `thirdperson` convar's callback
  initialises it correctly;
- the engine **publishes the real camera origin per slot** at
  `client + 0x256af48 + slot*0xc` — that is what the ESP's capsule silhouettes
  must use in third person (the **ragebot keeps using the player's eye**);
- **CS2's client implements no shoulder offset and no pivot**: the
  `c_thirdpersonshoulder*` convars are registered but never read, and the camera
  orbits the player's eye. The camera *angle* is smoothed toward the view yaw,
  so a fixed offset is damped, and the view setup's FOV field is unconfirmed.
  ⇒ of the settings the menu offers, **the distance and the on/off switch are
  real; the angle is best-effort; the pivot, the side and the FOV are not
  supported by the engine.**

What the RE has already established for T6 (material system, `materialsystem2.dll`):

- the interface is **`VMaterialSystem2_001`** (there is no `MaterialSystem`);
  `CreateInterface` is at RVA `0x990e0`, the list head at `0x1556e8`, and the
  `CMaterialSystem2` singleton at `0x163530` (vtable `0x12d920`);
- `CreateMaterial` is **vtable slot 29** of `CMaterialSystem2`; `CMaterial2` is a
  `0x7E0`-byte object with its vtable at `0x129ad0`;
- the material's colour is a **hashed named parameter** (slots 21/22), not a
  `SetColor` call;
- `debug/DebugFlat` **does not exist** in any of the four modules — a
  name-based flat-material lookup is unconfirmed;
- the **model-draw path is still unidentified** (no `DrawModel` string and no
  scene-system RTTI in this build) — this is the remaining blocker for T6.

### 3.2 The chams RE, after the resumed round

The blocker broke open, and the picture changed:

- **The scene system is a separate module** — `scenesystem.dll` (sha
  `040eb851…`), exporting `SceneSystem_002`. `CSceneSystem`'s vtable is
  `0x1805f1710`. That is why no scene-system RTTI was found in client.dll: it
  was never there.
- **The draw signature is recovered** from the MSVC RTTI lambda names:
  `CAnimatableSceneObjectDesc::DrawPrimitive(IRenderContext*,
  CMeshDrawPrimitive_t*, const IMaterial2*, const IMaterialMode*, DrawValues_t&,
  bool, …)`. The material reaches the GPU as an `IMaterial2*` and the blend and
  depth state as a **separate** `IMaterialMode*`.
- **client.dll keeps an interface-name → global table**: `client + 0x279CE48` is
  the `CMaterialSystem2*` and `client + 0x279CF10` the `ISceneSystem*` — so the
  payload can get the material system **without** `GetProcAddress`.
- `CMaterialSystem2` **vtable slot 14 is `FindMaterial(name)`** (proved from
  client.dll: `CPhysicsGameSystem::GameActivate` calls it with
  `materials/tools/show_collision_geometry_wire_nodepth.vmat`).
- **The real flat material is `materials/physics/flat_basic.vmat`**, with
  `…_wire_nodepth.vmat` for the see-through variant. `debug/DebugFlat` stays
  refuted.
- The name→value hash is **MurmurHash2, case-folded, seed `0x31415926`,
  `h = len ^ seed`** — reproduced exactly: `0x5D700411` = `"RepresentativeTexture"`.
- **CS2 ships its own wallhack path**: `CPlayerVisibilityDataColorSceneObjectDesc`
  with the convars `r_player_visibility_mode` / `_strength` / `_stencil`, the
  scene named value `"PlayerVisibilityDataColor"`, a colour vec4 written to the
  scene desc at `+0x5D4` / `+0x5DC`, and the materials
  `materials/dev/player_visibility*.vmat`. **A "see enemies through walls"
  rendering mode may therefore be reachable without a material override at
  all** — the cheapest possible chams, if the convars do the job.
- `C_BaseEntity`'s vtable is `0x181c47bd0`, and only **2 of its 96 slots** touch
  the scene system ⇒ the entity-side model submission is **not** a
  `C_BaseEntity` virtual. The client schema provably contains **no**
  model-material field.

**Still open for T6:** the client.dll function that submits the player *model*
to the scene system (the classic chams hook, and where the entity pointer is
available); the exact vtable slot of `DrawPrimitive`; how to identify an
"enemy player" from inside the scene-system draw; the parameter names behind
`0xCF382AB` / `0x28C67ADC` (they are in the game data, not in any module); and
the "set material parameter" API.


## 4. Worktree discipline

Several agents work in parallel, so **each agent gets a git worktree** — its own
directory, its own branch, its own build directories. (An earlier plan used one
shared tree with several Meson build directories per slot; that still left the
*source* files shared, which is the real hazard. Worktrees remove it: no two
agents can touch the same file.)

| Worktree | Branch | Task |
|---|---|---|
| `C:\Users\gpmpo\cs2-wt\t2` | `feat/t2-menu` | the menu overhaul |
| `C:\Users\gpmpo\cs2-wt\t3` | `feat/t3-strafe` | the directional autostrafe |
| `C:\Users\gpmpo\cs2-wt\t4` | `feat/t4-esp` | the ESP expansion |
| `C:\Users\gpmpo\cs2-wt\rev` | `review/wave` | the adversarial reviews |

`C:\Users\gpmpo\cs2-internal` is the **integration tree**: the orchestrator merges
each task branch into `feat/occlusion-penetration` there, in dependency order, and
runs the full verify loop on the merged result.

Each worktree carries its own `build/`, `build-payload/`, `build-overlay/` and
`build-injector/` (they are gitignored), so the agents never fight over ninja's
state.

### 4.1 The integration order and the conflicts to expect

The branches are merged into `feat/occlusion-penetration` in the integration
tree in this order, and the **expected conflicts** are listed so they are
resolved deliberately rather than with `--ours`:

| # | Branch | Merged | Expected conflict |
|---|---|---|---|
| 1 | `feat/t2-menu` | 1st | none |
| 2 | `feat/t3-strafe` | 2nd | `payload/src/payload_main.cpp` — T3 adds the strafe settings to `on_movement_settings` |
| 3 | `feat/t5-thirdperson` | 3rd | `payload/src/payload_main.cpp` (the same function) and `payload/features/meson.build` (the new source) |
| 4 | `feat/t4-esp` | 4th | `payload/features/meson.build` (the new sources) |

Every conflict is **additive** (two agents adding a different line to the same
function or the same source list), so they are resolved by keeping **both**
sides. Do not resolve with `git checkout --ours` / `--theirs` — read the hunk
and keep both changes.

The base for all of them is T1's commit (`206e81c`) plus the config-op wiring
(`ae49b72`), so the settings struct, the typed config and the config store are
already in every branch.

### 4.2 The review pipeline

Each landed branch gets an **adversarial review** in the `rev` worktree, using
the dispatch template in the session temp dir
(`review-prompt-template.md`). The reviews are **serial** — they share the
`rev` worktree — and they run **alongside** the next task's implementation,
which is in its own worktree and therefore unaffected.

## 5. The style sweep (T8)

`scripts/clang-tidy-fix.py` runs one check group per commit. The sweep already
ran `modernize-safe`, `modernize-types` and `trailing-return-type`; the
remaining groups (`nodiscard`, `const`, `performance`, `readability`,
`modernize-extra`, `readability-extra`, `misc-extra`) are deferred to the end of
the wave so the new code is swept together with the old.

The `trailing-return-type` group found a real break (the solver test's wall
lambda: a deduced return type turned into a spelled-out
`std::optional<OccluderCell>` moved the name lookup out of scope). **Every sweep
group is verified with the full loop before it is committed** — the sweep
aborts on the first failing group rather than committing a broken tree.

## 6. Candidate follow-ups (item 5 — "other features you think are important")

Ranked by (value ÷ effort) for this codebase, given what already exists. Each
would be its own task with its own spec and review.

| # | Feature | Why it fits here | Effort |
|---|---|---|---|
| 1 | **Triggerbot** — fires when the crosshair is over a target at a computed hit chance, with a delay/FOV/reaction-time model | The ragebot already computes a per-cell hit probability (`payload/aim`'s damage field). A trigger is the *safe* half of the same machinery: no angle is written, so it cannot break the shot model. It reuses `aim::solve` rather than duplicating it — and the expensive solve is gated behind cheap preconditions, because the ragebot's solve is measured at ~5 ms of the tick at the best spot. | med |
| 2 | **Anti-aim (fake-lag / desync / jitter)** — the movement side | Purely `CUserCmd` work, in the same module as the strafe and the bhop (`payload/control`). The repo already has both halves of the desync primitive: the **silent** `input_history` angles (what the client renders) and the **command's** own `viewangles` (what the server sees). High value, self-contained. The Neverlose reversal has a fake-lag cluster (`NEXT_SESSION_PROMPT.md` §7.4) to compare against. | med |
| 3 | **Hit log / damage indicators** — a killfeed with the hitbox hit, the damage and the distance | The payload already captures `player_hurt`, `player_death` and `weapon_fire` **with their arguments** (`payload/control/src/events.cpp`). This is nearly free and gives the operator the feedback loop the ragebot work is missing. | low |
| 4 | **Movement QoL** — auto-crouch in combat, ledge auto-jump, silent jump, ground auto-strafe, auto-backstab | All in `payload/control`, all reusing the existing movement model and the `on_ground` / velocity reads. Each is a small, independently testable feature. | low-med |
| 5 | **World-ESP completion** — smoke's *active* radius, molotov fire area, flashbang/HE blast radius, grenade trajectory preview | T4 adds the grenade/C4/weapon markers; the *volumes* (what the smoke actually blocks, where the molotov will burn) are the part that is actually useful in a round, and they need the projectile's radius and velocity. | med |
| 6 | **Anti-flash / auto-look-away, auto-defuse, auto-pickup** | Trivial, and each is a small, well-scoped feature that needs no new engine access. | low each |

**Deliberately not proposed:** anything whose purpose is to hide the payload from
a third party (HWID spoofing, anti-cheat evasion, stream-proofing). That is
outside the scope of a feature wave, and the repository's loader is already
written for the operator's own use.
