# The strafe's side-axis sign — the `leftmove` convention

> **Status (2026-10-20): settled from the engine's own code, and the write is
> fixed (T13).** The engine's second view-frame basis vector is the player's
> **LEFT** (`(-sin yaw, +cos yaw)` — the forward vector rotated **+90°**), and
> the command's side field is `leftmove` — **positive to the LEFT**. This
> repo's old model had *both* halves mirrored ("the basis is the right vector"
> and "a positive side axis is the right"), which cancelled in the goal
> heading — so `goal_for` was right — but did **not** cancel in the analog
> write: see §4.2, which is **now fixed** (the write goes through
> `engine_side_axis_for_wish()` and the button bits through
> `engine_move_buttons()`). Neverlose's `+1.0` for `IN_MOVELEFT` is **right**.
> The same pass found the repo's console button names wrong: CS2's strafe
> commands are **`+left` / `+right`**, not `+moveleft` / `+moveright` (§4.3).
>
> Live build **25947352**, `client.dll` sha256 `659940c1…`, image base
> `0x180000000`. Every address below is an RVA on that build. The derivation
> was done with REA/Ghidra 12.1.4 against
> `C:\Users\gpmpo\ghidra-analysis\client-25947352.dll` (the warm cached
> project).

## 1. The question

The directional auto-strafe derives its travel heading from the held W/A/S/D.
The A/D headings come from the engine's **view-frame basis** — the same vector
the engine multiplies the command's side axis by. Two sources disagreed:

| Source | Claim |
|---|---|
| this repo (before this note) | the basis is `right(yaw) = (sin yaw, -cos yaw)` and a **positive** side axis is the player's **right** |
| the Neverlose reversal | writes `cmd+0x30 = +1.0` when `IN_MOVELEFT` (A) is set — a **positive** side axis is the player's **left** |

They cannot both be right. This note settles it from the engine.

## 2. The engine's basis vector

The view-frame basis is Valve's `AngleVectors`, inlined twice in `client.dll`:

| RVA | Function | Note |
|---|---|---|
| `0x173cb90` | `FUN_18173cb90` | the copy the movement code calls |
| `0x1742af0` | `FUN_181742af0` | a byte-identical copy used by the input code |

Decompiled body (identical in both):

```c
V_sincosf(pitch * DEG2RAD, &sin_pitch, &cos_pitch);
V_sincosf(yaw   * DEG2RAD, &sin_yaw,   &cos_yaw);
V_sincosf(roll  * DEG2RAD, &sin_roll,  &cos_roll);

out[0] = cos_yaw * cos_pitch;          // forward.x
out[1] = sin_yaw * cos_pitch;          // forward.y
out[2] = -sin_pitch;                   // forward.z
out[4] = sin_roll * sin_pitch * cos_yaw - cos_roll * sin_yaw;   // basis.x
out[5] = sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw;   // basis.y
out[6] = sin_roll * cos_pitch;                                // basis.z
```

At the movement's roll = 0 and pitch = 0 the **second** output is

```
basis(yaw) = (-sin yaw, +cos yaw, 0)
```

which is the forward vector `(cos yaw, sin yaw)` rotated **+90°** (counter-
clockwise in the heading/atan2 sense) — **not** `(sin yaw, -cos yaw)`, which
is the -90° rotation. The repo's old `right(yaw)` was the **negation** of the
engine's second output.

## 3. The side axis: `leftmove`, positive to the left

Three independent facts in the live build:

1. **The field's name.** The movement services' schema string table
   (`client.dll` `0x1c39980`ff) has `m_flCmdForwardMove` (`0x1c39980`),
   **`m_flCmdLeftMove`** (`0x1c39998`), `m_flCmdUpMove`, then `m_flForwardMove`
   and `m_flLeftMove`. The side axis is the *left* move — and there is **no**
   `m_flCmdSideMove` string in the binary. The command's protobuf field is
   `leftmove` (field 6 of `CBaseUserCmdPB`), the same name Source 1's
   `CUserCmd::sideMove` carried with the comment "*+ left = 2048*".
2. **The engine's own input.** The client builds the lateral axis in
   `FUN_180b8f6e0` (RVA `0xb8f6e0`, the input system's move-vector
   computation):

   ```c
   fwd   = value(&symbol_forward) - value(&symbol_back);
   lateral = value(&symbol_left) - value(&symbol_right);      // <-- "left" MINUS "right"
   move  = scale * (forward_basis * fwd + basis * lateral);
   ```

   The two symbols are the input actions named **"left"** and **"right"**
   (the strings at `0x1c60498`ff; the *look* actions are separately named
   "turnleft"/"turnright", `0x1c60458`/`0x1c60468`). So the engine's lateral
   axis is **+1 when the left action is held** and -1 for the right one.
