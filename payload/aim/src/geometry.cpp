#include "cs2/aim/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

// Closest point to `p` on the segment `a`-`b`.
[[nodiscard]] auto closest_on_segment(const Vec2& a, const Vec2& b, const Vec2& p) noexcept -> Vec2 {
  const Vec2 ab = b - a;
  const float denom = game::dot(ab, ab);
  if (denom <= k_epsilon) {
    return a;
  }
  float t = game::dot(p - a, ab) / denom;
  t = std::clamp(t, 0.0F, 1.0F);
  return a + ab * t;
}

// The signed distance from `p` to the supporting line of edge `a`->`b` of a CCW
// polygon (positive inside).
[[nodiscard]] auto edge_distance(const Vec2& a, const Vec2& b, const Vec2& p, Vec2& normal_out,
                                 float& offset_out) noexcept -> float {
  const Vec2 e = b - a;
  const float len = game::length(e);
  normal_out = len <= k_epsilon ? Vec2{0.0F, 0.0F} : Vec2{e.y / len, -e.x / len};
  offset_out = game::dot(normal_out, a);
  return offset_out - game::dot(normal_out, p);
}

constexpr float k_pi = 3.14159265358979323846F;
constexpr float k_two_pi = 6.28318530717958647692F;

// Shift a yaw into the `±π` window about `centre` (the chart's `±π` seam).
[[nodiscard]] auto unwrap_yaw(float u, float centre) noexcept -> float {
  while (u - centre > k_pi) {
    u -= k_two_pi;
  }
  while (u - centre < -k_pi) {
    u += k_two_pi;
  }
  return u;
}

// The largest inscribed circle of a convex polygon: returns its radius and, when
// `centre_out` is non-null, its centre.
[[nodiscard]] auto chebyshev(const Polygon& polygon, Vec2* centre_out) -> float {
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    if (centre_out != nullptr) {
      *centre_out = Vec2{};
    }
    return 0.0F;
  }

  std::vector<Vec2> normals(n);
  std::vector<float> offsets(n);
  for (std::size_t i = 0; i < n; ++i) {
    (void)edge_distance(polygon.points[i], polygon.points[(i + 1) % n], polygon.points[i],
                        normals[i], offsets[i]);
  }

  const Vec2 centre = polygon_centroid(polygon);
  float hi = 0.0F;
  for (const Vec2& p : polygon.points) {
    hi = std::max(hi, game::length(p - centre));
  }

  Vec2 best = centre;
  float lo = 0.0F;
  for (int iter = 0; iter < 48; ++iter) {
    const float mid = 0.5F * (lo + hi);
    Polygon feasible = polygon;
    for (std::size_t i = 0; i < n && !feasible.empty(); ++i) {
      feasible = clip_half_plane(feasible, normals[i], offsets[i] - mid);
    }
    if (!feasible.empty()) {
      lo = mid;
      best = polygon_centroid(feasible);
    } else {
      hi = mid;
    }
  }
  if (centre_out != nullptr) {
    *centre_out = best;
  }
  return lo;
}

}  // namespace

auto yaw_scale(float pitch) noexcept -> float {
  const float c = std::cos(pitch);
  return 1.0F / (c > 0.05F ? c : 0.05F);
}

auto Basis::project(const Vec3& direction) const noexcept -> std::optional<Vec2> {
  const float len = game::length(direction);
  if (len <= k_epsilon) {
    return std::nullopt;  // a point at the eye has no direction
  }
  const float nx = direction.x / len;
  const float ny = direction.y / len;
  const float nz = direction.z / len;
  // Absolute world yaw/pitch (radians): a function of the world only, never the
  // camera. `u` is yaw about +Z, `v` is pitch above the XY plane.
  return Vec2{std::atan2(ny, nx), std::asin(std::clamp(nz, -1.0F, 1.0F))};
}

auto Basis::unproject(const Vec2& point) const noexcept -> Vec3 {
  const float cp = std::cos(point.y);
  return Vec3{cp * std::cos(point.x), cp * std::sin(point.x), std::sin(point.y)};
}

auto Obb::corners() const noexcept -> std::array<Vec3, 8> {
  std::array<Vec3, 8> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    const float sx = (i & 1U) != 0U ? 1.0F : -1.0F;
    const float sy = (i & 2U) != 0U ? 1.0F : -1.0F;
    const float sz = (i & 4U) != 0U ? 1.0F : -1.0F;
    out[i] = center + axis_x * (half.x * sx) + axis_y * (half.y * sy) + axis_z * (half.z * sz);
  }
  return out;
}

