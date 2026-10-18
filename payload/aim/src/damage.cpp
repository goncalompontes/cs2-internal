#include "cs2/aim/damage.hpp"

#include <algorithm>
#include <optional>

namespace cs2::aim {
namespace {

// Do two convex polygons' bounding boxes overlap? A conservative broad phase: it may
// keep a solid that turns out not to cover a cell, never the reverse.
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

// One region of the silhouette with the solids that cover it.
struct Fragment {
  Polygon polygon;
  std::vector<const OccluderCell*> covering;
};

}  // namespace

auto occlude_polygon(const Vec3& eye, const Basis& basis, const Polygon& silhouette,
                     float multiplier, float distance, std::span<const OccluderCell> occlusion,
                     const WeaponProfile& weapon, PenetrationModel model,
                     const PenetrationParams& params, std::span<char> used)
    -> std::vector<DamageFragment> {
  std::vector<DamageFragment> out;
  if (silhouette.empty()) {
    return out;
  }
  const float base = weapon.damage * multiplier * range_falloff(weapon.range_modifier, distance);
  if (base <= 0.0F) {
    return out;
  }

  // Broad phase (`docs/ragebot.md` §3): keep every solid whose chart silhouette can
  // overlap this hitbox's and that can be nearer than it. Overlap, not "the ray to the
  // centre enters it" (a solid covering only part of the silhouette still counts); the
  // depth bound is conservative (`lower_bound_distance`), so a solid entirely beyond
  // the hitbox is pruned without ever dropping one in front.
  std::vector<const OccluderCell*> candidates;
  candidates.reserve(occlusion.size());
  for (const OccluderCell& occluder : occlusion) {
    if (!polygon_bounds_overlap(silhouette, occluder.polygon) ||
        occluder.lower_bound_distance(eye) >= distance) {
      continue;
    }
    // A solid whose silhouette *fully contains* the hitbox is kept only if the centre
    // ray actually crosses it. For an exact convex projection this is always true; it
    // prunes the over-approximated silhouettes (a mesh's convex hull, a large/near
    // solid) that would otherwise fragment every hitbox. A partial cover is not
    // "contained", so it is always kept and split.
    bool contained = true;
    for (const Vec2& point : silhouette.points) {
      if (!polygon_contains(occluder.polygon, point)) {
        contained = false;
        break;
      }
    }
    if (contained) {
      const Vec3 centre_direction = basis.unproject(polygon_centroid(silhouette));
      if (!occluder.ray_span(eye, centre_direction).has_value()) {
        continue;
      }
    }
    const std::size_t index = static_cast<std::size_t>(&occluder - occlusion.data());
    if (index < used.size()) {
      used[index] = 1;
    }
    candidates.push_back(&occluder);
  }

  // Split the silhouette by each candidate solid's silhouette.
  std::vector<Fragment> fragments;
  fragments.push_back(Fragment{silhouette, {}});
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

  for (Fragment& fragment : fragments) {
    if (fragment.polygon.empty()) {
      continue;
    }
    // The solids this fragment's shot reaches *before* the hitbox, as the engine's
    // penetration stack: the exact in-front thickness per solid, sorted nearest-first
    // (docs/re/penetration.md).
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
    if (model == PenetrationModel::engine) {
      value = penetration_after_walls(base, params, layers);
    } else {
      float transmission = 1.0F;
      for (const PenetrationLayer& layer : layers) {
        transmission *= penetration_survival(layer.thickness, layer.enter.penetration_modifier,
                                             weapon.penetration);
        if (transmission <= 0.0F) {
          break;
        }
      }
      value = transmission <= 0.0F ? 0.0F : base * transmission;
    }
    if (value <= 0.0F) {
      continue;  // blocked
    }
    out.push_back(DamageFragment{fragment.polygon, value, base});
  }
  return out;
}

}  // namespace cs2::aim