3. **The wish.** `AirMove` (vprof `PreSource1AirMove`, `FUN_1808e57b0`,
   RVA `0x8e57b0`) builds the wish as

   ```c
   FUN_181742af0(movedata + 0x14, &fwd, &basis, scratch);   // the CMoveData's angles
   fmove  = *(float *)(movedata + 0x2c);                    // forwardmove
   smove  = *(float *)(movedata + 0x30);                    // leftmove
   wish.x = fwd.x * fmove + basis.x * smove;
   wish.y = basis.y * smove + fwd.y * fmove;
   ```

   so the side axis is multiplied by the **second** basis output of §2.

Putting (1)-(3) together: **a positive side axis is the player's LEFT, and it
is multiplied by the +90° (left) basis vector.** The two halves are coupled —
a positive-left axis could only be multiplied by the left vector, or the
engine's own `A` key would strafe right.

So, in world headings (the camera at `yaw`):

| Key | engine side axis | world lateral | heading |
|---|---|---|---|
| `A` (`IN_MOVELEFT`) | `+1` | `(-sin yaw, +cos yaw)` | `yaw + 90°` — the player's **left** |
| `D` (`IN_MOVERIGHT`) | `-1` | `(+sin yaw, -cos yaw)` | `yaw - 90°` — the player's **right** |

**Neverlose is right** (it writes `+1` for A and composes with the same
`yaw + 90°` lateral vector).

## 4. What it means for our code

### 4.1 The goal heading is right

`goal_for` asks for `D` → `yaw - 90°` and `A` → `yaw + 90°`, which is exactly
the table above. The old model reached the right answer by two cancelling
mistakes: it called the engine's basis "the right vector" (it is the negation
of it) *and* called a positive side axis "right" (it is left). The two
negations cancelled, so the heading was never wrong — and the *observable*
headings of the A/D keys are facts about the world, independent of the
engine's internal encoding.

`strafe_geometry.hpp` now states the engine's model instead of the assumption:
`goal_for` builds the heading from the engine's basis and the engine's side
axis (`engine_view_basis()` / `engine_side_axis()`), and the single constant
`k_engine_side_axis_positive_is_left` records the convention. The heading is
**unchanged** — the new code and the old code agree for every input, because
the two negations still cancel, only now visibly.

> The goal is **invariant** under the convention constant, and that is correct:
> the heading of the A/D keys is a fact about the world, and both candidate
> encodings of the engine produce the same world direction. What the constant
> changes is the **encoding** — i.e. the number the payload has to write into
> the command's side axis (§4.2). Flipping it therefore moves the write, not
> the goal.

### 4.2 The analog write was mirrored (a real bug, in `input.cpp`)

`Input::strafe_tick` wrote the camera-frame analog as

```cpp
const float rel = rot * k_deg_to_rad;
const float forward = std::cos(rel);
const float left = -std::sin(rel);              // <- the command's `leftmove`
```

`rel` is the wish's offset from the view forward, **counter-clockwise
positive**. The engine builds the wish as `forward * fmove + basis * leftmove`
with `basis` the +90° (left) vector, so the wish at `rel` needs

```
leftmove = +sin(rel)          (a counter-clockwise wish is a POSITIVE side axis)
```

but the payload wrote `-sin(rel)` — the mirrored value. Concretely, with the
camera at `yaw = 0` and a wish 90° counter-clockwise (`rel = +90°`):

| | written | engine's world lateral |
|---|---|---|
| payload (before the fix) | `leftmove = -1` | `basis * (-1) = (0, -1)` = the player's **right** |
| wanted | `leftmove = +1` | `basis * (+1) = (0, +1)` = the player's **left** |

The payload's *button* bit was right (`IN_MOVELEFT` for a leftward wish) while
its *value* was wrong, so the two contradicted each other; the value is what
multiplies into the wish, so the analog path (`strafe(subtick=0)`, the
default) steered the wrong way. The **sub-tick yaw path is unaffected**: it
writes `forwardmove = 1, leftmove = 0` and puts the direction in the
`yaw_delta` steps, which is a basis rotation and not a lateral axis.

