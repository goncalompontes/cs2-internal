# Rage autostop port

The rage input path now uses the recovered ground/air stop solver. This is an implementation of the recovered policy with an explicit physics adapter; it is not a claim that every source-dump runtime field has been identified or that the port has been validated in a live match.

## Code and ownership

- `payload/aim/include/cs2/aim/autostop.hpp`, `payload/aim/src/autostop.cpp`: allocation-free solver, independent of game memory.
- `payload/control/src/autostop_input.cpp`: live state reads, per-command cache, button handling and protobuf event encoding.
- `Input::apply`: stop runs after manual movement/command-angle overrides and before silent aim. Auto-strafe yields while rage stopping owns the current ground/air case. Rage silent angles and attack remain independent.
- `Service::on_snapshot`: publishes the active weapon VData and scoped state alongside a successful rage solve. No stop is applied without an active rage solve.

Autostop, airborne stopping and fractional mode default on for rage. Jump cancellation defaults off. They are available through the existing `rage` control command; no overlay wire-layout change was made. Disable stopping with `autostop:false` if you want the prior rage behavior.

## Recovered behavior included

The speed threshold is `(speed_limit * 0.34) * 0.99`. Ground prediction applies friction and acceleration before deciding whether to intervene. Ordinary mode rescales the existing input and checks the predicted result; fallback applies full counter-movement. Fractional mode checks exactly63 times,1/64 through63/64, selects the closest result strictly below threshold, and requires error<1 before producing forward/reverse phases.

Air mode counteracts speed in excess of threshold using the available acceleration budget. The special positive vertical-velocity branch splits the tick at an upward-quantized fraction and computes both movement phases. This is not a general landing predictor. The second interval uses movement factor1, matching the recovered state update. The core retains the optional force-full-air input, though the live adapter cannot identify the original runtime flag and leaves it false.

The conditional ground weapon scale is implemented separately: when enabled, selector0/1 chooses weapon speed (invalid selectors use0). Flagged states preserve multiplier1 if category<=1 or speed*0.52>=110; otherwise multiplier=min(speed/250,1). The dump's scaling-enable/category fields have not been proven to be scoped/zoom state, so automatic binding of those two fields is deliberately absent. Explicit `stop_weapon_scale` inputs enable that branch.

The adapter replaces only analog payloads in existing subtick steps, retaining button and angle payloads in mixed steps. Source absolute phases become an initial base movement plus analog deltas at the switch time. Angle changes, when needed, use separate yaw steps. Active steps are stably sorted, the32-entry budget is enforced after replacement, and inactive allocated pointers remain unique/reusable. Allocation failure before committing leaves the command untouched. Repeated hooks reuse the per-command solve instead of scaling its output again.

Optional jump clearing uses the current request and a17-tick cutoff from the last observed airborne command tick. This adapts the recovered caller policy to this project's observed command stream; it does not import the source's six caller contexts or its16-record history machinery. The source's unbound0x10000 mask clearing is not copied onto a guessed button name in the new command layout.

## Live inputs and calibration gap

This checkout arrived with no `calibration/*.json`, generated `payload/sdk/offsets.hpp` or generated client schema header. The existing project needs those generated artifacts for a functional payload build.

The adapter reuses the project's pawn flags/velocity readers. When schema fields exist, it also reads movement services, max speed, surface friction, stamina, duck amount and weapon VData max-speed values. These are adapter mappings to independently named engine fields, not verified names for every source-dump offset. Missing fields retain the following explicit estimated profile:

| Input | Default |
|---|---:|
| max speed |250|
| ground friction |5.2|
| ground acceleration |5.5|
| air acceleration |12|
| stop speed |80|
| vertical coefficient |800|
| movement factor |1|
| stamina recovery |0 (source coefficient unbound)|

Weapon max-speed slots use this project's scoped state for the live speed limit when available; conditional scaling remains separately configurable. Live schema values override profile fallback values. The `stop_profile` status string explicitly reports estimated physics. Core input validation rejects invalid states; zero positive-excess acceleration budget saturates to reverse without writing NaNs. A ground event that reverses a nonzero third movement axis falls back to ordinary mode because this project's protobuf has no third-axis delta.

No shot-delay gate was added. This changes stopping movement, not the rage fire decision. Analog-only mode may be affected by the server's input quantization; fractional mode is enabled by default. No new camera writes, injection, game launch or live process mutation were used to validate this change.

## Control examples

Existing protocol:

```json
{"id":1,"cmd":"rage","args":{"autostop":true,"air_stop":true,"fractional_stop":true,"cancel_jump":false}}
```

Physics fallback profile (a supplied object replaces the whole profile; omitted members use the documented defaults):

```json
{"id":2,"cmd":"rage","args":{"stop_physics":{"max_speed":250,"friction":5.2,"acceleration":5.5,"air_acceleration":12,"stop_speed":80,"gravity":800}}}
```

Explicit conditional-scale inputs (only supply meanings/values you have established for the desired setup):

```json
{"id":3,"cmd":"rage","args":{"stop_weapon_scale":{"enabled":true,"category":2,"speed0":250,"speed1":150}}}
```

Returned fields include `autostop`, `air_stop`, `fractional_stop`, `cancel_jump`, `stop_path`, `stop_writes`, `stop_profile`. Path IDs:0 inactive,1 unchanged,2 ground scaled,3 ground reverse,4 ground fractional,5 air,6 air fractional,7 invalid. `stop_path` is the selected policy; `stop_writes` counts successful applications.

## Verification

Run `scripts\test-autostop-windows.cmd` from Windows (MSVC x64 developer prompt, or the installed VS2022 Community environment). It builds and runs both suites without game binaries or generated SDK headers. Meson targets `aim-autostop` and, on Windows, `aim-autostop-input` are also registered.

- Solver:300 bit-preserving regression fixtures produced from the independent interpreter of source instructions52C3E8..52C503 (including6 successful fractional choices), plus ordinary ground, ground fallback, airborne impulse, fractional boundary, force-full, weapon-selector/category, zero-budget and invalid-input cases.
- Actual adapter under bounded fake game memory:100 repeated hooks; sorted steps; unique allocated protobuf pointers; preservation of attack/jump/mixed payloads; full movement-table replacement; allocation failure without partial writes; inactive release;17-tick jump-clearing cutoff.
- Both suites compiled with MSVC14.41, `/fp:strict /W4`, and passed. The modified `input.cpp` and `service.cpp` were separately compile-checked. `input.cpp` used a build-directory-only empty offset-declaration stub for that syntax check; it was not installed as calibration or used in a DLL.

A complete calibrated payload build and live validation remain outstanding. Do not infer them from the math/adapter tests. Source investigation remains in the separate IDA-analysis workspace; this port uses its verified arithmetic and keeps unresolved runtime inputs explicit.
