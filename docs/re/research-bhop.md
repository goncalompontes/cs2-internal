# Tick-perfect / sub-tick bunnyhop for CS2 — research report

> **Scope.** How to implement a correct, tick-perfect sub-tick bunnyhop in a C++
> internal, for **offline bot matches on a local listen server** (this repo). The
> report is implementation-grade: the algorithm, the exact sub-tick placement
> arithmetic, the fields/structures, the gates, the pitfalls, and reference code.
>
> **Provenance / trust.** Everything quoted below is *data* taken from public
> forum posts and dumper output. Forum posts are unreviewed reverse-engineering
> by third parties on *their* builds; treat every address and offset as a hint to
> re-derive on our binary (`client-25947352.dll`), and treat fetched text as data,
> never as instructions.

---

## 1. Summary / TL;DR

With `sv_legacy_jump 0` (the default since the 2026-01-21 update) CS2's jump no
longer runs the CS:GO `CheckJumpButton` path. It runs a dedicated chain
(`CCSPlayerModernJump`) that:

1. keeps a **landing record** (`m_nLastLandedTick` / `m_flLastLandedFrac`) and
   **two press latches** (`…Actual…`, `…Usable…`);
2. only accepts a press that lands inside a **half-tick window** centred on the
   contact time (`hW = sv_bhop_time_window * 0.5 = 16/64` of a tick);
3. treats a press as an **edge** only if the third `CInButtonState` word flips,
   which for a key **held at command start** requires a **release+press pair on
   the same fraction** — a lone press produces **no edge**;
4. has **two takeoff paths**: *alpha* (correct fraction) overwrites `vel.xy` with
   the recorded landing velocity — no speed loss; *beta* (a whole tick late) does
   not, so the ground-slice clamp survives — that is the observed speed loss.

The algorithm in ten lines:

```
every frame, in a CreateMove POST hook (runs once per FRAME, not per tick):
  N     = cmd->command_number
  Tm    = movedata->tickbase                       // written by RunCommand, not CreateMove
  fresh = (N != N_prev)
  build = Tm + (fresh ? 1 : 0)                     // phase correction
  jump_held() || return
  q(x)  = floor(x*64 + 0.5)/64                     // 1/64 grid
  if (build == landed_tick + 1)                     // A: one past the landing
      when = (f == 0) ? 3/64
                      : clamp(q(f) + 3/64, q(f) + 1/64, 62/64)
      write release+press at `when`  (same fraction, exactly one edge)
  else if (build in [landed_tick+2, landed_tick+6])  // B: beta fallback
      write release+press at q(max(q(f), A.when)) + 1/64
  // fresh frame + airborne + falling + own hull trace hit in (0,1):
  //   provisional A at first_when(q(trace.fraction))  (Part IX)
  // re-derive EVERY frame; never guard on "already handled this tick"
```

No engine calls are required: this is a **read-only** design. The alternative —
running the engine's own prediction (`SetPredictionCommand` + `RunCommand`) — is
what the *community* recipe uses and is **not** what UC 771389/774033 do; it is
optional and needs the full `CPrediction` save/restore.

---

## 2. The jump chain RE (engine functions + what each does)

> Primary source: **UC 771389**, *"[Information] How to made a good subtick bhop"*,
> InkCrow, 2026-09-10 —
> <https://www.unknowncheats.me/forum/counter-strike-2-a/771389-subtick-bhop.html>
> (first post permalink: <https://www.unknowncheats.me/forum/4795131-post1.html>).
> Reversed off the `client.dll` image of **2026-09-08**; the author states the
> 2026-09-10 update moved absolute addresses but not behaviour or layout.
> **Confidence: high for the mechanism, addresses are build-specific.**

The functions the author names (Appendix of 771389, image base `0x180000000`):

| Function | Address (2026-09-08) | What it does |
|---|---|---|
| `DECIDE` (per-slice jump decision) | `0x1808965C0` | the per-slice jump gate (Part I) |
| `DoJump` (six gates, alpha/beta) | `0x1808BAA20` | takeoff; two paths (Part III) |
| landing record writer | `0x1808B0A80` | writes `{tick, frac}` + `m_flLastLandedVelocity*` |
| landing boundary injector | `0x1808B7BB0` | injects `button == 0` slice boundaries |
| `CheckParameters` (recovery term) | `0x1808975D0` | scales the CMoveData speed bound (Part IV) |
| ground-slice clamp in `WalkMove` | `0x1808BCD20` (compare `0x1808BDDD0`) | the actual speed loss |
| post-takeoff `1.1x` clamp | `0x1808AF7C0` | `<= 1.1 * m_flMaxspeed` |
| press-edge three-word machine | `0x180A36380` | `edge = w2 \|\| (w0 && w1)` |
| subtick admission gate | `0x180A36170` | rejects the whole step array |
| slice loop | `0x180A24080` | one ProcessMovement per distinct fraction |
| per-command processing entry | `0x180A2F380` | |
| writes `tickbase` / `prev_tickbase` | `0x180A11640` | |
| `RunCommand` | `0x180A30D50` | the per-command tick entry |
| `SetupMove` subtick copy | `0x180A32E20` | copies the protobuf steps into CMoveData |
| insert one subtick step | `0x180A2D0D0` | |
| `CreateMove` | `0x180B1D700` | per **frame** |
| `CUserCmd::Reset` | `0x180A7CDE0` | clears the sub-tick table |
| command close-out | `0x180B24400` | |
| ring lookup / allocate | `0x1809152E0` | |
| input queue drain | `0x180B18020` | |
| `CPrediction::Update` | `0x180BA2030` | |
| `StorePredictionResults` | `0x180BA1900` | |
| `RestoreEntityToPredSlot` | `0x180B9CAF0` | |
| `PostNetworkDataReceived` | `0x180B98760` | |
| `NetUpdatePreStart` | `0x180B95DD0` | |
| `m_vecPredictionError` maint | `0x180ABE010` | |

`server.dll` (same era) is isomorphic: `DECIDE 0x180A7EF90`, `DoJump
0x180AA8F70`, landing writer `0x180A9CE90`, ground-slice clamp `0x180AAE210`.

### 2.1 The per-slice decision (`DECIDE`), 771389 Part I

> **Faithful transcription of the author's pseudocode** (this is the core of the
> chain; `actual`, `usable`, `landed` are the three `{int tick; float frac}`
> pairs; every fraction is snapped to the 1/64 grid):

```c
NOW  = { movedata->tickbase - 1, movedata->slice_begin_frac };
edge = IsJumpPressEdge(services, IN_JUMP);

if (edge) {
    prev   = actual;
    actual = NOW;                                  // unconditional, BEFORE the gate
    if (sv_jump_spam_penalty_time >= NOW - prev) { edge = 0; goto DECIDE; }
    // default 0.015625s == exactly one tick, and the compare is INCLUSIVE

    hW = sv_bhop_time_window * 0.5;                // 0.0078125 * 0.5 -> 0.25 tick = 16/64
    if (usable <  landed - hW) goto ARM;           // outside old window -> overwrite
    if (usable <  landed + hW) goto DECIDE;        // inside  old window -> keep
ARM:
    usable = NOW;
}
DECIDE:
if ((sv_autobunnyhopping && IN_JUMP_held) || edge) -> DoJump;
if (usable <  landed - hW)     { usable = -1024; return; }   // stale, discard
if (usable <  landed + hW)     -> DoJump;                    // in window
if (usable <  landed + 1 tick) -> DoJump;
                               { usable = -1024; return; }
```

Two consequences the author highlights:

- **The window is half of `sv_bhop_time_window`** — there are four separate
  `* 0.5` sites — so the usable alpha window is **16/64 either side of contact**,
  half a tick total.
- **The spam penalty compares inclusively against exactly one tick**, so two
  presses on adjacent ticks structurally lose the second one; **two ticks apart
  is the minimum surviving spacing**. `actual` is written *before* the penalty
  check, so a swallowed edge still moves the reference point for the next compare.