> **Fixed (2026-10-20, T13 — `fix(movement): write the strafe's wish in the
> engine's side axis`).** The write goes through the helpers, so the sign
> lives in exactly one place (`k_engine_side_axis_positive_is_left`):
>
> ```cpp
> // the command's `leftmove` — the engine's axis, positive to the LEFT
> const float left = cs2::control::engine_side_axis_for_wish(rot);
> ```
>
> and the button bits that go with it come from the same convention
> (`engine_move_buttons()`, in `strafe_geometry.hpp`):
>
> ```cpp
> const std::uint64_t held = cs2::control::engine_move_buttons(forward, left, k_move_epsilon);
> // a positive side axis -> IN_MOVELEFT, a negative one -> IN_MOVERIGHT
> ```
>
> The host test `control-strafe` pins both halves
> (`test_the_written_side_axis_carries_the_engine_sign`,
> `test_the_button_names_map_to_the_engine_bits`): a wish 90° to the player's
> left must write a **positive** side axis and the `IN_MOVELEFT` bit, and the
> engine's own wish construction (`forward * fmove + basis * leftmove`) must
> come out at `yaw + 90°`.

### 4.3 The button names: `left`, not `moveleft`

The same investigation turned up a second, independent defect: the repo's
**console** button names do not exist in CS2.

* The engine's **input actions** are named `left` / `right` (the **strafe**)
  and `turnleft` / `turnright` (the **look**). `client.dll` registers them —
  `FUN_1800d0980` registers the action `left` (the string at `0x1c60548`), and
  the input system's move-vector computation (`FUN_180b8f6e0`, RVA `0xb8f6e0`)
  reads `value("left") - value("right")` for the lateral axis.
* The **console command** is the action name with a `+` in front. CS2's own
  shipped key bindings say so — `game/csgo/cfg/user_keys_default.vcfg`:
  `"a" -> "+left"`, `"d" -> "+right"`, `"w" -> "+forward"`, `"s" -> "+back"`,
  `"LEFTARROW" -> "+turnleft"`, `"UPARROW" -> "+turnup"`; and
  `game/core/cfg/user_keys_default.vcfg` repeats the lateral two.
* The strings `moveleft` / `moveright` do **not** exist in `client.dll` at all
  (only `IN_MOVELEFT`, `onmoveleft` and `MoveLeft`), and CS2 has no
  `+lookup` / `+lookdown` either — the look commands are `+turnup` /
  `+turndown`.

So the repo's `+moveleft` / `+moveright` were **unknown console commands**: the
scenario runner's `press("a")` never registered the player's strafe-left, and
the payload's own name table accepted a name the engine does not know.

The engine's own **button-name table** (`client.dll` build 25947352, sha256
`659940c1…`, image base `0x180000000`: the table at `0x181b28cf0`, the names
at `0x181b28f70` — 20 records of `{const char* name; uint64_t bit;}`) gives
the bits and spells them the way CS2 does:

| name | mask | meaning |
|---|---|---|
| `IN_ALL` | `~0` | every button |
| `IN_ATTACK` | `1 << 0` | |
| `IN_JUMP` | `1 << 1` | |
| `IN_DUCK` | `1 << 2` | |
| `IN_FORWARD` | `1 << 3` | |
| `IN_BACK` | `1 << 4` | |
| `IN_USE` | `1 << 5` | |
| `IN_TURNLEFT` | `1 << 7` | the **look** left (CS2 has no `IN_LEFT`) |
| `IN_TURNRIGHT` | `1 << 8` | the **look** right (CS2 has no `IN_RIGHT`) |
| `IN_MOVELEFT` | `1 << 9` | the **strafe** left |
| `IN_MOVERIGHT` | `1 << 10` | the **strafe** right |
| `IN_ATTACK2` | `1 << 11` | |
| `IN_RELOAD` | `1 << 13` | |
| `IN_SPEED` | `1 << 16` | the **sprint** key (`+sprint`) |
| `IN_JOYAUTOSPRINT` | `1 << 17` | |
| `IN_FIRST_MOD_SPECIFIC_BIT` | `1 << 32` | the mod-specific range starts here |
| `IN_USEORRELOAD` | `1 << 32` | |
| `IN_SCORE` | `1 << 33` | the scoreboard (`+showscores`) |
| `IN_ZOOM` | `1 << 34` | |
| `IN_LOOK_AT_WEAPON` | `1 << 35` | (`+lookatweapon`) |

