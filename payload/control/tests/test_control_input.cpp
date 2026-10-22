// Unit tests for the input **backend's button gate**, for the ragebot's
// auto-stop counter-wish and for the **fire path** (the edge, the gate and the
// write).
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
//
// The fire path's tests drive the **whole wiring** — the decision, the write
// and the command cache — and one of them reads the payload's own source, so
// a break in the payload's call sites fails here too.

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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
// released between two shots), the **gate** (can the weapon take the shot at
// all?) and the **write** (the decision on the command's bytes).
//
// The tests here drive the **whole wiring**, not a neighbour of it: the
// decision (`decide_fire`), the write (`compose_fire_write`) and the command
// cache (`FireCommandCache`) are the payload's own functions, and the payload
// calls them — so a break in any of them fails here.

using cs2::control::AttackContributors;
using cs2::control::ClipSample;
using cs2::control::FireCapability;
using cs2::control::FireCommandCache;
using cs2::control::FireDecision;
using cs2::control::FireInputs;
using cs2::control::FireMode;
using cs2::control::FireRefusal;
using cs2::control::FireWrite;
using cs2::control::FireWriteInputs;

// The HUD's rage line buffer (`payload/features/src/overlay.cpp`): the line
// has to fit it, in the **worst** case.
constexpr std::size_t k_status_buffer = 192;

// A weapon that can shoot: a full clip, not reloading, not deploying, and the
// engine's own record says it may fire again.
auto ready_weapon(FireMode mode) -> FireCapability {
  FireCapability capability{};
  capability.mode = mode;
  capability.clip = 30;
  capability.clip_known = true;
  capability.ready = true;
  capability.ready_known = true;
  capability.interval = 0.1F;
  return capability;
}

// The inputs of one command: the ragebot has a solve, its fire is enabled, and
// the weapon is `elapsed` seconds past the ragebot's last press.
auto fire_inputs(FireMode mode, float elapsed) -> FireInputs {
  FireInputs inputs{};
  inputs.solved = true;
  inputs.enabled = true;
  inputs.capability = ready_weapon(mode);
  inputs.elapsed = elapsed;
  return inputs;
}

// The payload's **per-tick loop**, as the host test drives it.
//
// The loop is the wiring the payload runs: the fire path's decision goes in,
// the command's mask and the PB's words come out, and the next tick sees the
// command the payload has just written. Without the loop the tests would
// exercise a *snapshot* of the payload, not the payload.
struct FireLoop {
  // The command's mask, as the payload leaves it.
  std::uint64_t command{0};
  // The PB's words, as the payload leaves them.
  std::uint64_t pb_held{0};
  std::uint64_t pb_changed{0};
  // The **player's own** press of the attack button (the engine's own mask).
  bool player_attack{false};
  // The control channel's own press.
  bool control_attack{false};
  // The features' bits (the triggerbot's hold).
  std::uint64_t feature_mask{0};
  std::uint64_t control_set{0};
  std::uint64_t control_clear{0};

  auto tick(bool rage_press, bool attack_changed) -> FireWrite {
    const FireWriteInputs in{
        .current_mask = command,
        .control_set = control_set,
        .control_clear = control_clear,
        .feature_mask = feature_mask,
        .usercmd_backend = true,
        .player_attack = player_attack,
        .control_attack = control_attack,
        .rage_press = rage_press,
        .attack_changed = attack_changed,
        .assists = 0,
        .pb_held = pb_held,
        .pb_changed = pb_changed,
    };
    const FireWrite write = cs2::control::compose_fire_write(in);
    command = write.command_mask;
    pb_held = write.pb_held;
    pb_changed = write.pb_changed;
    return write;
  }
};

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

  // The clip could **not** be read: that is not an empty clip, and the gate
  // refuses the shot (*when in doubt, do not fire*).
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.clip_known = false;
  const auto clip_unknown = cs2::control::decide_fire(inputs);
  assert(!clip_unknown.press);
  assert(clip_unknown.refusal == FireRefusal::clip_unknown);

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

  // **Neither** source of the rate could be read: the refusal says so, so the
  // operator is not sent after a rate that is not the reason.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.capability.ready_known = false;
  inputs.capability.interval = 0.0F;
  const auto rate_unknown = cs2::control::decide_fire(inputs);
  assert(!rate_unknown.press);
  assert(rate_unknown.refusal == FireRefusal::rate_unknown);

  // No solve: the ragebot is not aiming at anybody.
  inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.solved = false;
  const auto no_solve = cs2::control::decide_fire(inputs);
  assert(!no_solve.press);
  assert(no_solve.refusal == FireRefusal::no_solve);
}

