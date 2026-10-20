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

// The wall layers the shot crosses **before the hitbox** along a fragment's (centroid)
// direction: the exact, span-filtered stack, sorted nearest-first (`docs/re/penetration.md`).
// Shared by the incremental block test and the final valuation so both use one definition.
[[nodiscard]] auto fragment_layers(const Fragment& fragment, const Vec3& eye, const Basis& basis,
                                   float distance) -> std::vector<PenetrationLayer> {
  std::vector<PenetrationLayer> layers;
  if (fragment.covering.empty()) {
    return layers;
  }
  layers.reserve(fragment.covering.size());
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
            [](const PenetrationLayer& a, const PenetrationLayer& b) { return a.entry < b.entry; });
  return layers;
}

// The damage a fragment carries for its current wall stack.
[[nodiscard]] auto fragment_value(std::span<const PenetrationLayer> layers, float base,
                                  float hitgroup_multiplier, float distance,
                                  const WeaponProfile& weapon, PenetrationModel model,
                                  const PenetrationParams& params) -> float {
  if (model == PenetrationModel::engine) {
    const float damage = penetration_damage_at_target(weapon.damage, weapon.range_modifier,
                                                      distance, params, layers);
    return damage * hitgroup_multiplier;
  }
  float transmission = 1.0F;
  for (const PenetrationLayer& layer : layers) {
    transmission *=
        penetration_survival(layer.thickness, layer.enter.penetration_modifier, weapon.penetration);
    if (transmission <= 0.0F) {
      break;
    }
  }
  return transmission <= 0.0F ? 0.0F : base * transmission;
}

}  // namespace

auto occlude_polygon(const Vec3& eye, const Basis& basis, const Polygon& silhouette,
                     float multiplier, float distance, std::span<const OccluderCell> occlusion,
                     const WeaponProfile& weapon, PenetrationModel model,
                     const PenetrationParams& params, bool& candidates_complete,
                     std::span<char> used) -> std::vector<DamageFragment> {
  candidates_complete = true;
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
  // the hitbox is pruned without ever dropping one in front. The survivors are ranked
  // nearest-first and the farthest are capped (see `k_max_candidate_occluders`).
  std::vector<std::pair<float, const OccluderCell*>> ranked;
  ranked.reserve(occlusion.size());
  for (const OccluderCell& occluder : occlusion) {
    if (!polygon_bounds_overlap(silhouette, occluder.polygon)) {
      continue;
    }
    const float lower = occluder.lower_bound_distance(eye);
    if (lower >= distance) {
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
    ranked.emplace_back(lower, &occluder);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // Temporary work cap (`PenetrationParams::max_occluders`): keep the nearest solids only.
  // This is not safe pruning, so report overflow; the aim solver fails closed rather than
  // using the resulting partial fragments as a score.
  if (params.max_occluders > 0 && ranked.size() > static_cast<std::size_t>(params.max_occluders)) {
    candidates_complete = false;
    ranked.resize(static_cast<std::size_t>(params.max_occluders));
  }

  std::vector<const OccluderCell*> candidates;
  candidates.reserve(ranked.size());
  for (const auto& entry : ranked) {
    candidates.push_back(entry.second);
    if (!used.empty()) {
      const std::size_t index = static_cast<std::size_t>(entry.second - occlusion.data());
      if (index < used.size()) {
        used[index] = 1;
      }
    }
  }

  // Split the silhouette by each candidate solid's silhouette, **nearest-first**. A
  // fragment is dropped as soon as its exact value is already 0 (it can only decrease as
  // more walls are added) — that is the engine's 4-wall budget / `< 0.1` modifier / `< 1`
  // damage stop, computed through the same model as the final valuation, so the result is
  // identical to valuing every arrangement cell (`docs/ragebot-perf.md` §3.7). No cap.
  const auto max_penetrations =
      params.max_penetrations > 0 ? static_cast<std::size_t>(params.max_penetrations) : 0;
  std::vector<Fragment> fragments;
  fragments.push_back(Fragment{silhouette, {}});
  for (const OccluderCell* occluder : candidates) {
    // The occluder's edge planes are invariant across all fragments generated by this
    // clip. Prepare them once instead of rebuilding/normalizing them per fragment.
    const ConvexClipper clipper = prepare_convex_clipper(occluder->polygon);
    std::vector<Fragment> next;
    next.reserve(fragments.size() * 2);
    for (Fragment& fragment : fragments) {
      // Only fragments that could already be over budget need the (relatively costly)
      // exact block test; a fragment with fewer coverings is kept and split.
      if (fragment.covering.size() > max_penetrations &&
          fragment_value(fragment_layers(fragment, eye, basis, distance), base, multiplier,
                         distance, weapon, model, params) <= 0.0F) {
        continue;  // provably blocked — identical to the value the full split would give
      }
      Polygon inside;
      std::vector<Polygon> outside;
      subtract_convex(fragment.polygon, clipper, inside, outside);
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
    if (fragments.empty()) {
      break;
    }
  }

  for (Fragment& fragment : fragments) {
    if (fragment.polygon.empty()) {
      continue;
    }
    const std::vector<PenetrationLayer> layers = fragment_layers(fragment, eye, basis, distance);
    const float value = fragment_value(layers, base, multiplier, distance, weapon, model, params);
    if (value <= 0.0F) {
      continue;  // blocked
    }
    out.push_back(DamageFragment{fragment.polygon, value, base});
  }
  return out;
}

}  // namespace cs2::aim
