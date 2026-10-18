#include "cs2/aim/occlusion.hpp"

#include <algorithm>
#include <cmath>

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

struct HalfPlane {
  Vec2 normal;
  float offset;
};

// The inward half-planes of a convex CCW polygon: inside is `dot(normal, p) <= offset`.
[[nodiscard]] auto inward_edges(const Polygon& polygon) -> std::vector<HalfPlane> {
  std::vector<HalfPlane> edges;
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return edges;
  }
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2 a = polygon.points[i];
    const Vec2 b = polygon.points[(i + 1) % n];
    const Vec2 e = b - a;
    const float len = game::length(e);
    if (len <= k_epsilon) {
      continue;
    }
    const Vec2 normal{e.y / len, -e.x / len};
    edges.push_back(HalfPlane{normal, game::dot(normal, a)});
  }
  return edges;
}

// Distance from `p` to the segment `a`-`b` (used by the capsule lower bound).
[[nodiscard]] auto point_segment_distance(const Vec3& p, const Vec3& a, const Vec3& b) noexcept
    -> float {
  const Vec3 ab = b - a;
  const float denom = game::dot(ab, ab);
  if (denom <= k_epsilon) {
    return game::distance(p, a);
  }
  return game::distance(p, a + ab * std::clamp(game::dot(p - a, ab) / denom, 0.0F, 1.0F));
}

// The ray interval inside a sphere `center`,`radius` (exact; `direction` unit).
[[nodiscard]] auto ray_sphere_span(const Vec3& eye, const Vec3& direction, const Vec3& center,
                                   float radius) noexcept -> std::optional<RaySpan> {
  const Vec3 oc = eye - center;
  const float b = game::dot(oc, direction);
  const float c = game::dot(oc, oc) - radius * radius;
  const float disc = b * b - c;
  if (disc < 0.0F) {
    return std::nullopt;
  }
  const float s = std::sqrt(disc);
  const float t_exit = -b + s;
  if (t_exit < 0.0F) {
    return std::nullopt;  // entirely behind the eye
  }
  return RaySpan{std::max(-b - s, 0.0F), t_exit};
}

// Where the ray is inside the *infinite* cylinder of `radius` about the axis through
// `a` in unit direction `axis`, clipped to the segment's axial extent [0, length].
[[nodiscard]] auto ray_cylinder_span(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                     const Vec3& axis, float length, float radius) noexcept
    -> std::optional<RaySpan> {
  const Vec3 oc = eye - a;
  const float d_par = game::dot(direction, axis);
  const float o_par = game::dot(oc, axis);
  const Vec3 d_perp = direction - axis * d_par;
  const Vec3 o_perp = oc - axis * o_par;
  const float quad_a = game::dot(d_perp, d_perp);
  const float quad_b = 2.0F * game::dot(o_perp, d_perp);
  const float quad_c = game::dot(o_perp, o_perp) - radius * radius;
  float t_enter = -1.0e30F;
  float t_exit = 1.0e30F;
  if (quad_a > k_epsilon) {
    const float disc = quad_b * quad_b - 4.0F * quad_a * quad_c;
    if (disc < 0.0F) {
      return std::nullopt;
    }
    const float s = std::sqrt(disc);
    t_enter = (-quad_b - s) / (2.0F * quad_a);
    t_exit = (-quad_b + s) / (2.0F * quad_a);
  } else if (quad_c > 0.0F) {
    return std::nullopt;  // parallel to the axis and outside the radius
  }
  // Clip to the slab where the axial coordinate stays within the segment.
  if (std::fabs(d_par) <= k_epsilon) {
    if (o_par < 0.0F || o_par > length) {
      return std::nullopt;
    }
  } else {
    float s0 = (0.0F - o_par) / d_par;
    float s1 = (length - o_par) / d_par;
    if (s0 > s1) {
      std::swap(s0, s1);
    }
    t_enter = std::max(t_enter, s0);
    t_exit = std::min(t_exit, s1);
  }
  if (t_enter > t_exit) {
    return std::nullopt;
  }
  return RaySpan{t_enter, t_exit};
}

// The ray interval inside a capsule (`a`-`b` thickened by `radius`). A capsule is the
// union of the two cap spheres and the side cylinder, all convex sub-parts of one
// convex solid, so the union of their intervals is the solid's single interval.
[[nodiscard]] auto ray_capsule_span(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                    const Vec3& b, float radius) noexcept
    -> std::optional<RaySpan> {
  float entry = 1.0e30F;
  float exit = -1.0e30F;
  bool hit = false;
  const auto consider = [&](const std::optional<RaySpan>& span) {
    if (span.has_value()) {
      entry = std::min(entry, span->entry);
      exit = std::max(exit, span->exit);
      hit = true;
    }
  };
  consider(ray_sphere_span(eye, direction, a, radius));
  consider(ray_sphere_span(eye, direction, b, radius));
  const Vec3 ab = b - a;
  const float length = game::length(ab);
  if (length > k_epsilon) {
    consider(ray_cylinder_span(eye, direction, a, ab * (1.0F / length), length, radius));
  }
  if (!hit || exit < 0.0F) {
    return std::nullopt;
  }
  return RaySpan{std::max(entry, 0.0F), exit};
}

