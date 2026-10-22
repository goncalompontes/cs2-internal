// Unit tests for the input **backend's button gate**, for the ragebot's
// auto-stop counter-wish and for the **fire path** (the edge and the gate).
//
// All of them are pure functions of the backend, of the geometry and of the
// weapon's capability, so they can be checked host-side. They live in
// functions at all because the failures they guard against are invisible in a
// live game:
//
//   * the triggerbot's press used to travel through the **control channel's**
//     mask, which the `console` backend (the default) never applies to the
//     command: the trigger decided to fire, counted the shot and answered
//     `{"firing": true}` while the gun stayed silent;
//   * the auto-stop's counter-wish wrote the **inverted** side axis, so the
//     "brake" pushed the player the way they were already moving — and its
//     forward axis was the sign of `cos(90 degrees)`, which in float is
//     `-4.37e-8`, so the brake pushed the player *backwards* instead of
//     sideways;
//   * the ragebot held `IN_ATTACK` down on **every** command, so a
//     **semi-automatic** weapon (a pistol, the R8, the scout) fired **once**
//     and never again: the engine needs a release->re-press **edge** between
//     two shots.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "cs2/control/strafe_geometry.hpp"

namespace {

using cs2::control::ButtonPath;
namespace button = cs2::control::button;

constexpr float k_epsilon = 0.001F;
// The dead band the payload applies to the move axes (`k_move_epsilon`).
constexpr float k_move_epsilon = 0.01F;
// One command tick (the game runs at 64 ticks per second).
constexpr float k_tick = 1.0F / 64.0F;

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

// --- The fire path -------------------------------------------------------------
//
// The ragebot's fire: the **edge** (a semi-automatic weapon needs the attack
// released between two shots) and the **gate** (can the weapon take the shot
// at all?).

using cs2::control::FireCapability;
using cs2::control::FireInputs;
using cs2::control::FireMode;
using cs2::control::FireRefusal;

// A weapon that can shoot: a full clip, not reloading, not deploying, and the
// engine's own record says it may fire again.
auto ready_weapon(FireMode mode) -> FireCapability {
  FireCapability capability{};
  capability.mode = mode;
  capability.clip = 30;
  capability.ready = true;
  capability.ready_known = true;
  capability.interval = 0.1F;
  return capability;
}

// The inputs of one command: the ragebot wants to fire, and the weapon is
// `elapsed` seconds past the ragebot's last press.
auto fire_inputs(FireMode mode, float elapsed) -> FireInputs {
  FireInputs inputs{};
  inputs.want = true;
  inputs.capability = ready_weapon(mode);
  inputs.elapsed = elapsed;
  return inputs;
}

// The **fire mode**: the weapon's VData says whether the weapon is full
// automatic. A weapon whose VData could not be read is **unknown**.
void test_the_fire_mode_is_classified() {
  assert(cs2::control::classify_fire_mode(true, true) == FireMode::full_auto);
  assert(cs2::control::classify_fire_mode(true, false) == FireMode::semi);
  assert(cs2::control::classify_fire_mode(false, true) == FireMode::unknown);
  assert(cs2::control::classify_fire_mode(false, false) == FireMode::unknown);
}

// The **unknown** mode degrades to the conservative path: an unknown weapon
// behaves exactly like a semi-automatic one (the edge), and the status says
// "unknown" — never "auto", which would be the unsafe assumption.
void test_the_unknown_fire_mode_is_conservative_and_reported() {
  const auto unknown = cs2::control::decide_fire(fire_inputs(FireMode::unknown, 0.0F));
  const auto semi = cs2::control::decide_fire(fire_inputs(FireMode::semi, 0.0F));
  assert(unknown.press == semi.press);
  assert(!unknown.press);  // the release the edge needs
  assert(unknown.refusal == semi.refusal);
  // The mode is **not** guessed: the status names it "unknown".
  assert(std::string_view(cs2::control::fire_mode_name(FireMode::unknown)) == "unknown");
  assert(std::string_view(cs2::control::fire_mode_name(FireMode::semi)) == "semi");
  assert(std::string_view(cs2::control::fire_mode_name(FireMode::full_auto)) == "auto");
}

// The **edge**: on a semi-automatic weapon the ragebot's two shots are two
// release->re-press edges. The engine must see the attack **released**
// between them, or the weapon fires once and never again.
void test_a_semi_weapon_gets_a_release_between_two_shots() {
  // The first shot: the weapon is ready and the ragebot has not pressed yet.
  const auto first = cs2::control::decide_fire(fire_inputs(FireMode::semi, 10.0F));
  assert(first.press);
  assert(first.refusal == FireRefusal::none);

  // The next command: the ragebot has just pressed, so the attack must be
  // **released** — the edge's release half.
  const auto release = cs2::control::decide_fire(fire_inputs(FireMode::semi, 0.0F));
  assert(!release.press);
  assert(release.refusal == FireRefusal::edge);

  // And the command after that: the weapon is ready again, so the ragebot
  // presses — the edge's second half, a **new** shot.
  const auto second = cs2::control::decide_fire(fire_inputs(FireMode::semi, 10.0F));
  assert(second.press);
  assert(second.refusal == FireRefusal::none);
}

// The whole sequence, driven through the decision: a semi-automatic weapon
// sees press -> release -> press, so every shot after the first is a new
// edge. The release must be visible on a command the engine simulates.
void test_the_semi_weapon_sequence_carries_the_edge() {
  float elapsed = 10.0F;  // the ragebot has not pressed yet
  bool pressed = false;
  int presses = 0;
  int releases = 0;
  for (int tick = 0; tick < 8; ++tick) {
    const auto decision = cs2::control::decide_fire(fire_inputs(FireMode::semi, elapsed));
    if (decision.press) {
      ++presses;
      elapsed = 0.0F;  // the press restarts the clock
      pressed = true;
    } else {
      if (pressed) {
        ++releases;
        pressed = false;
      }
      elapsed += k_tick;
    }
  }
  assert(presses >= 2);
  assert(releases >= 1);
}

// A **full-automatic** weapon fires while the trigger is held: the engine's
// own rate governs it, so the ragebot does not withdraw the attack between
// the shots (that would only cost a tick per shot).
void test_a_full_auto_weapon_keeps_the_trigger_down() {
  const auto held = cs2::control::decide_fire(fire_inputs(FireMode::full_auto, 0.0F));
  assert(held.press);
  assert(held.refusal == FireRefusal::none);
}

// The **gate**: each blocking condition blocks, and the empty-conditions case
// does not.
void test_the_gate_blocks_each_condition() {
  // Nothing blocks: the shot can be taken.
  const auto ok = cs2::control::decide_fire(fire_inputs(FireMode::semi, 10.0F));
  assert(ok.press);
  assert(ok.refusal == FireRefusal::none);

  // The clip is empty.
  auto inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.clip = 0;
  const auto no_ammo = cs2::control::decide_fire(inputs);
  assert(!no_ammo.press);
  assert(no_ammo.refusal == FireRefusal::no_ammo);

  // The weapon is reloading.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.reloading = true;
  const auto reloading = cs2::control::decide_fire(inputs);
  assert(!reloading.press);
  assert(reloading.refusal == FireRefusal::reloading);

  // The weapon is still being deployed.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.deploying = true;
  const auto deploying = cs2::control::decide_fire(inputs);
  assert(!deploying.press);
  assert(deploying.refusal == FireRefusal::deploying);

  // The weapon's own record says it may not fire again: the fire interval has
  // not elapsed. A press here would be **the shot being wasted**.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.ready = false;
  const auto interval = cs2::control::decide_fire(inputs);
  assert(!interval.press);
  assert(interval.refusal == FireRefusal::interval);

  // The engine's record could not be read: the VData's cycle time is the
  // fallback, and the same interval blocks.
  inputs = fire_inputs(FireMode::semi, 0.05F);
  inputs.capability.ready_known = false;
  inputs.capability.interval = 0.1F;
  const auto fallback = cs2::control::decide_fire(inputs);
  assert(!fallback.press);
  assert(fallback.refusal == FireRefusal::interval);
  // ... and once the cycle time has elapsed, the shot is taken again.
  inputs.elapsed = 0.2F;
  assert(cs2::control::decide_fire(inputs).press);

  // No solve: the ragebot is not aiming at anybody.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.want = false;
  const auto no_solve = cs2::control::decide_fire(inputs);
  assert(!no_solve.press);
  assert(no_solve.refusal == FireRefusal::no_solve);
}

// The **player's own press** is never cleared by the ragebot.
//
// The features' bits are only ever **set**, never cleared: a feature that
// releases the attack button must not take the player's own press of the same
// button away. The ragebot's release is scoped to **its own contribution** —
// it withdraws the bit it asserted and leaves the engine's own mask alone.
void test_the_ragebot_never_clears_the_players_own_attack() {
  // The engine's own mask: the player is holding the attack button.
  const std::uint64_t engine = button::k_attack;
  // The ragebot withdraws its contribution: the feature mask carries no
  // attack bit of its own.
  const std::uint64_t withdrawn = cs2::control::compose_button_mask(engine, 0, 0, 0, false);
  assert((withdrawn & button::k_attack) != 0);  // the player's press survives
  // The composition only ever **adds**: nothing the payload does can take a
  // bit the engine already had away.
  assert((withdrawn & engine) == engine);

  // And the ragebot's own press adds the bit on top of the player's.
  const std::uint64_t pressed =
      cs2::control::compose_button_mask(engine, 0, 0, button::k_attack, false);
  assert((pressed & button::k_attack) != 0);
  assert((pressed & engine) == engine);

  // The triggerbot's own hold is a **separate** contribution: the ragebot's
  // withdrawal must not take it away either.
  const std::uint64_t trigger_held =
      cs2::control::compose_button_mask(0, 0, 0, button::k_attack, false);
  assert((trigger_held & button::k_attack) != 0);
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

  test_the_fire_mode_is_classified();
  test_the_unknown_fire_mode_is_conservative_and_reported();
  test_a_semi_weapon_gets_a_release_between_two_shots();
  test_the_semi_weapon_sequence_carries_the_edge();
  test_a_full_auto_weapon_keeps_the_trigger_down();
  test_the_gate_blocks_each_condition();
  test_the_ragebot_never_clears_the_players_own_attack();

  std::puts("control-input: ok");
  return 0;
}
