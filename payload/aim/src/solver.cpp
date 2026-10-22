#include "cs2/aim/solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cs2::aim {
namespace {
constexpr float k_epsilon = 1e-6F;
struct AngularBounds {
  float min_x, max_x, min_y, max_y;
};
struct Prepared {
  DamageField field;
  std::vector<AngularBounds> hitbox_bounds;
  std::vector<Polygon> regions;
  std::vector<KernelSample> kernel;
  std::vector<KernelSample> poses;
  bool valid = true;
};
auto prepare(const SolveInput& input) -> Prepared {
  Prepared p;
  const auto finite = [](Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::abs(v.x) <= 1e8F && std::abs(v.y) <= 1e8F && std::abs(v.z) <= 1e8F;
  };
  const auto box_valid = [&](const Obb& b) {
    return finite(b.center) && finite(b.half) && b.half.x >= 0 && b.half.y >= 0 && b.half.z >= 0 &&
           finite(b.axis_x) && finite(b.axis_y) && finite(b.axis_z) &&
           std::abs(game::length(b.axis_x) - 1) < .01F &&
           std::abs(game::length(b.axis_y) - 1) < .01F &&
           std::abs(game::length(b.axis_z) - 1) < .01F &&
           std::abs(game::dot(b.axis_x, b.axis_y)) < .01F &&
           std::abs(game::dot(b.axis_y, b.axis_z)) < .01F &&
           std::abs(game::dot(b.axis_x, b.axis_z)) < .01F;
  };
  if (!finite(input.eye) || !finite(input.basis.forward) || !finite(input.basis.right) ||
      !finite(input.basis.up) || std::abs(game::length(input.basis.forward) - 1) > .01F ||
      std::abs(game::length(input.basis.right) - 1) > .01F ||
      std::abs(game::length(input.basis.up) - 1) > .01F ||
      std::abs(game::dot(input.basis.forward, input.basis.right)) > .01F ||
      std::abs(game::dot(input.basis.forward, input.basis.up)) > .01F ||
      std::abs(game::dot(input.basis.right, input.basis.up)) > .01F) {
    p.valid = false;
    return p;
  }
  for (const auto& h : input.hitboxes) {
    if (!std::isfinite(h.multiplier) || h.multiplier < 0 ||
        (h.is_capsule ? (!finite(h.capsule.a) || !finite(h.capsule.b) ||
                         !std::isfinite(h.capsule.radius) || h.capsule.radius < 0)
                      : !box_valid(h.box))) {
      p.valid = false;
      return p;
    }
  }
  for (const auto& w : input.world)
    if (!box_valid(w.box) || !std::isfinite(w.retained_per_unit) || w.retained_per_unit < 0 ||
        w.retained_per_unit > 1) {
      p.valid = false;
      return p;
    }

  if (!std::isfinite(input.weapon.damage) || input.weapon.damage <= 0 ||
      !std::isfinite(input.weapon.range_modifier) || input.weapon.range_modifier <= 0 ||
      !std::isfinite(input.policy.minimum_damage) || input.policy.minimum_damage < 0 ||
      !std::isfinite(input.policy.minimum_probability) || input.policy.minimum_probability < 0 ||
      input.policy.minimum_probability > 1 || !std::isfinite(input.pose_sigma) ||
      input.pose_sigma < 0 || input.kernel.size() > 256 || input.hitboxes.size() > 4096 ||
      input.world.size() > 128 || input.pose_sigma > 2 || input.weapon.range_modifier > 1 ||
      !std::isfinite(input.weapon.armor_ratio) || input.weapon.armor_ratio < 0 ||
      !std::isfinite(input.weapon.penetration) || input.weapon.penetration < 0 ||
      !std::isfinite(input.weapon.range) || input.weapon.range <= 0) {
    p.valid = false;
    return p;
  }
  for (const auto& hitbox : input.hitboxes) {
    Obb bound = hitbox.box;
    if (hitbox.is_capsule) {
      const auto& c = hitbox.capsule;
      const float r = c.radius;
      bound = aabb_obb(
          {std::min(c.a.x, c.b.x) - r, std::min(c.a.y, c.b.y) - r, std::min(c.a.z, c.b.z) - r},
          {std::max(c.a.x, c.b.x) + r, std::max(c.a.y, c.b.y) + r, std::max(c.a.z, c.b.z) + r});
    }
    AngularBounds bounds{std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    bool unbounded = false;
    for (const auto& corner : bound.corners()) {
      const auto projected = input.basis.project(corner - input.eye);
      if (!projected) {
        unbounded = true;
        break;
      }
      bounds.min_x = std::min(bounds.min_x, projected->x);
      bounds.max_x = std::max(bounds.max_x, projected->x);
      bounds.min_y = std::min(bounds.min_y, projected->y);
      bounds.max_y = std::max(bounds.max_y, projected->y);
    }
    if (unbounded)
      bounds = {-std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    p.hitbox_bounds.push_back(bounds);
    if (hitbox.multiplier == 0) continue;  // intervening player, not a proposal region
    Polygon polygon;
    float distance = 0;
    if (hitbox.is_capsule) {
      polygon = project_capsule(input.eye, hitbox.capsule.a, hitbox.capsule.b,
                                hitbox.capsule.radius, input.basis, 32);
      distance = game::distance(input.eye, (hitbox.capsule.a + hitbox.capsule.b) * .5F);
    } else {
      polygon = project_obb(input.eye, hitbox.box, input.basis);
      distance = game::distance(input.eye, hitbox.box.center);
    }
    p.field.add_hitbox(polygon, hitbox.multiplier, distance, input.weapon);
  }
  for (const auto& clip : input.occluders) p.field.clip(clip.normal, clip.offset);
  for (const auto& cell : p.field.cells) p.regions.push_back(cell.polygon);
  // Partition proposal regions at world silhouettes. This keeps small visible
  // windows/penetration boundaries available to the optimizer; final damage
  // still uses depth-ordered3D rays (a wall behind the target must not clip it).
  for (const auto& volume : input.world) {
    const auto silhouette = project_obb(input.eye, volume.box, input.basis);
    if (silhouette.empty()) continue;
    for (std::size_t edge = 0; edge < silhouette.points.size(); ++edge) {
      if (p.regions.size() >= 256) break;
      const auto a = silhouette.points[edge];
      const auto direction = silhouette.points[(edge + 1) % silhouette.points.size()] - a;
      const Vec2 normal{direction.y, -direction.x};
      const float n = game::length(normal);
      if (n < k_epsilon) continue;
      const Vec2 unit = normal * (1 / n);
      const float offset = game::dot(unit, a);
      std::vector<Polygon> split;
      split.reserve(p.regions.size() * 2);
      for (const auto& region : p.regions) {
        const auto inside = clip_half_plane(region, unit, offset);
        const auto outside = clip_half_plane(region, unit * (-1), -offset);
        if (!inside.empty() && !outside.empty() && split.size() + p.regions.size() < 256) {
          split.push_back(inside);
          split.push_back(outside);
        } else
          split.push_back(region);
      }
      p.regions = std::move(split);
    }
    if (p.regions.size() >= 256) break;
  }
  p.kernel = input.kernel;
  if (p.kernel.empty()) p.kernel.push_back({{0, 0}, 1});
  double sum = 0;
  for (const auto& sample : p.kernel) {
    if (!std::isfinite(sample.offset.x) || !std::isfinite(sample.offset.y) ||
        !std::isfinite(sample.weight) || sample.weight < 0) {
      p.valid = false;
      return p;
    }
    sum += sample.weight;
  }
  if (!(sum > 0)) {
    p.valid = false;
    return p;
  }
  for (auto& sample : p.kernel) sample.weight = static_cast<float>(sample.weight / sum);
  p.poses = pose_kernel(input.pose_sigma);
  return p;
}
auto ray(const SolveInput& input, const Prepared& p, Vec3 direction) -> RayDamage {
  const auto chart = input.basis.project(direction);
  if (!chart) return {};
  for (const auto& clip : input.occluders)
    if (game::dot(clip.normal, *chart) > clip.offset + k_epsilon) {
      RayDamage blocked;
      blocked.world_known = true;
      return blocked;
    }
  std::array<std::size_t, 64> indices{};
  bool overflow = false;
  std::size_t count = 0;
  for (std::size_t i = 0; i < p.hitbox_bounds.size(); ++i) {
    const auto& b = p.hitbox_bounds[i];
    if (chart->x >= b.min_x - k_epsilon && chart->x <= b.max_x + k_epsilon &&
        chart->y >= b.min_y - k_epsilon && chart->y <= b.max_y + k_epsilon) {
      if (count == indices.size()) {
        overflow = true;
        break;
      }
      indices[count++] = i;
    }
  }
  if (overflow)
    return evaluate_ray(input.eye, direction, input.hitboxes, input.weapon, input.defense,
                        input.world, input.world_complete, input.world_query, {},
                        input.target_query);
  if (count == 0) {
    RayDamage miss;
    miss.world_known = true;
    return miss;
  }
  return evaluate_ray(input.eye, direction, input.hitboxes, input.weapon, input.defense,
                      input.world, input.world_complete, input.world_query,
                      std::span<const std::size_t>(indices.data(), count), input.target_query);
}
auto evaluate(const SolveInput& input, const Prepared& p, Vec2 aim) -> AimMetrics {
  AimMetrics metrics;
  if (!p.valid || !std::isfinite(aim.x) || !std::isfinite(aim.y)) return metrics;
  const Vec3 direction = input.basis.unproject(aim);
  const auto nominal = ray(input, p, direction);
  metrics.nominal_damage = nominal.damage;
  metrics.nominal_hitbox = nominal.hitbox;
  const Basis basis = aimed_basis(input.basis, direction);
  double damage = 0, hits = 0, qualifying = 0, weight_sum = 0;
  bool known = true;
  for (const auto& sample : p.kernel) {
    if (sample.weight == 0) continue;
    for (const auto& pose : p.poses) {
      // Independent small-angle target-pose uncertainty, kept separate from the
      // supplied shot pattern. Spread itself is always applied in aim-local3D.
      const Vec3 d =
          game::normalized(basis.forward + basis.right * (sample.offset.x + pose.offset.x) +
                           basis.up * (sample.offset.y + pose.offset.y));
      const auto hit = ray(input, p, d);
      ++metrics.rays;
      const double weight = static_cast<double>(sample.weight) * pose.weight;
      weight_sum += weight;
      damage += weight * hit.damage;
      if (hit.damage > 0) hits += weight;
      if (hit.damage > 0 && hit.damage >= input.policy.minimum_damage) qualifying += weight;
      if (hit.hitbox >= 0) known = known && hit.world_known;
    }
  }
  if (weight_sum > 0) {
    metrics.expected_damage = static_cast<float>(damage / weight_sum);
    metrics.hit_probability = static_cast<float>(hits / weight_sum);
    metrics.damage_probability = static_cast<float>(qualifying / weight_sum);
    metrics.world_known = known;
  }
  return metrics;
}
auto admissible(const SolveInput& input, const AimMetrics& m) -> bool {
  return m.expected_damage > 0 && m.damage_probability > 0 &&
         m.damage_probability + k_epsilon >= input.policy.minimum_probability &&
         (!input.policy.require_known_world || m.world_known) &&
         (!input.policy.require_known_spread || input.spread_known);
}
auto clearance(const DamageField& field, Vec2 point) -> float {
  float best = 0;
  for (const auto& cell : field.cells)
    if (polygon_contains(cell.polygon, point)) {
      float distance = std::numeric_limits<float>::max();
      const auto& vertices = cell.polygon.points;
      for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto edge = vertices[(i + 1) % vertices.size()] - vertices[i];
        const float n = game::length(edge);
        if (n > k_epsilon)
          distance = std::min(distance, std::abs(game::cross(edge, point - vertices[i])) / n);
      }
      best = std::max(best, distance);
    }
  return best;
}
struct Candidate {
  Vec2 point;
  AimMetrics metrics;
  float margin = 0;
};
auto better(const SolveInput& input, const Candidate& a, const Candidate& b) -> bool {
  const bool valid_a = admissible(input, a.metrics), valid_b = admissible(input, b.metrics);
  if (valid_a != valid_b) return valid_a;
  const float primary_a = input.objective == AimObjective::hitchance ? a.metrics.damage_probability
                          : input.objective == AimObjective::damage  ? a.metrics.nominal_damage
                                                                     : a.metrics.expected_damage;
  const float primary_b = input.objective == AimObjective::hitchance ? b.metrics.damage_probability
                          : input.objective == AimObjective::damage  ? b.metrics.nominal_damage
                                                                     : b.metrics.expected_damage;
  if (primary_a != primary_b) return primary_a > primary_b;
  if (a.metrics.expected_damage != b.metrics.expected_damage)
    return a.metrics.expected_damage > b.metrics.expected_damage;
  if (a.metrics.damage_probability != b.metrics.damage_probability)
    return a.metrics.damage_probability > b.metrics.damage_probability;
  if (a.margin != b.margin) return a.margin > b.margin;
  return game::dot(a.point, a.point) < game::dot(b.point, b.point);
}
}  // namespace

auto evaluate_aim(const SolveInput& input, Vec2 aim) -> AimMetrics {
  const auto p = prepare(input);
  return evaluate(input, p, aim);
}

auto solve(const SolveInput& input) -> SolveResult {
  SolveResult result;
  const auto p = prepare(input);
  if (!p.valid || p.field.empty()) return result;
  std::vector<Vec2> seeds;
  const auto add = [&](Vec2 point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
    for (Vec2 existing : seeds)
      if (game::length(existing - point) < 1e-7F) return;
    if (seeds.size() < static_cast<std::size_t>(std::clamp(input.policy.maximum_seeds, 1, 4096))) seeds.push_back(point);
  };
  // Every projected component gets proposals; never reduce to the highest raw
  // damage cell before considering the kernel or world geometry.
  // Seed every region once before spending the remaining proposal budget.
  std::vector<Vec2> centers;
  centers.reserve(p.regions.size());
  for (const auto& region : p.regions) {
    centers.push_back(chebyshev_center(region));
    add(centers.back());
  }
  for (std::size_t region_index = 0; region_index < p.regions.size(); ++region_index) {
    const auto& region = p.regions[region_index];
    const auto center = polygon_centroid(region), safe = centers[region_index];
    add(safe);
    add(center);
    add(nearest_point(region));
    const auto& vertices = region.points;
    const std::size_t edge_stride = std::max(std::size_t{1}, vertices.size() / 8);
    for (std::size_t i = 0; i < vertices.size(); i += edge_stride) {
      add(vertices[i] * .98F + center * .02F);
      add((vertices[i] + vertices[(i + 1) % vertices.size()]) * .49F + center * .02F);
    }
    // Include inverse-pattern proposals: with known nonzero shot offsets the
    // best aim can lie OUTSIDE the unspread target silhouette.
    const std::size_t stride = std::max(std::size_t{1}, p.kernel.size() / 8);
    for (std::size_t i = 0; i < p.kernel.size(); i += stride)
      if (const auto compensated = compensate_sample(input.basis, safe, p.kernel[i].offset))
        add(*compensated);
  }
  Prepared search = p;
  const std::size_t coarse_count =
      static_cast<std::size_t>(std::clamp(input.policy.search_samples, 1, 256));
  const bool coarse = p.kernel.size() > coarse_count;
  if (coarse) {
    search.kernel.clear();
    for (std::size_t group = 0; group < coarse_count; ++group) {
      const auto begin = group * p.kernel.size() / coarse_count,
                 end = (group + 1) * p.kernel.size() / coarse_count;
      Vec2 offset{};
      float weight = 0;
      for (std::size_t j = begin; j < end; ++j) {
        offset = offset + p.kernel[j].offset * p.kernel[j].weight;
        weight += p.kernel[j].weight;
      }
      if (weight > 0) search.kernel.push_back({offset * (1 / weight), weight});
    }
    if (search.poses.size() > 4) search.poses = pose_kernel(input.pose_sigma, 4);
  }
  std::vector<Vec2> finalists = centers;
  std::vector<Candidate> candidates;
  candidates.reserve(seeds.size());
  for (auto point : seeds)
    candidates.push_back({point, evaluate(input, search, point), clearance(p.field, point)});
  if (candidates.empty()) return result;
  const auto ordering = [&](const Candidate& a, const Candidate& b) { return better(input, a, b); };
  std::stable_sort(candidates.begin(), candidates.end(), ordering);
  Candidate best = candidates.front();
  for (std::size_t i = 0; i < std::min(candidates.size(), std::size_t{8}); ++i)
    finalists.push_back(candidates[i].point);
  const auto bounds = p.field.bounds();
  const float width = bounds.points[1].x - bounds.points[0].x;
  const float height = bounds.points[2].y - bounds.points[0].y;
  const float initial_step = std::max({width, height, input.pose_sigma, 1e-5F}) * .25F;
  const int starts =
      std::min(static_cast<int>(candidates.size()), std::clamp(input.policy.refine_starts, 0, 32));
  for (int i = 0; i < starts; ++i) {
    Candidate current = candidates[static_cast<std::size_t>(i)];
    float step = initial_step;
    for (int iteration = 0; iteration < std::clamp(input.policy.refine_iterations, 0, 12);
         ++iteration) {
      Candidate next = current;
      for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
          if (x == 0 && y == 0) continue;
          const Vec2 point =
              current.point + Vec2{static_cast<float>(x) * step, static_cast<float>(y) * step};
          Candidate tested{point, evaluate(input, search, point), clearance(p.field, point)};
          if (better(input, tested, next)) next = tested;
        }
      current = next;
      step *= .5F;
    }
    finalists.push_back(current.point);
    if (better(input, current, best)) best = current;
  }
  if (coarse) {
    bool have_best = false;
    for (Vec2 point : finalists) {
      Candidate tested{point, evaluate(input, p, point), clearance(p.field, point)};
      if (!have_best || better(input, tested, best)) {
        best = tested;
        have_best = true;
      }
    }
    // Full-kernel local correction, followed by the full-kernel acceptance below.
    float step = initial_step * .125F;
    for (int iteration = 0; iteration < 3; ++iteration) {
      Candidate next = best;
      for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
          if (x == 0 && y == 0) continue;
          Vec2 point =
              best.point + Vec2{static_cast<float>(x) * step, static_cast<float>(y) * step};
          Candidate tested{point, evaluate(input, p, point), clearance(p.field, point)};
          if (better(input, tested, next)) next = tested;
        }
      best = next;
      step *= .5F;
    }
  }
  if (!admissible(input, best.metrics)) return result;
  // Final exact-direction recheck: proposals do not substitute for the physical field.
  best.metrics = evaluate(input, p, best.point);
  if (!admissible(input, best.metrics)) return result;
  result.valid = true;
  result.aim = best.point;
  result.direction = input.basis.unproject(best.point);
  result.value = best.metrics.nominal_damage;
  result.metrics = best.metrics;
  result.cells = p.field.cells.size();
  result.spread_known = input.spread_known;
  return result;
}
}  // namespace cs2::aim
