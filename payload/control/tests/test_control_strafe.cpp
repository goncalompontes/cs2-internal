// Unit tests for the auto-strafe's **decision maths**
// (`cs2/control/strafe_geometry.hpp`): the W/A/S/D travel heading, the side's
// dead zone and hysteresis, and the `AirAccelerate` optimum.
//
// The module is pure (no engine headers, no `<windows.h>`), so the whole
// decision can be checked host-side — the previous revision had the maths
// inline in `Input::strafe_tick`, where only a live game could reach it.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "cs2/control/strafe_geometry.hpp"

namespace {

using cs2::control::StrafeIntent;
using cs2::control::StrafePlan;
namespace button = cs2::control::button;

constexpr float k_epsilon = 0.001F;
// A camera yaw with no special angles, so every expected heading is readable.
constexpr float k_view_yaw = 30.0F;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

// The goal the **directional** reading of a button mask asks for.
auto goal_of(std::uint64_t buttons, float view_yaw) -> float {
  return cs2::control::goal_for(cs2::control::intent_from_buttons(buttons), view_yaw);
}

// The goal the **legacy** reading of a button mask asks for.
auto legacy_goal_of(std::uint64_t buttons, float view_yaw) -> float {
  return cs2::control::goal_for(cs2::control::legacy_intent_from_buttons(buttons), view_yaw);
}

// --- the intent -------------------------------------------------------------

void test_intent_decoding() {
  const auto w = cs2::control::intent_from_buttons(button::k_forward);
  assert(nearly(w.forward, 1.0F));
  assert(nearly(w.right, 0.0F));

  const auto s = cs2::control::intent_from_buttons(button::k_back);
  assert(nearly(s.forward, -1.0F));
  assert(nearly(s.right, 0.0F));

  const auto d = cs2::control::intent_from_buttons(button::k_moveright);
  assert(nearly(d.forward, 0.0F));
  assert(nearly(d.right, 1.0F));

  const auto a = cs2::control::intent_from_buttons(button::k_moveleft);
  assert(nearly(a.forward, 0.0F));
  assert(nearly(a.right, -1.0F));

  const auto wd = cs2::control::intent_from_buttons(button::k_forward | button::k_moveright);
  assert(nearly(wd.forward, 1.0F));
  assert(nearly(wd.right, 1.0F));

  const auto none = cs2::control::intent_from_buttons(0);
  assert(nearly(none.forward, 0.0F));
  assert(nearly(none.right, 0.0F));
}

// --- the goal heading --------------------------------------------------------

void test_goal_is_the_camera_forward() {
  // `W` alone asks for the camera's forward.
  assert(nearly(goal_of(button::k_forward, k_view_yaw), k_view_yaw));
  // No key at all falls back to the camera's forward (the legacy behaviour).
  assert(nearly(goal_of(0, k_view_yaw), k_view_yaw));
  assert(nearly(goal_of(button::k_jump, k_view_yaw), k_view_yaw));
}

void test_goal_is_the_lateral_keys() {
  // `D` alone asks for 90 degrees to the **right** of the camera's forward.
  assert(nearly(goal_of(button::k_moveright, k_view_yaw), k_view_yaw - 90.0F));
  // `A` alone asks for 90 degrees to the left.
  assert(nearly(goal_of(button::k_moveleft, k_view_yaw), k_view_yaw + 90.0F));
  // The diagonals are the half-way headings.
  assert(nearly(goal_of(button::k_forward | button::k_moveright, k_view_yaw), k_view_yaw - 45.0F));
  assert(nearly(goal_of(button::k_forward | button::k_moveleft, k_view_yaw), k_view_yaw + 45.0F));
}

void test_goal_is_the_backward_key() {
  // `S` asks for 180 degrees behind the camera (normalised: 210 -> -150).
  assert(nearly(goal_of(button::k_back, k_view_yaw), -150.0F));
}

void test_goal_wraps() {
  // The headings are normalised into (-180, 180].
  assert(nearly(goal_of(button::k_back, 170.0F), -10.0F));
  assert(nearly(goal_of(button::k_moveleft, 170.0F), -100.0F));
  assert(nearly(goal_of(button::k_moveright, 170.0F), 80.0F));
  assert(nearly(goal_of(button::k_forward, 170.0F), 170.0F));
}

void test_legacy_goal() {
  // The legacy reading only knows `W` and `S`, and `S` wins over `W`.
  assert(nearly(legacy_goal_of(button::k_forward, k_view_yaw), k_view_yaw));
  assert(nearly(legacy_goal_of(button::k_back, k_view_yaw), -150.0F));
  assert(nearly(legacy_goal_of(button::k_forward | button::k_back, k_view_yaw), -150.0F));
  // The lateral keys are not part of the legacy goal.
  assert(nearly(legacy_goal_of(button::k_moveright, k_view_yaw), k_view_yaw));
  assert(nearly(legacy_goal_of(button::k_moveleft, k_view_yaw), k_view_yaw));
}

// --- the side ----------------------------------------------------------------

// The plan for a `W` intent with the camera (and so the goal) at `view_yaw`,
// against a velocity heading of 0.
auto plan_for_w(float view_yaw, int previous_side) -> StrafePlan {
  return cs2::control::plan(StrafeIntent{.forward = 1.0F, .right = 0.0F}, view_yaw, 0.0F, 250.0F,
                            46.875F, 30.0F, previous_side);
}

void test_side_follows_the_goal() {
  // The goal 30 degrees to the **right** of the velocity: the wish is pushed
  // clockwise of it.
  const StrafePlan right = plan_for_w(-30.0F, 0);
  assert(right.decisive);
  assert(right.side == -1);
  // The goal 30 degrees to the **left** of the velocity: counter-clockwise.
  const StrafePlan left = plan_for_w(30.0F, 0);
  assert(left.decisive);
  assert(left.side == 1);
}

void test_dead_zone_keeps_the_previous_side() {
  // 5 degrees off the velocity is inside the dead zone: the side of the last
  // decisive tick is kept and the decision is not decisive.
  const StrafePlan held = plan_for_w(5.0F, -1);
  assert(!held.decisive);
  assert(held.side == -1);
  const StrafePlan kept = plan_for_w(-5.0F, 1);
  assert(!kept.decisive);
  assert(kept.side == 1);
  // With no previous side the legacy tie-break (+1) applies.
  const StrafePlan fresh = plan_for_w(5.0F, 0);
  assert(!fresh.decisive);
  assert(fresh.side == 1);
  // 30 degrees off is decisive: the side flips.
  const StrafePlan flipped = plan_for_w(30.0F, -1);
  assert(flipped.decisive);
  assert(flipped.side == 1);
  const StrafePlan flipped_back = plan_for_w(-30.0F, 1);
  assert(flipped_back.decisive);
  assert(flipped_back.side == -1);
}

void test_dead_zone_constant() {
  assert(nearly(cs2::control::k_strafe_side_deadzone_deg, 20.0F));
  // Exactly on the dead zone's edge the decision is decisive.
  const StrafePlan edge = plan_for_w(20.0F, -1);
  assert(edge.decisive);
  assert(edge.side == 1);
  // Just inside it the previous side is kept.
  const StrafePlan inside = plan_for_w(19.9F, -1);
  assert(!inside.decisive);
  assert(inside.side == -1);
}

void test_side_is_stable_across_ticks() {
  // Regression: `W` held with a constant velocity puts the goal exactly on the
  // velocity heading, so the tie-break must not flip the curve every tick.
  int previous = 0;
  for (int tick = 0; tick < 64; ++tick) {
    const StrafePlan step = plan_for_w(0.0F, previous);
    assert(!step.decisive);
    assert(step.side == 1);
    previous = step.side;
  }
  // The same run with a side already decided keeps that side.
  previous = -1;
  for (int tick = 0; tick < 64; ++tick) {
    const StrafePlan step = plan_for_w(0.0F, previous);
    assert(!step.decisive);
    assert(step.side == -1);
    previous = step.side;
  }
}

void test_wish_is_perpendicular_to_the_velocity() {
  // The wish sits at `theta*` off the velocity, on the chosen side.
  const StrafePlan step = plan_for_w(-30.0F, 0);
  assert(nearly(step.theta, 90.0F));  // A >= W: exactly perpendicular
  assert(nearly(step.wish_yaw, -90.0F));
  const StrafePlan left = plan_for_w(30.0F, 0);
  assert(nearly(left.wish_yaw, 90.0F));
}

// --- the `AirAccelerate` optimum ---------------------------------------------

void test_optimal_theta() {
  // `A >= W` collapses the optimum to exactly 90 degrees.
  assert(nearly(cs2::control::optimal_theta(250.0F, 30.0F, 30.0F), 90.0F));
  assert(nearly(cs2::control::optimal_theta(250.0F, 46.875F, 30.0F), 90.0F));
  assert(nearly(cs2::control::optimal_theta(250.0F, 60.0F, 30.0F), 90.0F));
  // A slow player needs a small angle: the wish is nearly parallel to the
  // velocity (the cap is unreachable).
  const float slow = cs2::control::optimal_theta(10.0F, 12.0F, 30.0F);
  assert(slow < 5.0F);
  assert(slow > 0.0F);
  // The result is always in (0, 90].
  const float speeds[] = {1.0F, 5.0F, 25.0F, 100.0F, 250.0F, 1000.0F};
  const float accels[] = {0.0F, 1.0F, 12.0F, 29.9F, 30.0F, 46.875F, 100.0F};
  for (const float speed : speeds) {
    for (const float accel : accels) {
      const float theta = cs2::control::optimal_theta(speed, accel, 30.0F);
      assert(theta > 0.0F);
      assert(theta <= 90.0F);
    }
  }
}

// --- the engine's side-axis convention ---------------------------------------
//
// The convention is recorded in exactly one place —
// `cs2::control::k_engine_side_axis_positive_is_left` — and derived from the
// engine's own code (see `docs/re/strafe-sign.md`). These tests pin it, so a
// change to the constant is a deliberate edit that shows up as a failing
// assertion instead of a silent 180-degree flip in the strafe.

void test_side_axis_convention_is_pinned() {
  // The engine's command field is `leftmove` (the movement services' schema
  // calls it `m_flCmdLeftMove`), so a positive side axis is the player's LEFT.
  // A failure here names the constant, so it says which way moved.
  assert(cs2::control::k_engine_side_axis_positive_is_left);
  // ... which means a rightward lateral quantity is a NEGATIVE engine side
  // axis.
  assert(nearly(cs2::control::engine_side_axis(1.0F), -1.0F));

  // The engine's second view-frame basis vector is the forward vector rotated
  // +90 degrees — the player's left, NOT the right.
  const auto basis = cs2::control::engine_view_basis(k_view_yaw);
  const float yaw = k_view_yaw * cs2::control::k_deg_to_rad;
  assert(nearly(basis.forward.x, std::cos(yaw)));
  assert(nearly(basis.forward.y, std::sin(yaw)));
  assert(nearly(basis.side.x, -std::sin(yaw)));
  assert(nearly(basis.side.y, std::cos(yaw)));
  // ... and it is 90 degrees counter-clockwise of the forward vector.
  assert(nearly(cs2::control::heading_of(basis.side.x, basis.side.y),
                k_view_yaw + 90.0F));

  // A lateral quantity that is positive to the player's right is a **negative**
  // engine side axis, and the other way round.
  assert(nearly(cs2::control::engine_side_axis(1.0F), -1.0F));
  assert(nearly(cs2::control::engine_side_axis(-1.0F), 1.0F));
  assert(nearly(cs2::control::engine_side_axis(0.0F), 0.0F));

  // A wish 90 degrees counter-clockwise of the view forward (to the player's
  // left) needs a **positive** side axis; 90 degrees clockwise needs a
  // negative one.
  assert(nearly(cs2::control::engine_side_axis_for_wish(90.0F), 1.0F));
  assert(nearly(cs2::control::engine_side_axis_for_wish(-90.0F), -1.0F));
  assert(nearly(cs2::control::engine_side_axis_for_wish(0.0F), 0.0F));
  assert(nearly(cs2::control::engine_side_axis_for_wish(30.0F), 0.5F));
}

void test_the_goal_matches_the_engine_wish() {
  // The goal heading and the engine's own wish construction must agree: build
  // the wish the way `AirMove` does — `forward * fmove + basis * leftmove` —
  // and its heading must be the goal.
  for (const float view_yaw : {0.0F, 30.0F, 90.0F, 170.0F, -120.0F}) {
    for (const std::uint64_t buttons :
         {button::k_forward, button::k_moveright, button::k_moveleft,
          button::k_forward | button::k_moveright,
          button::k_forward | button::k_moveleft}) {
      const auto intent = cs2::control::intent_from_buttons(buttons);
      const auto basis = cs2::control::engine_view_basis(view_yaw);
      const float side = cs2::control::engine_side_axis(intent.right);
      const float x = intent.forward * basis.forward.x + side * basis.side.x;
      const float y = intent.forward * basis.forward.y + side * basis.side.y;
      const float wish = cs2::control::heading_of(x, y);
      const float goal = cs2::control::goal_for(intent, view_yaw);
      assert(nearly(wish, goal));
    }
  }
}

void test_the_goal_headings_are_the_players_left_and_right() {
  // The world headings the A/D keys ask for, read straight off the engine's
  // basis: `A` is 90 degrees to the player's left (yaw + 90), `D` 90 degrees
  // to the right (yaw - 90). These are the facts the strafe is built on.
  assert(nearly(goal_of(button::k_moveleft, k_view_yaw), k_view_yaw + 90.0F));
  assert(nearly(goal_of(button::k_moveright, k_view_yaw), k_view_yaw - 90.0F));
  // And the engine's side axis for those two: `A` is positive (left), `D` is
  // negative.
  const auto a_axis = cs2::control::engine_side_axis(
      cs2::control::intent_from_buttons(button::k_moveleft).right);
  const auto d_axis = cs2::control::engine_side_axis(
      cs2::control::intent_from_buttons(button::k_moveright).right);
  assert(nearly(a_axis, 1.0F));
  assert(nearly(d_axis, -1.0F));
}

}  // namespace

auto main() -> int {
  test_intent_decoding();
  test_goal_is_the_camera_forward();
  test_goal_is_the_lateral_keys();
  test_goal_is_the_backward_key();
  test_goal_wraps();
  test_legacy_goal();
  test_side_follows_the_goal();
  test_dead_zone_keeps_the_previous_side();
  test_dead_zone_constant();
  test_side_is_stable_across_ticks();
  test_wish_is_perpendicular_to_the_velocity();
  test_optimal_theta();
  test_side_axis_convention_is_pinned();
  test_the_goal_matches_the_engine_wish();
  test_the_goal_headings_are_the_players_left_and_right();
  std::puts("control-strafe: ok");
  return 0;
}
