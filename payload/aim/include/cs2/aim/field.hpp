#pragma once

// The aim damage field (docs/ragebot.md, ADR-0022): a set of convex cells in the
// chart, each carrying a damage value. Occluder silhouettes clip cells; the two
// optima are the nearest point (max damage) and the Chebyshev centre (max
// hitchance), and pose uncertainty is a Gaussian blur of the field.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

// A weapon's damage model (docs/re/aim.md — the `CCSWeaponBaseVData` fields).
struct WeaponProfile {
  float damage = 0.0F;          // m_nDamage
  float range_modifier = 1.0F;  // m_flRangeModifier
  float penetration = 1.0F;     // m_flPenetration
  float headshot_multiplier = 4.0F;
  float spread = 0.0F;  // m_flSpread (+0x750, FiringModeFloat primary)
  // The **effective** inaccuracy the engine computed for the current shot
  // (`CsPlayerPawn::m_flInterpolatedInaccuracy`, client `+0x1F50`) — the crosshair
  // value, in the same units as the `m_flInaccuracy*` terms. This is the engine's own
  // composition of the stance/move/jump terms, so it needs no formula. 0 when unknown.
  float inaccuracy = 0.0F;
  // The raw `m_flInaccuracy*` terms (fallback when the effective value is unavailable).
  float inaccuracy_stand = 0.0F;       // +0x760
  float inaccuracy_crouch = 0.0F;      // +0x758
  float inaccuracy_move = 0.0F;        // +0x788
  float inaccuracy_jump_initial = 0.0F;  // +0x7B8
  float inaccuracy_jump_apex = 0.0F;     // +0x7BC
};

// The inaccuracy for the shooter's state: the engine's effective value when the payload
// read it (`m_flInterpolatedInaccuracy`), else a first-order stand/move estimate from the
// raw VData terms (the exact composition `FUN_18080b880` still needs a live cross-check,
// `docs/re/aim.md` §"Inaccuracy").
[[nodiscard]] inline auto inaccuracy(const WeaponProfile& weapon, float speed, bool on_ground,
                                     bool crouched) noexcept -> float {
  if (weapon.inaccuracy > 0.0F) {
    return weapon.inaccuracy;
  }
  float base = on_ground ? (crouched ? weapon.inaccuracy_crouch : weapon.inaccuracy_stand)
                         : weapon.inaccuracy_jump_apex;
  // The move term scales with speed (the engine's `move = (speed/250) * …`).
  base += weapon.inaccuracy_move * std::clamp(speed / 250.0F, 0.0F, 1.0F);
  return base;
}

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
  // The damage before the walls in front (the unoccluded value), so the solve can
  // tell whether the chosen aim is actually attenuated (an "only shoot what you can
  // see" gate).
  float base_value = 0.0F;
};

// A damage field over the chart. Cells never overlap by construction: they are
// the hitbox silhouettes clipped by the occluder half-planes.
struct DamageField {
  std::vector<Cell> cells;

  [[nodiscard]] auto empty() const noexcept -> bool {
    return cells.empty();
  }

  // Add a convex cell; empty polygons and non-positive values are dropped. `base` is
  // the unoccluded value (`value` when not given).
  void add(const Polygon& polygon, float value, float base = -1.0F);

  // Add a hitbox cell with the weapon value model applied.
  void add_hitbox(const Polygon& polygon, float hitgroup_multiplier, float distance,
                  const WeaponProfile& weapon);

  // Clip every cell by the occluder half-plane `dot(normal, p) <= offset`;
  // vanished cells are dropped.
  void clip(const Vec2& normal, float offset);

  // The damage at a chart point (max over containing cells), or 0.
  [[nodiscard]] auto value_at(const Vec2& point) const noexcept -> float;

  // The unoccluded damage at a chart point (max over containing cells' `base_value`).
  [[nodiscard]] auto base_value_at(const Vec2& point) const noexcept -> float;

  // The **hitchance**: the probability a shot aimed at `aim` lands inside the target's
  // hitbox after the cone perturbs its direction. The cone is an isotropic angular
  // Gaussian of std-dev `sigma` (the weapon inaccuracy + spread); on the chart its yaw
  // half-width is `sigma / cos(pitch)` (the yaw/pitch metric), so the integral is over
  // a chart ellipse, not a circle. 0 when the target is not hit; 1 when the whole cone
  // is inside it. With `sigma == 0` this is the binary indicator.
  [[nodiscard]] auto hit_probability(const Vec2& aim, float sigma) const noexcept -> float;

  // The aim maximizing the hitchance (`hit_probability`) for the cone `sigma`.
  [[nodiscard]] auto argmax_hit_probability(float sigma, int grid = 40) const -> Vec2;

  // The pellet-sum field `Σ_k D(aim + offset_k)` — the deterministic spread model
  // (`docs/ragebot.md` §2/§3: a shot fires K pellets whose directions are
  // `f(a) + o_k`, so the aim maximizes the *sum*, not a single ray). An empty or
  // all-zero offset set reduces to `value_at`.
  [[nodiscard]] auto value_at_pellets(const Vec2& aim, std::span<const Vec2> offsets) const noexcept
      -> float;

  // The aim maximizing the pellet-sum field. With one pellet (or all-zero offsets)
  // this is exactly `argmax_damage`; otherwise the optimum lies on the arrangement of
  // the cells shifted by each `-offset_k`, so it is searched over the finite
  // candidate set of each cell's optima and vertices shifted by every `-offset_k`.
  [[nodiscard]] auto argmax_pellets(std::span<const Vec2> offsets) const -> Vec2;

  // The highest-value cell (nullptr when empty).
  [[nodiscard]] auto best_cell() const noexcept -> const Cell*;

  // The aim maximizing damage. The value is constant across a cell, so this is the
  // best cell's Chebyshev centre — the most robust point of the highest-damage cell.
  [[nodiscard]] auto argmax_damage() const -> Vec2;

  // The aim maximizing margin: the Chebyshev centre of the best cell (or of a
  // comparable-value cell with a meaningfully larger margin).
  [[nodiscard]] auto argmax_hitchance() const -> Vec2;

  // The aim maximizing the pose-blurred field, `E[value]` under a Gaussian pose
  // kernel of angular std-dev `sigma`, by quadrature over the field bounds.
  [[nodiscard]] auto argmax_expected(float sigma, int grid = 48) const -> Vec2;

  // The union bounding box of the cells.
  [[nodiscard]] auto bounds() const noexcept -> Polygon;
};

}  // namespace cs2::aim
