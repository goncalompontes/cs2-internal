# Paired analog steering for straight gain — 2026-10-22

## Runtime evidence

The prestate build's actual process-tagged log reported sv_quantize_movement_input=false, sv_airaccelerate=100, sv_air_max_wishspeed=30, sv_staminarecoveryrate=60, valid physics binding, a retained pre-prediction state and no prestate misses. The user reported good gain at 99% strength/30 subticks but sideways drift. That server mode supports continuous analog impulses; the previous adapter still used the quantized yaw-steering path.

## Controller change

When live physics binding is valid and quantization is off, the adapter now selects a paired analog planner. The quantized/unknown-physics path retains its existing yaw fallback.

Each pair applies an accelerating wish, then solves the second wish so lateral velocity relative to the requested travel heading returns to zero when physically reachable. The solver uses the actual min(available air acceleration, capped wish-speed deficit) rule; it does not simply negate the previous button or wish. For unequal timestamp intervals, the first impulse's analog magnitude is limited to what the second interval can cancel. The second direction is solved in the forward hemisphere. Large initial lateral velocity is reduced within the available acceleration budget; instantaneous cancellation is not promised when that budget is insufficient.

Pair sides balance the modeled lateral displacement as well as velocity, instead of choosing an entire gain side for a command and correcting it on a later command. Displacement uses the model's post-acceleration velocity times interval; this is an illustrative movement model, not a direct measurement of installed-engine collision integration.

In this path strength scales the actual analog acceleration budget. It does not interpolate the ideal wish yaw toward camera-forward, which can eliminate available gain near the air cap. The encoder rotates absolute world wishes into the actual command frame and emits analog deltas against the frozen movement-service impulse baseline. Mouse timestamps share tracked additive analog fields and retain their original data across rebuilds/off transitions. An end-of-command analog restoration returns to the player's manual axes. No synthetic yaw is written by paired analog steering, including its restoration entry.

Silent shot correction rotates reconstructed absolute analog states before rebuilding wire deltas. It does not divide every accelerating impulse by the manual base diagonal's scale. Physical direction buttons remain physical, preventing repeated command builds from mistaking shot-frame correction for a new WASD request. Ownership snapshots are refreshed after correction.

## Validation

Core tests cover strengths .99/1, 16/30 slices, air acceleration 12/100, friction .25/1, speeds 300/700, headings 0/45/179/-179 and zero/partial prefixes. They require positive forward gain, terminal lateral speed below .002 and bounded modeled path bias. A repeated 60-command run verifies gain without accumulated sideways motion. These are synthetic checks, not live gain promises.

Native tests decode the actual paired command at the user's .99/30/air_accel100 settings and require positive forward gain, sideways velocity below .003, capacity <=32, no synthetic yaw fields, restoration of native manual axes, stable duplicate rebuilding, preserved silent aim, and preserved diagonal silent-shot acceleration and direction intent.

The new status/HUD fields identify paired analog versus yaw/native mode and the modeled terminal lateral speed. Both remain distinct from measured client movement velocity. A fresh capture can verify that the user's live server selects paired analog and assess actual travel. No live result from this new controller has been collected yet. Collisions, changing server physics, new manual directions and insufficient correction time prevent a guarantee of a perfectly straight physical path in every case.
