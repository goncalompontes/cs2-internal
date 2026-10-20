# Weave CS2 — ragebot, lagcomp, prediction and command plumbing

> Research note, 2026-10-20. The only public CS2 tree with a *working* ragebot.
> Companion to [`cs2-chams-t7.md`](cs2-chams-t7.md) (the chams half of the same
> tree) and [`cheat-dump-inventory.md`](cheat-dump-inventory.md) §2.1.
>
> **Artifact.** `cacamelio/weave-v3-csgo-and-cs2`, `cs2/counterstrike2/` subtree.
> Quarantine: `C:\Users\gpmpo\nea\quarantine\src\weave-v3-csgo-and-cs2\`.
> No licence file ⇒ all rights reserved. Read-only reference; describe, never copy.
>
> **Provenance caveat.** Unverified (leak vs community port). The tree is
> internally consistent and the CS2 plumbing matches the real game, but every
> byte pattern and hard offset below is build-fragile — treat as *how it was
> done*, not as *what to copy*.
>
> **Confidence.** `verified` = read in the source at the cited file:line;
> `inferred` = interpretation; `unknown`.
>
> **Headline.** The ragebot is a **CS:GO algorithm mechanically lifted into
> CS2's protobuf command model**. The CS2-specific work is all in the
> *plumbing* (protobuf command, input history, interp blocks, schema offsets,
> the engine's own bullet tracer); the *decision logic* (hitboxes, damage,
> hitchance, multipoints) is the legacy algorithm, with several parts
> unfinished, dead, or broken. The plumbing is what we need; the decision
> logic is a baseline we can already beat.

---

## 1. File map (the parts that matter)

| Path | Size | State |
|---|---|---|
| `feature/ragebot/ragebot.cpp` | 36.8 KB | the whole ragebot: config, target scan, multipoints, hitchance, nospread, autostop, fire output |
| `feature/ragebot/ragebot.hpp` | 5.6 KB | `c_rage_bot`, `c_ragebot_config`, `c_points`, `c_best_scan`, `c_firebullet` |
| `feature/antiaim/antiaim.cpp` | 5.9 KB | pitch, yaw, at-target, 4 jitter modes — **CS1-shaped, no desync** |
| `feature/penetration_system/firebullet_cs2.cpp` | 3.7 KB | **the live path**: the game's own CS2 bullet tracer |
| `feature/penetration_system/firebullet_csgo.cpp` | 11 KB | legacy CS:GO penetration model — compiled but **unreachable** except two damage helpers |
| `feature/penetration_system/fx_firebullet.cpp` | 623 B | dispatcher, gated on `sv_penetration_type` |
| `feature/lagcompensation/lagcompensation.cpp` | 10.7 KB | per-entity record buffer, apply/restore, server lerp math |
| `feature/prediction_system/prediction_system.cpp` | 4 KB | re-enters the engine's prediction from `CreateMove` |
| `feature/legitbot/legitbot.cpp` | 12.5 KB | FOV scan, smoothing, silent aim, triggerbot, RCS |
| `feature/movement/movement_.cpp` | 17.7 KB | bhop, autostrafe, slowwalk, fast-stop, autopeek |
| `feature/dormant/*` | — | **stubs** (0-byte .cpp, empty class, null global) |
| `sdk/classes/user_cmd.hpp` | 10.1 KB | the CS2 protobuf command model — the most valuable file here |
| `sdk/schema_system/schema_system.cpp` | — | schema offsets **fetched from the author's server**, not baked in |
| `sdk/classes/trace.hpp` | 7.3 KB | `c_trace_data` filter masks, `c_surfaces_data` |

The tree also contains ~10 Rider/VS **`.TMP` merge backups** that are *larger
and older* than the checked-in files (e.g. `player_esp.cpp` 14.5 KB vs TMPs
18–20 KB, `chams.cpp` 11.8 KB vs TMP 20 KB). The leak is a **working tree**,
and the TMPs may hold a different revision of several modules. `verified`

---

## 2. The ragebot pipeline

`c_rage_bot::on_post(c_user_cmd*)` — `ragebot.cpp:917`, called from the
`CreateMove` hook (`hooks/vtables/csgo_input.cpp:223`).

**Order** (`verified`, `ragebot.cpp:919-1205`):

1. revolver cock, then **autostop**;
2. sanity gates (connected, alive, group ≠ −1, not knife, not grenade);
3. **target bucketing** — enemies are distributed into per-tick buckets of at
   most **2 players** (`:972`, `:984-990`); only the bucket for the current
   tick is scanned. This is how the tree copes with the 256-trace hitchance
   cost.
4. **two scan passes**: a *predicted* pass (eye position replaced by
   `eye + velocity*0.03`, `:1027`) over all bucketed enemies, then a *real*
   pass that breaks at the first success and caches the player
   (`m_better_player`, `:1080`);
5. `vector_angles(best_point − eye)` → the aim angle (`:1094`);
6. auto-scope for snipers; autostop re-run;
7. **accuracy gate**: `no_spread()` **or** `hitchance()` (`:1114-1123`);
8. **aim-punch compensation** — subtracts `aim_punch_cache`'s last element × 2
   (`:1127-1131`);
9. fire gate `can_shoot(...)`, then auto-fire (alternating attack button per
   `sequence % 2`), then the **angle output** (below).

**Angle output** — the CS2 part that matters (`:1169-1205`):

- unless `ragebot_hideshots`, the yaw is written to
  `cmd->csgo_user_cmd.base->view->angles.y` (`:1180`);
- `cmd->set_input_history_angle(angle)` (`:1184`) writes the aim angle into
  **every** input-history entry — this is the CS2 **silent aim**;
- `cmd->override_tick(history_size, best_tick, 0)` (`:1197`) with
  `best_tick = TIME_TO_TICKS(record.sim_time + client_interp_amount)` (`:1189`)
  — the CS2 **fake-tick** mechanism;
- `Interfaces::csgo_input->SetViewAngle(angle)` (`:1204`) for the visible
  camera, unless `ragebot_auto_silent`.

---

## 3. Hit-chance: 256-sample Monte-Carlo with the game's own RNG

`c_rage_bot::hitchance(pos, matrix, player, index)` — `ragebot.cpp:477`.

**Spread model** (`calculate_spread`, `ragebot.cpp:163`):

- seeds the **game's** RNG through a pattern-scanned `random_seed`
  (`utilities/utilities.cpp:101-105`) and draws with `random_float` (`:108-112`);
- `r1 = rand(0,1)`, `r2 = rand(0,2π)`; `r3`/`r4` come from the weapon's own
  spread pattern when `weapon_accuracy_shotgun_spread_patterns` is on, else
  `r3=r1`, `r4=r2`;
- special cases: revolver (`r = 1 − r²`), Negev first shots (`:191-205`);
- the direction offset is the sum of an accuracy term `r1*accuracy` and a
  spread term `r3*spread`, rotated by `r2`/`r4` (`:213-220`).

**Sampling loop** (`:546-565`): **256 iterations**; each builds a direction
from the aim vector minus `right*spread.x` plus `up*spread.y`, casts a ray from
the eye position out to `weapon_data->range()`, and traces it with
`Interfaces::trace->clip_ray_to_entity` using filter mask `0x1C3003`, layer 4
(`:544`, `:561`). A sample counts as a hit iff `trace.entity == player` (`:563`).

**Threshold** (`:533`): `needed = ceil(hitchance * 255 / 100)`; pass iff
`hits >= needed` (`:567`). At 100 % that means 255 of 256 samples must hit.

> **Two weaknesses worth copying-around, not copying.** (a) The trace never
> checks `m_nHitBoxIndex`, so "hitchance" means *hits the player's collision,
> any hitbox* — not the intended hitbox. (b) `pos` and `matrix` are parameters
> that are **never used**; the function reads the member `best_point` instead.
> Both are `verified` at `ragebot.cpp:563` and `:477`.

**The alternative: hash-seeded no-spread** (`no_spread()`, `ragebot.cpp:570`).
Instead of sampling, it asks the engine for the **real** hash seed of the
current shot — `get_hash_seed(local_player, best_point, prediction_tick + 1)`
(`:158`, `:611`) — and requires a single trace to hit **the intended hitbox**
(`trace.entity == player && trace.trace_model->m_nHitBoxIndex == index`,
`:620`).

**This is the most interesting idea in the tree.** In CS2 the server-side
spread for a shot is derived from a **hash of the shooter state and the aim
point**, so a client that can compute that hash can *know* whether the shot
will hit, without sampling. `inferred` — the exact hash inputs are the
pattern-scanned `get_hash_seed` inside the game. If we can compute or predict
that seed, we get an **exact** hit test instead of a 256-sample estimate, and
the cost collapses from 256 traces to one.

---

## 4. Point generation

`c_rage_bot::multipoints(pawn, bones, index, points, predicted)` —
`ragebot.cpp:626`.

- hitbox data comes from the **model** (`get_hitbox(index)`, `:645`), the bone
  index from the pawn's bone name (`:650`);
- **predicted scan**: exactly one point per hitbox — the raw bone position
  (`:653-656`);
- **multipoint gating** per hitbox group (`:658-692`): if the group's
  `ragebot_multipoint_*` flag is off, only the bone position is used;
- **radius** (`:694`): `hitbox_shape_radius * (point_scale / 100)`;
- **point shapes** (`:706-732`), all transformed by the bone matrix:
  - *head*: 3 points — two lateral offsets of ±r at the hitbox max corner, and
    a third offset diagonally ("mini peka");
  - *chest / upper_chest / lower_chest / stomach*: 3 points — ±r along two
    local axes;
  - **every other hitbox (neck, pelvis, arms, legs, feet): the bone position
    only** — limbs never get multipoints.

**Two bugs in the leaked revision** (`verified`):

- `point_scale` is divided by 100 **twice** — once when the config is loaded
  (`:78`) and once at use (`:694`) — so a 1–100 slider yields 1 % of the
  hitbox radius, not 100 %;
- `if (radius <= 0) return false` (`:701`) fires *before* the single-point
  fallback, so with the default/unset scale the multipoint path yields
  **nothing at all**.

So the shipped tree's multipoints are effectively dead. The *design* is
still worth having; the *arithmetic* is not.

There is **no adaptive scaling** — one static slider per weapon group, no
per-hitbox scale, no distance- or damage-based adaptation. `verified`

---

## 5. Damage model

- `HitboxToHitgroup` (`:330-350`) maps the 19 hitboxes to head / neck→head /
  stomach / chest / arms / legs.
- `setup_hitboxes(pawn)` (`:352`) computes `weapon_data->damage()`, scales it
  per hitgroup, and **keeps a hitbox only if the scaled damage ≥
  `min(target_health, configured_min_damage)`** (`:375`).
- `c_firebullet::scale_damage` — `firebullet_csgo.cpp:36`:
  - head → `headshot_multiplier * mp_damage_scale_{ct,t}_head` (team-dependent,
    `:15-16`, `:21`);
  - stomach → `1.25 * body_scale`; legs → `0.75 * body_scale`; chest/arms →
    `body_scale` (`:24-31`);
  - **armour**: if `armor_value > 0` → `damage *= (armor_ratio * 0.5)`; for a
    headshot the reduction applies **only if the target has a helmet**
    (`:43-56`) — a deviation from the real game, where armour reduces
    headshot damage regardless of the helmet. `verified`
- the **per-shot** damage the ragebot uses comes from the *engine's* tracer
  (`firebullet_cs2.cpp:108-109`), with `scale_damage` applied afterwards to
  the trace's hitgroup.

**The min-damage rule is a "lethal or not" test in disguise**:
`damage >= min(target HP, configured min damage)`. With the configured
minimum ≥ the target's HP it degenerates to "only shoot if lethal". `verified`

---

## 6. Penetration: the engine prices it, not the cheat

`c_fx_firebullet::run` — `fx_firebullet.cpp:6` — dispatches on
`sv_penetration_type`:

- null → use the CS2 tracer;
- `== 2` → use the CS2 tracer;
- anything else → **return false** (`:15-18`) — the cheat refuses to trace at
  all, so the whole ragebot scan produces no damage.

`c_firebullet_cs2::run` — `firebullet_cs2.cpp:68`:

1. allocates and zeroes a `c_trace_data` (`:84-86`);
2. `init_trace_data(trace_data, start, end, filter, pen_count)` — a
   pattern-scanned game function, signature
   `(c_trace_data*, vector, vector, c_trace_filter*, int)` (`:44-48`);
3. `run_trace(trace_data, damage, penetration, range_modifier, pen_count=4,
   teamnum, sv_showimpacts_penetration)` — signature
   `(c_trace_data*, float, float, float, unsigned, unsigned, int*)` (`:56-66`),
   fed with `weapon_data->damage()`, `->penetration()`,
   `->range_modifier()`;
4. iterates the sub-traces and reads each with `get_trace` (`:96-102`); when
   the hit entity matches, it takes `trace_info.damage` and applies
   `scale_damage` to the trace's hitgroup (`:109-110`).

**So in CS2 the penetration model is the game's own** — damage falloff,
thickness limits, material rules all live inside `run_trace`. The cheat does
not model penetration; it *prices candidate points with the engine's tracer*
and lets the engine decide. The legacy CS:GO model in `firebullet_csgo.cpp`
(the classic `trace_to_exit` / `handle_bullet_penetration` with
`penetration_modifier`/`damage_lost_percent` constants) is compiled but dead
on this path.

**Consequence for us.** Our damage field is a *client-side* model of what the
engine will do. Weave's approach — call the engine's own tracer and read its
damage — is exact but costs a trace per candidate point. The two are
complementary: the field can *rank* candidates cheaply, and the engine tracer
can *price* the finalists. That combination is worth building.

---

## 7. Lag compensation: a client-side record buffer

`feature/lagcompensation/lagcompensation.cpp`.

- At `frame_net_update_end` (`hooks/vtables/source2.cpp:73`) it forces
  `calc_world_space_bones` for every enemy controller and pushes a new
  `c_lagrecord` at the **front** of a per-entity deque, capped at **32**
  records (`:50`, `:65-66`, `:71`).
- `c_lagrecord::setup` (`:230`) stores sim time, the **choked** tick count
  (`TIME_TO_TICKS(sim_time − old_sim_time)`, `:237`), origin/angles/eye/velocity/
  flags, and a **memcpy of the whole bone cache** (`:250`).
- `c_lagrecord::apply` (`:253`) writes the record back into the live pawn
  (sim time, scene origin, flags, angles, eye, velocity) and memcpy's the
  record's bones over the live bone cache (`:278`); with extrapolation it
  advances every bone by `velocity * 0.017 * (outgoing_latency*64 + 0.5)`
  (`:280-285`).
- `restore` (`:288`) puts the backup matrix back.
- `get_delta_time(tick_entity)` (`:88-209`) is a **decompiled copy of the
  server's interpolation-lerp maths**: `sv_maxunlag` clamped to 0.2, the
  `fmod(maxunlag, 0.015625)` remainder, and the tick/fraction arithmetic with
  outgoing/incoming latency. **This is the server's own interpolation
  re-implemented in the client** — the exact thing our backtrack work needs.
- `record_of_bounds()` (`:302`) picks the usable window: `maxunlag/3`,
  `maxunlag/2` or `maxunlag`, selected by the `ragebot_track` setting
  (Low/Medium/High) — i.e. **the cheat limits its own backtrack to a
  fraction of the server's tolerance**.
- The ragebot then scans **at most two records per target**: the newest, and
  the oldest one still inside the bounds (`:804-822`), applying each and
  restoring afterwards (`:830`, `:896`). There is **no 64-tick history scan**.

> A `breaking_lag_comp` flag (a target whose sim time did not advance, i.e. a
> choked target) is computed at `:238` and **never read** — the resolver that
> would have used it is missing. `verified`

---

## 8. Prediction

`feature/prediction_system/prediction_system.cpp`:

- `update(cmd)` (`:18`) caches realtime/curtime/tick count/on-ground/spread/
  accuracy, then **re-enters the engine's prediction**: `has_been_prediction`,
  `set_prediction(true)`, `m_prediction->in_prediction = true`,
  `m_first_prediction = false` (`:38-41`), calls the pattern-scanned
  `run_prediction` (`:43-44`), and then forces
  `curtime = TICKS_TO_TIME(local_player->get_controller()->tick_base())`
  (`:46-47`);
- `begin(cmd)` (`:50`) sets the prediction command; `end()` (`:67`) restores
  everything;
- hook order in `CreateMove`: `prediction_system->update` → `begin` →
  features → `end` (`hooks/vtables/csgo_input.cpp:217-218`, `:230`).

The classic CS:GO "hook RunCommand and run prediction inside it" trick is
**not** used — the `run_command`/`run_simulation` hooks are pure passthroughs
(`hooks/vtables/prediction.cpp:6-15`); prediction is forced inside
`CreateMove`. `verified`

---

## 9. The CS2 command model (the most reusable part)

`sdk/classes/user_cmd.hpp` + `sdk/classes/user_cmd.cpp`.

| Element | Detail | Line |
|---|---|---|
| Command type | the **protobuf** `ccsgo_user_cmd_pb`, not the legacy `CUserCmd` | `:240-256` |
| Base | `c_user_cmd_base` (angles, buttons, view) | `:212-236` |
| Input history | `RepeatedPtrField_t<csgo_input_history_entry>` | — |
| Attack history | `attack_history_index[3]` | — |
| History entry | `angle`, `pShootPosition`, 3 target-check angles, `cl_interp`, `sv_interp0`, `sv_interp1`, `player_interp`, `render_tick_count/fraction`, `player_tick_count/fraction`, `frame_number`, `nTargetEntIndex` — **size asserted 0x78** | `:175-196` |
| History bits | `EInputHistoryBits` — `0x1` VIEWANGLES, `0x800` PLAYERTICKCOUNT, `0x1000` PLAYERTICKFRACTION, … | `:23-40` |
| **Silent aim** | `set_input_history_angle` writes the angle into **every** history entry | `user_cmd.cpp:111-126` |
| **Fake tick** | `override_tick` rewrites `render_tick_count` and the `cl_interp` / `sv_interp0` / `sv_interp1` src-dst pairs | `:289-344` |
| **Attack history** | `set_attack_history(index)` sets `attack_history_index[0..2]` and the `ATTACK1/2/3START` bits — the server needs this to treat the right sub-tick as the attack | `:71-78` |
| Sub-tick steps | `base->subtick_move.m_nCurrentSize = 0` — **wiped every tick**; `add_subtick`/`create_subtick` exist but are never called | `hooks/vtables/csgo_input.cpp:197`; `user_cmd.cpp:84`, `:98` |
| **Move CRC** | after the anti-aim changes the movement, the command is re-serialised (`SerializePartialToArray`, pattern-scanned) and `move_crc` recomputed, or the server rejects the modified forward/sidemove | `sdk/crc.h:26-82`; `hooks/vtables/csgo_input.cpp:234-236` |
| Movement correction | movement is re-derived from the **pre-anti-aim** yaw (`store_angle` captured before the anti-aim runs) | `hooks/vtables/csgo_input.cpp:53-95`, `:204`, `:232` |
| Verification hook | `verify_input` restores the stored angle around the engine's verification | `hooks/vtables/csgo_input.cpp:239-246` |

**Why this matters to us.** Silent aim, fake ticks, attack-history indexing
and the move CRC are *server-acceptance* mechanics, not aim math. A CS2 cheat
that gets the angle right but the command wrong simply does not shoot. This
is a checklist we can implement directly:

1. write the angle into every input-history entry (silent) and/or the base
   view angles (visible);
2. set the attack-history index and the `ATTACKnSTART` bits for the tick you
   want the server to credit;
3. override the render tick and the interp blocks to the tick you are
   pretending to be on;
4. if you changed the movement, recompute the command CRC;
5. restore the stored angle around `verify_input`.

---

## 10. Anti-aim (CS1-shaped, and it shows)

`feature/antiaim/antiaim.cpp:21`.

- gates: enabled, not ladder/noclip, not frozen, not in the buy zone, not
  holding `IN_USE`, not mid-grenade-throw, and **not while the ragebot is
  firing** unless `ragebot_hideshots` is off (`:49`);
- pitch: an int combo — `1` → `+89°`, `2` → `−89°` (`:58-74`);
- base yaw: "at target" mode finds the enemy with the smallest FOV distance
  and yaws at it (`:89-132`, with a `todo` at `:126`); otherwise backwards
  (`+180°`) plus a manual offset;
- jitter: 4 modes — none, centre (side flips every call), 3-way (a counter
  applies −value / 0 / +value), random, and spin (accumulating yaw wrapped
  with `remainderf(·,360)`) (`:146-185`);
- output: `cmd->csgo_user_cmd.base->view->angles` + `BASE_BITS_VIEWANGLES`;
  the **input history is not touched**, and `CCSGOInput::SetViewAngle` is
  **not** called — so the anti-aim is server-visible but the local camera
  does not follow it (`:1202-1205` in the ragebot does call it).

**There is no desync model**: no real/fake yaw pair, no `fl_flicker`, no
`fixedyaw`, no per-tick side selection, and **no fake-lag or choking at all**
(the only `choke` token in the tree is a lag-comp field name). `verified`

This is the weakest part of the tree and it is *expected* — CS2's subtick
command model is where anti-aim implementations live or die, and this one
never got there.

---

## 11. Server gating (what the cheat will and will not do)

| Convar | Effect | Where |
|---|---|---|
| `sv_penetration_type` | must be **2** (or absent) or the cheat refuses to trace → the ragebot finds nothing | `fx_firebullet.cpp:8`, `:11-18` |
| `sv_maxunlag` | the backtrack window (`maxunlag/3`, `/2`, `/1`) and the decompiled server lerp maths | `lagcompensation.cpp:91`, `:304`; `ragebot.cpp:778` |
| `sv_autobunnyhopping` | if the server has autobhop, the cheat **disables its own bhop** | `movement_.cpp:55` |
| `sv_friction`, `sv_accelerate`, `sv_airaccelerate`, `sv_maxspeed`, `sv_jump_impulse` | the autostop/slowwalk physics model | `ragebot.cpp:285-291`; `movement_.cpp:441` |
| `mp_damage_scale_{ct,t}_{head,body}` | hitgroup damage scaling (deathmatch servers change it) | `firebullet_csgo.cpp:15-16` |
| `mp_teammates_are_enemies` | makes every other player an enemy | `c_cs_player_pawn.cpp:92` |
| `weapon_accuracy_shotgun_spread_patterns` | where the shotgun spread pattern comes from | `ragebot.cpp:180` |
| **`-insecure`** | the check is **commented out** (`sdk/interfaces/interfaces.cpp:120-123`) — the cheat does not verify it is on an insecure server, so it will run on a secure one | |
| `sv_cheats` | never referenced | |

The `sv_penetration_type == 2` gate is the striking one: on a normal CS2
community server (which uses 0 or 1) this tree's ragebot is **inert**. It was
developed against a locally hosted test server. `inferred`

---

## 12. Defects — do not copy these

`verified`, all in the leaked revision:

| Defect | Where |
|---|---|
| Point scale divided by 100 twice; `radius <= 0` aborts before the single-point fallback ⇒ **no points at all** with default config | `ragebot.cpp:78`, `:694`, `:701` |
| Predicted pass does not break on success ⇒ **last** enemy scanned wins | `:1029-1054` |
| The real pass overwrites the result even on failure ⇒ a failed real pass discards a successful predicted pass | `:1058-1089` |
| `ragebot_penetration` is a no-op: a failed trace falls through to a damage check that is always false | `:855-858` |
| Hitchance trace ignores the hitbox index | `:563` |
| `hitchance()`'s `pos`/`matrix` parameters are unused | `:477` |
| Autostop options `beetwen_shots` / `accuracy` / `molotov_ignore` are read into the config and never used | `:62-64` |
| `ragebot_norecoil` exists in the schema and persistence but no feature reads it | `flat_message/rage_settings_table.fbs:23` |
| Per-group config binding bug: the chest hitbox binds to the *head* variable | `:67` |
| `m_config.hide_shot` loaded but unused (the raw key is re-read instead) | `:84`, `:1178` |
| `c_rage_bot::AutoFire()` declared, never defined; `send_command`/`send_message` producers commented out ⇒ the whole silent/lag-comp `sub_tick_data` channel is dead | `ragebot.hpp:91`; `ragebot.cpp:1185`, `:1191` |
| `c_antiaim::freestanding()` declared, never defined | `antiaim.h:36` |
| `c_bone::copy()/set()` declared, never defined (a raw `memcpy` is used instead) | `lagcompensation.hpp:10-11` |
| Bone arrays hardcoded to 128 while the skeleton exposes a dynamic `bone_count` | `lagcompensation.hpp:13`, `:23`, `:34` |
| Build-fragile hardcoded layouts: `c_trace_data` padding, `pawn + 0x3D8`, `+0xc64/+0xc6c`, `m_pInventoryServices + 0x58` | `firebullet_cs2.cpp:17-31`; `c_cs_player_pawn.cpp:80`; `c_weapon_cs_base_v_data.hpp:71-77`; `core.cpp:174` |
| Inline pattern scans that will rot with any update | `c_cs_player_pawn.cpp:37`; `sdk/crc.h:57`, `:68`, `:71`; `source2.cpp:196` |

---

## 13. What we should take

1. **The command-acceptance checklist** (§9) — silent aim via input history,
   attack-history indexing, `override_tick` fake ticks, and the move CRC. This
   is the CS2 plumbing we are missing.
2. **The hash-seeded no-spread idea** (§3) — if the engine's shot hash is
   computable, the hit test becomes exact and cheap. Worth an experiment:
   locate the `get_hash_seed` pattern, reproduce it, and compare its output
   against our sampled hit rate.
3. **"Let the engine price the candidate"** (§6) — use our field to *rank*
   candidates and the engine's `run_trace` to *price* the finalists. Cheap
   ranking, exact pricing.
4. **The server's own lerp maths** (§7) — `get_delta_time` is a decompiled
   copy of the server's interpolation; re-deriving it ourselves is cheaper
   than guessing, and the `maxunlag/3, /2, /1` bound is a sane default for
   our backtrack window.
5. **The `min(target HP, configured min damage)` rule** (§5) — a one-line
   rule that turns a damage threshold into "lethal or not", which is probably
   what our damage gate should express.
6. **The config vocabulary** (§Config below) — adopt the names, drop the
   dead options.

## 14. Configuration surface (names only)

- **Global**: `ragebot_enable`, `ragebot_auto_fire`, `ragebot_auto_silent`,
  `ragebot_auto_scope`, `ragebot_penetration`, `ragebot_nospread`,
  `ragebot_hideshots`, `ragebot_rapidfire`, `ragebot_track` (Low/Med/High),
  `ragebot_slowwalk`.
- **Per weapon group** (suffix `_{0..7}` = Pistol, Rifle, Machine gun,
  Revolver, Deagle, Scout, AWP, Auto-sniper): `ragebot_autostop`,
  `ragebot_damage`, `ragebot_hitchance_amount`, `ragebot_multipoint_scale`,
  `ragebot_hitbox_{head,chest,stomach,arms,legs,foot}`,
  `ragebot_multipoint_{head,chest,stomach,arms,legs,foot}`,
  `ragebot_autostop_beetwen_shots`, `ragebot_autostop_accuracy`,
  `ragebot_autostop_molotov_ignore`.
- **Anti-aim**: `antiaim_enable`, `antiaim_at_target`, `antiaim_pitch`,
  `antiaim_yaw`, `antiaim_yaw_offset`, `antiaim_jitter_type`, `antiaim_jitter`.
- **Legit**: `legitbot_enabled`, `legitbot_trigger_bot`, `legitbot_draw_fov`,
  `legitbot_silent`, `legitbot_hitbox_{head,body,arms,legs}`, `legitbot_fov`,
  `legitbot_smooth`, `legitbot_standalone_rcs`, `legitbot_rcs_coefficient`.
- **Movement/misc**: `misc_autopeek`, `misc_autostrafe`, `misc_bunnyhop`,
  `misc_faststop`, `misc_duck_assist`.

## 15. Gap analysis against *our* payload

`verified` by grepping our own tree (`payload/control/…`).

| Weave/CS2 mechanism | Our state | Action |
|---|---|---|
| Write the aim angle into **every** `input_history` entry (silent aim) | **done** — `input.cpp:1894-1902` (`write_silent_angles`), history entries at PB `+0x20`/`+0x28`, `view_angles` at `+0x18` | none |
| Sub-tick view angles | **done** — `input.hpp:260`; the raw per-sub-tick array on `CCSGOInput` (count `+0xbc8`, stride `0x60`, angles `+0x10`) is the seam we steer | none |
| `attack1_start_history_index` (`pb+0x3c`) / `attack2_start_history_index` (`pb+0x40`) | **known, not written by us** — the *engine* writes them when it builds the command (`docs/re/subtick.md:181`); Weave sets them explicitly (`user_cmd.cpp:71-78`) | only if shots are misattributed to a tick |
| Movement without a stale command CRC | **we are better**: we write the **raw input axes** (`CCSGOInput + 0x270`, `input.cpp:1743`) and the engine re-derives `base->forwardmove` from them (`input.cpp:68`, `:761`), so the `move_crc` field is never stale. Weave instead mutates the command and re-computes the CRC with a pattern-scanned serialiser (`sdk/crc.h:26-82`) | none — keep the raw-axis approach |
| **`override_tick`** (rewrite `render_tick_count` + the `cl_interp`/`sv_interp0`/`sv_interp1` src-dst pairs) | **absent** — no `override_tick`/`cl_interp` in our tree | **the one real gap.** It is the CS2 "fake tick" mechanism: how a shot is attributed to a *past* tick. Our lagcomp work rewinds *our* view of the target, but without the interp/tick rewrite the server still credits the shot to the current tick |
| `verify_input` hook (restore the stored angle around the engine's verification) | we store an angle and re-read the command angles (`input.hpp:15`, `input.cpp:1885-1888`) | verify |
| Server rewind window (lagcomp) | **already implemented** — `docs/re/lagcomp.md:33`: `rewind_tick = tick - (int)((unlag + interp + 0.015625 + 0.1) * 64.0)`. Weave's decompiled `get_delta_time` (maxunlag clamped to 0.2, the `fmod(maxunlag, 0.015625)` tick remainder, latency terms) is **the same family of formula, derived independently** — two sources agreeing on the server's lerp arithmetic | none — corroboration only; worth reconciling the two formulations |
| Per-weapon hitchance | our solver is analytic (damage field), not sampled | see §3: the hash-seeded path may make sampling unnecessary |

**So the single actionable gap is the tick/interp override.** The attack-history
index, the CRC and the silent-aim seam are already handled (or deliberately
avoided) in our tree.

**Layout caveat, so nobody ports the wrong struct.** Our tree distinguishes two
different history structures (`payload/control/include/cs2/control/input.hpp`):

- the **raw** per-sub-tick angle array on `CCSGOInput` (count `+0xbc8`, rep
  `+0xbd0`, **stride `0x60`**, angles at `+0x10`) — the array the command's
  `input_history` is *built from* (the engine copies entry → PB, `input.hpp:68-76`);
- the **protobuf** `input_history` inside the command (RepeatedPtrField at
  PB `+0x20`/`+0x28`), whose entries carry `view_angles` at `+0x18` with the
  has-bits word at `+0x10` (`input.hpp:84-89`) — this is what we overwrite for
  silent aim, and it is the same channel Weave's `set_input_history_angle`
  writes.

Weave's `csgo_input_history_entry` (asserted size `0x78`) is a third naming of
the same area; **the mapping is not 1:1 and must be re-derived against the live
build** before porting. Note also that our tree's build reference is
**25815307** (`input.hpp:90`) — far newer than the CS2 build Weave targeted, so
every number in that tree is stale for us. `verified` for the offsets,
`unknown` for the Weave-side mapping.

---

## 16. Open questions

- Is the **hash-seed** path (`get_hash_seed`) reproducible outside the game?
  It is the single highest-value unknown in this note.
- The `.TMP` merge backups hold a *different revision* of several modules —
  do they contain a working multipoint implementation (the checked-in one is
  broken by the double division)? A diff would be cheap and could recover the
  intended code.
- The tree fetches **schema offsets from the author's server**
  (`schema_system.cpp:14-52`, local enumeration commented out) — so its
  schema handling is server-dependent, and the *structure* of the
  schema-offset plumbing is worth reading even though the values are not.