// A **disabled** fire is not a **missing solve**: the operator has to be able
// to tell the two apart (the fire toggle is off, or the visibility gate
// refused the shot).
void test_the_disabled_fire_is_not_a_missing_solve() {
  auto inputs = fire_inputs(FireMode::semi, 10.0F);
  inputs.enabled = false;
  const auto disabled = cs2::control::decide_fire(inputs);
  assert(!disabled.press);
  assert(disabled.refusal == FireRefusal::fire_off);
  assert(disabled.refusal != FireRefusal::no_solve);

  // The two refusals have their own names, so the status says which one it is.
  assert(std::string_view(cs2::control::fire_refusal_name(FireRefusal::fire_off)) == "fire-off");
  assert(std::string_view(cs2::control::fire_refusal_name(FireRefusal::no_solve)) == "no-solve");
}

// The **edge on the command**: the payload's own press is **replaceable**.
//
// This is the whole point of the fire path. On a semi-automatic weapon the
// engine must see the attack **released** between two shots: the command the
// engine simulates must **not** carry the attack bit the previous tick's
// command carried.
//
// The drive is the payload's own loop: the fire path presses, the command
// carries the attack bit, the fire path withdraws — and the **same** command
// (the one the payload has just written to) must **not** carry the bit any
// more.
void test_the_payload_can_take_its_own_press_back() {
  FireLoop loop{};
  // Tick 1: the fire path presses.
  const auto press = loop.tick(true, true);
  assert((press.command_mask & button::k_attack) != 0);
  assert((loop.command & button::k_attack) != 0);
  // The PB's held word follows the command's mask.
  assert((press.pb_held & button::k_attack) != 0);

  // Tick 2: the fire path withdraws — the edge's release half.
  const auto release = loop.tick(false, true);
  // **The command does not carry the attack bit any more.** That is the
  // release: the engine sees the trigger go up.
  assert((release.command_mask & button::k_attack) == 0);
  assert((loop.command & button::k_attack) == 0);
  // The PB's held word follows, so the release is on that channel too.
  assert((release.pb_held & button::k_attack) == 0);
  // The movement services' held word is **cleared**: the release is on that
  // channel too.
  assert((release.engine_clear & button::k_attack) != 0);
  assert((release.engine_set & button::k_attack) == 0);
  // The PB's changed word carries the release: a release is a change too.
  assert((release.pb_changed & button::k_attack) != 0);

  // Tick 3: the fire path presses again — the edge's second half, a **new**
  // shot.
  const auto again = loop.tick(true, true);
  assert((again.command_mask & button::k_attack) != 0);
  assert((loop.command & button::k_attack) != 0);

  // The whole sequence: the command's attack bit goes 1 -> 0 -> 1.
  FireLoop sequence{};
  std::vector<bool> bits{};
  bits.push_back((sequence.tick(true, true).command_mask & button::k_attack) != 0);
  bits.push_back((sequence.tick(false, true).command_mask & button::k_attack) != 0);
  bits.push_back((sequence.tick(true, true).command_mask & button::k_attack) != 0);
  assert(bits.size() == 3);
  assert(bits[0]);
  assert(!bits[1]);  // the release
  assert(bits[2]);   // the re-press
}

// The **player's own press** is never cleared by the ragebot.
//
// The features' bits are only ever **set**, never cleared: a feature that
// releases the attack button must not take the player's own press of the same
// button away. The ragebot's release is scoped to **its own contribution** —
// it withdraws the bit it asserted and leaves the player's alone.
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

  // **The invariant itself**, for every feature mask: the composition adds
  // the features' bits and never clears one. A composition that cleared them
  // would take the player's own press of the attack button away — the rule
  // the ragebot's scoped release rests on.
  const std::uint64_t features[] = {0ULL, button::k_attack, button::k_jump, ~0ULL};
  for (const std::uint64_t bits : features) {
    const std::uint64_t mask = cs2::control::compose_button_mask(engine, 0, 0, bits, false);
    assert((mask & engine) == engine);
    assert((mask & bits) == bits);  // the feature's own bits are there
  }
}

// The **write**, with the player holding the trigger: the ragebot's
// withdrawal must not take the player's press away.
//
// This is the unscoped release the repo's rule forbids: a composition that
// computed the attack bit from the ragebot's own bit alone would clear the
// player's press, and the semi-automatic weapon would never fire again.
void test_the_players_press_survives_the_ragebots_withdrawal() {
  FireLoop loop{};
  loop.player_attack = true;
  // The ragebot presses.
  const auto press = loop.tick(true, true);
  assert((press.command_mask & button::k_attack) != 0);
  // The ragebot withdraws — but the player is holding the trigger.
  const auto release = loop.tick(false, true);
  // The player's press is still on the command: the engine sees one continuous
  // press, so the edge is the **player's** to make.
  assert((release.command_mask & button::k_attack) != 0);
  assert((release.pb_held & button::k_attack) != 0);
  // ... and the payload did not ask the engine to clear it.
  assert((release.engine_clear & button::k_attack) == 0);
  assert((release.engine_set & button::k_attack) != 0);
}

