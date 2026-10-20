#pragma once

// The hitbox-derived skeleton.
//
// The ESP's skeleton is built from the pawn's **studio hitbox capsules**: every
// capsule carries a hitgroup and its two world endpoints, and the capsule's
// midpoint is the bone position.
//
// It is **not** the model's real bone hierarchy — that would need the model's
// bone-name table and the per-bone parent links (extra RE). What the hitgroups
// give us is the anatomical chain the studio boxes imply:
//
//   head -> neck -> chest -> stomach -> pelvis
//   chest -> left arm  (upper arm -> forearm -> hand)
//   chest -> right arm (upper arm -> forearm -> hand)
//   pelvis -> left leg  (thigh -> calf -> foot)
//   pelvis -> right leg (thigh -> calf -> foot)
//
// A limb's capsules all share one hitgroup, so they are ordered by their
// distance from the limb's root: the upper arm is the closest to the chest,
// the hand the farthest. A capsule that is missing drops the segments that
// need it, so the chain is never broken by an absent box.
//
// The chain builder is pure, so the host test can check it — the ESP's maths
// cannot be verified in a live game.

#include <cstdint>
#include <vector>

namespace cs2::features::esp_skeleton {

// The hitgroups the studio hitbox set uses (`docs/re/aim.md`).
inline constexpr int k_hitgroup_head = 1;
inline constexpr int k_hitgroup_chest = 2;
inline constexpr int k_hitgroup_stomach = 3;
inline constexpr int k_hitgroup_left_arm = 4;
inline constexpr int k_hitgroup_right_arm = 5;
inline constexpr int k_hitgroup_left_leg = 6;
inline constexpr int k_hitgroup_right_leg = 7;
inline constexpr int k_hitgroup_generic = 10;  // the neck and the pelvis

// One capsule's midpoint, tagged with its hitgroup.
struct BonePoint {
  int group = 0;
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

// One skeleton segment, in world space.
struct Segment {
  float ax = 0.0F;
  float ay = 0.0F;
  float az = 0.0F;
  float bx = 0.0F;
  float by = 0.0F;
  float bz = 0.0F;
};

// The chain's segments, in world space.
[[nodiscard]] auto build_chain(const std::vector<BonePoint>& points) -> std::vector<Segment>;

}  // namespace cs2::features::esp_skeleton
