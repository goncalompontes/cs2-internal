# Recovery and stationary command serialization choke — 2026-10-22

Implemented as an experimental project feature. The claim that Neverlose uses exactly this technique is user-provided; this implementation is not a recovered proprietary algorithm.

## Engine evidence

Installed engine2.dll SHA-256: 6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21.

The command stage RVA 0x64120 calls builder 0x642d0 at 0x6412d and sender 0x64ae0 at 0x64160. The builder is called with the network client in RCX; the caller does not use its return value. The sender takes the object and a double in XMM1, not an old Source 1 send_packet reference.

The builder resets its scratch buffer, reads the current command through client-interface slot 0x90, derives a retained range using network-client +0x2c357c at 0x644b5, clamps the range, serializes it, then writes the range start/end at +0x2ccf90/+0x2ccf94 (0x64706/0x6470d). It also builds a tick message after the command loop. The sender transmits the built buffer via channel virtual slot 0x158 at 0x64cdf, whose profile label is ClientSendMove.

The hook defers the builder. It leaves command generation, prediction, the surrounding stage, and the sender executing. Thus it delays command/tick-message serialization, not every network packet. On release the normal engine rebuilds from retained command history; the actual emitted range and later server execution must be checked live. Existing history limits and channel conditions can affect this. No direct tickbase, accuracy-memory, sequence or acknowledgement writes are made.

Binding requires unique builder/stage/sender signatures, verified relative calls, the exact profile label, transport slot, retained-range operand and range-store operands. The live object must also have the verified stage in vtable slot 0x288, a channel, and signon state 6.

## Project integration

Enable Ragebot and `Command choke (4, recovery + stationary)` in either menu. Off by default. Shared settings use one reserved byte, preserving Settings size and existing field offsets. Control channel: rage arguments may contain `recovery_choke: true/false`; response includes binding, held count, deferred-command count, release-attempt count, serialized range and server-executed counter.

Sampling happens on the existing rage command callback after native weapon validation. It uses the live schema's C_CSWeaponBase::m_fAccuracyPenalty and C_BaseEntity::m_fFlags, plus a checked read of the networked C_BaseEntity::m_vecVelocity. It does not require a target, damage solve, or auto fire. Ragebot must be enabled to provide samples. Disabled/dead/missing-source paths invalidate samples; stale samples also stop suppression.

Eligibility requires consecutive unique command numbers, the same pawn/weapon/ammo, grounded state, valid measured horizontal speed, and either a measured penalty decrease greater than 1e-6 or stationary speed (at most 1.0 unit/second). The first observation is a normal baseline. The policy defers four consecutive commands, permits the fifth, and repeats while either recovery continues or the player remains stationary on the ground, including at a flat penalty. Callback repetition cannot consume the budget. Attack is reread from the finalized command at the builder hook so automatic fire releases immediately. Secondary attack also releases. Discontinuities, weapon/ammo changes, airborne state, recovery plateaus while moving faster than the stationary threshold, invalid samples, disabled settings and 100 ms freshness/hold limits pass through. Releasing permits the normal builder; it does not guarantee transport success.

HUD and copied diagnostics show enabled/bound/held status, deferred commands, release attempts, penalty, serialized range and server-reported executed command. A missing binding is reported instead of substituting a guessed address. Hook runtime/trampoline storage remains resident for callbacks in flight; disable restores normal processing.

## Validation and limits

Policy tests exercise repeated four-command recovery cycles and twenty stationary cycles at a flat penalty, stationary threshold transitions, invalid velocity, duplicate callbacks, final attack changes, disabling, stale state, identities, ammo, airborne state, plateaus and command gaps. The binding probe maps the actual engine PE into a private buffer, resolves the send path, uses MinHook to patch/restore its actual builder prologue, and verifies altered range code is rejected. It never executes engine code.

No live measurement of this new choke feature has been taken. Neither deferred builder calls nor red receive-margin telemetry establish faster server-authoritative accuracy recovery. Compare actual serialized range, server-executed follow-up, weapon penalty and shot results before drawing that conclusion. This feature does not request additional move-clock iterations.