// The **triggerbot's** hold and the **control channel's** press are
// contributors of their own: the ragebot's withdrawal takes neither away.
void test_the_other_contributors_survive_the_withdrawal() {
  // The triggerbot holds the attack button (a feature bit).
  FireLoop trigger{};
  trigger.feature_mask = button::k_attack;
  const auto held = trigger.tick(false, true);
  assert((held.command_mask & button::k_attack) != 0);

  // The control channel pressed the attack button (`press("attack")`).
  FireLoop control{};
  control.control_set = button::k_attack;
  control.control_attack = true;
  const auto pressed = control.tick(false, true);
  assert((pressed.command_mask & button::k_attack) != 0);

  // The control channel's own **release** takes the button away entirely —
  // that is the operator's release, and it outranks the contributors.
  FireLoop released{};
  released.control_clear = button::k_attack;
  const auto cleared = released.tick(true, true);
  assert((cleared.command_mask & button::k_attack) == 0);
}

// The **command cache**: the engine's own attack bit is read **per command**,
// so the payload never reads **its own** press back as the player's.
//
// The payload writes to two commands per tick — the slot-25 copy and the
// slot-5 command the engine has just built — and the engine reuses the
// command objects: a command the payload wrote last tick still carries the
// payload's own press.
void test_the_command_cache_reads_the_players_press() {
  FireCommandCache cache{};
  // The engine has just built command 1: the player is holding the trigger.
  const auto first = cache.read(1, true, true);
  assert(first.player_attack);
  // The payload puts its own press on it.
  cache.wrote(1, true);
  // The same command, read again (the slot-25 copy): the mask now carries the
  // payload's press as well, so the reading is **not** refreshed — the cached
  // one is used.
  const auto second = cache.read(1, true, false);
  assert(second.player_attack);
  // A **new** command (the engine builds one every tick): the reading is
  // clean again, and it follows the player's trigger.
  const auto third = cache.read(2, false, true);
  assert(!third.player_attack);
  // And the copy of that command reuses the tick's reading.
  const auto fourth = cache.read(2, false, false);
  assert(!fourth.player_attack);
}

// The **engine's shots**: the clip's fall is a shot — but a **reload** and a
// **failed read** are not.
void test_the_engine_shot_counter_ignores_a_reload_and_a_failed_read() {
  // A round left the magazine: that is the engine's shot.
  const ClipSample before{.known = true, .clip = 30, .reloading = false};
  const ClipSample after{.known = true, .clip = 29, .reloading = false};
  assert(cs2::control::is_engine_shot(before, after));

  // A **reload** empties the magazine: not a shot.
  const ClipSample reloading{.known = true, .clip = 0, .reloading = true};
  assert(!cs2::control::is_engine_shot(before, reloading));
  // ... and the tick **after** the reload is not a shot either.
  const ClipSample loaded{.known = true, .clip = 30, .reloading = false};
  assert(!cs2::control::is_engine_shot(reloading, loaded));

  // A **failed read** reads as zero: that is not a shot.
  const ClipSample failed{.known = false, .clip = 0, .reloading = false};
  assert(!cs2::control::is_engine_shot(before, failed));
  assert(!cs2::control::is_engine_shot(failed, before));

  // The magazine did not shrink: not a shot.
  assert(!cs2::control::is_engine_shot(before, before));
}

// The **status line** must fit the HUD's buffer in the **worst** case: every
// field at its longest.
//
// The line is the operator's only window on the ragebot, and the HUD's buffer
// is 192 bytes (`payload/features/src/overlay.cpp`). The fields that do not
// fit — the aim **writes** and the **backtracks** — are in the `rage`
// command's JSON instead.
void test_the_status_line_fits_the_hud_buffer() {
  cs2::control::RageStatusFields fields{};
  // Every field at its longest: the longest aim state, the longest numbers, the
  // longest objective name, the longest markers and the longest refusal.
  fields.aim = "hold";
  fields.target = -2147483648;
  fields.hitbox = -2147483648;
  fields.objective = "monte_carlo";
  fields.monte_carlo = " mc-refused";
  fields.shots = 18446744073709551615ULL;
  fields.presses = 18446744073709551615ULL;
  fields.edges = 18446744073709551615ULL;
  fields.fire_mode = "unknown";
  fields.fire_refusal = "clip-unknown";
  fields.stop = " stop";
  fields.occlusion_query = " occ-inc";
  fields.occlusion_candidates = " occ-cap";
  fields.player_attack = " player";

  char line[k_status_buffer] = {};
  const std::size_t written = cs2::control::format_rage_status(fields, line, sizeof(line));
  // The line fits: nothing is truncated.
  assert(written < k_status_buffer);
  assert(written == std::strlen(line));
  // And it is the **whole** line, not a prefix of a longer one.
  assert(std::strstr(line, "clip-unknown") != nullptr);
  assert(std::strstr(line, "player") != nullptr);

  // The realistic case has room to spare.
  cs2::control::RageStatusFields plain{};
  plain.aim = "AIM";
  plain.target = 12;
  plain.hitbox = 1;
  plain.objective = "hitchance";
  plain.shots = 120;
  plain.presses = 130;
  plain.edges = 12;
  plain.fire_mode = "semi";
  plain.fire_refusal = "ok";
  char realistic[k_status_buffer] = {};
  const std::size_t short_written =
      cs2::control::format_rage_status(plain, realistic, sizeof(realistic));
  assert(short_written < k_status_buffer);
  assert(short_written == std::strlen(realistic));
}

