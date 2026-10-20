#pragma once

// Occlusion / penetration for the aim model (docs/ragebot.md, ADR-0022 — Tier 2).
//
// The settled design does **not** trace rays. A world solid (a collision hull /
// brush) is projected into the same angular chart as the hitboxes; its silhouette
// becomes an occluder region and its along-view extent gives the wall thickness
// for any direction inside it. Projecting every solid between the eye and the
// target yields the *entire* occlusion/penetration field in one pass, and the
// per-cell damage becomes
//   `damage · hitgroup · range_falloff · Π_occluders (1 − t(u)·mod/m_flPenetration)`.
//
// This file is the platform-independent geometry: the payload supplies the world
// solids (world-space convex vertices + a surface modifier) from the physics query.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

// A convex solid's inward half-space: its interior is `dot(normal, p) <= dist`.
struct OccluderPlane {
  Vec3 normal{};
  float dist = 0.0F;
};

// Where a ray lies inside a solid, measured as distances from the eye along a unit
// direction (`exit >= entry`).
struct RaySpan {
  float entry = 0.0F;
  float exit = 0.0F;
};

// A median-split bounding-volume hierarchy over a triangle soup (flat triples, 3
// world-space points per triangle). `OccluderCell::ray_span` uses it for collision
// **meshes** so a ray tests O(log n) triangles instead of the whole soup — the
// `docs/ragebot-review.md` §5.4 "mesh BVH". Built once per mesh, at parse time, and
// immutable afterwards. An empty tree is valid (the caller then scans the flat soup).
struct MeshBvh {
  struct Node {
    Vec3 min{};
    Vec3 max{};               // node AABB
    std::uint32_t start = 0;   // leaf: first index into `order`
    std::uint32_t count = 0;   // 0 = internal node, else the leaf's triangle count
    std::uint32_t right = 0;   // internal: right child index (left = own index + 1)
  };
  std::vector<Node> nodes;
  std::vector<std::uint32_t> order;  // triangle indices, leaves contiguous
  std::vector<Vec3> triangles;       // the soup (3 points per triangle)
  [[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
};

// Build the BVH over `triangles` (flat triples). Returns an empty tree for no triangles.
[[nodiscard]] auto build_mesh_bvh(std::vector<Vec3> triangles) -> MeshBvh;

// The ray/mesh interval (the two nearest triangle crossings) using the BVH — identical
// result to scanning every triangle. nullopt when the ray misses.
[[nodiscard]] auto ray_mesh_bvh(const MeshBvh& bvh, const Vec3& eye, const Vec3& direction)
    -> std::optional<RaySpan>;

// The kind of solid an occluder is. A brush/hull is a general convex polytope; a
// sphere or capsule (the shape of many props) is carried analytically, so its
// silhouette and its ray interval are exact with no vertex approximation — the
// design's "sphere/capsule (+ as hulls of their extremes)" (`docs/ragebot.md` §10).
enum class SolidKind : std::uint8_t { polytope, sphere, capsule, mesh };

// A world solid projected into the chart: its silhouette polygon, its geometry for
// the exact ray test, and the surface's penetration modifier.
struct OccluderCell {
  Polygon polygon;  // chart silhouette, convex, CCW
  SolidKind kind = SolidKind::polytope;
  std::vector<Vec3> vertices;         // polytope: world-space convex hull vertices
  std::vector<OccluderPlane> planes;  // polytope: inward half-spaces (the hull's faces)
  // mesh: the triangles as flat triples (3 world-space points per triangle). A
  // collision **mesh** is a triangle soup and is *not* convex, so it cannot be
  // described by half-spaces; the exact ray test intersects its triangles and the
  // first entry/exit pair is the wall thickness (docs/re/vphysics2.md).
  std::vector<Vec3> triangles;
  // mesh: the BVH over `triangles` (built once at parse time). When set, `ray_span`
  // uses it; otherwise it scans the flat soup. Shared, so projecting the same mesh for
  // many hitboxes does not rebuild it (`docs/ragebot-perf.md` §3.7).
  std::shared_ptr<const MeshBvh> mesh_bvh;
  Vec3 a{};                           // sphere: centre; capsule: endpoint a; mesh: AABB min
  Vec3 b{};                           // capsule: endpoint b; mesh: AABB max
  float radius = 0.0F;                // sphere / capsule radius
  // The target (enemy) whose eye→enemy pyramid enumerated this solid. A solid only
  // attenuates that enemy's hitboxes, so `aim::solve` feeds each hitbox just its own
  // target's occluders (`-1` = shared/unknown, applies to every hitbox). Passing every
  // enemy's solids to every hitbox was what exploded `occlude_polygon`.
  int target = -1;
  // The surface's penetration properties (docs/re/penetration.md). Until the
  // payload resolves the per-solid material these default to a neutral surface.
  float surface_modifier = 1.0F;      // surfacedata_t.game.penetrationmodifier
  float damage_modifier = 1.0F;       // surfacedata_t.game.damagemodifier
  int material = -1;                  // surfacedata_t.game.material (CHAR_TEX_*)
  bool grate = false;                 // CONTENTS_GRATE / glass

  // The wall thickness along `direction` (unit): the distance between the hull's
  // entry and exit support planes.
  [[nodiscard]] auto thickness_along(const Vec3& direction) const noexcept -> float;

  // Where the ray `eye + t·direction` (unit) is inside this solid — the exact
  // "is this solid between the eye and that cell?" test (`docs/ragebot.md` §3:
  // occluders cut the hitbox projection by *depth*, not just by silhouette).
  // nullopt when the ray misses the solid or the planes are unknown.
  [[nodiscard]] auto ray_span(const Vec3& eye, const Vec3& direction) const noexcept
      -> std::optional<RaySpan>;

  // A **lower bound** on the distance from `eye` to this solid (0 when the eye is
  // inside it). Cheap broad-phase for "can this solid be nearer than the hitbox?":
  // because it never over-estimates, pruning when it is `>= hitbox_distance` cannot
  // drop a solid that is genuinely in front. It uses the hull's plane distances
  // (each individually a lower bound on the distance to the bounded hull), so it
  // stays correct even for a solid whose nearest point is a face interior.
  [[nodiscard]] auto lower_bound_distance(const Vec3& eye) const noexcept -> float;
};

// Project a world-space convex solid (its world vertices + inward planes) into the
// chart. Returns nullopt when it does not project meaningfully (behind the eye /
// degenerate).
[[nodiscard]] auto project_occluder(const Vec3& eye, const Basis& basis,
                                    const std::vector<Vec3>& world_vertices,
                                    const std::vector<OccluderPlane>& planes,
                                    float surface_modifier) -> std::optional<OccluderCell>;

// Project an analytic sphere / capsule into the chart (exact silhouette + ray test).
[[nodiscard]] auto project_sphere_occluder(const Vec3& eye, const Basis& basis, const Vec3& center,
                                           float radius, float surface_modifier)
    -> std::optional<OccluderCell>;
[[nodiscard]] auto project_capsule_occluder(const Vec3& eye, const Basis& basis, const Vec3& a,
                                            const Vec3& b, float radius, float surface_modifier)
    -> std::optional<OccluderCell>;

// Project a collision **mesh** (a triangle soup; docs/re/vphysics2.md) into the
// chart. The chart silhouette is the convex hull of the projected vertices (a
// superset of the true, non-convex silhouette — only used for the broad phase), and
// the exact depth/thickness along a direction comes from `ray_span` intersecting the
// triangles (via `bvh` when supplied). `triangles` is a flat list of world-space
// points, 3 per triangle.
[[nodiscard]] auto project_mesh_occluder(const Vec3& eye, const Basis& basis,
                                         const std::vector<Vec3>& vertices,
                                         const std::vector<Vec3>& triangles, float surface_modifier,
                                         std::shared_ptr<const MeshBvh> bvh = nullptr)
    -> std::optional<OccluderCell>;

// The inward face planes of the convex hull of a point set — for a collision **mesh**
// (docs/re/vphysics2.md: "mesh (convex hull)"), whose world vertices the payload
// supplies. Exact brute force (every point triple that is an extreme face), so it is
// O(n^4) and intended for bounded inputs; returns empty for fewer than 4 points or a
// degenerate/coplanar set. The normals are unit and point inward, matching `ray_span`
// (and `project_occluder` re-orients them from the vertex centroid anyway).
[[nodiscard]] auto convex_hull_3d(std::span<const Vec3> points) -> std::vector<OccluderPlane>;

// Intersect two convex polygons (clip `a` by `b`'s half-planes). Empty when disjoint.
[[nodiscard]] auto clip_convex(const Polygon& a, const Polygon& b) -> Polygon;

// The inward half-planes for a convex polygon, prepared for repeated clipping. Preparing
// once per occluder avoids rebuilding edge normals for every fragment in the arrangement.
struct ConvexClipEdge {
  Vec2 normal{};
  float offset = 0.0F;
};

struct ConvexClipper {
  std::vector<ConvexClipEdge> edges;
};

[[nodiscard]] auto prepare_convex_clipper(const Polygon& polygon) -> ConvexClipper;

// Split `a` into the part inside `b` and the (convex) parts outside `b`. The inside
// part is returned; the outside pieces are appended to `outside`. Both inputs must
// be convex and wound the same way.
void subtract_convex(const Polygon& a, const Polygon& b, Polygon& inside,
                     std::vector<Polygon>& outside);
void subtract_convex(const Polygon& a, const ConvexClipper& clipper, Polygon& inside,
                     std::vector<Polygon>& outside);

}  // namespace cs2::aim