The table is the engine's **own** button registry, not a header: the
`in_forceinput` console command (`client.dll` `0x180b85b80`) looks a name up in
it (`FUN_1804b6750`), resolves it to the enumerator's value
(`FUN_181982160`) and ORs that value into the forced-button mask
(`DAT_18255f140` / `DAT_18255f138`). So these are the bits the engine itself
puts in a command's button mask.

The **input actions** carry the same bits independently: each console-command
object holds the mask its action sets at `<object> - 8` (`client.dll`
`0x182233d80`ff), so `attack` → `1 << 0`, `jump` → `1 << 1`, `duck` →
`1 << 2`, `forward` → `1 << 3`, `back` → `1 << 4`, `use` → `1 << 5`,
`turnleft` → `1 << 7`, `turnright` → `1 << 8`, `left` → `1 << 9`, `right` →
`1 << 10`, `attack2` → `1 << 11`, `reload` → `1 << 13` and **`sprint` →
`1 << 16`** (the shift key). The **analog** actions (`turnup`, `turndown`,
`moveup`, `movedown`, `strafe`, `klook`) set no button at all.

The strafe bits the payload uses (`IN_MOVELEFT = 1 << 9`,
`IN_MOVERIGHT = 1 << 10`) are therefore **right**; only the *names* were
wrong. The fix (T13):

* `strafe_geometry.hpp` renames the bits 7/8 to `k_turnleft` / `k_turnright`
  (they are `IN_TURNLEFT` / `IN_TURNRIGHT` in CS2, not `IN_LEFT` /
  `IN_RIGHT`) and owns the **name → bit map**, so the host test can pin it;
* the name `left` now resolves to `IN_MOVELEFT` (the engine's strafe-left
  action) and `right` to `IN_MOVERIGHT`; the look actions are `turnleft` /
  `turnright`;
* `scripts/scenario/control.py`'s `BUTTONS` table maps `"a" -> "left"` and
  `"d" -> "right"`, so `press("a")` now issues `+left`.

