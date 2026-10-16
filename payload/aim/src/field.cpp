#include "cs2/aim/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

// A deterministic low-discrepancy sample of a 2-D Gaussian of std-dev `sigma`
// (radial inverse-CDF + a golden-angle spiral), used to blur the field.
[[nodiscard]] auto gaussian_offsets(float sigma, std::size_t count) -> std::vector<Vec2> {
  std::vector<Vec2> offsets;
  if (sigma <= k_epsilon || count == 0) {
    offsets.push_back(Vec2{});
    return offsets;
  }
  constexpr float k_golden_angle = 2.39996322972865332F;  // pi*(3 - sqrt(5))
  offsets.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(count);
    const float radius = sigma * std::sqrt(-2.0F * std::log(1.0F - u));
    const float theta = static_cast<float>(i) * k_golden_angle;
    offsets.push_back(Vec2{radius * std::cos(theta), radius * std::sin(theta)});
  }
  return offsets;
}

}  // namespace

void DamageField::add(const Polygon& polygon, float value) {
  if (polygon.empty() || value <= 0.0F) {
    return;
  }
  cells.push_back(Cell{polygon, value});
}

void DamageField::add_hitbox(const Polygon& polygon, float hitgroup_multiplier, float distance,
                             const WeaponProfile& weapon) {
  add(polygon, weapon.damage * hitgroup_multiplier * range_falloff(weapon.range_modifier, distance));
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
  const Cell* best = best_cell();
  return best == nullptr ? Vec2{} : nearest_point(best->polygon);
}

auto DamageField::argmax_hitchance() const -> Vec2 {
  const Cell* best = best_cell();
  return best == nullptr ? Vec2{} : chebyshev_center(best->polygon);
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

  const std::vector<Vec2> kernel = gaussian_offsets(sigma, 32);
  const float inv_kernel = 1.0F / static_cast<float>(kernel.size());

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
