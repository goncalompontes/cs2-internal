#include "cs2/aim/hitbox.hpp"

#include <algorithm>

namespace cs2::aim {

auto hitbox_obb(const Vec3& bone_origin, const Vec3& axis_x, const Vec3& axis_y,
                const Vec3& axis_z, const Vec3& local_center, const Vec3& local_half) noexcept
    -> Obb {
  return Obb{
      .center = bone_origin + axis_x * local_center.x + axis_y * local_center.y +
                axis_z * local_center.z,
      .axis_x = axis_x,
      .axis_y = axis_y,
      .axis_z = axis_z,
      .half = local_half,
  };
}

auto rotate(const BoneTransform& bone, const Vec3& v) noexcept -> Vec3 {
  // Unit quaternion `(x, y, z, w)`; `world = q · v · q*` (Hamilton product form).
  const float qx = bone.rotation[0];
  const float qy = bone.rotation[1];
  const float qz = bone.rotation[2];
  const float qw = bone.rotation[3];
  const float tx = 2.0F * (qy * v.z - qz * v.y);
  const float ty = 2.0F * (qz * v.x - qx * v.z);
  const float tz = 2.0F * (qx * v.y - qy * v.x);
  return Vec3{
      .x = v.x + qw * tx + (qy * tz - qz * ty),
      .y = v.y + qw * ty + (qz * tx - qx * tz),
      .z = v.z + qw * tz + (qx * ty - qy * tx),
  };
}

auto capsule_from_bone(const BoneTransform& bone, const Vec3& local_mins, const Vec3& local_maxs,
                       float radius) noexcept -> Capsule {
  return Capsule{
      .a = bone.position + rotate(bone, local_mins),
      .b = bone.position + rotate(bone, local_maxs),
      .radius = radius,
  };
}

auto aabb_obb(const Vec3& mins, const Vec3& maxs) noexcept -> Obb {
  return Obb{
      .center = Vec3{.x = (mins.x + maxs.x) * 0.5F,
                     .y = (mins.y + maxs.y) * 0.5F,
                     .z = (mins.z + maxs.z) * 0.5F},
      .half = Vec3{.x = (maxs.x - mins.x) * 0.5F,
                   .y = (maxs.y - mins.y) * 0.5F,
                   .z = (maxs.z - mins.z) * 0.5F},
  };
}

auto split_player_bounds(const Vec3& mins, const Vec3& maxs, float head_height,
                         float head_multiplier, float body_multiplier) -> std::array<Hitbox, 2> {
  const float height = std::max(maxs.z - mins.z, 0.0F);
  const float head = std::clamp(head_height, 0.0F, height);
  const float half = head * 0.5F;
  const float mid_x = (mins.x + maxs.x) * 0.5F;
  const float mid_y = (mins.y + maxs.y) * 0.5F;

  // The head is a **small** box on top of the AABB. The real head hitbox is ~8
  // units; the collision AABB is ~32 wide, so a full-width head box would aim at
  // its own (far) edge and miss the real head. A cube of `head` units centred on
  // the body's x/y keeps the aim on the head.
  const Hitbox head_box{
      .box = Obb{.center = Vec3{.x = mid_x, .y = mid_y, .z = maxs.z - half},
                 .half = Vec3{.x = half, .y = half, .z = half}},
      .multiplier = head_multiplier,
      .group = 1,
  };
  const Hitbox body_box{
      .box = aabb_obb(mins, Vec3{.x = maxs.x, .y = maxs.y, .z = maxs.z - head}),
      .multiplier = body_multiplier,
      .group = 2,
  };
  return {head_box, body_box};
}

auto split_player_at_head(const Vec3& mins, const Vec3& maxs, const Vec3& eye, float head_half,
                          float head_multiplier, float body_multiplier) -> std::array<Hitbox, 2> {
  const Hitbox head_box{
      .box = Obb{.center = eye, .half = Vec3{.x = head_half, .y = head_half, .z = head_half}},
      .multiplier = head_multiplier,
      .group = 1,
  };
  const float body_top = std::max(mins.z, eye.z - head_half);
  const Hitbox body_box{
      .box = aabb_obb(mins, Vec3{.x = maxs.x, .y = maxs.y, .z = body_top}),
      .multiplier = body_multiplier,
      .group = 2,
  };
  return {head_box, body_box};
}

}  // namespace cs2::aim
