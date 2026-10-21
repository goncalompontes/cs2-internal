// Unit tests for the anti-aim's **decision maths** (`cs2/control/anti_aim.hpp`):
// the fake (desync) yaw, the pitch clamp, the per-command jitter and — the part
// that makes the feature correct — the **movement re-base**.
//
// The module is pure (no engine headers, no `<windows.h>`, no state), so the
// whole decision can be checked host-side. The re-base is the load-bearing
// piece: the operator's W/A/S/D must stay camera-relative while the command
// carries the fake angles, and the test pins that **exactly** — the wish the
// engine computes from the re-based axes, in the anti-aim frame, is the wish it
// would have computed from the original axes, in the camera frame.
//
// The last test is a **source** check: the camera reads the *live* view angles
// (`CCSGOInput + 0x688`) and the anti-aim writes the *command's* angles, so a
// stray write to the camera's field would make the camera follow the fake
// angles — the classic mistake this feature must not make.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "cs2/control/anti_aim.hpp"

namespace {

using cs2::control::anti_aim::Angles;
using cs2::control::anti_aim::Axes;
using cs2::control::anti_aim::MoveIntent;
using cs2::control::anti_aim::Side;
using cs2::control::anti_aim::Vec2;

constexpr float k_epsilon = 0.001F;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

auto length(Vec2 value) -> float {
  return std::sqrt(value.x * value.x + value.y * value.y);
}

// The unit vector of a wish — the direction the player walks.
auto direction(Vec2 wish) -> Vec2 {
  const float size = length(wish);
  if (size <= 0.0F) {
    return Vec2{};
  }
  return Vec2{wish.x / size, wish.y / size};
}

// --- the engine's wish model ---------------------------------------------------

// The model of `AirMove` the re-base is built on: the wish is the forward axis
// times the basis's forward plus the side axis times the basis's **left**.
void test_the_wish_model_is_the_engines_basis() {
  // The camera at yaw 0: the forward is +X and the left is +Y.
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Vec2 forward = cs2::control::anti_aim::engine_wish(camera, 1.0F, 0.0F);
  assert(nearly(forward.x, 1.0F));
  assert(nearly(forward.y, 0.0F));

  // The side axis is positive to the player's **left** (`docs/re/strafe-sign.md`).
  const Vec2 left = cs2::control::anti_aim::engine_wish(camera, 0.0F, 1.0F);
  assert(nearly(left.x, 0.0F));
  assert(nearly(left.y, 1.0F));

  // The basis turns with the yaw: at yaw 90 the forward is +Y and the left -X.
  const Angles turned{0.0F, 90.0F, 0.0F};
  const Vec2 turned_forward = cs2::control::anti_aim::engine_wish(turned, 1.0F, 0.0F);
  assert(nearly(turned_forward.x, 0.0F));
  assert(nearly(turned_forward.y, 1.0F));
  const Vec2 turned_left = cs2::control::anti_aim::engine_wish(turned, 0.0F, 1.0F);
  assert(nearly(turned_left.x, -1.0F));
  assert(nearly(turned_left.y, 0.0F));

  // The pitch tips the forward axis out of the walk's plane: at 90 degrees the
  // forward contributes nothing horizontally (this is the case the down-pitch
  // inversion is about).
  const Angles down{90.0F, 0.0F, 0.0F};
  const Vec2 tipped = cs2::control::anti_aim::engine_wish(down, 1.0F, 0.0F);
  assert(std::fabs(tipped.x) < 0.001F);
  assert(std::fabs(tipped.y) < 0.001F);
}

// --- the re-base ----------------------------------------------------------------

// With no anti-aim (the anti-aim's angles are the camera's) the movement must
// be **untouched**: the axes come back as the player's own intent.
void test_the_rebase_is_the_identity_without_an_anti_aim() {
  const Angles camera{15.0F, 42.0F, 0.0F};
  const Axes forward = cs2::control::anti_aim::rebase_axes(camera, camera, MoveIntent{1.0F, 0.0F});
  assert(nearly(forward.forward, 1.0F));
  assert(nearly(forward.left, 0.0F));

  const Axes back = cs2::control::anti_aim::rebase_axes(camera, camera, MoveIntent{-1.0F, 0.0F});
  assert(nearly(back.forward, -1.0F));
  assert(nearly(back.left, 0.0F));

  const Axes diagonal = cs2::control::anti_aim::rebase_axes(camera, camera, MoveIntent{1.0F, 1.0F});
  assert(nearly(diagonal.forward, 1.0F));
  assert(nearly(diagonal.left, 1.0F));

  const Angles flat{0.0F, 0.0F, 0.0F};
  const Axes still = cs2::control::anti_aim::rebase_axes(flat, flat, MoveIntent{0.0F, 0.0F});
  assert(nearly(still.forward, 0.0F));
  assert(nearly(still.left, 0.0F));
}

// A hand-computed case: the classic 180-degree desync. The anti-aim's forward
// axis points the other way, so the re-based forward axis must be **-1** —
// that is what keeps the operator's `W` walking where the camera looks.
void test_the_rebase_compensates_a_desync() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Angles anti_aim{0.0F, 180.0F, 0.0F};
  const Axes axes = cs2::control::anti_aim::rebase_axes(camera, anti_aim, MoveIntent{1.0F, 0.0F});
  assert(nearly(axes.forward, -1.0F));
  assert(nearly(axes.left, 0.0F));