> **Closed by T14.** The same table shows that CS2 moved `IN_SCORE` to
> `1 << 33` and `IN_ZOOM` to `1 << 34` (and that `1 << 16` is `IN_SPEED`, the
> **sprint** key), while the repo still used `1 << 16` / `1 << 19` / `1 << 17`
> for them (`strafe_geometry.hpp`'s `button` namespace). The whole `button`
> namespace has been re-derived from the engine's table and pinned by a host
> test (`test_the_button_bits_are_the_engine_bits`, the table as data with the
> address it was read from) — see §4.4.

### 4.4 The button bits, audited (T14)

Every `IN_*` constant the repo defines was compared with the engine's own
button-name table (the table above). Three were **wrong** and five did not
exist in this build at all:

| repo constant | was | now | the engine's name |
|---|---|---|---|
| `k_score` | `1 << 16` | **`1 << 33`** | `IN_SCORE` |
| `k_speed` | `1 << 17` | **`1 << 16`** | `IN_SPEED` (the sprint key) |
| `k_zoom` | `1 << 19` | **`1 << 34`** | `IN_ZOOM` |
| `k_cancel` | `1 << 6` | *(removed)* | not in this build |
| `k_run` | `1 << 12` | *(removed)* | not in this build |
| `k_alt1` | `1 << 14` | *(removed)* | not in this build |
| `k_alt2` | `1 << 15` | *(removed)* | not in this build |
| `k_walk` | `1 << 18` | *(removed)* | not in this build |

The wrong three came from the **community's CS2 SDK header**, which still
lists the Source-1 `IN_SCORE` 16 / `IN_SPEED` 17 / `IN_WALK` 18 / `IN_ZOOM` 19
*and* the CS2 `IN_SCORE` 33 / `IN_ZOOM` 34 — the two `IN_SCORE` and two
`IN_ZOOM` entries cannot both be the engine's. The engine's own table has
exactly one of each (`client.dll` contains one `IN_SCORE` string and one
`IN_ZOOM` string, both in that table), so **the engine's table is the truth**
and the SDK header is a stale reconstruction.

The five removed constants name bits that do not exist in this build: the
strings `IN_CANCEL`, `IN_RUN`, `IN_ALT1`, `IN_ALT2` and `IN_WALK` occur
**nowhere** in `client.dll`. A constant for them is a bit the engine never
reads — the same hazard the three wrong ones were. `k_run` and `k_walk` were
also in the payload's name map (`"run"`, `"walk"`), so those two names are
gone from it; `"sprint"` was added instead (the engine's own action for
`IN_SPEED`).

**The bits the payload uses elsewhere are unaffected:** the auto-strafe, the
bunnyhop (`IN_JUMP`), the triggerbot (`IN_ATTACK`) and the ragebot's
auto-stop (`IN_FORWARD` / `IN_BACK` / `IN_MOVELEFT` / `IN_MOVERIGHT`) all use
bits the table confirms.

**The auto-scope** is the one feature that depended on a wrong bit: it
pressed `IN_ATTACK2 | IN_ZOOM`. `IN_ATTACK2` (`1 << 11`) is unchanged and is
what *triggers* the scope (it is the right mouse, the engine's `attack2`
action); the `IN_ZOOM` half was `1 << 19`, a bit this build's table does not
name at all, so the write was inert. It is now the engine's real `IN_ZOOM`
(`1 << 34`). `IN_ZOOM` sits in the "mod specific" range that starts at
`IN_FIRST_MOD_SPECIFIC_BIT` (`1 << 32`) and no client action produces it — the
engine sets it itself — so the write is a *state* assertion, and the scope
still rides on `IN_ATTACK2`. **Not verified in a live game**: whether a
client-written `IN_ZOOM` is accepted, ignored or rejected by the server, and
whether it makes the local model read as scoped a command early.

The **console-command table** (`scripts/scenario/control.py`) was audited
against the same build at the same time; see §4.5.

### 4.5 The console commands (T14)

The runner's `BUTTONS` table maps a friendly key to an engine **console
command**, and the engine's console commands are its **input action** names
with a `+` in front. The engine registers these actions (`client.dll`, the
registrations `FUN_1800d0660`ff, the names at `0x181c60448`ff):

`attack`, `attack2`, `back`, `duck`, `forward`, `jump`, `klook`, `left`,
`movedown`, `moveup`, `reload`, `right`, `sprint`, `strafe`, `turndown`,
`turnleft`, `turnright`, `turnup`, `use`.

CS2's own shipped binds are the ground truth
(`game/csgo/cfg/user_keys_default.vcfg` and
`game/core/cfg/user_keys_default.vcfg`): `"a" -> "+left"`, `"d" -> "+right"`,
`"w" -> "+forward"`, `"s" -> "+back"`, `"e" -> "+use"`, `"r" -> "+reload"`,
`"CTRL" -> "+duck"`, `"SHIFT" -> "+sprint"`, `"SPACE" -> "+jump"`,
`"MOUSE1" -> "+attack"`, `"MOUSE2" -> "+attack2"`, `"UPARROW" -> "+turnup"`,
`"DOWNARROW" -> "+turndown"`, `"LEFTARROW" -> "+turnleft"`,
`"RIGHTARROW" -> "+turnright"`.

Four more of the repo's entries were wrong (or named a command CS2 does not
have):

| runner key | was | now | why |
|---|---|---|---|
| `shift` | `+speed` | **`+sprint`** | there is no `+speed` in CS2; the shift key is `+sprint` (`IN_SPEED`) |
| `up` | `+lookup` | **`+turnup`** | CS2 has no `+lookup` (nor `+lookdown`) |
| `down` | `+lookdown` | **`+turndown`** | the look commands are `+turnup` / `+turndown` |
| `walk` | `+speed` | *(removed)* | there is no `+walk` in CS2 either, and no `IN_WALK` |

`+turnup` / `+turndown` are **analog** pitch actions: they set no button bit
at all, so they are console-only names (the payload's mask has no bit for
them).

### 4.6 Naming clean-ups the same finding implies

* `docs/re/strafing.md` called the field `m_flCmdSideMove`; the build's schema
  says **`m_flCmdLeftMove`**.
* The old "`right(yaw) = (sin yaw, -cos yaw)`" note is the *negation* of the
  engine's basis. The engine's own `AngleVectors` calls that output "right",
  which is Valve's historical name for it; with the engine's own "left" action
  feeding it, it is geometrically the **left** vector.

## 5. The live check (the confirmation, and how to re-run it)

### 5.0 The result (2026-10-20): the convention is confirmed

**Holding `A` moves the player to their LEFT**, so the engine's side axis is
**positive-LEFT** and §2-§4 hold. The measured displacement over a 1.5 s hold,
projected onto the engine's own left vector `(−sin yaw, +cos yaw)`:

```
displacement: 71.7 units
  along the engine's LEFT vector: +20.1      <- holding A goes LEFT
  along the view forward:           +68.8
```

