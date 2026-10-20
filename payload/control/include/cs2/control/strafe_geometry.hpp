#pragma once

// The auto-strafe's **decision maths**: the travel heading the player's WASD
// asks for, the side of the velocity the wish is pushed on, and the
// `AirAccelerate` optimum the wish sits at.
//
// Why a module of its own: this used to live inline in `Input::strafe_tick`, a
// 150-line member function that could only be exercised in a live game. The
// decision is a pure function of the command's button mask, the camera yaw, the
// velocity and the previous side, so it belongs in a module that depends on
// nothing but the standard library — the host test then covers the goal
// formula, the dead zone and the optimum without a game.
//
// The engine conventions encoded here (`docs/re/strafing.md`):
//   * `forward(yaw) = (cos yaw, sin yaw)` and `right(yaw) = (sin yaw, -cos yaw)`,
//     so the right vector's world heading is `yaw - 90` and `leftmove` is
//     positive to the **right**;
//   * the wish is pushed **perpendicular** to the velocity (at the
//     `AirAccelerate` optimum `theta*`), so the velocity curves toward the goal
//     and re-perpendicularises every tick instead of decaying.
//
// This header must stay dependency-free: no engine headers, no `<windows.h>`.
// It is compiled into the host test and into the payload.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs2::control {

// --- The engine's button bits -----------------------------------------------
//
// They live here rather than in `input.hpp` so this header stays
// self-contained: the pure maths decodes a command's mask without dragging the
// engine in. The names are the engine's own `IN_*` bits (`docs/re/subtick.md`
// §"The command build"); the source of the mask is the `CUserCmd` button word
// at `cmd + 0x60`.
namespace button {
inline constexpr std::uint64_t k_attack = 1ull << 0;
inline constexpr std::uint64_t k_jump = 1ull << 1;
inline constexpr std::uint64_t k_duck = 1ull << 2;
inline constexpr std::uint64_t k_forward = 1ull << 3;
inline constexpr std::uint64_t k_back = 1ull << 4;
inline constexpr std::uint64_t k_use = 1ull << 5;
inline constexpr std::uint64_t k_cancel = 1ull << 6;
inline constexpr std::uint64_t k_left = 1ull << 7;
inline constexpr std::uint64_t k_right = 1ull << 8;
inline constexpr std::uint64_t k_moveleft = 1ull << 9;
inline constexpr std::uint64_t k_moveright = 1ull << 10;
inline constexpr std::uint64_t k_attack2 = 1ull << 11;
inline constexpr std::uint64_t k_run = 1ull << 12;
inline constexpr std::uint64_t k_reload = 1ull << 13;
inline constexpr std::uint64_t k_alt1 = 1ull << 14;
inline constexpr std::uint64_t k_alt2 = 1ull << 15;
inline constexpr std::uint64_t k_score = 1ull << 16;
inline constexpr std::uint64_t k_speed = 1ull << 17;
inline constexpr std::uint64_t k_walk = 1ull << 18;
inline constexpr std::uint64_t k_zoom = 1ull << 19;
}  // namespace button

// --- Constants ---------------------------------------------------------------

//: Pi, for the degrees <-> radians conversions.
inline constexpr float k_pi = 3.14159265358979323846F;
//: Radians per degree.
inline constexpr float k_deg_to_rad = k_pi / 180.0F;

//: The dead zone of the side decision, in degrees. Inside it the strafe keeps
//: the side of the last decisive tick instead of re-deciding it. Holding `W`
//: alone puts the goal exactly on the velocity heading, and the tie-break
//: there would flip the curve every tick — `W` would curve one way and `W`
//: plus a hair of `D` the other. 20 degrees sits below the 45 degrees a
//: diagonal key press moves the goal, so a deliberate `A`/`D` still flips the
//: side, and well above the per-tick drift of the velocity heading, so the
//: curve does not chatter.
inline constexpr float k_strafe_side_deadzone_deg = 20.0F;

//: The smallest wish angle the strafe writes, in degrees. Below it the wish is
//: parallel to the velocity and gains nothing, so it is clamped away.
inline constexpr float k_strafe_min_theta_deg = 1.0F;

//: The largest wish angle: a purely perpendicular push.
inline constexpr float k_strafe_max_theta_deg = 90.0F;

// --- The decision ------------------------------------------------------------

// How the auto-strafe derives its goal heading. The numbering is the wire's
// `movement_strafe_mode` byte and matches `cs2::features::StrafeMode`.
enum class StrafeMode : std::uint8_t {
  off = 0,              // the feature is disabled
  camera_relative = 1,  // the goal is the camera's forward/back (the legacy goal)
  directional = 2,      // the goal is the player's W/A/S/D intent
};

// The player's travel intent, read in the **view frame**: `forward` is the
// forward/back axis (+1 = `W`, -1 = `S`) and `right` the left/right axis
// (+1 = `D`, -1 = `A`). Both are 0 when the key is not held, so an empty
// intent means "no movement key" — the strafe then follows the camera.
struct StrafeIntent {
  float forward{0.0F};
  float right{0.0F};
};

// The auto-strafe's decision for one tick.
struct StrafePlan {
  float goal_yaw{0.0F};  // the travel heading the wish rotates toward (degrees)
  int side{1};           // +1 = counter-clockwise of the velocity, -1 = clockwise
  float theta{0.0F};     // the `AirAccelerate` optimum (degrees)
  float wish_yaw{0.0F};  // the heading of the wish (degrees)
  bool decisive{false};  // false when the dead zone kept the previous side
};

