# T3 — Directional autostrafe (WASD dictates the wish)

**Goal:** the auto-strafer must derive its **travel heading** from the player's
W/A/S/D input instead of only from the camera's forward/back, and A/D must no
longer switch the feature off.

**File ownership (you may ONLY touch these):**

- `payload/control/include/cs2/control/input.hpp`
- `payload/control/src/input.cpp`
- `payload/control/include/cs2/control/service.hpp`
- `payload/control/src/service.cpp`
- `payload/control/include/cs2/control/strafe_geometry.hpp` *(new)*
- `payload/control/tests/test_control_strafe.cpp` *(new)*
- `payload/control/meson.build`, root `meson.build` (test wiring only)
- `payload/src/payload_main.cpp` (only the settings→control plumbing)

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`docs/re/strafing.md` (especially §"The silent movement-yaw channel", the
gap-analysis table and the "two biggest suspects"), `docs/re/subtick.md`
§"The movement yaw channel", and `docs/handoff.md` §"Strafer".

**Task T1 owns `wire::Settings`.** It adds these fields — use them, do not add
your own:

```
uint8_t movement_strafe_mode;          // 0 = off, 1 = camera-relative, 2 = directional (WASD)
uint8_t movement_strafe_ignore_keys;   // 0 = A/D biases the heading, 1 = A/D stands down (legacy)
float  movement_strafe_wishspeed;
uint8_t movement_strafe_subticks;
```

---

## 1. The bug in the current implementation (read this first)

Today `Input::strafe_tick` (`payload/control/src/input.cpp:479`) does:

```cpp
constexpr std::uint64_t side_bits = button::k_moveleft | button::k_moveright;
if ((buttons & side_bits) != 0) {
  strafe_reason_.store(7);      // "the player is holding A/D — their input wins"
  strafe_airborne_.store(false);
  return;                       // <-- the feature stands down
}
const float goal = normalize_angle(view_yaw + ((buttons & button::k_back) != 0 ? 180.0F : 0.0F));
```

So the goal heading is **only** the camera's forward (W) or backward (S), and
holding A or D switches the whole feature off.

**Requirement:** the goal heading must be the **player's travel intent**, computed
from the held W/A/S/D in the view frame.

## 2. The new goal computation

World frame conventions used by the engine (and by the existing code):

