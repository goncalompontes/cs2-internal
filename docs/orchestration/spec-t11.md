# T11 — a triggerbot (the safe half of the aim)

**Goal:** fire when the crosshair is over a target **at a computed hit chance**,
without writing any view angle. The ragebot already computes a per-cell damage
field and a hit probability; a trigger is the *conservative* consumer of the same
mathematics — it can only ever pull the trigger, never aim.

**File ownership (you may ONLY touch these):**

- `payload/control/include/cs2/control/trigger.hpp` *(new)*
- `payload/control/src/trigger.cpp` *(new)*
- `payload/control/include/cs2/control/service.hpp`
- `payload/control/src/service.cpp`
- `payload/control/tests/test_control_trigger.cpp` *(new)*
- `payload/control/meson.build`, the root `meson.build` (the test wiring)
- `payload/src/payload_main.cpp` (the settings→trigger plumbing only)
- `docs/ragebot.md` (a section: the trigger)

**Do NOT touch** `payload/aim/**` (the damage field and the solver are existing
machinery — **use** them), `payload/features/**`, or the wire struct.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`docs/ragebot.md` (the solver's contract) and `payload/control/src/service.cpp`
(the `do_aim` handler — the trigger's maths is the same solve).

---

## 1. What already exists

- `payload/aim`'s **damage field** and solver: `aim::SolveInput`, `aim::solve()`,
  `aim::SolveResult` — it takes a snapshot, a weapon profile, the enemies and the
  occluders, and returns the best aim point with `{valid, yaw, pitch, value,
  hitchance, …}` and the per-hitbox list.
- `Service::do_aim` (`payload/control/src/service.cpp`) drives that solve and
  serialises the result for the control channel.
- The **game-thread sink** `Service::on_game_thread` — where the per-tick work
  happens (the ragebot's `rage_tick` runs there).
- The **input** backend: `cs2::control::Input` writes the command's button mask
  (`press`/`release`) and the silent angles.

**Do not re-implement the solver.** The trigger calls the same `aim::solve` the
ragebot uses, and adds the *decision* on top: is the crosshair on the solved
point, and is the hit chance good enough?

## 2. The decision

On each evaluation the trigger:

1. **solves** the field (reuse `aim::solve`; do not call the control channel's
   own `aim` command — the trigger runs inside the payload);
2. compares the **current view angles** with the solved aim point:
   - the yaw error and the pitch error, each against its own threshold;
   - a **hitbox** target is preferred: if the solved point lands on a head hitbox
     and the crosshair is within the head's radius, the trigger fires at a lower
     hitchance threshold (the classic "head = trigger" behaviour) — make that
     configurable, not hard-coded;
3. compares the solve's **hitchance** against the threshold;
4. fires (a button press) when all of it holds.

**The trigger must never write a view angle.** That is the whole point of it: it
is the "safe" mode. Make that a property of the code, not a convention.

## 3. The settings

The trigger's settings are **not** in the wire struct (the feature wave's
settings are full, and the wire struct is versioned). So the trigger takes its
configuration through the **control channel** and the **environment**, like the
ragebot's own tuning does:

- a `trigger` **control command** with: `on`, `hitchance` (the minimum, 0..1),
  `head_hitchance` (the head-hitbox minimum), `yaw_tolerance` (degrees),
  `pitch_tolerance` (degrees), `delay_ms` (the reaction delay), `key` (an optional
  trigger key: fire only while it is held; 0 = always on), `fire` (whether the
  trigger pulls the trigger itself) and `mode` (`hold` / `always` / `toggle`).
- The same values are readable from the control channel (`trigger` with no
  arguments returns the current state) so a scenario or an agent can drive it.
- The `delay_ms` is a **reaction delay**: after the conditions first hold, wait
  that long before firing. Implement it as a timestamp on the game thread, not a
  sleep.

**Do not add fields to `wire::Settings`.** If you think the trigger's settings
belong there, say so in your report instead — and remember that adding one
requires bumping `wire::k_version` **and** recomputing the golden settings
checksum.

## 4. The firing

- The trigger pulls the trigger through the **existing input backend**
  (`Input::press`/`Input::release` with the attack bit) — not through a console
  command, and not through a synthetic OS event.
- The press/release must be **balanced**: a trigger that presses and never
  releases would leave the gun firing. The trigger's state must be reset when it
  is disabled, when the game state changes, and when the channel disconnects.
- If the trigger is driven by a **held key** (`key != 0`), the key's own state
  is read from the command's button mask — **the same latch landmine** the
  autostrafe hit: do not read a button state that the feature itself writes.

## 5. Where it runs

The evaluation is per-tick work on the **game thread** (the same place the
ragebot's `rage_tick` runs) — not on the render thread, and not from the control
thread. The control thread only *configures* it.

The solve is not free: the ragebot's own solve is measured at ~5 ms of the tick
at the best spot and much worse at dense spots. **Measure the trigger's cost and
report it.** Options if it is too expensive:
- evaluate at a lower rate (e.g. every other tick) and say so;
- or evaluate only when the cheap preconditions hold (the crosshair is near an
  enemy's screen-space position) before doing the expensive solve.

**A trigger that costs 5 ms a tick is a bug.** The preconditions gate matters:
the expensive solve should only run when the crosshair is plausibly on someone.

## 6. The host test

The decision logic must be **pure and testable**: a function that takes
`(view_angles, solve_result, config, now_ms)` and returns the decision
(`{fire, reason, delay_remaining}`). Put it in `trigger.hpp` and test it in
`payload/control/tests/test_control_trigger.cpp`:

- fires when the crosshair is on the target and the hitchance is good;
- does **not** fire when the hitchance is below the threshold;
- does **not** fire when the yaw/pitch error is too large;
- fires at the **head** threshold when the solve landed on a head hitbox;
- honours the **delay** (no fire before it elapses, fires after);
- does not fire twice for one continuous aim (the "already firing" latch);
- the reason string says **why** it did not fire (the operator's window).

Prove the test can fail.

## 7. Style

`docs/style-guide.md` is normative: trailing return types, `snake_case`,
`SCREAMING_SNAKE_CASE` constants, `enum class`, `[[nodiscard]]`, `noexcept`,
fully-qualified `std::`, **no exceptions**, no new global mutable state
(ADR-0010). The payload is **manually mapped**: no CRT static-init, no
`<print>`, every engine read through the fault-safe helpers.

## 8. Verification

- `meson compile -C build && meson test -C build` → green (23/23 at baseline).
- `meson compile -C build-payload` → clean.
- **Prove the tests can fail.**
- You cannot launch CS2 (it needs `schtasks /run /tn CS2Launch` and a GPU in the
  interactive session) — say clearly what is unverified.

## 9. Report

- The decision function's shape and where it runs.
- **The cost**: how the solve is gated, what the per-tick cost is, and what you
  measured or estimated.
- The settings surface (the control command) and how it is configured.
- The firing path and how the press/release is balanced.
- The test cases, and what you could not verify without a live game.
