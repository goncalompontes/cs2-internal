#include "cs2/aim/occlusion.hpp"

#include <algorithm>
#include <cmath>

// The exact ray/solid span arithmetic now lives in the shared header, so the
// Monte-Carlo hit test reuses it instead of duplicating it
// (`docs/orchestration/spec-t34-montecarlo.md` §2.3). The arithmetic is unchanged.
#include "cs2/aim/ray_span.hpp"

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

using HalfPlane = ConvexClipEdge;

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
    const Vec2 normal{.x = e.y / len, .y = -e.x / len};
    edges.push_back(HalfPlane{.normal = normal, .offset = game::dot(normal, a)});
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
// --- the span arithmetic lives in `cs2/aim/ray_span.hpp` (shared with the
// Monte-Carlo hit test); it is used unchanged by the occluders below.

// Möller–Trumbore ray/triangle intersection; the ray distance when it hits the
// triangle at `t > 0`, else nullopt. Exact for a collision-mesh triangle.
// `entering` (optional) reports the face the ray met: the triangle's outward normal
// opposes the ray (front face, the ray enters the shell) or follows it (back face).
[[nodiscard]] auto ray_triangle(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                const Vec3& b, const Vec3& c, bool* entering = nullptr) noexcept
    -> std::optional<float> {
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
  if (entering != nullptr) {
    // `det = dot(e1, cross(direction, e2)) = -dot(direction, cross(e1, e2))`, so the
    // sign is the facing without building the normal.
    *entering = det > 0.0F;
  }
  return t;
}

// The ray's crossing of a collision **mesh** (a triangle soup): the **nearest**
// triangle crossing, as a zero-thickness span — the surface the bullet hits.
//
// A collision mesh is not a convex solid and, in CS2, it is not a closed shell
// either: the world body's meshes are the map's merged wall/terrain **surfaces**
// (`docs/re/bullet-trace.md`), and live probing shows the engine reports them as a
// single record at exactly the crossing distance. Pairing the two nearest
// crossings (the old rule) was wrong twice over on such a soup: an isolated wall
// gave entry == exit (and was then dropped as zero thickness), while two unrelated
// surfaces in the same cluster — a wall and the floor behind it — were paired into
// one **fake** solid hundreds of units thick, which zeroed the damage of every shot
// through that cluster. One crossing is one surface; the engine charges the surface's
// entry cost (`penetration_layer_damage`), which is thickness-independent.
//
// A closed collision mesh would therefore be counted surface by surface rather than
// as one solid. That is the conservative direction (each surface costs its entry
// fee) and it is the case CS2 does not produce: gameplay props are multi-hull
// bodies, not meshes (`docs/re/occlusion.md`, "Still to pin" 3).
[[nodiscard]] auto ray_mesh_span(const Vec3& eye, const Vec3& direction,
                                 const std::vector<Vec3>& triangles) noexcept
    -> std::optional<RaySpan> {
  float nearest = 1.0e30F;
  for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
    if (const auto t =
            ray_triangle(eye, direction, triangles[i], triangles[i + 1], triangles[i + 2])) {
      nearest = std::min(nearest, *t);
    }
  }
  if (nearest >= 1.0e30F) {
    return std::nullopt;
  }
  return RaySpan{.entry = nearest, .exit = nearest};
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

// --- mesh BVH (`docs/ragebot-perf.md` §3.7) --------------------------------

constexpr std::uint32_t k_mesh_leaf = 8;  // triangles per BVH leaf

// Ray vs AABB (slab test); fills the near/far distances. False when the ray misses.
[[nodiscard]] auto ray_aabb(const Vec3& eye, const Vec3& direction, const Vec3& mn, const Vec3& mx,
                            float& tnear, float& tfar) noexcept -> bool {
  tnear = -1.0e30F;
  tfar = 1.0e30F;
  const float o[3] = {eye.x, eye.y, eye.z};
  const float d[3] = {direction.x, direction.y, direction.z};
  const float lo[3] = {mn.x, mn.y, mn.z};
  const float hi[3] = {mx.x, mx.y, mx.z};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-12F) {
      if (o[i] < lo[i] || o[i] > hi[i]) {
        return false;
      }
      continue;
    }
    const float inv = 1.0F / d[i];
    float t1 = (lo[i] - o[i]) * inv;
    float t2 = (hi[i] - o[i]) * inv;
    if (t1 > t2) {
      std::swap(t1, t2);
    }
    tnear = std::max(tnear, t1);
    tfar = std::min(tfar, t2);
    if (tnear > tfar) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto mesh_centroid(const Vec3& mn, const Vec3& mx) noexcept -> Vec3 {
  return Vec3{.x = (mn.x + mx.x) * 0.5F, .y = (mn.y + mx.y) * 0.5F, .z = (mn.z + mx.z) * 0.5F};
}

