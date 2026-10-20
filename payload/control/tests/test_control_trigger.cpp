// Unit tests for the triggerbot's **decision**
// (`cs2/control/trigger.hpp`, `docs/ragebot.md` §"The trigger").
//
// The decision is a pure function of the view angles, the solve's answer, the
// configuration and the clock, so every rule is checkable host-side: the angle
// gate, the hitchance gate, the head hitbox's lower threshold, the reaction
// delay and the "already firing" latch.
//
// The runtime (the cheap pre-check, the solve, the press/release) needs a live
// game and is **not** covered here — see the report for what that leaves
// unverified.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cs2/control/trigger.hpp"

namespace {

using cs2::control::decide;
using cs2::control::TriggerConfig;
using cs2::control::TriggerDecision;
using cs2::control::TriggerLatch;
using cs2::control::TriggerMode;
using cs2::control::TriggerReason;
using cs2::control::TriggerSolve;
using cs2::control::TriggerView;

constexpr float k_epsilon = 0.001F;

// A configuration with everything permissive: the trigger is on, the hit chance
// threshold is low, the tolerances are wide and there is no reaction delay.
auto open_config() -> TriggerConfig {
  TriggerConfig config{};
  config.on = true;
  config.hitchance = 0.5F;
  config.head_hitchance = 0.3F;
  config.yaw_tolerance = 2.0F;
  config.pitch_tolerance = 2.0F;
  config.delay_ms = 0;
  config.key = 0;
  config.fire = true;
  config.mode = TriggerMode::always;
  return config;
}

// The solve's answer: a valid target at (yaw 10, pitch -5) with a good hit
// chance.
auto good_solve() -> TriggerSolve {
  TriggerSolve solve{};
  solve.valid = true;
  solve.hitchance = 0.9F;
  solve.yaw = 10.0F;
  solve.pitch = -5.0F;
  solve.head = false;
  return solve;
}

// The view, looking exactly at the solved aim point.
auto on_target_view() -> TriggerView {
  TriggerView view{};
  view.yaw = 10.0F;
  view.pitch = -5.0F;
  view.key_held = true;
  return view;
}

// --- the cases ----------------------------------------------------------------

// The trigger fires when the crosshair is on the target and the hit chance is
// good enough.
void test_fires_on_target() {
  const TriggerDecision decision =
      decide(on_target_view(), good_solve(), open_config(), 1000, TriggerLatch{});
  assert(decision.fire);
  assert(decision.hold);
  assert(decision.reason == TriggerReason::on_target);
  assert(decision.latch.firing);
}

// A low hit chance keeps the trigger quiet — and says why.
void test_no_fire_below_hitchance() {
  TriggerSolve solve = good_solve();
  solve.hitchance = 0.2F;
  const TriggerDecision decision =
      decide(on_target_view(), solve, open_config(), 1000, TriggerLatch{});
  assert(!decision.fire);
  assert(!decision.hold);
  assert(decision.reason == TriggerReason::hitchance);
  assert(!decision.latch.firing);
  // The reason string is the operator's window: it must name the hitchance.
  assert(std::strcmp(cs2::control::to_string(decision.reason), "hitchance too low") == 0);
}

// The thresholds are inclusive: exactly the threshold still fires.
void test_hitchance_threshold_is_inclusive() {
  TriggerConfig config = open_config();
  config.hitchance = 0.75F;
  TriggerSolve solve = good_solve();
  solve.hitchance = 0.75F;
  const TriggerDecision decision = decide(on_target_view(), solve, config, 1000, TriggerLatch{});
  assert(decision.fire);
  solve.hitchance = 0.749F;
  const TriggerDecision quiet = decide(on_target_view(), solve, config, 1000, TriggerLatch{});
  assert(!quiet.fire);
  assert(quiet.reason == TriggerReason::hitchance);
}

// The crosshair must be on the aim point: the yaw and the pitch have their own
// tolerance.
void test_no_fire_when_off_target() {
  TriggerView view = on_target_view();
  view.yaw = 40.0F;  // 30 degrees off
  const TriggerDecision yaw = decide(view, good_solve(), open_config(), 1000, TriggerLatch{});
  assert(!yaw.fire);
  assert(yaw.reason == TriggerReason::yaw_error);

  view = on_target_view();
  view.pitch = 30.0F;  // 35 degrees off
  const TriggerDecision pitch = decide(view, good_solve(), open_config(), 1000, TriggerLatch{});
  assert(!pitch.fire);
  assert(pitch.reason == TriggerReason::pitch_error);

  // Just inside the tolerance: it fires.
  view = on_target_view();
  view.yaw = 11.0F;  // 1 degree off
  const TriggerDecision near = decide(view, good_solve(), open_config(), 1000, TriggerLatch{});
  assert(near.fire);
}

// A head hitbox uses the lower threshold: the same hit chance that is too low
// for the body still fires on the head.
void test_head_uses_the_lower_threshold() {
  TriggerConfig config = open_config();
  config.hitchance = 0.8F;
  config.head_hitchance = 0.4F;
  TriggerSolve solve = good_solve();
  solve.hitchance = 0.5F;
  solve.head = true;
  const TriggerDecision head = decide(on_target_view(), solve, config, 1000, TriggerLatch{});
  assert(head.fire);
  assert(head.reason == TriggerReason::on_target);

  // The same hit chance on the body is refused.
  solve.head = false;
  const TriggerDecision body = decide(on_target_view(), solve, config, 1000, TriggerLatch{});
  assert(!body.fire);
  assert(body.reason == TriggerReason::hitchance);
}

// The reaction delay: the shot waits, then fires — and the delay is a
// timestamp, not a sleep.
void test_delay() {
  TriggerConfig config = open_config();
  config.delay_ms = 100;
  const TriggerView view = on_target_view();

  // The conditions first hold at t = 1000: the shot waits.
  const TriggerDecision armed = decide(view, good_solve(), config, 1000, TriggerLatch{});
  assert(!armed.fire);
  assert(!armed.hold);
  assert(armed.reason == TriggerReason::delay);
  assert(armed.delay_remaining_ms == 100);
  assert(armed.latch.armed);
  assert(!armed.latch.firing);

  // Still waiting 50 ms later.
  const TriggerDecision waiting = decide(view, good_solve(), config, 1050, armed.latch);
  assert(!waiting.fire);
  assert(waiting.reason == TriggerReason::delay);
  assert(waiting.delay_remaining_ms == 50);

  // The delay has elapsed: the shot fires.
  const TriggerDecision fired = decide(view, good_solve(), config, 1100, waiting.latch);
  assert(fired.fire);
  assert(fired.hold);
  assert(fired.reason == TriggerReason::on_target);
  assert(fired.delay_remaining_ms == 0);
}

// A broken condition re-arms the delay: the next continuous aim waits again.
void test_delay_rearms_after_a_break() {
  TriggerConfig config = open_config();
  config.delay_ms = 100;
  const TriggerView view = on_target_view();
  const TriggerDecision armed = decide(view, good_solve(), config, 1000, TriggerLatch{});
  assert(armed.reason == TriggerReason::delay);

  // The crosshair leaves the target: the delay is disarmed and the shot is off.
  TriggerView away = view;
  away.yaw = 90.0F;
  const TriggerDecision broken = decide(away, good_solve(), config, 1050, armed.latch);
  assert(!broken.hold);
  assert(!broken.latch.armed);
  assert(broken.latch.armed_at_ms == 0);

  // Back on the target: the delay starts again.
  const TriggerDecision rearmed = decide(view, good_solve(), config, 1100, broken.latch);
  assert(rearmed.reason == TriggerReason::delay);
  assert(rearmed.delay_remaining_ms == 100);
}

// One continuous aim is one shot: the second evaluation does not fire again,
// it reports the continuation.
void test_no_double_fire_for_one_aim() {
  const TriggerView view = on_target_view();
  const TriggerDecision first = decide(view, good_solve(), open_config(), 1000, TriggerLatch{});
  assert(first.fire);
  assert(first.hold);
  assert(first.reason == TriggerReason::on_target);

  const TriggerDecision second = decide(view, good_solve(), open_config(), 1016, first.latch);
  assert(!second.fire);  // no new press
  assert(second.hold);   // but the shot stays down
  assert(second.reason == TriggerReason::already_firing);
  assert(second.latch.firing);

  // The aim breaks: the shot is released, and the next aim fires again.
  TriggerView away = view;
  away.yaw = 90.0F;
  const TriggerDecision broken = decide(away, good_solve(), open_config(), 1032, second.latch);
  assert(!broken.hold);
  assert(!broken.latch.firing);

  const TriggerDecision again = decide(view, good_solve(), open_config(), 1048, broken.latch);
  assert(again.fire);
  assert(again.reason == TriggerReason::on_target);
}

// The feature off: the trigger never fires and the reason says so.
void test_disabled() {
  TriggerConfig config = open_config();
  config.on = false;
  const TriggerDecision decision =
      decide(on_target_view(), good_solve(), config, 1000, TriggerLatch{});
  assert(!decision.fire);
  assert(!decision.hold);
  assert(decision.reason == TriggerReason::disabled);
  assert(std::strcmp(cs2::control::to_string(decision.reason), "disabled") == 0);
}

// The hold mode: the trigger fires only while the key is held.
void test_hold_mode_needs_the_key() {
  TriggerConfig config = open_config();
  config.mode = TriggerMode::hold;
  config.key = 0x58;  // VK_X
  TriggerView view = on_target_view();
  view.key_held = false;
  const TriggerDecision idle = decide(view, good_solve(), config, 1000, TriggerLatch{});
  assert(!idle.fire);
  assert(idle.reason == TriggerReason::not_engaged);
  assert(std::strcmp(cs2::control::to_string(idle.reason), "key not held") == 0);

  view.key_held = true;
  const TriggerDecision held = decide(view, good_solve(), config, 1000, idle.latch);
  assert(held.fire);
}

// The toggle mode: the key's press edge toggles the trigger.
void test_toggle_mode() {
  TriggerConfig config = open_config();
  config.mode = TriggerMode::toggle;
  config.key = 0x58;
  TriggerView view = on_target_view();

  // The key goes down: the toggle turns the trigger on.
  view.key_held = true;
  const TriggerDecision pressed = decide(view, good_solve(), config, 1000, TriggerLatch{});
  assert(pressed.fire);
  assert(pressed.latch.toggled);

  // The key stays down: no new edge, the trigger keeps its state.
  const TriggerDecision held = decide(view, good_solve(), config, 1016, pressed.latch);
  assert(held.hold);
  assert(!held.fire);
  assert(held.latch.toggled);

  // The key goes up: no edge, the trigger keeps firing.
  view.key_held = false;
  const TriggerDecision released = decide(view, good_solve(), config, 1032, held.latch);
  assert(released.hold);
  assert(released.latch.toggled);

  // The key goes down again: the toggle turns the trigger off.
  view.key_held = true;
  const TriggerDecision toggled_off = decide(view, good_solve(), config, 1048, released.latch);
  assert(!toggled_off.hold);
  assert(!toggled_off.latch.toggled);
  assert(toggled_off.reason == TriggerReason::not_engaged);
}

// A solve that found nothing: the trigger stays quiet and says so.
void test_no_solution() {
  TriggerSolve solve{};
  solve.valid = false;
  const TriggerDecision decision =
      decide(on_target_view(), solve, open_config(), 1000, TriggerLatch{});
  assert(!decision.fire);
  assert(decision.reason == TriggerReason::no_solution);
  assert(std::strcmp(cs2::control::to_string(decision.reason), "no solution") == 0);
}

// The yaw error is measured the short way round: 359 degrees and 1 degree are
// two degrees apart, not 358.
void test_yaw_error_wraps() {
  TriggerSolve solve = good_solve();
  solve.yaw = 359.0F;
  TriggerView view = on_target_view();
  view.yaw = 1.0F;
  const TriggerDecision decision = decide(view, solve, open_config(), 1000, TriggerLatch{});
  assert(decision.fire);
}

}  // namespace

auto main() -> int {
  test_fires_on_target();
  test_no_fire_below_hitchance();
  test_hitchance_threshold_is_inclusive();
  test_no_fire_when_off_target();
  test_head_uses_the_lower_threshold();
  test_delay();
  test_delay_rearms_after_a_break();
  test_no_double_fire_for_one_aim();
  test_disabled();
  test_hold_mode_needs_the_key();
  test_toggle_mode();
  test_no_solution();
  test_yaw_error_wraps();
  std::puts("control-trigger: ok");
  return 0;
}
