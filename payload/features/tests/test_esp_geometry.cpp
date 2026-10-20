// Host regression test for the ESP's pure geometry and the hitbox-derived
// skeleton chain.
//
// The ESP runs on the render thread inside a live game, so its maths cannot
// be checked there. Everything exercised here is a pure function in a
// dependency-free header, so the host build can prove it.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "cs2/features/esp_geometry.hpp"
#include "cs2/features/esp_skeleton.hpp"

namespace {

namespace geometry = cs2::features::esp_geometry;
namespace skeleton = cs2::features::esp_skeleton;

auto close(float value, float expected, float epsilon = 1e-4F) -> bool {
  return std::fabs(value - expected) <= epsilon;
}

void test_box_thickness() {
  // The wire's 0..20 maps to 0.5..5.0 px.
  assert(close(geometry::box_thickness_px(0), 0.5F));
  assert(close(geometry::box_thickness_px(20), 5.0F));
  assert(close(geometry::box_thickness_px(4), 1.4F));
  // The mapping is monotonic.
  for (std::uint8_t i = 1; i <= 20; ++i) {
    assert(geometry::box_thickness_px(i) > geometry::box_thickness_px(i - 1));
  }
}

void test_distance_scale() {
  // 1.0 at the muzzle, the floor at the reference distance (40 m).
  assert(close(geometry::distance_scale(0.0F, 0.5F), 1.0F));
  assert(close(geometry::distance_scale(20.0F, 0.5F), 0.75F));
  assert(close(geometry::distance_scale(40.0F, 0.5F), 0.5F));
  // The scale never drops below the configured floor.
  assert(close(geometry::distance_scale(400.0F, 0.5F), 0.5F));
  assert(close(geometry::distance_scale(400.0F, 0.25F), 0.25F));
  // A floor of 1.0 leaves the box alone.
  assert(close(geometry::distance_scale(120.0F, 1.0F), 1.0F));
  // A floor above 1.0 enlarges the box instead of shrinking it, and the
  // scale stays inside the interval between 1.0 and the floor.
  assert(close(geometry::distance_scale(0.0F, 2.0F), 1.0F));
  assert(close(geometry::distance_scale(40.0F, 2.0F), 2.0F));
  assert(geometry::distance_scale(10.0F, 2.0F) >= 1.0F);
  assert(geometry::distance_scale(10.0F, 2.0F) <= 2.0F);
  // A zero reference distance disables the ramp.
  assert(close(geometry::distance_scale(50.0F, 0.5F, 0.0F), 1.0F));
}

void test_corner_segments() {
  const std::vector<geometry::Segment> segments =
      geometry::box_corner_segments(0.0F, 0.0F, 100.0F, 100.0F);
  assert(segments.size() == 8);
  for (const geometry::Segment& segment : segments) {
    // Every segment is a quarter of a side.
    const float length = std::sqrt((segment.x1 - segment.x0) * (segment.x1 - segment.x0) +
                                   (segment.y1 - segment.y0) * (segment.y1 - segment.y0));
    assert(close(length, 25.0F));
  }
  // The top-left corner carries two of them.
  int at_origin = 0;
  for (const geometry::Segment& segment : segments) {
    if ((close(segment.x0, 0.0F) && close(segment.y0, 0.0F)) ||
        (close(segment.x1, 0.0F) && close(segment.y1, 0.0F))) {
      ++at_origin;
    }
  }
  assert(at_origin == 2);
}

void test_screen_direction() {
  // Yaw 0 faces +X; +Y is to the player's left.
  const geometry::Direction ahead = geometry::screen_direction(100.0F, 0.0F, 0.0F);
  assert(close(ahead.x, 0.0F));
  assert(close(ahead.y, -100.0F));  // up the screen
  const geometry::Direction right = geometry::screen_direction(0.0F, -100.0F, 0.0F);
  assert(close(right.x, 100.0F));  // -Y is to the right
  assert(close(right.y, 0.0F));
  const geometry::Direction behind = geometry::screen_direction(-100.0F, 0.0F, 0.0F);
  assert(close(behind.y, 100.0F));  // down the screen
  // At yaw 90 the player faces +Y, so +X is to their right.
  const geometry::Direction turned = geometry::screen_direction(100.0F, 0.0F, 90.0F);
  assert(close(turned.x, 100.0F));
  assert(close(turned.y, 0.0F));
}

void test_place_arrow() {
  constexpr float k_width = 1920.0F;
  constexpr float k_height = 1080.0F;
  constexpr float k_margin = 24.0F;

  // A target to the right: the arrow sits on the right edge.
  const geometry::ArrowPlacement right =
      geometry::place_arrow(geometry::Direction{.x = 1.0F, .y = 0.0F}, k_width, k_height);
  assert(right.valid);
  assert(close(right.x, k_width - k_margin));
  assert(close(right.y, k_height * 0.5F));
  assert(close(right.angle, 0.0F));

  // A target above: the arrow sits on the top edge.
  const geometry::ArrowPlacement up =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = -1.0F}, k_width, k_height);
  assert(up.valid);
  assert(close(up.x, k_width * 0.5F));
  assert(close(up.y, k_margin));

  // A target below: the arrow sits on the bottom edge.
  const geometry::ArrowPlacement down =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = 1.0F}, k_width, k_height);
  assert(down.valid);
  assert(close(down.y, k_height - k_margin));

  // A diagonal target keeps the margin on both axes.
  const geometry::ArrowPlacement diagonal =
      geometry::place_arrow(geometry::Direction{.x = 1.0F, .y = 1.0F}, k_width, k_height);
  assert(diagonal.valid);
  assert(diagonal.x <= k_width - k_margin + 1e-3F);
  assert(diagonal.y <= k_height - k_margin + 1e-3F);

  // A degenerate direction has no placement.
  const geometry::ArrowPlacement none =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = 0.0F}, k_width, k_height);
  assert(!none.valid);
}