// Möller–Trumbore ray/triangle intersection; the ray distance when it hits the
// triangle at `t > 0`, else nullopt. Exact for a collision-mesh triangle.
[[nodiscard]] auto ray_triangle(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                const Vec3& b, const Vec3& c) noexcept -> std::optional<float> {
  const Vec3 e1 = b - a;
  const Vec3 e2 = c - a;
  const Vec3 p = game::cross(direction, e2);
  const float det = game::dot(e1, p);
  if (std::fabs(det) < 1e-9F) {
    return std::nullopt;
  }
  const float inv = 1.0F / det;
  const Vec3 tvec = eye - a;
  const float u = game::dot(tvec, p) * inv;
  if (u < 0.0F || u > 1.0F) {
    return std::nullopt;
  }
  const Vec3 q = game::cross(tvec, e1);
  const float v = game::dot(direction, q) * inv;
  if (v < 0.0F || u + v > 1.0F) {
    return std::nullopt;
  }
  const float t = game::dot(e2, q) * inv;
  if (t <= 0.0F) {
    return std::nullopt;
  }
  return t;
}

// The ray interval inside a collision **mesh** (a triangle soup): the nearest two
// triangle crossings are the entry and the exit of the first solid the ray enters.
// A mesh is not convex, so this is a triangle intersection, not a half-space clip
// (`docs/re/vphysics2.md` — the map's large/merged surfaces are meshes). `triangles`
// is a flat list of world-space points, 3 per triangle.
[[nodiscard]] auto ray_mesh_span(const Vec3& eye, const Vec3& direction,
                                 const std::vector<Vec3>& triangles) noexcept
    -> std::optional<RaySpan> {
  float t0 = 1.0e30F;
  float t1 = 1.0e30F;
  for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
    if (const auto t = ray_triangle(eye, direction, triangles[i], triangles[i + 1],
                                    triangles[i + 2])) {
      if (*t < t0) {
        t1 = t0;
        t0 = *t;
      } else if (*t < t1) {
        t1 = *t;
      }
    }
  }
  if (t0 >= 1.0e30F) {
    return std::nullopt;
  }
  return RaySpan{t0, t1 >= 1.0e30F ? t0 : t1};
}

// Distance from `p` to an axis-aligned box (`mn`,`mx`) — 0 inside.
[[nodiscard]] auto point_aabb_distance(const Vec3& p, const Vec3& mn, const Vec3& mx) noexcept
    -> float {
  const float pv[3] = {p.x, p.y, p.z};
  const float lo[3] = {mn.x, mn.y, mn.z};
  const float hi[3] = {mx.x, mx.y, mx.z};
  float d2 = 0.0F;
  for (int i = 0; i < 3; ++i) {
    if (pv[i] < lo[i]) {
      const float d = lo[i] - pv[i];
      d2 += d * d;
    } else if (pv[i] > hi[i]) {
      const float d = pv[i] - hi[i];
      d2 += d * d;
    }
  }
  return std::sqrt(d2);
}

}  // namespace

auto OccluderCell::thickness_along(const Vec3& direction) const noexcept -> float {
  if (kind == SolidKind::sphere) {
    return 2.0F * radius;
  }
  if (kind == SolidKind::capsule) {
    return std::fabs(game::dot(b - a, direction)) + 2.0F * radius;
  }
  if (kind == SolidKind::mesh) {
    return 0.0F;  // the mesh's exact thickness comes from `ray_span`, not a support map
  }
  if (vertices.empty()) {
    return 0.0F;
  }
  float lo = game::dot(vertices.front(), direction);
  float hi = lo;
  for (const Vec3& v : vertices) {
    const float t = game::dot(v, direction);
    lo = std::min(lo, t);
    hi = std::max(hi, t);
  }
  return hi - lo;
}

