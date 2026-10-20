// Unit tests for the occlusion / penetration model (docs/ragebot.md, ADR-0022).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

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
    v.push_back(cs2::game::Vec3{cx + ((i & 1) != 0 ? hx : -hx), cy + ((i & 2) != 0 ? hy : -hy),
                                cz + ((i & 4) != 0 ? hz : -hz)});
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
      OccluderPlane{Vec3{1.0F, 0.0F, 0.0F}, cx + hx},
      OccluderPlane{Vec3{-1.0F, 0.0F, 0.0F}, -(cx - hx)},
      OccluderPlane{Vec3{0.0F, 1.0F, 0.0F}, cy + hy},
      OccluderPlane{Vec3{0.0F, -1.0F, 0.0F}, -(cy - hy)},
      OccluderPlane{Vec3{0.0F, 0.0F, 1.0F}, cz + hz},
      OccluderPlane{Vec3{0.0F, 0.0F, -1.0F}, -(cz - hz)},
  };
}

auto make_occluder(const cs2::aim::Basis& basis, float cx, float cy, float cz, float hx, float hy,
                   float hz) -> std::optional<cs2::aim::OccluderCell> {
  return cs2::aim::project_occluder(cs2::game::Vec3{}, basis, box_vertices(cx, cy, cz, hx, hy, hz),
                                    box_planes(cx, cy, cz, hx, hy, hz), 1.0F);
}

auto square(float half) -> cs2::aim::Polygon {
  using cs2::aim::Vec2;
  return cs2::aim::Polygon{
      {Vec2{-half, -half}, Vec2{half, -half}, Vec2{half, half}, Vec2{-half, half}}};
}

