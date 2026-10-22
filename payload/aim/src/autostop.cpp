#include "cs2/aim/autostop.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2::aim {
namespace {
constexpr float k_pi = 3.14159265358979323846F;
constexpr float k_epsilon = 0.1F;
struct Vec {
  float x;
  float y;
};
auto length(Vec v) noexcept -> float {
  return std::sqrt(v.x * v.x + v.y * v.y);
}
auto dot(Vec a, Vec b) noexcept -> float {
  return a.x * b.x + a.y * b.y;
}
auto wish(const StopCommand& c) noexcept -> Vec {
  // This project's side axis follows Source's right vector (sin(yaw),-cos(yaw)).
  const float yaw = c.yaw * k_pi / 180.0F, pitch = c.pitch * k_pi / 180.0F;
  const float roll = c.roll * k_pi / 180.0F;
  const float sy = std::sin(yaw), cy = std::cos(yaw);
  const float sp = std::sin(pitch), cp = std::cos(pitch);
  const float sr = std::sin(roll), cr = std::cos(roll);
  Vec forward{cp * cy, cp * sy}, right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy};
  // 312E50 flattens and normalizes each basis vector before combining axes.
  const float forward_length = length(forward), right_length = length(right);
  if (forward_length != 0) {
    forward.x /= forward_length;
    forward.y /= forward_length;
  }
  if (right_length != 0) {
    right.x /= right_length;
    right.y /= right_length;
  }
  return {forward.x * c.axes.forward + right.x * c.axes.side,
          forward.y * c.axes.forward + right.y * c.axes.side};
}
auto friction(float speed, float t, float coefficient, float stop) noexcept -> float {
  if (speed < k_epsilon) return speed;
  return speed * (std::max(speed - (t * std::max(speed, stop)) * coefficient, 0.0F) / speed);
}
auto drag(Vec v, const StopState& s) noexcept -> Vec {
  const float speed = length(v);
  if (speed < k_epsilon) return v;
  const float ratio = friction(speed, s.interval, s.ground_friction, s.stop_speed) / speed;
  return {v.x * ratio, v.y * ratio};
}
auto prepare_speed(const StopState& s, float& stamina, float t) noexcept -> float {
  float speed = s.speed_limit;
  if (stamina > 0.0F) {
    float factor = std::clamp(1.0F + stamina / -100.0F, 0.0F, 1.0F);
    speed *= factor * factor;
    stamina = std::max(stamina - t * s.stamina_recovery, 0.0F);
  }
  if (s.duck_active) speed *= ((s.duck_amount * 0.34F) + 1.0F) - s.duck_amount;
  return speed;
}
auto accelerate(Vec v, const StopCommand& cmd, float max_speed, const StopState& s, float scale,
                bool air, float t, float factor) noexcept -> Vec {
  const Vec w = wish(cmd);
  const float magnitude = length(w);
  if (magnitude == 0.0F) return v;
  const Vec d{w.x / magnitude, w.y / magnitude};
  const float wanted = std::min(magnitude, 1.0F) * max_speed;
  const float available = (air ? std::min(s.air_wish_cap, wanted) : wanted) - dot(v, d);
  if (available > 0.0F) {
    const float rate = air ? (wanted * s.air_acceleration) * factor
                           : (std::max(wanted, 250.0F) * s.ground_acceleration) * scale;
    if (air || rate > 0.0F) {
      const float amount = std::min(available, rate * t);
      v.x += d.x * amount;
      v.y += d.y * amount;
    }
  }
  return v;
}
void reverse(const StopState& s, StopCommand& c) noexcept {
  c.pitch = 0.0F;
  c.roll = 0.0F;
  c.yaw = std::atan2(s.vy, s.vx) * 180.0F / k_pi;
  c.axes.forward = -1.0F;
  c.axes.side = 0.0F;
}
auto air_counter(Vec v, float speed, const StopState& s, float t, float factor, float threshold,
                 StopCommand& cmd) noexcept -> bool {
  const float excess = length(v) - threshold;
  if (excess <= 0.0F) return false;
  const float budget = ((s.air_acceleration * factor) * speed) * t;
  // Positive excess / zero budget is +infinity in the source and saturates
  // to full reverse. Spell it explicitly without leaking a nonfinite axis.
  const float ratio = budget > 0.0F ? excess / budget : std::numeric_limits<float>::infinity();
  cmd.yaw = std::atan2(v.y, v.x) * 180.0F / k_pi;
  cmd.yaw -= std::floor(cmd.yaw / 360.0F + 0.5F) * 360.0F;
  cmd.axes.forward = s.force_full_air ? -1.0F : -std::min(ratio, 1.0F);
  cmd.axes.side = 0.0F;
  return true;
}
auto valid(const StopState& s, const StopCommand& c) noexcept -> bool {
  const float values[] = {s.vx,
                          s.vy,
                          s.vz,
                          s.speed_limit,
                          s.interval,
                          s.movement_factor,
                          s.ground_friction,
                          s.stop_speed,
                          s.ground_acceleration,
                          s.air_acceleration,
                          s.air_wish_cap,
                          s.vertical_coefficient,
                          s.stamina,
                          s.stamina_recovery,
                          s.duck_amount,
                          s.weapon_speed[0],
                          s.weapon_speed[1],
                          c.axes.forward,
                          c.axes.side,
                          c.axes.up,
                          c.pitch,
                          c.yaw,
                          c.roll};
  for (float v : values)
    if (!std::isfinite(v)) return false;
  return s.speed_limit > 0 && s.interval > 0 && s.movement_factor >= 0 && s.ground_friction >= 0 &&
         s.stop_speed >= 0 && s.ground_acceleration >= 0 && s.air_acceleration >= 0 &&
         s.air_wish_cap >= 0 && s.stamina_recovery >= 0 && s.duck_amount >= 0 && s.duck_amount <= 1;
}
}  // namespace