void test_arrow_triangle() {
  const geometry::ArrowPlacement placement{.x = 100.0F, .y = 50.0F, .angle = 0.0F, .valid = true};
  const std::vector<std::pair<float, float>> triangle = geometry::arrow_triangle(placement, 10.0F);
  assert(triangle.size() == 3);
  // The apex leads along the angle.
  assert(close(triangle[0].first, 110.0F));
  assert(close(triangle[0].second, 50.0F));
  // The base trails behind it.
  assert(triangle[1].first < placement.x);
  assert(triangle[2].first < placement.x);
}

void test_radar_offset() {
  // The radar's radius is 50 m and 80 px, so 10 m is 16 px.
  constexpr float k_radius_px = 80.0F;
  const geometry::RadarPoint ahead = geometry::radar_offset(10.0F, 0.0F, 0.0F, k_radius_px);
  assert(close(ahead.x, 0.0F));
  assert(close(ahead.y, -16.0F));  // the view direction is up
  // -Y is to the player's right.
  const geometry::RadarPoint right = geometry::radar_offset(0.0F, -10.0F, 0.0F, k_radius_px);
  assert(close(right.x, 16.0F));
  assert(close(right.y, 0.0F));
  // A target at the radar's radius lands on the ring.
  const geometry::RadarPoint edge =
      geometry::radar_offset(geometry::k_radar_radius_m, 0.0F, 0.0F, k_radius_px);
  assert(close(edge.y, -k_radius_px));
}

void test_circle_points() {
  const std::vector<std::pair<float, float>> points = geometry::circle_points(10.0F, 20.0F, 5.0F);
  assert(points.size() == static_cast<std::size_t>(geometry::k_circle_segments));
  for (const std::pair<float, float>& point : points) {
    const float dx = point.first - 10.0F;
    const float dy = point.second - 20.0F;
    assert(close(std::sqrt(dx * dx + dy * dy), 5.0F));
  }
  const std::vector<std::pair<float, float>> diamond = geometry::diamond_points(0.0F, 0.0F, 4.0F);
  assert(diamond.size() == 4);
}

// A player model: the head, the neck, two chest capsules, the stomach, the
// pelvis and the limbs.
auto player_points() -> std::vector<skeleton::BonePoint> {
  return {
      {skeleton::k_hitgroup_head, 0.0F, 0.0F, 64.0F},
      {skeleton::k_hitgroup_generic, 0.0F, 0.0F, 60.0F},  // the neck
      {skeleton::k_hitgroup_chest, 0.0F, 0.0F, 50.0F},    // spine_2
      {skeleton::k_hitgroup_chest, 0.0F, 0.0F, 45.0F},    // spine_1
      {skeleton::k_hitgroup_stomach, 0.0F, 0.0F, 40.0F},
      {skeleton::k_hitgroup_generic, 0.0F, 0.0F, 36.0F},     // the pelvis
      {skeleton::k_hitgroup_left_arm, -8.0F, 0.0F, 50.0F},   // upper arm
      {skeleton::k_hitgroup_left_arm, -16.0F, 0.0F, 46.0F},  // forearm
      {skeleton::k_hitgroup_left_arm, -22.0F, 0.0F, 42.0F},  // hand
      {skeleton::k_hitgroup_right_arm, 8.0F, 0.0F, 50.0F},
      {skeleton::k_hitgroup_right_arm, 16.0F, 0.0F, 46.0F},
      {skeleton::k_hitgroup_right_arm, 22.0F, 0.0F, 42.0F},
      {skeleton::k_hitgroup_left_leg, -5.0F, 0.0F, 30.0F},  // thigh
      {skeleton::k_hitgroup_left_leg, -5.0F, 0.0F, 16.0F},  // calf
      {skeleton::k_hitgroup_left_leg, -5.0F, 0.0F, 2.0F},   // foot
      {skeleton::k_hitgroup_right_leg, 5.0F, 0.0F, 30.0F},
      {skeleton::k_hitgroup_right_leg, 5.0F, 0.0F, 16.0F},
      {skeleton::k_hitgroup_right_leg, 5.0F, 0.0F, 2.0F},
  };
}