auto convex_hull(std::span<const Vec2> points) -> Polygon {
  Polygon out;
  if (points.size() < 3) {
    return out;
  }

  std::vector<Vec2> sorted(points.begin(), points.end());
  std::sort(sorted.begin(), sorted.end(), [](const Vec2& a, const Vec2& b) noexcept {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  });

  std::vector<Vec2> hull(sorted.size() * 2);
  std::size_t k = 0;
  const auto turn = [](const Vec2& o, const Vec2& a, const Vec2& b) noexcept {
    return game::cross(a - o, b - o);
  };

  // Lower hull, then upper hull (Andrew's monotone chain). `<= 0` drops
  // collinear points, so a degenerate set collapses below 3 vertices.
  for (const Vec2& p : sorted) {
    while (k >= 2 && turn(hull[k - 2], hull[k - 1], p) <= 0.0F) {
      --k;
    }
    hull[k++] = p;
  }
  for (std::size_t i = sorted.size() - 1, t = k + 1; i-- > 0;) {
    while (k >= t && turn(hull[k - 2], hull[k - 1], sorted[i]) <= 0.0F) {
      --k;
    }
    hull[k++] = sorted[i];
  }

  hull.resize(k - 1);  // the last point repeats the first
  if (hull.size() < 3) {
    return out;
  }
  out.points = std::move(hull);
  return out;
}

auto polygon_area(const Polygon& polygon) noexcept -> float {
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return 0.0F;
  }
  float twice = 0.0F;
  for (std::size_t i = 0; i < n; ++i) {
    twice += game::cross(polygon.points[i], polygon.points[(i + 1) % n]);
  }
  return twice * 0.5F;
}

auto polygon_centroid(const Polygon& polygon) noexcept -> Vec2 {
  const std::size_t n = polygon.points.size();
  if (n == 0) {
    return Vec2{};
  }
  if (n < 3) {
    Vec2 sum{};
    for (const Vec2& p : polygon.points) {
      sum = sum + p;
    }
    return sum * (1.0F / static_cast<float>(n));
  }

  float area = 0.0F;
  Vec2 acc{};
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& a = polygon.points[i];
    const Vec2& b = polygon.points[(i + 1) % n];
    const float cr = game::cross(a, b);
    area += cr;
    acc = acc + (a + b) * cr;
  }
  area *= 0.5F;
  if (std::fabs(area) <= k_epsilon) {
    Vec2 sum{};
    for (const Vec2& p : polygon.points) {
      sum = sum + p;
    }
    return sum * (1.0F / static_cast<float>(n));
  }
  return acc * (1.0F / (6.0F * area));
}

auto polygon_contains(const Polygon& polygon, const Vec2& point) noexcept -> bool {
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& a = polygon.points[i];
    const Vec2& b = polygon.points[(i + 1) % n];
    if (game::cross(b - a, point - a) < -k_epsilon) {
      return false;
    }
  }
  return true;
}

auto clip_half_plane(const Polygon& polygon, const Vec2& normal, float offset) -> Polygon {
  Polygon out;
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return out;
  }
  out.points.reserve(n + 1);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& cur = polygon.points[i];
    const Vec2& prev = polygon.points[(i + n - 1) % n];
    const float dc = game::dot(normal, cur) - offset;
    const float dp = game::dot(normal, prev) - offset;
    if (dc <= 0.0F) {
      if (dp > 0.0F) {
        const float t = dp / (dp - dc);
        out.points.push_back(prev + (cur - prev) * t);
      }
      out.points.push_back(cur);
    } else if (dp <= 0.0F) {
      const float t = dp / (dp - dc);
      out.points.push_back(prev + (cur - prev) * t);
    }
  }
  return out;
}

auto nearest_point(const Polygon& polygon) -> Vec2 { return nearest_point_to(polygon, Vec2{}); }

auto chebyshev_center(const Polygon& polygon) -> Vec2 {
  Vec2 centre{};
  (void)chebyshev(polygon, &centre);
  return centre;
}

auto chebyshev_radius(const Polygon& polygon) -> float { return chebyshev(polygon, nullptr); }

auto polygon_margin(const Polygon& polygon, const Vec2& point) -> float {
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return -1.0F;
  }
  float best = 1.0e30F;
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& a = polygon.points[i];
    const Vec2& b = polygon.points[(i + 1) % n];
    const Vec2 closest = closest_on_segment(a, b, point);
    const Vec2 e = b - a;
    const float len = game::length(e);
    float distance = game::length(point - closest);
    if (len > k_epsilon) {
      // Interior is `dot(normal, p) <= offset` with this normal (the same convention the
      // clipper uses), so the point is outside an edge when the dot product is positive.
      const Vec2 normal{e.y / len, -e.x / len};
      if (game::dot(point - a, normal) > 0.0F) {
        distance = -distance;
      }
    }
    if (std::fabs(distance) < std::fabs(best)) {
      best = distance;
    }
  }
  return best;
}

