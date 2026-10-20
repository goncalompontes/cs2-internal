#pragma once

// The ESP's pure screen-space geometry.
//
// Every function here is a pure function of its arguments: no engine state, no
// globals, no allocation that outlives the call. The ESP's maths cannot be
// checked in a live game, so it is kept in this dependency-free header where
// the host test (`payload/features/tests/test_esp_geometry.cpp`) can reach it.
//
// The screen space is the one the projection produces: the origin is the
// top-left corner, x grows to the right and **y grows downwards**.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cs2::features::esp_geometry {

// Source 2's world unit is an inch: a standing player is 72 units (1.83 m)
// tall, so this is how many world units make a metre.
inline constexpr float k_units_per_metre = 39.3701F;

// The distance at which the distance scale reaches its floor (40 m).
inline constexpr float k_distance_scale_reference_m = 40.0F;

// The radar's radius, in metres.
inline constexpr float k_radar_radius_m = 50.0F;

// The number of segments a circle is built from (a `filled_poly` of this many
// points is the wire's circle primitive).
inline constexpr int k_circle_segments = 24;

// The clearance between an off-screen arrow and the viewport edge, in pixels.
inline constexpr float k_offscreen_margin_px = 24.0F;

[[nodiscard]] constexpr auto degrees_to_radians(float degrees) noexcept -> float {
  return degrees * 0.017453292519943295F;
}

[[nodiscard]] constexpr auto units_to_metres(float units) noexcept -> float {
  return units / k_units_per_metre;
}

[[nodiscard]] constexpr auto metres_to_units(float metres) noexcept -> float {
  return metres * k_units_per_metre;
}

// One straight segment, in screen space.
struct Segment {
  float x0 = 0.0F;
  float y0 = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
};

// --- The box ----------------------------------------------------------------

// The box's line width in pixels. The wire carries 0..20, which maps to
// 0.5..5.0 px (`k_esp_box_thickness_max`).
[[nodiscard]] constexpr auto box_thickness_px(std::uint8_t raw) noexcept -> float {
  return 0.5F + static_cast<float>(raw) * 0.225F;
}

// The box's scale factor at `distance_m` metres.
//
// A linear ramp from 1.0 at the muzzle to `max_scale` at the reference
// distance, clamped at both ends: the scale never leaves the interval between
// 1.0 and `max_scale`, so it never drops below the floor the menu promises. A
// `max_scale` above 1.0 therefore *enlarges* the box with distance instead of
// shrinking it.
[[nodiscard]] inline auto distance_scale(float distance_m, float max_scale,
                                         float reference_m = k_distance_scale_reference_m) noexcept
    -> float {
  if (reference_m <= 0.0F) {
    return 1.0F;
  }
  const float t = std::clamp(distance_m / reference_m, 0.0F, 1.0F);
  return 1.0F + (max_scale - 1.0F) * t;
}

// The eight corner brackets of a cornered box: each side is drawn only for
// its first and last `fraction` (a quarter by default), so the box reads as
// four L-shaped corners rather than a full rectangle.
[[nodiscard]] inline auto box_corner_segments(float x0, float y0, float x1, float y1,
                                              float fraction = 0.25F) -> std::vector<Segment> {
  std::vector<Segment> segments;
  segments.reserve(8);
  const float part = std::clamp(fraction, 0.0F, 0.5F);
  const float width = (x1 - x0) * part;
  const float height = (y1 - y0) * part;
  segments.push_back(Segment{x0, y0, x0 + width, y0});   // top, left half
  segments.push_back(Segment{x0, y0, x0, y0 + height});  // left, top half
  segments.push_back(Segment{x1 - width, y0, x1, y0});   // top, right half
  segments.push_back(Segment{x1, y0, x1, y0 + height});  // right, top half
  segments.push_back(Segment{x1, y1 - height, x1, y1});  // right, bottom half
  segments.push_back(Segment{x1 - width, y1, x1, y1});   // bottom, right half
  segments.push_back(Segment{x0, y1 - height, x0, y1});  // left, bottom half
  segments.push_back(Segment{x0 + width, y1, x0, y1});   // bottom, left half
  return segments;
}

// --- The off-screen arrow ---------------------------------------------------

// A screen-space direction (y grows downwards, as on the screen).
struct Direction {
  float x = 0.0F;
  float y = 0.0F;
};

// The direction, in screen space, from the camera towards a world offset.
//
// Source 2's yaw 0 faces +X and the yaw grows towards +Y, so the view's
// forward is (cos yaw, sin yaw). The view's *right* is (sin yaw, -cos yaw):
// +Y is to the player's left, so at yaw 0 the right is -Y. A target behind
// the camera therefore points *down* the screen, which is the convention
// every off-screen indicator uses.
[[nodiscard]] inline auto screen_direction(float dx, float dy, float yaw_degrees) noexcept
    -> Direction {
  const float yaw = degrees_to_radians(yaw_degrees);
  const float forward = dx * std::cos(yaw) + dy * std::sin(yaw);
  const float right = dx * std::sin(yaw) - dy * std::cos(yaw);
  return Direction{.x = right, .y = -forward};
}

