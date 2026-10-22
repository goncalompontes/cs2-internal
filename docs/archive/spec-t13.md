# T13 — fix the mirrored side-axis write (a real 180° bug)

**Goal:** the engine's side axis is `m_flCmdLeftMove` — **positive = the player's
LEFT** — and the payload writes the strafe's lateral wish with the **opposite
sign**. The strafe's *heading* is right, but the *value* it writes steers the
player the wrong way in the analog path.

**File ownership (you may ONLY touch these):**

- `payload/control/src/input.cpp`
- `payload/control/include/cs2/control/input.hpp`
- `payload/control/include/cs2/control/strafe_geometry.hpp` (only to use the
  helpers T10 added — **not** to change the convention)
- `payload/control/tests/test_control_strafe.cpp`
- `docs/re/strafe-sign.md`
- `docs/re/strafing.md` (the sign section)
- `docs/research/neverlose-mining.md` (the resolution note)
- `scripts/scenario/control.py` — **only** the `BUTTONS` table (see §3)

**Do NOT touch** anything else.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`docs/re/strafe-sign.md` (T10's verdict, with the addresses), then
`payload/control/src/input.cpp`.

---

## 1. The bug

T10 established, from the engine's own code on the live build (client.dll sha
`659940c1…`):

| # | Fact | RVA |
|---|---|---|
| 1 | the basis function's **second** output is `(-sin yaw, +cos yaw)` — the forward vector rotated **+90°**, i.e. the player's **LEFT** | `0x173cb90` |
| 2 | `AirMove` builds the wish as `forward*movedata[0x2c] + basis*movedata[0x30]` — the side axis multiplies that second output | `0x8e57b0` |
| 3 | the engine forms the lateral axis as `value("left") - value("right")` | `0xb8f6e0` |
| 4 | the field is `m_flCmdLeftMove` (there is no `m_flCmdSideMove`); the proto field is `leftmove` | — |

So `A` → side axis `+1` → the player's LEFT; `D` → `-1` → RIGHT.

**Our repo's old model was inverted in both halves** — it called the engine's
basis "right" (it is its negation) *and* called a positive side axis "right" (it
is left). **The two negations cancelled**, so the A/D *goal headings* were right
(they are world facts, invariant under either encoding) — and the strafe's
*heading* is correct today.

**But the analog write is not a heading — it is a value.** `input.cpp` computes
the camera-frame analog as

```cpp
const float rel = rot * k_pi / 180.0F;
const float forward = std::cos(rel);
const float left = -std::sin(rel);
```

and writes `left` into the command's **leftmove** field. The engine multiplies
that field by its **left** basis vector, so the payload must write
`+sin(rel)` where it writes `-sin(rel)`. At `yaw = 0` and `rel = +90°` the
payload writes `-1` → the engine moves the player **right** — while the wish was
to the left. **The `subtick = 0` (the default) analog path steers the wrong
way.** The sub-tick `yaw_delta` path is unaffected (it writes
`forwardmove = 1, leftmove = 0` and rotates the basis).

T10 left the fix here because this file was owned by another agent at the time.

## 2. The fix

T10 added the helpers to `payload/control/include/cs2/control/strafe_geometry.hpp`
(on the `feat/t10-strafe-sign` branch, **already merged**):

- `cs2::control::k_engine_side_axis_positive_is_left` — the one constant that
  records the convention (with the derivation in its comment);
- `cs2::control::engine_view_basis(yaw)` — the engine's second basis vector;
- `cs2::control::engine_side_axis(lateral)` — a rightward lateral quantity → the
  engine's side axis;
- `cs2::control::engine_side_axis_for_wish(rot)` — the exact value to write for
  a wish offset of `rot` degrees from the view.

**Route the write through those helpers** — do **not** re-derive the sign
inline, and do **not** flip the constant (the constant is correct). The write
becomes:

```cpp
const float rel = rot * k_pi / 180.0F;
const float forward = std::cos(rel);
const float left = cs2::control::engine_side_axis_for_wish(rot);
```

