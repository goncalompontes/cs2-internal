#include "cs2/aim/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

// A deterministic low-discrepancy sample of a 2-D Gaussian (radial inverse-CDF +
// a golden-angle spiral), used to blur the field: `sigma_u`/`sigma_v` are the
// chart-space std-devs, and the chart's `u` (yaw) spans `1/cos(pitch)` more radians
// per true angular radian than `v` (pitch), so a physically isotropic cone is an
// ellipse in the chart.
[[nodiscard]] auto gaussian_offsets_aniso(float sigma_u, float sigma_v, std::size_t count)
    -> std::vector<Vec2> {
  std::vector<Vec2> offsets;
  if ((sigma_u <= k_epsilon && sigma_v <= k_epsilon) || count == 0) {
    offsets.push_back(Vec2{});
    return offsets;
  }
  constexpr float k_golden_angle = 2.39996322972865332F;  // pi*(3 - sqrt(5))
  offsets.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(count);
    const float radius = std::sqrt(-2.0F * std::log(1.0F - u));
    const float theta = static_cast<float>(i) * k_golden_angle;
    offsets.push_back(
        Vec2{radius * std::cos(theta) * sigma_u, radius * std::sin(theta) * sigma_v});
  }
  return offsets;
}

}  // namespace

namespace {

constexpr float k_field_epsilon = 1e-4F;

// The "interesting" chart points of the field, over *all* cells: each cell's Chebyshev
// centre, its centroid, its vertices, and its point nearest the field centroid. The
// optima are searched over this finite set plus the objective's own grid, so the answer
// is the global best point rather than a single cell's own optimum.
[[nodiscard]] auto field_candidates(const std::vector<Cell>& cells) -> std::vector<Vec2> {
  std::vector<Vec2> out;
  out.reserve(cells.size() * 12);
  Vec2 sum{};
  std::size_t points = 0;
  for (const Cell& cell : cells) {
    for (const Vec2& p : cell.polygon.points) {
      sum = sum + p;
      ++points;
    }
  }
  const Vec2 centre = points > 0 ? sum * (1.0F / static_cast<float>(points)) : Vec2{};
  for (const Cell& cell : cells) {
    out.push_back(chebyshev_center(cell.polygon));
    out.push_back(polygon_centroid(cell.polygon));
    out.push_back(nearest_point_to(cell.polygon, centre));
    for (const Vec2& p : cell.polygon.points) {
      out.push_back(p);
    }
  }
  out.push_back(centre);
  return out;
}

// The largest distance the aim at `point` can drift before it leaves the union of the
// cells currently covering it: the best per-cell margin among the cells that contain it.
[[nodiscard]] auto field_margin(const std::vector<Cell>& cells, const Vec2& point) -> float {
  float best = -1.0F;
  for (const Cell& cell : cells) {
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, polygon_margin(cell.polygon, point));
    }
  }
  return best;
}

}  // namespace

void DamageField::add(const Polygon& polygon, float value, float base) {
  if (polygon.empty() || value <= 0.0F) {
    return;
  }
  cells.push_back(Cell{polygon, value, base >= 0.0F ? base : value});
}

void DamageField::add_hitbox(const Polygon& polygon, float hitgroup_multiplier, float distance,
                             const WeaponProfile& weapon) {
  add(polygon,
      weapon.damage * hitgroup_multiplier * range_falloff(weapon.range_modifier, distance));
}

void DamageField::clip(const Vec2& normal, float offset) {
  for (Cell& cell : cells) {
    cell.polygon = clip_half_plane(cell.polygon, normal, offset);
  }
  std::erase_if(cells, [](const Cell& cell) { return cell.polygon.empty(); });
}

auto DamageField::value_at(const Vec2& point) const noexcept -> float {
  float best = 0.0F;
  for (const Cell& cell : cells) {
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, cell.value);
    }
  }
  return best;
}

auto DamageField::base_value_at(const Vec2& point) const noexcept -> float {
  float best = 0.0F;
  for (const Cell& cell : cells) {
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, cell.base_value);
    }
  }
  return best;
}