auto OccluderCell::ray_span(const Vec3& eye, const Vec3& direction) const noexcept
    -> std::optional<RaySpan> {
  if (kind == SolidKind::sphere) {
    return ray_sphere_span(eye, direction, a, radius);
  }
  if (kind == SolidKind::capsule) {
    return ray_capsule_span(eye, direction, a, b, radius);
  }
  if (kind == SolidKind::mesh) {
    return ray_mesh_span(eye, direction, triangles);
  }
  if (planes.empty()) {
    return std::nullopt;
  }
  // Clip the ray against every inward half-space `dot(n, p) <= d`. A convex solid
  // is the intersection of those, so the surviving interval is exactly where the ray
  // is inside it — no BVH traversal, planar arithmetic only (ADR-0022 Tier 2).
  float t_enter = -1.0e30F;
  float t_exit = 1.0e30F;
  for (const OccluderPlane& plane : planes) {
    const float denom = game::dot(plane.normal, direction);
    const float num = plane.dist - game::dot(plane.normal, eye);
    if (std::fabs(denom) < k_epsilon) {
      if (num < 0.0F) {
        return std::nullopt;  // parallel to this face and outside it
      }
      continue;
    }
    const float t = num / denom;
    if (denom > 0.0F) {
      t_exit = std::min(t_exit, t);
    } else {
      t_enter = std::max(t_enter, t);
    }
    if (t_enter > t_exit) {
      return std::nullopt;
    }
  }
  if (t_exit < 0.0F) {
    return std::nullopt;  // entirely behind the eye
  }
  return RaySpan{std::max(t_enter, 0.0F), t_exit};
}

auto OccluderCell::lower_bound_distance(const Vec3& eye) const noexcept -> float {
  if (kind == SolidKind::sphere) {
    return std::max(0.0F, game::distance(eye, a) - radius);
  }
  if (kind == SolidKind::capsule) {
    return std::max(0.0F, point_segment_distance(eye, a, b) - radius);
  }
  if (kind == SolidKind::mesh) {
    // The mesh's world AABB is a superset of the solid, so the distance to the box
    // is a conservative lower bound (0 inside). Never drops a mesh in front.
    return point_aabb_distance(eye, a, b);
  }
  if (planes.empty()) {
    return 0.0F;  // no planes -> cannot bound; never prune (safe)
  }
  bool inside = true;
  float bound = 1.0e30F;
  for (const OccluderPlane& plane : planes) {
    const float len = game::length(plane.normal);
    if (len <= k_epsilon) {
      continue;
    }
    // Positive when the eye is on the outside of this face.
    const float outside = (game::dot(plane.normal, eye) - plane.dist) / len;
    if (outside > 0.0F) {
      inside = false;
      bound = std::min(bound, outside);
    }
  }
  return inside ? 0.0F : bound;
}

auto project_occluder(const Vec3& eye, const Basis& basis, std::vector<Vec3> world_vertices,
                      std::vector<OccluderPlane> planes, float surface_modifier)
    -> std::optional<OccluderCell> {
  std::vector<Vec2> projected;
  projected.reserve(world_vertices.size());
  for (const Vec3& v : world_vertices) {
    if (const auto p = basis.project(v - eye)) {
      projected.push_back(*p);
    }
  }
  Polygon polygon = convex_hull(projected);
  if (polygon.empty()) {
    return std::nullopt;
  }
  // Orient every face plane inward (`dot(n, x) <= d`), which is what `ray_span`
  // assumes. The engine's `RnHull_t` planes are `dot(n, x) = d` but may be stored
  // with the normal pointing either way, and the vertex centroid is inside the hull
  // by construction, so the centroid decides the sign — robust across builds.
  if (!planes.empty() && !world_vertices.empty()) {
    Vec3 centroid{};
    for (const Vec3& v : world_vertices) {
      centroid = centroid + v;
    }
    centroid = centroid * (1.0F / static_cast<float>(world_vertices.size()));
    int violations = 0;
    for (const OccluderPlane& plane : planes) {
      if (game::dot(plane.normal, centroid) > plane.dist) {
        ++violations;
      }
    }
    if (violations * 2 > static_cast<int>(planes.size())) {
      for (OccluderPlane& plane : planes) {
        plane.normal = plane.normal * -1.0F;
        plane.dist = -plane.dist;
      }
    }
  }
  OccluderCell cell;
  cell.polygon = std::move(polygon);
  cell.kind = SolidKind::polytope;
  cell.vertices = std::move(world_vertices);
  cell.planes = std::move(planes);
  cell.surface_modifier = surface_modifier;
  return cell;
}

auto project_sphere_occluder(const Vec3& eye, const Basis& basis, const Vec3& center, float radius,
                             float surface_modifier) -> std::optional<OccluderCell> {
  if (radius <= 0.0F) {
    return std::nullopt;
  }
  Polygon polygon = project_sphere(eye, center, radius, basis);
  if (polygon.empty()) {
    return std::nullopt;
  }
  OccluderCell cell;
  cell.polygon = std::move(polygon);
  cell.kind = SolidKind::sphere;
  cell.a = center;
  cell.radius = radius;
  cell.surface_modifier = surface_modifier;
  return cell;
}

