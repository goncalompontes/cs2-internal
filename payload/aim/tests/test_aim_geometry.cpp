// Unit tests for the ragebot aim geometry (docs/ragebot.md, ADR-0022).
//
// Pure host tests: the chart projection, convex hull, polygon ops and the two
// optima (nearest point / Chebyshev centre) with no game required.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "cs2/aim/geometry.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

auto near2(const cs2::aim::Vec2& a, const cs2::aim::Vec2& b, float tolerance = 1e-3F) -> bool {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance);
}

}  // namespace

auto main() -> int {
  using cs2::aim::Basis;
  using cs2::aim::Obb;
  using cs2::aim::Polygon;
  using cs2::aim::Vec2;
  using cs2::aim::Vec3;

  // --- chart projection round-trip ----------------------------------------
  const Basis basis = Basis::world();
  {
    const auto p = basis.project(Vec3{10.0F, 1.0F, 2.0F});
    expect(p.has_value(), "a direction projects");
    if (p.has_value()) {
      // Absolute world yaw/pitch (radians): u = atan2(y, x), v = asin(z / |d|).
      expect(near(p->x, std::atan2(1.0F, 10.0F)), "project u (world yaw)");
      expect(near(p->y, std::asin(2.0F / std::sqrt(105.0F))), "project v (world pitch)");
      const Vec3 back = basis.unproject(*p);
      expect(near(back.x, 10.0F / std::sqrt(105.0F), 1e-4F), "unproject round-trip x");
    }
    // The chart is absolute, so a direction behind the eye still has yaw/pitch.
    const auto behind = basis.project(Vec3{-1.0F, 0.0F, 0.0F});
    expect(behind.has_value() && near(std::fabs(behind->x), 3.14159265F, 1e-3F),
           "a behind direction projects to yaw ~ pi");
  }

  // --- convex hull ---------------------------------------------------------
  const Vec2 square[] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}, {0.5F, 0.5F}};
  {
    const Polygon hull = cs2::aim::convex_hull(square);
    expect(hull.points.size() == 4, "hull drops the interior point");
    expect(near(cs2::aim::polygon_area(hull), 1.0F), "hull area");
    expect(near2(cs2::aim::polygon_centroid(hull), Vec2{0.5F, 0.5F}), "hull centroid");
    expect(cs2::aim::polygon_contains(hull, Vec2{0.5F, 0.5F}), "hull contains centre");
    expect(!cs2::aim::polygon_contains(hull, Vec2{1.5F, 0.5F}), "hull excludes outside");
    // CCW winding (positive area) is required by the edge normals below.
    expect(cs2::aim::polygon_area(hull) > 0.0F, "hull is counter-clockwise");
  }

  // --- clipping ------------------------------------------------------------
  {
    const Polygon hull = cs2::aim::convex_hull(square);
    const Polygon clipped = cs2::aim::clip_half_plane(hull, Vec2{1.0F, 0.0F}, 0.5F);
    expect(near(cs2::aim::polygon_area(clipped), 0.5F), "clip keeps the near half");
  }

  // --- optima --------------------------------------------------------------
  {
    const Polygon hull = cs2::aim::convex_hull(square);
    expect(near2(cs2::aim::nearest_point(hull), Vec2{0.0F, 0.0F}), "nearest point is the near corner");

    // A square offset away from the origin: the nearest point is its near edge.
    const Vec2 offset[] = {{2.0F, -1.0F}, {4.0F, -1.0F}, {4.0F, 1.0F}, {2.0F, 1.0F}};
    const Polygon away = cs2::aim::convex_hull(offset);
    expect(near2(cs2::aim::nearest_point(away), Vec2{2.0F, 0.0F}), "nearest point on the near edge");

    const Vec2 centre = cs2::aim::chebyshev_center(hull);
    expect(near2(centre, Vec2{0.5F, 0.5F}), "chebyshev centre of the unit square");
  }

  // --- OBB projection ------------------------------------------------------
  {
    const Obb ahead{.center = Vec3{10.0F, 0.0F, 0.0F}, .half = Vec3{1.0F, 1.0F, 1.0F}};
    const Polygon poly = cs2::aim::project_obb(Vec3{}, ahead, basis);
    expect(poly.points.size() == 4, "head-on box projects to a quad");
    expect(near2(cs2::aim::polygon_centroid(poly), Vec2{0.0F, 0.0F}), "box centroid at the aim");
    expect(cs2::aim::polygon_contains(poly, Vec2{0.0F, 0.0F}), "box silhouette covers the aim");
    // Near face at x=9, half-extent 1 -> angular half-size 1/9.
    expect(near(poly.points.size() == 4 ? cs2::aim::polygon_area(poly) : 0.0F, 4.0F / 81.0F, 2e-3F),
           "head-on box area");

    const Obb behind{.center = Vec3{-10.0F, 0.0F, 0.0F}, .half = Vec3{1.0F, 1.0F, 1.0F}};
    const Polygon behind_poly = cs2::aim::project_obb(Vec3{}, behind, basis);
    expect(!behind_poly.empty(), "a box behind the eye still projects (absolute chart)");
    float max_yaw = 0.0F;
    for (const Vec2& p : behind_poly.points) {
      max_yaw = std::fabs(p.x) > max_yaw ? std::fabs(p.x) : max_yaw;
    }
    expect(max_yaw > 3.0F, "the behind box sits at |yaw| ~ pi");
  }

  // --- the +-pi seam -------------------------------------------------------
  {
    // A silhouette whose projected points straddle the seam (a target due west) must
    // stay one contiguous polygon, not a chart-wide hull. Points at u = +-3.13 about
    // a reference of pi (the west direction).
    const Vec2 straddle[] = {{3.13F, -0.01F}, {3.13F, 0.01F}, {-3.13F, 0.01F}, {-3.13F, -0.01F}};
    const Polygon wide = cs2::aim::convex_hull(straddle);
    expect(cs2::aim::polygon_area(wide) > 0.1F, "a raw hull across the seam is chart-wide");
    const Polygon tight = cs2::aim::convex_hull_unwrapped(straddle, 3.14159265F);
    expect(cs2::aim::polygon_area(tight) < 0.01F, "unwrapping keeps the seam silhouette tight");
  }

  // --- sphere projection at a steep pitch (the chart metric) ----------------
  {
    // The same sphere at the same distance, seen level and 60 degrees up. The chart's
    // yaw spans `1/cos(pitch)` more radians per true angular radian, so the silhouette
    // is an ellipse about that much wider in u than in v; a chart-space *circle* (the
    // old projection) would be wrong by exactly that factor, which is the
    // angle-dependent error the ragebot showed.
    const float r = 1.0F;
    const float dist = 10.0F;
    for (const float pitch : {0.0F, 1.047F}) {  // 0 and 60 degrees
      const Vec3 centre{dist * std::cos(pitch), 0.0F, dist * std::sin(pitch)};
      const Polygon poly = cs2::aim::project_sphere(Vec3{}, centre, r, basis, 64);
      expect(!poly.empty(), "steep sphere projects");
      float min_u = 0.0F;
      float max_u = 0.0F;
      float min_v = 0.0F;
      float max_v = 0.0F;
      if (!poly.points.empty()) {
        min_u = max_u = poly.points.front().x;
        min_v = max_v = poly.points.front().y;
      }
      for (const Vec2& p : poly.points) {
        min_u = std::min(min_u, p.x);
        max_u = std::max(max_u, p.x);
        min_v = std::min(min_v, p.y);
        max_v = std::max(max_v, p.y);
      }
      const float half_u = 0.5F * (max_u - min_u);
      const float half_v = 0.5F * (max_v - min_v);
      const float expected = cs2::aim::yaw_scale(pitch);
      expect(near(half_u / half_v, expected, 0.1F), "sphere yaw width scales with 1/cos(pitch)");
    }
  }

  // --- sphere projection ---------------------------------------------------
  {
    const Polygon head = cs2::aim::project_sphere(Vec3{}, Vec3{10.0F, 0.0F, 0.0F}, 1.0F, basis);
    expect(!head.empty(), "sphere projects");
    expect(near2(cs2::aim::polygon_centroid(head), Vec2{0.0F, 0.0F}), "sphere centred on the aim");
    const float rho = std::tan(std::asin(0.1F));
    expect(near(cs2::aim::polygon_area(head), 3.14159265F * rho * rho, 3e-3F), "sphere area");
  }

  if (failures == 0) {
    std::printf("aim-geometry: ok\n");
    return 0;
  }
  return 1;
}
