#pragma once

// The aim damage field (docs/ragebot.md, ADR-0022): a set of convex cells in the
// chart, each carrying a damage value. Occluder silhouettes clip cells; the two
// optima are the nearest point (max damage) and the Chebyshev centre (max
// hitchance), and pose uncertainty is a Gaussian blur of the field.

#include <cmath>
#include <cstddef>
#include <vector>

#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

// A weapon's damage model (docs/re/aim.md — the `CCSWeaponBaseVData` fields).
struct WeaponProfile {
  float damage = 0.0F;             // m_nDamage
  float range_modifier = 1.0F;     // m_flRangeModifier
  float penetration = 1.0F;        // m_flPenetration
  float headshot_multiplier = 4.0F;
};

// Damage retained over `distance` world units (`range_modifier^(d/500)`).
[[nodiscard]] inline auto range_falloff(float range_modifier, float distance) noexcept -> float {
  return std::pow(range_modifier, distance / 500.0F);
}

// Damage fraction surviving a wall: `1 - thickness*modifier/penetration`. <= 0
// means the wall blocks the shot (docs/re/aim.md, the server penetration model).
[[nodiscard]] inline auto penetration_survival(float thickness, float surface_modifier,
                                               float weapon_penetration) noexcept -> float {
  if (weapon_penetration <= 0.0F) {
    return 0.0F;
  }
  return 1.0F - (thickness * surface_modifier) / weapon_penetration;
}

// One convex region of the chart with a uniform damage value.
struct Cell {
  Polygon polygon;
  float value = 0.0F;
};

// A damage field over the chart. Cells never overlap by construction: they are
// the hitbox silhouettes clipped by the occluder half-planes.
struct DamageField {
  std::vector<Cell> cells;

  [[nodiscard]] auto empty() const noexcept -> bool { return cells.empty(); }

  // Add a convex cell; empty polygons and non-positive values are dropped.
  void add(const Polygon& polygon, float value);

  // Add a hitbox cell with the weapon value model applied.
  void add_hitbox(const Polygon& polygon, float hitgroup_multiplier, float distance,
                  const WeaponProfile& weapon);

  // Clip every cell by the occluder half-plane `dot(normal, p) <= offset`;
  // vanished cells are dropped.
  void clip(const Vec2& normal, float offset);

  // The damage at a chart point (max over containing cells), or 0.
  [[nodiscard]] auto value_at(const Vec2& point) const noexcept -> float;

  // The highest-value cell (nullptr when empty).
  [[nodiscard]] auto best_cell() const noexcept -> const Cell*;

  // The aim maximizing damage: the best cell's point nearest the chart origin.
  [[nodiscard]] auto argmax_damage() const -> Vec2;

  // The aim maximizing margin: the best cell's Chebyshev centre.
  [[nodiscard]] auto argmax_hitchance() const -> Vec2;

  // The aim maximizing the pose-blurred field, `E[value]` under a Gaussian pose
  // kernel of angular std-dev `sigma`, by quadrature over the field bounds.
  [[nodiscard]] auto argmax_expected(float sigma, int grid = 48) const -> Vec2;

  // The union bounding box of the cells.
  [[nodiscard]] auto bounds() const noexcept -> Polygon;
};

}  // namespace cs2::aim
