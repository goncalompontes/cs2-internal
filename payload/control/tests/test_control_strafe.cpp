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
#include <string_view>

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
  // The headings are normalised into [-180, 180).
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

// The normalised angle is in `[-180, 180)`: 180 degrees normalises to -180.
void test_normalize_angle_range() {
  assert(nearly(cs2::control::normalize_angle(180.0F), -180.0F));
  assert(nearly(cs2::control::normalize_angle(-180.0F), -180.0F));
  assert(nearly(cs2::control::normalize_angle(190.0F), -170.0F));
  assert(nearly(cs2::control::normalize_angle(0.0F), 0.0F));
  assert(nearly(cs2::control::normalize_angle(360.0F), 0.0F));
}

// --- the legacy mode ---------------------------------------------------------

// The **old** rule, verbatim: the side follows the sign of the goal's offset
// from the velocity heading.
//
//   `delta = normalize_angle(goal - vel_yaw)`
//   `side  = (delta >= 0) ? +1 : -1`
//   `wish_yaw = normalize_angle(vel_yaw + side * theta)`
auto old_side(float goal_yaw, float vel_yaw) -> int {
  const float delta = cs2::control::normalize_angle(goal_yaw - vel_yaw);
  return delta >= 0.0F ? 1 : -1;
}

// The legacy mode restores the old behaviour **exactly**.
//
// The regression: the legacy modes (the camera-relative goal and
// `strafe_ignore_keys`) were pushed through the new `plan()`, so the dead zone
// and the hysteresis still applied and the side could differ from the old
// rule — 509 of 2596 sampled decisions, every one of them inside the dead
// zone.
void test_legacy_mode_is_the_old_rule() {
  // The legacy goal knows `W` and `S` (and nothing else).
  const StrafeIntent intents[] = {
      {.forward = 1.0F, .right = 0.0F},   // W
      {.forward = -1.0F, .right = 0.0F},  // S
      {.forward = 0.0F, .right = 0.0F},   // no key: the camera's forward
  };
  int differences = 0;
  int samples = 0;
  for (int view = -180; view < 180; view += 5) {
    for (int vel = -180; vel < 180; vel += 5) {
      for (const int previous_side : {-1, 1}) {
        for (const StrafeIntent& intent : intents) {
          const float view_yaw = static_cast<float>(view);
          const float vel_yaw = static_cast<float>(vel);
          const StrafePlan legacy =
              cs2::control::legacy_plan(intent, view_yaw, vel_yaw, 250.0F, 46.875F, 30.0F);
          // The legacy rule **is** the old rule, in every single case.
          assert(legacy.side == old_side(legacy.goal_yaw, vel_yaw));
          // There is no dead zone and no hysteresis: the decision is always
          // decisive.
          assert(legacy.decisive);
          // The wish is the old wish.
          assert(nearly(legacy.wish_yaw,
                        cs2::control::normalize_angle(vel_yaw + static_cast<float>(legacy.side) *
                                                                    legacy.theta)));

          // The new rule would differ inside the dead zone, so the check
          // above is not vacuous: the difference is real and the legacy mode
          // has to be a path of its own.
          const StrafePlan current =
              cs2::control::plan(intent, view_yaw, vel_yaw, 250.0F, 46.875F, 30.0F, previous_side);
          if (current.side != legacy.side) {
            ++differences;
          }
          ++samples;
        }
      }
    }
  }
  assert(samples > 2000);
  // The dead zone really does change the decision in a good share of the
  // samples: this sweep measures 1521 of 31104 (the reviewer measured 509 of
  // 2596 with a coarser one), so the check above is not vacuous.
  assert(differences > samples / 50);
  assert(differences < samples);
}

