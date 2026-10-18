# Programmatic scenario / control harness (current feature)

**Status: control core + input implemented and validated live** (2026-10, CS2
build 25815307). `payload/control/` + the Python `scripts/scenario/` runner drive
a real game: launch via the `CS2Launch` task, inject, `exec`/`state`/`wait_for`,
plus runner-side `look`/`press`/`noclip`/`teleport` (engine `setang` and button
commands through `exec`). The `join-bot-match` scenario runs end to end and each
piece is verified live. **Game events** are implemented **and validated live**:
the payload patches the client manager's dispatch vtable (`FireEvent` /
`FireEventClientSide`) and records the fired names, and `events` /
`wait_for(event=…)` expose them. **The `usercmd` input backend is implemented and
validated live for discrete buttons**: the payload hooks
`CCSGOInput::CreateMove` and writes the `CUserCmd` button mask, and
`usercmd-move` drives `jump` and `bhop` (repeated hops) in a real game. The
**sub-tick view angles** are implemented **and validated**: `look(...,
via="usercmd")` writes every `input_history` entry's `view_angles`; `subtick-aim`
shows the write lands every tick and is *silent* (the camera does not follow), and
`subtick-shot` proves the **shot uses it** (camera 45° off a bot, silent aim, the
bot loses hp). **Analog movement is validated live** — `move(forward=1.0)` walks
204 units in a second (1.01× the engine's own console `+forward`) and
`move(forward=0.5)` walks 115 (0.57×), so the magnitude is honoured; the missing
ingredient was the engine's own units (`±1`, not 450), plus a clear spot
(`movement-probe` pins one). Remaining: **console-output capture**.
**Screens/round phases are now modelled once** in the screen state machine
(`scripts/scenario/screens.py`) instead of being re-derived per scenario: the
payload's new `level` block is the **main-screen rule** (the level's
`C_CSGameRulesProxy` entity exists — the `game_rules` global is readable at the
main menu too), and its `rules` block (from `C_CSGameRules`) gives the
authoritative `warmup`/`freeze`/`round_win_status`/buy-window signals; scenarios
gate on capabilities (`require("move")`) or drive transitions
(`ensure(Screen.LIVE)`). **The ragebot aim solve** (`docs/ragebot.md`) is exposed
as the `aim` control command and **validated live** (`aim-probe`): it points at an
enemy's head within ~0.2° and the returned damage matches the headshot model.

This is the resume document for the feature we are actively working on: a way to
programmatically drive CS2 for testing — launch, inject, join a bot match, add /
place bots, aim, run input sequences, await events, assert.

## Goal

A scriptable harness that can set up and run test situations against the real
game, e.g.:

```python
with cs2_session() as g:          # launch (CS2Launch task) + inject + connect
    g.exec("sv_cheats 1")
    g.exec("game_type 0; game_mode 0; map de_dust2")
    g.wait_for(local_alive=True, in_map=True, timeout=90)
    g.exec("bot_add_t; bot_add_ct")
    g.wait_for(players_gte=3)
    g.look(yaw=0, pitch=0)
    g.press("w", hold=1.0)
    g.exec("bot_place")
    assert len(g.state()["players"]) >= 3
```

## Decisions already made

| Topic | Decision |
|---|---|
| Transport (payload ↔ runner) | **Win32 named pipe** + NDJSON (default `\\.\pipe\cs2-internal-control`; env `CS2_CONTROL_PIPE`). Chosen over TCP/shm: pure Win32, cross-session same-user, openable from Python stdlib (`open(r'\\.\pipe\name','r+b')`), no ports. |
| `exec` (console commands) | Run **in-process** in the payload (primary): `Cbuf_AddText` = `FUN_1801b8790(&g_InputService /*RVA 0x8dbf20*/, 5, line, 0)` (RVA `0x1b8790`), thread-safe, no launch flags. `-concommandpipe` (built-in external named-pipe console channel, `-insecure`-gated) is kept as a **no-injection fallback / debug channel**; note it is input-only in this build. See `docs/re/engine2.md`. |
| Aim / `look` | **Direct view-angle control via the engine's own `setang` command** (needs `sv_cheats 1`), executed through the payload `exec` — no synthetic mouse, no offsets, no hook. Validated live. `look(yaw=…)`/`look(pitch=…)` read the current angles from `state` and keep the omitted components. |
| `press` (keys/buttons) | **Engine button commands** (`+forward`…`-forward`, `+attack`, `+jump`, `+duck`, …) through the same `exec`, *not* OS `SendInput`: the runner may be in session 0 while the game is in session 1, so synthetic input from the runner cannot reach it. `hold` = `+cmd` … sleep … `-cmd`. Validated live. |
| Scenario runner | **Python** package under `scripts/`. |
| First milestone | **Control core only**: pipe + `ping/state/exec/wait_for/quit` + RE of the console entry + Python client/launcher + a join-bot-match scenario. |

## Architecture (planned)

```
 scenario runner (Python, session 0)
        │  NDJSON over named pipe  \\.\pipe\cs2-internal-control
        ▼
 payload.dll inside cs2.exe (session 1)
   control thread ── state  → StateReader (RPM self, safe)
                  ── exec   → Cbuf_AddText (FUN_1801b8790, RVA 0x1b8790)
                                [fallback: -concommandpipe]
                  ── input  → engine button commands (+forward/…) & `setang`,
                                injected through exec (no OS SendInput)
                  ── wait   → predicate over snapshots
                  ── events → IGameEventListener2 registered with the client
                                CGameEventManager (client.dll), fed into an
                                EventRing (docs/re/client-dll.md §"Game events")
   publish thread ── shm ──▶ Vulkan overlay layer (render)
```

## Command surface (v1)

Requests are `{"id": <n>, "cmd": <name>, "args": {…}}`; responses are
`{"id": <n>, "ok": <bool>, "result": {…}}` or `{"id": <n>, "ok": false,
"error": "<message>"}`.