- forward(yaw) = `(cos yaw, sin yaw)`
- right(yaw)   = `(sin yaw, -cos yaw)`   (the engine's `right(yaw)`; note the `-cos`)
- `leftmove` is positive to the **right**.
- The right vector's world heading is `yaw - 90°`.

Compute the intent in the view frame and rotate it into the world:

```
f = (+1 if W) + (-1 if S)          // forward/back
l = (+1 if D) + (-1 if A)          // left/right, +1 = right

intent_world = f * forward(view_yaw) + l * right(view_yaw)
```

- If `f == 0 && l == 0` (no movement key held) → **fall back to the camera
  forward** (the legacy behaviour: the strafe still acts, following the camera).
- Otherwise normalise `intent_world` and take
  `goal = atan2(intent_world.y, intent_world.x)`.

`goal` is the heading the player's own input asks for:
`W` → `view_yaw`, `S` → `view_yaw + 180°`, `D` → `view_yaw - 90°`,
`A` → `view_yaw + 90°`, `W+D` → `view_yaw - 45°`.

## 3. Choosing the side (and the degenerate case)

Keep the existing rule — the wish is pushed so that it rotates the velocity
**toward** the goal:

```
delta = normalize_angle(goal - vel_yaw)
side  = (delta >= 0) ? +1 : -1
wish_yaw = vel_yaw + side * theta
```

**But** with the WASD goal there is a new degenerate case that the old code did
not have: **holding W alone puts the goal exactly on the velocity heading**, so
`delta == 0` and the side is decided by an arbitrary tie-break. That is a
discontinuity: `W` alone would curve one way and `W` + a hair of `D` the other.

**Required behaviour — a dead zone with hysteresis:**

- If `|delta| >= k_strafe_side_deadzone_deg` (use **20°**) → `side = sign(delta)`.
- Otherwise → **keep the previous side** (the side of the last tick that had a
  decisive `delta`). This makes `W` alone continue the current curve instead of
  flipping every tick, and makes the classic "tap A and D alternately" air-strafe
  work.
- If there is no previous side yet → default to `+1` (the legacy tie-break).
- The dead zone and the last side are **state**: add a member (e.g.
  `std::atomic<int> strafe_side_last_`) and update it whenever the side is
  decided decisively. Reset it when the strafe stops acting (grounded, disabled,
  or below the minimum speed) so a fresh jump starts from the default.

**Legacy mode.** `movement_strafe_ignore_keys == 1` must restore exactly the old
behaviour: a held A/D stands the feature down, and the goal is the camera
forward/back. `movement_strafe_ignore_keys == 0` (the default) is the new
behaviour. `movement_strafe_mode == 0` disables the feature entirely, `1` is the
legacy camera-relative behaviour, `2` is the new directional behaviour.

## 4. Extract the maths into a testable unit

The goal/side/theta maths must be **pure and host-testable** — the current code
has it inline in a 150-line member function that can only be exercised in a live
game.

Create `payload/control/include/cs2/control/strafe_geometry.hpp`:

```cpp
#pragma once
namespace cs2::control {
// A pure, engine-free description of the auto-strafe's decision.
struct StrafeIntent { float forward; float right; };          // -1 .. 1
struct StrafePlan {
  float goal_yaw{};        // the travel heading the wish rotates toward (degrees)
  int side{1};             // +1 / -1
  float theta{};           // the AirAccelerate optimum (degrees)
  float wish_yaw{};        // the heading of the wish (degrees)
  bool decisive{false};    // false when the dead zone kept the previous side
};
auto intent_from_buttons(std::uint64_t buttons) noexcept -> StrafeIntent;
auto goal_for(const StrafeIntent& intent, float view_yaw) noexcept -> float;
auto optimal_theta(float speed, float accel_per_tick, float cap) noexcept -> float;
auto plan(const StrafeIntent& intent, float view_yaw, float vel_yaw, float speed,
          float accel_per_tick, float cap, int previous_side,
          float deadzone_deg) noexcept -> StrafePlan;
}  // namespace cs2::control
```

The exact signatures are yours to refine, but:

- the module must depend on **nothing** but `<cmath>`/`<algorithm>`/`<cstdint>`
  (no engine headers, no Windows headers);
- `strafe_tick` must call it — **do not duplicate the maths**;
- the `button::` bit constants must be available to it. `button::` lives in
  `input.hpp` today; either move the button bits into a small dependency-free
  header (`strafe_geometry.hpp` or a new `buttons.hpp`) or pass the already-decoded
  `StrafeIntent` in from `strafe_tick`. **Prefer passing the decoded intent** so
  the pure module does not need the button mask at all — but then
  `intent_from_buttons` must live where the button bits are. Pick one and make it
  clean.

**Host test** `payload/control/tests/test_control_strafe.cpp`, wired the way
`test_control_core.cpp` is wired (`payload/control/meson.build` + the root
`meson.build`). It must cover at least:

- `W` alone → the goal is the camera forward.
- `D` alone → the goal is the camera forward **minus 90°**.
- `A` alone → the camera forward **plus 90°**.
- `W+D` → the camera forward **minus 45°**; `W+A` → **plus 45°**.
- `S` → the camera forward **plus 180°**.
- No key → the goal is the camera forward.
- The side flips when the goal crosses the velocity heading
  (e.g. the goal 30° to the right of the velocity → side −1; 30° to the left → +1).
- The dead zone: a goal 5° off the velocity → the side is the *previous* side and
  `decisive == false`; a goal 30° off → the side flips and `decisive == true`.
- `optimal_theta`: `accel >= cap` → exactly 90°; a slow speed → a small angle; the
  result is always in `(0, 90]`.
- A regression case: the side is stable across a run of ticks with `W` held and a
  constant velocity (no per-tick flipping).

## 5. Settings plumbing

- `Input` needs setters for the new knobs (`strafe_mode`, `strafe_ignore_keys`,
  `strafe_wishspeed`, `strafe_subticks`) — follow the existing
  `set_strafe_config` / `set_strafe` pattern and the atomic-member style.
- `Service::set_movement_features` currently takes `(bool auto_strafe, bool bhop)`.
  Extend it (or add a dedicated setter) so the new fields reach the `Input`.
- `payload/src/payload_main.cpp`: the settings sink (`on_movement_settings`) must
  forward the new fields. **Only touch that function** — nothing else in the file.

## 6. Diagnostics

The existing `strafe_*` status fields are the operator's window into the feature
(`input()` in `payload/control/src/service.cpp`). Keep every existing field and
**add**:

- `strafe_goal` — already exists; it must now report the **WASD** goal, not just
  the camera forward.
- `strafe_intent_f` / `strafe_intent_l` — the decoded view-frame intent
  (`forward`, `right`), so the operator can see the feature read the keys.
- `strafe_side_locked` — 1 when the dead zone kept the previous side.
- `strafe_deadzone_deg` — the configured dead zone (constant, but expose it so the
  operator can confirm the build).

The `strafe_reason` codes are a diagnostic language; **do not renumber the
existing ones**. Add new codes for new outcomes and **document the code list** in
the header.

## 7. Constraints

- The strafe must stay **silent**: no view angle is written, the camera must not
  move, the pawn's eye angles must not drift (the existing test asserts
  `eye_drift` — keep that assertion).
- The strafe stays **airborne only** and must keep the `on_ground()` gate.
- The strafe must keep working in **both** encodings: the sub-tick yaw rotation
  (`subticks >= 1`, the default 8) and the camera-frame analog
  (`subticks == 0`).
- Do **not** change the `theta*` formula, the per-slice simulation, the
  `write_strafe_wish` encoding, the idempotency check (`strafe_steps_present`) or
  the step ordering (`sort_last_step`) unless there is a reason — those are
  validated live (see `docs/handoff.md` §"Strafer": +85.1 u/s/hop at 8 slices).
- Do **not** touch the ragebot.

## 8. Verification

- `meson compile -C build && meson test -C build` → 17/17 **plus** your new test.
- `meson compile -C build-payload` → clean.
- The new host test must actually fail if you break the maths (check it by
  temporarily inverting a sign and confirming the test fails, then reverting).
- Report: the new goal formula, the dead-zone/hysteresis behaviour, the test
  cases, and **what you could not verify without a live game** (you cannot launch
  CS2 — it needs `schtasks /run /tn CS2Launch` and a GPU; say so explicitly).
