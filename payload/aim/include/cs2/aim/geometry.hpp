#pragma once

// Angular geometry for the ragebot aim model (docs/ragebot.md, ADR-0022).
//
// Everything here works in a *chart*: the gnomonic (tangent-plane) projection of
// world directions about an aim direction. A direction `d` maps to
// (u, v) = (dot(d, right) / dot(d, forward), dot(d, up) / dot(d, forward)); for
// the small angles a shot covers this is (approximately) radians. The aim
// decision lives here: a hitbox is an oriented box whose silhouette is a convex
// polygon in the chart, and the damage field (field.hpp) is a set of such cells.
//
// The projection of a convex solid from a point is the convex hull of its
// projected vertices, so an oriented box reduces to 8 projections + a hull.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "cs2/game/math.hpp"

namespace cs2::aim {

using game::Vec2;
using game::Vec3;

// An orthonormal view basis about the aim direction (all unit length).
struct Basis {
  Vec3 forward{0.0F, 1.0F, 0.0F};
  Vec3 right{1.0F, 0.0F, 0.0F};
  Vec3 up{0.0F, 0.0F, 1.0F};

  // Gnomonic projection of a world direction (relative to the eye). `std::nullopt`
  // when the direction is at or behind the eye plane.
  [[nodiscard]] auto project(const Vec3& direction) const noexcept -> std::optional<Vec2>;

  // Inverse: a chart point back to a unit world direction.
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

// Project a sphere by sampling its3D tangent cone and projecting those rays.
// This preserves the off-axis conic shape; the polygon is still an approximation.
[[nodiscard]] auto project_sphere(const Vec3& eye, const Vec3& center, float radius,
                                  const Basis& basis, std::size_t segments = 16) -> Polygon;

// Project a capsule — the segment `a`-`b` thickened by `radius` — from `eye`.
// A capsule is the convex hull of two spheres and the gnomonic chart preserves
// convexity, so its silhouette is the convex hull of the two endpoint-sphere
// silhouettes. This is the real studio hitbox shape (`docs/re/aim.md`): the
// engine stores each hitbox as a capsule (two bone-local endpoints + a radius).
[[nodiscard]] auto project_capsule(const Vec3& eye, const Vec3& a, const Vec3& b, float radius,
                                   const Basis& basis, std::size_t segments = 16) -> Polygon;

// The view basis for Source eye angles (degrees) — the standard `AngleVectors`.
[[nodiscard]] auto basis_from_angles(const game::Angle& angles) noexcept -> Basis;

}  // namespace cs2::aim
