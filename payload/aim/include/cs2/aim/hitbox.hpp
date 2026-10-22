#pragma once

// Hitbox construction: turning a bone transform / a collision AABB into the
// world oriented box the aim geometry projects (docs/re/aim.md, docs/ragebot.md).

#include <array>

#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

// A real studio hitbox capsule in world space (`docs/re/aim.md`): the engine
// stores each hitbox as two bone-local endpoints plus a radius, and transforms
// them by the bone.
struct Capsule {
  Vec3 a{};
  Vec3 b{};
  float radius = 0.0F;
};

// A bone's world transform (the bone array at `ModelState + 0x80`, stride 0x20):
// a position and a unit quaternion `(x, y, z, w)`.
struct BoneTransform {
  Vec3 position{};
  float rotation[4]{0.0F, 0.0F, 0.0F, 1.0F};
};

// One hitbox as the solver consumes it.
struct Hitbox {
  Obb box;
  float multiplier = 1.0F;  // hitgroup multiplier;0 makes an opaque player blocker
  int group = 0;            // hitgroup id (diagnostics)
  // When true this is a real studio hitbox: a **capsule** (the segment
  // `capsule.a`-`capsule.b` thickened by `capsule.radius`) in world space, from
  // `read_model_hitboxes`. `box` is unused then.
  bool is_capsule = false;
  Capsule capsule{};
  int bone = -1;  // the bone it rides (diagnostics)
};

// Rotate `v` by `bone`'s quaternion.
[[nodiscard]] auto rotate(const BoneTransform& bone, const Vec3& v) noexcept -> Vec3;

// Build the world capsule for a studio hitbox: `local_mins`/`local_maxs` are the
// bone-local endpoints and `radius` the capsule radius.
[[nodiscard]] auto capsule_from_bone(const BoneTransform& bone, const Vec3& local_mins,
                                     const Vec3& local_maxs, float radius) noexcept -> Capsule;

// A raw hitbox from the model's runtime studio hitbox set (`docs/re/aim.md`):
// bone-local endpoints + the bone it rides.
struct ModelHitbox {
  Vec3 mins{};   // bone-local endpoint A
  Vec3 maxs{};   // bone-local endpoint B
  Vec3 world_a{};  // world endpoint A (bone-transformed)
  Vec3 world_b{};  // world endpoint B
  float radius = 0.0F;
  int bone = -1;
  int group = 0;  // m_nGroupId — the hitgroup id (1 = head)
  int shape = 0;  // m_nShapeType
  int mesh = -1;  // the render mesh the box was read from (diagnostics)
  BoneTransform transform{};  // the bone transform used (diagnostics)
  char name[24]{};
};

// An oriented box from a bone transform: the hitbox's local centre / half-extents
// rotated and translated by the bone's axes and origin (docs/re/aim.md — the
// bone array is `m_modelState + 0x80`, stride 0x20).
[[nodiscard]] auto hitbox_obb(const Vec3& bone_origin, const Vec3& axis_x, const Vec3& axis_y,
                              const Vec3& axis_z, const Vec3& local_center,
                              const Vec3& local_half) noexcept -> Obb;

// An axis-aligned box from world-space bounds (the collision-AABB proxy used
// until the studio hitbox set is wired).
[[nodiscard]] auto aabb_obb(const Vec3& mins, const Vec3& maxs) noexcept -> Obb;

// Split a player's world bounds into a head box (the top `head_height` units) and
// a body box, with the given hitgroup multipliers.
[[nodiscard]] auto split_player_bounds(const Vec3& mins, const Vec3& maxs, float head_height,
                                       float head_multiplier, float body_multiplier)
    -> std::array<Hitbox, 2>;

// Split a player into a head box centred on `eye` (the pawn's eye sits in the
// head — a real reference, unlike a guess from the collision AABB) and a body box
// below it.
[[nodiscard]] auto split_player_at_head(const Vec3& mins, const Vec3& maxs, const Vec3& eye,
                                        float head_half, float head_multiplier,
                                        float body_multiplier) -> std::array<Hitbox, 2>;

}  // namespace cs2::aim
