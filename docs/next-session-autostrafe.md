# Next session: implement a CS2 auto-strafer

Copy-paste this as the opening prompt:

---

Implement a **normal auto-strafer** for CS2 in this repo — a payload movement feature
alongside the bhop.

1. **Research first, with one subagent, in one pass.** Spawn a single `general`
   subagent to research how a working CS2/CS:GO auto-strafer is actually implemented
   at the code level (UnknownCheats auto-strafe / subtick / movement-fix / yaw-delta
   threads, open-source CS2 internals on GitHub, and the Source
   `AirMove`/`AirAccelerate` model). Have it answer, with citations: which channel a
   working auto-strafe writes each tick (`CBaseUserCmdPB.forwardmove`/`leftmove`, the
   `CCSGOInput` raw move axes, a `CSubtickMoveStep` `yaw_delta` and/or the
   `analog_*_delta` sub-steps, the command's own `viewangles`, the
   `input_history` view angles); the **magnitude** it writes (unit `{0,±1}` vs
   `cl_forwardspeed` 450) and how it deals with `sv_quantize_movement_input`; the
   side-selection formula for "follow the camera"; the sub-tick recipe (`when`
   values, step counts); and the common failure modes. Require a "what a working
   auto-strafe writes per tick" section, a channel comparison table, and a Sources
   list. Do **not** modify files in that pass.

2. **Then do the RE and implement it.** Using the research, re-derive what is needed
   on the **live** build and implement the feature. Start from
   `docs/re/strafing.md` (engine model, `AirMove`/`AirAccelerate`, the `SetupMove`
   analog sanitizer, the silent sub-tick yaw channel) and
   `docs/re/research-strafe.md`; the REA workflow is `docs/re/README.md`.
   Wire it in like the bhop: overlay menu `Movement` tab (`wire::Settings`, ADR-0021)
   + the control channel, and add an `autostrafe` scenario.

Known landmines (already hit — don't repeat):

- The engine's own command analog is a **unit** value (`1.0`); `SetupMove` drops any
  axis with `|x| ≥ 10`. Writing `450` moves nothing.
- A "your input wins (hold A/D)" gate must **not** read a button state the strafe
  itself writes — it latches (the strafe thinks you are holding A/D and stands down).
- The **overlay's settings block owns** the feature: a control-side enable is reverted
  on the next frame. Put the toggle in the menu and test through it.
- A write to the command/raw analog **after** the engine consumed the input does
  nothing; check the write ordering around `create_move_input` (slot 5).
- Measured on the live build: a perpendicular **unit** analog at ~250 u/s gains only
  **≈ +1.8 u/s per 0.6 s** (`AirMove` uses `wishspeed = min(|wish|, m_flMaxSpeed)`,
  and the unit is `1.0`). The analog **magnitude** and/or the **sub-tick** channel is
  the likely lever — prove the gain with a controlled measurement
  (`scripts/scenario/scenarios/strafe_probe.py` and `movement-probe`) before tuning.

Deliverable: a working, **silent** auto-strafer (airborne only, follows the camera,
respects held A/D), with the measured speed gain, wired into the menu and the control
channel like the bhop.
