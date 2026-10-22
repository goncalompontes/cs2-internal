#pragma once

// The aim solver: build the damage field from hitbox silhouettes and occluders,
// then pick the aim (docs/ragebot.md, ADR-0022).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cs2/aim/ballistics.hpp"
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

struct AimPolicy {
  float minimum_damage = 1.0F;
  float minimum_probability = 0.0F;  // probability of dealing minimum_damage
  bool require_known_world = false;
  bool require_known_spread = false;
  int search_samples = 8;  // coarse proposal kernel; final acceptance uses every sample
  int maximum_seeds = 4096;
  int refine_starts = 8;
  int refine_iterations = 6;
};

struct AimMetrics {
  float nominal_damage = 0.0F;
  float expected_damage = 0.0F;
  float hit_probability = 0.0F;
  float damage_probability = 0.0F;
  bool world_known = false;
  int nominal_hitbox = -1;
  std::size_t rays = 0;
};

struct SolveInput {
  Vec3 eye{};
  Basis basis{};
  std::vector<Hitbox> hitboxes;
  std::vector<Occluder> occluders;
  WeaponProfile weapon{};
  bool weapon_known = false;
  AimObjective objective = AimObjective::expected;
  std::vector<KernelSample> kernel;  // empty means an unspread ray, not verified zero spread
  bool spread_known = false;
  TargetDefense defense{};
  std::vector<WorldVolume> world;
  bool world_complete = false;
  WorldDamageQuery world_query;
  TargetDamageQuery target_query;
  AimPolicy policy{};
  float pose_sigma = 0.0F;  // pose-uncertainty blur (0 = none)
  std::uintptr_t pawn = 0;  // the target pawn (diagnostics / hitbox read)
};

struct SolveResult {
  bool valid = false;
  Vec2 aim{};          // chart point
  Vec3 direction{};    // world direction
  float value = 0.0F;  // field value at the aim
  std::size_t cells = 0;
  AimMetrics metrics{};
  bool spread_known = false;
};

// Query a point of the final weighted damage/probability heatmap. Kernel
// directions rotate with aim, matching the binary's normalize(F+xR+yU) path.
[[nodiscard]] auto evaluate_aim(const SolveInput& input, Vec2 aim) -> AimMetrics;

[[nodiscard]] auto solve(const SolveInput& input) -> SolveResult;

}  // namespace cs2::aim
