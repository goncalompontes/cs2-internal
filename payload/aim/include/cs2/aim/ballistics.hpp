#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/hitbox.hpp"

namespace cs2::aim {

struct RayInterval {
  float enter = 0.0F;
  float exit = 0.0F;
};
[[nodiscard]] auto intersect_box(Vec3 eye, Vec3 direction, const Obb& box, float range)
    -> std::optional<RayInterval>;
[[nodiscard]] auto intersect_capsule(Vec3 eye, Vec3 direction, const Capsule& capsule, float range)
    -> std::optional<float>;

// Optional native world geometry. Opaque by default. Transmission is a supplied
// material model, NOT a recovered CS2 penetration formula.
struct WorldVolume {
  Obb box;
  float retained_per_unit = 0.0F;
};

struct TargetDefense {
  int armor = 0;
  bool helmet = false;
  std::uintptr_t pawn = 0;  // optional identity for a calibrated target backend
};
struct WorldDamage {
  float damage = 0.0F;  // after world/range loss, before target/hitgroup scaling
  bool known = false;   // complete coverage for this ray
  int hitgroup = -1;    // verified engine contact; -1 keeps the geometric group
};
using WorldDamageQuery = std::function<WorldDamage(Vec3, Vec3, float, const WeaponProfile&)>;

using TargetDamageQuery =
    std::function<float(float, const Hitbox&, const WeaponProfile&, const TargetDefense&)>;

struct RayDamage {
  float damage = 0.0F;
  float distance = 0.0F;
  int hitbox = -1;
  bool world_known = false;
};

// First intersected target surface wins, even when a higher multiplier lies
// behind it. World intervals past that surface cannot affect the shot.
[[nodiscard]] auto evaluate_ray(Vec3 eye, Vec3 direction, std::span<const Hitbox> hitboxes,
                                const WeaponProfile& weapon, const TargetDefense& defense,
                                std::span<const WorldVolume> world, bool world_complete,
                                const WorldDamageQuery& query = {},
                                std::span<const std::size_t> hitbox_indices = {},
                                const TargetDamageQuery& target_query = {}) -> RayDamage;

struct KernelSample {
  Vec2 offset{};
  float weight = 1.0F;
};
// 64-node deterministic quadrature of the recovered two uniform-radius components.
//  This is an uncertainty distribution, not the engine's command-seeded RNG.
[[nodiscard]] auto spread_kernel(float spread, float inaccuracy, std::size_t count = 64)
    -> std::vector<KernelSample>;
[[nodiscard]] auto pose_kernel(float sigma, std::size_t count = 16) -> std::vector<KernelSample>;
[[nodiscard]] auto compensate_sample(const Basis& reference, Vec2 desired, Vec2 offset)
    -> std::optional<Vec2>;
[[nodiscard]] auto aimed_basis(const Basis& reference, Vec3 forward) noexcept -> Basis;

}  // namespace cs2::aim