| cmd | args | returns |
|---|---|---|
| `ping` | — | `{pong, pid, exec_ready, events_ready, input_ready, input_hooked, input_backend, state_ready}` |
| `state` | — | snapshot: `valid`, `in_map`, `local_pawn`, `local{…}`, `players[…]` |
| `exec` | `line` | `{queued}` (semicolon-separated console line) |
| `events` | `limit`, `subscribe` | `{count, events: [{name, seq}, …]}` — recent game events (oldest first); `subscribe` is a name or array of names to register on demand |
| `wait_for` | `expr`, `event`, `timeout_ms` | `{matched, snapshot}` |
| `input` | `via` (`"console"`\|`"usercmd"`) | backend + hook status `{backend, ready, hooked, slot, slot5, buttons_set, buttons_clear, bhop, calls, writes, move_*, angles, base_has_bits, base_move, raw_move, entry_move, steps, step_dump, entry_writes, step_writes, cmd_words, base_words}` plus the bhop block `{bhop_writes, bhop_presses, bhop_a, bhop_b, bhop_skipped, bhop_landed_tick, bhop_landed_frac, bhop_build, bhop_age, bhop_tick_base, bhop_client_tick, bhop_cmd_number, bhop_mode, bhop_when, bhop_holding_ticks, bhop_air_ticks}` (`bhop_a`/`bhop_b` = landings answered with placement A / the B catch-up; **A firing is the phase proof**, validated live) and the strafe block `{strafe, strafe_airborne, strafe_speed, strafe_delta, strafe_goal, strafe_wish, strafe_rot, strafe_side, strafe_steps, strafe_reason, strafe_clamped, strafe_wishspeed, strafe_writes}` (`strafe_rot` = the basis rotation written, `strafe_steps` the yaw steps queued, `strafe_reason` why the tick did/did not act (1 ground, 2 slow, 3 no angles, 4 wrote, 5 no room), `strafe_clamped` how often the rotation had to be clamped so the gate could not drop the shared step array) |
| `press` / `release` | `buttons` (name, array, or mask); `reset` | hold / release command buttons on the active backend |
| `look` | `pitch`, `yaw`, `roll`; `clear` | write the sub-tick `view_angles` (silent; `usercmd` backend) |
| `aim` | `target` (entity index; default nearest), `objective` (`damage`\|`hitchance`\|`expected`\|`pellets`), `sigma`, `damage`, `range_modifier`, `penetration`, `headshot`, `write`, `seed`/`sequence` + `spread`/`inaccuracy`/`pellets` (for `objective: pellets`), `cone` (the hitchance cone, radians) | **ragebot solve** (`docs/ragebot.md`): solve the damage field for the enemy and return `{valid, yaw, pitch, value, hitchance, cone, cells, aim_u, aim_v, wrote_view, hitcount, hitboxes[…]}`. `hitchance` is the probability the shot (perturbed by the `cone`) lands on the target (`docs/ragebot.md` §2); the objective `hitchance` maximizes it when `cone` > 0, else it uses the Chebyshev-centre margin. `write: true` applies the solve to the **silent** sub-tick `view_angles`. `hitboxes` is the pawn's **real studio hitbox set** (bone, hitgroup, name, radius, bone-local `min/max`, world centre). `objective: pellets` maximizes the deterministic pellet sum (`docs/ragebot.md` §2/§3): the seed is `seed` or `sequence`→`MD5_PseudoRandom`, `spread` defaults to the weapon's real `m_flSpread`. |
| `rage` | `on`, `key` (VK code, 0 = none), `fire`, `force` (bypass the key check), `objective`, `damage`, `headshot`, `range_modifier`, `sigma`, `hitbox` (0 auto, 1 head, 2 body), `fov`, `min_damage`, `min_hitchance` | **hold-key ragebot** (`docs/ragebot.md`): the payload solves per frame (render thread) and writes the result per tick as the silent sub-tick aim, pressing attack while the key is held. Target selection is the nearest enemy within `fov` (degrees; 0 = unlimited); the aim/fire is gated on `min_damage`/`min_hitchance`; the solve is wallbang-aware (the enumerated solids attenuate the hitboxes with the engine penetration model). Returns `{enabled, key, fire, force, active, target, hitbox, objective, fov, min_damage, min_hitchance, occluders, backtracks, record, solves, writes, shots}`. **The overlay menu's `Ragebot` tab owns the same state** (it re-writes the settings every published frame), so a control-side set is reverted — use `force` to drive it from a scenario.
| `heatmap` | `target` (centre the chart on this entity index), `fov` (horizontal degrees, default 90), `history` (lag records per player, default 6), `sample` (toggle continuous record sampling), `clear` (empty the record store), `all` (include teammates as well as enemies), `walls` (enumerate the world solids and attenuate by penetration — `docs/re/occlusion.md`), `pen` (`simple`\|`engine` — the penetration model), `pen_mod`/`dmg_mod`/`max_pen`/`material`/`grate` (override the real per-shape surface for the engine model), `damage`, `range_modifier`, `penetration`, `headshot` | **multi-target damage heatmap** (`docs/ragebot.md`, ADR-0022): projects **every** enemy's hitboxes — and their sampled **lag records** — into one shared angular chart, valued by damage, and returns the tagged silhouette cells for the Python renderer. Returns `{valid, eye, basis, fov, extent, weapon, sampling, history, capacity, tick_base, rewind_tick, window_ticks, targets[…], cells[…], cell_count, walls{enabled,ok,faulted,records,shapes,hulls,occluders}}`. The render thread samples each enemy's hitboxes per frame into a bounded ring (default 32); `history` selects how many recent records per player join the chart. With `walls: true` the payload enumerates the world collision solids between the eye and each target (`CGamePhysicsQueryInterface::EntitiesInBox`, fault-guarded) and attenuates the silhouettes by penetration. `walls` also returns `bodies`, `type_counts` (shape-type histogram), `occluder_polygons` (the projected world solids in chart space, for the renderer), and two one-record RE probes: `material_token`/`material_float` and `compound` (docs/re/occlusion.md, docs/re/vphysics2.md). Rendered by `scripts/heatmap-view.py` / the `heatmap` scenario; `scripts/heatmap-live.py` is a **live** debug view (a local HTTP server + canvas page polling the pipe at ~10 Hz, with `mode`/`fov`/`history`/`target`/rate controls, `/_look`+`/_exec` passthrough and a `--png` frame dump) and `scripts/heatmap-walls.py` renders a base/cells/walls/occluders comparison set. The `walls` solids attenuate only where the shot to a hitbox reaches them *before* the hitbox (`OccluderCell::ray_span`, per-cell depth — `docs/re/occlusion.md`). With `pen=engine` the cells are valued with the engine's real per-surface penetration loss instead of the approximation (`docs/re/penetration.md`), and a new **`penetration`** Python render mode draws that damage with the solids and each cell's attenuation outline (`scenario/heatmap.py :: render(mode="penetration")`, `heatmap-view.py --mode penetration --pen engine`, the live viewer's *penetration* mode). |
| `bhop` | `on` | toggle the payload auto-jump |
| `move` | `forward`, `side`, `up`; `clear`; `quantize`, `step`, `raw_entry` | set the analog move; the three flags opt into the community ingredients (`docs/re/subtick.md` §"Community cross-check, second pass") |
| `quit` | — | closes the channel |

`wait_for`'s `expr` is a structured object, ANDed across keys, evaluated every
100 ms on the control thread (so a `wait_for` blocks other commands for its
duration — single-client v1):

```jsonc
{"local_alive": true} {"players_gte": 4} {"alive_gte": 2} {"local_hp_gte": 1}
{"team": "T"} {"in_map": true} {"on_ground": true} {"playable": true}
{"all": [ … ]} {"any": [ … ]} {"not": { … }}
```

`in_map` is `valid && local player present`; `players_gte` counts every player,
`alive_gte` counts `life_state == 0`; `on_ground` is the local pawn's
`m_fFlags & FL_ONGROUND`; `playable` is `alive && team ∈ {T, CT}` — **false on the
team-select screen** (`team == 0`), as a spectator, or when dead, so tests can
gate on it. Unknown keys are rejected so a typo is an error rather than an
always-true predicate.

`wait_for` also takes an optional `event` (a game-event name, e.g.
`"player_death"`): it subscribes on demand and matches once that event has fired
*after* the request started. When both `expr` and `event` are given, both must
hold. Events themselves are read with the `events` command (or
`game.events(...)`), backed by the payload's listener (below).

## Input helpers (runner-side)

Input is **engine-native** — it never uses OS input (which cannot cross sessions).
The default backend rides `exec` (engine `setang` / `+button` commands, no offsets
or hooks); `via="usercmd"` instead writes the per-tick `CUserCmd` through the
payload's `CreateMove` hook (`docs/re/client-dll.md` §"Input path"). Provided by
`scripts/scenario/control.py` and mirrored on `Game`:

| helper | effect |
|---|---|
| `look(yaw=…, pitch=…, roll=…, via=None, command=False, raw=False, step_yaw=None)` | aim. Default: engine `setang` (blocks until the eye angles reach the target — compared as the **shortest angle**, so `180` matches a reported `-180`; without that, every heading at or past due-south "never converges" and times out). `via="usercmd"`: write the **sub-tick** `view_angles` — silent for *shots* (`subtick-shot`), and **inert for movement**. `command=True` also writes the command's own `viewangles` (steers movement, but the eye angles follow — visible). `raw=True` writes the raw per-sub-tick input history (inert for movement). **`step_yaw=<deg>`** appends a `CSubtickMoveStep` carrying only a yaw delta — **the silent movement-yaw channel**: a 90° step turns the walk 89.9° (209 units) with the eye angles unchanged before *and* after. That is what an auto-strafe uses (per tick: `step_yaw = desired yaw − camera yaw`, plus `move(forward/side)`). Omitted components keep their current value. Needs `sv_cheats 1` for `setang`. |
| `press(key, hold=0, via=None)` | hold a button. Default: engine `+forward` (works for analog movement). `via="usercmd"`: set the command's button bit (payload `press`) — works for **discrete** actions (`space`/`jump`, `ctrl`/`duck`, `mouse1`/`attack`, `r`/`reload`), not analog movement. `hold` releases after that many seconds. Keys map to buttons (`w`→`forward`, `space`→`jump`, `ctrl`→`duck`, `mouse1`→`attack`, `r`→`reload`, …). |
| `release(key, via=None)` | release a button held with `press(..., hold=0)`. |
| `input(via=None)` | select/query the backend (`"console"`/`"usercmd"`) and read the hook status. |
| `buttons(press=…, release=…, reset=False)` | raw `usercmd` button overrides (a name, array of names, or a mask). |
| `bhop(on=True)` | payload auto-jump — a **tick-perfect sub-tick assist**. It acts only while the player actually holds jump (reads the engine's own `CInButtonState`), and answers each landing with a **release+press pair sharing one sub-tick `when`** placed at the engine's recorded landing fraction (`m_ModernJump`'s `m_flLastLandedFrac`), so the takeoff uses `DoJump`'s speed-preserving *alpha* path instead of being a whole tick late. A lone press is not a press edge while the key is held (the edge machine is `C || (A && B)`) — the pair is what makes one. Read-only (no engine calls); re-derived and re-appended every frame; owned by the overlay menu's `Movement` tab (`wire::Settings.bhop`), so a control command is corrected on the next frame. See `docs/re/prediction.md` §"RE findings" and `docs/re/research-bhop.md`. |
| `strafe(on=True, wishspeed=…, subtick=…)` | the payload's **silent auto-strafe** (airborne only): the goal heading comes from the held keys in the view frame (`W`/nothing forward, `S` behind; a held `A`/`D` stands it down), and the air-acceleration wish is pushed perpendicular to the *current* velocity at the `AirAccelerate` optimum `theta*`. `subtick=8` (**default**, validated live: **+85.1 median / +104.9 max u/s per hop**, scaling with the slice count) keeps the analog on the valid set and rotates the movement basis with `N` sub-tick `yaw_delta` steps, each set from a per-slice simulation (`ref_air_accel_sim`) so every slice lands on `theta*`; `subtick=0` carries the direction in the camera-frame analog instead (nearly flat). Owned by the menu's `Movement` tab. See `docs/re/strafing.md`. |
| `move(forward=…, side=…, up=…, clear=False, quantize=False, step=False, raw_entry=False)` | `usercmd` analog movement — **validated live**: `forward=1.0` = 204 units/s (1.01× the console `+forward`), `forward=0.5` = 115 (0.57×). **Units are the engine's own: `1.0` = full speed** (a console `+forward` writes `1.0` into the raw axes *and* the command's `forwardmove`; measured 2026-10-15 — `450` was a wrong guess). The payload writes the raw axes (`CCSGOInput + 0x270`) plus the command floats/buttons; `quantize` snaps to the `sv_quantize_movement_input` valid set `{-1, 0, +1}` and `step` pushes the residual onto the sub-tick analog channel. **`raw_entry` is unsafe** (writing `CCSGOInput + 0xb58` crashed the game in the input path, 2026-10-15) — do not enable it. Measure with `subtick-move` / `movement-probe` (which kicks the bots, pins a test spot, waits for the teleport *and* zero velocity, and sweeps for a clear heading first — a blocked walk otherwise masquerades as a dead write). |
| `noclip(bool)` / `teleport(x, y, z=None)` | `noclip` / `setpos` (needs `sv_cheats 1`) — positioning for tests. |

Verified live: `look(yaw=90)` moved the eye angles; `press("w", hold=1.0)` moved
the player (~270 units along the facing direction); `press("space", via="usercmd")`
left the ground and `bhop(True)` hopped repeatedly; `look(..., via="usercmd")`
wrote the silent sub-tick angles without moving the camera.

### The screen state machine (`scripts/scenario/screens.py`)

Almost every silent failure in this harness has been "the test ran in the wrong
screen". The machine makes that explicit: one **screen**, the **capabilities**
that screen has, and the **transitions** between them, driven from the payload's
state (no more per-scenario guessing from raw fields).

**Signals.** The payload's `state` carries a `level` block (the main-screen rule)
and a `rules` block read from the level's `C_CSGameRules` (`client.dll`
`game_rules` global; `docs/re/client-dll.md` §"`game_rules`"):

| signal | meaning |
|---|---|
| `in_game` | **the main-screen rule** (harness-derived, not a payload field): the local player is *spawned in a level* — a non-zero origin or a team, or a running match (see `docs/re/client-dll.md` §"Am I in a game?" for why the "obvious" signals fail: the menu has a readable `game_rules`, a rules proxy and hundreds of entities, and `m_iConnected` is 0 in-game too). The canonical community rule is `CNetworkGameClient::m_bIsBackgroundMap == 0 && m_nSignonState >= 6` — a documented follow-up. |
| `level.up` / `level.proxy` | the level's `C_CSGameRulesProxy` entity exists — **true at the main menu too** (the menu is a background map); diagnostics, not the rule. |
| `level.identities` | how many entities the identity list holds (menu ≈ 258–382, level ≈ 726) — diagnostics. |
| `local.connected` | `CBasePlayerController::m_iConnected` (`+0x6f4`) — measured 0 both in the menu **and in a game** on this build; diagnostics, not the rule. |
| `in_map` | a local player snapshot exists — **also true at the main menu**, which is why it cannot be the level test. |
| `rules.loaded` | the `game_rules` global was readable — **also true at the main menu** (a default, unstarted `C_CSGameRules` with `warmup = freeze = true`, `match_started = false`; measured live 2026-10-15). A fact to report, *not* the level test. |
| `rules.warmup` / `rules.freeze` | `m_bWarmupPeriod` / `m_bFreezePeriod` — warmup, and the round started but input is frozen |
| `rules.round_win_status` | `m_iRoundWinStatus` — a winner is set ⇒ the round is over |
| `rules.t_cant_buy` / `ct_cant_buy` | whether each team's buy window has closed |
| `rules.game_phase`, `round_time`, `freeze_time`, `bomb_planted`, `match_started` | the rest of the round phase (`m_gamePhase`, `m_iRoundTime`, …) |
| `local.team` | 0 = no team (team-select/spectator screen), 1 = spectator, 2 = T, 3 = CT |
| `local.alive` | `life_state == 0`; dead players ignore input |

`Machine.ensure` also **debounces**: it only acts on a screen whose loading
progress (player count / level / team) has stopped changing, because a level that
is still connecting reports `team = 0` and looks exactly like team-select.

**Screens** (mutually exclusive; the deduction order is in `screens.deduce`):

| screen | how it is recognised | what works |
|---|---|---|
| `NOT_READY` | `valid=false` — the engine has no readable local state yet (still booting after injection, or a level teardown in flight) | nothing, and **no `exec`**: queued console lines fault inside the engine before its input service is up |
| `NOT_IN_GAME` | `in_game` false — the main screen (or a level still loading): no rules-proxy entity | nothing (`map <name>` is the transition) |
| `TEAM_SELECT` | level up, `team == 0` | nothing (`jointeam` is the transition) |
| `SPECTATOR` | `team == 1` | look |
| `WARMUP` | `rules.warmup` | move/jump/attack (no round structure yet) |
| `FREEZE` | `rules.freeze` | look, buy |
| `LIVE` | alive, in a team, not warmup/freeze, no winner | everything |
| `DEAD` | in a team, not alive (`life_state != 0`; `1` while dying, `2` when dead — the machine keys on `alive`) | nothing until respawn |
| `ROUND_END` | `rules.round_win_status != 0` (or the newest round event is `round_end`) | buy |

**Capabilities** (`state.can("move")`, `state.require("attack")`,
`game.require("move")`) are `state`, `look`, `move`, `jump`, `attack`, `reload`,
`buy`, `teleport`; `buy` additionally needs the buy window from the rules, and
`why()` returns the reason a capability is blocked (`"blocked: dead does not
allow move"`).

**Transitions** (`screens.TRANSITIONS`, driven by `Machine.ensure(target)`):

| from → to | action |
|---|---|
| `NOT_READY` → `NOT_IN_GAME` | wait (never `exec`) |
| `NOT_IN_GAME` → `TEAM_SELECT` | `game_type 0; game_mode 0; map <name>` — **never re-issued while a load is in flight** (a re-issue restarts the load; a load slower than the re-issue window then never finishes — the 2026-10-15 livelock). Each attempt gets a `load_budget` (60 s), refreshed while player/event counts still change |
| `TEAM_SELECT` / `SPECTATOR` → `FREEZE` | `jointeam <2\|3>` |
| `WARMUP` → `FREEZE` | `mp_warmup_end; mp_restartgame 1` |
| `FREEZE` → `LIVE` | wait for `m_bFreezePeriod` to clear |
| `ROUND_END` → `LIVE` | wait for the next `round_start` |
| `DEAD` → `LIVE` | wait for the respawn, else `mp_restartgame 1` |

Scenarios use it as:

```python
observation = awaiting.ensure(game, Screen.LIVE)   # drives the transitions
awaiting.require(game, "move")                     # or fail with the reason
game.ensure_screen(Screen.LIVE)                    # same, mirrored on Game
game.require("attack")
```

`ensure(..., verify_movable=True)` keeps the old liveness gate on top of the
screen: warmup/freeze/dead can all *look* live, so a console `+forward` must
actually move the player.

**Validated live (2026-10-15)** by the `screens` scenario: main screen →
`not-ready`/`in-game` false (the rule), `load_map` out of the main screen,
`team-select → wait_team → join_team → freeze → wait_freeze_end → live`, then
`kill` → `dead` with `move`/`attack`/`look` refused (capability gate) →
`wait_respawn` → `live` again. The scenario prints the tables above, so they
cannot silently drift from the code.

### Readiness helpers (`scripts/scenario/awaiting.py`)

The payload is only useful once the game is in a **started round with an alive,
movable local player**. These are the thin, backwards-compatible face of the
machine (re-exported from `scenario` and mirrored on `Game`):

| helper | effect |
|---|---|
| `state(game)` / `screen(game)` | the deduced `GameState` / `Screen` |
| `require(game, "move")` | assert a capability now, with the reason if blocked |
| `ensure(game, Screen.LIVE, …)` | drive the documented transitions to a screen |
| `machine(game, map_name=…)` | the `Machine` itself (`ensure`/`observe`/`require`/`log`) |
| `pick_clear_heading(game, home=…)` | sweep evenly-spaced yaws from `home`, hold the console `+forward` at each, and aim down the most open one — so a blocked walk cannot masquerade as a dead write |
| `recenter(game, spot)` / `settle(game)` / `prepare(game, spot)` | teleport to the test spot and wait for the move to land; wait for zero velocity and ground contact; both together (the quiet start a movement measurement needs — `setpos` does not clear velocity) |
| `ensure_map(game, map_name="de_dust2")` | load a casual offline map if no level is up |
| `ensure_live_round(game, …)` | `ensure(Screen.LIVE, verify_movable=True)` |
| `wait_round_started(game)` | wait for `round_start` + `round_freeze_end` (the raw event waits) |
| `is_movable` / `wait_movable(game)` | whether a `+forward` actually moves the player (the liveness gate) |
| `wait_airborne` / `wait_on_ground(game)` | wait for the local player to leave / touch the ground |
| `measure_move(game, key="w", via=…, yaw=…)` | hold a key and return `{before, after, moved}` (horizontal displacement) |
| `round_state(game)` | diagnostics: screen, capabilities blocked, rules, recent events |
| `local`/`origin`/`on_ground`/`alive`/`displacement` | state readers |

Use `game.ensure_live_round()` (or `ensure(game, Screen.LIVE)`) before any input
test — testing during warmup, freeze, team-select or while dead silently produces
zero movement, and `require(...)` now says so instead of failing obscurely.

## Event helpers (runner-side)

Events come from hooks on the client manager's dispatch (`CGameEventManager::FireEvent`
/ `FireEventClientSide`; `docs/re/client-dll.md` §"Game events"). The payload
validates names against the build's own event list
(`payload/control/include/cs2/control/event_names.hpp`, generated by
`scripts/extract-gameevents.py`) and keeps a small ring of the most recent events
plus a total count.

