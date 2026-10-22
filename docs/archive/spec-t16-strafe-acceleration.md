# T16 — the auto-strafe no longer accelerates: find out why and fix it

**Your worktree:** `C:\Users\gpmpo\cs2-wt\t16` (branch `feat/t16-strafe-acceleration`),
created for you. **Do all your work there.**

**This is the first task of the next session.** It is a two-minute measurement
followed, probably, by a one-line fix.

## The report

The operator reports that **the movement no longer accelerates** — either the
auto-strafer or the bhop. They did not say which, and they did not say when it
broke.

## The strong suspect (a default this wave changed)

`wire::Settings::movement_strafe_subticks` was changed from **0 to 8** in
`d6fc5bc` ("fix(movement): default the auto-strafe to directional and 8
sub-tick slices"), on the reasoning that 8 is the live-validated slice count
(+85.1 median / +104.9 max u/s per hop, `docs/handoff.md`).

But the two values select **different code paths**:

| `subticks` | path | how it steers |
|---|---|---|
| **0** | the **camera-frame analog** | writes the command's move floats + the raw axes |
| **≥ 1** | the **sub-tick `yaw_delta` rotation** | rotates the movement *basis* with N steps |

And the rotation path is **gated**: `docs/re/strafing.md` (§"The two biggest
suspects") records that *"the rotation ran but **did not move the velocity**,
which is what landmine 11 predicts when the `(*vtable+0x150)()` policy is off"*,
and the policy is `sv_subtick_movement_view_angles` — which was **off** on the
listen server used for the earlier measurements. So the default flip may have
moved the strafe from the path that works to the path the server gates off, and
it would then **silently stop accelerating**.

The sign fix (T13) also changed the analog write (`-sin(rel)` → `+sin(rel)`, and
`IN_MOVERIGHT` → `IN_MOVELEFT`). That one is **correct** — confirmed live:
holding A moved the player +20.1 units along the engine's LEFT vector
(`docs/re/strafe-sign.md` §5.0) — and an air-strafe gains speed in either
direction, so it should not be the cause. **Check it anyway**, but the `subticks`
default is the stronger suspect.

## Do this

1. **Measure first, before changing anything.** With a bot match up, run the
   built-in scenario from your worktree:

   ```powershell
   python scripts/run-scenario.py autostrafe --set watch=6
   ```

   It observes the menu-owned strafe and reports the gain and the eye drift. Run
   it with `movement_strafe_subticks` at **0** and at **8** and compare. Also
   try `sv_subtick_movement_view_angles 1` on the server and re-measure.

   The MCP tools are available too: `cs2_run_scenario({name: "autostrafe",
   args: {watch: 6}})`, `cs2_settings_set`, `cs2_state`.

   **Record the numbers.** The gain is the thing that decides the fix, and the
   repo says a "before/after" is not comparable across a game restart — so
   measure the two settings **in the same session**.

2. **Establish whether it is the strafe or the bhop.** The `bhop` scenario
   (`python scripts/run-scenario.py bhop`) measures the bunnyhop separately.

3. **Then fix it.** Most likely, one of:
   - default `movement_strafe_subticks` back to **0** (in
     `overlay/include/cs2/overlay/wire.hpp` **and** the matching default in
     `payload/features/include/cs2/features/esp.hpp` — the two must agree, the
     host test `features-config` asserts `to_wire_settings(Config{}) ==
     default_settings()` byte for byte);
   - or make the strafe **fall back** to the analog path when the rotation has
     demonstrably not steered the velocity (the feature already reports
     `strafe_writes`, `strafe_rot` and `strafe_wish` — the same reasoning as
     the chams' fail-closed rule: **do not act on a path the engine gates off**).

   **Any change to a default requires recomputing the golden settings checksum**
   (`wire::k_default_settings_checksum`). The `static_assert` fires in all three
   builds and prints the new value in its message. Say so in the commit.

4. **Consider making the choice explicit.** The strafe has two encodings and
   only one of them is known to gain speed on a listen server. Whatever you
   change, the *operator should be able to see which encoding is in force* — the
   feature already reports `strafe_subticks`; make sure the report says whether
   the rotation actually steered.

## Rules

- Only touch the files you need; the movement code is
  `payload/control/{include/cs2/control,src}/{input,service}.*` and the settings
  are `overlay/include/cs2/overlay/wire.hpp` +
  `payload/features/include/cs2/features/esp.hpp`.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`; never
  amend or rebase.
- `docs/style-guide.md` is normative.
- Prove any new test can fail.
- You cannot launch CS2 from the agent shell: the game runs in the interactive
  session, so use `schtasks /run /tn CS2Launch` (or the `cs2_launch` MCP tool)
  and inject with `build-injector\injector.exe cs2.exe build-payload\payload.dll
  --load-missing` (or the `cs2_inject` MCP tool). **Inject once** — a second
  injection into a running game double-hooks it.

## Report

The measurements (both settings, both scenarios, the numbers), the diagnosis, the
fix, and the checksum recomputation if a default moved.