// The **wiring** in `input.cpp`: the composition is the pure function's, the
// ragebot's press is the **service's decision**, and the player's own press
// is the engine's own bit.
//
// These are the two breaks the host test has to catch, and they are both in
// the payload's own file: a ragebot that holds `IN_ATTACK` on every command
// (the original defect) and a withdrawal that takes the player's own press
// away (the rule the repo states). The pure functions above pin the
// **semantics**; this pins the **call sites**.
void test_the_fire_path_wiring_is_the_pure_write(const std::filesystem::path& root) {
  const std::filesystem::path path = root / "payload" / "control" / "src" / "input.cpp";
  std::ifstream file(path);
  assert(file.good());
  if (!file.good()) {
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  // The **code**, without the whitespace: the assertion is about what the
  // wiring does, not about how it is laid out.
  std::string code;
  code.reserve(text.size());
  for (const char c : text) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      code.push_back(c);
    }
  }
  // The composition is the pure function's.
  assert(code.find("compose_fire_write(write_inputs)") != std::string::npos);
  // The payload does **not** compose the mask add-only any more: that is what
  // made the payload's own press unremovable.
  assert(code.find("compose_button_mask(") == std::string::npos);
  // The ragebot's press is the **service's decision** — not a constant. The
  // original defect (`rage_press = rage_firing`) held `IN_ATTACK` down on
  // every command.
  assert(code.find("rage_press_.load()") != std::string::npos);
  // The engine's own attack bit comes from the **cache**, so the payload
  // never reads its own press back as the player's.
  assert(code.find("fire_commands_.read(") != std::string::npos);
  // The player's own press is the engine's own bit — a contributor of its
  // own, so a withdrawal cannot take it away.
  assert(code.find(".player_attack=reading.player_attack") != std::string::npos);
  // The PB's button state is written **unconditionally**: the release has to
  // reach it even when there is nothing else to write.
  assert(code.find("pb_words[0]=write.pb_held") != std::string::npos);
  // ... and the write is guarded by **writability alone**: a guard of the
  // form "there is something to write" is exactly what skipped the release.
  assert(code.find("if(pb_words!=nullptr){") != std::string::npos);
  assert(code.find("if(pb_words!=nullptr&&") == std::string::npos);
}

// The repository's root, so the test can read the payload's own source.
//
// The `control-input` target is not given the root as an argument (the root
// `meson.build` is not this task's), so the path is derived from the
// translation unit's own path: `__FILE__` is the source file as the compiler
// saw it, and the test runs with the build directory as its working
// directory.
auto repository_root() -> std::filesystem::path {
  const std::filesystem::path here = std::filesystem::path(__FILE__);
  // .../payload/control/tests/test_control_input.cpp -> the repository's root.
  return here.parent_path().parent_path().parent_path().parent_path();
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
  test_a_semi_weapon_gets_a_release_between_two_shots();
  test_the_semi_weapon_sequence_carries_the_edge();
  test_the_unknown_fire_mode_is_conservative_and_reported();
  test_a_full_auto_weapon_keeps_the_trigger_down();
  test_the_gate_blocks_each_condition();
  test_the_disabled_fire_is_not_a_missing_solve();
  test_the_payload_can_take_its_own_press_back();
  test_the_ragebot_never_clears_the_players_own_attack();
  test_the_players_press_survives_the_ragebots_withdrawal();
  test_the_other_contributors_survive_the_withdrawal();
  test_the_command_cache_reads_the_players_press();
  test_the_engine_shot_counter_ignores_a_reload_and_a_failed_read();
  test_the_status_line_fits_the_hud_buffer();
  test_the_fire_path_wiring_is_the_pure_write(repository_root());

  std::puts("control-input: ok");
  return 0;
}