// Recursive median-split build. Returns the node's index; its left child is `index + 1`.
[[nodiscard]] auto build_mesh_node(MeshBvh& bvh, const std::vector<Vec3>& tmin,
                                   const std::vector<Vec3>& tmax, std::uint32_t lo,
                                   std::uint32_t hi) -> std::uint32_t {
  const auto index = static_cast<std::uint32_t>(bvh.nodes.size());
  bvh.nodes.emplace_back();
  Vec3 mn = tmin[bvh.order[lo]];
  Vec3 mx = tmax[bvh.order[lo]];
  for (std::uint32_t i = lo + 1; i < hi; ++i) {
    const Vec3& a = tmin[bvh.order[i]];
    const Vec3& b = tmax[bvh.order[i]];
    mn.x = std::min(mn.x, a.x);
    mn.y = std::min(mn.y, a.y);
    mn.z = std::min(mn.z, a.z);
    mx.x = std::max(mx.x, b.x);
    mx.y = std::max(mx.y, b.y);
    mx.z = std::max(mx.z, b.z);
  }
  bvh.nodes[index].min = mn;
  bvh.nodes[index].max = mx;
  if (hi - lo <= k_mesh_leaf) {
    bvh.nodes[index].start = lo;
    bvh.nodes[index].count = hi - lo;
    return index;
  }
  // Split on the widest axis of the centroids.
  Vec3 cmin = mesh_centroid(tmin[bvh.order[lo]], tmax[bvh.order[lo]]);
  Vec3 cmax = cmin;
  for (std::uint32_t i = lo + 1; i < hi; ++i) {
    const Vec3 c = mesh_centroid(tmin[bvh.order[i]], tmax[bvh.order[i]]);
    cmin.x = std::min(cmin.x, c.x);
    cmin.y = std::min(cmin.y, c.y);
    cmin.z = std::min(cmin.z, c.z);
    cmax.x = std::max(cmax.x, c.x);
    cmax.y = std::max(cmax.y, c.y);
    cmax.z = std::max(cmax.z, c.z);
  }
  const float extent[3] = {cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z};
  const int axis =
      extent[0] >= extent[1] && extent[0] >= extent[2] ? 0 : (extent[1] >= extent[2] ? 1 : 2);
  const std::uint32_t mid = lo + (hi - lo) / 2;
  const auto key = [&](std::uint32_t tri) -> float {
    const Vec3 c = mesh_centroid(tmin[tri], tmax[tri]);
    return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
  };
  std::nth_element(bvh.order.begin() + lo, bvh.order.begin() + mid, bvh.order.begin() + hi,
                   [&](std::uint32_t a, std::uint32_t b) -> bool { return key(a) < key(b); });
  build_mesh_node(bvh, tmin, tmax, lo, mid);  // the left child is `index + 1`
  const std::uint32_t right = build_mesh_node(bvh, tmin, tmax, mid, hi);
  bvh.nodes[index].count = 0;
  bvh.nodes[index].right = right;
  return index;
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
    if (mesh_bvh != nullptr && !mesh_bvh->empty()) {
      return ray_mesh_bvh(*mesh_bvh, eye, direction);
    }
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
  return RaySpan{.entry = std::max(t_enter, 0.0F), .exit = t_exit};
}

// Defined below, next to the BVH span it shares its traversal with.
namespace {
void ray_mesh_crossings(const MeshBvh& bvh, const Vec3& eye, const Vec3& direction,
                        std::vector<MeshCrossing>& out, int max_surfaces);
}  // namespace

void OccluderCell::ray_crossings(const Vec3& eye, const Vec3& direction,
                                 std::vector<MeshCrossing>& out, int max_surfaces) const {
  if (max_surfaces <= 0) {
    return;
  }
  if (kind != SolidKind::mesh) {
    if (const std::optional<RaySpan> span = ray_span(eye, direction); span.has_value()) {
      out.push_back(MeshCrossing{.distance = span->entry, .entering = true});
      if (span->exit > span->entry) {
        out.push_back(MeshCrossing{.distance = span->exit, .entering = false});
      }
    }
    return;
  }
  const std::size_t before = out.size();
  if (mesh_bvh != nullptr && !mesh_bvh->empty()) {
    ray_mesh_crossings(*mesh_bvh, eye, direction, out, max_surfaces);
  } else {
    for (std::size_t i = 0; i + 2 < triangles.size() && static_cast<int>(out.size()) < max_surfaces;
         i += 3) {
      bool entering = false;
      if (const auto t = ray_triangle(eye, direction, triangles[i], triangles[i + 1],
                                      triangles[i + 2], &entering)) {
        out.push_back(MeshCrossing{.distance = *t, .entering = entering});
      }
    }
  }
  std::sort(
      out.begin() + static_cast<std::ptrdiff_t>(before), out.end(),
      [](const MeshCrossing& a, const MeshCrossing& b) -> bool { return a.distance < b.distance; });
}

