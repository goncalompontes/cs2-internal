#pragma once

// The multi-target damage heatmap (docs/ragebot.md, ADR-0022). Unlike the single
// target `DamageField` (`field.hpp`), this projects **every** player's hitboxes —
// and every lag record of every player — into **one** shared angular chart, tagging
// each silhouette cell with the player and the record it came from. Layer 1 of the
// heatmap design: silhouettes -> damage, with the record information carried along.

#include <cstdint>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/geometry.hpp"
#include "cs2/aim/hitbox.hpp"
#include "cs2/aim/occlusion.hpp"
#include "cs2/aim/penetration.hpp"
#include "cs2/aim/solver.hpp"

namespace cs2::aim {

// How a wall stack reduces a hitbox's damage (`docs/re/penetration.md`).
//  - `simple`: the first-pass approximation, `prod(1 - t*mod/m_flPenetration)`.
//  - `engine`: the engine's real per-wall loss (`penetration_after_walls`), with the
//    per-surface penetration/damage modifiers, the 4-wall budget and the ≈90-unit
//    thickness cap.
enum class PenetrationModel : std::uint8_t { simple, engine };

// One projected hitbox silhouette in the shared chart, tagged with the player and
// the lag record it was sampled from.
struct HeatCell {
  Polygon polygon;
  float value = 0.0F;   // the damage at this cell (hitgroup x range x weapon, after walls)
  float base_value = 0.0F;  // the damage without the walls (for the penetration fraction)
  int target = -1;      // the player's entity index
  int hitgroup = 0;     // the hitgroup id (1 = head)
  int record = -1;      // the lag record's id (-1 = the newest / live pose)
  float age = 0.0F;     // how many records behind the newest pose (0 = newest)
};

// One sampled pose of one player: its hitboxes at a point in time. The newest
// (live) pose uses `record = -1`; a lag record carries the record's id and age.
struct TargetPose {
  int target = -1;
  int record = -1;
  float age = 0.0F;
  std::vector<Hitbox> hitboxes;
};

struct HeatmapOptions {
  WeaponProfile weapon{};
  std::vector<Occluder> occluders{};      // legacy analytic half-planes (unused yet)
  // The world solids between the eye and the target, already projected into the
  // chart (`project_occluder`). `build_heatmap` splits each hitbox cell by them and
  // scales the value by the surviving fraction — the whole occlusion field in one
  // pass, no per-ray probes.
  std::vector<OccluderCell> occlusion{};
  // How the covering solids reduce the damage (docs/re/penetration.md). The default
  // keeps the first-pass approximation; `engine` applies the real per-surface model.
  PenetrationModel penetration = PenetrationModel::simple;
  PenetrationParams penetration_params{};
};

struct Heatmap {
  Basis basis{};
  Vec3 eye{};
  // The chart is absolute world yaw/pitch. `view` is the camera direction (absolute
  // yaw/pitch, radians) the render is centred on; `extent` is the window half-width
  // (yaw) and the pitch half-height is `min(extent, pi/2)`. The values are a function
  // of the world only — the window is a view, not part of the computation.
  Vec2 view{};
  float extent = 0.0F;
  WeaponProfile weapon{};
  std::vector<HeatCell> cells;
  // The subset of `options.occlusion` that is actually **between the eye and a target**
  // — i.e. that overlapped a hitbox silhouette and passed the depth bound (the broad
  // phase). The renderer reports only these, not every solid the enumeration box caught.
  std::vector<const OccluderCell*> used_occluders;

  [[nodiscard]] auto empty() const noexcept -> bool { return cells.empty(); }
};

// Project every pose's hitboxes into the absolute `basis` about `eye`, value each by
// damage and clip by the occluders and the view window (`view` ± `extent`, absolute
// yaw/pitch). All players and records share the one chart, so the result is a single
// heatmap carrying the record information. `view` defaults to the chart origin.
[[nodiscard]] auto build_heatmap(const Vec3& eye, const Basis& basis, float extent,
                                 const std::vector<TargetPose>& poses,
                                 const HeatmapOptions& options, Vec2 view = Vec2{})
    -> Heatmap;

// Clip a convex chart polygon to the view window centred at `view` with half-width
// `extent` (yaw) and half-height `min(extent, pi/2)` (pitch). Yaw is unwrapped about
// `view.x` first, so a window that straddles the ±pi seam is handled.
[[nodiscard]] auto clip_to_view(const Polygon& polygon, const Vec2& view, float extent)
    -> Polygon;

}  // namespace cs2::aim