// The legacy goal is the old goal: the camera's forward, or 180 degrees behind
// it when `S` is held.
void test_legacy_goal_is_the_old_goal() {
  for (int view = -180; view < 180; view += 5) {
    const float view_yaw = static_cast<float>(view);
    // `W` (and no key at all): the camera's forward.
    const float forward = cs2::control::normalize_angle(view_yaw);
    const StrafePlan with_w =
        cs2::control::legacy_plan(cs2::control::legacy_intent_from_buttons(button::k_forward),
                                  view_yaw, 0.0F, 250.0F, 46.875F, 30.0F);
    assert(nearly(with_w.goal_yaw, forward));
    const StrafePlan without = cs2::control::legacy_plan(
        cs2::control::legacy_intent_from_buttons(0), view_yaw, 0.0F, 250.0F, 46.875F, 30.0F);
    assert(nearly(without.goal_yaw, forward));
    // `S`: 180 degrees behind the camera.
    const StrafePlan with_s =
        cs2::control::legacy_plan(cs2::control::legacy_intent_from_buttons(button::k_back),
                                  view_yaw, 0.0F, 250.0F, 46.875F, 30.0F);
    assert(nearly(with_s.goal_yaw, cs2::control::normalize_angle(view_yaw + 180.0F)));
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

// --- the analog write --------------------------------------------------------
//
// The **value** the payload writes into the command's side axis is what
// multiplies into the engine's wish (`AirMove`, RVA `0x8e57b0`:
// `wish = forward * fmove + basis * leftmove`), so its sign decides which way
// the player is steered. The engine's side axis is **positive to the LEFT**
// (`docs/re/strafe-sign.md`), so a wish to the player's left needs a positive
// value — and the button bit that goes with it is `IN_MOVELEFT`.
//
// These are the two lines `Input::strafe_tick` writes (the analog path, the
// `subticks == 0` mode):
//
//   const float forward = std::cos(rot * k_deg_to_rad);
//   const float left = engine_side_axis_for_wish(rot);

//: The camera-frame analog written for a wish `rot` degrees counter-clockwise
//: of the view forward.
struct WrittenAxes {
  float forward{0.0F};
  float side{0.0F};
};

auto written_axes(float rot) -> WrittenAxes {
  const float rel = rot * cs2::control::k_deg_to_rad;
  WrittenAxes axes{};
  axes.forward = std::cos(rel);
  axes.side = cs2::control::engine_side_axis_for_wish(rot);
  return axes;
}

//: The dead band the payload applies to the axes (`k_move_epsilon`).
constexpr float k_move_epsilon = 0.01F;

void test_the_written_side_axis_carries_the_engine_sign() {
  // A wish 90 degrees to the player's LEFT: the side axis is **positive**.
  const WrittenAxes left = written_axes(90.0F);
  assert(nearly(left.side, 1.0F));
  // A wish 90 degrees to the right: **negative**.
  const WrittenAxes right = written_axes(-90.0F);
  assert(nearly(right.side, -1.0F));
  // The forward axis is the cosine of the offset (unchanged by the fix).
  assert(nearly(left.forward, 0.0F));
  assert(nearly(written_axes(0.0F).forward, 1.0F));
  assert(nearly(written_axes(0.0F).side, 0.0F));
  // A half-way wish keeps the sign of the side it leans to.
  assert(written_axes(45.0F).side > 0.0F);
  assert(written_axes(-45.0F).side < 0.0F);

  // The button bits agree with the value: a positive side axis is the player's
  // LEFT, so it goes with `IN_MOVELEFT`; a negative one with `IN_MOVERIGHT`.
  const std::uint64_t left_bits =
      cs2::control::engine_move_buttons(left.forward, left.side, k_move_epsilon);
  assert((left_bits & button::k_moveleft) != 0);
  assert((left_bits & button::k_moveright) == 0);
  const std::uint64_t right_bits =
      cs2::control::engine_move_buttons(right.forward, right.side, k_move_epsilon);
  assert((right_bits & button::k_moveright) != 0);
  assert((right_bits & button::k_moveleft) == 0);

  // And the engine's own wish construction agrees: build the wish the way
  // `AirMove` does — `forward * fmove + basis * leftmove`, with the engine's
  // second basis vector — and its heading must be the player's LEFT.
  for (const float view_yaw : {0.0F, 30.0F, -120.0F, 170.0F}) {
    const auto basis = cs2::control::engine_view_basis(view_yaw);
    const WrittenAxes axes = written_axes(90.0F);
    const float x = axes.forward * basis.forward.x + axes.side * basis.side.x;
    const float y = axes.forward * basis.forward.y + axes.side * basis.side.y;
    assert(nearly(cs2::control::heading_of(x, y), cs2::control::normalize_angle(view_yaw + 90.0F)));
  }
}

// --- the button names --------------------------------------------------------
//
// The engine's own input actions are named `left`/`right` (the **strafe**) and
// `turnleft`/`turnright` (the **look**) — `client.dll` registers them and its
// own button-name table gives the bits (`docs/re/strafe-sign.md` §6). The
// payload's name -> bit map has to agree, or the console path presses a
// command the engine does not know and the mask path sets the wrong bit.

void test_the_button_names_map_to_the_engine_bits() {
  // The strafe actions.
  assert(cs2::control::button_bit("left") == button::k_moveleft);
  assert(cs2::control::button_bit("right") == button::k_moveright);
  // The look actions — the *turn* bits, which CS2 spells `IN_TURNLEFT` /
  // `IN_TURNRIGHT` (there is no `IN_LEFT`/`IN_RIGHT`).
  assert(cs2::control::button_bit("turnleft") == button::k_turnleft);
  assert(cs2::control::button_bit("turnright") == button::k_turnright);
  // The bits themselves are the engine's own (`client.dll`'s button-name table
  // at `0x181b28cf0`).
  assert(button::k_moveleft == (1ULL << 9));    // IN_MOVELEFT
  assert(button::k_moveright == (1ULL << 10));  // IN_MOVERIGHT
  assert(button::k_turnleft == (1ULL << 7));    // IN_TURNLEFT
  assert(button::k_turnright == (1ULL << 8));   // IN_TURNRIGHT
  // The runner's aliases resolve to the same bits.
  assert(cs2::control::button_bit("a") == button::k_moveleft);
  assert(cs2::control::button_bit("d") == button::k_moveright);
  // "moveleft"/"moveright" are not engine names (CS2's console command is
  // `+left`), but the payload still accepts them as aliases.
  assert(cs2::control::button_bit("moveleft") == button::k_moveleft);
  assert(cs2::control::button_bit("moveright") == button::k_moveright);
  // An unknown name has no bit.
  assert(cs2::control::button_bit("no-such-button") == 0);
}

// --- the engine's button-bit table ------------------------------------------
//
// **The engine's own button-name table**, read from `client.dll` (build
// 25947352, sha256 `659940c1…`, image base `0x180000000`): the table at
// `0x181b28cf0` — 20 records of `{const char* name; uint64_t bit;}` — and
// the name strings at `0x181b28f70`. The engine uses that very table to
// force a button from the console (`in_forceinput <IN_NAME> 0/1`,
// `client.dll` `0x180b85b80`), so the bits below are the ones the engine
// itself puts in a command's button mask.
//
// It is pinned here **as data**, with the address it was read from: a game
// update that moves a bit then fails this test instead of silently changing
// what a button means.

struct EngineButton {
  const char* name;
  std::uint64_t bit;
};

constexpr EngineButton k_engine_buttons[] = {
    {.name = "IN_ALL", .bit = 0xffffffffffffffffULL},  // the "every button" mask
    {.name = "IN_ATTACK", .bit = 1ULL << 0},
    {.name = "IN_JUMP", .bit = 1ULL << 1},
    {.name = "IN_DUCK", .bit = 1ULL << 2},
    {.name = "IN_FORWARD", .bit = 1ULL << 3},
    {.name = "IN_BACK", .bit = 1ULL << 4},
    {.name = "IN_USE", .bit = 1ULL << 5},
    {.name = "IN_TURNLEFT", .bit = 1ULL << 7},
    {.name = "IN_TURNRIGHT", .bit = 1ULL << 8},
    {.name = "IN_MOVELEFT", .bit = 1ULL << 9},
    {.name = "IN_MOVERIGHT", .bit = 1ULL << 10},
    {.name = "IN_ATTACK2", .bit = 1ULL << 11},
    {.name = "IN_RELOAD", .bit = 1ULL << 13},
    {.name = "IN_SPEED", .bit = 1ULL << 16},
    {.name = "IN_JOYAUTOSPRINT", .bit = 1ULL << 17},
    {.name = "IN_FIRST_MOD_SPECIFIC_BIT", .bit = 1ULL << 32},  // the mod-specific range starts here
    {.name = "IN_USEORRELOAD", .bit = 1ULL << 32},
    {.name = "IN_SCORE", .bit = 1ULL << 33},
    {.name = "IN_ZOOM", .bit = 1ULL << 34},
    {.name = "IN_LOOK_AT_WEAPON", .bit = 1ULL << 35},
};

// The engine's **input actions** and the button mask each one puts in the
// command (`client.dll`: the console-command objects at `0x182233d80`ff carry
// their mask at `<object> - 8`). The look and strafe **analog** actions set no
// button at all — they are a float, not a bit.
struct EngineAction {
  const char* action;
  std::uint64_t bit;
};

constexpr EngineAction k_engine_actions[] = {
    {.action = "attack", .bit = 1ULL << 0},
    {.action = "jump", .bit = 1ULL << 1},
    {.action = "duck", .bit = 1ULL << 2},
    {.action = "forward", .bit = 1ULL << 3},
    {.action = "back", .bit = 1ULL << 4},
    {.action = "use", .bit = 1ULL << 5},
    {.action = "turnleft", .bit = 1ULL << 7},
    {.action = "turnright", .bit = 1ULL << 8},
    {.action = "left", .bit = 1ULL << 9},
    {.action = "right", .bit = 1ULL << 10},
    {.action = "attack2", .bit = 1ULL << 11},
    {.action = "reload", .bit = 1ULL << 13},
    {.action = "sprint", .bit = 1ULL << 16},
    {.action = "turnup", .bit = 0},
    {.action = "turndown", .bit = 0},
    {.action = "moveup", .bit = 0},
    {.action = "movedown", .bit = 0},
    {.action = "strafe", .bit = 0},
    {.action = "klook", .bit = 0},
};

// The repo's constants, by the engine's own name.
struct RepoButton {
  const char* engine_name;
  std::uint64_t bit;
};

constexpr RepoButton k_repo_buttons[] = {
    {.engine_name = "IN_ATTACK", .bit = button::k_attack},
    {.engine_name = "IN_JUMP", .bit = button::k_jump},
    {.engine_name = "IN_DUCK", .bit = button::k_duck},
    {.engine_name = "IN_FORWARD", .bit = button::k_forward},
    {.engine_name = "IN_BACK", .bit = button::k_back},
    {.engine_name = "IN_USE", .bit = button::k_use},
    {.engine_name = "IN_TURNLEFT", .bit = button::k_turnleft},
    {.engine_name = "IN_TURNRIGHT", .bit = button::k_turnright},
    {.engine_name = "IN_MOVELEFT", .bit = button::k_moveleft},
    {.engine_name = "IN_MOVERIGHT", .bit = button::k_moveright},
    {.engine_name = "IN_ATTACK2", .bit = button::k_attack2},
    {.engine_name = "IN_RELOAD", .bit = button::k_reload},
    {.engine_name = "IN_SPEED", .bit = button::k_speed},
    {.engine_name = "IN_SCORE", .bit = button::k_score},
    {.engine_name = "IN_ZOOM", .bit = button::k_zoom},
};

// The engine's bit for one `IN_*` name (0 when the name is unknown).
auto engine_bit(std::string_view name) -> std::uint64_t {
  for (const EngineButton& entry : k_engine_buttons) {
    if (name == entry.name) {
      return entry.bit;
    }
  }
  return 0;
}

// The engine's button mask for one input action (0 when it sets no button).
auto engine_action_bit(std::string_view action) -> std::uint64_t {
  for (const EngineAction& entry : k_engine_actions) {
    if (action == entry.action) {
      return entry.bit;
    }
  }
  return 0;
}

void test_the_button_bits_are_the_engine_bits() {
  // The table's shape is pinned too: a build that adds or drops a row has to
  // be looked at, not absorbed.
  assert(sizeof(k_engine_buttons) / sizeof(k_engine_buttons[0]) == 20);
  assert(sizeof(k_engine_actions) / sizeof(k_engine_actions[0]) == 19);

  // Every constant the repo defines is the engine's bit for that name — this
  // is the assertion that catches a wrong constant (the community's CS2 SDK
  // header still has the old `IN_SCORE` 16 / `IN_ZOOM` 19 / `IN_SPEED` 17).
  for (const RepoButton& repo : k_repo_buttons) {
    const std::uint64_t expected = engine_bit(repo.engine_name);
    assert(expected != 0);
    assert(repo.bit == expected);
  }

  // And no repo constant sits on a bit the engine does not name: a bit the
  // engine never reads is a bit that cannot mean anything.
  for (const RepoButton& repo : k_repo_buttons) {
    bool named = false;
    for (const EngineButton& entry : k_engine_buttons) {
      if (entry.bit == repo.bit) {
        named = true;
        break;
      }
    }
    assert(named);
  }

  // The bits that were wrong (T14): the engine moved `IN_SCORE` to 33 and
  // `IN_ZOOM` to 34, and `IN_SPEED` is 16 (bit 17 is `IN_JOYAUTOSPRINT`).
  assert(button::k_score == (1ULL << 33));
  assert(button::k_zoom == (1ULL << 34));
  assert(button::k_speed == (1ULL << 16));
  assert(engine_bit("IN_JOYAUTOSPRINT") == (1ULL << 17));

  // The engine's **action** table is a second, independent reading of the same
  // bits (the console-command objects carry the mask themselves), so the two
  // must agree — and the console command's action is what the runner's
  // `BUTTONS` table drives.
  assert(engine_action_bit("forward") == button::k_forward);
  assert(engine_action_bit("back") == button::k_back);
  assert(engine_action_bit("left") == button::k_moveleft);
  assert(engine_action_bit("right") == button::k_moveright);
  assert(engine_action_bit("attack") == button::k_attack);
  assert(engine_action_bit("attack2") == button::k_attack2);
  assert(engine_action_bit("jump") == button::k_jump);
  assert(engine_action_bit("duck") == button::k_duck);
  assert(engine_action_bit("use") == button::k_use);
  assert(engine_action_bit("reload") == button::k_reload);
  assert(engine_action_bit("turnleft") == button::k_turnleft);
  assert(engine_action_bit("turnright") == button::k_turnright);
  // The shift key is the **sprint** key, and `sprint` carries `IN_SPEED`.
  assert(engine_action_bit("sprint") == button::k_speed);
  // The pitch/strafe analog actions set no button at all.
  assert(engine_action_bit("turnup") == 0);
  assert(engine_action_bit("turndown") == 0);

  // Every name the payload's mask accepts resolves to a bit the engine names
  // — so the mask can never set a bit the engine does not read.
  for (const cs2::control::NamedButton& entry : cs2::control::k_named_buttons) {
    bool named = false;
    for (const EngineButton& engine : k_engine_buttons) {
      if (engine.bit == entry.bit) {
        named = true;
        break;
      }
    }
    assert(named);
  }
  // The engine's own action names are in the mask's map too, with the engine's
  // bits.
  assert(cs2::control::button_bit("sprint") == button::k_speed);
  assert(cs2::control::button_bit("speed") == button::k_speed);
  assert(cs2::control::button_bit("shift") == button::k_speed);
  assert(cs2::control::button_bit("zoom") == button::k_zoom);
  // The Source-1 buttons CS2 dropped are not accepted any more.
  assert(cs2::control::button_bit("run") == 0);
  assert(cs2::control::button_bit("walk") == 0);
}

// --- the steering verdict ------------------------------------------------------

// The verdict is what tells the operator whether the silent encoding can work
// at all on this server: the engine turns the velocity toward the wish when it
// applies the basis rotation, and does not turn it at all when the server
// ignores the sub-tick view angles (`sv_subtick_movement_view_angles 0`).
void test_steering_verdict() {
  using cs2::control::SteerVerdict;
  // The velocity turned toward the wish (the side is +1 = counter-clockwise):
  // the rotation was applied.
  assert(cs2::control::classify_steer(100.0F, 104.0F, 1, 84.0F) == SteerVerdict::followed);
  // The other way round: the velocity turned clockwise on a counter-clockwise
  // wish — as wrong as no turn at all.
  assert(cs2::control::classify_steer(100.0F, 96.0F, 1, 84.0F) == SteerVerdict::ignored);
  // The clockwise wish and a clockwise turn: followed.
  assert(cs2::control::classify_steer(100.0F, 96.0F, -1, 84.0F) == SteerVerdict::followed);
  // The velocity did not move at all: the engine ignored the rotation.
  assert(cs2::control::classify_steer(100.0F, 100.0F, 1, 84.0F) == SteerVerdict::ignored);
  // The noise floor: a turn below the threshold is not a turn.
  assert(cs2::control::classify_steer(100.0F, 100.1F, 1, 84.0F) == SteerVerdict::ignored);
  // Nothing was asked for: the verdict is unknown, not "ignored".
  assert(cs2::control::classify_steer(100.0F, 100.0F, 1, 0.0F) == SteerVerdict::unknown);
  assert(cs2::control::classify_steer(100.0F, 100.0F, 1, 2.0F) == SteerVerdict::unknown);
  // The wrap around 180 degrees counts as a turn (the headings are angles):
  // -179 -> 179 is two degrees **clockwise**, 179 -> -179 is two degrees
  // counter-clockwise.
  assert(cs2::control::classify_steer(-179.0F, 179.0F, 1, 84.0F) == SteerVerdict::ignored);
  assert(cs2::control::classify_steer(179.0F, -179.0F, 1, 84.0F) == SteerVerdict::followed);
  // The thresholds are what the payload uses.
  assert(cs2::control::k_strafe_min_requested_turn_deg == 5.0F);
  assert(cs2::control::k_strafe_min_applied_turn_deg == 0.25F);
}

}  // namespace

auto main() -> int {
  test_intent_decoding();  test_goal_is_the_camera_forward();
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
  test_normalize_angle_range();
  test_legacy_mode_is_the_old_rule();
  test_legacy_goal_is_the_old_goal();
  test_side_axis_convention_is_pinned();
  test_the_goal_matches_the_engine_wish();
  test_the_goal_headings_are_the_players_left_and_right();
  test_the_written_side_axis_carries_the_engine_sign();
  test_the_button_names_map_to_the_engine_bits();
  test_the_button_bits_are_the_engine_bits();
  test_steering_verdict();
  std::puts("control-strafe: ok");
  return 0;
}