  // The wish the engine computes in the anti-aim frame is the camera's wish.
  const Vec2 before = cs2::control::anti_aim::engine_wish(camera, 1.0F, 0.0F);
  const Vec2 after = cs2::control::anti_aim::engine_wish(anti_aim, axes.forward, axes.left);
  assert(nearly(before.x, after.x));
  assert(nearly(before.y, after.y));
}

// The invariant, over a spread of camera angles, anti-aim angles and
// intents: the wish the engine computes from the re-based axes, in the
// anti-aim frame, points the **same way** as the wish it would have computed
// from the original axes, in the camera frame. A re-base that only looks
// plausible would walk the operator at an angle; this pins that it does not.
void test_the_rebase_preserves_the_world_space_wish() {
  struct Case {
    float camera_pitch;
    float camera_yaw;
    float anti_aim_pitch;
    float anti_aim_yaw;
    float forward;
    float left;
  };
  const Case cases[] = {
      {0.0F, 0.0F, 0.0F, 90.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, 180.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, -135.0F, 1.0F, 0.0F},
      {0.0F, 37.0F, 0.0F, 37.0F + 90.0F, 1.0F, 0.0F},
      {0.0F, 37.0F, 0.0F, 37.0F - 135.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, 45.0F, 1.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, 1.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, -1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, 0.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, 0.0F, -1.0F},
      {12.0F, 15.0F, 0.0F, 15.0F + 180.0F, 1.0F, 0.0F},
      {-20.0F, 200.0F, -20.0F, 200.0F + 90.0F, 1.0F, 1.0F},
      {0.0F, 0.0F, 10.0F, 0.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, 10.0F, 75.0F, 1.0F, 0.0F},
      {0.0F, 0.0F, -10.0F, -75.0F, 1.0F, 0.0F},
  };
  for (const Case& item : cases) {
    const Angles camera{item.camera_pitch, item.camera_yaw, 0.0F};
    const Angles anti_aim{item.anti_aim_pitch, item.anti_aim_yaw, 0.0F};
    const MoveIntent intent{item.forward, item.left};
    const Axes axes = cs2::control::anti_aim::rebase_axes(camera, anti_aim, intent);

    const Vec2 before = cs2::control::anti_aim::engine_wish(camera, intent.forward, intent.left);
    const Vec2 after = cs2::control::anti_aim::engine_wish(anti_aim, axes.forward, axes.left);
    assert(length(before) > 0.0F);
    const Vec2 want = direction(before);
    const Vec2 got = direction(after);
    assert(nearly(want.x, got.x));
    assert(nearly(want.y, got.y));
    // The re-base never makes the player walk *faster* than they would have.
    assert(length(after) <= length(before) + k_epsilon);
  }
}

// The down-pitch inversion: with the camera past 89 degrees the operator is
// looking straight down, the camera's forward axis points into the ground and
// its sign flips at 90 degrees — the community's rule inverts the forward axis
// there so the player does not walk backwards.
void test_the_down_pitch_inversion_flips_the_forward_axis() {
  // Just past the threshold, and the anti-aim's frame is the camera's: the
  // forward axis comes back as the intent's forward, **inverted**.
  const Angles down{89.5F, 0.0F, 0.0F};
  const Axes inverted = cs2::control::anti_aim::rebase_axes(down, down, MoveIntent{1.0F, 0.0F});
  assert(nearly(inverted.forward, -1.0F));
  assert(nearly(inverted.left, 0.0F));

  // Just below the threshold the inversion does not apply.
  const Angles shallow{89.0F, 0.0F, 0.0F};
  const Axes plain = cs2::control::anti_aim::rebase_axes(shallow, shallow, MoveIntent{1.0F, 0.0F});
  assert(nearly(plain.forward, 1.0F));
  assert(nearly(plain.left, 0.0F));

  // The lateral axis is not inverted: only the forward one flips.
  const Axes lateral = cs2::control::anti_aim::rebase_axes(down, down, MoveIntent{0.0F, 1.0F});
  assert(nearly(lateral.forward, 0.0F));
  assert(nearly(lateral.left, 1.0F));
}

