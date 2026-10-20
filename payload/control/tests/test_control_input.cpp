// Unit tests for the input **backend's button gate** and for the ragebot's
// auto-stop counter-wish (`cs2/control/strafe_geometry.hpp`).
//
// Both are pure functions of the backend and of the geometry, so they can be
// checked host-side. They live in functions at all because the failures they
// guard against are invisible in a live game:
//
//   * the triggerbot's press used to travel through the **control channel's**
//     mask, which the `console` backend (the default) never applies to the
//     command: the trigger decided to fire, counted the shot and answered
//     `{"firing": true}` while the gun stayed silent;
//   * the auto-stop's counter-wish wrote the **inverted** side axis, so the
//     "brake" pushed the player the way they were already moving — and its
//     forward axis was the sign of `cos(90 degrees)`, which in float is
//     `-4.37e-8`, so the brake pushed the player *backwards* instead of
//     sideways.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "cs2/control/strafe_geometry.hpp"

namespace {

using cs2::control::ButtonPath;
namespace button = cs2::control::button;

constexpr float k_epsilon = 0.001F;
// The dead band the payload applies to the move axes (`k_move_epsilon`).
constexpr float k_move_epsilon = 0.01F;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

// --- the backend's button gate ------------------------------------------------

// A **feature's** button reaches the command in every backend.
//
// The features (the triggerbot, the ragebot's fire, the movement assists) have
// no console equivalent: the `console` backend drives the engine's own
// `+button` commands, so a feature cannot be expressed through it. The
// triggerbot's press used to go through the control channel's mask, which is
// applied only in the `usercmd` backend — so in the default backend the
// trigger fired on paper and not in the game.
void test_a_features_button_reaches_the_command_in_every_backend() {
  assert(cs2::control::button_reaches_command(ButtonPath::feature, false));
  assert(cs2::control::button_reaches_command(ButtonPath::feature, true));
}

// A **control command's** button reaches the command only in the `usercmd`
// backend: in the `console` backend the runner's own `exec` does it, and the
// command must be left alone.
void test_a_control_commands_button_needs_the_usercmd_backend() {
  assert(!cs2::control::button_reaches_command(ButtonPath::control_command, false));
  assert(cs2::control::button_reaches_command(ButtonPath::control_command, true));
}

// The composed mask: the engine's own bits, the control command's bits and the
// features' bits.
void test_the_composed_mask_gates_the_control_path_only() {
  const std::uint64_t engine = button::k_jump;
  const std::uint64_t control = button::k_use;

  // The `console` backend: the control command's button is not applied, the
  // feature's is.
  const std::uint64_t console =
      cs2::control::compose_button_mask(engine, control, 0, button::k_attack, false);
  assert((console & button::k_jump) != 0);
  assert((console & button::k_use) == 0);
  assert((console & button::k_attack) != 0);

  // The `usercmd` backend: both are applied.
  const std::uint64_t usercmd =
      cs2::control::compose_button_mask(engine, control, 0, button::k_attack, true);
  assert((usercmd & button::k_jump) != 0);
  assert((usercmd & button::k_use) != 0);
  assert((usercmd & button::k_attack) != 0);

  // The control channel's **clear** mask still clears its own bits, and the
  // feature's bit survives it: a feature's release must not be able to take
  // the player's own press of the same button away, and the other way round.
  const std::uint64_t cleared =
      cs2::control::compose_button_mask(engine, 0, control, button::k_attack, true);
  assert((cleared & button::k_use) == 0);
  assert((cleared & button::k_attack) != 0);
}

// --- the auto-stop's counter-wish ---------------------------------------------

// The counter-wish at 90 degrees is **lateral only**: the forward axis is zero
// (not `-1`), because the sign of `cos(90 degrees)` in float is `-4.37e-8` and
// the brake must not push the player backwards.
void test_the_counter_wish_is_lateral_at_ninety_degrees() {
  const auto left = cs2::control::counter_wish_for(90.0F);
  assert(nearly(left.forward, 0.0F));
  assert(nearly(left.side, 1.0F));

  const auto right = cs2::control::counter_wish_for(-90.0F);
  assert(nearly(right.forward, 0.0F));
  assert(nearly(right.side, -1.0F));
}

// The **sign** of the side axis: the counter-wish's component along the aim's
// **left** vector is `+sin(rel)`, and the engine's `leftmove` is positive to
// the left. A counter-wish 90 degrees to the player's left therefore needs a
// **positive** side axis — the old code wrote the exact negative, so the
// brake pushed the player the way they were already moving.
void test_the_counter_wish_side_carries_the_engine_sign() {
  assert(nearly(cs2::control::counter_wish_for(90.0F).side, 1.0F));
  assert(nearly(cs2::control::counter_wish_for(-90.0F).side, -1.0F));
  // The same fact, read off the convention helper the counter-wish goes
  // through.
  assert(nearly(cs2::control::counter_wish_for(90.0F).side,
                cs2::control::engine_side_axis_for_wish(90.0F)));
  assert(nearly(cs2::control::counter_wish_for(-90.0F).side,
                cs2::control::engine_side_axis_for_wish(-90.0F)));
}

// The button bits agree with the value: a positive side axis is the player's
// LEFT, so it goes with `IN_MOVELEFT`; a negative one with `IN_MOVERIGHT`.
void test_the_counter_wish_buttons_agree_with_the_value() {
  const auto left = cs2::control::counter_wish_for(90.0F);
  const std::uint64_t left_bits =
      cs2::control::engine_move_buttons(left.forward, left.side, k_move_epsilon);
  assert((left_bits & button::k_moveleft) != 0);
  assert((left_bits & button::k_moveright) == 0);
  // A lateral brake presses no forward/back button.
  assert((left_bits & (button::k_forward | button::k_back)) == 0);

  const auto right = cs2::control::counter_wish_for(-90.0F);
  const std::uint64_t right_bits =
      cs2::control::engine_move_buttons(right.forward, right.side, k_move_epsilon);
  assert((right_bits & button::k_moveright) != 0);
  assert((right_bits & button::k_moveleft) == 0);
}

// Along the aim the wish is a plain forward/back brake, with no lateral part.
void test_the_counter_wish_along_the_aim() {
  const auto ahead = cs2::control::counter_wish_for(0.0F);
  assert(nearly(ahead.forward, 1.0F));
  assert(nearly(ahead.side, 0.0F));

  const auto behind = cs2::control::counter_wish_for(180.0F);
  assert(nearly(behind.forward, -1.0F));
  assert(nearly(behind.side, 0.0F));
}

// The dead zone: a component that is only the rounding noise is dropped, not
// signed. `cos(90 degrees)` in float is `-4.37e-8`, and the old code's
// `sign()` of it was `-1` — the brake pushed the player backwards.
void test_the_counter_wish_drops_the_rounding_noise() {
  const float noise = std::cos(90.0F * cs2::control::k_deg_to_rad);
  assert(noise < 0.0F);  // the noise the old code signed
  assert(nearly(cs2::control::signed_axis(noise, cs2::control::k_counter_wish_deadzone), 0.0F));
  assert(nearly(cs2::control::signed_axis(0.5F, cs2::control::k_counter_wish_deadzone), 1.0F));
  assert(nearly(cs2::control::signed_axis(-0.5F, cs2::control::k_counter_wish_deadzone), -1.0F));
}

}  // namespace

auto main() -> int {
  test_a_features_button_reaches_the_command_in_every_backend();
  test_a_control_commands_button_needs_the_usercmd_backend();
  test_the_composed_mask_gates_the_control_path_only();

  test_the_counter_wish_is_lateral_at_ninety_degrees();
  test_the_counter_wish_side_carries_the_engine_sign();
  test_the_counter_wish_buttons_agree_with_the_value();
  test_the_counter_wish_along_the_aim();
  test_the_counter_wish_drops_the_rounding_noise();

  std::puts("control-input: ok");
  return 0;
}