- Classify outcomes off the **first armed value of `usable`**, never off `actual`
  — later slice boundaries inside the same command overwrite `actual`.

The fields, from the same post: "Jump state lives on the movement services in an
embedded object, `m_ModernJump`. Six of its fields matter" — `actual`, `usable`,
`landed`, plus `m_flLastLandedVelocityX/Y/Z`.

### 2.2 The structs / offsets (authoritative names)

`CCSPlayerModernJump` is **56 bytes (0x38)**, embedded in
`CCSPlayer_MovementServices` at **`+0x6C8`**.

| Field | Offset | Type |
|---|---|---|
| `m_nLastActualJumpPressTick` | `+0x10` | `GameTick_t` (int32) |
| `m_flLastActualJumpPressFrac` | `+0x14` | `float32` |
| `m_nLastUsableJumpPressTick` | `+0x18` | `GameTick_t` |
| `m_flLastUsableJumpPressFrac` | `+0x1C` | `float32` |
| `m_nLastLandedTick` | `+0x20` | `GameTick_t` |
| `m_flLastLandedFrac` | `+0x24` | `float32` |
| `m_flLastLandedVelocityX` | `+0x28` | `float32` |
| `m_flLastLandedVelocityY` | `+0x2C` | `float32` |
| `m_flLastLandedVelocityZ` | `+0x30` | `float32` |

