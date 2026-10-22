#include "cs2/aim/ballistics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2::aim {
namespace {
constexpr float k_epsilon = 1e-7F;
constexpr float k_tau = 6.2831853071795864769F;
struct PreciseVec {
  double x, y, z;
};
auto precise(Vec3 v) -> PreciseVec {
  return {v.x, v.y, v.z};
}
auto subtract(PreciseVec a, PreciseVec b) -> PreciseVec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scaled(PreciseVec v, double n) -> PreciseVec {
  return {v.x * n, v.y * n, v.z * n};
}
auto dot(PreciseVec a, PreciseVec b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto sphere(Vec3 o, Vec3 d, Vec3 center, float radius, float range) -> std::optional<float> {
  const auto w = subtract(precise(o), precise(center)), direction = precise(d);
  const double a = dot(direction, direction), c = dot(w, w) - static_cast<double>(radius) * radius;
  if (c <= 0) return 0.0F;
  const double b = dot(w, direction), h = b * b - a * c;
  if (!(a > 0) || h < 0) return std::nullopt;
  const double t = (-b - std::sqrt(h)) / a;
  if (t < 0 || t > range) return std::nullopt;
  return static_cast<float>(t);
}
auto radical_inverse(std::size_t value, std::size_t base) -> float {
  float result = 0, factor = 1.0F / static_cast<float>(base);
  while (value != 0) {
    result += static_cast<float>(value % base) * factor;
    value /= base;
    factor /= static_cast<float>(base);
  }
  return result;
}
auto armor_damage(float damage, int group, const WeaponProfile& weapon,
                  const TargetDefense& defense) -> float {
  const bool armored =
      defense.armor > 0 && ((group == 1 && defense.helmet) || (group >= 2 && group <= 5));
  if (!armored) return damage;
  // Explicit standard-armor model. Heavy armor and build-specific rule changes
  // require a target damage oracle; this is not recovered from the obfuscated callback.
  const float ratio = std::clamp(weapon.armor_ratio * 0.5F, 0.0F, 1.0F);
  const float reduced = damage * ratio;
  const float spent = (damage - reduced) * 0.5F;
  return spent > static_cast<float>(defense.armor)
             ? std::max(damage - static_cast<float>(defense.armor) * 2.0F, 0.0F)
             : reduced;
}
}  // namespace

auto intersect_box(Vec3 eye, Vec3 d, const Obb& box, float range) -> std::optional<RayInterval> {
  float enter = 0, exit = range;
  const Vec3 delta = eye - box.center;
  const Vec3 axes[] = {box.axis_x, box.axis_y, box.axis_z};
  const float half[] = {box.half.x, box.half.y, box.half.z};
  for (int i = 0; i < 3; ++i) {
    if (!(half[i] >= 0)) return std::nullopt;
    const float origin = game::dot(delta, axes[i]), direction = game::dot(d, axes[i]);
    if (std::abs(direction) < k_epsilon) {
      if (origin < -half[i] || origin > half[i]) return std::nullopt;
      continue;
    }
    float a = (-half[i] - origin) / direction, b = (half[i] - origin) / direction;
    if (a > b) std::swap(a, b);
    enter = std::max(enter, a);
    exit = std::min(exit, b);
    if (enter > exit) return std::nullopt;
  }
  return RayInterval{enter, exit};
}

auto intersect_capsule(Vec3 eye, Vec3 d, const Capsule& capsule, float range)
    -> std::optional<float> {
  if (!(capsule.radius >= 0) || !(range >= 0)) return std::nullopt;
  const auto v = subtract(precise(capsule.b), precise(capsule.a));
  const auto w = subtract(precise(eye), precise(capsule.a)), direction = precise(d);
  const double vv = dot(v, v), radius2 = static_cast<double>(capsule.radius) * capsule.radius;
  if (vv < 1e-14) return sphere(eye, d, capsule.a, capsule.radius, range);
  const double wv = dot(w, v), dv = dot(direction, v);
  const double projection = std::clamp(wv / vv, 0.0, 1.0);
  const auto nearest = subtract(w, scaled(v, projection));
  if (dot(nearest, nearest) <= radius2) return 0.0F;
  auto best = sphere(eye, d, capsule.a, capsule.radius, range);
  const auto end = sphere(eye, d, capsule.b, capsule.radius, range);
  if (end && (!best || *end < *best)) best = end;
  const auto perpendicular_d = subtract(direction, scaled(v, dv / vv));
  const auto perpendicular_w = subtract(w, scaled(v, wv / vv));
  const double a = dot(perpendicular_d, perpendicular_d), b = dot(perpendicular_w, perpendicular_d);
  const double c = dot(perpendicular_w, perpendicular_w) - radius2, h = b * b - a * c;
  if (a > 1e-14 && h >= 0) {
    const double root = std::sqrt(h);
    for (double t : {(-b - root) / a, (-b + root) / a}) {
      const double along = (wv + t * dv) / vv;
      if (t >= 0 && t <= range && along >= 0 && along <= 1 && (!best || t < *best))
        best = static_cast<float>(t);
    }
  }
  return best;
}

auto evaluate_ray(Vec3 eye, Vec3 direction, std::span<const Hitbox> hitboxes,
                  const WeaponProfile& weapon, const TargetDefense& defense,
                  std::span<const WorldVolume> world, bool complete, const WorldDamageQuery& query,
                  std::span<const std::size_t> indices, const TargetDamageQuery& target_query)
    -> RayDamage {
  RayDamage out;
  out.world_known = complete;
  direction = game::normalized(direction);
  if (game::length(direction) < 0.5F || !std::isfinite(weapon.range) || weapon.range <= 0)
    return out;
  float nearest = weapon.range;
  const std::size_t count = indices.empty() ? hitboxes.size() : indices.size();
  for (std::size_t item = 0; item < count; ++item) {
    const auto i = indices.empty() ? item : indices[item];
    if (i >= hitboxes.size()) continue;
    const auto& h = hitboxes[i];
    std::optional<float> distance;
    if (h.is_capsule)
      distance = intersect_capsule(eye, direction, h.capsule, weapon.range);
    else if (auto interval = intersect_box(eye, direction, h.box, weapon.range))
      distance = interval->enter;
    if (distance && (out.hitbox < 0 || *distance < nearest)) {
      nearest = *distance;
      out.hitbox = static_cast<int>(i);
    }
  }
  if (out.hitbox < 0) return out;
  out.distance = nearest;
  if (hitboxes[static_cast<std::size_t>(out.hitbox)].multiplier == 0) {
    out.world_known = true;
    return out;
  }
  int engine_group = -1;
  float damage = weapon.damage * range_falloff(weapon.range_modifier, nearest);
  if (query) {
    const WorldDamage answer = query(eye, direction, nearest, weapon);
    out.world_known = answer.known;
    engine_group = answer.hitgroup;
    damage = answer.known && std::isfinite(answer.damage) ? std::max(answer.damage, 0.0F) : 0.0F;
  } else {
    struct Layer {
      RayInterval interval;
      float retained;
    };
    std::vector<Layer> layers;
    for (const auto& volume : world) {
      const auto interval = intersect_box(eye, direction, volume.box, nearest);
      if (interval && interval->enter < nearest && interval->exit > interval->enter)
        layers.push_back({*interval, volume.retained_per_unit});
    }
    std::sort(layers.begin(), layers.end(),
              [](const Layer& a, const Layer& b) { return a.interval.enter < b.interval.enter; });
    for (const auto& layer : layers) {
      if (!(layer.retained > 0) || weapon.penetration <= 0) {
        damage = 0;
        break;
      }
      const float thickness = layer.interval.exit - layer.interval.enter;
      damage *= std::pow(std::clamp(layer.retained, 0.0F, 1.0F), thickness / weapon.penetration);
    }
  }
  auto hitbox = hitboxes[static_cast<std::size_t>(out.hitbox)];
  if (engine_group >= 0 && engine_group <= 11) {
    hitbox.group = engine_group;
    hitbox.multiplier = engine_group == 1 ? weapon.headshot_multiplier
                        : engine_group == 3 ? 1.25F
                        : engine_group == 6 || engine_group == 7 ? 0.75F : 1.0F;
  }
  out.damage = target_query
                   ? target_query(damage, hitbox, weapon, defense)
                   : armor_damage(damage * hitbox.multiplier, hitbox.group, weapon, defense);
  if (!std::isfinite(out.damage) || out.damage < 0) out.damage = 0;
  return out;
}

auto spread_kernel(float spread, float inaccuracy, std::size_t count) -> std::vector<KernelSample> {
  std::vector<KernelSample> out;
  if (!std::isfinite(spread) || !std::isfinite(inaccuracy) || spread < 0 || inaccuracy < 0 ||
      count == 0)
    return out;
  if (spread == 0 && inaccuracy == 0) return {{{0, 0}, 1}};
  count = std::min(count, std::size_t{256});
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const float r1 = (static_cast<float>(i) + .5F) / static_cast<float>(count);
    const float a1 = k_tau * radical_inverse(i + 1, 2);
    const float r2 = radical_inverse(i + 1, 3), a2 = k_tau * radical_inverse(i + 1, 5);
    out.push_back({{spread * r1 * std::cos(a1) + inaccuracy * r2 * std::cos(a2),
                    spread * r1 * std::sin(a1) + inaccuracy * r2 * std::sin(a2)},
                   1});
  }
  return out;
}
auto pose_kernel(float sigma, std::size_t count) -> std::vector<KernelSample> {
  if (sigma <= 0 || !std::isfinite(sigma) || count == 0) return {{{0, 0}, 1}};
  std::vector<KernelSample> out;
  count = std::min(count, std::size_t{32});
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const float u = (static_cast<float>(i) + .5F) / static_cast<float>(count);
    const float r = sigma * std::sqrt(-2 * std::log(1 - u)),
                a = static_cast<float>(i) * 2.3999632297F;
    out.push_back({{r * std::cos(a), r * std::sin(a)}, 1});
  }
  return out;
}
auto compensate_sample(const Basis& reference, Vec2 desired, Vec2 offset) -> std::optional<Vec2> {
  const Vec3 target = reference.unproject(desired);
  Vec3 forward = reference.unproject(desired - offset);
  const float scale = std::sqrt(1 + offset.x * offset.x + offset.y * offset.y);
  for (int i = 0; i < 20; ++i) {
    const auto basis = aimed_basis(reference, forward);
    forward = game::normalized(target * scale - basis.right * offset.x - basis.up * offset.y);
  }
  return reference.project(forward);
}
auto aimed_basis(const Basis& reference, Vec3 forward) noexcept -> Basis {
  Basis b;
  b.forward = game::normalized(forward);
  b.right = game::normalized(reference.right - b.forward * game::dot(reference.right, b.forward));
  b.up = game::normalized(reference.up - b.forward * game::dot(reference.up, b.forward) -
                          b.right * game::dot(reference.up, b.right));
  return b;
}
}  // namespace cs2::aim
