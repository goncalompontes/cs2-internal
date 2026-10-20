// Host regression test for the ESP's pure geometry and the hitbox-derived
// skeleton chain.
//
// The ESP runs on the render thread inside a live game, so its maths cannot
// be checked there. Everything exercised here is a pure function in a
// dependency-free header, so the host build can prove it.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build. Meson's `b_ndebug` default
// is `if-release`, so `NDEBUG` is defined in the configuration
// `scripts/release.sh` builds and every `assert` would be compiled out — a
// test that only bites in the debug build is not a test.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cs2/features/esp_geometry.hpp"
#include "cs2/features/esp_skeleton.hpp"
#include "cs2/features/esp_world_class.hpp"

namespace {

namespace geometry = cs2::features::esp_geometry;
namespace skeleton = cs2::features::esp_skeleton;

// The number of failed checks. The test's exit code is the verdict, so a
// release build (where `assert` is compiled out) still reports the failure.
int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// A check whose description is built at run time (the loop cases).
void expect_named(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
  }
}

auto close(float value, float expected, float epsilon = 1e-4F) -> bool {
  return std::fabs(value - expected) <= epsilon;
}

void test_box_thickness() {
  // The wire's 0..20 maps to 0.5..5.0 px.
  expect(close(geometry::box_thickness_px(0), 0.5F), "the box thickness starts at 0.5 px");
  expect(close(geometry::box_thickness_px(20), 5.0F), "the box thickness ends at 5 px");
  expect(close(geometry::box_thickness_px(4), 1.4F), "the box thickness maps the wire's 4");
  // The mapping is monotonic.
  for (std::uint8_t i = 1; i <= 20; ++i) {
    expect_named(geometry::box_thickness_px(i) > geometry::box_thickness_px(i - 1),
                 "the box thickness is monotonic at " + std::to_string(i));
  }
}

void test_distance_scale() {
  // 1.0 at the muzzle, the floor at the reference distance (40 m).
  expect(close(geometry::distance_scale(0.0F, 0.5F), 1.0F), "the scale is 1.0 at the muzzle");
  expect(close(geometry::distance_scale(20.0F, 0.5F), 0.75F), "the scale halves over the ramp");
  expect(close(geometry::distance_scale(40.0F, 0.5F), 0.5F), "the scale reaches the floor at 40 m");
  // The scale never drops below the configured floor.
  expect(close(geometry::distance_scale(400.0F, 0.5F), 0.5F), "the scale keeps its floor");
  expect(close(geometry::distance_scale(400.0F, 0.25F), 0.25F), "the scale keeps a lower floor");
  // A floor of 1.0 leaves the box alone.
  expect(close(geometry::distance_scale(120.0F, 1.0F), 1.0F),
         "a floor of 1.0 leaves the box alone");
  // A floor above 1.0 enlarges the box instead of shrinking it, and the
  // scale stays inside the interval between 1.0 and the floor.
  expect(close(geometry::distance_scale(0.0F, 2.0F), 1.0F), "a floor above 1.0 starts at 1.0");
  expect(close(geometry::distance_scale(40.0F, 2.0F), 2.0F), "a floor above 1.0 ends at the floor");
  expect(geometry::distance_scale(10.0F, 2.0F) >= 1.0F, "the scale never drops below 1.0");
  expect(geometry::distance_scale(10.0F, 2.0F) <= 2.0F, "the scale never rises above the floor");
  // A zero reference distance disables the ramp.
  expect(close(geometry::distance_scale(50.0F, 0.5F, 0.0F), 1.0F),
         "a zero reference distance disables the ramp");
}

