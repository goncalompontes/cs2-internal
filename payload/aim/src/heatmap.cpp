#include "cs2/aim/heatmap.hpp"

#include <algorithm>
#include <cmath>

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

// The damage this hitbox contributes *before* walls: `m_nDamage * multiplier *
// range_falloff` (docs/re/aim.md — the muzzle damage model). `transmission` is the
// fraction surviving the walls in front.
[[nodiscard]] auto hitbox_damage(const Hitbox& hitbox, float distance, float transmission,
                                 const WeaponProfile& weapon) noexcept -> float {
  const float multiplier =
      hitbox.group == k_hitgroup_head ? weapon.headshot_multiplier : hitbox.multiplier;
  return weapon.damage * multiplier * range_falloff(weapon.range_modifier, distance) * transmission;
}

// One region of a hitbox silhouette with a constant set of covering occluders.
struct Fragment {
  Polygon polygon;
  std::vector<const OccluderCell*> covering;
};

// Do two convex polygons' bounding boxes overlap? A conservative broad phase: it
// may keep a solid that turns out not to cover a cell, never the reverse.
[[nodiscard]] auto polygon_bounds_overlap(const Polygon& a, const Polygon& b) noexcept -> bool {
  if (a.empty() || b.empty()) {
    return false;
  }
  float a_min_x = a.points.front().x;
  float a_max_x = a_min_x;
  float a_min_y = a.points.front().y;
  float a_max_y = a_min_y;
  for (const Vec2& p : a.points) {
    a_min_x = std::min(a_min_x, p.x);
    a_max_x = std::max(a_max_x, p.x);
    a_min_y = std::min(a_min_y, p.y);
    a_max_y = std::max(a_max_y, p.y);
  }
  float b_min_x = b.points.front().x;
  float b_max_x = b_min_x;
  float b_min_y = b.points.front().y;
  float b_max_y = b_min_y;
  for (const Vec2& p : b.points) {
    b_min_x = std::min(b_min_x, p.x);
    b_max_x = std::max(b_max_x, p.x);
    b_min_y = std::min(b_min_y, p.y);
    b_max_y = std::max(b_max_y, p.y);
  }
  return a_max_x >= b_min_x && a_min_x <= b_max_x && a_max_y >= b_min_y && a_min_y <= b_max_y;
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

      // Broad phase: keep every solid whose chart silhouette can overlap this
      // hitbox's and that can be nearer than it (`docs/ragebot.md` §3 — occluders
      // cut Ω_i into cells, by *overlap* then by *depth*). Two points matter here:
      //  - overlap, not "the ray to the hitbox centre passes through it": a solid
      //    covering only *part* of the silhouette still counts (pruning by the
      //    centre ray silently dropped those partial covers);
      //  - the depth bound is a conservative lower bound (`lower_bound_distance`),
      //    so a solid entirely beyond the hitbox (the floor under the target, a
      //    wall behind it) is pruned without ever dropping a solid in front.
      std::vector<const OccluderCell*> candidates;
      candidates.reserve(options.occlusion.size());
      for (const OccluderCell& occluder : options.occlusion) {
        if (!polygon_bounds_overlap(base, occluder.polygon) ||
            occluder.lower_bound_distance(eye) >= distance) {
          continue;
        }
        // A solid whose chart silhouette *fully contains* the hitbox is kept only if
        // the centre ray actually crosses it. For an exact convex projection this is
        // always true (the silhouette is exactly the set of directions that hit the
        // solid), so it never drops a real occluder; it prunes the *over-approximated*
        // silhouettes — a mesh's convex hull, and the yaw/pitch hull of a large or
        // near solid — which would otherwise fragment every hitbox. A solid that covers
        // only *part* of the hitbox is not "contained", so it is always kept and split
        // (the partial-cover case is preserved).
        {
          bool contained = true;
          for (const Vec2& point : base.points) {
            if (!polygon_contains(occluder.polygon, point)) {
              contained = false;
              break;
            }
          }
          if (contained) {
            const Vec3 centre_direction = basis.unproject(polygon_centroid(base));
            if (!occluder.ray_span(eye, centre_direction).has_value()) {
              continue;
            }
          }
        }
        const std::size_t index = static_cast<std::size_t>(&occluder - options.occlusion.data());
        if (index < used.size()) {
          used[index] = 1;
        }
        candidates.push_back(&occluder);
      }

      // Split the silhouette by each candidate solid's silhouette; the fragments
      // then carry the exact solids that cover them.
      std::vector<Fragment> fragments;
      fragments.push_back(Fragment{base, {}});
      for (const OccluderCell* occluder : candidates) {
        std::vector<Fragment> next;
        for (const Fragment& fragment : fragments) {
          Polygon inside;
          std::vector<Polygon> outside;
          subtract_convex(fragment.polygon, occluder->polygon, inside, outside);
          if (!inside.empty()) {
            Fragment covered;
            covered.polygon = std::move(inside);
            covered.covering = fragment.covering;
            covered.covering.push_back(occluder);
            next.push_back(std::move(covered));
          }
          for (Polygon& piece : outside) {
            next.push_back(Fragment{std::move(piece), fragment.covering});
          }
        }
        fragments = std::move(next);
      }

      const float base_damage = hitbox_damage(hitbox, distance, 1.0F, options.weapon);
      if (base_damage <= 0.0F) {
        continue;
      }
      for (Fragment& fragment : fragments) {
        if (fragment.polygon.empty()) {
          continue;
        }
        // The solids this fragment's shot reaches *before* the hitbox, as the
        // engine's penetration stack: the exact in-front thickness per solid, sorted
        // nearest-first (docs/re/penetration.md). The depth test is the key step —
        // a solid the cell merely overlaps (the floor under the target, a wall
        // behind it) contributes nothing.
        std::vector<PenetrationLayer> layers;
        layers.reserve(fragment.covering.size());
        if (!fragment.covering.empty()) {
          const Vec3 direction = basis.unproject(polygon_centroid(fragment.polygon));
          for (const OccluderCell* occluder : fragment.covering) {
            const std::optional<RaySpan> span = occluder->ray_span(eye, direction);
            if (!span.has_value() || span->entry >= distance) {
              continue;
            }
            const float thickness = std::min(span->exit, distance) - span->entry;
            if (thickness <= 0.0F) {
              continue;
            }
            PenetrationLayer layer;
            layer.thickness = thickness;
            layer.entry = span->entry;
            layer.enter.penetration_modifier = occluder->surface_modifier;
            layer.enter.damage_modifier = occluder->damage_modifier;
            layer.enter.material = occluder->material;
            layer.enter.grate = occluder->grate;
            layer.exit = layer.enter;  // one surface per brush in our enumeration
            layers.push_back(layer);
          }
          std::sort(layers.begin(), layers.end(),
                    [](const PenetrationLayer& a, const PenetrationLayer& b) {
                      return a.entry < b.entry;
                    });
        }

        float value = 0.0F;
        if (options.penetration == PenetrationModel::engine) {
          // The engine's real per-wall loss (per-surface modifiers, the 4-wall
          // budget, the ≈90-unit crossed-thickness cap).
          value = penetration_after_walls(base_damage, options.penetration_params, layers);
        } else {
          // The first-pass approximation: a product of per-wall survival fractions.
          float transmission = 1.0F;
          for (const PenetrationLayer& layer : layers) {
            transmission *= penetration_survival(layer.thickness, layer.enter.penetration_modifier,
                                                 options.weapon.penetration);
            if (transmission <= 0.0F) {
              break;
            }
          }
          value = transmission <= 0.0F ? 0.0F : base_damage * transmission;
        }
        if (value <= 0.0F) {
          continue;  // blocked
        }
        map.cells.push_back(HeatCell{fragment.polygon, value, base_damage, pose.target, hitbox.group,
                                     pose.record, pose.age});
      }
    }
  }

  for (const Occluder& occluder : options.occluders) {
    for (HeatCell& cell : map.cells) {
      cell.polygon = clip_half_plane(cell.polygon, occluder.normal, occluder.offset);
    }
  }
  std::erase_if(map.cells, [](const HeatCell& cell) { return cell.polygon.empty(); });

  // Report only the solids that are actually between the eye and a target.
  for (std::size_t i = 0; i < used.size(); ++i) {
    if (used[i] != 0) {
      map.used_occluders.push_back(&options.occlusion[i]);
    }
  }
  return map;
}

}  // namespace cs2::aim