auto DamageField::hit_probability(const Vec2& aim, float sigma) const noexcept -> float {
  if (cells.empty()) {
    return 0.0F;
  }
  if (sigma <= k_epsilon) {
    return value_at(aim) > 0.0F ? 1.0F : 0.0F;
  }
  // The cone is isotropic on the sphere; on the chart its yaw half-width is
  // `sigma * yaw_scale(pitch)`. Ignoring this made the hitchance (and the fire gate)
  // wrong by `1/cos(pitch)` whenever the shot was not level.
  const std::vector<Vec2> kernel =
      gaussian_offsets_aniso(sigma * yaw_scale(aim.y), sigma, 32);
  int hits = 0;
  for (const Vec2& offset : kernel) {
    if (value_at(aim + offset) > 0.0F) {
      ++hits;
    }
  }
  return static_cast<float>(hits) / static_cast<float>(kernel.size());
}

auto DamageField::argmax_hit_probability(float sigma, int grid) const -> Vec2 {
  const Polygon box = bounds();
  if (box.empty()) {
    return Vec2{};
  }
  float min_x = box.points.front().x;
  float max_x = min_x;
  float min_y = box.points.front().y;
  float max_y = min_y;
  for (const Vec2& p : box.points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  Vec2 best = polygon_centroid(box);
  float best_p = -1.0F;
  const auto consider = [&](const Vec2& candidate) {
    const float p = hit_probability(candidate, sigma);
    if (p > best_p) {
      best_p = p;
      best = candidate;
    }
  };
  // The cell optima and vertices are the natural maxima of the (piecewise-constant)
  // probability, so seed the search with them, then refine on a grid.
  for (const Vec2& point : field_candidates(cells)) {
    consider(point);
  }
  const int n = std::max(grid, 1);
  const float step_x = (max_x - min_x) / static_cast<float>(n);
  const float step_y = (max_y - min_y) / static_cast<float>(n);
  for (int iy = 0; iy <= n; ++iy) {
    for (int ix = 0; ix <= n; ++ix) {
      consider(Vec2{min_x + static_cast<float>(ix) * step_x,
                    min_y + static_cast<float>(iy) * step_y});
    }
  }
  return best;
}

auto DamageField::value_at_pellets(const Vec2& aim, std::span<const Vec2> offsets) const noexcept
    -> float {
  if (offsets.empty()) {
    return value_at(aim);
  }
  float sum = 0.0F;
  for (const Vec2& offset : offsets) {
    sum += value_at(aim + offset);
  }
  return sum;
}

auto DamageField::argmax_pellets(std::span<const Vec2> offsets) const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // One pellet (or an all-zero offset set) is exactly the damage optimum.
  bool trivial = true;
  for (const Vec2& offset : offsets) {
    if (offset.x != 0.0F || offset.y != 0.0F) {
      trivial = false;
      break;
    }
  }
  if (trivial) {
    return argmax_damage();
  }

  // Candidate aims: each cell's own optima and vertices, and each of those shifted by
  // `-offset_k` so a candidate sits on a cell boundary reached by a pellet. The
  // pellet sum is constant on each region these points bound, so the maximum is one
  // of them (the design's "the optimum lies in a finite candidate set").
  std::vector<Vec2> candidates;
  candidates.reserve(cells.size() * (2 + 8) * (offsets.size() + 1));
  candidates.push_back(Vec2{});
  for (const Cell& cell : cells) {
    std::vector<Vec2> anchors;
    anchors.push_back(nearest_point(cell.polygon));
    anchors.push_back(chebyshev_center(cell.polygon));
    for (const Vec2& point : cell.polygon.points) {
      anchors.push_back(point);
    }
    for (const Vec2& anchor : anchors) {
      candidates.push_back(anchor);
      for (const Vec2& offset : offsets) {
        candidates.push_back(Vec2{anchor.x - offset.x, anchor.y - offset.y});
      }
    }
  }

  Vec2 best{};
  float best_value = -1.0F;
  for (const Vec2& candidate : candidates) {
    const float value = value_at_pellets(candidate, offsets);
    if (value > best_value + k_epsilon) {
      best_value = value;
      best = candidate;
    }
  }
  return best;
}

auto DamageField::best_cell() const noexcept -> const Cell* {
  const Cell* best = nullptr;
  for (const Cell& cell : cells) {
    if (best == nullptr || cell.value > best->value) {
      best = &cell;
    }
  }
  return best;
}

