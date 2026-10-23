// Unit tests for the occlusion / penetration model (docs/ragebot.md, ADR-0022).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "cs2/aim/damage.hpp"
#include "cs2/aim/heatmap.hpp"
#include "cs2/aim/occlusion.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-2F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

// A world-space AABB as 8 convex-hull vertices.
auto box_vertices(float cx, float cy, float cz, float hx, float hy, float hz)
    -> std::vector<cs2::game::Vec3> {
  std::vector<cs2::game::Vec3> v;
  for (int i = 0; i < 8; ++i) {
    v.push_back(cs2::game::Vec3{.x = cx + ((i & 1) != 0 ? hx : -hx),
                                .y = cy + ((i & 2) != 0 ? hy : -hy),
                                .z = cz + ((i & 4) != 0 ? hz : -hz)});
  }
  return v;
}

// The same box's inward half-spaces (`dot(normal, p) <= dist`), so the exact ray
// test (`OccluderCell::ray_span`, the per-cell depth check) can be exercised.
auto box_planes(float cx, float cy, float cz, float hx, float hy, float hz)
    -> std::vector<cs2::aim::OccluderPlane> {
  using cs2::aim::OccluderPlane;
  using cs2::game::Vec3;
  return {
      OccluderPlane{.normal = Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, .dist = cx + hx},
      OccluderPlane{.normal = Vec3{.x = -1.0F, .y = 0.0F, .z = 0.0F}, .dist = -(cx - hx)},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, .dist = cy + hy},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = -1.0F, .z = 0.0F}, .dist = -(cy - hy)},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F}, .dist = cz + hz},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, .dist = -(cz - hz)},
  };
}

auto make_occluder(const cs2::aim::Basis& basis, float cx, float cy, float cz, float hx, float hy,
                   float hz) -> std::optional<cs2::aim::OccluderCell> {
  return cs2::aim::project_occluder(cs2::game::Vec3{}, basis, box_vertices(cx, cy, cz, hx, hy, hz),
                                    box_planes(cx, cy, cz, hx, hy, hz), 1.0F);
}

auto square(float half) -> cs2::aim::Polygon {
  using cs2::aim::Vec2;
  return cs2::aim::Polygon{{Vec2{.x = -half, .y = -half}, Vec2{.x = half, .y = -half},
                            Vec2{.x = half, .y = half}, Vec2{.x = -half, .y = half}}};
}

// A head capsule at `x` along the view axis.
auto head_capsule(float x) -> cs2::aim::Hitbox {
  cs2::aim::Hitbox hitbox;
  hitbox.is_capsule = true;
  hitbox.capsule = cs2::aim::Capsule{.a = cs2::game::Vec3{.x = x, .y = 0.0F, .z = 0.0F},
                                     .b = cs2::game::Vec3{.x = x, .y = 0.0F, .z = 4.0F},
                                     .radius = 4.0F};
  hitbox.group = 1;
  hitbox.multiplier = 4.0F;
  return hitbox;
}

}  // namespace

