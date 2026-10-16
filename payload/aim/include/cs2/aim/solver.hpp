#pragma once

// The aim solver: build the damage field from hitbox silhouettes and occluders,
// then pick the aim (docs/ragebot.md, ADR-0022).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/hitbox.hpp"

namespace cs2::aim {

// An occluder silhouette half-plane in the chart (outward normal + offset):
// keeps `dot(normal, p) <= offset`.
struct Occluder {
  Vec2 normal;
  float offset = 0.0F;
};

enum class AimObjective {
  damage,     // nearest point of the best cell
  hitchance,  // Chebyshev centre of the best cell
  expected,   // pose-blurred field maximum
};

struct SolveInput {
  Vec3 eye{};
  Basis basis{};
  std::vector<Hitbox> hitboxes;
  std::vector<Occluder> occluders;
  WeaponProfile weapon{};
  AimObjective objective = AimObjective::damage;
  float pose_sigma = 0.0F;  // pose-uncertainty blur (0 = none)
  std::uintptr_t pawn = 0;  // the target pawn (diagnostics / hitbox read)
};

struct SolveResult {
  bool valid = false;
  Vec2 aim{};           // chart point
  Vec3 direction{};     // world direction
  float value = 0.0F;   // field value at the aim
  std::size_t cells = 0;
};

[[nodiscard]] auto solve(const SolveInput& input) -> SolveResult;

}  // namespace cs2::aim