void test_corner_segments() {
  const std::vector<geometry::Segment> segments =
      geometry::box_corner_segments(0.0F, 0.0F, 100.0F, 100.0F);
  expect(segments.size() == 8, "a cornered box has eight segments");
  for (const geometry::Segment& segment : segments) {
    // Every segment is a quarter of a side.
    const float length = std::sqrt((segment.x1 - segment.x0) * (segment.x1 - segment.x0) +
                                   (segment.y1 - segment.y0) * (segment.y1 - segment.y0));
    expect_named(close(length, 25.0F), "a corner segment is a quarter of its side");
  }
  // The top-left corner carries two of them.
  int at_origin = 0;
  for (const geometry::Segment& segment : segments) {
    if ((close(segment.x0, 0.0F) && close(segment.y0, 0.0F)) ||
        (close(segment.x1, 0.0F) && close(segment.y1, 0.0F))) {
      ++at_origin;
    }
  }
  expect(at_origin == 2, "the top-left corner carries two segments");
}

void test_screen_direction() {
  // Yaw 0 faces +X; +Y is to the player's left.
  const geometry::Direction ahead = geometry::screen_direction(100.0F, 0.0F, 0.0F);
  expect(close(ahead.x, 0.0F), "a target ahead has no sideways offset");
  expect(close(ahead.y, -100.0F), "a target ahead is up the screen");  // up the screen
  const geometry::Direction right = geometry::screen_direction(0.0F, -100.0F, 0.0F);
  expect(close(right.x, 100.0F), "-Y is to the right");
  expect(close(right.y, 0.0F), "-Y has no forward offset");
  const geometry::Direction behind = geometry::screen_direction(-100.0F, 0.0F, 0.0F);
  expect(close(behind.y, 100.0F), "a target behind is down the screen");
  // At yaw 90 the player faces +Y, so +X is to their right.
  const geometry::Direction turned = geometry::screen_direction(100.0F, 0.0F, 90.0F);
  expect(close(turned.x, 100.0F), "at yaw 90 +X is to the right");
  expect(close(turned.y, 0.0F), "at yaw 90 there is no forward offset");
}

void test_place_arrow() {
  constexpr float k_width = 1920.0F;
  constexpr float k_height = 1080.0F;
  constexpr float k_margin = 24.0F;

  // A target to the right: the arrow sits on the right edge.
  const geometry::ArrowPlacement right =
      geometry::place_arrow(geometry::Direction{.x = 1.0F, .y = 0.0F}, k_width, k_height);
  expect(right.valid, "a rightward target has a placement");
  expect(close(right.x, k_width - k_margin), "the right arrow keeps the margin");
  expect(close(right.y, k_height * 0.5F), "the right arrow is at the centre's height");
  expect(close(right.angle, 0.0F), "the right arrow points right");

  // A target above: the arrow sits on the top edge.
  const geometry::ArrowPlacement up =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = -1.0F}, k_width, k_height);
  expect(up.valid, "an upward target has a placement");
  expect(close(up.x, k_width * 0.5F), "the upper arrow is at the centre's width");
  expect(close(up.y, k_margin), "the upper arrow keeps the margin");

  // A target below: the arrow sits on the bottom edge.
  const geometry::ArrowPlacement down =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = 1.0F}, k_width, k_height);
  expect(down.valid, "a downward target has a placement");
  expect(close(down.y, k_height - k_margin), "the lower arrow keeps the margin");

  // A diagonal target keeps the margin on both axes.
  const geometry::ArrowPlacement diagonal =
      geometry::place_arrow(geometry::Direction{.x = 1.0F, .y = 1.0F}, k_width, k_height);
  expect(diagonal.valid, "a diagonal target has a placement");
  expect(diagonal.x <= k_width - k_margin + 1e-3F, "the diagonal arrow keeps the right margin");
  expect(diagonal.y <= k_height - k_margin + 1e-3F, "the diagonal arrow keeps the bottom margin");

  // A degenerate direction has no placement.
  const geometry::ArrowPlacement none =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = 0.0F}, k_width, k_height);
  expect(!none.valid, "a degenerate direction has no placement");
}

