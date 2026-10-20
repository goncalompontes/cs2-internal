#include "cs2/aim/heatmap.hpp"

#include <algorithm>
#include <cmath>

#include "cs2/aim/damage.hpp"

namespace cs2::aim {
namespace {

constexpr float k_pi = 3.14159265358979323846F;
constexpr float k_two_pi = 6.28318530717958647692F;

// Unwrap a chart yaw `u` to within pi of `centre` so a view window that straddles the
// +-pi seam is handled.
[[nodiscard]] auto unwrap_yaw(float u, float centre) noexcept -> float {
  while (u - centre > k_pi) {
    u -= k_two_pi;
  }
  while (u - centre < -k_pi) {
    u += k_two_pi;
  }
  return u;
}

// The hitgroup id of the head (`HITGROUP_HEAD`). The head's multiplier is the
// weapon's, independent of how the hitbox was sampled, so a lag record and the
// live pose are valued by the same weapon model.
constexpr int k_hitgroup_head = 1;

// The hitbox centre.
[[nodiscard]] auto hitbox_center(const Hitbox& hitbox) noexcept -> Vec3 {
  return hitbox.is_capsule ? (hitbox.capsule.a + hitbox.capsule.b) * 0.5F : hitbox.box.center;
}

}  // namespace

auto clip_to_view(const Polygon& polygon, const Vec2& view, float extent) -> Polygon {
  if (polygon.empty() || extent <= 0.0F) {
    return polygon;
  }
  const float half_v = extent < k_pi * 0.5F ? extent : k_pi * 0.5F;
  Polygon clipped = polygon;
  for (Vec2& point : clipped.points) {
    point.x = unwrap_yaw(point.x, view.x);
  }
  clipped = clip_half_plane(clipped, Vec2{1.0F, 0.0F}, view.x + extent);    // u <= view.x + extent
  clipped = clip_half_plane(clipped, Vec2{-1.0F, 0.0F}, -(view.x - extent));  // u >= view.x - extent
  clipped = clip_half_plane(clipped, Vec2{0.0F, 1.0F}, view.y + half_v);    // v <= view.y + half_v
  clipped = clip_half_plane(clipped, Vec2{0.0F, -1.0F}, -(view.y - half_v));  // v >= view.y - half_v
  return clipped;
}

auto build_heatmap(const Vec3& eye, const Basis& basis, float extent,
                   const std::vector<TargetPose>& poses, const HeatmapOptions& options, Vec2 view)
    -> Heatmap {
  Heatmap map;
  map.basis = basis;
  map.eye = eye;
  map.view = view;
  map.extent = extent;
  map.weapon = options.weapon;

  std::vector<char> used(options.occlusion.size(), 0);

  for (const TargetPose& pose : poses) {
    for (const Hitbox& hitbox : pose.hitboxes) {
      Polygon base = hitbox.is_capsule ? project_capsule(eye, hitbox.capsule.a, hitbox.capsule.b,
                                                         hitbox.capsule.radius, basis)
                                       : project_obb(eye, hitbox.box, basis);
      base = clip_to_view(base, view, extent);
      if (base.empty()) {
        continue;
      }

      const Vec3 center = hitbox_center(hitbox);
      const float distance = game::distance(eye, center);

      const float multiplier =
          hitbox.group == k_hitgroup_head ? options.weapon.headshot_multiplier : hitbox.multiplier;
      // Split the silhouette by the solids in front and value each fragment with the
      // chosen penetration model (the shared `occlude_polygon`, docs/ragebot.md §3).
      bool candidates_complete = true;
      const std::vector<DamageFragment> fragments = occlude_polygon(
          eye, basis, base, multiplier, distance, options.occlusion, options.weapon,
          options.penetration, options.penetration_params, candidates_complete, used);
      map.occlusion_candidates_complete = map.occlusion_candidates_complete && candidates_complete;
      for (const DamageFragment& fragment : fragments) {
        map.cells.push_back(HeatCell{fragment.polygon, fragment.value, fragment.base_value,
                                     pose.target, hitbox.group, pose.record, pose.age});
      }
    }
  }

  for (const Occluder& occluder : options.occluders) {
    for (HeatCell& cell : map.cells) {
      cell.polygon = clip_half_plane(cell.polygon, occluder.normal, occluder.offset);
    }
  }
  std::erase_if(map.cells, [](const HeatCell& cell) -> bool { return cell.polygon.empty(); });

  // Report only the solids that are actually between the eye and a target.
  for (std::size_t i = 0; i < used.size(); ++i) {
    if (used[i] != 0) {
      map.used_occluders.push_back(&options.occlusion[i]);
    }
  }
  return map;
}

}  // namespace cs2::aim