The positive lateral component is the confirmation: the repo's **goal** was
right, the repo's **analog write** was mirrored, and the fix of §4.2 (write
`+sin(rel)`, and pair it with `IN_MOVELEFT`) is correct.

The check was run through the OpenCode MCP tools — `cs2_press({key: "a",
hold: 1.5})` then `cs2_state()` — against a live bot match, with the payload
reporting `protocol: 2`.

### 5.1 How to re-run it

The static derivation above is self-consistent and complete, but one
observation confirms it end to end: **read the command's side axis while the
player holds `A` and nothing else.**

```powershell
# 1. the game is running (session 1) and the payload is injected;
# 2. hold A (and nothing else) in the game;
# 3. in the agent's shell, read the command's move axes:
python -c "import sys,json; sys.path.insert(0,'scripts'); from scenario.control import Control; c=Control().connect(); s=c.request('input'); print(json.dumps({k:s.get(k) for k in ('base_move','raw_move','entry_move')}, indent=1))"
```

`base_move` is the command's own `forwardmove`/`leftmove` pair (`base + 0x58`),
`raw_move` the raw axes (`CCSGOInput + 0x270/0x274`) and `entry_move` the
per-controller input entry's move pair — all three carry the same lateral
value, and `side` is the lateral one.

| observed `side` while holding `A` | meaning |
|---|---|
| **+1.0** | the engine's side axis is positive-LEFT — §3 holds, the repo's goal is right, and the analog write needs the sign flip of §4.2 |
| **-1.0** | the engine's side axis is positive-RIGHT — the repo's *old* model was right end to end; then the basis of §2 would have to be the -90° vector for `A` to strafe left, which the decompilation contradicts, so re-derive before touching anything |

If the strafe-left button cannot be held from the console, hold the physical
`A` key — that is the check that matters. (The engine's input action for the
strafe-left is named **`left`** — the string at `0x1c60548`; the look actions
are `turnleft`/`turnright` at `0x1c60458`/`0x1c60468`. `moveleft` does not
appear anywhere in this build, so the console command for the strafe-left
button is **`+left`**, not `+moveleft` — confirmed by CS2's own shipped
binds, `game/csgo/cfg/user_keys_default.vcfg`: `"a" -> "+left"`.)

The behavioural twin of the same check, and the one that actually matters for
the strafe: with the auto-strafe on in the **analog** mode (`subtick=0`) and
`A` held, the velocity must curve to the player's **left**. If it curves
right, the write of §4.2 is still mirrored.

## 6. The evidence, in one table

| # | Fact | Where (RVA / address, build 25947352) |
|---|---|---|
| 1 | the second basis output is `(-sin yaw, +cos yaw)` (forward rotated +90°) | `0x173cb90` and `0x1742af0` |
| 2 | the wish is `forward * movedata[0x2c] + basis * movedata[0x30]` | `0x8e57b0` (`PreSource1AirMove`) |
| 3 | the engine's lateral axis is `value("left") - value("right")` | `0xb8f6e0`; the action names at `0x1c60548`/`0x1c60550` |
| 4 | the side field is the *left* move | `m_flCmdLeftMove` at `0x1c39998`; `CBaseUserCmdPB.leftmove` (field 6) |
| 5 | the CMoveData's move axes are at `+0x2c` / `+0x30` | `0x8e47e0` (`SetupMove`) zeroes them |
| 6 | the button bits: `IN_TURNLEFT` = `1 << 7`, `IN_TURNRIGHT` = `1 << 8`, `IN_MOVELEFT` = `1 << 9`, `IN_MOVERIGHT` = `1 << 10` | the button-name table at `0x181b28cf0`, the names at `0x181b28f70` |
| 7 | the console command for an action is `+` + the action name (`+left`, `+turnleft`, …); there is no `+moveleft` | CS2's shipped binds, `game/csgo/cfg/user_keys_default.vcfg` |
| 8 | the whole button-name table, and the action → button mask (e.g. `sprint` → `IN_SPEED` = `1 << 16`); the `in_forceinput` command ORs the table's values into the forced-button mask | the table at `0x181b28cf0`; the action objects at `0x182233d80`ff (mask at `<object> - 8`); `in_forceinput` at `0x180b85b80` |

Companion documents: [`strafing.md`](strafing.md) (the engine model),
[`research-strafe.md`](research-strafe.md) (the community research, §9.3
item 7 is this question), [`../research/neverlose-mining.md`](../research/neverlose-mining.md)
(§1.4, the Neverlose finding).