// The edge clamp must not invert on a viewport that is smaller than twice
// the margin: the distance to the border is negative there, so an
// unclamped division puts the arrow on the **wrong** side of the screen.
void test_place_arrow_small_viewport() {
  constexpr float k_width = 40.0F;
  constexpr float k_height = 40.0F;

  const geometry::ArrowPlacement left =
      geometry::place_arrow(geometry::Direction{.x = -1.0F, .y = 0.0F}, k_width, k_height);
  expect(left.valid, "a leftward target has a placement on a small viewport");
  expect(left.x <= k_width * 0.5F, "the left arrow is not to the right of the centre");
  expect(left.x >= 0.0F, "the left arrow stays on the viewport");
  expect(close(left.y, k_height * 0.5F), "the left arrow is at the centre's height");

  const geometry::ArrowPlacement right =
      geometry::place_arrow(geometry::Direction{.x = 1.0F, .y = 0.0F}, k_width, k_height);
  expect(right.valid, "a rightward target has a placement on a small viewport");
  expect(right.x >= k_width * 0.5F, "the right arrow is not to the left of the centre");
  expect(right.x <= k_width, "the right arrow stays on the viewport");

  const geometry::ArrowPlacement up =
      geometry::place_arrow(geometry::Direction{.x = 0.0F, .y = -1.0F}, k_width, k_height);
  expect(up.valid, "an upward target has a placement on a small viewport");
  expect(up.y <= k_height * 0.5F, "the upper arrow is not below the centre");
  expect(up.y >= 0.0F, "the upper arrow stays on the viewport");
}

void test_arrow_triangle() {
  const geometry::ArrowPlacement placement{.x = 100.0F, .y = 50.0F, .angle = 0.0F, .valid = true};
  const std::vector<std::pair<float, float>> triangle = geometry::arrow_triangle(placement, 10.0F);
  expect(triangle.size() == 3, "the arrow is a triangle");
  // The apex leads along the angle.
  expect(close(triangle[0].first, 110.0F), "the apex leads along the angle");
  expect(close(triangle[0].second, 50.0F), "the apex keeps the placement's height");
  // The base trails behind it.
  expect(triangle[1].first < placement.x, "the base trails behind the apex");
  expect(triangle[2].first < placement.x, "the base trails behind the apex");
}

void test_radar_offset() {
  // The radar's radius is 50 m and 80 px, so 10 m is 16 px.
  constexpr float k_radius_px = 80.0F;
  const geometry::RadarPoint ahead = geometry::radar_offset(10.0F, 0.0F, 0.0F, k_radius_px);
  expect(close(ahead.x, 0.0F), "a target ahead has no sideways offset");
  expect(close(ahead.y, -16.0F), "a target ahead is up the radar");  // the view direction is up
  // -Y is to the player's right.
  const geometry::RadarPoint right = geometry::radar_offset(0.0F, -10.0F, 0.0F, k_radius_px);
  expect(close(right.x, 16.0F), "-Y is to the right on the radar");
  expect(close(right.y, 0.0F), "-Y has no forward offset on the radar");
  // A target at the radar's radius lands on the ring.
  const geometry::RadarPoint edge =
      geometry::radar_offset(geometry::k_radar_radius_m, 0.0F, 0.0F, k_radius_px);
  expect(close(edge.y, -k_radius_px), "a target at the radius lands on the ring");
}

// The radar is a disc, not the square around it.
void test_radar_is_a_disc() {
  constexpr float k_radius_px = 80.0F;
  // A point at 45 degrees and exactly the radius is on the ring: the
  // coordinate-wise test drew the square around the circle and dropped it.
  const float diagonal = k_radius_px * 0.70710678F;
  expect(geometry::inside_radar(geometry::RadarPoint{.x = diagonal, .y = diagonal}, k_radius_px),
         "a point on the ring at 45 degrees is inside the radar");
  expect(!geometry::inside_radar(geometry::RadarPoint{.x = diagonal + 1.0F, .y = diagonal},
                                 k_radius_px),
         "a point past the ring at 45 degrees is outside the radar");
  expect(geometry::inside_radar(geometry::RadarPoint{.x = 0.0F, .y = -k_radius_px}, k_radius_px),
         "a point on the ring is inside the radar");
  expect(!geometry::inside_radar(geometry::RadarPoint{.x = k_radius_px + 1.0F, .y = 0.0F},
                                 k_radius_px),
         "a point past the ring is outside the radar");
}