// A head capsule at `x` along the view axis.
auto head_capsule(float x) -> cs2::aim::Hitbox {
  cs2::aim::Hitbox hitbox;
  hitbox.is_capsule = true;
  hitbox.capsule =
      cs2::aim::Capsule{cs2::game::Vec3{x, 0.0F, 0.0F}, cs2::game::Vec3{x, 0.0F, 4.0F}, 4.0F};
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

  const Basis basis{.forward = Vec3{1.0F, 0.0F, 0.0F},
                    .right = Vec3{0.0F, 1.0F, 0.0F},
                    .up = Vec3{0.0F, 0.0F, 1.0F}};

  // --- thickness along a direction -----------------------------------------
  {
    auto cell = make_occluder(basis, 50, 0, 0, 2, 10, 10);
    expect(cell.has_value(), "a box in front projects");
    if (cell.has_value()) {
      expect(near(cell->thickness_along(Vec3{1, 0, 0}), 4.0F), "thickness along the view axis");
      // Along +Y the box is 20 deep.
      expect(near(cell->thickness_along(Vec3{0, 1, 0}), 20.0F), "thickness along another axis");

      // The exact ray test: the eye looks down +X, so the box spans x in [48, 52].
      const auto span = cell->ray_span(Vec3{}, Vec3{1, 0, 0});
      expect(span.has_value(), "the ray through the box hits it");
      if (span.has_value()) {
        expect(near(span->entry, 48.0F), "the ray enters the front face");
        expect(near(span->exit, 52.0F), "the ray leaves the back face");
      }
      // A ray that misses it (offset in y) reports no span.
      expect(!cell->ray_span(Vec3{}, cs2::aim::Vec3{1.0F, 1.0F, 0.0F}).has_value(),
             "a ray that misses the box has no span");
      // A box behind the eye is never entered.
      const auto behind_span = cell->ray_span(cs2::game::Vec3{100.0F, 0.0F, 0.0F}, Vec3{1, 0, 0});
      expect(!behind_span.has_value(), "a box behind the eye has no span");
    }
    // A box behind the eye still projects (the chart is absolute yaw/pitch), but the
    // forward ray never reaches it — `ray_span` is what keeps it from occluding.
    const auto behind = make_occluder(basis, -50, 0, 0, 2, 2, 2);
    expect(behind.has_value(), "a box behind the eye projects (absolute chart)");
    if (behind.has_value()) {
      expect(!behind->ray_span(cs2::game::Vec3{}, Vec3{1, 0, 0}).has_value(),
             "a box behind the eye has no forward ray span");
    }
  }

  // --- convex subtraction (the cell split) ---------------------------------
  {
    const auto a = square(1.0F);
    const auto b = cs2::aim::Polygon{
        {cs2::aim::Vec2{0, 0}, cs2::aim::Vec2{1, 0}, cs2::aim::Vec2{1, 1}, cs2::aim::Vec2{0, 1}}};
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
      expect(near(front->lower_bound_distance(Vec3{50, 0, 0}), 0.0F),
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
    auto sphere = cs2::aim::project_sphere_occluder(Vec3{}, basis, Vec3{50, 0, 0}, 2.0F, 1.0F);
    expect(sphere.has_value(), "the sphere projects");
    if (sphere.has_value()) {
      expect(sphere->kind == cs2::aim::SolidKind::sphere, "the cell carries the sphere kind");
      const auto span = sphere->ray_span(Vec3{}, Vec3{1, 0, 0});
      expect(span.has_value() && near(span->entry, 48.0F) && near(span->exit, 52.0F),
             "the sphere ray span is exact");
      expect(near(sphere->lower_bound_distance(Vec3{}), 48.0F), "the sphere lower bound");
      expect(!sphere->ray_span(Vec3{}, Vec3{0, 1, 0}).has_value(),
             "a ray that misses the sphere has no span");
    }

    auto capsule = cs2::aim::project_capsule_occluder(Vec3{}, basis, Vec3{48, 0, 0}, Vec3{52, 0, 0},
                                                      1.0F, 1.0F);
    expect(capsule.has_value(), "the capsule projects");
    if (capsule.has_value()) {
      expect(capsule->kind == cs2::aim::SolidKind::capsule, "the cell carries the capsule kind");
      const auto span = capsule->ray_span(Vec3{}, Vec3{1, 0, 0});
      expect(span.has_value() && near(span->entry, 47.0F) && near(span->exit, 53.0F),
             "the axial span includes the rounded caps");
      const auto side = capsule->ray_span(Vec3{50, 0, 0}, Vec3{0, 1, 0});
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
    if (auto sphere = cs2::aim::project_sphere_occluder(Vec3{}, basis, Vec3{50, 0, 0}, 5.0F, 1.0F);
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
    const std::vector<Vec3> cube = {Vec3{5, 0, 0}, Vec3{6, 0, 0}, Vec3{6, 1, 0}, Vec3{5, 1, 0},
                                    Vec3{5, 0, 1}, Vec3{6, 0, 1}, Vec3{6, 1, 1}, Vec3{5, 1, 1}};
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
      const auto span = cell->ray_span(Vec3{}, Vec3{1.0F, 0.0F, 0.0F});
      expect(span.has_value() && near(span->entry, 5.0F) && near(span->exit, 6.0F),
             "the mesh-derived solid's ray span matches the cube");
    }

    // A coplanar set has no 3-D faces.
    const std::vector<Vec3> flat = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}};
    expect(cs2::aim::convex_hull_3d(flat).empty(), "a coplanar set has no 3-D faces");
  }

  // --- projection degenerate cases + bounds (plan P4.23) --------------------
  {
    // One point inside the cube's hull does not add faces.
    const std::vector<Vec3> cube = {Vec3{5, 0, 0}, Vec3{6, 0, 0}, Vec3{6, 1, 0}, Vec3{5, 1, 0},
                                    Vec3{5, 0, 1}, Vec3{6, 0, 1}, Vec3{6, 1, 1}, Vec3{5, 1, 1}};
    std::vector<Vec3> with_center = cube;
    with_center.push_back(Vec3{5.5F, 0.5F, 0.5F});
    expect(cs2::aim::convex_hull_3d(with_center).size() == 6,
           "an interior point does not add hull faces");
    // A tetrahedron has exactly 4 faces.
    const std::vector<Vec3> tetra = {Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{0, 4, 0}, Vec3{0, 0, 4}};
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
    const float x0 = 49.0F, x1 = 51.0F, y0 = -5.0F, y1 = 5.0F, z0 = -4.7F, z1 = 5.3F;
    const std::vector<Vec3> verts = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                                     {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
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
      const auto span = mesh->ray_span(Vec3{}, Vec3{1.0F, 0.0F, 0.0F});
      expect(span.has_value() && near(span->entry, x0, 0.1F) && near(span->exit, x1, 0.1F),
             "the mesh ray span is the box's near/far faces");
      expect(near(mesh->lower_bound_distance(Vec3{}), x0, 0.05F),
             "the mesh lower bound is the AABB distance");
      expect(!mesh->ray_span(Vec3{}, Vec3{0.0F, 1.0F, 0.0F}).has_value(),
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
            const Vec3 direction{1.0F, static_cast<float>(iy) * 0.3F,
                                 static_cast<float>(iz) * 0.3F};
            const auto flat = mesh->ray_span(Vec3{}, direction);       // flat triangle scan
            const auto fast = mesh_bvh->ray_span(Vec3{}, direction);   // BVH
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

    // The heatmap attenuates a head behind the mesh wall (thickness 2).
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));
    HeatmapOptions options;
    options.weapon = WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .penetration = 4.0F, .headshot_multiplier = 4.0F};
    if (mesh.has_value()) {
      options.occlusion.push_back(*mesh);
    }
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    expect(!map.cells.empty(), "the mesh occluder leaves a cell");
    for (const auto& cell : map.cells) {
      expect(near(cell.value, 200.0F, 5.0F), "the mesh wall attenuates the head (1 - 2/4)");
    }
  }

  if (failures == 0) {
    std::printf("aim-occlusion: ok\n");
    return 0;
  }
  std::printf("aim-occlusion: %d failure(s)\n", failures);
  return 1;
}
