#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "cs2/aim/nl_movement.hpp"
using namespace cs2;
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
int main() {
  aim::NlAirState s;
  s.velocity = {300, 0};
  s.goal_yaw = 90;
  s.steps = 1;
  s.blend = 0;
  auto wish = aim::nl_air_wish(s, 1.0F / 64);
  check(std::fabs(wish.x + .078125F) < .00001F, "dump turning-angle fixture: -budget/(2*speed)");
  auto turn = aim::nl_air_plan(s);
  check(turn.valid && std::fabs(game::length(turn.final_velocity) - 300) < .01F,
        "turning branch preserves kinetic speed at the recovered boundary");
  s.blend = 1;
  auto fast = aim::nl_air_plan(s);
  check(fast.valid && std::fabs(game::length(fast.final_velocity) -
                                std::sqrt(300.0F * 300 + 30.0F * 30)) < .02F,
        "speed branch accelerates");
  s.steps = 16;
  auto split = aim::nl_air_plan(s);
  check(split.valid && split.count == 16 &&
            game::length(split.final_velocity) > game::length(fast.final_velocity),
        "recompute steering and velocity per slice");
  for (int i = 0; i < split.count; ++i)
    check(split.events[i].when == i / 16.0F &&
              std::abs(game::length(split.events[i].world_wish) - 1) < .0001F,
          "monotonic 1/64-grid events and normalized wishes");
  s.goal_yaw = 179;
  auto rear = aim::nl_air_wish(s, 1.0F / 64);
  s.goal_yaw = -179;
  auto other = aim::nl_air_wish(s, 1.0F / 64);
  check(std::fabs(rear.x - other.x) < .0001F && std::fabs(rear.y - other.y) < .0001F,
        "dump's 170 degree turn-side branch");
  s.velocity = {0, 0};
  s.goal_yaw = 90;
  wish = aim::nl_air_wish(s, 1.0F / 64);
  check(wish.y > .999F, "low-speed forward launch follows requested heading");
  s.velocity.x = std::numeric_limits<float>::quiet_NaN();
  check(!aim::nl_air_plan(s).valid, "invalid velocity rejected");
  // Native gain mode must not turn a low speed-gain blend into braking.
  // Keep the recovered source fixture above unchanged; this limit is native-only.
  for (float speed : {80.0F, 150.0F, 300.0F, 600.0F})
    for (float blend : {0.0F, .25F, .5F, 1.0F})
      for (int steps : {1, 3, 16, 30}) {
        aim::NlAirState native;
        native.velocity = {speed, 0};
        native.goal_yaw = 90;
        native.blend = blend;
        native.steps = steps;
        native.allow_counter = false;
        const auto plan = aim::nl_air_plan(native);
        auto velocity = native.velocity;
        check(plan.valid, "native non-braking plan remains valid");
        for (int n = 0; n < plan.count; ++n) {
          const auto& event = plan.events[n];
          check(game::dot(velocity, event.world_wish) >= -.001F,
                "native wish never opposes the current simulated velocity");
          const float end = n + 1 < plan.count ? plan.events[n + 1].when : 1;
          const float needed = std::max(0.0F, 30 - game::dot(velocity, event.world_wish));
          const float gain = std::min(needed, 12 * 250 * (end - event.when) / 64);
          velocity = velocity + event.world_wish * gain;
        }
        check(game::length(velocity) >= speed - .001F,
              "bounded strafe cannot remove speed even at minimum blend");
      }
  aim::NlAirState smooth;
  smooth.velocity = {300, 0};
  smooth.side = -1;
  smooth.allow_counter = false;
  smooth.smooth_yaw = true;
  const auto smoothed = aim::nl_air_plan(smooth);
  check(smoothed.valid && std::fabs(smoothed.events[0].world_wish.x - std::sqrt(.5F)) < .001F,
        "native first slice turns 45 degrees instead of snapping 85 degrees");
  auto simulated = smooth.velocity;
  float heading = 0;
  for (int n = 0; n < smoothed.count; ++n) {
    const auto wish = smoothed.events[n].world_wish;
    const float yaw = std::atan2(wish.y, wish.x) * 180 / 3.14159265F;
    check(std::fabs(std::remainder(yaw - heading, 360)) <= 45.001F,
          "native world-wish transitions remain bounded");
    heading = yaw;
    const float end = n + 1 < smoothed.count ? smoothed.events[n + 1].when : 1;
    const float amount = std::min(std::max(0.0F, 30 - game::dot(simulated, wish)),
                                  12 * 250 * (end - smoothed.events[n].when) / 64);
    simulated = simulated + wish * amount;
  }
  check(game::length(simulated - smoothed.final_velocity) < .001F,
        "look-ahead integrates the emitted bounded wish, not the unclamped ideal");
  aim::NlAirState directional;
  directional.directional_feedback = true;
  directional.velocity = {300, 0};
  directional.steps = 1;
  directional.goal_yaw = 90;
  const auto positive = aim::nl_air_wish(directional, 1.0F / 64);
  directional.goal_yaw = -90;
  const auto negative = aim::nl_air_wish(directional, 1.0F / 64);
  check(positive.y > .99F && negative.y < -.99F && std::fabs(positive.x - negative.x) < .0001F,
        "native reference theta turns on the short arc toward either requested side");
  directional.goal_yaw = 0;
  directional.phase = 0;
  const auto phase0 = aim::nl_air_wish(directional, 1.0F / 64);
  directional.phase = 1;
  const auto phase1 = aim::nl_air_wish(directional, 1.0F / 64);
  check(phase0.y > 0 && phase1.y < 0,
        "near-heading gain alternates instead of permanently selecting one side");
  directional.smooth_yaw = true;
  directional.allow_counter = false;
  directional.steps = 16;
  unsigned gain_phase = 0;
  const float initial_speed = game::length(directional.velocity);
  float first_gain = 0;
  for (int command = 0; command < 60; ++command) {
    directional.phase = gain_phase;
    const float initial_heading =
        std::atan2(directional.velocity.y, directional.velocity.x) * 180 / 3.14159265F;
    const bool near_heading = std::fabs(initial_heading) <= 2;
    directional.seed_yaw = 0;
    const auto before = game::length(directional.velocity);
    const auto plan = aim::nl_air_plan(directional);
    const float gain = game::length(plan.final_velocity) - before;
    if (command == 0) {
      first_gain = gain;
      check(gain > 3.0F, "straight input must earn positive air gain, not just preserve speed");
    }
    check(plan.valid && gain > .5F, "every modeled gain command earns acceleration");
    check(std::fabs(std::atan2(plan.final_velocity.y, plan.final_velocity.x) * 180 / 3.14159265F) <
              12,
          "per-command gain sweeps remain bounded around the requested heading");
    directional.velocity = plan.final_velocity;
    if (near_heading) ++gain_phase;
  }
  const float final_speed = game::length(directional.velocity);
  check(final_speed - initial_speed > 100,
        "one second of modeled straight travel has substantial accumulated gain");
  std::printf(
      "Modeled gain: first command %.3f units/s; 60 commands %.3f -> %.3f units/s (synthetic, not "
      "server measurement)\n",
      first_gain, initial_speed, final_speed);
  directional.velocity = {8, 0};
  directional.goal_yaw = 90;
  const auto launch_wish = aim::nl_air_wish(directional, 1.0F / 64);
  check(launch_wish.y > .999F, "low-speed native wish follows travel intent");
  // Straight travel requires cancelling lateral velocity within each command,
  // not merely alternating an entire command's direction on the next one.
  for (float accel : {12.0F, 100.0F})
    for (float strength : {.99F, 1.0F})
      for (int slices : {16, 30})
        for (float yaw : {0.0F, 45.0F, 179.0F, -179.0F})
          for (float speed : {300.0F, 700.0F})
            for (float friction : {.25F, 1.0F})
              for (float begin : {0.0F, .25F}) {
                const float rad = yaw * 3.14159265F / 180;
                const game::Vec2 forward{std::cos(rad), std::sin(rad)}, left{-forward.y, forward.x};
                aim::NlAirState balanced;
                balanced.velocity = forward * speed;
                balanced.goal_yaw = yaw;
                balanced.air_acceleration = accel;
                balanced.blend = strength;
                balanced.steps = slices;
                balanced.friction = friction;
                balanced.begin = begin;
                balanced.balanced_lateral = true;
                const auto plan = aim::nl_air_plan(balanced);
                check(plan.valid && game::dot(plan.final_velocity, forward) > speed + .05F,
                      "paired analog plan gains forward speed at runtime and stock physics");
                check(std::fabs(game::dot(plan.final_velocity, left)) < .002F,
                      "paired acceleration cancels lateral velocity within the command");
                if(std::fabs(game::dot(plan.displacement,left))>=.02F)
                  std::printf("drift failure accel=%.1f strength=%.2f steps=%d yaw=%.1f speed=%.1f friction=%.2f begin=%.2f lateral=%.6f\n",accel,strength,slices,yaw,speed,friction,begin,game::dot(plan.displacement,left));
                check(std::fabs(game::dot(plan.displacement, left)) < .02F,
                      "alternating pair impulses avoid accumulating a lateral path bias");
              }
  aim::NlAirState straight;
  straight.velocity = {300, 0};
  straight.steps = 30;
  straight.blend = .99F;
  straight.air_acceleration = 100;
  straight.balanced_lateral = true;
  game::Vec2 position{};
  for (int n = 0; n < 60; ++n) {
    straight.phase = static_cast<unsigned>(n);
    const auto plan = aim::nl_air_plan(straight);
    check(plan.valid && std::fabs(plan.final_velocity.y) < .002F,
          "repeated paired gain stays straight");
    straight.velocity = plan.final_velocity;
    position = position + plan.displacement;
  }
  check(std::fabs(position.y) < .05F && straight.velocity.x > 500,
        "long paired run gains speed without sideways drift");
  std::printf(
      "Paired model (air_accel 100, strength .99, 30 slices): 300 -> %.3f, lateral velocity %.6f, "
      "lateral displacement %.6f (synthetic)\n",
      straight.velocity.x, straight.velocity.y, position.y);
  aim::NlHopState h;
  h.grounded = true;
  check(!aim::nl_hop_plan(h).pulse, "no automatic jump without held input");
  h.held = true;
  auto hop = aim::nl_hop_plan(h);
  check(hop.pulse && hop.mode == 3 && hop.when == 0,
        "grounded fallback jumps immediately without random delay");
  h.grounded = false;
  h.record_valid = true;
  h.command_tick = 101;
  h.landed_tick = 100;
  h.landed_fraction = 20 / 64.0F;
  hop = aim::nl_hop_plan(h);
  check(hop.pulse && hop.when == 21 / 64.0F,
        "native record bridge places pulse just after fresh contact");
  h.grounded = true;
  hop = aim::nl_hop_plan(h);
  check(hop.pulse && hop.mode == 4 && hop.when == 21 / 64.0F,
        "fresh contact takes precedence over blind ground jitter after prediction");
  h.grounded = false;
  h.command_tick = 102;
  check(!aim::nl_hop_plan(h).pulse, "stale landing cannot spam jump pulses");
  h.command_tick = 101;
  h.landed_fraction = 63 / 64.0F;
  check(!aim::nl_hop_plan(h).pulse, "no event at or beyond command end");
  h.grounded = true;
  check(aim::nl_hop_plan(h).pulse && aim::nl_hop_plan(h).when == 0,
        "late contact never suppresses an already grounded jump");
  h.command_tick = 102;
  h.landed_fraction = 20 / 64.0F;
  check(aim::nl_hop_plan(h).when == 0,
        "previous-command landing fraction is not replayed as a current delay");
  h.grounded = false;
  h.command_tick = 100;
  check(!aim::nl_hop_plan(h).pulse, "wrong-clock record cannot inject an airborne press");
  h.command_tick = 101;
  h.landed_fraction = 20.9F / 64;
  check(aim::nl_hop_plan(h).when == 21 / 64.0F,
        "non-grid contact advances to the first following boundary");
  aim::NlHopState predicted;
  predicted.held = true;
  predicted.command_tick = 100;
  predicted.predicted_contact = true;
  predicted.contact_fraction = .501F;
  check(aim::nl_hop_plan(predicted).when == 33 / 64.0F && aim::nl_hop_plan(predicted).mode == 6,
        "predicted contact is rounded up into its current command");
  predicted.actual_valid = true;
  predicted.actual_tick = 98;
  predicted.actual_fraction = .75F;
  check(!aim::nl_hop_plan(predicted).pulse && aim::nl_hop_plan(predicted).mode == 5,
        "new press inside inclusive one-tick spam window is deferred");
  predicted.command_tick = 101;
  check(aim::nl_hop_plan(predicted).pulse,
        "cooldown can expire without being reset by rejected edges");
  predicted.actual_valid = false;
  predicted.contact_fraction = .999F;
  check(!aim::nl_hop_plan(predicted).pulse,
        "late predicted contact defers instead of emitting when=1");
  std::puts(
      "PASS: dump-derived steering fixtures, subtick acceleration, timing grid, turn behavior and "
      "bounded hop policy");
}