auto has_segment(const std::vector<skeleton::Segment>& chain, float ax, float ay, float az,
                 float bx, float by, float bz) -> bool {
  for (const skeleton::Segment& segment : chain) {
    if (close(segment.ax, ax) && close(segment.ay, ay) && close(segment.az, az) &&
        close(segment.bx, bx) && close(segment.by, by) && close(segment.bz, bz)) {
      return true;
    }
    // The chain is undirected.
    if (close(segment.ax, bx) && close(segment.ay, by) && close(segment.az, bz) &&
        close(segment.bx, ax) && close(segment.by, ay) && close(segment.bz, az)) {
      return true;
    }
  }
  return false;
}

void test_skeleton_chain() {
  const std::vector<skeleton::Segment> chain = skeleton::build_chain(player_points());
  // The torso: head, neck, two chest capsules, the stomach and the pelvis.
  assert(chain.size() >= 17);
  assert(has_segment(chain, 0.0F, 0.0F, 64.0F, 0.0F, 0.0F, 60.0F));  // head -> neck
  assert(has_segment(chain, 0.0F, 0.0F, 60.0F, 0.0F, 0.0F, 50.0F));  // neck -> chest
  assert(has_segment(chain, 0.0F, 0.0F, 50.0F, 0.0F, 0.0F, 45.0F));  // chest -> chest
  assert(has_segment(chain, 0.0F, 0.0F, 45.0F, 0.0F, 0.0F, 40.0F));  // chest -> stomach
  assert(has_segment(chain, 0.0F, 0.0F, 40.0F, 0.0F, 0.0F, 36.0F));  // stomach -> pelvis
  // The arms hang off the topmost chest capsule, near to far.
  assert(has_segment(chain, 0.0F, 0.0F, 50.0F, -8.0F, 0.0F, 50.0F));
  assert(has_segment(chain, -8.0F, 0.0F, 50.0F, -16.0F, 0.0F, 46.0F));
  assert(has_segment(chain, -16.0F, 0.0F, 46.0F, -22.0F, 0.0F, 42.0F));
  // The legs hang off the pelvis.
  assert(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F));
  assert(has_segment(chain, -5.0F, 0.0F, 30.0F, -5.0F, 0.0F, 16.0F));
  assert(has_segment(chain, -5.0F, 0.0F, 16.0F, -5.0F, 0.0F, 2.0F));
}

void test_skeleton_missing_capsule() {
  // The stomach is missing: the chain joins its neighbours instead of
  // breaking.
  std::vector<skeleton::BonePoint> points = player_points();
  points.erase(std::remove_if(points.begin(), points.end(),
                              [](const skeleton::BonePoint& point) {
                                return point.group == skeleton::k_hitgroup_stomach;
                              }),
               points.end());
  const std::vector<skeleton::Segment> chain = skeleton::build_chain(points);
  assert(has_segment(chain, 0.0F, 0.0F, 45.0F, 0.0F, 0.0F, 36.0F));
  // The arms and the legs are still there.
  assert(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F));
}

void test_skeleton_empty() {
  const std::vector<skeleton::Segment> chain = skeleton::build_chain({});
  assert(chain.empty());
}

}  // namespace

auto main() -> int {
  test_box_thickness();
  test_distance_scale();
  test_corner_segments();
  test_screen_direction();
  test_place_arrow();
  test_arrow_triangle();
  test_radar_offset();
  test_circle_points();
  test_skeleton_chain();
  test_skeleton_missing_capsule();
  test_skeleton_empty();
  std::puts("features-esp-geometry: ok");
  return 0;
}