// The off-screen arrow wins over the degenerate box: a target outside the
// viewport must get its arrow even when its box is tiny.
void test_player_box_outcome() {
  using geometry::PlayerBoxOutcome;
  expect(geometry::player_box_outcome(true, 40.0F, 80.0F) == PlayerBoxOutcome::draw,
         "an on-screen box is drawn");
  expect(geometry::player_box_outcome(false, 40.0F, 80.0F) == PlayerBoxOutcome::arrow,
         "an off-screen box gets the arrow");
  expect(geometry::player_box_outcome(true, 0.5F, 1.0F) == PlayerBoxOutcome::degenerate,
         "a tiny on-screen box is dropped");
  expect(geometry::player_box_outcome(false, 0.5F, 1.0F) == PlayerBoxOutcome::arrow,
         "a tiny off-screen box still gets the arrow");
  expect(geometry::player_box_outcome(true, 1.0F, 3.0F) == PlayerBoxOutcome::draw,
         "the smallest drawable box is drawn");
}

// The world walk's class prefixes: the grenades the list did not name get a
// marker, and the deployed incendiary is a grenade, not a dropped weapon.
void test_world_classes() {
  namespace world = cs2::features::esp_world;
  expect(world::classify("smokegr") == world::WorldClass::smoke, "the smoke grenade is classified");
  expect(world::classify("flashba") == world::WorldClass::flash, "the flashbang is classified");
  expect(world::classify("molotov") == world::WorldClass::molotov, "the molotov is classified");
  expect(world::classify("incendi") == world::WorldClass::incendiary,
         "the incendiary is classified");
  expect(world::classify("hegrena") == world::WorldClass::he, "the HE grenade is classified");
  expect(world::classify("decoy_p") == world::WorldClass::decoy, "the decoy is classified");
  expect(world::classify("sensorgr") == world::WorldClass::sensor,
         "the sensor grenade is classified");
  expect(world::classify("bumpmine") == world::WorldClass::bumpmine, "the bump mine is classified");
  expect(world::classify("planted") == world::WorldClass::c4, "the planted bomb is classified");
  expect(world::classify("c4") == world::WorldClass::c4, "the C4 is classified");
  expect(world::classify("weapon_") == world::WorldClass::weapon, "a dropped weapon is classified");
  expect(world::classify("player") == world::WorldClass::none,
         "an unknown class is not classified");
  // The deployed incendiary: "weapon_" plus the rest of the name.
  expect(world::is_deployed_incendiary("taggrenad"), "the deployed incendiary is recognised");
  expect(!world::is_deployed_incendiary("ak47"), "a dropped rifle is not an incendiary");
  expect(!world::is_deployed_incendiary("deagle"), "a dropped pistol is not an incendiary");
}

