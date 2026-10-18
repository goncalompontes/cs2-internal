#pragma once

// Value a hitbox silhouette over the chart with the world occluders (docs/ragebot.md
// §3). The silhouette is split by the projected solids; each fragment is valued by the
// damage surviving the walls in front of it, using the chosen penetration model. Shared
// by the multi-target heatmap (`heatmap.cpp`, the render) and the single-target solver
// (`solver.cpp`, the aim) so both attenuate identically.

#include <span>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/geometry.hpp"
#include "cs2/aim/occlusion.hpp"
#include "cs2/aim/penetration.hpp"

namespace cs2::aim {

// One convex piece of a hitbox silhouette with the damage it carries.
struct DamageFragment {
  Polygon polygon;
  float value = 0.0F;       // the damage after the walls in front
  float base_value = 0.0F;  // the damage without any walls
};

// Split a projected hitbox silhouette (about `eye`/`basis`) by `occlusion` and value
// each fragment. `multiplier` is the resolved hitgroup multiplier and `distance` the
// eye→hitbox distance (for the range falloff and the per-solid depth test). `used`
// (optional, sized to `occlusion`) is marked for every solid that ended up between the
// eye and the hitbox — the renderer's "between" set. Solids behind the hitbox or merely
// overlapping its silhouette contribute nothing (the exact `OccluderCell::ray_span`
// depth test, `docs/re/occlusion.md`).
[[nodiscard]] auto occlude_polygon(const Vec3& eye, const Basis& basis, const Polygon& silhouette,
                                   float multiplier, float distance,
                                   std::span<const OccluderCell> occlusion,
                                   const WeaponProfile& weapon, PenetrationModel model,
                                   const PenetrationParams& params, std::span<char> used = {})
    -> std::vector<DamageFragment>;

}  // namespace cs2::aim