auto DamageField::argmax_damage() const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // A **global** search over every cell's candidate points (not "the best cell's own
  // optimum"): maximize the damage, then the local margin so a tie between a solid
  // chest fragment and a thin head sliver prefers the robust point. The value is
  // constant across a cell, so this returns the best point of the field as a whole.
  const std::vector<Vec2> candidates = field_candidates(cells);
  Vec2 best = candidates.front();
  float best_value = -1.0F;
  float best_margin = -1.0F;
  for (const Vec2& candidate : candidates) {
    const float value = value_at(candidate);
    const float margin = field_margin(cells, candidate);
    const bool better = value > best_value + k_field_epsilon ||
                        (std::fabs(value - best_value) <= k_field_epsilon &&
                         margin > best_margin + k_field_epsilon);
    if (better) {
      best = candidate;
      best_value = value;
      best_margin = margin;
    }
  }
  return best;
}

auto DamageField::argmax_hitchance() const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // Global: the largest margin across the whole field, but only among points that carry
  // a **comparable damage** (`>= 0.5 · best`) so a fat low-value limb cannot out-margin
  // the head (`docs/ragebot.md` §7). This is the best point of the field as a whole, not
  // of one fragment.
  const float best_value = best_cell() != nullptr ? best_cell()->value : 0.0F;
  const float floor_value = best_value * 0.5F;
  const std::vector<Vec2> candidates = field_candidates(cells);
  Vec2 best = candidates.front();
  float best_margin = -1.0F;
  float shared_value = -1.0F;
  for (const Vec2& candidate : candidates) {
    const float value = value_at(candidate);
    if (value < floor_value) {
      continue;
    }
    const float margin = field_margin(cells, candidate);
    const bool better = margin > best_margin + k_field_epsilon ||
                        (std::fabs(margin - best_margin) <= k_field_epsilon &&
                         value > shared_value + k_field_epsilon);
    if (better) {
      best = candidate;
      best_margin = margin;
      shared_value = value;
    }
  }
  return best;
}

auto DamageField::bounds() const noexcept -> Polygon {
  Polygon box;
  if (cells.empty()) {
    return box;
  }
  float min_x = cells.front().polygon.points.front().x;
  float max_x = min_x;
  float min_y = cells.front().polygon.points.front().y;
  float max_y = min_y;
  for (const Cell& cell : cells) {
    for (const Vec2& p : cell.polygon.points) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_y = std::min(min_y, p.y);
      max_y = std::max(max_y, p.y);
    }
  }
  box.points = {Vec2{min_x, min_y}, Vec2{max_x, min_y}, Vec2{max_x, max_y}, Vec2{min_x, max_y}};
  return box;
}

auto DamageField::argmax_expected(float sigma, int grid) const -> Vec2 {
  const Polygon box = bounds();
  if (box.empty()) {
    return Vec2{};
  }

  float min_x = box.points[0].x;
  float max_x = box.points[0].x;
  float min_y = box.points[0].y;
  float max_y = box.points[0].y;
  for (const Vec2& p : box.points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  const int n = std::max(grid, 1);
  const float step_x = (max_x - min_x) / static_cast<float>(n);
  const float step_y = (max_y - min_y) / static_cast<float>(n);

  Vec2 best = polygon_centroid(box);
  float best_value = -1.0F;
  for (int iy = 0; iy <= n; ++iy) {
    for (int ix = 0; ix <= n; ++ix) {
      const Vec2 candidate{min_x + static_cast<float>(ix) * step_x,
                           min_y + static_cast<float>(iy) * step_y};
      // The pose kernel is isotropic in world angles; on the chart its yaw spread is
      // `sigma * yaw_scale(pitch)` (the same metric fix as `hit_probability`).
      const std::vector<Vec2> kernel =
          gaussian_offsets_aniso(sigma * yaw_scale(candidate.y), sigma, 32);
      const float inv_kernel = 1.0F / static_cast<float>(kernel.size());
      float sum = 0.0F;
      for (const Vec2& offset : kernel) {
        sum += value_at(candidate + offset);
      }
      const float expected = sum * inv_kernel;
      if (expected > best_value) {
        best_value = expected;
        best = candidate;
      }
    }
  }
  return best;
}

}  // namespace cs2::aim