auto convex_hull_unwrapped(std::span<const Vec2> points, float reference_yaw) -> Polygon {
  std::vector<Vec2> local(points.begin(), points.end());
  for (Vec2& p : local) {
    p.x = unwrap_yaw(p.x, reference_yaw);
  }
  return convex_hull(local);
}

auto nearest_point_to(const Polygon& polygon, const Vec2& reference) -> Vec2 {
  const std::size_t n = polygon.points.size();
  if (n < 3) {
    return Vec2{};
  }
  if (polygon_contains(polygon, reference)) {
    return reference;
  }

  Vec2 best{};
  float best_sq = -1.0F;
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2 candidate =
        closest_on_segment(polygon.points[i], polygon.points[(i + 1) % n], reference);
    const Vec2 delta = candidate - reference;
    const float d = game::dot(delta, delta);
    if (best_sq < 0.0F || d < best_sq) {
      best_sq = d;
      best = candidate;
    }
  }
  return best;
}

auto project_obb(const Vec3& eye, const Obb& box, const Basis& basis) -> Polygon {
  std::array<Vec2, 8> projected{};
  std::size_t count = 0;
  for (const Vec3& corner : box.corners()) {
    if (const auto p = basis.project(corner - eye)) {
      projected[count++] = *p;
    }
  }
  if (count < 3) {
    return Polygon{};
  }
  // Unwrap the yaws about the box centre's yaw so a box at world yaw ≈ ±π stays one
  // contiguous polygon instead of a chart-wide hull across the seam.
  float reference_yaw = 0.0F;
  if (const auto centre = basis.project(box.center - eye)) {
    reference_yaw = centre->x;
  }
  return convex_hull_unwrapped(std::span<const Vec2>(projected.data(), count), reference_yaw);
}

auto project_sphere(const Vec3& eye, const Vec3& center, float radius, const Basis& basis,
                    std::size_t segments) -> Polygon {
  const Vec3 to_center = center - eye;
  const float dist = game::length(to_center);
  if (dist <= k_epsilon || radius <= 0.0F) {
    return Polygon{};
  }
  const Vec3 axis = to_center * (1.0F / dist);
  // The sphere's silhouette is the cone of directions at `asin(radius/dist)` about the
  // axis. Each boundary direction is built in 3-D and projected, so the silhouette is
  // exact at any pitch — a chart-space circle of angular radius `rho` would be wrong by
  // `1/cos(pitch)` in yaw (the aim-dependent error this replaces).
  const Vec3 reference =
      std::fabs(axis.z) < 0.9F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{1.0F, 0.0F, 0.0F};
  const Vec3 right = game::normalized(game::cross(reference, axis));
  const Vec3 up = game::cross(axis, right);
  const float rho = std::asin(std::clamp(radius / dist, 0.0F, 1.0F));
  const float cos_rho = std::cos(rho);
  const float sin_rho = std::sin(rho);

  std::vector<Vec2> points;
  const std::size_t n = std::max<std::size_t>(segments, 3);
  points.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float theta = (2.0F * k_pi * static_cast<float>(i)) / static_cast<float>(n);
    const Vec3 direction =
        axis * cos_rho + (right * std::cos(theta) + up * std::sin(theta)) * sin_rho;
    if (const auto p = basis.project(direction)) {
      points.push_back(*p);
    }
  }
  if (points.size() < 3) {
    return Polygon{};
  }
  const auto centre = basis.project(axis);
  return convex_hull_unwrapped(points, centre.has_value() ? centre->x : 0.0F);
}

auto project_capsule(const Vec3& eye, const Vec3& a, const Vec3& b, float radius,
                     const Basis& basis, std::size_t segments) -> Polygon {
  const Vec3 centre_point = (a + b) * 0.5F;
  float reference_yaw = 0.0F;
  if (const auto centre = basis.project(centre_point - eye)) {
    reference_yaw = centre->x;
  }
  if (radius <= 0.0F) {
    // A zero-radius capsule is a segment: project its two endpoints and hull.
    std::array<Vec2, 2> endpoints{};
    std::size_t count = 0;
    if (const auto p = basis.project(a - eye)) {
      endpoints[count++] = *p;
    }
    if (const auto p = basis.project(b - eye)) {
      endpoints[count++] = *p;
    }
    return convex_hull_unwrapped(std::span<const Vec2>(endpoints.data(), count), reference_yaw);
  }
  const Polygon circle_a = project_sphere(eye, a, radius, basis, segments);
  const Polygon circle_b = project_sphere(eye, b, radius, basis, segments);
  std::vector<Vec2> points;
  points.reserve(circle_a.points.size() + circle_b.points.size());
  points.insert(points.end(), circle_a.points.begin(), circle_a.points.end());
  points.insert(points.end(), circle_b.points.begin(), circle_b.points.end());
  if (points.size() < 3) {
    return Polygon{};
  }
  return convex_hull_unwrapped(points, reference_yaw);
}

}  // namespace cs2::aim
