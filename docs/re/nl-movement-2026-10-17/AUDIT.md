# NL movement port — 2026-10-17

The IDA database is cs2_212C3300000.bin.i64 (image base 0). This analysis uses binary code/assembly. No database annotations or functions were modified. The raw exports preserve decompiler limitations: AVX-heavy prototypes and condition variables are unreliable, so steering decisions were checked against assembly.

## Verified source map

| Address | Recovered behavior |
| --- | --- |
| 0x4CF450–0x4CF70F | Air steering solver: horizontal speed/atan2, optimum and turning asin angles, configurable interpolation, 170-degree turn-side special case, internal yaw plus digital side wish |
| 0xD8ED10 / 0xD8ED30 | asinf dispatch/implementation; identified by ±1 returning ±pi/2 and its literal asinf error path |
| 0x4CFD70–0x4D18A1 | Subtick driver: remaining-fraction budget bounded by floor(64*(1-start)), grid-rounded fractions, per-slice steering and velocity updates, 72-byte internal events |
| 0x4CFDcf–0x4CFE47 | Remaining interval, count and first fraction construction |
| 0x4CFE7C / 0x4CFEBB / 0x4CFFCA | Prediction-state update, steering evaluation, capped horizontal acceleration |
| 0x4D14B2–0x4D16A6 | Subsequent slice loop, restoring the original goal heading before recalculating steering |
| 0x4C4880–0x4C49B3 | Remove old jump records while retaining other event records |
| 0x4C49B7–0x4C4B46 | Airborne jump handling and private prediction search; not a self-contained portable function |
| 0x4C4B4B–0x4C4C26 | Ground pulse branch: clear held jump, adjust jump transition/scroll state, choose grid-rounded fraction from 0..0.2, append pressed then released at the same fraction |
| 0x4CBEB0 / 0x4CBF10 | Append 72-byte button events; confirms time, button and pressed fields and append order |
| 0x338CC0 | Private simulation wrapper used by airborne jump search; mapped engine ABI/state restoration is incomplete |

## Recovered steering equations

For horizontal speed s, per-slice acceleration budget A, capped wish speed W and wrapped goal/velocity heading difference d:

```
A = air_acceleration * wish_speed * surface_friction * slice_seconds
W = min(air_cap, wish_speed)
optimal = degrees(asin(max(W - A, 0) / s))
turning = degrees(asin(-min(A, 2*W) / (2*s)))
boundary = 2 * max(abs(optimal), abs(turning))
if abs(d) > boundary:
    optimal = turning + blend * (optimal - turning)
side = +1 if abs(d) > 170 else (+1 if d > 0 else -1)
internal_yaw = velocity_yaw - optimal * side
internal_forward = 0
internal_side = side
```

The source returns zero for an out-of-domain asin ratio in this steering calculation. At speed below 1 it requests forward movement along the current goal. After each slice, capped acceleration is applied to update the velocity used by the following slice. A whole-tick implementation therefore does not reproduce the slice sequence.

Constants read from the dump include radians-to-degrees 57.295776, 170 degrees, percent divisor 100, tick interval 0.015625 and fraction count 64. The `(x + 131072) - 131072` float construction rounds fractions to a 1/64 grid.

## Current native adapter

The recovered internal event layout is not the current engine protobuf. After user testing reported restricted airborne direction control, the native adapter no longer replaces base movement axes, direction-button words or physical analog/button events. It emits only owned movement-yaw events, using the actual input basis at each slice. The nearest-eight-direction adapter from the preceding package is retired. Input is read again on each rebuilt command rather than cached from a prior output.

Manual A/D/S input has priority and bypasses the gain pattern; owned steering yaw is cleared immediately, including a key change within the same command. W retains the forward air-strafe assist. Grounded commands, jump launches, speeds below 80 units/s and zero travel input receive no air steering. This is an explicit forward-assist/manual-control behavior choice, not a claim of fully directional NL feature parity. The menu explains the handoff.

For assisted commands, the native driver keeps one turn side for the command and recomputes the recovered acceleration angle/velocity each slice. Original analog and button messages remain byte-for-byte intact. Previously owned yaw is identified by command number, object pointer and value, then replaced rather than accumulated. Pointer ownership, allocation rollback, the 32-event budget and stable timestamp sorting remain enforced.

Silent aim shifts the first owned movement-yaw event when it changes base command yaw, preserving the physical wish without turning a digital wish into fractional base input. The analog-only reconciliation case rebases the initial movement impulses as well. Rage autostop retires owned strafe yaw and retains movement priority.

When a jump is planned from a grounded state, no strafe yaw is injected. Jump/attack events and native movement are preserved; the existing jump-only impulse reconciliation is used when there is no physical analog timeline. Ladder/noclip/dead/missing-state cases are excluded.

All player/movement fields are resolved through live schema: movement services, actual move type, flags, absolute velocity, buttons, max speed, surface friction, previous forward/left impulses, and ModernJump landing tick/fraction. The installed-client data-only probe verifies the required static schema descriptors. The old native bhop used the stale literal movement-services offset 0x1330; the new native path does not use it.

## Bhop behavior and explicit recovery boundary

The dump contains a ground press/release pair and randomized 0..0.2 timing, but its private event representation is not interchangeable with the current engine protobuf. Following user reports of sloppy hops, the native adapter now emits one timed press with all three command jump bits cleared. The following airborne command releases jump. An additional scroll bit would create an untimed second edge at command start. Physical command input, rather than simulated held state from the last processed command, controls whether the user is requesting bhop.

