#pragma once

// Angular geometry for the ragebot aim model (docs/ragebot.md, ADR-0022).
//
// The chart is the **absolute world yaw/pitch** of a direction from the eye: a
// direction `d` maps to `(u, v) = (atan2(d.y, d.x), asin(d.z / |d|))` — yaw about the
// world Z axis, pitch above the world XY plane, both in radians. It is a function of
// the world geometry only, never the camera angles, so the projected hitbox/occluder
// cells do not move when the player looks around. (It used to be a gnomonic tangent
// plane about the camera forward, which made the whole field view-relative.) For the
// small angles a shot covers the two agree to first order, so the local geometry math
// (convex hulls, clipping, the optima) is unchanged.
//
// The projection of a convex solid from a point is the convex hull of its projected
// vertices, so an oriented box reduces to 8 projections + a hull.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "cs2/game/math.hpp"

namespace cs2::aim {

using game::Vec2;
using game::Vec3;

// The absolute-world chart frame. `forward`/`right`/`up` are the world axes the chart
// is measured from (`+X` is the chart origin, `+Y` is increasing yaw, `+Z` increasing
// pitch); they are carried for diagnostics and the JSON, not for the projection.
struct Basis {
  Vec3 forward{1.0F, 0.0F, 0.0F};
  Vec3 right{0.0F, 1.0F, 0.0F};
  Vec3 up{0.0F, 0.0F, 1.0F};

  // The single absolute-world chart frame (yaw/pitch about the world axes).
  [[nodiscard]] static auto world() noexcept -> Basis { return Basis{}; }

  // The absolute world yaw/pitch (radians) of a direction from the eye. `std::nullopt`
  // only for a zero-length direction (a point at the eye).
  [[nodiscard]] auto project(const Vec3& direction) const noexcept -> std::optional<Vec2>;

  // Inverse: a chart point (yaw, pitch) back to a unit world direction.
  [[nodiscard]] auto unproject(const Vec2& point) const noexcept -> Vec3;
};

// An oriented box (a hitbox or a world brush). `axis_*` are unit and mutually
// orthogonal; `half` are the half-extents along them.
struct Obb {
  Vec3 center{};
  Vec3 axis_x{1.0F, 0.0F, 0.0F};
  Vec3 axis_y{0.0F, 1.0F, 0.0F};
  Vec3 axis_z{0.0F, 0.0F, 1.0F};
  Vec3 half{0.5F, 0.5F, 0.5F};

  [[nodiscard]] auto corners() const noexcept -> std::array<Vec3, 8>;
};

// A convex polygon in the chart, wound counter-clockwise.
struct Polygon {
  std::vector<Vec2> points;

  [[nodiscard]] auto empty() const noexcept -> bool { return points.size() < 3; }
};

// The convex hull of a point set (Andrew's monotone chain). Fewer than 3 points,
// or a degenerate (collinear) set, yields an empty polygon.
[[nodiscard]] auto convex_hull(std::span<const Vec2> points) -> Polygon;

[[nodiscard]] auto polygon_area(const Polygon& polygon) noexcept -> float;
[[nodiscard]] auto polygon_centroid(const Polygon& polygon) noexcept -> Vec2;
[[nodiscard]] auto polygon_contains(const Polygon& polygon, const Vec2& point) noexcept -> bool;

// Keep the part of a convex polygon where `dot(normal, p) <= offset` (unit
// `normal`). Sutherland-Hodgman; the result stays convex.
[[nodiscard]] auto clip_half_plane(const Polygon& polygon, const Vec2& normal, float offset)
    -> Polygon;

// The polygon point nearest the chart origin — the *damage* optimum (least
// range falloff / wall thickness). The origin itself when it is inside.
[[nodiscard]] auto nearest_point(const Polygon& polygon) -> Vec2;

// The centre of the polygon's largest inscribed circle — the *hitchance* optimum
// (maximum margin to the boundary). Empty polygon yields the origin.
[[nodiscard]] auto chebyshev_center(const Polygon& polygon) -> Vec2;

// Project an oriented box from `eye` into the chart. Empty when the box is not
// (mostly) in front of the eye.
[[nodiscard]] auto project_obb(const Vec3& eye, const Obb& box, const Basis& basis) -> Polygon;

// Project a sphere of `radius` at `center` (a head / limb hitbox). Approximates
// the exact silhouette conic by a circle of angular radius `asin(radius / dist)`
// about the projected centre (valid for the small angles of a shot).
[[nodiscard]] auto project_sphere(const Vec3& eye, const Vec3& center, float radius,
                                  const Basis& basis, std::size_t segments = 16) -> Polygon;

// Project a capsule — the segment `a`-`b` thickened by `radius` — from `eye`.
// A capsule is the convex hull of two spheres and the yaw/pitch chart preserves
// convexity for the small angles of a shot, so its silhouette is the convex hull of
// the two endpoint-sphere silhouettes. This is the real studio hitbox shape
// (`docs/re/aim.md`): the engine stores each hitbox as a capsule (two bone-local
// endpoints + a radius).
[[nodiscard]] auto project_capsule(const Vec3& eye, const Vec3& a, const Vec3& b, float radius,
                                   const Basis& basis, std::size_t segments = 16) -> Polygon;

}  // namespace cs2::aim
