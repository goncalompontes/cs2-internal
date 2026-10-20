# Fatality CS:GO — the combat algorithms, and what transfers to CS2

> Research note, 2026-10-20. The only *readable, complete* implementation of a
> top-tier ragebot we have found in any leak — CS:GO era, but the algorithms
> are the same family our CS2 payload uses.
>
> **Artifact.** `cacamelio/fatality-source-fixed` (CS:GO), `features/`.
> Quarantine: `C:\Users\gpmpo\nea\quarantine\src\fatality-source-fixed\`.
> No licence ⇒ all rights reserved. Read-only reference; describe, never copy.
>
> **Confidence.** `verified` = read at the cited file:line; `inferred` =
> interpretation; `unknown`.
>
> **Why this note exists.** [`cs2-chams-t7.md`](cs2-chams-t7.md) and
> [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) cover the CS2 artefacts.
> This one is the *algorithm* reference: point generation, hit-chance sampling,
> the penetration model, the safety tiers, the resolver, and the anti-aim
> delivery. It is also the only source that shows a **resolver as a hypothesis
> set updated from server feedback** — the closest thing in public to the
> exactness work our own solver is missing.
>
> **Read this note for §9 (the portable list) and §6 (safe points).** The rest
> is reference.

---

## 1. The pipeline

`verified` — per command, in this order (`hooks/create_move.cpp:181-293`):

1. hittability pass per enemy (`aimbot::calculate_hittable_enemies`);
2. anti-aim choke / fake duck;
3. resolver's per-command wall-detect;
4. `aimbot::run()`:
   - **target filtering** (`aimbot.cpp:211-254`): alive, enemy, not local, not
     immune, not dormant, has lag records, inside PVS, hittable, and inside
     the FOV cone (default 180°);
   - **record selection** (`aimbot.cpp:684-812`): up to **5** lag records per
     target, chosen by heuristics rather than a full scan — the newest valid
     one, the newest in which the *target* fired, the one whose origin moved
     most, the one whose eye yaw differs most, and the newest uncrouched one;
   - **scan** → per record, `hitscan::get_best_hitpoint`;
   - **hitbox filtering** → multipoint generation;
   - **visibility/autowall + damage** per candidate point;
   - **point selection** by a scoring ladder;
   - **target ranking** by shots-to-kill, safety and server tick;
   - **corner refinement** of the chosen point;
   - a **max-hitchance pre-gate** (cheap early-out) then the full hitchance
     gate, then the delay-shot / lag-comp-break / revolver / scope gates;
   - **fire** (view angles + `IN_ATTACK`) and the resolver's shot record;
   - **post-shot re-validation** that can cancel the shot;
5. the anti-aim's send-time pass rewrites the *choked* commands' angles, but
   only if the packet is actually sent.

---

## 2. Point generation

`hitscan::multipoint_hitbox` — `features/hitscan.cpp:385-725`. `verified`

- The hitbox is a **capsule**: an AABB from the bone matrix, with a radius.
- For each (lag record, resolver hypothesis) pair it generates **24 raw 2-D
  points**: twelve directions (four cardinals plus eight diagonals produced by
  interpolating between adjacent cardinals) × the two AABB corners, scaled by
  the hitbox radius.
- Those are **projected onto the plane perpendicular to the view direction**,
  mapped into a local 2-D basis, and reduced to a **convex hull** with a
  Graham scan (`misc/intersection.h`).
- From the hull it builds **three anchors** — the centre lerped toward the
  leftmost, toward the rightmost, and (head only) toward the topmost point,
  each by the scale factor `rs`.
- **Head special case** (`extra_head_calc`, `:577-613`): it walks six steps
  down the head capsule, tracing at each step, and stops at the first step
  that no longer hits the head hitgroup — then takes the **midpoint** between
  the top and that point. In other words, the head point is *slid down* until
  it is hittable. This is a concrete, portable trick.
- If `rs < 0.2` or the hitbox is the head, a single centre point is used;
  otherwise **every vertex of the hull** becomes a candidate point.

### 2.1 The radius scale `rs` — a complete, portable formula

`verified` — `features/hitscan.cpp:446-473`:

| Step | Value |
|---|---|
| base | **0.975** |
| legs | × **0.8** |
| not "no spread" | × `(1 − clamp(total_shots·0.2, 0, 0.8))` where `total_shots = shots_spread + max(0, shots − 2)` — **the multipoint radius shrinks as the target sprays at us** |
| the `pointscale` slider (0–100) | × `(0.5 + 0.5·clamp(ps,0,100)·0.01)` — so 0 ⇒ ×0.5, 100 ⇒ ×1.0 |
| lag compensation or prediction disabled | × **0.8** |
| live spread cone | `rs −= spread_radius·0.1` |
| the hitbox's multipoint toggle is off | `rs = 0` |
| final | clamp to `[0, 0.975]` |

`spread_radius` is the **linear radius of the weapon's minimum-spread cone at
the target's distance**: `spread_rad` is the weapon's lowest inaccuracy,
`corner_rad = 90° − spread_rad`, and `spread_radius = (dist / sin(corner_rad)) ·
sin(spread_rad)` (`:33-36`).

> This is directly usable in our solver: the multipoint radius is a function of
> (hitbox group, shot history, weapon spread, prediction state), and the
> **spread-cone term** is a *geometric* constraint we can apply per candidate
> point instead of sampling it.

### 2.2 "Safety size" — score by polygon area, not by a boolean

`verified` — `features/hitscan.cpp:643-647`: the safety metric is the **area of
the surviving intersection polygon**, scaled ×0.1 for the head and ×0.2 for the
upper chest so the number is comparable across hitboxes (bots get a flat 10).
When two points have equal shots-to-kill, the one with the **larger surviving
area** wins (`:293-294`).

> Directly applicable to our polygon-clipping solver: we already compute the
> visible region; turning its *area* into the tie-break metric is free.

---

## 3. Hit chance

### 3.1 Sampling (the live path)

`verified` — `aimbot_helpers::calc_penhc`, `features/aimbot_helpers.cpp:294-419`:

- **64 samples**, uniform over a disc, using a **pre-built seed table** of 255
  tuples `(r, sin θ, cos θ)` where `r` and `θ` come from the **engine's own**
  `RandomFloat` (`misc/globals.cpp:6-8` → `vstdlib.dll`), seeded per tuple
  (`build_seed_table`, `:193-208`). The sample set therefore *reproduces the
  game's spread distribution* rather than inventing one.
- `seed_inaccuracy = r · inaccuracy`; the spread vector is
  `(cos θ · inacc, sin θ · inacc)` added to the forward vector.
- Crucially the **view angles are rotated by the spread delta** (`:351-356`) and
  the endpoint is recomputed, so the sampled point is the *world-space point the
  bullet would actually reach*, not an angular approximation.
- Each sample is traced with the **forced hitgroup** of the chosen point
  (`:372`).
- The traces are spread over a thread pool.

**Hit counting** (`:402-416`), in order:

1. damage < 1 ⇒ not a hit;
2. unless the weapon is at its best possible accuracy (or a Zeus), the sample's
   **hitbox must equal** the chosen point's hitbox;
3. unless at *true* best accuracy (or Zeus), the sample's **hitgroup must
   equal** the chosen point's hitgroup;
4. otherwise it counts if the weapon is at true best accuracy **or** the damage
   ≥ the minimum-damage threshold **or** the damage ≥ the target's health.

**Result**: `min(hits · bullets_per_shot · 1.5625, 100)` — with 64 samples and
a one-bullet weapon each sample is worth 1.5625 %.

### 3.2 The early-out and the adaptive escalation

`verified`:

- **Max-hitchance pre-gate** (`:557-588`): compute the hitchance once at the
  weapon's *theoretical best* inaccuracy; if even that cannot reach
  `min(configured, 83)`, abort before the expensive computation.
- **Adaptive escalation** (`:531-555`): if the target has already missed more
  than 2 shots *due to spread*, the **required** hitchance is raised by
  `max(5, (spread_misses − 2) · (100 − hitchance) / 6)`, clamped to [0, 100].
- A "hit at the weapon's best accuracy" case short-circuits to **100 %**.
- A Scout (SSG08) jump shot with inaccuracy < 0.009 forces hitchance 100.

> The escalation loop is two lines and closes a loop our solver does not have:
> the *server's* spread-miss feedback raises the bar for the next spray.

### 3.3 A second, unused implementation

`calc_hc` (`:212-292`) traces 32 samples against a **precomputed capsule/OBB
intersection** instead of the engine trace, and reports hits and hits-under-roll
separately. It has **no call site** — a dead implementation of the same idea.
Worth knowing: the "roll" variant was an attempt to separate horizontal from
vertical spread outcomes.

---

## 4. Penetration and damage

### 4.1 The damage-reduction closed form

`verified` — `features/wall_penetration.cpp:313-326`:

- **thickness** = (exit − enter distance)², scaled by `1 / combined_penetration_modifier`, then **÷ 24**;
- **loss** = `max(0, current_damage · final_damage_modifier + max(0, 1/modifier) · 3.0 · max(0, (3.0 / penetration_power) · 1.25) + thickness)`;
- `final_damage_modifier` = **0.16** normally, **0.05** for glass/grate;
- if the loss exceeds the current damage the shot fails; otherwise the damage is
  reduced, and a damage below 1 fails the shot.

**Material → penetration modifier** (`:278-311`): glass or grate-texture → 3.0;
solid or light surface → 1.0; flesh → the `ff_damage_reduction_bullets` convar
(0 ⇒ the shot fails outright); default → the **average** of the entry and exit
surface modifiers; equal entry/exit material → cardboard/wood 3.0, plastic 2.0.

**Limits**: at most **4** walls (`:379`), the wall walk is capped at **90
units** in 4-unit steps (`:203`), and a weapon with penetration ≤ 0 cannot
penetrate at all.

**Distance falloff**: `damage ×= pow(range_modifier, distance / 500)` at each
500-unit step (`:421`, `:460`, `:481`) — and the same value is **pre-computed
during the scan** so hitboxes can be filtered before any trace (`:420-421`).

### 4.2 The damage model

`verified` — `penetration::scale_damage`, `:107-174`:

- **head** ×4 (×2 when the target has heavy armour);
- **stomach** ×1.25; **legs** ×0.75; everything else ×1;
- **armour**: non-heavy → ratio `armorratio·0.5`, bonus ratio 0.5; heavy → bonus
  ratio 0.33, ratio `armorratio·0.5·0.5`, and an extra ×0.85; if the armour
  *could* absorb the difference, the armour soaks it entirely
  (`damage = current − armour / bonus_ratio`);
- armour only applies to the body — for a headshot it needs a **helmet**.

### 4.3 The minimum-damage gate — a genuinely useful convention

`verified` — `misc/config.h:951-960`:

- `minimum_dmg = mindmg_override_enabled ? mindmg_override : mindmg`;
- **`mindmg > 100` means "target HP + N"** (the menu renders it as `HP + %.0f`);
- the effective threshold is `min(100, min(minimum_dmg, min(highest_damage_found + extra, target_health + extra)))` — the threshold is **capped by what the target's health allows** and by the best damage found for that target;
- the scan rejects a hitbox outright when `int(scaled_damage) + 1 < threshold`
  (`:429-430`) — **before generating any points or traces**.

> The "HP + N" convention and the "cap the threshold by the target's health"
> rule are directly portable, and the pre-filter turns an N-trace problem into
> a field lookup.

---

## 5. Shots-to-kill ranking, and the world-drift correction

`verified`:

- Points are ranked by **shots-to-kill**, not by raw damage:
  `ceil(health / min(floor(damage · doubletap_mult), health))`, with
  `doubletap_mult = 2` when double-tap is available and the weapon is not a
  single-shot (`:250-253`, `:265-274`).
- **"+5 HP per extra penetrated surface"**: when the traced shot crossed more
  than one surface, the target's effective health is raised by 5 (`:250`,
  `:265-266`) — a generic "the world moved between my trace and the server's
  resolution" correction. The same signal (the number of penetration points) is
  free from any penetration tracer.

---

## 6. Safe points

`verified` — the most interesting idea in the whole tree.

**Three tiers**: `safety_none = 0`, `safety_no_roll = 2`, `safety_full = 6`
(`features/wall_penetration.h:17-22`).

**Geometric tiering** (`hitscan.cpp:673-724`): the candidate polygons are
computed **per resolver hypothesis** (eight of them: networked, max, zero, min,
and four "extra" variants), and each tier is the **intersection** of the
hypothesis polygons:

- `safety_none` — the resolved hypothesis' polygon alone;
- `safety_no_roll` — `min ∩ max` (plus the extrapolated/extra-matrix variants);
- `safety_full` — additionally `∩ min_min ∩ max_max ∩ min_extra ∩ max_extra`.

**Trace-level tiering** (`wall_penetration.cpp:534-560`): the same ray is
**re-traced under the alternative hypothesis matrices**, and the point is
rejected unless it hits the forced hitgroup under *all* of them for the tier.

**Requirement selection** (`hitscan.cpp:353-373`): the tier required of a point
escalates with the target's state (airborne, moving, standing), with the shot
count, with the "anti-exploit" heuristic (an unusual pitch seen recently), and
with pitch jitter. If no `safety_full` point exists, the requirement is
**downgraded** to `safety_no_roll`. Bots are always treated as `safety_full`.

**Preference** (`overwrite_point`, `:241-328`): a safe point is only traded
away for an unsafe one when the fallback mode says so —
`fallback_mode == 2` ("Prefer safepoint") always, `fallback_mode == 1`
("Adaptive") only after the player has been shot. A lethal safe point is never
traded for an unsafe one. Equal shots-to-kill ⇒ the safer point wins ⇒ the
larger safety area wins ⇒ then hitbox **volume** (a capsule-volume estimate)
wins, then distance to centre.

**What makes a point unsafe**: the forced-hitgroup trace fails; it hits a
different hitgroup; it does not survive the hypothesis re-traces; its safety
area is below the limb threshold; or it was never generated (outside the hull).

> **This is the mechanism our solver lacks.** We compute "is this point
> visible"; they compute "is this point visible **under every hypothesis we
> might be wrong about**", and the answer is a *set* (an intersection) whose
> **area** is the safety metric. The hypothesis dimension is agnostic to what
> the hypotheses mean — in CS2 it can be timing, position, or side.

---

## 7. The resolver as a hypothesis set with server feedback

`verified` — `features/resolver.cpp`.

**Hypotheses** (`features/structs.h:90-102`): eight yaw hypotheses — networked,
max, zero, min, and the four "extra" variants (which add a ±50° roll).

**Cache**: per player, the current direction is cached together with a
**per-direction blacklist** (`structs.h:498-511`); the cache is *not* per tick —
it is updated when a shot is resolved or by the wall-detect probe.

**Server-feedback update** (`get_brute_angle`, `:884-1087`, run on
`FRAME_NET_UPDATE_END` for each confirmed shot):

- **On a miss**: re-evaluate every hypothesis against the shot line; every
  hypothesis that *would* have hit is **blacklisted** (`:941-956`).
- **On a hit that the server disagrees with**: pick the hypothesis whose head
  bone is **furthest** from the state we aimed at — the maximal plausible
  desync (`:958-984`).
- **On a hit with the correct hitgroup**: measure the distance from each
  hypothesis' predicted impact to the **server-reported impact** and keep the
  closest (`:1016-1068`).
- If the resolver state is unknown and the current direction differs from the
  shot's, the choice is mirrored into the other mode (`:1079-1086`).

**Miss classification** (`calc_missed_shots`, `:1089-1146`): a miss is attributed
to *spread*, *server yaw correction*, *extrapolation error* or *anti-exploit*,
and the counters feed the adaptive hitchance (§3.2) and the safety escalation
(§6).

**Wall-detect** (`:322-452`): once per command, for enemies that moved, it
probes **four rays** (left/right at 25 units from the local eye and from the
enemy's eye) with a synthetic AWP profile and damage 200; if exactly one side
is penetrable it concludes the target hides on that side, and then picks the
hypothesis whose head-bone position best matches that angle.

> **This is a discrete particle filter over a hypothesis set, updated from
> server feedback.** CS2 has no `aim_yaw_min/max`, but it has the same
> *shape*: a set of candidate target states, a per-candidate visibility
> computation, and a server-feedback rule that eliminates candidates. The rule
> is engine-agnostic; only the hypothesis generator changes.

---

## 8. Anti-aim (CS:GO-specific, and the list of what will not transfer)

`verified` — `features/antiaim.cpp`.

- **Yaw modes**: keep / reverse (+180) / zero / random; at-target (lowest-FOV
  enemy); spin (linear offset wrapping at the spin range, scaled by the choke
  count); jitter (±range/2, alternating or random); a constant add.
- **Pitch**: +89 / −89 / 0, with the aim-punch folded in under "no spread".
- **Desync**: a percentage `fake_amount`; the sign picks the min or max
  animation extreme; **body yaw = min_max · yaw_modifier · |delta|** where
  `yaw_modifier` comes from a movement-speed model
  (`animations.cpp:3-20`: a ground-fraction and ducking term, with
  `movement_speed = clamp(speed/(max_speed·0.520), 0, 1)`).
- **Delivery** (`run_on_send`, `:133-399`): it runs **only when the packet is
  sent**, and it rewrites the view angles of **already-created choked commands**
  in the input buffer, using the classic **"compensate angle"** — biasing the
  *view* yaw so that the server-derived *body* yaw lands exactly on the
  intended desync. It also applies an **intermediate body-yaw correction** to
  every command except the last (nudging until the foot-yaw delta is ≥ 2.5°),
  applies **roll/lean** on the final command (searching for the largest roll
  that does not slow the player), re-projects the movement onto the new view
  basis, and **patches the command CRC** afterwards.
- **Freestand is computed with the penetration model as an oracle** — probe
  four offset positions and pick the side that is *not* wall-penetrable.
- **Choking** is expressed purely as `*send_packet = chokedcommands >= to_choke`,
  with a "skip animation" heuristic comparing the current animation layers with
  the predicted next state.

**What does not transfer to CS2** (`verified` by the presence of the CS:GO-only
mechanisms, and by their absence in the CS2 tree we read separately):

1. the whole desync model (`aim_yaw_min/max`, the 0.520/0.340 constants, the
   yaw-modifier formula) — CS2 has no `CCSGOPlayerAnimState`;
2. the "compensate angle" trick — a CS:GO artefact of the server deriving body
   yaw from view yaw plus anim state;
3. roll/lean via `viewangles.z` — CS2 has no roll angle;
4. the delivery mechanism (rewriting `m_pCommands[i].viewangles` for created
   choked commands and patching the per-command CRC) — CS2's sub-tick commands
   each carry their own angles and time field;
5. `sendpacket` as a `bool&` out-param of `cl_create_move`, `chokedcommands`,
   `m_nChokedPackets`, `sv_maxusrcmdprocessticks`;
6. `TraceToStudioHitgroupsPriority` and the `mstudiobbox_t` capsule model;
7. the `weapon_accuracy_nospread` convar and the `RandomFloat` seed table —
   CS:GO's spread algorithm;
8. the `HITGROUP_*` semantics used by the armour/damage table;
9. the `m_pCommands` ring and its `time_to_ticks` accounting;
10. the `CCSWeaponData` profile fields (`flinaccuracy*`, `flrangemodifier`,
    `flpenetration`, `flarmorratio`).

---

## 9. The portable list — the ten techniques worth taking

Ranked for a CS2 payload that already has a polygon-clipping occlusion solver,
a damage-field aim solver, per-point spread sampling and a server-feedback
channel.

1. **Intersect the visibility polygon across all plausible target hypotheses**
   (§6). Keep N hypotheses and take the intersection of their occlusion
   polygons; the survivor is visible under every hypothesis by construction.
   This upgrades a binary occlusion solver into a hypothesis-robust one, and
   the mechanism is agnostic to *what* the hypotheses mean. `verified`
2. **Score points by the area of the surviving polygon**, not by a boolean
   (§2.2) — with per-hitbox scaling so the metric is comparable. Free for us:
   we already have the polygon. `verified`
3. **Server-feedback hypothesis elimination** (§7): on a miss, eliminate every
   hypothesis that would have hit; on a hit, keep the hypothesis whose
   predicted impact is closest to the server's. A discrete particle filter over
   a hypothesis set. `verified`
4. **Pin spread samples to the intended hitbox/hitgroup** (§3.1, rules 2–3).
   Counting "any sample that damages the enemy" biases the number downward and
   makes the percentage mean something different from what the user expects.
   Applies verbatim to our sampler. `verified`
5. **Adaptive hitchance escalation from spread-miss feedback** (§3.2): raise the
   required hitchance by `max(5, (spread_misses−2)·(100−hc)/6)` after the target
   sprays. Two lines, closes a loop we do not have. `verified`
6. **Pre-filter hitboxes by best-case damage before generating points**
   (§4.3) — with our damage field this is a lookup instead of N traces; adopt
   the "mindmg > 100 ⇒ target HP + N" convention and the "cap by the target's
   health" rule. `verified`
7. **Rank by shots-to-kill, not by raw damage** (§5). `verified`
8. **"+5 HP per extra penetrated surface"** world-drift correction (§5) — free
   from any penetration tracer. `verified`
9. **Reuse the penetration damage-reduction closed form** (§4.1): material class
   → penetration modifier → **squared** thickness → linear loss with a floor and
   a "damage < 1 ⇒ fail" cutoff. The CS2 constants differ; the *structure*
   transfers. `verified`
10. **Use the autowall solver as an oracle** for freestanding and "which side
    is the target on" (§7 wall-detect, §8 freestand): probe a handful of offset
    positions and pick the side that is *not* wall-penetrable. This yields a
    **binary side hypothesis** that can be fed straight into the hypothesis set
    as an extra dimension. `verified`

**Honourable mentions** — corner-point refinement (six-step lerp back toward the
hitbox centre, keeping any step that does not worsen shots-to-kill, then one
bisection); the max-hitchance pre-gate (cheap early-out at the weapon's best
possible accuracy); miss-cause classification (split a miss into spread /
server correction / extrapolation / anti-exploit and use it as a learning
signal); and a "no-penetration damage" pre-estimate so the selector can reason
about a wallbang without a second trace.

---

## 10. What we did not get from this tree

- It is **CS:GO**: the hitbox model, the spread model, the armour/damage table,
  the command delivery and the animation state are all CS:GO internals. The
  *shapes* transfer; the constants and the plumbing do not.
- The resolver is the part we would most want to port, and it is the part that
  depends on CS:GO's `aim_yaw_min/max` hypothesis generator. **The update rule
  is portable; the generator is not.** In CS2 the hypothesis set has to be built
  from something else — timing, position, or the side the target is on (§9.10).
- The tree is `fixed`/`source-fixed` — a community re-release, so it may carry
  local edits.

## 11. Cross-references

- [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) — the CS2 implementation of
  the same product family (256-sample hitchance, engine-priced penetration,
  lagcomp, command plumbing). Read this note and that one together: this one
  has the *algorithms*, that one has the *CS2 mechanics*.
- [`cs2-chams-t7.md`](cs2-chams-t7.md) — the CS2 material override.
- [`gamesense-csgo-re.md`](gamesense-csgo-re.md) — the other top-tier product's
  config vocabulary and Lua API (`client_damage`/`server_damage`,
  `safe_points`, `damage_override`).
- [`fatality-cs2-base.md`](fatality-cs2-base.md) — the CS2 sibling of this
  product (config schema only).
- [`cheat-dump-inventory.md`](cheat-dump-inventory.md) — where this artifact
  sits in the inventory.
