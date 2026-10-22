#include "cs2/aim/nl_movement.hpp"
#include <algorithm>
#include <cmath>
namespace cs2::aim {
namespace {
constexpr float degrees = 57.2957763671875F, radians = .017453292519943295F;
float wrap(float value) {
  return std::remainder(value, 360.0F);
}
float angle(float ratio) {
  return std::fabs(ratio) <= 1 ? std::asin(ratio) * degrees : 0;
}
NlAirPlan balanced_plan(NlAirState s, const float* times, int count) {
  NlAirPlan plan;
  const float angle = s.goal_yaw * radians;
  const game::Vec2 forward{std::cos(angle), std::sin(angle)}, left{-forward.y, forward.x};
  const float strength = std::clamp(s.blend, 0.0F, 1.0F);
  const auto magnitude_gain = [&](game::Vec2 velocity, game::Vec2 direction, float magnitude,
                                  float dt) {
    const float speed = s.wish_speed * magnitude;
    return std::min(std::max(0.0F, std::min(s.air_cap, speed) - game::dot(velocity, direction)),
                    s.air_acceleration * speed * s.friction * dt);
  };
  const auto emit = [&](float when, float end, game::Vec2 direction, float magnitude) {
    const float dt = (end - when) * s.tick_interval;
    const auto before = s.velocity;
    const float gain = magnitude_gain(before, direction, magnitude, dt);
    s.velocity = s.velocity + direction * gain;
    plan.displacement = plan.displacement + s.velocity * dt;
    plan.events[plan.count++] = {when, direction * magnitude};
    s.stamina = std::max(0.0F, s.stamina - dt * s.stamina_recovery);
  };
  const auto cancel_lateral = [&](float dt, float magnitude) {
    const float cross = game::dot(s.velocity, left), amount = std::fabs(cross);
    if (amount < .00001F) return forward;
    const float sign = cross > 0 ? -1.0F : 1.0F;
    const auto direction = [&](float phi) {
      return forward * std::cos(phi) + left * (sign * std::sin(phi));
    };
    const auto residual = [&](float phi) {
      return amount - magnitude_gain(s.velocity, direction(phi), magnitude, dt) * std::sin(phi);
    };
    float lo = 0, hi = 1.5707963267948966F;
    if (residual(hi) > 0) return direction(hi);
    for (int n = 0; n < 30; ++n) {
      const float mid = (lo + hi) * .5F;
      if (residual(mid) > 0)
        lo = mid;
      else
        hi = mid;
    }
    return direction((lo + hi) * .5F);
  };
  for (int i = 0; i < count; i += 2) {
    const float when = times[i], end = i + 1 < count ? times[i + 1] : 1;
    const float dt1 = (end - when) * s.tick_interval;
    if (i + 1 == count) {
      emit(when, 1, cancel_lateral(dt1, strength), strength);
      break;
    }
    const float end2 = i + 2 < count ? times[i + 2] : 1, dt2 = (end2 - end) * s.tick_interval;
    // A longer first interval must not create more lateral velocity than the
    // second interval can remove. Analog magnitude controls the actual budget.
    const float first_strength = strength * std::min(1.0F, dt2 / dt1);
    const float cross = game::dot(s.velocity, left), offset = game::dot(plan.displacement, left);
    const int side = std::fabs(cross) > .001F
                         ? (cross > 0 ? -1 : 1)
                         : (std::fabs(offset) > .000001F ? (offset > 0 ? -1 : 1)
                                                         : ((s.phase + (i / 2)) & 1u ? -1 : 1));
    const float speed = game::length(s.velocity);
    game::Vec2 wish = forward;
    if (speed >= 15) {
      const float budget = s.air_acceleration * s.wish_speed * first_strength * s.friction * dt1;
      const float cap = std::min(s.air_cap, s.wish_speed * first_strength);
      const float projection = budget >= cap ? cap * .5F : cap - budget;
      const float theta = std::acos(std::clamp(projection / speed, -1.0F, 1.0F));
      const float yaw = std::atan2(s.velocity.y, s.velocity.x) + side * theta;
      wish = {std::cos(yaw), std::sin(yaw)};
    }
    emit(when, end, wish, first_strength);
    emit(end, end2, cancel_lateral(dt2, strength), strength);
  }
  plan.valid = true;
  plan.final_velocity = s.velocity;
  return plan;
}
}  // namespace
float nl_grid(float fraction) noexcept {
  return std::nearbyint(fraction * 64) / 64;
}
game::Vec2 nl_air_wish(const NlAirState& s, float dt) noexcept {
  const float speed = game::length(s.velocity);
  if (speed < 1) {
    const float a = s.goal_yaw * radians;
    return {std::cos(a), std::sin(a)};
  }
  const float velocity_yaw = std::atan2(s.velocity.y, s.velocity.x) * degrees;
  if (s.directional_feedback) {
    if (speed < 15) {
      const float yaw = s.goal_yaw * radians;
      return {std::cos(yaw), std::sin(yaw)};
    }
    const float cap = std::min(s.air_cap, s.wish_speed);
    const float budget = s.air_acceleration * s.wish_speed * s.friction * dt;
    const float projection = budget >= cap ? cap * .5F : cap - budget;
    const float theta =
        std::max(1.0F, std::acos(std::clamp(projection / speed, -1.0F, 1.0F)) * degrees);
    const float delta = wrap(s.goal_yaw - velocity_yaw);
    const float side = s.side ? (s.side > 0 ? 1.0F : -1.0F)
                              : (std::fabs(delta) > 2 ? (delta > 0 ? 1.0F : -1.0F)
                                                      : ((s.phase & 1u) ? -1.0F : 1.0F));
    const float optimal_yaw = velocity_yaw + side * theta;
    const float yaw =
        (s.goal_yaw + std::clamp(s.blend, 0.0F, 1.0F) * wrap(optimal_yaw - s.goal_yaw)) * radians;
    return {std::cos(yaw), std::sin(yaw)};
  }
  const float budget = s.air_acceleration * s.wish_speed * s.friction * dt;
  const float cap = std::min(s.air_cap, s.wish_speed);
  float optimum = angle(std::max(cap - budget, 0.0F) / speed);
  const float turning = angle(-std::min(budget, 2 * cap) / (2 * speed));
  const float delta = wrap(s.goal_yaw - velocity_yaw);
  const float boundary = 2 * std::max(std::fabs(optimum), std::fabs(turning));
  if (std::fabs(delta) > boundary)
    optimum = turning + std::clamp(s.blend, 0.0F, 1.0F) * (optimum - turning);
  // Preserve dump behavior by default. The native gain adapter may prohibit
  // opposing acceleration independently of its turning/optimal-angle blend.
  if (!s.allow_counter) optimum = std::max(optimum, 0.0F);
  const float sign = s.side ? (s.side > 0 ? 1.0F : -1.0F)
                            : (std::fabs(delta) > 170 ? 1.0F : (delta > 0 ? 1.0F : -1.0F));
  // Dump stores side=sign and internal yaw=velocity_yaw-optimum*sign.
  // Convert that pair to a world wish vector; the native adapter preserves camera angles.
  const float yaw = (velocity_yaw + sign * (90 - optimum)) * radians;
  return {std::cos(yaw), std::sin(yaw)};
}
NlAirPlan nl_air_plan(NlAirState s, std::span<const float> synchronized_times) noexcept {
  NlAirPlan p;
  const float values[] = {
      s.velocity.x,       s.velocity.y,       s.goal_yaw,      s.wish_speed,      s.air_cap,
      s.air_acceleration, s.friction,         s.tick_interval, s.begin,           s.blend,
      s.seed_yaw,         s.maximum_yaw_step, s.stamina,       s.stamina_recovery};
  for (float x : values)
    if (!std::isfinite(x)) return p;
  if (s.wish_speed <= 0 || s.air_cap <= 0 || s.air_acceleration <= 0 || s.friction <= 0 ||
      s.tick_interval <= 0 || s.begin < 0 || s.begin >= 1 || s.steps < 1 ||
      s.maximum_yaw_step <= 0 || s.maximum_yaw_step > 180 || s.stamina < 0 || s.stamina > 100 ||
      s.stamina_recovery < 0)
    return p;
  const float begin = nl_grid(s.begin);
  const int regular_count = std::min({s.steps, 32, static_cast<int>(std::floor((1 - begin) * 64))});
  if (regular_count <= 0 || synchronized_times.size() > 32) return p;
  std::array<float, 64> times{};
  int count = 0;
  for (int i = 0; i < regular_count; ++i)
    times[count++] = nl_grid(begin + (1 - begin) * i / regular_count);
  for (float when : synchronized_times) {
    if (!std::isfinite(when) || when < 0 || when >= 1) return {};
    if (when >= begin) times[count++] = when;
  }
  std::sort(times.begin(), times.begin() + count);
  count = static_cast<int>(std::unique(times.begin(), times.begin() + count) - times.begin());
  if (count > 32) return {};
  if (s.balanced_lateral) return balanced_plan(s, times.data(), count);
  // A 45-degree yaw admission limit needs multiple slices to reach the gain
  // angle. Alternating the requested side every slice can produce 45,0,45,0:
  // every wish then projects above the air cap, so the entire command gains zero.
  if (s.directional_feedback && !s.side) {
    const float heading = std::atan2(s.velocity.y, s.velocity.x) * degrees;
    const float delta = wrap(s.goal_yaw - heading);
    s.side = std::fabs(delta) > 2 ? (delta > 0 ? 1 : -1) : ((s.phase & 1u) ? -1 : 1);
  }
  float previous_yaw = s.seed_yaw;
  for (int i = 0; i < count; ++i) {
    const float when = times[i];
    const float end = i + 1 < count ? times[i + 1] : 1;
    const float dt = (end - when) * s.tick_interval;
    if (dt <= 0) return {};
    auto steering = s;
    steering.velocity = steering.velocity * std::clamp(1 - s.stamina / 100, 0.0F, 1.0F);
    steering.phase = s.phase + static_cast<unsigned>(i);
    auto wish = nl_air_wish(steering, dt);
    if (s.smooth_yaw) {
      const float wanted = std::atan2(wish.y, wish.x) * degrees;
      float actual = previous_yaw + std::clamp(wrap(wanted - previous_yaw), -s.maximum_yaw_step,
                                               s.maximum_yaw_step);
      wish = {std::cos(actual * radians), std::sin(actual * radians)};
      // If the starting intent itself opposes velocity, smoothing may cross
      // the unsafe half-plane. Meet the non-braking boundary first instead.
      if (!s.allow_counter && game::dot(s.velocity, wish) < -.0001F) {
        const float velocity_yaw = std::atan2(s.velocity.y, s.velocity.x) * degrees;
        const float left = velocity_yaw + 90, right = velocity_yaw - 90;
        actual = std::fabs(wrap(left - actual)) < std::fabs(wrap(right - actual)) ? left : right;
        wish = {std::cos(actual * radians), std::sin(actual * radians)};
      }
      previous_yaw = actual;
    }
    p.events[p.count++] = {when, wish};
    if (s.directional_feedback) s.stamina = std::max(0.0F, s.stamina - dt * s.stamina_recovery);
    const float needed = std::min(s.air_cap, s.wish_speed) - game::dot(s.velocity, wish);
    if (needed > 0) {
      const float acceleration =
          std::min(needed, s.air_acceleration * s.wish_speed * s.friction * dt);
      s.velocity = s.velocity + wish * acceleration;
    }
  }
  p.valid = true;
  p.final_velocity = s.velocity;
  return p;
}
NlHopPlan nl_hop_plan(const NlHopState& s) noexcept {
  NlHopPlan p;
  if (!s.held) return p;
  p.release_held = true;
  const auto age = static_cast<std::int64_t>(s.command_tick) - s.landed_tick;
  if (s.predicted_contact && std::isfinite(s.contact_fraction) && s.contact_fraction > 0 &&
      s.contact_fraction < 1) {
    const float boundary = std::ceil(s.contact_fraction * 64);
    // A late contact wraps into the next grounded command; don't submit when=1.
    if (boundary >= 64) return p;
    p.pulse = true;
    p.when = boundary / 64;
    p.mode = 6;
  } else if (s.record_valid && age == 1 && std::isfinite(s.landed_fraction) &&
             s.landed_fraction >= 0 && s.landed_fraction < 1) {
    const float after_contact = std::floor(s.landed_fraction * 64) + 1;
    if (after_contact < 64) {
      p.pulse = true;
      p.when = after_contact / 64;
      p.mode = 4;
    }
  }
  if (!p.pulse && s.grounded) {
    p.pulse = true;
    p.when = 0;
    p.mode = 3;
  }
  if (p.pulse && s.actual_valid && std::isfinite(s.actual_fraction) && s.actual_fraction >= 0 &&
      s.actual_fraction < 1) {
    const auto ticks = static_cast<std::int64_t>(s.command_tick) - 1 - s.actual_tick;
    const double elapsed = static_cast<double>(ticks) + p.when - s.actual_fraction;
    // The modern-jump spam gate updates actual even for rejected edges, then
    // rejects elapsed <= one tick. Sending another edge every tick can lock it.
    if (elapsed <= 1) {
      p.pulse = false;
      p.mode = 5;
    }
  }
  return p;
}
}  // namespace cs2::aim
