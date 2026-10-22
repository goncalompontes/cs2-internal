# Ragebot debug indicators — 2026-10-17

This build adds diagnostics for the current firing failure. It does not claim to fix shooting. The latest user gun logs reach the shooting-position check and stop there; the panel now exposes the individual failing check and raw inputs.

## Use

1. Restart CS2 and run Run Injector.cmd from the new rage-debug package.
2. The Ragebot diagnostics panel is visible by default, even with the menu closed. Insert -> Ragebot -> Ragebot debug indicators toggles it independently of the old Debug HUD window.
3. Equip a gun and enable Ragebot / Auto fire as desired. Read the first red BLOCKED row and the explanation above the table.
4. Press Insert to interact with the panel. Expand Source details / rejection counts, use Freeze debug snapshot to hold the display, or Copy diagnostics to copy the complete text report. Freezing pauses only the displayed snapshot; it does not pause commands or change firing settings.

The panel is movable while the menu is open. It becomes click-through with the menu closed. Height is constrained to the viewport with scrolling when needed.

## Indicators

The table distinguishes PASS, BLOCKED, WAIT and -- (not reached). Stages are command callback, local player, accuracy bindings, schema, weapon fields/type, shooting position, spread, ballistics, target geometry, proposals, damage, probability, record timing, trigger and serialization.

Additional values include:

- Hook state, heartbeat age, snapshot age, callback/unique/duplicate command counts.
- Current weapon name, live/snapshot magazine values, reload, client tick, controller tick base and next-primary tick.
- Weapon type, item definition, pellet count, recoil, spread, inaccuracy and sample count.
- Weapon-service pointer, eye function address, whether it belongs to the client, whether the call succeeded, returned/local eye positions, their distance and history-eye selection.
- Enemy/valid-target counts and immunity/data, age, model and changing-pose rejection counts.
- Candidate count, checks performed, center fallback, worker submissions/completions and last calculation duration.
- Nominal/expected/required damage, sampled damage probability/requirement, world-result validity, selected target and group.
- Planned stop, accepted aim, final Input trigger rejection, shot requests, observed ammo decreases, CRC success/failure and view restoration counts.

Shot requests are not server-confirmed shots. Ammo decreases include manual firing. Serialization can pass while targeting is blocked, proving command finalization is running without implying a shot was accepted.

## Capture consistency and verification

Game-thread diagnostics are published as copied value snapshots using a try-lock. Rendering never calls the engine sampling/trace functions or waits for the aim worker. Worker counters and final Input trigger/serialization counters are read atomically. All early exits publish their stage and reason. Duplicate calls preserve the last detailed unique-command result while advancing heartbeat/duplicate counters.

The shooting-position sampler now distinguishes unreadable service/vtable, a function outside the client, a call exception, and an invalid or excessively distant result. Unsupported weapon types are distinguished from valid gun fields. These are diagnostic changes; the 128-unit eye validation and firing acceptance rules remain.

Tests: host 17/17, payload 14/14, overlay 1/1. The new panel test verifies the copied failing-gate report and closed-menu rendering within the viewport. Source tests verify that a deliberately invalid returned eye produces the precise failed stage and raw values. A hidden software-D3D preview was rendered and visually inspected; its data is explicitly simulated, not captured from CS2. The actual payload also rendered through its own DXGI hook in a hidden WARP host. The installed-client code/schema probe passes.

## Shooting-position binding correction

The expanded user screenshot reports a successful call to client RVA 0x912b80, but an unchanged zero output. Read-only inspection resolved the live CCSPlayer_WeaponServices vtable. Its slot 29 points to an unrelated state update routine. Current engine callers request the position using vtable+0x100 (slot 32); the corresponding wrapper at RVA 0xa7fd00 delegates to the owner's EyePosition virtual and returns the vector pointer.

The sampler now locates the verified wrapper by a unique code signature, finds its entry in the current service vtable, validates the local owner, and reads the returned Vec3 pointer. It no longer calls the reference's stale slot 29. The debug panel and copied report now include the resolved eye slot. No zero-position or validation bypass was added.

The installed-client RTTI/vtable probe resolves slot 32. Unit tests place a trap at slot 29 and the getter at slot 32, verify the trap remains untouched, and test a getter returning a different readable vector pointer. All tests and synthetic rendering pass. This new binding still requires in-game confirmation after restarting and loading the corrected package.