// Where an off-screen arrow goes.
struct ArrowPlacement {
  float x = 0.0F;  // the arrow's centre, on the viewport edge
  float y = 0.0F;
  float angle = 0.0F;  // the direction the apex points, in radians
  bool valid = false;  // false when there is no direction to point in
};

// Place the arrow on the viewport edge, along `direction`, keeping `margin`
// pixels of clearance from the border.
[[nodiscard]] inline auto place_arrow(Direction direction, float width, float height,
                                      float margin = k_offscreen_margin_px) noexcept
    -> ArrowPlacement {
  ArrowPlacement placement;
  const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
  if (length <= 1e-4F || width <= 0.0F || height <= 0.0F) {
    return placement;
  }
  const float dx = direction.x / length;
  const float dy = direction.y / length;
  const float cx = width * 0.5F;
  const float cy = height * 0.5F;
  // The distance from the centre to the border along the direction; the
  // smaller of the two keeps the arrow inside the margin on both axes.
  //
  // The margin is clamped at zero: a viewport narrower than twice the
  // margin has no room for it, and the unclamped distance would be
  // negative — which puts the arrow on the **wrong** side of the screen (a
  // target to the left would be drawn to the right of the centre).
  const float tx = (std::abs)(dx) > 1e-4F ? (std::max)(cx - margin, 0.0F) / (std::abs)(dx) : 1e9F;
  const float ty = (std::abs)(dy) > 1e-4F ? (std::max)(cy - margin, 0.0F) / (std::abs)(dy) : 1e9F;
  const float t = (std::min)(tx, ty);
  placement.x = cx + dx * t;
  placement.y = cy + dy * t;
  placement.angle = std::atan2(dy, dx);
  placement.valid = true;
  return placement;
}

// The arrow's triangle: the apex leads along the placement's angle, the base
// trails behind it.
[[nodiscard]] inline auto arrow_triangle(const ArrowPlacement& placement, float size)
    -> std::vector<std::pair<float, float>> {
  std::vector<std::pair<float, float>> points;
  points.reserve(3);
  const float ax = std::cos(placement.angle);
  const float ay = std::sin(placement.angle);
  points.emplace_back(placement.x + ax * size, placement.y + ay * size);
  points.emplace_back(placement.x - ax * size * 0.5F - ay * size * 0.6F,
                      placement.y - ay * size * 0.5F + ax * size * 0.6F);
  points.emplace_back(placement.x - ax * size * 0.5F + ay * size * 0.6F,
                      placement.y - ay * size * 0.5F - ax * size * 0.6F);
  return points;
}

// --- The radar --------------------------------------------------------------

// A point in the radar's screen space.
struct RadarPoint {
  float x = 0.0F;
  float y = 0.0F;
};

// Map a world offset (in metres) to the radar's screen space, with the
// viewer's own direction pointing up.
[[nodiscard]] inline auto radar_offset(float dx_m, float dy_m, float yaw_degrees, float radius_px,
                                       float radius_m = k_radar_radius_m) noexcept -> RadarPoint {
  if (radius_m <= 0.0F) {
    return RadarPoint{};
  }
  const float yaw = degrees_to_radians(yaw_degrees);
  const float forward = dx_m * std::cos(yaw) + dy_m * std::sin(yaw);
  const float right = dx_m * std::sin(yaw) - dy_m * std::cos(yaw);
  const float scale = radius_px / radius_m;
  return RadarPoint{.x = right * scale, .y = -forward * scale};
}

// --- Primitives -------------------------------------------------------------

// A circle as a filled polygon of `segments` points. The wire has no circle
// primitive, so a circle is a `filled_poly` (ADR-0016).
[[nodiscard]] inline auto circle_points(float cx, float cy, float radius,
                                        int segments = k_circle_segments)
    -> std::vector<std::pair<float, float>> {
  std::vector<std::pair<float, float>> points;
  const int count = (std::max)(segments, 3);
  points.reserve(static_cast<std::size_t>(count));
  constexpr float k_tau = 6.28318530717958648F;
  for (int i = 0; i < count; ++i) {
    const float theta = k_tau * static_cast<float>(i) / static_cast<float>(count);
    points.emplace_back(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
  }
  return points;
}

// A diamond (a square rotated 45 degrees) as a filled polygon — the marker
// the world ESP draws for a grenade or a dropped weapon.
[[nodiscard]] inline auto diamond_points(float cx, float cy, float radius)
    -> std::vector<std::pair<float, float>> {
  return {{cx, cy - radius}, {cx + radius, cy}, {cx, cy + radius}, {cx - radius, cy}};
}

}  // namespace cs2::features::esp_geometry