auto stop_weapon_scale(const StopState& s) noexcept -> float {
  if (!s.weapon_scaling) return 1.0F;
  const int selector = s.weapon_selector >= 0 && s.weapon_selector < 2 ? s.weapon_selector : 0;
  const float speed = s.weapon_speed[static_cast<std::size_t>(selector)];
  if (s.duck_active && (s.weapon_category <= 1 || speed * 0.52F >= 110.0F)) return 1.0F;
  return std::min(speed / 250.0F, 1.0F);
}

auto solve_autostop(const StopState& s, const StopCommand& cmd) noexcept -> StopResult {
  StopResult out;
  out.command = cmd;
  if (!valid(s, cmd)) {
    out.path = StopPath::invalid;
    return out;
  }
  out.path = StopPath::unchanged;
  out.threshold = (s.speed_limit * 0.34F) * 0.99F;
  float stamina = s.stamina;
  Vec v{s.vx, s.vy};
  if (!s.ground) {
    if (dot(v, v) < 0.8F) return out;
    const float vertical_step = s.vertical_coefficient * s.interval;
    if (s.fractional && s.vz > 0.0F && vertical_step > s.vz) {
      const float r = s.vz / vertical_step;
      float q = (r + 131072.0F) - 131072.0F;
      if (q < r) q += 1.0F / 64.0F;
      const float t1 = q * s.interval;
      float speed = prepare_speed(s, stamina, t1);
      if (air_counter(v, speed, s, t1, s.movement_factor, out.threshold, out.command))
        v = accelerate(v, out.command, speed, s, 1.0F, true, t1, s.movement_factor);
      out.events[0] = {0.0F, out.command, true};
      const float t2 = (1.0F - q) * s.interval;
      speed = prepare_speed(s, stamina, t2);
      (void)air_counter(v, speed, s, t2, 1.0F, out.threshold, out.command);
      out.events[1] = {q, out.command, true};
      out.event_count = 2;
      out.path = StopPath::air_fractional;
      return out;
    }
    const float speed = prepare_speed(s, stamina, s.interval);
    if (air_counter(v, speed, s, s.interval, s.movement_factor, out.threshold, out.command))
      out.path = StopPath::air;
    return out;
  }
  const float scale = stop_weapon_scale(s);
  float speed = prepare_speed(s, stamina, s.interval);
  Vec predicted = accelerate(drag(v, s), cmd, speed, s, scale, false, s.interval, 1.0F);
  if (out.threshold * out.threshold > dot(predicted, predicted)) return out;
  if (cmd.axes.forward != 0.0F || cmd.axes.side != 0.0F) {
    stamina = s.stamina;
    speed = prepare_speed(s, stamina, s.interval);
    if (s.fractional) {
      const float input =
          std::sqrt(cmd.axes.forward * cmd.axes.forward + cmd.axes.side * cmd.axes.side);
      const float wanted = std::min(input, 1.0F) * speed;
      const float speed0 = length(v), control = std::max(speed0, s.stop_speed);
      const float accel = (std::max(wanted, 250.0F) * scale) * s.ground_acceleration;
      float best = std::numeric_limits<float>::max(), best_q = 0.0F;
      for (int i = 1; i < 64; ++i) {
        const float q = static_cast<float>(i) * (1.0F / 64.0F), t1 = q * s.interval;
        float candidate = speed0;
        if (speed0 >= k_epsilon)
          candidate =
              speed0 * (std::max(speed0 - (t1 * control) * s.ground_friction, 0.0F) / speed0);
        float gap = wanted - candidate;
        if (gap > 0.0F && accel > 0.0F) candidate += std::min(t1 * accel, gap);
        const float t2 = (1.0F - q) * s.interval;
        candidate = friction(candidate, t2, s.ground_friction, s.stop_speed);
        gap = candidate + wanted;
        if (gap > 0.0F && accel > 0.0F) candidate -= std::min(t2 * accel, gap);
        const float error = std::abs(candidate - out.threshold);
        if (candidate < out.threshold && error < best) {
          best = error;
          best_q = q;
        }
      }
      if (best < 1.0F) {
        out.events[0] = {0.0F, cmd, false};
        StopCommand opposite = cmd;
        opposite.axes = {-cmd.axes.forward, -cmd.axes.side, -cmd.axes.up};
        out.events[1] = {best_q, opposite, false};
        out.event_count = 2;
        out.path = StopPath::ground_fractional;
        return out;
      }
    } else {
      const Vec w = wish(cmd);
      const float n = length(w);
      const float wanted = std::min(n, 1.0F) * speed;
      if (wanted > 0.0F) {
        const Vec dir{w.x / n, w.y / n};
        float factor = (dot(v, dir) + (out.threshold - length(v))) / wanted;
        const float input =
            std::sqrt(cmd.axes.forward * cmd.axes.forward + cmd.axes.side * cmd.axes.side);
        if (input > 1.0F) factor *= 1.0F / input;
        out.command.axes.forward *= factor;
        out.command.axes.side *= factor;
        predicted = accelerate(drag(v, s), out.command, speed, s, scale, false, s.interval, 1.0F);
        if (dot(predicted, predicted) <= out.threshold * out.threshold + 1.0F) {
          out.path = StopPath::ground_scaled;
          return out;
        }
      }
    }
  }
  reverse(s, out.command);
  out.path = StopPath::ground_reverse;
  return out;
}
}  // namespace cs2::aim