The native bridge now reads schema-resolved controller tick base, applies a stable +1 build phase per command, and compares that clock with ModernJump. The current movement command records its slices as {build_tick - 1, fraction}; only that contact age is accepted for timed airborne assistance. A non-grid contact advances to the first following 1/64 boundary. Without a usable current-command record, an already grounded player jumps at fraction zero, including contacts near the tick end. Random ground delay and replaying older fractions are removed. Mode 3 denotes immediate ground fallback and mode 4 current-command contact timing.

The dump's complete airborne search uses private prediction state/callbacks that are not fully recovered. This build does not call an unverified simulation wrapper or claim guaranteed pre-contact prediction. It avoids another contact press during ascent. Source ground-pulse recovery remains valid evidence; the revised native jump encoding is an implementation adaptation, not a newly recovered NL branch.

This is a binary-derived steering/pulse port with an explicit native landing-record bridge, not a claim of complete bit-for-bit NL prediction behavior. Horizontal look-ahead uses the recovered air-acceleration model; it does not reproduce the dump's entire collision/prediction environment. Air acceleration/cap use the existing profile defaults (12/30), while max speed, friction and velocity are live values. Non-default server physics may require profile calibration.

## Menu and diagnostics

Movement tab: Auto-strafe (NL subtick), Bunnyhop (NL pulses), Strafe subticks (default 16, maximum 30 before the shared budget), and Speed gain (turning/optimal-angle blend, default 100%). The controls are adapter labels; encrypted NL UI labels were not used to identify the functions.

Hold jump to use bhop. The Movement tab shows current state, emitted event count, speed, hop mode/fraction and write counts. Copy diagnostics also includes movement state. The displayed event count includes analog/launch overhead, so it can exceed the selected steering-slice count.

## Validation

Host 18/18, payload 15/15, overlay 1/1. Tests cover recovered numerical fixtures, full-tick versus per-slice acceleration, normalized grid timing, turn-side behavior, held-input/fresh-contact/stale/end-boundary hop decisions, actual Input adapter writes, repeated commands, active/spare pointer uniqueness, preserved attack/yaw events, full event budgets, rollback, jump release, launch steering, ladder exclusion and arbitrary silent-aim yaw.

The installed-client code/schema probe passes, including the new movement fields. The actual DLL renders through its native hook in a hidden synthetic WARP host. The first package was tested by the user and reported weak steering/sloppy bhop. This corrected package still needs in-game verification; no agent injection into CS2 was performed.


## Native correction regression checks

The actual adapter test interprets every emitted slice using accumulated analog impulses, native diagonal normalization and accumulated yaw, checking the resulting world wish against the analytic plan. It checks bounded per-event and total command yaw travel, camera-left intent with zero base axes, movement-key releases, one ground press without a scroll duplicate, and stale simulated jump state after physical release. Existing silent-aim, launch, allocation rollback and command-budget tests remain green. Host 18/18, payload 15/15 and overlay 1/1 checks pass after the correction, along with installed-client binding validation and the actual payload WARP smoke test.


## Air-control regression correction

The user clarified that grounded movement was normal and the failure occurred in the air with Auto-strafe enabled. The superseded test suite checked mathematical reconstruction of synthetic output without requiring native WASD preservation. The replacement tests require every cardinal/diagonal direction's base axes and direction mask to survive, no air steering during jump launch/low speed/no input, manual air-control priority, immediate retirement of owned yaw on a same-command takeover, and byte-for-byte preservation of physical mixed analog/button messages. The movement test passes with the new policy; host 18/18 and payload 15/15 also pass. Render smoke validation uses only an agent-owned synthetic WARP process. In-game verification remains pending.


## Counter-component investigation and revised native adapter

The user rejected the manual A/D/S bypass as a diagnosis and suggested excessive counter-movement. The recovered formula was checked separately from native command emission: its full speed-gain blend has a nonnegative velocity projection; lower blends can use a negative projection down to -min(A,2W)/(2*speed), while the separate ragebot air-autostop can request full reverse. This does not establish which mechanism caused the observed in-game failure.

The current native adapter restores WASD assistance and sets an explicit native-only allow_counter=false policy. Negative turning angles are clamped to zero before constructing the world wish, so ordinary strafe gain never deliberately opposes simulated velocity. The source helper retains its recovered behavior by default, including the original negative-projection turning fixture. No change was made to intentional ragebot braking strength.

The earlier yaw-only revision also assumed base movement axes were the starting impulses when no physical analog event existed. That contradicts the known native subtick path, which begins with previous service impulses once a subtick timeline exists. The adapter now reconciles previous impulses to current input once in its first owned yaw/analog event when no physical analog timeline is present. Negative deltas can be necessary input corrections; they are not blindly reduced or mistaken for opposing wishes. Physical analog/button messages remain intact. Owned reconciliation is replaced on rebuild, retired together with its yaw on travel release, and included in silent-aim rebasing. The earlier test interpreter assumption is corrected accordingly.

The Movement tab and Copy diagnostics now expose the maximum predicted reverse-wish component independently from whether ragebot autostop applied and its path. These are command/planning diagnostics, not server confirmation. New regressions cover minimum-to-maximum blend, 1/3/16/30 slices, speeds 80/150/300/600, nonnegative velocity projection at every slice, and explicit prior-back/current-forward command reconstruction. This is a bounded native correction and a testable hypothesis; in-game confirmation remains required. Full private landing prediction remains unrecovered.