auto project_capsule_occluder(const Vec3& eye, const Basis& basis, const Vec3& a, const Vec3& b,
                              float radius, float surface_modifier) -> std::optional<OccluderCell> {
  if (radius <= 0.0F) {
    return std::nullopt;
  }
  Polygon polygon = project_capsule(eye, a, b, radius, basis);
  if (polygon.empty()) {
    return std::nullopt;
  }
  OccluderCell cell;
  cell.polygon = std::move(polygon);
  cell.kind = SolidKind::capsule;
  cell.a = a;
  cell.b = b;
  cell.radius = radius;
  cell.surface_modifier = surface_modifier;
  return cell;
}

auto project_mesh_occluder(const Vec3& eye, const Basis& basis, std::vector<Vec3> vertices,
                           std::vector<Vec3> triangles, float surface_modifier)
    -> std::optional<OccluderCell> {
  if (vertices.empty() || triangles.size() < 3) {
    return std::nullopt;
  }
  std::vector<Vec2> projected;
  projected.reserve(vertices.size());
  for (const Vec3& v : vertices) {
    if (const auto p = basis.project(v - eye)) {
      projected.push_back(*p);
    }
  }
  Polygon polygon = convex_hull(projected);
  if (polygon.empty()) {
    return std::nullopt;
  }
  Vec3 mn = vertices.front();
  Vec3 mx = mn;
  for (const Vec3& v : vertices) {
    mn.x = std::min(mn.x, v.x);
    mn.y = std::min(mn.y, v.y);
    mn.z = std::min(mn.z, v.z);
    mx.x = std::max(mx.x, v.x);
    mx.y = std::max(mx.y, v.y);
    mx.z = std::max(mx.z, v.z);
  }
  OccluderCell cell;
  cell.polygon = std::move(polygon);
  cell.kind = SolidKind::mesh;
  cell.triangles = std::move(triangles);
  cell.a = mn;
  cell.b = mx;
  cell.surface_modifier = surface_modifier;
  return cell;
}

auto convex_hull_3d(std::span<const Vec3> points) -> std::vector<OccluderPlane> {
  std::vector<OccluderPlane> planes;
  const std::size_t n = points.size();
  if (n < 4) {
    return planes;
  }
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      for (std::size_t k = j + 1; k < n; ++k) {
        const Vec3 raw = game::cross(points[j] - points[i], points[k] - points[i]);
        const float len = game::length(raw);
        if (len <= k_epsilon) {
          continue;  // collinear triple
        }
        Vec3 normal = raw * (1.0F / len);
        float dist = game::dot(normal, points[i]);
        // A triple is an extreme face when every other point is on one side of its
        // plane and at least one is strictly off it. A plane through interior points
        // (points on both sides) or through a coplanar set (all points on the plane)
        // is not a face. The winding of the triple is arbitrary, so flip to the
        // inward half-space `dot(n, p) <= dist`.
        bool positive = false;
        bool negative = false;
        for (std::size_t m = 0; m < n && !(positive && negative); ++m) {
          const float side = game::dot(normal, points[m]) - dist;
          if (side > k_epsilon) {
            positive = true;
          } else if (side < -k_epsilon) {
            negative = true;
          }
        }
        if ((positive && negative) || (!positive && !negative)) {
          continue;  // an interior plane, or a degenerate/flat set
        }
        if (positive) {
          normal = normal * -1.0F;
          dist = -dist;
        }
        bool duplicate = false;
        for (const OccluderPlane& plane : planes) {
          if (game::dot(plane.normal, normal) > 0.9999F && std::fabs(plane.dist - dist) < 1e-3F) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          planes.push_back(OccluderPlane{normal, dist});
        }
      }
    }
  }
  return planes;
}

auto clip_convex(const Polygon& a, const Polygon& b) -> Polygon {
  Polygon result = a;
  for (const HalfPlane& edge : inward_edges(b)) {
    result = clip_half_plane(result, edge.normal, edge.offset);
    if (result.empty()) {
      break;
    }
  }
  return result;
}

void subtract_convex(const Polygon& a, const Polygon& b, Polygon& inside,
                     std::vector<Polygon>& outside) {
  inside = a;
  outside.clear();
  if (a.empty()) {
    return;
  }
  for (const HalfPlane& edge : inward_edges(b)) {
    // The part on the far side of this edge is outside `b` and is kept as a
    // (still convex) piece; the remainder keeps splitting against later edges.
    Polygon out_part = clip_half_plane(inside, edge.normal * -1.0F, -edge.offset);
    if (!out_part.empty()) {
      outside.push_back(std::move(out_part));
    }
    inside = clip_half_plane(inside, edge.normal, edge.offset);
    if (inside.empty()) {
      break;
    }
  }
}

}  // namespace cs2::aim