| helper | effect |
|---|---|
| `events(limit=64, subscribe=None)` | recent events, oldest first: `{count, events: [{name, seq, args}, …]}`. `args` holds the event's declared fields (e.g. `player_death`'s `attacker`, `weapon`, `headshot`). `subscribe` registers names on demand (a small default set is always watched) and reports `accepted`/`rejected`, so an unknown name is an error. |
| `wait_for(event="player_death", event_args={"headshot": True}, timeout=…)` | wait until the (validated) event fires *after* the call **carrying those field values**, AND any state predicates hold. |

Names and fields are the game's own (e.g. `player_death`, `round_start`,
`round_freeze_end`, `bomb_planted`); `not_a_real_event` is rejected rather than
silently waited on. Values are compared as strings against the captured format, so
JSON scalars match (`7`, `1.5`, `true`/`false`, a string, or a player **slot**
index for `userid`/`attacker`/`assister`). `args` holds at most 6 fields per
event (field order comes from the game's definitions).

### Input backends (implemented)

Console commands are a *proxy* for input: `setang` sets the view for one frame
and `+forward` toggles the engine's own button state. The primary path is the
**real `CUserCmd`** the client builds each tick; the console path is kept as a
fallback.

| backend | how | notes |
|---|---|---|
| `usercmd` | hook `CCSGOInput::CreateMove` (client.dll); write the `CUserCmd` button mask, the sub-tick `input_history` angles, and the raw analog move axes | exact, per-tick; **discrete** buttons (jump/bhop/attack/duck/reload) work; silent angles work; **analog movement works** (`move(forward=1.0)` = 1.01× the console path, `0.5` = 0.57×) |
| `console` (default) | `setang` + button commands through `exec` | no offsets or hooks; works for analog movement too; the fallback when the payload is not loaded |

Payload-side state (shared by both backends):

- **buttons** — a held set/clear mask (`IN_ATTACK`, `IN_JUMP`, `IN_DUCK`,
  `IN_FORWARD`, `IN_MOVELEFT`, …); `usercmd` writes it every tick, `console`
  emits `+<button>`/`-<button>` on transitions.
- **bhop** — the payload's **tick-perfect sub-tick jump assist**. It acts only
  while the player is actually holding jump (the read is the engine's own
  `CInButtonState` word 0, `IN_JUMP`), so it never jumps on its own, and it answers
  each landing with a **release+press pair sharing one sub-tick `when`**: the
  fraction comes from the landing the engine's own prediction recorded
  (`m_ModernJump.m_nLastLandedTick/m_flLastLandedFrac`, in `{build-1, f}`
  coordinates, with `build = m_nTickBase + (fresh ? 1 : 0)`), placed past the
  contact inside `sv_bhop_time_window * 0.5 = 16/64`. The pair is required because
  a lone press produces **no** press edge while the key is held
  (`edge = C || (A && B)`); it is re-derived and re-appended every frame (the
  command's sub-tick table is rebuilt each frame), budget-checked against the gate's
  32-entry limit and insertion-sorted so its monotonicity clause cannot discard the
  array. Fully read-only — no engine calls, nothing cleared. See
  `docs/re/prediction.md` §"RE findings" (and `docs/re/research-bhop.md` for the
  community sources).
- **`move(forward=0, side=0)` no longer strips your keys.** A *set* `move` request
  with all-zero axes used to take the movement bits out of every command's button
  mask, which silently removes **all** movement (found 2026-10-15: a scenario
  finishing with `move(forward=0, side=0)` left the payload in that state and the
  operator could not move at all). The write now only takes the movement bits over
  when the resolved wish is non-zero; use `move(clear=True)` to turn the override
  off for good.
- **strafe — silent auto-strafe (airborne only).** While the local pawn is airborne
  and moving faster than ~25 u/s the payload takes the goal heading from the held
  keys in the **view** frame (`W`/nothing = the camera's forward, `S` = behind) and
  pushes the air-acceleration wish **perpendicular to the current velocity** on that
  side, at the `AirAccelerate` optimum `theta* = A >= W ? 90 : acos((W - A) / speed)`
  (`W = sv_air_max_wishspeed = 30`, `A = sv_airaccelerate * wishspeed * frametime`),
  re-derived every tick so it re-perpendicularises. Nothing writes a view angle, so
  it is **silent** (the camera and the pawn's eye angles stay put). Two encodings
  (`strafe(subtick=…)`): `8` (**default**, **validated live: +85.1 median / +104.9 max
  u/s per hop**, scaling with the slice count) keeps the analog on the valid set
  `(1, 0)` and rotates the movement basis with `N` `CSubtickMoveStep` `yaw_delta`s —
  each set from a **per-slice simulation** (the engine accumulates the deltas, so an
  even split ramps the basis and only the last slice is optimal); exact under
  quantisation, and needs `sv_subtick_movement_view_angles`. `0` puts the direction in
  the camera-frame analog instead (exact when `sv_quantize_movement_input` is off, but
  the `{-1,0,1}` snap loses the perpendicular angle — measured nearly flat).
  A held `A`/`D` stands the feature down (your own air control wins). RE and the
  community sources: `docs/re/strafing.md`, `docs/re/research-strafe.md`. The `strafe`
  control command, the `wire::Settings.auto_strafe` byte and the menu toggle are all
  wired to it (menu-owned, ADR-0021).
- **bhop** — the record-based design from `docs/re/prediction.md` §"RE findings",
  validated live (`bhop_a` fires). The manual `RunCommand` prediction path crashes
  without the `CPrediction` envelope and stays off by default (`predict(on)`).
  See the wrap-up at the top of `docs/handoff.md`.

**Where the movement features are controlled.** They are **payload features**, not
something the external harness has to drive: the payload publishes the initial state
as the defaults in the shared settings block and the **overlay menu's `Movement` tab**
owns them (ADR-0021 — the same settings mechanism the ESP toggles use). The payload
re-applies the settings **every published frame**, so the menu is the single owner: a
`strafe`/`bhop` control command is corrected on the next frame (those commands remain
only as a convenience for a one-frame nudge, and scenarios should not rely on them).
`CS2_PAYLOAD_MOVEMENT=strafe,bhop` seeds the initial state, but note it is read from
the **game's** environment — the game inherits *Steam's* environment, so setting it in
`scripts/launch-cs2.cmd` only works when that script also starts Steam; use a user
environment variable (`setx` + restart Steam) or the menu for an always-on seed.
Verified live: a seqlock write of the settings block (exactly what the menu does)
turned `strafe`/`bhop` on in the payload and back off, and a stale flag left by an
earlier session no longer sticks.
- **view angles / move axes** — `console` uses `setang`; the `usercmd` path writes
  the sub-tick `input_history` angles and the raw analog move axes
  (`CCSGOInput + 0x270`) — see below.

Control surface (payload): `input` (select/query the backend), `press`,
`release`, `bhop`. Runner wrappers (`press`, `release`, `look`) take
`via="usercmd" | "console"` to override the backend per call.

### `usercmd` backend — implementation

**RE done and cross-checked** (`docs/re/client-dll.md` §"Input path"); the
payload implements it in `payload/control/{include/cs2/control/input.hpp,
src/input.cpp}`:

- On `refresh` it resolves the `CCSGOInput` singleton (`ccsgo_input`) and scans
  its vtable for the `create_move` signature (`+0xd01610`, slot 25 in build
  25815307), then patches that slot (`VtableHook`-style, ADR-0015). The RE
  anchors: `CCSGOInput` object `+0x2572460`, vtable `+0x1c97b28`, handle
  (`dwCSGOInput`) `+0x2233d10`.
- The detour calls the original first, then writes `cmd->buttons`
  (`cmd + 0x60`, the `CInButtonState` mask) from the held set/clear mask and the
  `bhop` state. Writes are **inert unless `via="usercmd"`** is selected, so the
  console path is unaffected.
- `CUserCmd`: stride `0x98` (150 per `CUserCmdArray` global `+0x221bd10`);
  command number `+0x8`, `CSGOUserCmdPB` `+0x10`, `InButtonState` `+0x58`.
- `on_ground` comes from the local pawn's `m_fFlags` (`C_BaseEntity`), noted from
  the last `state`/`wait_for` snapshot (also exposed as a `wait_for` key).
- **Movement (second pass, `docs/re/subtick.md`):** `move(...)` gained three
  default-off ingredients — `quantize` (snap the axes to the
  `sv_quantize_movement_input` valid set `{-450, 0, +450}`), `step` (append 12
  `CSubtickMoveStep`s carrying the residual the quantised base cannot express,
  `when = i/12`), and `raw_entry` (write the per-controller input entry
  `CCSGOInput + 0xb58`, entry 0 = local, so `FUN_180b5f7b0` copies our value into
  `+0x270` inside slot 5). `input()` now reports the decoded movement probes
  (`base_has_bits`, `base_move`, `raw_move`, `entry_move`, `steps`, `step_dump`,
  `entry_writes`, `step_writes`), and `movement-probe` drives the whole matrix.

**Live-validated (build 25815307).** `usercmd-move` drives `jump` (leaves the
ground) and `bhop` (repeated hops) with the write in place. The button-mask offset
is confirmed live: a console `+forward` sets `cmd + 0x60` bit 3 and `+jump` bit 1.
The hook also carries a diagnostic capture of the command's first 16 qwords
(`input` → `cmd_words`), used to map fields.

**Sub-tick view angles (implemented, silent).** `look(..., via="usercmd")` writes
**every** `input_history` entry's `view_angles` (3 floats; `docs/re/subtick.md`)
every tick — the shot reads the entry at `attack1_start_history_index`
(`pb+0x3c`), so all entries must carry the angle. `subtick-aim` validates the
write lands (dozens/tick) and the **camera does not follow**; `subtick-shot`
proves the *shot* uses it — camera 45° off a bot, silent aim at its body centre,
fire, and the bot's **health drops** (read from `state`; a `wait_for(event=…)`
started *after* the shot can never see the hit it caused).

**Analog movement works (validated live 2026-10-15).** `create_move_input`
*derives* `base->forwardmove` from `CCSGOInput + 0x270` and `base->leftmove` from
`+0x274` every tick (and re-derives `+0x270` from the input entry inside
`FUN_180b5f7b0`), so the write must land around the slot-5 call — which the payload
does (before and after). The decisive detail is the **unit**: the engine's own
input writes `±1` (a console `+forward` = `forwardmove 1.0` on the command *and*
`1.0` on the raw axes), while the payload used to write `450`
(`cl_forwardspeed`, applied downstream). With `1.0` the player walks — measured
from one fixed, unobstructed spot with a console baseline of 203 units in 1 s:
`move(1.0)` = 204.5 (1.01×) and `move(0.5)` = 115 (0.57×, the analog value is
honoured). `quantize`/`step` are neutral for a full-speed request. An obstructed
test direction or a freeze-period run reads as "no movement", which is what made
this look broken; `movement-probe` now pins a test spot, waits for the teleport and
zero velocity, sweeps for a clear heading and reports a console baseline.

The `console` backend (`setang` + button commands through `exec`) stays as the
no-injection fallback. The console-input RE anchors in the older notes
(`CreateMove` `0x1c9c334`, `CCSGOInput` `0x2320304`, `m_nButtons` `0x1c338a8`)
were **strings**, now superseded above.

## Code layout

- `payload/control/` (new): `include/cs2/control/{json,predicate,snapshot_json,
  console,events,event_ring,input,channel,service}.hpp` + `src/*.cpp` +
  `meson.build` (`control_lib`/`control_dep`). Started from `install_thread` in
  `payload/src/payload_main.cpp`, gated by `CS2_PAYLOAD_NO_CONTROL`. Uses
  `payload/game`'s `StateReader` for snapshots. `json`/`predicate`/
  `snapshot_json`/`event_ring` are platform-independent and built host-side by
  the `control-core` Meson test (`payload/control/tests/test_control_core.cpp`).
  `input.cpp` is the `usercmd` backend (hooks `CreateMove`).
- `scripts/scenario/`: `session.py` (kill stale `cs2.exe` → run the `CS2Launch`
  task → wait for `client.dll` → inject `build-payload/payload.dll` →
  connect → teardown), `control.py` (pipe client + typed helpers),
  `screens.py` (the screen state machine: deduction, capabilities, transitions),
  `awaiting.py` (readiness: map/round/alive/movable, `measure_move`, and the
  bot-match setup `ensure_bot_match`/`add_bots`/`apply_convars`),
  `scenarios/*.py` (`bot-match`, `join-bot-match`, `move-and-aim`, `game-events`,
  `usercmd-move`, `bhop`, `subtick-aim`, `subtick-shot`, `subtick-move`,
  `movement-probe`, `strafe-probe`, `autostrafe`, `move-to`, `screens`, `status`,
  `aim-probe`, `aim-shot`, `rage`);
  `scripts/run-scenario.py` (CLI).
- PID/module discovery: `external/dumper/target/release/cs2-dumper.exe
  --process cs2.exe --json`; injection: `build-injector/injector.exe`.

```
python scripts/run-scenario.py --list
python scripts/run-scenario.py join-bot-match
python scripts/run-scenario.py bot-match      # 5 bots a side + practice convars
```

`bot-match` (and `awaiting.ensure_bot_match`, or `game.ensure_bot_match(...)`
from a scenario) loads the level, applies the practice convars
(`PLAYGROUND_CONVARS`/`RESPAWN_CONVARS`: `mp_freezetime 0`, `mp_roundtime[_defuse]
60`, respawn-on-death, `sv_infinite_ammo 2`, `mp_maxmoney`/`mp_startmoney`/
`mp_afterroundmoney` 60000, a wide buy window), drives to a live round, adds
`per_team` bots to each side (each add confirmed with `wait_for`), then freezes
them (`bot_stop 1`). Knobs: `map_name`, `per_team`, `respawn`, `bot_stop`.

### Console-output capture (open)

`exec` returns `{queued}` only — the Cbuf path produces no text. `-concommandpipe`
is input-only, so console output must come from `-condebug` (console log), a
payload hook on the console-print/logging path, or a game-event listener. Until
then, `wait_for` predicates are state-based (an `exec_output`/`text` predicate is
TBD).

## RE status for `exec`

**Complete** — see **`docs/re/engine2.md`** for the full detail. Summary:

- `CInputService` singleton at `0x1808dbf20` (`&g_InputService`); its console
  command buffers live at `+0x588` (6 slots, stride `0x86d8`), drained every
  frame by `FUN_1801c6300`.
- **In-process `Cbuf_AddText`**: `FUN_1801b8790(&g_InputService, 5, line, 0)`
  (wrapper → `CCommandBuffer::AddText` `FUN_1801c70e0`). Slot 5 is the same slot
  the engine uses for its own queued/pipe input. Thread-safe (per-slot atomic
  mutex). **This is the primary `exec` for the payload.**
- **`-concommandpipe <in>,<out>`** (needs `-insecure`) is a real built-in
  named-pipe console channel: the engine is the pipe *client* (`CreateFile`
  `OPEN_EXISTING`), reads command lines from `<in>` and feeds them through the
  same Cbuf path. In this build the `<out>` handle is never written, so it is
  effectively **input-only** (no console output back); console output must come
  from `-condebug`/a payload hook/the logging system.
- Both primitives are covered by curated signatures in
  `external/dumper/src/analysis.rs` (`input_service`, `cbuf_add_text`) so the
  RVAs re-resolve across builds (ADR-0012).

## What already exists to build on (validated)

- Injection + state + overlay are proven on native Windows — see
  `docs/handoff.md` and `docs/windows-validation.md`.
- Launcher: `CS2Launch` scheduled task → `scripts/launch-cs2.cmd` (session 1).
- `payload/tls::create_thread_no_attach` for the control thread; `payload/game`
  `StateReader` for state; `payload/features` for the publish thread pattern.

## RE tooling (REA + Ghidra) — how to resume analysis

REA now has a **persistent Ghidra project cache**: a target is imported and
analyzed once and reused by later sessions (CLI and MCP). The one-time Windows
gap in the existing-project launch path was fixed upstream (PR #2358, commit
`9adad2c`); the cache itself is the new PR `feat/ghidra-project-cache` (merged
with `main`).

- Installed build: `C:\Users\gpmpo\rea-src-install` (v6.4.0 + cache), `npm link`ed
  so the global `rea` shim points there. The PR source is
  `C:\Users\gpmpo\rea-src` (branch `feat/ghidra-project-cache`). **Windows cannot
  run the merged build** — its native loader needs an addon field the prebuilt
  v6.4.0 artifact lacks, and the addon lane is Linux + MinGW.
- Ghidra **12.1.4**: `C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC`; Java 21:
  `C:\Program Files\Java\jdk-21`.
- OpenCode MCP entry (`C:\Users\gpmpo\.config\opencode\opencode.jsonc`): command
  `node C:\Users\gpmpo\rea-src-install\scripts\rea.mjs mcp`, env
  `REA_ANALYSIS_PROVIDER=ghidra`, `GHIDRA_INSTALL_DIR`, `JAVA_HOME`,
  `REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000`, **`REA_GHIDRA_PROJECT_CACHE=on`**.
- Warm targets are automatic and keyed by target, so no config edit to switch:
  `engine2.dll` (first open 256.8 s → hit ~31 s) and `client.dll` (~21 min → hit
  ~49 s) are seeded from `ghidra-analysis\rea-project` / `client-warm`. See
  `docs/re/README.md` §8 for setup, seeding and `rea cache-*`.
- The running `rea` MCP server **hot-reloads** config edits; do **not**
  `opencode service restart` (it kills the agent session). Windows Ghidra P0 is
  **read-only** (no annotation).

Warm-analysis env for CLI use:

```powershell
$env:GHIDRA_INSTALL_DIR       = "C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC"
$env:JAVA_HOME                = "C:\Program Files\Java\jdk-21"
$env:REA_ANALYSIS_PROVIDER    = "ghidra"
$env:REA_GHIDRA_PROJECT_CACHE = "on"
# Run the installed build so the native addon matches:
node C:\Users\gpmpo\rea-src-install\scripts\rea.mjs analyze <target> --provider ghidra --json
```

## Next actions (pick up here)

1. **Analog movement** (autostrafe) — **community recipe implemented, pending live
   validation.** `move(...)` can snap the axes to the `sv_quantize_movement_input`
   valid set (`quantize`), append the residual as sub-tick `CSubtickMoveStep`s
   (`step`), and write the input entry (`raw_entry`) `FUN_180b5f7b0` copies into
   `+0x270`. Run **`movement-probe`** first: it dumps the engine's own convention
   (hold `+forward`/`+jump`, read `base_has_bits`/`base_move`/`raw_move`/
   `entry_move`/steps), then measures every variant with
   `sv_quantize_movement_input` 0 and 1, so the has-bit values, the delta sign and
   the simulated command are settled by data rather than guesswork. See
   [`docs/re/subtick.md`](re/subtick.md) §"Community cross-check, second pass".
2. **Aim validation — done.** `look(..., via="usercmd")` writes every
   `input_history` entry's silent sub-tick angles; `subtick-shot` proves the shot
   uses them (camera 45° off a bot, silent aim at its body centre, health drop,
   camera stays put). Still open: relate `dwViewAngles` (the camera global) to
   `view_angles` for a "camera differing from view angles" feature.
3. **Console-output capture** for `wait_for(text=…)` and authoritative `exec`
   results. `exec` returns `{queued}` only and `-concommandpipe` is input-only,
   so output must come from `-condebug` (console log), a payload hook on the
   console-print/logging path, or the logging system.
4. Then `screenshot` and a `players_lte` / `text` predicate.

## Live validation notes (2026-10)

The full runner path works (`python scripts/run-scenario.py join-bot-match`).
A few issues cost time; all are handled now:

- **`injector.exe` is quarantined by Windows Defender after it runs** (a
  manual-map injector trips the heuristic — see `toolchain/README.md`). Rebuild
  it (`meson compile -C build-injector`) before each run, or add a folder
  exclusion.
- **Do not inject a stale/terminating instance.** `CreateRemoteThread` into a
  process that is still tearing down fails with `ACCESS_DENIED (5)`. The session
  now waits for the killed process to exit and injects the **exact PID** it
  launched (not by name).
- The payload originally imported `IMM32.dll` (which the game does not load at
  boot) via Dear ImGui's default Win32 IME handler; disabled in
  `payload/third_party/imgui/imconfig.h` (the overlay builds no Win32 backend).
  The injector still runs with `--load-missing` for robustness.
- `exec` is reliable, including `;`-separated lines. Adding several `bot_add_*`
  in one frame races the game's bot processing (an add can silently not stick),
  so scenarios pace adds and confirm each with `wait_for`.

**Game events (2026-10).** Getting events took several dead ends worth recording:

- The client manager's definitions load **per level**, not at injection: its tree
  is empty until a map loads (the payload reports `defs`/`entries`).
- `CGameEventManager::AddListener` must not be called from a non-game thread. From
  the control thread it returns `-1` for every name and, when it *does* find one,
  faults inside `tier0` (`tier0+0x1ffce9`, `read addr=0x40`).
- The engine's `CGameEventSystem` queue (`+0x100..+0x114`) is never populated in
  this build (`cap=0 data=null`), so the engine pump is not the path.
- Working approach: patch the two dispatch vtable slots (`+0x38`/`+0x40`). Their
  prologues are 18/16 bytes and contain a RIP-relative load, so the inline hook's
  16-byte trampoline rejects them (`unsupported_instruction`) — a vtable patch
  (ADR-0015 technique) works and needs no signature beyond `game_event_manager`.
- A listen server dispatches each event through both slots, so identical names
  within ~50 ms are de-duplicated.

**`usercmd` input backend (2026-10).** Validated live; worth recording:

- The hook installs cleanly (patch the `CCSGOInput` vtable slot holding
  `create_move`, slot 25) and the detour runs every tick in a live round.
- **Wait for the round to actually start.** `mp_restartgame` gives a warmup then a
  freeze period; during warmup/freeze nothing moves and the hook looks broken.
  Wait for `round_start` **and** `round_freeze_end`, then gate on a console
  `+forward` nudge actually moving the player — see `scripts/scenario/awaiting.py`.
- **A dead player ignores input.** Check `local_alive` first.
- The legacy button mask is `cmd + 0x60`: a console `+forward` sets bit 3 and
  `+jump` bit 1 (read live via the payload's `input` → `cmd_words` capture).
- **Writing that mask drives discrete actions but not analog movement.** `jump`
  leaves the ground and `bhop` hops repeatedly, but `forward` moves ~0. The
  `CBaseUserCmdPB` move floats (`+0x58/+0x5c/+0x60`) do not move either — holding
  `+forward` sets no move float in the command — so CS2's movement magnitude is
  sub-tick (`CSubtickMoveStep`). Keep analog movement on the console backend until
  the sub-tick write lands.
- **The sub-tick view angles are writable and silent.** Writing every
  `input_history` entry's `view_angles` (`docs/re/subtick.md`) lands every tick and
  the camera does not follow (`subtick-aim`) — the silent-aim seam; `subtick-shot`
  proves the shot uses it.
- `exec` can fault (`engine2!CCommandBuffer::AddText`, `read addr=0x0`) if it is
  issued *very* early after launch (before the engine input service is up); give
  the game a moment after connecting.

## Notes / cautions for the next session

- **Do not edit `.cpp`/`.hpp`** while the user's automated C++ tidy/fix pass is
  running.
- **C++ style**: follow `docs/style-guide.md` (normative: `.clang-format`,
  `.clang-tidy`, ADR-0010). New `.cpp` definitions use `auto f() -> T`
  (non-void); declarations in headers keep the leading return type.
- The generated SDK (`payload/sdk/*.hpp`) and calibration dumps
  (`calibration/*.json`) are **gitignored** and build-specific. `exec` needs the
  engine2 `input_service` / `cbuf_add_text` offsets, which
  `scripts/calibrate-live.py` now emits (it resolves `client.dll` +
  `engine2.dll`); regenerate with `scripts/calibrate-live.py` or `--file` dumps.
- Game build for all addresses here: **25815307** (patch 1.41.9.0).