and the **button bits** that go with it must match: the value that is positive
means the player's LEFT, so the `IN_MOVELEFT` bit goes with a positive value
and `IN_MOVERIGHT` with a negative one. Check the current mapping in
`write_strafe_wish` and correct it to agree with the value.

**Everything else stays.** The goal headings, the dead zone, the hysteresis, the
legacy mode, the `theta*` formula, the per-slice simulation, the encoding and the
step ordering are all validated and must not change.

## 3. The button names — a second, related question

T10 noticed a **side observation** that needs checking:

> `moveleft` / `moveright` appear **nowhere** in this build; the engine's action
> for the strafe-left is named `left` (and the look is `turnleft`), so the
> repo's `+moveleft` console command and its `"left" → IN_LEFT` mapping are
> probably wrong for CS2.

So: the repo's `BUTTONS` table in `scripts/scenario/control.py` maps
`"a" → "moveleft"` and `"d" → "moveright"`, and the payload's `k_named_bits`
(`payload/control/src/input.cpp`) accepts the names `moveleft` / `moveright`.
If the engine's console commands are `+left` / `+right` (not `+moveleft` /
`+moveright`), then:

- the **console** button path (`exec "+moveleft"`) would be an **unknown
  command**, and the player's A/D would never be registered by the scenario
  runner;
- the payload's own named-button table would accept a name the engine does not
  know.

**Check it.** The engine's action names are in client.dll — the strings `left`,
`right`, `turnleft`, `turnright` (T10 gives the addresses of the `left`/`right`
action name strings: `0x1c60548` / `0x1c60550`). The **button** commands are a
different set (`+forward`, `+back`, `+jump`, `+duck`, `+use`, `+attack`,
`+moveleft`/`+moveright` in Source 1). Confirm from the build which button
command names CS2 actually has, and **fix the mapping** so that holding A
through the scenario runner's `press("a")` really moves the player left.

Use the REA MCP server (client.dll is warm) if you need to confirm the names.
If the REA session is busy, fall back to the repo's notes and say what you
could not verify.

**Also check the payload's own `k_named_bits`** (`input.cpp`): the names it
accepts for the button mask must be the ones the engine's `IN_*` bits correspond
to. The payload's table is its **own** name→bit map, so it is fine as long as the
*bit* is right; but a name that does not exist in the engine (for the **console**
path) is wrong.

**Deliverable for this part:** a table of the engine's real strafe button
commands, and the corrected mapping in `scripts/scenario/control.py` and (if
needed) in `input.cpp`'s named-bit table. Add a **host test** that pins the
mapping so it cannot drift again.

## 4. The tests

- A **host test** that the wish's lateral value has the engine's sign: for a
  wish offset of `+90°` (to the player's left), the value written to the
  command's side axis must be **positive**, and the button bit must be
  `IN_MOVELEFT`.
- The existing `control-strafe` tests must stay green — **all of them**, including
  the sign-convention pins T10 added (`test_side_axis_convention_is_pinned`,
  `test_the_goal_matches_the_engine_wish`,
  `test_the_goal_headings_are_the_players_left_and_right`).
- **Prove the new test can fail**: invert the sign on purpose, watch it fail,
  revert.

## 5. Verification

- `meson compile -C build && meson test -C build` → green (27/27 at baseline).
- `meson compile -C build-payload` → clean.
- `python -m unittest discover -s scripts\scenario\tests -t scripts` → green
  (142 at baseline).
- You cannot launch CS2 (it needs `schtasks /run /tn CS2Launch` and a GPU in the
  interactive session) — say clearly what is unverified.

## 6. Report

- The change you made to the write, and the exact value it now writes for a
  `+90°` and a `-90°` wish.
- The button-bit mapping, before and after.
- The engine's real strafe button command names, with the evidence.
- The live check the operator should run to confirm it (the one in
  `docs/re/strafe-sign.md`).
- What you could not verify without a live game.