auto mesh_spans(const std::vector<MeshCrossing>& crossings) -> std::vector<RaySpan> {
  std::vector<RaySpan> spans;
  spans.reserve(crossings.size());
  int open = -1;
  for (int i = 0; i < static_cast<int>(crossings.size()); ++i) {
    const MeshCrossing& crossing = crossings[static_cast<std::size_t>(i)];
    if (open < 0) {
      open = i;
      continue;
    }
    const bool opposite = crossing.entering != crossings[static_cast<std::size_t>(open)].entering;
    if (opposite) {
      // The two faces face opposite ways: the ray entered through one and left
      // through the other. That is a solid, and its thickness is the distance
      // between them. The absolute polarity is irrelevant — a shell can be wound
      // either way round, and the engine's own enter/leave flag follows the shell.
      spans.push_back(RaySpan{.entry = crossings[static_cast<std::size_t>(open)].distance,
                              .exit = crossing.distance});
      open = -1;
      continue;
    }
    // The same facing twice with no opposite face between them: the first crossing
    // was an **open sheet** (the map's walls), not the near face of a solid. It stays
    // a surface; the new crossing opens the next span.
    const float at = crossings[static_cast<std::size_t>(open)].distance;
    spans.push_back(RaySpan{.entry = at, .exit = at});
    open = i;
  }
  if (open >= 0) {
    const float at = crossings[static_cast<std::size_t>(open)].distance;
    spans.push_back(RaySpan{.entry = at, .exit = at});
  }
  return spans;
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

auto build_mesh_bvh(std::vector<Vec3> triangles) -> MeshBvh {
  MeshBvh bvh;
  bvh.triangles = std::move(triangles);
  const std::size_t triangle_count = bvh.triangles.size() / 3;
  if (triangle_count == 0) {
    return bvh;
  }
  bvh.order.resize(triangle_count);
  std::vector<Vec3> tmin(triangle_count);
  std::vector<Vec3> tmax(triangle_count);
  for (std::size_t i = 0; i < triangle_count; ++i) {
    bvh.order[i] = static_cast<std::uint32_t>(i);
    const Vec3* t = &bvh.triangles[i * 3];
    Vec3 mn = t[0];
    Vec3 mx = t[0];
    for (int k = 1; k < 3; ++k) {
      mn.x = std::min(mn.x, t[k].x);
      mn.y = std::min(mn.y, t[k].y);
      mn.z = std::min(mn.z, t[k].z);
      mx.x = std::max(mx.x, t[k].x);
      mx.y = std::max(mx.y, t[k].y);
      mx.z = std::max(mx.z, t[k].z);
    }
    tmin[i] = mn;
    tmax[i] = mx;
  }
  bvh.nodes.reserve(triangle_count * 2);
  build_mesh_node(bvh, tmin, tmax, 0, static_cast<std::uint32_t>(triangle_count));
  return bvh;
}

namespace {

// Every triangle the ray hits, appended unsorted. The BVH is the acceleration only:
// the answer is the same as scanning the soup (the host test pins that).
void ray_mesh_crossings(const MeshBvh& bvh, const Vec3& eye, const Vec3& direction,
                        std::vector<MeshCrossing>& out, int max_surfaces) {
  if (bvh.nodes.empty()) {
    return;
  }
  struct Entry {
    std::uint32_t node = 0;
  };
  Entry stack[128];
  int depth = 0;
  float tnear = 0.0F;
  float tfar = 0.0F;
  if (!ray_aabb(eye, direction, bvh.nodes[0].min, bvh.nodes[0].max, tnear, tfar)) {
    return;
  }
  stack[depth++] = Entry{.node = 0};
  while (depth > 0) {
    const Entry current = stack[--depth];
    const MeshBvh::Node& node = bvh.nodes[current.node];
    if (node.count > 0) {
      for (std::uint32_t i = 0; i < node.count; ++i) {
        if (static_cast<int>(out.size()) >= max_surfaces) {
          return;
        }
        const std::uint32_t tri = bvh.order[node.start + i] * 3;
        bool entering = false;
        if (const auto t = ray_triangle(eye, direction, bvh.triangles[tri], bvh.triangles[tri + 1],
                                        bvh.triangles[tri + 2], &entering)) {
          out.push_back(MeshCrossing{.distance = *t, .entering = entering});
        }
      }
      continue;
    }
    const std::uint32_t left = current.node + 1;
    const std::uint32_t right = node.right;
    float lt = 0.0F;
    float lf = 0.0F;
    float rt = 0.0F;
    float rf = 0.0F;
    if (ray_aabb(eye, direction, bvh.nodes[left].min, bvh.nodes[left].max, lt, lf) &&
        depth + 2 <= 128) {
      stack[depth++] = Entry{.node = left};
    }
    if (ray_aabb(eye, direction, bvh.nodes[right].min, bvh.nodes[right].max, rt, rf) &&
        depth + 2 <= 128) {
      stack[depth++] = Entry{.node = right};
    }
  }
}

}  // namespace

auto ray_mesh_bvh(const MeshBvh& bvh, const Vec3& eye, const Vec3& direction)
    -> std::optional<RaySpan> {
  if (bvh.nodes.empty()) {
    return std::nullopt;
  }
  float t0 = 1.0e30F;  // the nearest crossing: the surface the ray hits
  struct Entry {
    std::uint32_t node = 0;
    float tnear = 0.0F;
  };
  Entry stack[128];
  int depth = 0;
  float tnear = 0.0F;
  float tfar = 0.0F;
  if (!ray_aabb(eye, direction, bvh.nodes[0].min, bvh.nodes[0].max, tnear, tfar)) {
    return std::nullopt;
  }
  stack[depth++] = Entry{.node = 0, .tnear = tnear};
  while (depth > 0) {
    const Entry current = stack[--depth];
    if (current.tnear > t0) {
      continue;  // farther than the nearest crossing already found
    }
    const MeshBvh::Node& node = bvh.nodes[current.node];
    if (node.count > 0) {
      for (std::uint32_t i = 0; i < node.count; ++i) {
        const std::uint32_t tri = bvh.order[node.start + i] * 3;
        if (const auto t = ray_triangle(eye, direction, bvh.triangles[tri], bvh.triangles[tri + 1],
                                        bvh.triangles[tri + 2])) {
          t0 = std::min(t0, *t);
        }
      }
      continue;
    }
    const std::uint32_t left = current.node + 1;
    const std::uint32_t right = node.right;
    float lt = 0.0F;
    float lf = 0.0F;
    float rt = 0.0F;
    float rf = 0.0F;
    const bool lhit = ray_aabb(eye, direction, bvh.nodes[left].min, bvh.nodes[left].max, lt, lf);
    const bool rhit = ray_aabb(eye, direction, bvh.nodes[right].min, bvh.nodes[right].max, rt, rf);
    if (lhit && rhit) {
      if (lt <= rt) {
        stack[depth++] = Entry{.node = right, .tnear = rt};
        stack[depth++] = Entry{.node = left, .tnear = lt};
      } else {
        stack[depth++] = Entry{.node = left, .tnear = lt};
        stack[depth++] = Entry{.node = right, .tnear = rt};
      }
    } else if (lhit) {
      stack[depth++] = Entry{.node = left, .tnear = lt};
    } else if (rhit) {
      stack[depth++] = Entry{.node = right, .tnear = rt};
    }
  }
  if (t0 >= 1.0e30F) {
    return std::nullopt;
  }
  // One crossing is one surface: see `ray_mesh_span` for why the two nearest
  // crossings must not be paired into a fake solid.
  return RaySpan{.entry = t0, .exit = t0};
}

auto project_occluder(const Vec3& eye, const Basis& basis, const std::vector<Vec3>& world_vertices,
                      const std::vector<OccluderPlane>& planes, float surface_modifier)
    -> std::optional<OccluderCell> {
  if (world_vertices.empty()) {
    return std::nullopt;
  }
  // The reference direction is the solid's own centre; vertices behind it (a solid that
  // straddles the eye, e.g. a large brush the eye stands next to) are dropped so the
  // silhouette cannot wrap around the chart and over-cover every hitbox.
  Vec3 centroid{};
  for (const Vec3& v : world_vertices) {
    centroid = centroid + v;
  }
  centroid = centroid * (1.0F / static_cast<float>(world_vertices.size()));
  const Vec3 reference = centroid - eye;
  const float reference_len = game::length(reference);
  float reference_yaw = 0.0F;
  if (const auto centre = basis.project(reference); centre.has_value()) {
    reference_yaw = centre->x;
  }

  std::vector<Vec2> projected;
  projected.reserve(world_vertices.size());
  for (const Vec3& v : world_vertices) {
    const Vec3 to_vertex = v - eye;
    if (reference_len > k_epsilon && game::dot(to_vertex, reference) < 0.0F) {
      continue;  // behind the solid's own facing
    }
    if (const auto p = basis.project(to_vertex)) {
      projected.push_back(*p);
    }
  }
  Polygon polygon = convex_hull_unwrapped(projected, reference_yaw);
  if (polygon.empty()) {
    return std::nullopt;
  }
  // Orient every face plane inward (`dot(n, x) <= d`), which is what `ray_span`
  // assumes. The engine's `RnHull_t` planes are `dot(n, x) = d` but may be stored
  // with the normal pointing either way, and the vertex centroid is inside the hull
  // by construction, so the centroid decides the sign — robust across builds.
  bool flip_planes = false;
  if (!planes.empty()) {
    int violations = 0;
    for (const OccluderPlane& plane : planes) {
      if (game::dot(plane.normal, centroid) > plane.dist) {
        ++violations;
      }
    }
    flip_planes = violations * 2 > static_cast<int>(planes.size());
  }
  OccluderCell cell;
  cell.polygon = std::move(polygon);
  cell.kind = SolidKind::polytope;
  // The cell owns its geometry (the exact ray test runs later, after the projection lock
  // is released), so copy it in once here rather than taking the caller's vectors by value
  // on every call.
  cell.vertices = world_vertices;
  cell.planes.reserve(planes.size());
  for (const OccluderPlane& plane : planes) {
    cell.planes.push_back(
        flip_planes ? OccluderPlane{.normal = plane.normal * -1.0F, .dist = -plane.dist} : plane);
  }
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

auto project_mesh_occluder(const Vec3& eye, const Basis& basis, const std::vector<Vec3>& vertices,
                           const std::vector<Vec3>& triangles, float surface_modifier,
                           std::shared_ptr<const MeshBvh> bvh) -> std::optional<OccluderCell> {
  if (vertices.empty() || triangles.size() < 3) {
    return std::nullopt;
  }
  // The reference direction is the mesh cluster's centre; vertices behind it are dropped
  // from the broad-phase silhouette so a cluster the eye stands beside cannot wrap
  // around the chart. (The exact depth/thickness still comes from `ray_span` over the
  // full triangle list.)
  Vec3 centre{};
  for (const Vec3& v : vertices) {
    centre = centre + v;
  }
  centre = centre * (1.0F / static_cast<float>(vertices.size()));
  const Vec3 reference = centre - eye;
  const float reference_len = game::length(reference);
  float reference_yaw = 0.0F;
  if (const auto c = basis.project(reference); c.has_value()) {
    reference_yaw = c->x;
  }
  std::vector<Vec2> projected;
  projected.reserve(vertices.size());
  for (const Vec3& v : vertices) {
    const Vec3 to_vertex = v - eye;
    if (reference_len > k_epsilon && game::dot(to_vertex, reference) < 0.0F) {
      continue;
    }
    if (const auto p = basis.project(to_vertex)) {
      projected.push_back(*p);
    }
  }
  Polygon polygon = convex_hull_unwrapped(projected, reference_yaw);
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
  // Only copy the soup when there is no BVH: with the BVH, `ray_span` uses it and the
  // cell does not need to own the triangles. This avoids copying an entire mesh cluster
  // (up to thousands of points) for every projected cluster, every frame.
  if (bvh == nullptr) {
    cell.triangles = triangles;
  }
  cell.mesh_bvh = std::move(bvh);
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
          planes.push_back(OccluderPlane{.normal = normal, .dist = dist});
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

auto prepare_convex_clipper(const Polygon& polygon) -> ConvexClipper {
  return ConvexClipper{inward_edges(polygon)};
}

void subtract_convex(const Polygon& a, const Polygon& b, Polygon& inside,
                     std::vector<Polygon>& outside) {
  subtract_convex(a, prepare_convex_clipper(b), inside, outside);
}

void subtract_convex(const Polygon& a, const ConvexClipper& clipper, Polygon& inside,
                     std::vector<Polygon>& outside) {
  inside = a;
  outside.clear();
  if (a.empty()) {
    return;
  }
  for (const ConvexClipEdge& edge : clipper.edges) {
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