//: Shortest signed difference `a - b` in degrees, in `(-180, 180]`.
[[nodiscard]] inline auto normalize_angle(float value) noexcept -> float {
  auto wrapped = std::fmod(value + 180.0F, 360.0F);
  if (wrapped < 0.0F) {
    wrapped += 360.0F;
  }
  return wrapped - 180.0F;
}

//: The heading of a world direction, in degrees.
[[nodiscard]] inline auto heading_of(float x, float y) noexcept -> float {
  return static_cast<float>(std::atan2(y, x) * 180.0 / k_pi);
}

// Decode a command's button mask into the view-frame intent: `W` is +1
// forward, `S` is -1, `D` is +1 to the right and `A` is -1. A key held on both
// sides cancels (`W`+`S` is no forward intent at all).
[[nodiscard]] inline auto intent_from_buttons(std::uint64_t buttons) noexcept -> StrafeIntent {
  StrafeIntent intent{};
  intent.forward = ((buttons & button::k_forward) != 0 ? 1.0F : 0.0F) -
                   ((buttons & button::k_back) != 0 ? 1.0F : 0.0F);
  intent.right = ((buttons & button::k_moveright) != 0 ? 1.0F : 0.0F) -
                 ((buttons & button::k_moveleft) != 0 ? 1.0F : 0.0F);
  return intent;
}

// The **legacy** intent: only `W` and `S` reach the goal, and `S` wins over
// `W` (the old code tested the `S` bit directly, so holding both pointed
// backwards). The lateral keys are not part of it — in the legacy behaviour a
// held `A`/`D` stands the whole feature down.
[[nodiscard]] inline auto legacy_intent_from_buttons(std::uint64_t buttons) noexcept
    -> StrafeIntent {
  StrafeIntent intent{};
  if ((buttons & button::k_back) != 0) {
    intent.forward = -1.0F;
  } else if ((buttons & button::k_forward) != 0) {
    intent.forward = 1.0F;
  }
  return intent;
}

// The travel heading the intent asks for, in the world frame (degrees).
//
//   `intent_world = forward(view_yaw) * f + right(view_yaw) * l`
//
// which turns `W` into the camera's forward, `S` into 180 degrees behind it,
// `D` into 90 degrees to the right, `A` into 90 degrees to the left and
// `W`+`D` into 45 degrees to the right. An empty intent (no movement key)
// falls back to the camera's forward — the strafe still acts and follows the
// camera, as it always did.
[[nodiscard]] inline auto goal_for(const StrafeIntent& intent, float view_yaw) noexcept -> float {
  const float yaw = view_yaw * k_deg_to_rad;
  const float forward_x = std::cos(yaw);
  const float forward_y = std::sin(yaw);
  const float right_x = std::sin(yaw);
  const float right_y = -std::cos(yaw);
  const float x = intent.forward * forward_x + intent.right * right_x;
  const float y = intent.forward * forward_y + intent.right * right_y;
  if (x == 0.0F && y == 0.0F) {
    return normalize_angle(view_yaw);
  }
  return normalize_angle(heading_of(x, y));
}

// The `AirAccelerate` optimum: the angle off the velocity at which the wish
// gains the most.
//
//   `theta* = acos((W - A) / speed)`
//
// with `W = sv_air_max_wishspeed` (the air cap, 30) and `A` the per-call
// acceleration budget. `A >= W` collapses the optimum to exactly 90 degrees (a
// pure perpendicular push), which is the usual case for a full analog. The
// result is always in `(0, 90]`.
[[nodiscard]] inline auto optimal_theta(float speed, float accel_per_tick, float cap) noexcept
    -> float {
  if (cap - accel_per_tick <= 0.0F || speed <= 0.0F) {
    return k_strafe_max_theta_deg;  // A >= W: exactly perpendicular
  }
  const auto radians = std::acos(std::clamp((cap - accel_per_tick) / speed, -1.0F, 1.0F));
  return std::clamp(static_cast<float>(radians * 180.0 / k_pi), k_strafe_min_theta_deg,
                    k_strafe_max_theta_deg);
}

// The whole decision for one tick.
//
// The wish is pushed **perpendicular** to the velocity, on the side that turns
// the velocity toward the goal:
//
//   `delta = normalize_angle(goal - vel_yaw)`
//   `side  = (delta >= 0) ? +1 : -1`
//   `wish_yaw = vel_yaw + side * theta`
//
// `delta` is the goal's offset from the velocity heading. When it is decisive
// (at or beyond `deadzone_deg`) the side follows it; when it is not, the
// previous side is kept — that is the hysteresis, and it is what makes holding
// `W` alone continue the current curve instead of flipping every tick. With no
// previous side the legacy tie-break (+1) applies.
[[nodiscard]] inline auto plan(const StrafeIntent& intent, float view_yaw, float vel_yaw,
                               float speed, float accel_per_tick, float cap, int previous_side,
                               float deadzone_deg = k_strafe_side_deadzone_deg) noexcept
    -> StrafePlan {
  StrafePlan result{};
  result.goal_yaw = goal_for(intent, view_yaw);
  const float delta = normalize_angle(result.goal_yaw - vel_yaw);
  if (std::fabs(delta) >= std::fabs(deadzone_deg)) {
    result.side = delta >= 0.0F ? 1 : -1;
    result.decisive = true;
  } else {
    result.side = previous_side != 0 ? previous_side : 1;
    result.decisive = false;
  }
  result.theta = optimal_theta(speed, accel_per_tick, cap);
  result.wish_yaw = normalize_angle(vel_yaw + static_cast<float>(result.side) * result.theta);
  return result;
}

}  // namespace cs2::control
