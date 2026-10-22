# Rage command lifecycle changes — 2026-10-17

This package changes the native Windows ragebot pipeline. Existing heatmap selection, dump-derived stopping calculations, penetration backend and camera features remain. This is not full parity with every optional Velocity feature.

## Implemented

- A committed automatic shot sets attack in all three legacy button words. Busy/reloading/empty commands release automatic attack and invalidate its history index. Duplicate processing reuses the command decision and does not count another request.
- A live shot gate reads ammo, reload, next-primary-attack tick, client tick and controller tick base. It enforces a two-tick minimum request gap, isolates pawn/weapon identities, and recovers on a clock reset. Synthetic shot requests are reported separately from observed ammunition decreases.
- Spread/inaccuracy and weapon-specific spread samples are captured on the command thread. The engine spread calculator receives the weapon item definition, pellet count and recoil index. Its left-coordinate output is converted to the heatmap basis. There are 32 sampled directions per acceptance check; this estimates hitchance and is not exact seeded-shot prediction.
- Native spread getter layout is additionally checked against live m_flSpread and m_weaponMode schema operands. Accuracy scratch touched by the getter is restored. Missing/invalid accuracy never becomes zero spread. Failed binding/schema retries are bounded.
- The command thread captures the local weapon state, shooting origin, target simulation times and copied hitboxes. A target whose simulation timestamp changes during capture is rejected. The background worker performs pure geometry searches on those value copies; it never reads engine memory or calls the engine penetration backend.
- Background proposals retain target/pawn/weapon identity and generation. They are anchored to freshly captured target bones at acceptance time. Head, torso and limb alternatives remain available. Expired/missing worker results use current center proposals through the same acceptance checks, so worker latency does not permanently inhibit shooting.
- The command thread rechecks nominal damage, expected damage and damage probability using current shooting position, current spread, current target geometry and engine world tracing. A blocked nominal ray cannot pass solely because some spread samples would connect. The last successful proposal receives priority, while other candidates are rotated through a bounded work budget (at most six checks; a soft four-millisecond budget between checks).
- The selected current target record's render tick is stamped into shot history with the generated protobuf presence bits. Player timing is preserved unless a valid command history shooting position and its matching player clock were selected together. Angles, timing, attack-history index, button PB and move_crc are finalized together.
- Stop requests and shot commits are separate. A reachable but inaccurate candidate can request braking while automatic attack remains released. Reloading/empty weapons do not request braking; cooldown commands do not start a new stop. Autostop coefficient fields now prefer live schema lookup. Existing ground/air/fractional braking math remains.
- Menu disable/generation changes invalidate in-flight results. Native render snapshots no longer publish or cancel command-thread decisions. Failed command finalization cancels owned automatic attack before retrying serialization.
- Native Ragebot settings now expose Minimum damage and Hit chance. Defaults are 20 damage and 60 percent; damage is capped to the target's remaining health for lethal acceptance. Auto fire remains separate from the master toggle, and no activation key is required.

## Current-engine evidence

The previous Velocity spread getter signature does not match the installed client. Current disassembly identifies the getter at RVA 0x80d490, GetInaccuracy at 0x80c1a0, and the engine spread calculation at 0xd23fe0. The spread callsite is RVA 0xd23d73, with E8 at +8 and its rel32 operand at +9. The getter loads the mode-selected VData spread array; runtime schema operand checks protect those field assumptions.

The offline PE probe validates these signatures alongside the existing command/CRC/view and camera bindings without executing client code. Runtime field resolution and virtual shooting-position behavior still need in-game confirmation.

## Validation

- Host: 17/17 tests.
- Payload: 13/13 tests.
- Legacy overlay: 1/1 test.
- Actual payload loaded and rendered using its own DXGI hook in an agent-created hidden WARP host.
- All native command/camera/weapon-accuracy signatures passed the installed-client PE probe.

The repeated-fire regression models a weapon across sixty commands with cooldown and reload intervals, requires at least ten engine-style press events, verifies duplicate handling, empty-magazine suppression, weapon switching and stop-without-shot behavior. Other tests cover timing presence bits and separate shooter/target clocks, engine sample ABI/coordinate conversion, accuracy-state restoration, unknown spread rejection, immutable worker geometry, target identity changes, moved poses, blocked heads and viable body alternatives.

## Practical limits

These changes are not yet verified inside CS2. They implement coherent current-record timing and final revalidation; they do not add full historical backtracking, a verified latency/unlag-window bridge, extrapolation, manual engine prediction simulation, revolver cocking, doubletap, no-spread seed correction, knife/taser logic, or all nine Velocity weapon-policy groups. Those require their own complete dependency paths and are not advertised as implemented menu features. Current proposals use engine-produced command/weapon state and current-record geometry rather than speculative worker-side engine simulation.

The UI's hitchance is sampled damage probability at the configured minimum damage. It can differ from Velocity's hitbox-intersection-only percentage. A request counter is not proof of a server-confirmed shot. The ammo-decrease counter also includes manual shots.

## Diagnostics

Runtime log: %LOCALAPPDATA%/cs2-internal/cs2-payload.log

- [rage-cycle] live weapon timing and spread schema operands verified
- [rage-cycle] command=... tick=... next=... ammo=... reason=... trigger=... requests=... ammo_decreases=... damage=... chance=...

The planner reason and input trigger gate are separate; the periodic line runs before Input applies that command, so trigger is the last applied gate. The input-status response reports the current shot_block, command_crc_writes/failures and camera_restores. The HUD displays damage, sampled probability and a rejection reason.

Installed client SHA256: `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`

## Post-build regression correction

The previous package tested CUserCmd+0x94 == 2 after calling the original CreateMove builder and returned early. Current client disassembly at RVA 0xb661f6 writes 2 to that field while completing an ordinary command. Consequently the coordinator, aim application and move_crc finalization never ran. The latest user log has input-hook installation but no subsequent rage-cycle/finalization markers, consistent with that early exit.

The incorrect post-build check has been removed. Duplicate shot protection remains in ShotGate. The post-builder coordinator/application/finalization path is shared with a regression fixture containing a completed state-2 command. The test first failed against the old condition, then passes after removal, asserting coordinator invocation, attack commit and final serialization. This is a correction to the previous implementation; the earlier synthetic tests did not cover this lifecycle boundary.

## Spread/schema regression correction

The post-build-fix user log confirms command finalization and repeated entry into LiveRage, but reports command=0 / reason=weapon data and no successful schema-operand marker. Inspection found that the initially bound getter at RVA 0x80c890 loads VData+0x748. The installed PE's CCSWeaponBaseVData schema declares m_flSpread at +0x750. That mismatch blocks sample capture before targeting.

The actual mode-selected spread getter at RVA 0x80d490 loads +0x750. Its mode operand is at function+3; its spread operand is at function+0x1d. The runtime now binds that getter and retains schema validation. The data-only PE probe resolves static class/field descriptors for every required weapon/target input and checks the getter operands against m_flSpread / m_weaponMode. It also requires the neighboring +0x748 field to be rejected. This closes the gap in the earlier signature-only test, which proved a function could be found but did not prove it read the intended field.

Sampling diagnostics now identify missing schema names, getter/field mismatch, field/type/pellet reads, weapon-service shooting position, or engine sampling/accuracy restoration. Command identity is retained in diagnostics even when schema resolution fails. The corrected package has passed host/payload tests, the expanded PE/schema probe and actual payload rendering in the synthetic host; CS2 firing still needs user confirmation.