// The engine's analog is in unit values, and the server rounds it to
// `{-1, 0, +1}`; the re-based axes must stay inside that range.
void test_the_axes_stay_within_the_engine_unit_range() {
  const float pitches[] = {0.0F, 5.0F, -5.0F, 30.0F, -30.0F, 60.0F, 89.0F};
  const float yaws[] = {0.0F, 45.0F, 90.0F, 135.0F, 180.0F, -90.0F};
  const MoveIntent intents[] = {{1.0F, 0.0F},  {1.0F, 1.0F},  {-1.0F, 1.0F},
                                {0.0F, -1.0F}, {1.0F, -1.0F}, {0.0F, 0.0F}};
  for (const float camera_pitch : pitches) {
    for (const float camera_yaw : yaws) {
      for (const float anti_aim_pitch : pitches) {
        for (const float anti_aim_yaw : yaws) {
          for (const MoveIntent& intent : intents) {
            const Angles camera{camera_pitch, camera_yaw, 0.0F};
            const Angles anti_aim{anti_aim_pitch, anti_aim_yaw, 0.0F};
            const Axes axes = cs2::control::anti_aim::rebase_axes(camera, anti_aim, intent);
            assert(std::fabs(axes.forward) <= 1.0F);
            assert(std::fabs(axes.left) <= 1.0F);
            assert(std::isfinite(axes.forward));
            assert(std::isfinite(axes.left));
          }
        }
      }
    }
  }
}

// --- the fake angles -----------------------------------------------------------

// The fake yaw is the camera's yaw (the base angle) plus the configured
// offset, on the chosen side.
void test_the_fake_yaw_is_the_camera_yaw_plus_the_offset() {
  constexpr std::uint32_t k_seed = 0x5eed1234U;
  const float camera_yaw = 100.0F;
  const float offset = 45.0F;

  const float left =
      cs2::control::anti_aim::fake_yaw(camera_yaw, offset, Side::left, k_seed, 1, 0.0F);
  assert(nearly(left, 145.0F));

  const float right =
      cs2::control::anti_aim::fake_yaw(camera_yaw, offset, Side::right, k_seed, 1, 0.0F);
  assert(nearly(right, 55.0F));

  // The offset's magnitude is what the side applies to: a negative offset is
  // the same magnitude.
  const float negative =
      cs2::control::anti_aim::fake_yaw(camera_yaw, -offset, Side::left, k_seed, 1, 0.0F);
  assert(nearly(negative, 145.0F));

  // The automatic side alternates with the command's parity.
  const float even =
      cs2::control::anti_aim::fake_yaw(camera_yaw, offset, Side::automatic, k_seed, 2, 0.0F);
  const float odd =
      cs2::control::anti_aim::fake_yaw(camera_yaw, offset, Side::automatic, k_seed, 3, 0.0F);
  assert(nearly(even, 145.0F));
  assert(nearly(odd, 55.0F));

  // The yaw is wrapped into the engine's range.
  const float wrapped =
      cs2::control::anti_aim::fake_yaw(350.0F, 45.0F, Side::left, k_seed, 1, 0.0F);
  assert(wrapped > -180.0F);
  assert(wrapped < 180.0F);
  assert(nearly(std::fabs(wrapped), 35.0F));
}

// The jitter is re-rolled **once per command**: the same seed and tick always
// give the same offset, and it never leaves the configured amplitude.
void test_the_jitter_is_deterministic() {
  constexpr std::uint32_t k_seed = 0x5eed1234U;
  constexpr float k_amplitude = 7.0F;
  const float first = cs2::control::anti_aim::jitter_offset(k_seed, 1234, k_amplitude);
  const float again = cs2::control::anti_aim::jitter_offset(k_seed, 1234, k_amplitude);
  assert(nearly(first, again));
  assert(std::fabs(first) <= k_amplitude);

  // A different tick gives a different offset (the jitter moves per command).
  bool moved = false;
  for (std::uint64_t tick = 1235; tick < 1265; ++tick) {
    const float other = cs2::control::anti_aim::jitter_offset(k_seed, tick, k_amplitude);
    assert(std::fabs(other) <= k_amplitude);
    if (!nearly(other, first)) {
      moved = true;
    }
  }
  assert(moved);

  // A different seed gives a different stream.
  const float other_seed = cs2::control::anti_aim::jitter_offset(k_seed + 1U, 1234, k_amplitude);
  assert(!nearly(other_seed, first) || std::fabs(first) > k_amplitude);

  // No amplitude, no jitter.
  assert(nearly(cs2::control::anti_aim::jitter_offset(k_seed, 1234, 0.0F), 0.0F));
}