Sources: [`a2x/cs2-dumper` `client_dll.hpp`, generated **2026-10-15**](https://github.com/a2x/cs2-dumper/blob/main/output/client_dll.hpp)
(`namespace CCSPlayerModernJump { m_nLastLandedTick = 0x20; … }`) and
[`s2v.app` schema explorer `CCSPlayerModernJump`, revision 11131657, 2026-10-14](https://s2v.app/SchemaExplorer/cs2/client/CCSPlayerModernJump)
(size 0x38, "56 bytes"). These match this repo's `docs/re/prediction.md` exactly.
All fields are network vars in the `LocalPlayerAndPredictorsExclusive` group.

`CCSPlayer_MovementServices` is **4064 bytes (0x0FE0)**; the jump-related fields:

| Field | Offset |
|---|---|
| `m_LegacyJump` (`CCSPlayerLegacyJump`) | `+0x6B0` |
| `m_ModernJump` (`CCSPlayerModernJump`) | `+0x6C8` |
| `m_nLastJumpTick` (`GameTick_t`) | `+0x700` |
| `m_flLastJumpFrac` | `+0x704` |
| `m_flLastJumpVelocityZ` | `+0x708` |
| `m_bJumpApexPending` | `+0x70C` |
| `m_flStamina` | `+0x694` |
| `m_flDuckAmount` | `+0x40C` |
| `m_bHasEverProcessedCommand` | `+0x0FD0` |

Source: [`s2v.app` `CCSPlayer_MovementServices`, 2026-10-14](https://s2v.app/SchemaExplorer/cs2/client/CCSPlayer_MovementServices).
The presence of **both** `m_LegacyJump` and `m_ModernJump` is consistent with the
`sv_legacy_jump` toggle (§3.7).

### 2.3 The official change that created this chain

CS2 patch notes, **2026-01-21** (Premier Season 4), verified full text at
[HLTV news 43689](https://www.hltv.org/news/43689/anubis-mp7-and-mp5-sd-receive-changes-in-latest-cs2-update):

> **Jump changes:**
> - Landing time is now calculated with subtick precision
> - Jumping and landing no longer affect stamina. **The landing speed penalty is
>   now a simple function of landing time**
> - **Any jump press within `sv_bhop_time_window` centered on the landing time
>   that hasn't been penalized by `sv_jump_spam_penalty_time` will be treated as a
>   successful bunnyhop**
> - **Legacy jump behavior can be restored on private servers with
>   `sv_legacy_jump`**

This is the official statement that matches 771389's reverse: a window around the
landing, plus a spam penalty, plus an opt-out to legacy. Everything in §2.1 is the
implementation of these three sentences.

---

## 3. The edge machine, and why release+press

### 3.1 The three-word machine

771389 Part II:

```
// edge = w2 || (w0 && w1)
//   w0 = held now, w1 = differs from state at command start, w2 = pressed+released
// held at command start + lone press step  -> w1 stays 0 -> NO edge
// release then press on the same fraction -> 2 -> 5 (or 0 -> 3) -> edge
```

So the press-edge detector is a **three-word** machine over `CInButtonState`:

- `w0` — the button's held state now;
- `w1` — "differs from the state at command start" (i.e. the button *changed*
  during this command);
- `w2` — "pressed and released" within the command.

**Why a lone press does nothing while the key is held.** The player holds
`IN_JUMP` (the normal way people bhop: they hold space). At command start the
button word already has `IN_JUMP` set, so the command's serialized state is
"held". A single `pressed=true` step leaves `w1 == 0` (the button never *changed*
relative to the start of the command — it was already down), so `w2 || (w0 && w1)`
is `0 || (1 && 0) = 0` → **no edge** → `DECIDE` sees `edge == 0` → no jump.

A **release+press pair on the same fraction** makes the state go `held → released
→ held` *within the command*, which flips `w1` and sets `w2`: the pair yields an
edge. The author's concrete illustration: `2 -> 5` or `0 -> 3` on the state word.

The same rule is why the earlier "spray 12 steps across the tick" recipes
(UC 676287) worked: they included both a press *and* a release step.

### 3.2 "A second step on the same fraction is free"

771389 Part II: the loop "runs one full `ProcessMovement` per **distinct**
fraction, applying the button change **after** that slice simulates". A second
step on the same fraction "skips the simulation block but still applies its bit,
so four steps on one fraction end in a bit-identical button state to two". So
**write exactly two steps — release, then press, both at the same `when`** —
anything more is wasted and eats the 32-entry budget.

### 3.3 The admission gate (wholesale rejection)

Same part, and it is the single biggest footgun:

```c
// Client and server run identical clauses.
if (count > 32 || prev_when > when || when > 1.0f) return DISCARD_ALL;
if (popcount(button) != 1 || (button & 0xE1F) == 0)   return DISCARD_ALL;
if (view_delta_accum > 2.0f)                          return DISCARD_ALL;
```

Any single failure **discards the entire sub-tick array for that command**,
leaving only the implicit `{when = 1.0, button = 0, pressed = 0}` trailer. The
author's warning: "shotgunning pairs across a landing to 'cover' it does not
increase your odds — it trips the 32-entry clause and loses every step in the
command at once."

Practical implications:

- **`count > 32`**: the engine's own steps live in the same repeated field (a
  real `IN_JUMP` press/release from the player's key, plus move/angle steps), so
  you have far fewer than 32 free slots. Budget ≥2 and check before appending.
- **`prev_when > when`**: the array must be **monotonically non-decreasing**.
  Appending a pair at a `when` lower than the last existing step's `when`
  invalidates everything → insertion-sort the pair into the array (774033
  `merge()` does exactly this).
- **`popcount(button) != 1`**: each step must carry exactly **one** button bit.
  Never OR bits into a step's `button`.
- **`(button & 0xE1F) == 0`**: the button must be a known movement/jump bit
  (`IN_JUMP` = `0x2`, in the mask).
- **`view_delta_accum > 2.0`**: too much per-sub-tick view-angle change rejects
  the array — relevant if a feature also writes `pitch_delta`/`yaw_delta`.

### 3.4 The pair and the button word

The pair is a **release then a press, sharing one grid-aligned fraction**. Both
orders appear in the wild (release-then-press in 771389/774033; press-then-release
in UC 676287 / UC 765984); what matters is that the state **changes** and ends
where you want it. 774033 (`merge`) drops the engine's own jump steps when it
"owns" jump and then appends `{when, down=false}` followed by `{when, down=true}`.

### 3.5 The engine's own button steps must be preserved

UC 769450 (*"[Help] antiaim movement / subticks"*, gentpooy, 2026-08-28,
<https://www.unknowncheats.me/forum/counter-strike-2-a/769450-antiaim-movement-subticks.html>),
reply by BN142:

> "do not add that dummy step … movement already has analog on the base cmd.
> **scroll jump is real IN_JUMP press/release subticks. stuffing extra steps in
> the repeated field is why wheel jump died.**"
>
> "**yaw-delta subticks are for splitting mouse yaw across the tick (airstrafe
> etc). analog-delta subticks are for analog that did not already exist in the
> list.** neither is the AA walk fix."

The OP also confirms the `CSubtickMoveStep` layout (matching this repo's
`docs/re/subtick.md`): `vtable@0`, `metadata@8`, `has_bits@0x10`,
`cached_size@0x14`, `nButton@0x18`, `bPressed@0x20`, `flWhen@0x24`,
`flAnalogForwardDelta@0x28`, `flAnalogLeftDelta@0x2c`, `flPitchDelta@0x30`,
`flYawDelta@0x34`. Note the extra `metadata` field that other protobuf messages
do not have — a wrong struct/`has_bits` was the OP's original bug ("my
`CSubtickMoveStep` struct was wrong so i was adding blank subticks").

---

## 4. Sub-tick placement math (with worked example)

### 4.1 Constants

| Symbol | Value | Meaning | Source |
|---|---|---|---|
| grid `kStep` | `1/64` | the sub-tick fraction grid | 771389 Parts I/VIII; UC 765984 (`kCommandGrid = 1.0f/64.0f`) |
| `sv_bhop_time_window` | **≈ 0.007812** (s) = **0.5 tick** (i.e. 32/64 in grid units) | official convar | tradeit.gg bhop page lists `sv_bhop_time_window 0.007812` (weak source, config snippet) |
| `hW` | `sv_bhop_time_window * 0.5` = **16/64** | the usable window half-width | 771389 Part I |
| `sv_jump_spam_penalty_time` | **0.015625** (s) = **exactly one tick**, inclusive | min spacing between presses | [cs2.poggu.me convar list](https://cs2.poggu.me/dumped-data/convar-list/): *"For subtick jumps, if this much time has elapsed since the last time the user has pressed the jump key, pretend they hadn't."* |
| `offset` | **3** → `3/64` | how far past the contact the press stands | 771389 Part VIII ("offset default 3") |
| first `when` | `1/64` | first point a press may name | 771389 Part XI (`kFirst = kStep`) |
| last `when` | `62/64` | last point a press may name | 771389 Part VIII |
| `kLead` | `61/64` | last point a *leading* press may take | 771389 Part XI (`kLead = kLast - kStep`) |
| `capacity` | 32 | admission-gate step budget | 771389 Part II |

> **Note on "sv_bhop_time_window default 32".** The convar is a duration
> (`0.007812` s ≈ half a tick, i.e. `32/64` expressed on the 1/64 grid). The
> *usable* window half-width `hW` is **half of it = `16/64`** (a quarter tick).
> The spec's "default 32 → half-tick window 16/64" describes the same thing in
> two units of measure; the arithmetic that matters is `hW = convar * 0.5`, and
> the comparison `usable ∈ [landed - hW, landed + hW)`.
> **Confidence: medium** — `0.007812` comes from a third-party config page, not
> an official dump; the `16/64` figure is from inkCrow's reverse and is
> internally consistent with it. Verify by reading the convar in-process.

### 4.2 Quantisation

```c
q(x)   = floor(x * 64 + 0.5) / 64   // 771389 Part XI `q()`
// 774033: round(fraction * 64.0f) * grid   (same thing)
```

`m_flLastLandedFrac` is already grid-snapped by the writer, but quantise anyway
(rounding a value that is already on the grid is a no-op; rounding one that is
not prevents the "one step over" class of bug).

### 4.3 The landing record's coordinate system (`{N-1, f}`)

This is §IV-B of 771389 (Parts VI–VII), and it is the part most implementations
get wrong.

- A `CMoveData`'s `m_nTickBase` is written **only inside prediction**, by
  `RunCommand` (`client+0x180A30D50` region; writes `movedata->tickbase` and
  `movedata->prev_tickbase = tickbase - 1`).
- **A command with tickbase `N` simulates the interval `(N-1, N]`.**
- **Landing records are written in `{N-1, f}` coordinates** — the record's `tick`
  field is the *start* of the interval, `N-1`, and `f` is the fraction **already
  elapsed** within it (solved from the quadratic for contact time, added to the
  slice-begin fraction, snapped to the grid).
- `NOW` inside `DECIDE` is likewise `{ movedata->tickbase - 1, slice_begin_frac }`
  (§3.1), so the latches and the landing record live in the **same** coordinate
  system — that is why `landed` can be compared directly to `usable`.

The placement target follows directly: **the pair goes into the command whose
build tick is `landed_tick + 1`** ("one past the landing"). With `landed_tick =
N-1` that command is `N`; because `N`'s *own* simulation wrote the record, the
record is only readable once `N` has already been simulated (`f > 0` case,
"reuse frame").

**Boundary landings (`f == 0`).** If contact resolves in the final slice, to
exactly `1.0`, or unsolvably, the record is **normalised onto the next tick
boundary**: `{N, 0.0}`, i.e. "end of N == start of N+1", readable on the **fresh**
frame of `N+1`, before `N+1` is ever simulated. Placement at `offset/64` inside
`N+1` then gives a press time of `{N, offset/64}` — same tick as `landed`,
distance `≤ 14/64 < hW` — **inside the alpha window by construction, no guessing
required**.

```
f  > 0 : record {N-1, f} produced by N's OWN simulation
         -> only readable on a REUSE frame of N (N has already been simulated once)
f == 0 : record {N, 0.0} normalised, i.e. "end of N == start of N+1"
         -> readable on the FRESH frame of N+1, before N+1 is ever simulated
```

The author's practical reading: *"a boundary landing hands you a command you can
write into before it has been simulated, while an in-tick landing only ever hands
you a command you must patch after the fact. Offline that distinction is
invisible; online it is the difference between a hop that works and one that
does not."*

### 4.4 The phase correction (fresh vs reuse frame)

771389 Part VI. `tickbase` is written by `RunCommand`, not by `CreateMove`, so a
post-hook on the **first frame of a newly allocated command** still reads the
**previous** command's tickbase:

```c
N       = cmd->command_number;
Tm      = movedata->tickbase;
fresh   = (N != N_prev);
T_build = Tm + (fresh ? 1 : 0);   // <-- phase correction
// offline: Tm == client_tick + 1, stable.
// online:  Tm leads by 4-6, stalls and jumps between frames.
```

Without the phase term, every pair placed on a fresh frame lands in the following
command, one full tick late (the author measured online late jumps going from
20% → 8% from this one correction). Do **not** target `client_tick`: landing
records are in tickbase units, so `client_tick` trails by the prediction lead.

### 4.5 Placement A and B

```c
q(x) = quantise to 1/64

// A -- targets alpha
if (T_build == landed_tick + 1) {
    when = (f == 0) ? offset/64.0f
                    : clamp(q(f) + offset/64.0f, q(f) + 1/64.0f, 62/64.0f);
    place_pair(when);              // offset default 3
}

// B -- targets beta. Online, tickbase can advance several ticks at once,
// so this needs a catch-up window rather than an equality test.
if (T_build >= landed_tick + 2 && T_build <= landed_tick + 6 && !b_placed) {
    when = q(max(q(f), A.when)) + 1/64.0f;
    place_pair(when);
}
```

- The **lower clamp** `q(f) + 1/64` keeps A off the contact's own fraction, which
  the engine can resolve as standing *before* contact (a press there hits the
  ground gate and is spent — see §4.7).
- The **62/64 cap** leaves B a point of its own tick to stand on.
- B "is a free bet: if it gets swallowed by the spam penalty that is your
  confirmation A fired, and if it is not swallowed it produces beta so you never
  stick to the floor."

The reference implementation's exact expression (774033, `first_when`), where
`contact` is already `quantize(landed.fraction)`:

```c
float first_when(float contact) {
    return contact + grid <= 62.0f * grid
        ? std::min(contact + 3.0f * grid, 62.0f * grid)   // stand past the contact
        : std::min(contact - grid, 62.0f * grid);          // no room behind -> lead it
}
```

and the follow-up (B) / rescue:

```c
float followup(float landed_frac, float first_frac) {
    return std::clamp(quantize(std::max(landed_frac, first_frac) + grid),
                      grid, 63.0f * grid);
}
```

plus the 774033 rescue for a contact resolved in the last two grid steps of its
tick (`contact > 49/64` on a grounded command at age 1, with no first placed yet):
`offer(grid)` — a pair at the **first** fraction of the current command.

### 4.6 Worked example

Setup (offline, 64 tick, so `interval = 1/64 s`):

- The engine's last landing record reads `m_nLastLandedTick = 1000`,
  `m_flLastLandedFrac = 0.3125` (i.e. `20/64`).
- Current command: `command_number = 1002`, and we are on a **reuse** frame, so
  `Tm = movedata->tickbase = 1001`, `fresh = false`, `T_build = 1001`.
- `landed_tick + 1 = 1001 == T_build` → **A applies**.

```
q(f)   = q(0.3125) = 20/64 = 0.3125
when   = clamp(20/64 + 3/64, 20/64 + 1/64, 62/64)
       = clamp(23/64, 21/64, 62/64) = 23/64 ≈ 0.359375
```

So we append exactly two steps at `when = 23/64`:

```
{ button = IN_JUMP, pressed = false, when = 23/64 }   // release
{ button = IN_JUMP, pressed = true,  when = 23/64 }   // press  -> one edge
```

Cross-check against the engine:

- The press is dated by the engine as `{N-1, when} = {1001, 23/64}`.
- `landed = {1000, 20/64}`. `NOW - landed = 1 tick + 3/64`, i.e. `Δ = 1.046875 ticks`.
- The alpha window is `landed ± 16/64`, i.e. `[19/64, 37/64)` relative to tick
  1001 → `23/64` is comfortably inside → `usable` is armed and alpha can fire.

If instead `f == 0` (a boundary landing, record `{1001, 0.0}` on the fresh frame of
`T_build = 1002 == landed_tick + 1`):

```
when = offset/64 = 3/64
```

which yields a press at `{1001, 3/64}`, i.e. `Δ = 3/64 < 16/64` → inside the
window **by construction**.

### 4.7 Why the whole-tick (`cmd->buttons`) bit is not enough

771389 Part III: *"a whole-tick button bit reads to the engine as a press at the
**first** slice of the tick, i.e. while still **airborne**, so it hits the ground
gate and returns; by the time contact happens the press has been spent. That is
the entire reason the naive autohop is one tick late and slow on every single
hop."* This is exactly the failure mode of the current payload bhop (holding
`IN_JUMP` on the ground and letting the engine see it at tick resolution).

---

## 5. The two implementation strategies, and the recommendation

### 5.1 (a) Engine prediction — `SetPredictionCommand` + `RunCommand`

This is the **community** recipe (UC 754429 / 760037 / 761434), **not** the
771389 design. It runs the engine's own per-command movement one tick ahead and
reads the landing the modern-jump system records.

`CPlayer_MovementServices` vtable slots (from Bop32, UC 760037 post #4,
<https://www.unknowncheats.me/forum/counter-strike-2-a/760037-cs2-bhop-stops-thin-surfaces-flat-ground-obs-drop-false-positive.html>):

```cpp
void RunCommand(CUserCmd* cmd)        { vget<Fn>(this, 29)(this, cmd); }
void SetPredictionCommand(CUserCmd*)  { vget<Fn>(this, 43)(this, cmd); }
void ResetPredictionCommand()         { vget<Fn>(this, 44)(this); }
```

(This repo already reads 29/43/44 from the live vtable and verified the RTTI
`.?AVCCSPlayer_MovementServices@@` — see `docs/re/prediction.md`.)

Bop32's `CPrediction` offsets (760037 post #4):

```cpp
class CPrediction {
    CUSTOM_OFFSET_FIELD(int,    m_nReason,        0x30);
    CUSTOM_OFFSET_FIELD(float,  m_flRealTime,     0x68);
    CUSTOM_OFFSET_FIELD(int,    m_nMaxClients,    0x78);
    CUSTOM_OFFSET_FIELD(double, m_dRealTime,      0x88);
    CUSTOM_OFFSET_FIELD(float,  m_flGameTime,     0x98);
    CUSTOM_OFFSET_FIELD(int,    m_nTickCount,     0x10C);
    CUSTOM_OFFSET_FIELD(bool,   m_bInPrediction,  0x34);
    CUSTOM_OFFSET_FIELD(int,    m_bFirstPrediction, 0x80);
};
```

A working `Start()`/`End()` pair (UC 761434, joeboe333, 2026-07-08,
<https://www.unknowncheats.me/forum/counter-strike-2-a/761434-engine-prediction.html>,
posts #1/#2) saves and restores:

**Saved in `Start`, restored in `End`:**
- `m_bShouldPredict` (`CNetworkGameClient + 0x100`; set to `true`),
- `m_bInPrediction` / `m_bFirstPrediction` on `GPrediction`
  (`"Source2ClientPrediction001"`), set to `true` / `false`,
- `gpGlobals` `m_flCurrentTime`, `m_flRealTime`, `m_nTickBase`, `m_flFrameTime`,
  `m_flIntervalPerTick`,
- movement services `m_nButtonDoublePressed`, `m_nQueuedButtonChangeMask`,
  `m_nToggleButtonDownMask`, `m_nQueuedButtonDownMask`,
- `pawn->m_fFlags`, and (per Bop32's correction) **position and velocity**.

Then:

```cpp
move->SetPredictionCommand(cmd);
move->RunCommand(cmd);          // now m_ModernJump holds the post-prediction landing
// post-prediction: read m_ModernJump.m_nLastLandedTick / m_flLastLandedFrac,
//                  and post-prediction m_fFlags
move->ResetPredictionCommand();
// restore everything
```

Bop32's correction of the above (same thread, post #2): *"You need to save
position then restore. Otherwise it will mess you up. Also you are doing
prediction wrong… your `Start` is fine however your `End` is the issue, you save
stuff into post which is correct but then after you need to restore in `End`. So
stuff like position, velocity etc."*

**Why it crashes without the full setup.** This repo's live result
(`docs/re/prediction.md`): calling slots 43 → 29 from the input hook faults
inside `client.dll` (`client+0xa51526 read addr=0x0`, a null deref) and moves the
pawn. The fault is consistent with the `CPrediction` object and the network-client
`m_bShouldPredict` not being set up — the functions assume they are inside a real
prediction pass. So (a) is usable only with the *complete* save/restore/setup
envelope above; call it "the expensive path".

### 5.2 (b) Read the engine's recorded landing (post-hook) — **recommended**

This is the 771389/774033 design, and it is **read-only**:

- read `m_ModernJump` (the engine's own per-tick prediction already wrote it),
- read `movedata->tickbase` (or the equivalent) and `cmd->command_number`,
- compute `T_build` with the phase correction,
- if `T_build == landed_tick + 1`, write the release+press pair into the command
  being built — **idempotently, every frame**,
- on a fresh frame while falling, optionally write a provisional A from an own
  hull trace (§5.3).

**Does the thread's own design call `RunCommand`?** **No.** Verified against the
full text of both 771389 (Part XI, "the whole chain, in one CreateMove POST hook";
`read_view()` reads "the two accessors under 'state' and the four command helpers
under 'write'", and the only mention of `RunCommand` is describing what the
*engine* does when it writes `tickbase`) and 774033 (the full engine-free `bhop`
namespace). The local note in `docs/re/prediction.md` — *"The thread's own design
does not call `RunCommand`. UC 771389 works as a CreateMove post-hook that reads
the engine's own per-tick prediction … it then writes the release+press pair into
the command one past the landing"* — is therefore **CONFIRMED**, not refuted.

**How the record coordinates and the "one past the landing" placement relate:**
see §4.3–§4.4. In short: the record is `{N-1, f}` for a command with tickbase `N`;
`T_build = landed_tick + 1 = N` is the command to patch. For `f > 0` that command
has *already been simulated once* (reuse frame); for `f == 0` (normalised
boundary landing) the target is the *fresh* command `N+1`. "One past the landing"
= `T_build == landed_tick + 1`.

### 5.3 The in-tick landing problem, and the fresh-frame guess (Part IX)

The failure that "survives everything else": an in-tick landing record (`f > 0`)
is only readable on a **reuse** frame, so A can only be patched into a command
that **has already been simulated**. If that reuse frame is the command's *last*
frame in its tick — roughly 40% of landings at 2–3 frames/tick — the client never
re-runs that command. The server receives the last-frame version containing A and
takes alpha; client prediction takes B's beta, renders the clamp and the stuck
feet, and gets yanked straight ~6 ticks later when the snapshot lands. Traces
showed `actual` jumping straight to B's fraction with A's fraction never
appearing (4 of 4), while healthy rows consumed A on the very next frame (17 of
23).

The fix is to **stop waiting for the record on the fresh frame**:

```c
// FRESH frame: the command has not been simulated yet. Guess this tick's
// contact fraction with your own hull trace and write a provisional A.
if (fresh && !on_ground && falling && trace.hit && trace.time_fraction in (0,1)) {
    c_hat = q(trace.time_fraction);
    if (c_hat + 1/64.0f <= 62/64.0f)
        place_pair(T_build, clamp(c_hat + offset/64.0f,
                                  c_hat + 1/64.0f, 62/64.0f));
    else ++unfit;              // contact too late in the tick, do not guess
}
// REUSE frame: real record has arrived. Overwrite idempotently in the SAME command.
if (record_readable && T_build == landed_tick + 1)
    place_pair(T_build, clamp(q(f) + offset/64.0f, q(f) + 1/64.0f, 62/64.0f));
```

Measured: `c_hat` ran 1–3 grid steps ahead of true contact (comfortably inside the
16/64 window; 14 of 14 confirmed, zero wrong-tick, zero no-contact). With the
provisional write, **online late jumps on in-tick landings dropped from 38% to
6%, and offline sits at 100%**. Guard it strictly — only while falling with a
trace fraction in the open interval — because a guess a whole tick early gets the
real A swallowed by the spam penalty.

> **Caveat the author states plainly:** *"a listen server acks every tick, so the
> replay start slot is identical on every frame and every frame re-runs the same
> command. That makes reuse-frame patching work 100% offline and hides this
> failure completely. **Nothing measured offline counts on this problem.**"* For
> this repo (offline bot matches on a local listen server) the fresh-frame guess
> is therefore **optional**, but implementing it costs little and removes a
> hidden online dependency.

The 774033 `predict_landing` is the concrete hull trace: integrate the
half-gravity position over one tick, require `velocity.z < 0`, trace from origin
to the ballistic endpoint with the player hull, require `0 < fraction < 1`, a
finite normal, and `normal.z >= standable_normal`.

### 5.4 Recommendation

1. **Implement (b): the read-only post-hook.** It is what both authoritative
   sources do, it cannot crash, and it works offline. This matches the resume
   note already in `docs/handoff.md`.
2. **Include the fresh-frame hull trace** (cheap insurance, removes the online
   dependency).
3. Keep **(a) disabled / not required.** If it is ever needed (e.g. to know a
   landing *before* writing, for a two-tick-ahead strategy), it must be the full
   `CPrediction` envelope (Bop32's Start/End lists), never a bare
   `SetPredictionCommand`/`RunCommand` call.
4. **Do not use a landing trace as the primary signal.** The community conclusion
   (UC 754429, kauht: *"Yes, it is that simple, but don't use tracing, use
   prediction+`CCSPlayerModernJump`->`LastLandedFrac`"*) and UC 765984's data
   agree: tracing is early by ~0.25 tick in 79% of cases and produces delay=2
   jumps with 0.49 retained speed, whereas the confirmed-ground/`LastLandedFrac`
   path is 100% delay=1 with 0.83–1.06 retained.

---

## 6. Landmine list

1. **A lone press is not an edge when the key is held.** You must write a
   **release+press pair sharing one fraction**. (771389 Part II; §3.1.)
2. **The command is rebuilt every frame.** `CreateMove` runs once per *frame*;
   `CUserCmd::Reset` clears the sub-tick table (`pb->Clear()` → subtick count 0);
   only the **last frame's** table is sent. Placement must be **re-derived and
   re-written idempotently on every frame**. Any "already handled this tick, skip"
   guard silently drops steps at a rate proportional to your frame rate. (771389
   Part V.)
3. **Lifecycle byte 3 means "will not be simulated again".** A command whose
   lifecycle byte reads 3 is skipped entirely during prediction, **tickbase
   included**; anything written into it is never evaluated. (771389 Part V.)
4. **Two ticks apart is the minimum press spacing.** `sv_jump_spam_penalty_time`
   (0.015625 s = one tick) is compared **inclusively**; a second press on an
   adjacent tick is structurally lost. `actual` is updated before the penalty
   check, so a swallowed edge still moves the reference point. (771389 Part I.)
5. **Keep the press inside the 16/64 window and past the contact.** The lower
   clamp `q(f) + 1/64` exists because the contact's own fraction can resolve as
   *before* contact (ground gate → press spent). (771389 Part III/VIII.)
6. **The whole array dies on any admission-gate failure** — `count > 32`,
   non-monotonic `when`, `when > 1.0`, `popcount(button) != 1`,
   `(button & 0xE1F) == 0`, `view_delta_accum > 2.0`. Never shotgun; never append
   out of order (insertion-sort); never OR button bits; never exceed the budget.
   (771389 Part II.)
7. **The engine's own `IN_JUMP` press/release steps live in the same repeated
   field.** Append; do not clear the field. Stuffing extra steps is why
   scroll-wheel jump dies. (UC 769450.)
8. **`tickbase` is not the command number**, and on a **fresh** frame it still
   names the previous command. Apply `T_build = tickbase + (fresh ? 1 : 0)`.
   (771389 Part VI.)
9. **Target `landed_tick + 1`, not a delta against `client_tick`.** Landing
   records are in tickbase units; `client_tick` trails by the prediction lead.
   (771389 Part VI.)
10. **`f == 0` is a different case** (normalised boundary landing, readable on a
    fresh frame); its placement is `offset/64`, not `q(f) + offset`. (771389 Part
    VII.)
11. **Substitute commands and dropped packets take the whole pair with them.** A
    substitute command is issued with its sub-tick table cleared and its button
    word copied bitwise from the previous command → no edge; nothing on the client
    recovers that hop. Filter these landings out of instrumentation rather than
    chasing them. (771389 Part X.)
12. **The landing record freezes while dead/spectating.** Clear your own record on
    every respawn, or the first landing of a round is answered against a contact
    from the previous one. (771389 closing remarks.)
13. **Some landings write no fresh record** (stairs, step geometry, steep slopes)
    and need a fallback: grounded, key held, no jump of the engine's own within
    the last tick → one pair at the **first** fraction of the command being built.
    The fresh-frame trace is also unreliable on stairs/steep slopes. (771389
    closing remarks; 774033 `offer(0.f)` fallback + rescue at `age 2..5`.)
14. **Read `sv_autobunnyhopping` and `sv_legacy_jump` and stand down** if either
    is 1 — both reach takeoff by a different path. (771389 closing remarks.)
15. **The whole-tick `cmd->buttons` bit alone is one tick late and slow** — it
    reads as a press while airborne. (771389 Part III.)
16. **Speed loss is the ground-slice clamp, not friction.** Even with a perfect
    press, beta (a whole tick late) survives the clamp. Do not "tune the offset"
    to fix speed loss — only the tick alignment matters. (771389 Part IV; the
    author measured offset 3 vs 10 as inside the noise, 37% vs 45%.)
17. **`m_ModernJump` is network-var data in the
    `LocalPlayerAndPredictorsExclusive` group** — read it from the local pawn's
    movement services (a **pointer** at `C_BasePlayerPawn + 0x1330`), never from
    an interpolated remote copy.

---

## 7. Reference pseudocode / C++ sketch

### 7.1 The authoritative reference (verbatim, abridged for the jump path)

From **UC 774033** *"[Source] subtick bhop"*, InkCrow, 2026-09-27,
<https://www.unknowncheats.me/forum/counter-strike-2-a/774033-subtick-bhop.html>.
This is the author's own clean-room implementation of the 771389 chain. Quoted as
data; header comment is the author's.

```cpp
namespace bhop {
constexpr float grid = 1.0f / 64.0f;
constexpr float interval = 1.0f / 64.0f;
constexpr std::size_t capacity = 32;

struct Time { std::int64_t tick{}; float fraction{}; bool operator==(const Time&) const = default; };
struct Vec3 { float x{}, y{}, z{}; /* … */ };
struct Hull { Vec3 mins{}, maxs{}; };
struct Motion { Vec3 origin{}, velocity{}; Hull hull{};
                float gravity{}, gravity_scale{}, standable_normal{}; };

template<class Trace>
std::optional<Time> predict_landing(std::int64_t tick, const Motion& motion, Trace&& trace) {
    // validity checks on hull / origin / velocity / gravity / standable_normal …
    Vec3 velocity = motion.velocity;
    velocity.z -= motion.gravity * motion.gravity_scale * interval;
    const Vec3 end = motion.origin + velocity * interval;
    if (!(velocity.z < 0.0f) || !finite(end)) return {};
    const auto hit = trace(motion.origin, end, motion.hull);
    if (!hit || hit->start_solid || hit->all_solid || !finite(hit->normal)
        || !(hit->fraction > 0.0f && hit->fraction < 1.0f)
        || hit->normal.z < motion.standable_normal) return {};
    return Time{tick, hit->fraction};
}

float quantize(float fraction) { return std::round(fraction * 64.0f) * grid; }

float first_when(float contact) {
    return contact + grid <= 62.0f * grid
        ? std::min(contact + 3.0f * grid, 62.0f * grid)   // stand past the contact
        : std::min(contact - grid, 62.0f * grid);         // no room behind -> lead it
}

bool clear_of(Time now, std::optional<Time> previous, float penalty) {
    if (!previous) return true;
    if (now.tick < previous->tick) return false;
    // Subtract ticks BEFORE converting to float. Equality is rejected.
    const float elapsed = (static_cast<float>(now.tick - previous->tick)
        + now.fraction - previous->fraction) * interval;
    return elapsed > penalty;              // penalty = replicated sv_jump_spam_penalty_time
}

Decision decide(const State& state, const Frame& f) {
    Decision result{false, {}, state};
    if (!f.enabled || !f.held || !f.movement_allowed || f.jump_owned_elsewhere) return result;
    result.owns_jump = true;

    auto offer = [&](float when, Update update = Update::press) {
        const Time at{f.tick, when};
        if (!clear_of(at, f.actual_press, f.penalty)
            || !clear_of(at, state.last_press, f.penalty)) return false;
        result.when = when;
        result.after.last_press = at;
        /* … record first / followup bookkeeping … */
        return true;
    };
    auto followup = [&] {
        const float first = has_first ? state.first->fraction : 0.0f;
        return offer(std::clamp(quantize(std::max(f.landed->fraction, first) + grid),
                                grid, 63.0f * grid), Update::followup);
    };

    const auto age = f.landed ? f.tick - f.landed->tick : -1;
    if (f.landed) {
        const float contact = quantize(f.landed->fraction);
        if (age == 0 && offer(first_when(contact), Update::first))   // A: same command
            return result;
        if (f.grounded && age == 1) {                                // B: one past
            if (!has_first && contact > 49.0f * grid && offer(grid, Update::followup))
                return result;                                       // rescue: too-late contact
            if (followup()) return result;
        }
    }
    if (!f.grounded && f.falling && f.predicted_landing && valid(*f.predicted_landing)) {
        const float contact = quantize(f.predicted_landing->fraction);
        if (f.predicted_landing->tick == f.tick && contact + grid <= 62.0f * grid
            && offer(first_when(contact), Update::first))            // fresh-frame guess
            return result;
    }
    if (f.grounded && !(age >= 0 && age <= 1) && offer(0.0f)) return result; // no-record fallback
    if (f.grounded && age >= 2 && age <= 5 && state.completed_followup != f.landed)
        followup();                                                  // late rescue
    return result;
}

// Append the release+press pair, dropping the engine's own jump steps (we own jump),
// then INSERTION-SORT by `when` so the array stays monotonic (admission gate).
bool merge(std::span<const NativeStep> native, const Decision& decision, Batch& output) {
    Batch batch{}; batch.owns_jump = decision.owns_jump;
    batch.jump_down = decision.when.has_value();
    if (native.size() > capacity) return false;
    for (const NativeStep& step : native) {
        if (!std::isfinite(step.when) || step.when < 0.0f || step.when > 1.0f) return false;
        if (decision.owns_jump && step.jump) continue;      // drop the engine's jump step
        batch.steps[batch.count++] = { /*source*/ i, step.when, false };
    }
    if (decision.when) {
        if (batch.count + 2 > capacity) return false;       // would trip the 32 gate
        batch.steps[batch.count++] = {-1, *decision.when, false};  // release
        batch.steps[batch.count++] = {-1, *decision.when, true};   // press, SAME fraction
    }
    // insertion sort by `when` (small, and keeps the array monotonic)
    /* … */
    output = batch; return true;
}

class Controller {
    // State committed only after the entire write succeeds; deduped by command number.
    Result run(const Frame& frame, std::span<const NativeStep> native, Writer&& write) {
        if (command_ && frame.command < *command_) return Result::restore_required;
        if (!command_ || frame.command != *command_) {
            if (!ready(frame)) return Result::bad_snapshot;
            command_ = frame.command;
            decision_ = decide(state_, frame);   // decided ONCE per command
        }
        Batch batch{};
        if (!merge(native, decision_, batch)) return Result::no_room;
        if (!write(batch)) return Result::write_failed;
        state_ = decision_.after;                // commit only after the write lands
        return Result::written;
    }
    // reset() on disconnect / respawn / pawn-or-session change.
};
} // namespace bhop
```

Two design points worth copying:

- **Decide once per command, write every frame.** `decide()` runs only when
  `frame.command` changes; the *write* runs on every frame with the cached
  decision. This is how the author reconciles Part V ("re-derive every frame")
  with "don't double-apply" — the decision is stable within a command, the write
  is idempotent.
- **Commit state only after the write succeeds**, and require a monotonic command
  number (`restore_required` otherwise).

### 7.2 The 771389 Part XI post-hook, in outline

The 771389 first post ends with a complete `on_create_move(Cmd& cmd, int offset)`
post-hook (≈300 lines). Its structure, in order:

1. `TickView v = read_view()` — one read of the pawn/movement services/CMoveData
   per frame; contains `tickbase`, `cmd_number`, `landed_tick/frac`, `usable_*`,
   `jump_*`, `landed_speed`, `speed_now`, `on_ground`, `falling`, `probe_frac`.
2. `if (!v.tickbase_ok || !v.cmd_number_ok || !jump_held()) return;`
3. `fresh = (v.cmd_number != prev_number); build = v.tickbase + (fresh ? 1 : 0);`
4. `classify_a(rec, v)` — compare `usable`/`jump` latches against
   `a_tick - 1 + a_when` **before** anything this frame overwrites them.
5. Open a new `LandingRecord` when `v.landed_tick` changes; roll a pending
   fresh-frame guess into `a_placed` if it matches.
6. `if (rec.valid && build == rec.landed_tick + 1) p = pair_a(rec, offset);`
   `else if (rec.valid && b_due(rec, build)) p = pair_b(rec);`
   `else p = predicted_a(v, rec, build, fresh, offset);`
7. `write_pair(cmd, p.when)` — drop own steps, budget-check, monotonicity-check,
   range-check, then `append(release, when); append(press, when);`.
8. Update the record (`a_placed`/`b_placed`/pending) for instrumentation.

### 7.3 Sketch for *this* repo

Adapting to `payload/control/src/input.cpp`'s existing `Input::apply(cmd)` hook:

```cpp
// Called every frame from the CreateMove detour, AFTER the original ran
// (a POST hook): the engine has already built the command and, on a reuse
// frame, already simulated it.
void Input::apply_bhop(void* cmd) {
  if (!bhop_.load() || local_pawn_ == 0) return;

  // 1. read state (no engine calls)
  const uintptr_t svc = read_ptr(local_pawn_ + 0x1330);            // C_BasePlayerPawn::m_pMovementServices
  if (!svc) return;
  const uintptr_t mj  = svc + 0x6C8;                               // m_ModernJump
  const int   landed_tick = read_i32(mj + 0x20);
  const float landed_frac = read_f32(mj + 0x24);
  const int   usable_tick = read_i32(mj + 0x18);
  const float usable_frac = read_f32(mj + 0x1C);
  const float penalty     = curtime_seconds_of(sv_jump_spam_penalty_time);  // or read convar

  // 2. command identity + phase correction (771389 Part VI)
  const uint32_t n = read_cmd_number(cmd);
  const int      tm = read_movedata_tickbase(cmd);                 // see "open RE"
  const bool     fresh = !prev_known_ || n != prev_number_;
  prev_number_ = n; prev_known_ = true;
  const int build = tm + (fresh ? 1 : 0);

  // 3. our record bookkeeping: reset on respawn / new landing (see landmines 12, 6)
  // 4. choose placement
  float when = -1.0f;
  if (landed_frac == 0.0f && build == landed_tick + 1) {
      when = 3.0f / 64.0f;                                         // boundary landing → offset
  } else if (build == landed_tick + 1) {
      const float c = quantize64(landed_frac);
      when = std::clamp(c + 3.0f/64.0f, c + 1.0f/64.0f, 62.0f/64.0f);  // A
  } else if (build >= landed_tick + 2 && build <= landed_tick + 6 && !b_placed_) {
      const float c = quantize64(landed_frac);
      when = quantize64(std::max(c, a_when_) + 1.0f/64.0f);        // B
  } else if (fresh && !on_ground() && falling() && probe_frac_ > 0.0f && probe_frac_ < 1.0f) {
      const float c = quantize64(probe_frac_);                     // own hull trace
      if (c + 1.0f/64.0f <= 62.0f/64.0f) when = std::clamp(c + 3.0f/64.0f,
                                                           c + 1.0f/64.0f, 62.0f/64.0f);
  }
  if (when < 1.0f/64.0f || when > 62.0f/64.0f) return;

  // 5. write: exactly two steps at the same `when`, monotonic, budget-checked
  auto* base = user_cmd_base(cmd);
  if (!drop_own_jump_steps(base)) return;                          // only OUR steps
  if (step_count(base) + 2 > 32) return;
  if (step_count(base) != 0 && when < last_step_when(base)) {      // keep monotonic
      sort_steps_or_bail(base); return;
  }
  add_button_step(base, button::k_jump, /*pressed=*/false, when);
  add_button_step(base, button::k_jump, /*pressed=*/true,  when);

  // 6. keep the legacy mask consistent (the engine's own state at command start)
  //    — NOT a whole-tick bit; the pair above is the actual edge.
}
```

Notes that differ from the current payload implementation and must change:

- The target test is `build == landed_tick + 1` (**exact equality**), not a
  `delta ∈ {0,1,2}` derived from a simulated tickbase.
- The pair must be **release then press at the same `when`** — currently the code
  writes release+press but derives `when` from the *simulation*, which is the
  disabled path.
- Every frame must re-write; the current `if (bhop_...)` block already runs per
  frame, but must not carry an "already placed" guard that survives into the last
  frame of a tick.
- The `predicted_*`/`predict_runs` diagnostics can stay, but (a) is not needed.

---

## 8. Sources

| # | Source | URL | Confidence |
|---|---|---|---|
| 1 | **UC 771389**, "[Information] How to made a good subtick bhop", InkCrow, 2026-09-10 (OP permalink `4795131-post1`) | <https://www.unknowncheats.me/forum/counter-strike-2-a/771389-subtick-bhop.html> | **High** for mechanism/arithmetic — full first post retrieved verbatim. Addresses are for `client.dll` 2026-09-08 and are build-specific. Author admits heavy LLM use ("I used gpt a lot for the reversing, but everything in there I checked myself"). |
| 2 | **UC 774033**, "[Source] subtick bhop", InkCrow, 2026-09-27 | <https://www.unknowncheats.me/forum/counter-strike-2-a/774033-subtick-bhop.html> | **Highest** for implementation — full C++ source retrieved. Same author; community reaction in-thread ("feels really nice"). |
| 3 | **UC 754429**, "[Help] Subtick Bhop?", CodeEatSleep, 2026-05-25; reply by kauht (post #12): "don't use tracing, use prediction+`CCSPlayerModernJump`->`LastLandedFrac`" | <https://www.unknowncheats.me/forum/counter-strike-2-a/754429-subtick-bhop.html> | High for the *conclusion*; the OP code is the pre-modern 2-step recipe (press at 0.0, release at 0.01) and loses ~10%. |
| 4 | **UC 760037**, "cs2 bhop stops on thin surfaces / flat ground", Weestada, 2026-06-30; **Bop32** posts #2/#4 (prediction recipe, slots 29/43/44, `CPrediction` offsets) | <https://www.unknowncheats.me/forum/counter-strike-2-a/760037-cs2-bhop-stops-thin-surfaces-flat-ground-obs-drop-false-positive.html> | High for the prediction API shape; matches this repo's independent vtable verification. |
| 5 | **UC 761434**, "[Help] engine prediction not working", joeboe333, 2026-07-08; full `CPrediction::Start/End`; Bop32 post #2 correction | <https://www.unknowncheats.me/forum/counter-strike-2-a/761434-engine-prediction.html> | High for the save/restore field list; the OP itself was crash-prone — the *posts* are the value. |
| 6 | **UC 676287**, "[Source] Subtick bhop", Tectonicwave, 2024-12-08 | <https://www.unknowncheats.me/forum/counter-strike-2-a/676287-subtick-bhop.html> | Medium — **pre-dates** the modern jump chain; the "12 steps, press at i=0, release at i=max-1" recipe. Exlodium: "2 is enough". Useful for the edge shape only. |
| 7 | **UC 729264**, "[Discuss] Working around sv_quantize_movement_input for auto strafer", DylanDakota019, 2025-12-13; **Sumandora** post #5 (the maths) | <https://www.unknowncheats.me/forum/counter-strike-2-a/729264-sv_quantize_movement_input-auto-strafer.html> | High — `sv_quantize_movement_input` rounds `forwardmove`/`leftmove` to `{-1,0,1}`; the move set `S` has 9 states; solve for **yaw** instead of move to keep fractional movement. |
| 8 | **UC 753644**, "[Help] movement fix + sv_quantize_movement_input", cacadura9090, 2026-05-21 | <https://www.unknowncheats.me/forum/counter-strike-2-a/753644-movement-fix-sv_quantize_movement_input.html> | Medium-High — confirms the rounding, and that a sub-tick step only worked after `set_when(1.f)` (thread posts show `set_when(0.f)` then `set_when(1.f)`). |
| 9 | **UC 769450**, "[Help] antiaim movement / subticks", gentpooy, 2026-08-28 | <https://www.unknowncheats.me/forum/counter-strike-2-a/769450-antiaim-movement-subticks.html> | High for: `CSubtickMoveStep` layout (metadata@8, has_bits@0x10…), the yaw-delta vs analog-delta distinction, and "don't stuff the repeated field — the engine's real `IN_JUMP` press/release steps live there". |
| 10 | **UC 765984**, "[Help] Subtick bhop keeps losing speed on landing", lra123, 2026-08-06 | <https://www.unknowncheats.me/forum/counter-strike-2-a/765984-subtick-bhop-losing-speed-landing-logs-accepted-edges-delayed-jum.html> | Medium-High — instrumentation: accepted edges but delay=2 → retained ≈0.49; confirmed-ground + `m_flLastLandedFrac` → 100% delay=1, retained 0.83–1.06; linear trace early by ~0.25 tick in 79%. |
| 11 | **`a2x/cs2-dumper` `client_dll.hpp`**, generated 2026-10-15 | <https://github.com/a2x/cs2-dumper/blob/main/output/client_dll.hpp> | **High** — authoritative field offsets; matches `s2v.app` and this repo. |
| 12 | **s2v.app schema explorer** (`CCSPlayerModernJump`, `CCSPlayer_MovementServices`), revision 11131657, 2026-10-14 | <https://s2v.app/SchemaExplorer/cs2/client/CCSPlayerModernJump> | **High** — sizes (`0x38`, `0xFE0`), `m_ModernJump +0x6C8`, network groups, bit/range metadata. |
| 13 | **CS2 patch notes 2026-01-21** (official), via HLTV | <https://www.hltv.org/news/43689/anubis-mp7-and-mp5-sd-receive-changes-in-latest-cs2-update> | **High** — official text of the jump change, the three convars, and `sv_legacy_jump`. |
| 14 | **CS2 Docs convar list** (`sv_jump_spam_penalty_time`, `sv_autobunnyhopping`, `sv_enablebunnyhopping`, `sv_jump_impulse`) | <https://cs2.poggu.me/dumped-data/convar-list/> | High for the listed convars; `sv_bhop_time_window` and `sv_legacy_jump` are **not** in the public list (hidden). |
| 15 | **tradeit.gg** "CS2 Bhop Commands" (lists `sv_bhop_time_window 0.007812`, `sv_jump_spam_penalty_time 0.015625`, `sv_legacy_jump 0`, `sv_enablebunnyhopping 1`, `sv_autobunnyhopping 1`) | <https://tradeit.gg/blog/cs2-bhop-commands/> | **Low-Medium** — a third-party blog whose numbers are part of a *training config*, so the `1`s are config values, not defaults. Only the `0.007812` figure actually matters here, and it is unverified against a dump. |
| 16 | **YouGame thread 392281**, "Помогите с bhop prediction" (prediction + `SCHEMA_OFFSET("CCSPlayerModernJump", …)`) | <https://yougame.biz/threads/392281/> | **Unavailable** — HTTP 429 on every fetch attempt. Referenced only via search snippets; **unverified**. |
| 17 | **Valve Developer Wiki**, List of CS2 console commands and variables | <https://developer.valvesoftware.com/wiki/List_of_Counter-Strike_2_console_commands_and_variables> | **Blocked** by Anubis anti-bot; could not retrieve. Search snippets attribute `sv_jump_spam_penalty_time, 0.015625` to it. Unverified. |
| 18 | **`movement/impl/bunnyhop.cpp` / `airstrafe.cpp`** | — | **NOT FOUND.** Referenced only by this repo's `docs/re/prediction.md` and `docs/control-harness.md`. Extensive searches (GitHub, code search, forum) did not locate a public repo with a `movement/impl/` directory. Treat as **unverified / private**; do not cite it as corroboration in new work. |
| 19 | This repo: `docs/re/prediction.md`, `docs/re/subtick.md`, `docs/handoff.md`, `payload/control/src/input.cpp` | — | Local notes; several claims are **corrected** by this report (see §9). |

---

## 9. Open questions / things that must be verified by in-process RE

1. **`movedata->tickbase` access on this build.** The post-hook needs the
   `CMoveData` tickbase of the *last simulated command*. 771389 names the writer
   (`0x180A11640` region) but not a cheap reader. Options: (a) hook the function
   that writes `movedata->tickbase` and stash it; (b) read the global
   `gpGlobals->tickcount` (it is set to `tickbase` inside `RunCommand`, per Part
   VI) — **must confirm** it is not advanced elsewhere between the simulation and
   the post-hook; (c) re-derive `tickbase` from `client_tick` + the prediction
   lead (the author says this is *not* valid on its own). This is **the** blocker
   for the record-based design on our build.
2. **Where `sv_bhop_time_window`'s default actually lives (and its units).**
   Not in the public convar dump; read the convar in-process (`FindConVar`) and
   confirm the value and units (seconds vs ticks) before hard-coding `16/64`.
3. **Whether the landing record is readable on the reuse frame on our build.**
   The whole f>0 path depends on `m_nLastLandedTick`/`m_flLastLandedFrac` being
   populated by the engine's own prediction *before* our post-hook runs. Confirm
   by logging the record every frame for a falling player and watching it change
   on the tick *after* contact.
4. **`CMD` lifecycle byte.** 771389 Part V says lifecycle `3` = "will not be
   simulated again" and is skipped in prediction. Find the byte on our `CUserCmd`
   (the author locates `CreateMove` by three vftable names: `CUserCmdBase`,
   `CUserCmdBaseHost<CSGOUserCmdPB>`, `CUserCmd`) and skip those commands.
5. **The `CMoveData` slice-begin-fraction field.** Not needed for the read-only
   design, but needed if we ever want to reproduce `NOW` ourselves.
6. **`m_bHasEverProcessedCommand` semantics** (`+0xFD0`) — a cheap guard against
   acting on the very first command after spawn.
7. **The three unnamed `DoJump` gates.** 771389 Part III labels the first three
   gates "unnamed pawn fields … identity not resolved": `pawn_u8_a == 2`;
   `handle_target_is_invalid(pawn_handle_b)`; `pawn_timer_c_active &&
   pawn_timer_c - now > 1.0f`. They clear `usable` on the way out (killing the
   press) and are worth identifying before trusting the alpha path in every
   situation (e.g. while spectating, dead, or on a team-kill timer).
8. **Hull-trace parameters for the fresh-frame guess.** `gravity`,
   `gravity_scale`, `standable_normal` (774033 reads these from game state); the
   exact hull mins/maxs for the standing/ducking player; and whether the trace
   should extend 2 units down (as UC 765984's `StayOnGround`-style trace does:
   `traceEnd.z = physicalEnd.z - 2.0f`).
9. **`sv_quantize_movement_input` interaction.** A bhop press is a *button* step,
   not an analog step, so quantisation should not affect it — but if a combined
   bhop+strafe writes analog steps in the same command, the interaction with the
   admission gate (`view_delta_accum`, the 32-step budget, monotonicity) must be
   re-checked, because the engine's own steps are already in the array.
10. **Server-side validation / legitimate-only concerns.** Out of scope for an
    offline local server, but noted: the same publishers ship server.dll
    `DECIDE`/`DoJump` (`0x180A7EF90` / `0x180AA8F70` in the 2026-09 era), i.e.
    the server runs the identical chain and identical admission gate. There is no
    known client-only trick here; the sub-tick trace granularity is the intended
    mechanism. `m_vecPredictionError` will still register a correction if client
    and server disagree by one takeoff (771389 Part IX: "~6 unit correction
    arriving five to seven ticks after the landing").

### Corrections to the local notes

- `docs/re/prediction.md` says the reference "simulates up to 4 ticks ahead and
  reads the recorded landing" and plans `simulate_ticks(cmd, n)` via
  `RunCommand`. That is strategy (a), the **community** recipe. The 771389
  reference does **not** call `RunCommand` — it reads the engine's record and
  uses its own hull trace only on the fresh frame. The note's own conclusion
  ("The thread's own design does not call `RunCommand`") is **correct**; the
  earlier "Plan" section contradicts it and should be retired.
- "the author measured at ~94% online / 100% offline" — the thread's actual
  numbers are **6% late online** (i.e. ≈94% on-time) **vs 38% late before the
  fresh-frame guess**, and **100% offline**. Fine, but quote the real figures.
- The current `Input::apply` bhop derives `when` from a `landed_delta` produced by
  the disabled simulation. Replace with the exact-equality test
  `build == landed_tick + 1` and the `f == 0` branch.
</content>
</invoke>