auto main() -> int {
  using cs2::aim::Basis;
  using cs2::aim::HeatmapOptions;
  using cs2::aim::OccluderCell;
  using cs2::aim::TargetPose;
  using cs2::aim::Vec3;
  using cs2::aim::WeaponProfile;

  const Basis basis{.forward = Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                    .right = Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                    .up = Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F}};

  // --- thickness along a direction -----------------------------------------
  {
    auto cell = make_occluder(basis, 50, 0, 0, 2, 10, 10);
    expect(cell.has_value(), "a box in front projects");
    if (cell.has_value()) {
      expect(near(cell->thickness_along(Vec3{.x = 1, .y = 0, .z = 0}), 4.0F),
             "thickness along the view axis");
      // Along +Y the box is 20 deep.
      expect(near(cell->thickness_along(Vec3{.x = 0, .y = 1, .z = 0}), 20.0F),
             "thickness along another axis");

      // The exact ray test: the eye looks down +X, so the box spans x in [48, 52].
      const auto span = cell->ray_span(Vec3{}, Vec3{.x = 1, .y = 0, .z = 0});
      expect(span.has_value(), "the ray through the box hits it");
      if (span.has_value()) {
        expect(near(span->entry, 48.0F), "the ray enters the front face");
        expect(near(span->exit, 52.0F), "the ray leaves the back face");
      }
      // A ray that misses it (offset in y) reports no span.
      expect(!cell->ray_span(Vec3{}, cs2::aim::Vec3{.x = 1.0F, .y = 1.0F, .z = 0.0F}).has_value(),
             "a ray that misses the box has no span");
      // A box behind the eye is never entered.
      const auto behind_span = cell->ray_span(cs2::game::Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F},
                                              Vec3{.x = 1, .y = 0, .z = 0});
      expect(!behind_span.has_value(), "a box behind the eye has no span");
    }
    // A box behind the eye still projects (the chart is absolute yaw/pitch), but the
    // forward ray never reaches it — `ray_span` is what keeps it from occluding.
    const auto behind = make_occluder(basis, -50, 0, 0, 2, 2, 2);
    expect(behind.has_value(), "a box behind the eye projects (absolute chart)");
    if (behind.has_value()) {
      expect(!behind->ray_span(cs2::game::Vec3{}, Vec3{.x = 1, .y = 0, .z = 0}).has_value(),
             "a box behind the eye has no forward ray span");
    }
  }

  // --- convex subtraction (the cell split) ---------------------------------
  {
    const auto a = square(1.0F);
    const auto b =
        cs2::aim::Polygon{{cs2::aim::Vec2{.x = 0, .y = 0}, cs2::aim::Vec2{.x = 1, .y = 0},
                           cs2::aim::Vec2{.x = 1, .y = 1}, cs2::aim::Vec2{.x = 0, .y = 1}}};
    cs2::aim::Polygon inside;
    std::vector<cs2::aim::Polygon> outside;
    cs2::aim::subtract_convex(a, b, inside, outside);
    expect(near(cs2::aim::polygon_area(inside), 1.0F, 0.05F), "inside = a ∩ b");
    float total = cs2::aim::polygon_area(inside);
    for (const auto& piece : outside) {
      total += cs2::aim::polygon_area(piece);
    }
    expect(near(total, 4.0F, 0.05F), "inside + outside = a");
    expect(!outside.empty(), "the outside is made of convex pieces");
    const auto clipper = cs2::aim::prepare_convex_clipper(b);
    cs2::aim::Polygon prepared_inside;
    std::vector<cs2::aim::Polygon> prepared_outside;
    cs2::aim::subtract_convex(a, clipper, prepared_inside, prepared_outside);
    expect(prepared_inside.points.size() == inside.points.size(),
           "prepared clipper preserves the inside polygon topology");
    expect(prepared_outside.size() == outside.size(),
           "prepared clipper preserves the outside fragment count");
    bool same_inside = prepared_inside.points.size() == inside.points.size();
    for (std::size_t i = 0; i < inside.points.size() && same_inside; ++i) {
      same_inside = near(prepared_inside.points[i].x, inside.points[i].x, 1e-6F) &&
                    near(prepared_inside.points[i].y, inside.points[i].y, 1e-6F);
    }
    expect(same_inside, "prepared clipper preserves inside vertices");
    bool same_outside = prepared_outside.size() == outside.size();
    for (std::size_t i = 0; i < outside.size() && same_outside; ++i) {
      same_outside = prepared_outside[i].points.size() == outside[i].points.size();
      for (std::size_t j = 0; j < outside[i].points.size() && same_outside; ++j) {
        same_outside = near(prepared_outside[i].points[j].x, outside[i].points[j].x, 1e-6F) &&
                       near(prepared_outside[i].points[j].y, outside[i].points[j].y, 1e-6F);
      }
    }
    expect(same_outside, "prepared clipper preserves outside vertices");
    expect(near(cs2::aim::polygon_area(prepared_inside), cs2::aim::polygon_area(inside), 1e-6F),
           "prepared clipper preserves inside area");
    float prepared_total = cs2::aim::polygon_area(prepared_inside);
    for (const auto& piece : prepared_outside) {
      prepared_total += cs2::aim::polygon_area(piece);
    }
    expect(near(prepared_total, total, 1e-6F), "prepared clipper preserves total area");
  }

  // --- the heatmap splits a silhouette by an occluder ----------------------
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .penetration = 1.0F, .headshot_multiplier = 4.0F};
    // A thin box (thickness 0.5) partially covering the silhouette (narrow in y).
    const auto occ = make_occluder(basis, 50, 0, 0, 0.25F, 1.0F, 1.0F);
    expect(occ.has_value(), "the wall projects");
    if (occ.has_value()) {
      options.occlusion.push_back(*occ);
    }
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    expect(map.cells.size() >= 2, "the occluder splits the silhouette");
    float lo = 1e9F;
    float hi = -1e9F;
    for (const auto& cell : map.cells) {
      lo = std::min(lo, cell.value);
      hi = std::max(hi, cell.value);
    }
    expect(near(hi, 400.0F, 1.0F), "the uncovered part keeps full damage");
    expect(near(lo, 200.0F, 2.0F), "the covered part keeps half (1 - 0.5)");

    if (occ.has_value()) {
      HeatmapOptions capped = options;
      capped.penetration_params.max_occluders = 1;
      capped.occlusion.push_back(*occ);
      const auto incomplete = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, capped);
      expect(!incomplete.occlusion_candidates_complete,
             "the heatmap reports when an occluder cap truncated its geometry");
    }

    // A blocking wall (thickness >= penetration) drops the hitbox entirely.
    HeatmapOptions blocking = options;
    blocking.occlusion.clear();
    if (const auto thick = make_occluder(basis, 50, 0, 0, 0.5F, 10.0F, 10.0F); thick.has_value()) {
      blocking.occlusion.push_back(*thick);
    }
    const auto blocked = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, blocking);
    expect(blocked.cells.empty(), "a wall the weapon cannot penetrate drops the hitbox");

    // DEPTH (the whole point): a solid *behind* the hitbox must not attenuate it,
    // even though its silhouette covers the same chart area. This is the floor /
    // wall-behind-the-target case that a silhouette-only test gets wrong.
    HeatmapOptions behind_target = options;
    behind_target.occlusion.clear();
    if (const auto far_wall = make_occluder(basis, 200, 0, 0, 0.5F, 10.0F, 10.0F);
        far_wall.has_value()) {
      behind_target.occlusion.push_back(*far_wall);
    }
    const auto untouched = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, behind_target);
    float best = 0.0F;
    for (const auto& cell : untouched.cells) {
      best = std::max(best, cell.value);
    }
    expect(near(best, 400.0F, 1.0F), "a solid beyond the hitbox does not attenuate it");
  }

  // --- a solid that covers only PART of the silhouette still attenuates -------
  // Regression (2026-10-17): the broad phase used to be "the ray to the hitbox
  // CENTRE enters the solid", so a pillar beside the centre ray that covers half the
  // silhouette was dropped, and the covered half kept full damage.
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .penetration = 1.0F, .headshot_multiplier = 4.0F};
    // Offset in y so the eye→centre ray (y = 0) MISSES it, yet it still overlaps the
    // left part of the head silhouette (the box spans y in [-5, -1] at x = 50).
    const auto occ = make_occluder(basis, 50, -3, 0, 0.25F, 2.0F, 4.0F);
    expect(occ.has_value(), "the offset wall projects");
    if (occ.has_value()) {
      options.occlusion.push_back(*occ);
    }
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    float lo = 1e9F;
    float hi = -1e9F;
    for (const auto& cell : map.cells) {
      lo = std::min(lo, cell.value);
      hi = std::max(hi, cell.value);
    }
    expect(near(hi, 400.0F, 1.0F), "a partial cover leaves the rest at full damage");
    expect(near(lo, 200.0F, 2.0F), "a solid covering only part of the silhouette attenuates it");
  }

  // --- the conservative depth lower bound -----------------------------------
  {
    auto front = make_occluder(basis, 50, 0, 0, 2, 2, 2);
    expect(front.has_value(), "the front box projects");
    if (front.has_value()) {
      expect(near(front->lower_bound_distance(Vec3{}), 48.0F, 0.05F),
             "the lower bound is the nearest face distance");
      expect(near(front->lower_bound_distance(Vec3{.x = 50, .y = 0, .z = 0}), 0.0F),
             "an eye inside the solid bounds at 0");
    }
    // A solid the eye is beyond (behind) has a bound larger than any in-front target.
    auto far_away = make_occluder(basis, 200, 0, 0, 2, 2, 2);
    expect(far_away.has_value(), "the far box projects");
    if (far_away.has_value()) {
      expect(far_away->lower_bound_distance(Vec3{}) > 100.0F,
             "a solid beyond the hitbox bounds below the eye-to-hitbox distance");
    }
  }

  // --- analytic sphere / capsule occluders ----------------------------------
  {
    auto sphere =
        cs2::aim::project_sphere_occluder(Vec3{}, basis, Vec3{.x = 50, .y = 0, .z = 0}, 2.0F, 1.0F);
    expect(sphere.has_value(), "the sphere projects");
    if (sphere.has_value()) {
      expect(sphere->kind == cs2::aim::SolidKind::sphere, "the cell carries the sphere kind");
      const auto span = sphere->ray_span(Vec3{}, Vec3{.x = 1, .y = 0, .z = 0});
      expect(span.has_value() && near(span->entry, 48.0F) && near(span->exit, 52.0F),
             "the sphere ray span is exact");
      expect(near(sphere->lower_bound_distance(Vec3{}), 48.0F), "the sphere lower bound");
      expect(!sphere->ray_span(Vec3{}, Vec3{.x = 0, .y = 1, .z = 0}).has_value(),
             "a ray that misses the sphere has no span");
    }

    auto capsule = cs2::aim::project_capsule_occluder(Vec3{}, basis, Vec3{.x = 48, .y = 0, .z = 0},
                                                      Vec3{.x = 52, .y = 0, .z = 0}, 1.0F, 1.0F);
    expect(capsule.has_value(), "the capsule projects");
    if (capsule.has_value()) {
      expect(capsule->kind == cs2::aim::SolidKind::capsule, "the cell carries the capsule kind");
      const auto span = capsule->ray_span(Vec3{}, Vec3{.x = 1, .y = 0, .z = 0});
      expect(span.has_value() && near(span->entry, 47.0F) && near(span->exit, 53.0F),
             "the axial span includes the rounded caps");
      const auto side =
          capsule->ray_span(Vec3{.x = 50, .y = 0, .z = 0}, Vec3{.x = 0, .y = 1, .z = 0});
      expect(side.has_value() && near(side->entry, 0.0F) && near(side->exit, 1.0F),
             "a side ray from the axis spans the radius");
      expect(near(capsule->lower_bound_distance(Vec3{}), 47.0F), "the capsule lower bound");
    }
  }

  // --- a sphere occluder attenuates the silhouette --------------------------
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = WeaponProfile{.damage = 100.0F,
                                   .range_modifier = 1.0F,
                                   .penetration = 20.0F,
                                   .headshot_multiplier = 4.0F};
    if (auto sphere = cs2::aim::project_sphere_occluder(Vec3{}, basis,
                                                        Vec3{.x = 50, .y = 0, .z = 0}, 5.0F, 1.0F);
        sphere.has_value()) {
      options.occlusion.push_back(std::move(*sphere));
    }
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    expect(!map.cells.empty(), "the head still yields a cell");
    for (const auto& cell : map.cells) {
      expect(cell.value < 400.0F, "the sphere occluder attenuates the silhouette");
    }
  }

  // --- the 3-D convex hull (mesh -> polytope occluder) ----------------------
  {
    // A unit cube (offset to x in [5,6]) -> 6 face planes.
    const std::vector<Vec3> cube = {Vec3{.x = 5, .y = 0, .z = 0}, Vec3{.x = 6, .y = 0, .z = 0},
                                    Vec3{.x = 6, .y = 1, .z = 0}, Vec3{.x = 5, .y = 1, .z = 0},
                                    Vec3{.x = 5, .y = 0, .z = 1}, Vec3{.x = 6, .y = 0, .z = 1},
                                    Vec3{.x = 6, .y = 1, .z = 1}, Vec3{.x = 5, .y = 1, .z = 1}};
    const auto hull = cs2::aim::convex_hull_3d(cube);
    expect(hull.size() == 6, "a cube's hull has 6 face planes");
    bool contains_all = true;
    for (const cs2::aim::OccluderPlane& plane : hull) {
      for (const Vec3& v : cube) {
        if (cs2::game::dot(plane.normal, v) > plane.dist + 1e-3F) {
          contains_all = false;
        }
      }
    }
    expect(contains_all, "every cube corner is inside every hull plane");

    // The polytope projects and ray-tests like a solid (the mesh path).
    if (const auto cell = cs2::aim::project_occluder(Vec3{}, basis, cube, hull, 1.0F);
        cell.has_value()) {
      const auto span = cell->ray_span(Vec3{}, Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
      expect(span.has_value() && near(span->entry, 5.0F) && near(span->exit, 6.0F),
             "the mesh-derived solid's ray span matches the cube");
    }

    // A coplanar set has no 3-D faces.
    const std::vector<Vec3> flat = {Vec3{.x = 0, .y = 0, .z = 0}, Vec3{.x = 1, .y = 0, .z = 0},
                                    Vec3{.x = 1, .y = 1, .z = 0}, Vec3{.x = 0, .y = 1, .z = 0}};
    expect(cs2::aim::convex_hull_3d(flat).empty(), "a coplanar set has no 3-D faces");
  }

  // --- projection degenerate cases + bounds (plan P4.23) --------------------
  {
    // One point inside the cube's hull does not add faces.
    const std::vector<Vec3> cube = {Vec3{.x = 5, .y = 0, .z = 0}, Vec3{.x = 6, .y = 0, .z = 0},
                                    Vec3{.x = 6, .y = 1, .z = 0}, Vec3{.x = 5, .y = 1, .z = 0},
                                    Vec3{.x = 5, .y = 0, .z = 1}, Vec3{.x = 6, .y = 0, .z = 1},
                                    Vec3{.x = 6, .y = 1, .z = 1}, Vec3{.x = 5, .y = 1, .z = 1}};
    std::vector<Vec3> with_center = cube;
    with_center.push_back(Vec3{.x = 5.5F, .y = 0.5F, .z = 0.5F});
    expect(cs2::aim::convex_hull_3d(with_center).size() == 6,
           "an interior point does not add hull faces");
    // A tetrahedron has exactly 4 faces.
    const std::vector<Vec3> tetra = {Vec3{.x = 0, .y = 0, .z = 0}, Vec3{.x = 4, .y = 0, .z = 0},
                                     Vec3{.x = 0, .y = 4, .z = 0}, Vec3{.x = 0, .y = 0, .z = 4}};
    expect(cs2::aim::convex_hull_3d(tetra).size() == 4, "a tetrahedron has 4 hull faces");

    // A solid straddling the eye still yields a finite silhouette (near-plane path).
    if (const auto straddle = make_occluder(basis, 0, 0, 0, 5, 2, 2); straddle.has_value()) {
      expect(!straddle->polygon.empty(), "a straddling solid projects");
      for (const auto& p : straddle->polygon.points) {
        expect(std::isfinite(p.x) && std::isfinite(p.y), "the straddle silhouette is finite");
      }
    }
    // A solid whose front face is a hair from the eye stays finite (no division blow-up).
    if (const auto tiny = make_occluder(basis, 0, 0, 0, 1e-3F, 1.0F, 1.0F); tiny.has_value()) {
      for (const auto& p : tiny->polygon.points) {
        expect(std::isfinite(p.x) && std::isfinite(p.y), "a near-plane solid projects finitely");
      }
    }
  }

  // --- a collision MESH occluder (triangle soup; the map's large walls) ------
  // Regression (2026-10-18): the payload skipped every mesh shape (a vertex cap of
  // 160 + an O(n^4) convex hull), but the map's large/merged wall surfaces ARE meshes
  // (docs/re/bullet-trace.md), so a wall between the eye and a target did not occlude.
  {
    // A closed box (12 triangles) at x in [49, 51], offset in z so the +X ray hits a
    // face interior rather than a triangulation diagonal.
    const float x0 = 49.0F;
    const float x1 = 51.0F;
    const float y0 = -5.0F;
    const float y1 = 5.0F;
    const float z0 = -4.7F;
    const float z1 = 5.3F;
    const std::vector<Vec3> verts = {{.x = x0, .y = y0, .z = z0}, {.x = x1, .y = y0, .z = z0},
                                     {.x = x1, .y = y1, .z = z0}, {.x = x0, .y = y1, .z = z0},
                                     {.x = x0, .y = y0, .z = z1}, {.x = x1, .y = y0, .z = z1},
                                     {.x = x1, .y = y1, .z = z1}, {.x = x0, .y = y1, .z = z1}};
    const int faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
                              {3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
    std::vector<Vec3> tris;
    for (const auto& face : faces) {
      tris.push_back(verts[static_cast<std::size_t>(face[0])]);
      tris.push_back(verts[static_cast<std::size_t>(face[1])]);
      tris.push_back(verts[static_cast<std::size_t>(face[2])]);
    }

    const auto mesh = cs2::aim::project_mesh_occluder(Vec3{}, basis, verts, tris, 1.0F);
    expect(mesh.has_value(), "the mesh projects");
    if (mesh.has_value()) {
      expect(mesh->kind == cs2::aim::SolidKind::mesh, "the cell carries the mesh kind");
      // A mesh is a triangle **soup**: the span is the nearest crossing, i.e. the
      // surface the bullet hits. Pairing the two nearest crossings would turn a
      // wall in front of the floor into one fake solid hundreds of units thick
      // (`docs/re/bullet-trace.md`), so one crossing is one surface.
      const auto span = mesh->ray_span(Vec3{}, Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
      expect(span.has_value() && near(span->entry, x0, 0.1F) && near(span->exit, x0, 0.1F),
             "the mesh ray span is the nearest surface");
      expect(near(mesh->lower_bound_distance(Vec3{}), x0, 0.05F),
             "the mesh lower bound is the AABB distance");
      expect(!mesh->ray_span(Vec3{}, Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F}).has_value(),
             "a ray that misses the mesh has no span");
    }

    // The triangle BVH (`MeshBvh`) must give the *identical* span to the flat scan for
    // every ray — it is a pure acceleration (`docs/ragebot-perf.md` §3.7).
    {
      auto bvh = std::make_shared<const cs2::aim::MeshBvh>(cs2::aim::build_mesh_bvh(tris));
      const auto mesh_bvh = cs2::aim::project_mesh_occluder(Vec3{}, basis, verts, tris, 1.0F, bvh);
      expect(mesh_bvh.has_value(), "the BVH mesh projects");
      if (mesh.has_value() && mesh_bvh.has_value()) {
        int compared = 0;
        for (int iy = -6; iy <= 6; ++iy) {
          for (int iz = -6; iz <= 6; ++iz) {
            const Vec3 direction{
                .x = 1.0F, .y = static_cast<float>(iy) * 0.3F, .z = static_cast<float>(iz) * 0.3F};
            const auto flat = mesh->ray_span(Vec3{}, direction);      // flat triangle scan
            const auto fast = mesh_bvh->ray_span(Vec3{}, direction);  // BVH
            expect(flat.has_value() == fast.has_value(), "the BVH and flat scan agree on a hit");
            if (flat.has_value() && fast.has_value()) {
              expect(near(flat->entry, fast->entry, 1e-3F) && near(flat->exit, fast->exit, 1e-3F),
                     "the BVH span equals the flat scan");
            }
            ++compared;
          }
        }
        expect(compared > 0, "BVH equivalence rays ran");
      }
    }

    // The heatmap attenuates a head behind the mesh surface: the engine's entry
    // cost for the solid the box bounds (one enter/leave span, thickness 2), applied
    // to the weapon's damage and then scaled by the headshot multiplier:
    // lost = 2^2/24 + (3/4 * 1.25) * 3 + 0.16 * 100 = 18.98, so 81.02 * 4.
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));
    HeatmapOptions options;
    options.weapon = WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .penetration = 4.0F, .headshot_multiplier = 4.0F};
    options.penetration = cs2::aim::PenetrationModel::engine;
    options.penetration_params.weapon_penetration = 4.0F;
    if (mesh.has_value()) {
      options.occlusion.push_back(*mesh);
    }
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    expect(!map.cells.empty(), "the mesh occluder leaves a cell");
    // The ray crosses the box **twice** (the near face at 49 and the far face at 51),
    // but a closed shell is **one solid**: the near face is the entry, the far face
    // its leave, and the thickness is 2. Charging two entry fees and no thickness
    // (the old rule) is the bug that let a shot through a building value like a shot
    // through a doorway.
    for (const auto& cell : map.cells) {
      expect(near(cell.value, 324.08F, 0.5F), "a closed mesh shell is one solid with thickness");
    }
  }

  // --- several surfaces inside ONE cluster ----------------------------------
  // The regression that made the ragebot shoot through walls: a mesh cluster covers a
  // whole area of the map, so one ray can cross it several times (a wall, then the
  // floor, then another wall). Reporting only the nearest crossing made every surface
  // after the first free.
  {
    const float near_x = 30.0F;
    const float far_x = 70.0F;
    std::vector<Vec3> verts;
    std::vector<Vec3> tris;
    for (const float x : {near_x, far_x}) {
      verts.push_back(Vec3{.x = x, .y = -40.0F, .z = -40.0F});
      verts.push_back(Vec3{.x = x, .y = 40.0F, .z = -40.0F});
      verts.push_back(Vec3{.x = x, .y = 40.0F, .z = 40.0F});
      verts.push_back(Vec3{.x = x, .y = -40.0F, .z = 40.0F});
      const std::size_t base = verts.size() - 4;
      tris.insert(tris.end(), {verts[base], verts[base + 1], verts[base + 2], verts[base],
                               verts[base + 2], verts[base + 3]});
    }
    const auto two = cs2::aim::project_mesh_occluder(Vec3{}, basis, verts, tris, 1.0F);
    expect(two.has_value(), "the two-sheet cluster projects");
    if (two.has_value()) {
      std::vector<cs2::aim::MeshCrossing> crossings;
      two->ray_crossings(Vec3{}, Vec3{.x = 1.0F, .y = 0.5F, .z = 0.1F}, crossings);
      expect(crossings.size() == 2, "a ray crosses both sheets");
      if (crossings.size() == 2) {
        expect(
            near(crossings[0].distance, near_x, 0.1F) && near(crossings[1].distance, far_x, 0.1F),
            "the crossings are in order along the ray");
        // Both sheets are wound the same way, so neither is the back face of the
        // other: they stay two surfaces (no fake volume between them).
        const std::vector<cs2::aim::RaySpan> spans = cs2::aim::mesh_spans(crossings);
        expect(spans.size() == 2, "two same-facing sheets stay two surfaces");
        for (const cs2::aim::RaySpan& span : spans) {
          expect(near(span.entry, span.exit, 1e-3F), "neither sheet becomes a volume");
        }
      }
      // And the field charges for both: 100 → 81.1875 → 65.385, times 4.
      std::vector<TargetPose> two_poses;
      TargetPose two_pose;
      two_pose.target = 1;
      two_pose.hitboxes.push_back(head_capsule(100.0F));
      two_poses.push_back(std::move(two_pose));
      HeatmapOptions two_options;
      two_options.weapon = WeaponProfile{.damage = 100.0F,
                                         .range_modifier = 1.0F,
                                         .penetration = 4.0F,
                                         .headshot_multiplier = 4.0F};
      two_options.penetration = cs2::aim::PenetrationModel::engine;
      two_options.penetration_params.weapon_penetration = 4.0F;
      two_options.occlusion.push_back(*two);
      const auto two_map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, two_poses, two_options);
      for (const auto& cell : two_map.cells) {
        expect(near(cell.value, 261.5F, 0.5F), "both sheets in the cluster attenuate");
      }
    }
  }

  // --- pairing a mesh's crossings into the solids the bullet crosses ---------
  {
    using cs2::aim::MeshCrossing;
    // A closed shell: a front face and the back face that closes it are **one**
    // solid with the shell's thickness.
    const std::vector<cs2::aim::RaySpan> box =
        cs2::aim::mesh_spans({MeshCrossing{.distance = 49.0F, .entering = true},
                              MeshCrossing{.distance = 51.0F, .entering = false}});
    expect(box.size() == 1 && near(box[0].entry, 49.0F) && near(box[0].exit, 51.0F),
           "a front/back pair is one solid with its thickness");
    // Two sheets wound the same way never become a fake volume between them.
    const std::vector<cs2::aim::RaySpan> sheets =
        cs2::aim::mesh_spans({MeshCrossing{.distance = 30.0F, .entering = false},
                              MeshCrossing{.distance = 70.0F, .entering = false}});
    expect(sheets.size() == 2 && near(sheets[0].entry, sheets[0].exit) &&
               near(sheets[1].entry, sheets[1].exit),
           "two sheets in one cluster stay two surfaces");
    // A sheet followed by a closed volume: the lone entry stays a surface.
    const std::vector<cs2::aim::RaySpan> mixed =
        cs2::aim::mesh_spans({MeshCrossing{.distance = 10.0F, .entering = true},
                              MeshCrossing{.distance = 20.0F, .entering = true},
                              MeshCrossing{.distance = 30.0F, .entering = false}});
    expect(mixed.size() == 2 && near(mixed[0].entry, 10.0F) && near(mixed[0].exit, 10.0F) &&
               near(mixed[1].entry, 20.0F) && near(mixed[1].exit, 30.0F),
           "a lone entry is a surface; the pair that follows is a solid");
  }

  // --- an OPEN mesh surface (a map wall, not a closed prop) ------------------
  // A collision mesh is a triangle **soup**, and the map's walls are open sheets:
  // a ray crosses the surface once, so the two nearest crossings the span is built
  // from are the *same* triangle (entry == exit). The wall is still a wall — it is
  // what the bullet hits — so the shot must lose the surface's entry cost. This is
  // the regression the live ragebot hit: every mesh wall was silently dropped
  // because the layer needed `thickness > 0` (`docs/re/bullet-trace.md`).
  {
    const float wall_x = 50.0F;
    const std::vector<Vec3> verts = {{.x = wall_x, .y = -40.0F, .z = -40.0F},
                                     {.x = wall_x, .y = 40.0F, .z = -40.0F},
                                     {.x = wall_x, .y = 40.0F, .z = 40.0F},
                                     {.x = wall_x, .y = -40.0F, .z = 40.0F}};
    // Two triangles, both windings absent: the ray test does not cull.
    const std::vector<Vec3> tris = {verts[0], verts[1], verts[2], verts[0], verts[2], verts[3]};
    const auto wall = cs2::aim::project_mesh_occluder(Vec3{}, basis, verts, tris, 1.0F);
    expect(wall.has_value(), "the open mesh wall projects");
    if (wall.has_value()) {
      const auto span = wall->ray_span(Vec3{}, Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
      expect(span.has_value() && near(span->entry, wall_x, 0.1F),
             "the open mesh wall is hit at the surface");
      // The surface is infinitely thin: entry and exit are the same crossing.
      expect(span.has_value() && near(span->exit, span->entry, 1e-3F),
             "an open sheet's span has no depth");

      std::vector<TargetPose> wall_poses;
      TargetPose pose;
      pose.target = 1;
      pose.hitboxes.push_back(head_capsule(100.0F));
      wall_poses.push_back(std::move(pose));
      // The **engine** model is what the ragebot runs (`solve_input->penetration`
      // in `service.cpp`), and its entry cost does not depend on the depth.
      HeatmapOptions wall_options;
      wall_options.weapon = WeaponProfile{.damage = 100.0F,
                                          .range_modifier = 1.0F,
                                          .penetration = 4.0F,
                                          .headshot_multiplier = 4.0F};
      wall_options.penetration = cs2::aim::PenetrationModel::engine;
      wall_options.penetration_params.weapon_penetration = 4.0F;
      wall_options.occlusion.push_back(*wall);
      const auto unoccluded = cs2::aim::build_heatmap(
          Vec3{}, basis, 1.0F, wall_poses, HeatmapOptions{.weapon = wall_options.weapon});
      const auto blocked = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, wall_poses, wall_options);
      expect(!blocked.cells.empty(), "the open mesh wall leaves a cell");
      float unoccluded_value = 0.0F;
      for (const auto& cell : unoccluded.cells) {
        unoccluded_value = std::max(unoccluded_value, cell.value);
      }
      // The engine's entry cost, applied to the weapon's damage and *then* scaled by
      // the headshot multiplier: 100 - (0.16 * 100 + (3/4 * 1.25) * 3) = 81.1875,
      // times 4 = 324.75. The thickness term is zero: the surface is a sheet.
      for (const auto& cell : blocked.cells) {
        expect(near(cell.value, 324.75F, 0.5F),
               "an open mesh wall attenuates the head (a thin surface still costs)");
        expect(cell.value < unoccluded_value, "the open mesh wall is not free");
      }
      expect(blocked.used_occluders.size() == 1, "the open mesh wall is the used solid");
    }
  }

  if (failures == 0) {
    std::printf("aim-occlusion: ok\n");
    return 0;
  }
  std::printf("aim-occlusion: %d failure(s)\n", failures);
  return 1;
}