// The fake pitch is clamped to the engine's view-angle range.
void test_the_pitch_is_clamped_to_the_engine_range() {
  assert(nearly(cs2::control::anti_aim::clamp_pitch(0.0F), 0.0F));
  assert(nearly(cs2::control::anti_aim::clamp_pitch(45.0F), 45.0F));
  assert(nearly(cs2::control::anti_aim::clamp_pitch(120.0F), 89.0F));
  assert(nearly(cs2::control::anti_aim::clamp_pitch(-120.0F), -89.0F));
}

// The server's `sv_quantize_movement_input` rounds the analog to an integer,
// so the payload writes the snapped value.
void test_the_quantized_axes_are_the_engines_discrete_set() {
  const Axes snapped = cs2::control::anti_aim::quantize_axes(Axes{0.4F, -0.6F});
  assert(nearly(snapped.forward, 0.0F));
  assert(nearly(snapped.left, -1.0F));

  const Axes whole = cs2::control::anti_aim::quantize_axes(Axes{1.0F, 0.5F});
  assert(nearly(whole.forward, 1.0F));
  assert(nearly(whole.left, 1.0F));  // 0.5 rounds away from zero

  const Axes over = cs2::control::anti_aim::quantize_axes(Axes{4.0F, -4.0F});
  assert(nearly(over.forward, 1.0F));
  assert(nearly(over.left, -1.0F));
}

// --- the camera is never written ------------------------------------------------

// The camera reads the **live** view angles (`CCSGOInput + 0x688`); the
// anti-aim writes the **command's** angles (`base + 0x40`). They are different
// fields, so the camera cannot follow the fake angles — the split needs no
// pin. The classic mistake is a stray write to the camera's field, so the
// input backend must touch that field through exactly one constant, and that
// constant is read-only.
void test_the_camera_angles_are_only_read(const std::string& root) {
  const std::string path = root + "/payload/control/src/input.cpp";
  std::ifstream file(path);
  assert(file.good());
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string source = buffer.str();

  std::size_t occurrences = 0;
  for (std::size_t at = source.find("k_input_view_angles_offset"); at != std::string::npos;
       at = source.find("k_input_view_angles_offset", at + 1)) {
    ++occurrences;
  }
  assert(occurrences == 1);

  // The single use is a **read**: the angles are copied out of the input, and
  // the address is never the target of a store.
  const std::size_t use = source.find("k_input_view_angles_offset");
  const std::size_t line_start = source.rfind('\n', use) + 1;
  const std::size_t line_end = source.find('\n', use);
  const std::string line = source.substr(line_start, line_end - line_start);
  assert(line.find("read") != std::string::npos);
  assert(line.find('=') == std::string::npos);
  assert(line.find("reinterpret_cast<float*>") == std::string::npos);
}

// The command's own `viewangles` must have **one** writer: the `look` control
// command and the anti-aim share the field, so a second mechanism for it would
// be two features fighting over the same three floats. The field's has-bit is
// set by that one writer, so the bit is the marker.
void test_the_command_angles_have_one_writer(const std::string& root) {
  const std::string path = root + "/payload/control/src/input.cpp";
  std::ifstream file(path);
  assert(file.good());
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string source = buffer.str();

  std::size_t writers = 0;
  for (std::size_t at = source.find("k_base_viewangles_bit"); at != std::string::npos;
       at = source.find("k_base_viewangles_bit", at + 1)) {
    ++writers;
  }
  assert(writers == 1);

  // The shared writer exists and both callers use it.
  assert(source.find("auto Input::write_command_angles(") != std::string::npos);
  std::size_t calls = 0;
  for (std::size_t at = source.find("write_command_angles("); at != std::string::npos;
       at = source.find("write_command_angles(", at + 1)) {
    ++calls;
  }
  // The definition, the `look` command's path and the anti-aim's path.
  assert(calls >= 3);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  assert(argc >= 2);
  test_the_wish_model_is_the_engines_basis();
  test_the_rebase_is_the_identity_without_an_anti_aim();
  test_the_rebase_compensates_a_desync();
  test_the_rebase_preserves_the_world_space_wish();
  test_the_down_pitch_inversion_flips_the_forward_axis();
  test_the_axes_stay_within_the_engine_unit_range();
  test_the_fake_yaw_is_the_camera_yaw_plus_the_offset();
  test_the_jitter_is_deterministic();
  test_the_pitch_is_clamped_to_the_engine_range();
  test_the_quantized_axes_are_the_engines_discrete_set();
  test_the_camera_angles_are_only_read(argv[1]);
  test_the_command_angles_have_one_writer(argv[1]);
  std::puts("control-anti-aim: ok");
  return 0;
}
