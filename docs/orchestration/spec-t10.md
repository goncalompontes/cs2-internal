# T10 — settle the strafe's side-axis sign (the `leftmove` convention)

**Goal:** resolve a **180° ambiguity** in the auto-strafe's A/D handling that two
independent sources disagree about, and make the answer impossible to get wrong
silently again.

**Why this matters.** The directional auto-strafe derives its travel heading from
the held W/A/S/D. The A and D headings are computed from the engine's
**view-frame basis** — the same `right(yaw)` vector the engine multiplies the
command's side axis by. Two sources disagree about which way that vector points:

- **This repo** assumes `right(yaw) = (sin yaw, -cos yaw)` and that a
  **positive** side axis is the player's **right**
  (`docs/re/strafing.md` §"AirMove", `docs/control/include/.../strafe_geometry.hpp`).
- **The Neverlose reversal** (`docs/research/neverlose-mining.md`) found that
  Neverlose writes `cmd+0x30 = +1.0` when `IN_MOVELEFT` (A) is set — i.e. it
  treats a **positive** side axis as **left**.

If the repo's assumption is wrong, the strafe curves the **wrong way** for A and
for D: the feature would work with W (the forward/back case is symmetric) and be
exactly backwards on the lateral case. The repo's own research already flagged it
as open (`docs/re/research-strafe.md` §9.3).

**File ownership (you may ONLY touch these):**

- `docs/re/strafe-sign.md` *(new)* — the analysis and the live check
- `docs/re/strafing.md` (the sign convention section only)
- `payload/control/include/cs2/control/strafe_geometry.hpp` — **only** to make the
  assumption explicit and switchable, **not** to change the behaviour
- `payload/control/tests/test_control_strafe.cpp` — **only** to pin the assumption
- `docs/research/neverlose-mining.md` — **only** to add a cross-reference

**Do NOT touch** `payload/control/src/input.cpp` or anything else — another
agent (FIX-3) is working in that area. If the fix needs `input.cpp`, say so in
your report instead.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

---

## 1. What the evidence actually says

Read these and work from them, not from memory:

- `docs/re/strafing.md` §"`AirMove` — how the wish is built" — the engine's own
  construction:
  `wish = fwd * movedata[0x2c] + right * movedata[0x30]`, where `fwd` and `right`
  come from the engine's basis function
  `FUN_18173ccd0(movedata + 0x14, &fwd, &right, scratch)`.
- `docs/re/strafing.md` §"The `SetupMove` analog sanitizer" and the
  `m_flCmdSideMove` row — where the side axis comes from.
- `docs/re/research-strafe.md` — the community research; §9.3 item 7 is the open
  question.
- `docs/research/neverlose-mining.md` §1 — the Neverlose finding.
- `payload/control/include/cs2/control/strafe_geometry.hpp` — our current
  assumption, in code.
- `payload/control/src/input.cpp` — how the goal and the side are used.

**The key question:** is the engine's second basis output the player's **right**
or their **left**? Everything else follows from it.

Note the distinction you must keep straight — these are **two different things**
that are easy to conflate:

1. **The basis vector** `FUN_18173ccd0`'s second output, which the engine
   multiplies the side axis by. Our `strafe_geometry.hpp` models this as
   `right(yaw) = (sin yaw, -cos yaw)`.
2. **The command's side-axis field** (`movedata + 0x30`, the `leftmove` /
   `m_flCmdSideMove` pair) — its *name* says "left", and the repo's comment says
   "positive `leftmove` moves right". Those two statements are only consistent if
   the basis vector is the **right** vector.

So: if the basis vector is the engine's **right**, then a positive side axis is
the player's right and the repo is right (and Neverlose is inverted). If the
basis vector is the engine's **left** (the RE's name "right" was the analyst's
assumption), then a positive side axis is the player's left, the repo is
**inverted**, and Neverlose is right.

**Derive the answer from the engine's own code if you can.** The basis function
is at `client.dll + 0x173ccd0` (RVA `0x173ccd0`) on the live build
(sha `659940c1…`). The REA MCP server can decompile it — read the *body* and
see how the second vector is computed from the yaw. A basis built as
`(sin yaw, -cos yaw)` is the forward vector rotated **−90°**; a basis built as
`(-sin yaw, cos yaw)` is **+90°**. Then say which one the engine's
`AirMove` call site and the field's name imply.

**Also look for a second, independent witness in this repo:** the `+forward` /
`+left` console commands, the `+moveleft` / `+moveright` button commands, and the
engine's own `SetupMove` sanitiser. The repo's `BUTTONS` table maps `"a"` →
`moveleft` and `"d"` → `moveright`, and the engine's button commands set the
side axis — so **the engine's own button command for `moveright` tells you the
sign directly**. Find where the engine sets the side axis for `+moveright` /
`+moveleft` and read the sign. That is the cleanest witness and it may already
be in the repo's notes.

## 2. Deliverable: `docs/re/strafe-sign.md`

A short, decisive document:

1. **The verdict** — the basis vector is the player's right, or their left.
2. **The evidence**, with the address or the repo citation for each step:
   - the engine's basis function and how it builds the second vector;
   - the side-axis sign the engine's own `+moveright` / `+moveleft` commands
     produce (the second witness);
   - what the Neverlose finding implies, and whether it is consistent or
     whether Neverlose uses the axis differently (e.g. writing a *diagonal* pair,
     or a different field — check its claim rather than assuming it contradicts
     us).
3. **What it means for our code** — whether
   `strafe_geometry.hpp`'s `right(yaw) = (sin yaw, -cos yaw)` and the A/D goal
   headings are correct, or inverted.
4. **If it cannot be settled statically**, the **exact live check** — one command,
   with the expected output for each answer, so the operator can settle it in a
   minute:
   - what to read (the command's side axis, the raw axis, or the eye-angle drift
     while holding A);
   - where the value is visible (a `state`/`input` field, the payload's log, or a
     `control` command);
   - the two possible results and what each implies.

## 3. The code change: make the assumption explicit

**Do not change the behaviour** — the answer may be "it is already right". But
make the assumption **explicit and switchable**, so that if the live check
contradicts it, the fix is one line and cannot be applied in two places by
mistake:

- In `strafe_geometry.hpp`, introduce a single named constant that captures the
  convention, e.g. `k_side_axis_positive_is_right` (or the basis-vector form),
  with a comment that says exactly what the engine does, where that was
  established, and what to change if the live check says otherwise.
- Route **every** use of the side-axis sign through that one constant, so there
  is exactly **one** place to flip.
- Add a host test that **pins the current convention explicitly** (so a future
  change is a deliberate edit to a named constant, and the test's failure message
  says which direction changed).

Keep the pure module dependency-free (`<algorithm>/<cmath>/<cstdint>` only).

## 4. Verification

- `meson compile -C build && meson test -C build` → green (23/23 at baseline).
- `python -m unittest discover -s scripts\scenario\tests -t scripts` → green (84).
- Prove the new test can fail (flip the constant on purpose, watch it fail,
  revert).

## 5. Report

- The verdict, with the evidence.
- Whether our current assumption is right or inverted.
- The exact live check (if it could not be settled statically) — as a copy-paste
  command with the expected outputs.
- What you changed in the code, and where the one line to flip is.