void test_circle_points() {
  const std::vector<std::pair<float, float>> points = geometry::circle_points(10.0F, 20.0F, 5.0F);
  expect(points.size() == static_cast<std::size_t>(geometry::k_circle_segments),
         "the circle has the segment count");
  for (const std::pair<float, float>& point : points) {
    const float dx = point.first - 10.0F;
    const float dy = point.second - 20.0F;
    expect_named(close(std::sqrt(dx * dx + dy * dy), 5.0F), "a circle point is on the circle");
  }
  const std::vector<std::pair<float, float>> diamond = geometry::diamond_points(0.0F, 0.0F, 4.0F);
  expect(diamond.size() == 4, "the diamond has four points");
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
  expect(chain.size() >= 17, "the full model has at least 17 segments");
  expect(has_segment(chain, 0.0F, 0.0F, 64.0F, 0.0F, 0.0F, 60.0F), "the head joins the neck");
  expect(has_segment(chain, 0.0F, 0.0F, 60.0F, 0.0F, 0.0F, 50.0F), "the neck joins the chest");
  expect(has_segment(chain, 0.0F, 0.0F, 50.0F, 0.0F, 0.0F, 45.0F), "the chest capsules are joined");
  expect(has_segment(chain, 0.0F, 0.0F, 45.0F, 0.0F, 0.0F, 40.0F), "the chest joins the stomach");
  expect(has_segment(chain, 0.0F, 0.0F, 40.0F, 0.0F, 0.0F, 36.0F), "the stomach joins the pelvis");
  // The arms hang off the topmost chest capsule, near to far.
  expect(has_segment(chain, 0.0F, 0.0F, 50.0F, -8.0F, 0.0F, 50.0F),
         "the left arm hangs off the chest");
  expect(has_segment(chain, -8.0F, 0.0F, 50.0F, -16.0F, 0.0F, 46.0F), "the left forearm follows");
  expect(has_segment(chain, -16.0F, 0.0F, 46.0F, -22.0F, 0.0F, 42.0F), "the left hand follows");
  // The legs hang off the pelvis.
  expect(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F),
         "the left leg hangs off the pelvis");
  expect(has_segment(chain, -5.0F, 0.0F, 30.0F, -5.0F, 0.0F, 16.0F), "the left calf follows");
  expect(has_segment(chain, -5.0F, 0.0F, 16.0F, -5.0F, 0.0F, 2.0F), "the left foot follows");
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
  expect(has_segment(chain, 0.0F, 0.0F, 45.0F, 0.0F, 0.0F, 36.0F),
         "the missing stomach joins its neighbours");
  // The arms and the legs are still there.
  expect(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F),
         "the legs survive a missing stomach");
}

// The chest capsules are missing: the neck and the pelvis are both "generic",
// so the chain must still tell them apart — otherwise the pelvis is never
// identified and **the legs vanish**.
void test_skeleton_missing_chest() {
  std::vector<skeleton::BonePoint> points = player_points();
  points.erase(std::remove_if(points.begin(), points.end(),
                              [](const skeleton::BonePoint& point) {
                                return point.group == skeleton::k_hitgroup_chest;
                              }),
               points.end());
  const std::vector<skeleton::Segment> chain = skeleton::build_chain(points);
  expect(chain.size() >= 15, "a model without the chest keeps its segments");
  expect(has_segment(chain, 0.0F, 0.0F, 40.0F, 0.0F, 0.0F, 36.0F),
         "the stomach joins the pelvis without the chest");
  expect(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F),
         "the legs are still attached without the chest");
  expect(has_segment(chain, 0.0F, 0.0F, 36.0F, 5.0F, 0.0F, 30.0F),
         "the right leg is still attached without the chest");
}

// A third "generic" capsule (a gear box) must not steal the pelvis role.
void test_skeleton_gear_capsule() {
  std::vector<skeleton::BonePoint> points = player_points();
  points.push_back({skeleton::k_hitgroup_generic, 0.0F, 0.0F, 4.0F});  // a gear box
  const std::vector<skeleton::Segment> chain = skeleton::build_chain(points);
  expect(has_segment(chain, 0.0F, 0.0F, 40.0F, 0.0F, 0.0F, 36.0F),
         "the stomach still joins the pelvis");
  expect(has_segment(chain, 0.0F, 0.0F, 36.0F, -5.0F, 0.0F, 30.0F),
         "the legs hang off the pelvis, not off the gear box");
}

void test_skeleton_empty() {
  const std::vector<skeleton::Segment> chain = skeleton::build_chain({});
  expect(chain.empty(), "an empty model has no segments");
}

}  // namespace

auto main() -> int {
  test_box_thickness();
  test_distance_scale();
  test_corner_segments();
  test_screen_direction();
  test_place_arrow();
  test_place_arrow_small_viewport();
  test_arrow_triangle();
  test_radar_offset();
  test_radar_is_a_disc();
  test_player_box_outcome();
  test_world_classes();
  test_circle_points();
  test_skeleton_chain();
  test_skeleton_missing_capsule();
  test_skeleton_missing_chest();
  test_skeleton_gear_capsule();
  test_skeleton_empty();
  if (failures != 0) {
    std::printf("features-esp-geometry: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-esp-geometry: ok");
  return 0;
}
