# Directional steering corrections from Velocity — 2026-10-22
Update: [Straight-input gain correction](strafe-gain-2026-10-22.md) supersedes the near-heading per-slice side alternation described below; turn side is now held through each command so bounded yaw steps reach an accelerating angle.

Update: [Silent steering and active-mouse gain](silent-strafe-2026-10-22.md) supersedes the full mouse-prefix delay and raw-payload ownership details: mouse events now share timed corrections, cache metadata is excluded, and a restoration tail cancels synthetic yaw.

Reference source was inspected at C:/Users/ollim/Desktop/cs2/velocity-cs2/project/core/features/movement/impl/test_strafer.cpp, subtick_strafer.cpp, airstrafe.cpp, bunnyhop.cpp, and core/hooks/impl/cheat.cpp. No Lua references were used. The reference checkout was not modified.

## Corrected discrepancies

- Native steering now uses Velocity-style ideal-angle selection: project (cap - available acceleration), or cap/2 when one slice can reach the cap, then acos of that projection divided by effective speed. Near the requested heading the side alternates; outside the two-degree band it follows the shortest signed heading error. The previous native adapter held a single turn side through all slices.
- A native-only directional_feedback option separates this behavior from the recovered dump fixtures. The recovered standalone formula remains available and its tests still run.
- Assist starts at five units/second rather than being suppressed below eighty. Under fifteen, desired wish follows travel intent. Existing ground/ladder/noclip gates remain.
- Last-pressed directional keys take priority over a held opposite key. Releasing the newer key restores the still-held older key immediately. Previous direct subtraction made opposite keys cancel assistance.
- Real-camera yaw selects travel intent using the verified native view getter. Existing command-frame impulses are retained for encoding; already corrected command axes are not replaced by real-frame WASD values. The rotated-frame regression confirms decoded world steering remains equivalent.
- Physical mouse/button/analog events remain intact. Synthetic turns start on the next 1/64 boundary after the physical prefix. A prefix at the end of the command leaves native input in control. The model advances velocity through that prefix and simulates only the remaining interval. If analog events are introduced after a button/yaw-only prefix, a zero-time impulse reconciliation preserves the initial native direction.
- Subtick ownership checks compare the entire encoded payload, not only pointer and yaw. An engine-recycled zero-yaw entry containing new physical analog input is preserved on command rebuilds. Silent-aim correction refreshes the ownership record when it changes an owned yaw.
- Landing-record jump mode 4, as well as predicted contact mode 6, suppresses air-gain steering for the landing command. Autostop retains movement ownership. Jump generation/timing itself was not rewritten in this change.
- The former Speed gain slider is labeled Steering strength. Zero removes assisted steering; other values blend requested travel heading with the reference ideal wish. Full strength selects the reference ideal. Opposing wish is labeled as a braking-only diagnostic; zero does not mean assistance is inactive.

## Validation

Tests cover both turn directions, phase alternation, sixty aligned model commands without accumulated one-sided drift, low-speed activation, retained WASD/diagonals, opposite-key transitions, zero strength, crouch scaling, prefix timing and end-of-command fallback, repeated-command cleanup, recycled entry preservation, silent shot yaw, fake command-frame equivalence, allocation rollback, event capacity, launch/landing behavior and ladder exclusion. World wishes are decoded from actual encoded fixture steps. Host and native payload test suites are run before packaging.

## Scope and remaining verification

This is a source-guided adaptation, not a claim of exact Velocity or Neverlose behavior. Our adapter preserves physical events instead of copying Velocity's whole-array desubtick clear. It integrates the emitted bounded wish rather than an unclamped ideal and retains the existing non-braking guard. Model physics still use the project's air-acceleration setting, default air cap 30 and stamina recovery 60; live convar discovery and non-default server physics are not validated by these tests. The reference's complete prediction subsystem was not transplanted.

No new in-game movement capture has been taken for this build. Test gains and heading stability are modeled fixture results, not server measurements. Use the Movement panel's State, Steps, Speed and Strafe writes for live diagnosis. The separate stationary four-command choke remains included and can be disabled for an isolated steering comparison.
