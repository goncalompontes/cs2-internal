// Unit tests for the anti-aim's **decision maths** (`cs2/control/anti_aim.hpp`):
// the fake (desync) yaw, the pitch clamp, the per-command jitter and â€” the part
// that makes the feature correct â€” the **movement re-base**.
//
// The module is pure (no engine headers, no `<windows.h>`, no state), so the
// whole decision can be checked host-side. The re-base is the load-bearing
// piece: the operator's W/A/S/D must stay camera-relative while the command
// carries the fake angles â€” and the test pins that on the value that is
// actually **delivered** (a member of the engine's discrete analog set, the
// only thing the server acts on: `sv_quantize_movement_input` rounds the
// analog it receives).
//
// The last two tests are **source** checks over the whole payload tree: the
// camera reads the *live* view angles (`CCSGOInput + 0x688`) and the anti-aim
// writes the *command's* angles, so a stray write to the camera's field would
// make the camera follow the fake angles â€” the classic mistake this feature
// must not make â€” and the command's angles must have exactly one writer.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/control/anti_aim.hpp"

namespace {

using cs2::control::anti_aim::Angles;
using cs2::control::anti_aim::Axes;
using cs2::control::anti_aim::Delivery;
using cs2::control::anti_aim::Side;
using cs2::control::anti_aim::Vec2;

constexpr float k_epsilon = 0.001F;
//: The discrete analog set's granularity: the nearest of the eight digital
//: directions is at most 22.5 degrees away.
constexpr float k_digital_granularity = 22.5F;

auto nearly(float value, float expected, float tolerance = k_epsilon) -> bool {
  return std::fabs(value - expected) <= tolerance;
}

using cs2::control::anti_aim::length;

// The unit vector of a wish â€” the direction the player walks.
auto direction(Vec2 wish) -> Vec2 {
  const float size = length(wish);
  if (size <= 0.0F) {
    return Vec2{};
  }
  return Vec2{wish.x / size, wish.y / size};
}

// The camera-frame wish: what the engine computes with no anti-aim.
auto camera_wish(Angles camera, Axes axes) -> Vec2 {
  return cs2::control::anti_aim::engine_wish(camera, axes.forward, axes.left);
}

// --- the engine's wish model ---------------------------------------------------

// The model of `AirMove` the re-base is built on: the wish is the forward axis
// times the basis's forward plus the side axis times the basis's **left**.
void test_the_wish_model_is_the_engines_basis() {
  // The camera at yaw 0: the forward is +X and the left is +Y.
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Vec2 forward = camera_wish(camera, Axes{1.0F, 0.0F});
  assert(nearly(forward.x, 1.0F));
  assert(nearly(forward.y, 0.0F));

  // The side axis is positive to the player's **left** (`docs/re/strafe-sign.md`).
  const Vec2 left = camera_wish(camera, Axes{0.0F, 1.0F});
  assert(nearly(left.x, 0.0F));
  assert(nearly(left.y, 1.0F));

  // The basis turns with the yaw: at yaw 90 the forward is +Y and the left -X.
  const Angles turned{0.0F, 90.0F, 0.0F};
  const Vec2 turned_forward = camera_wish(turned, Axes{1.0F, 0.0F});
  assert(nearly(turned_forward.x, 0.0F));
  assert(nearly(turned_forward.y, 1.0F));
  const Vec2 turned_left = camera_wish(turned, Axes{0.0F, 1.0F});
  assert(nearly(turned_left.x, -1.0F));
  assert(nearly(turned_left.y, 0.0F));

  // The pitch tips the forward axis out of the walk's plane: at 90 degrees the
  // forward contributes nothing horizontally (this is the case the down-pitch
  // inversion is about).
  const Angles down{90.0F, 0.0F, 0.0F};
  const Vec2 tipped = camera_wish(down, Axes{1.0F, 0.0F});
  assert(std::fabs(tipped.x) < 0.001F);
  assert(std::fabs(tipped.y) < 0.001F);
}

// --- the re-base ----------------------------------------------------------------

// With no anti-aim (the anti-aim's angles are the camera's) the movement must
// be **untouched**: the delivered axes are the player's own intent.
void test_the_rebase_is_the_identity_without_an_anti_aim() {
  const Angles camera{15.0F, 42.0F, 0.0F};
  const Delivery forward = cs2::control::anti_aim::deliver_axes(camera, camera, Axes{1.0F, 0.0F});
  assert(nearly(forward.axes.forward, 1.0F));
  assert(nearly(forward.axes.left, 0.0F));
  assert(nearly(forward.direction_error_deg, 0.0F));

  const Delivery back = cs2::control::anti_aim::deliver_axes(camera, camera, Axes{-1.0F, 0.0F});
  assert(nearly(back.axes.forward, -1.0F));
  assert(nearly(back.axes.left, 0.0F));

  const Delivery diagonal = cs2::control::anti_aim::deliver_axes(camera, camera, Axes{1.0F, 1.0F});
  assert(nearly(diagonal.axes.forward, 1.0F));
  assert(nearly(diagonal.axes.left, 1.0F));
  assert(nearly(diagonal.direction_error_deg, 0.0F));

  const Delivery still = cs2::control::anti_aim::deliver_axes(camera, camera, Axes{0.0F, 0.0F});
  assert(nearly(still.axes.forward, 0.0F));
  assert(nearly(still.axes.left, 0.0F));
}

// A hand-computed case: the classic 180-degree desync. The anti-aim's forward
// axis points the other way, so the delivered forward axis must be **-1** â€”
// that is what keeps the operator's `W` walking where the camera looks.
void test_the_rebase_compensates_a_desync() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Angles anti_aim{0.0F, 180.0F, 0.0F};
  const Delivery delivery =
      cs2::control::anti_aim::deliver_axes(camera, anti_aim, Axes{1.0F, 0.0F});
  assert(nearly(delivery.axes.forward, -1.0F));
  assert(nearly(delivery.axes.left, 0.0F));

  // The wish the engine computes in the anti-aim frame is the camera's wish.
  const Vec2 before = camera_wish(camera, Axes{1.0F, 0.0F});
  const Vec2 after =
      cs2::control::anti_aim::engine_wish(anti_aim, delivery.axes.forward, delivery.axes.left);
  assert(nearly(before.x, after.x));
  assert(nearly(before.y, after.y));
  assert(nearly(delivery.direction_error_deg, 0.0F));
}

// The delivered axes must be a member of the engine's discrete set: the server
// rounds the analog it receives (`sv_quantize_movement_input`), so a
// fractional axis is quantised away and the client would predict something
// the server never acts on.
void test_the_delivered_axes_are_the_engines_discrete_set() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  for (int offset = -180; offset <= 180; offset += 5) {
    const Angles anti_aim{0.0F, static_cast<float>(offset), 0.0F};
    for (const Axes& intent : {Axes{1.0F, 0.0F}, Axes{0.0F, 1.0F}, Axes{1.0F, 1.0F},
                               Axes{-1.0F, 0.0F}, Axes{0.0F, -1.0F}, Axes{0.0F, 0.0F}}) {
      const Delivery delivery = cs2::control::anti_aim::deliver_axes(camera, anti_aim, intent);
      for (const float axis : {delivery.axes.forward, delivery.axes.left}) {
        assert(axis == 0.0F || axis == 1.0F || axis == -1.0F);
      }
    }
  }
}

// The invariant, on the value that is **delivered**: the wish the engine
// computes from the delivered axes, in the anti-aim frame, points the same way
// as the wish it would have computed from the original axes, in the camera
// frame. The discrete set can only express eight directions, so the delivered
// direction is within the set's granularity â€” and it is **exact** for an
// offset that is a multiple of 45 degrees.
void test_the_delivered_direction_is_preserved() {
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
      {0.0F, 0.0F, 0.0F, 30.0F, 1.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, -1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, 0.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 30.0F, 0.0F, -1.0F},
      {12.0F, 15.0F, 0.0F, 15.0F + 180.0F, 1.0F, 0.0F},
      {-20.0F, 200.0F, -20.0F, 200.0F + 90.0F, 1.0F, 1.0F},
      // The camera pitched down and a 30-degree desync: the ideal axes are
      // `(0.61, -0.35)`, and a **per-axis rounding** would deliver `(1, 0)` —
      // 30 degrees off. The direction-optimal snap delivers `(1, -1)`: 15.
      {45.0F, 0.0F, 0.0F, 30.0F, 1.0F, 0.0F},
      {45.0F, 0.0F, 0.0F, 30.0F, 1.0F, 1.0F},
  };
  for (const Case& item : cases) {
    const Angles camera{item.camera_pitch, item.camera_yaw, 0.0F};
    const Angles anti_aim{item.anti_aim_pitch, item.anti_aim_yaw, 0.0F};
    const Axes intent{item.forward, item.left};
    const Delivery delivery = cs2::control::anti_aim::deliver_axes(camera, anti_aim, intent);

    const Vec2 before = camera_wish(camera, intent);
    const Vec2 after =
        cs2::control::anti_aim::engine_wish(anti_aim, delivery.axes.forward, delivery.axes.left);
    assert(length(before) > 0.0F);
    const Vec2 want = direction(before);
    const Vec2 got = direction(after);
    // The delivered direction is the camera's, up to the discrete set's
    // granularity â€” and the **reported** error says exactly how far.
    const float error =
        std::atan2(std::fabs(want.x * got.y - want.y * got.x), want.x * got.x + want.y * got.y) *
        180.0F / 3.14159265358979323846F;
    assert(error <= k_digital_granularity + k_epsilon);
    assert(nearly(error, delivery.direction_error_deg, 0.01F));
  }
}

// The offsets that are a multiple of 45 degrees are delivered **exactly**:
// the digital set can express them.
void test_the_delivered_direction_is_exact_for_a_multiple_of_45() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  for (int offset = -180; offset <= 180; offset += 45) {
    const Angles anti_aim{0.0F, static_cast<float>(offset), 0.0F};
    const Delivery delivery =
        cs2::control::anti_aim::deliver_axes(camera, anti_aim, Axes{1.0F, 0.0F});
    assert(nearly(delivery.direction_error_deg, 0.0F));
  }
}

// The 30-degree offset is the interesting one: the ideal re-base is
// `(0.866, -0.500)`, and a per-axis rounding would deliver `(1, 0)` â€” 30
// degrees off. The direction-optimal snap delivers `(1, -1)`: 15 degrees off.
void test_the_delivered_direction_is_not_a_per_axis_rounding() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Angles anti_aim{0.0F, 30.0F, 0.0F};
  const Delivery delivery =
      cs2::control::anti_aim::deliver_axes(camera, anti_aim, Axes{1.0F, 0.0F});
  assert(nearly(delivery.axes.forward, 1.0F));
  assert(nearly(delivery.axes.left, -1.0F));
  assert(nearly(delivery.direction_error_deg, 15.0F, 0.5F));
}

// A steep fake pitch is the saturated case: the engine multiplies the forward
// axis by `cos(pitch)`, the compensation needs more than Â±1, and the delivery
// has to compromise. What it gives up is reported: a single-axis intent keeps
// its direction but loses its **speed** (`cos(89Â°) = 0.017`), and a diagonal
// intent keeps its speed but loses its **direction** (the walk goes straight
// instead of diagonal).
void test_the_saturated_rebase_reports_what_it_gives_up() {
  const Angles camera{0.0F, 0.0F, 0.0F};
  const Angles steep{89.0F, 0.0F, 0.0F};

  // `W` alone: the direction survives, the walk speed does not.
  const Delivery forward = cs2::control::anti_aim::deliver_axes(camera, steep, Axes{1.0F, 0.0F});
  assert(forward.saturated);
  assert(forward.wish_factor < 0.05F);
  assert(nearly(forward.direction_error_deg, 0.0F, 0.5F));

  // `W`+`A`: the walk speed survives, the direction does not.
  const Delivery diagonal = cs2::control::anti_aim::deliver_axes(camera, steep, Axes{1.0F, 1.0F});
  assert(diagonal.saturated);
  assert(diagonal.direction_error_deg > k_digital_granularity);

  // A flat fake pitch does not saturate.
  const Angles flat{0.0F, 0.0F, 0.0F};
  const Delivery clean = cs2::control::anti_aim::deliver_axes(camera, flat, Axes{1.0F, 0.0F});
  assert(!clean.saturated);
  assert(nearly(clean.wish_factor, 1.0F));
}

// The engine's analog is in unit values; the delivered axes must stay inside
// that range, and they must be finite.
void test_the_axes_stay_within_the_engine_unit_range() {
  const float pitches[] = {0.0F, 5.0F, -5.0F, 30.0F, -30.0F, 60.0F, 89.0F};
  const float yaws[] = {0.0F, 45.0F, 90.0F, 135.0F, 180.0F, -90.0F};
  const Axes intents[] = {{1.0F, 0.0F},  {1.0F, 1.0F},  {-1.0F, 1.0F},
                          {0.0F, -1.0F}, {1.0F, -1.0F}, {0.0F, 0.0F}};
  for (const float camera_pitch : pitches) {
    for (const float camera_yaw : yaws) {
      for (const float anti_aim_pitch : pitches) {
        for (const float anti_aim_yaw : yaws) {
          for (const Axes& intent : intents) {
            const Angles camera{camera_pitch, camera_yaw, 0.0F};
            const Angles anti_aim{anti_aim_pitch, anti_aim_yaw, 0.0F};
            const Delivery delivery =
                cs2::control::anti_aim::deliver_axes(camera, anti_aim, intent);
            assert(std::fabs(delivery.axes.forward) <= 1.0F);
            assert(std::fabs(delivery.axes.left) <= 1.0F);
            assert(std::isfinite(delivery.axes.forward));
            assert(std::isfinite(delivery.axes.left));
            assert(std::isfinite(delivery.direction_error_deg));
            assert(std::isfinite(delivery.wish_factor));
          }
        }
      }
    }
  }
}

// The down-pitch inversion: with the camera past 89 degrees the operator is
// looking straight down, the camera's forward axis points into the ground and
// its sign flips at 90 degrees — the community's rule inverts the forward axis
// there, so the player does not walk backwards.
//
// **The branch is defensive**: the engine clamps the *camera's* pitch to ±89
// degrees (`cl_pitch`), so a live game never reaches it. The test pins it as
// the contract it is, on the re-base's ideal axes (the delivery's
// direction-optimal snap washes the inversion out — it delivers the direction
// the camera's, which is the better answer).
void test_the_down_pitch_inversion_flips_the_forward_axis() {
  // Just past the threshold, and the anti-aim's frame is the camera's: the
  // forward axis comes back as the intent's forward, **inverted**.
  const Angles down{89.5F, 0.0F, 0.0F};
  const Axes inverted = cs2::control::anti_aim::rebase_axes(down, down, Axes{1.0F, 0.0F});
  assert(nearly(inverted.forward, -1.0F));
  assert(nearly(inverted.left, 0.0F));

  // Just below the threshold the inversion does not apply.
  const Angles shallow{89.0F, 0.0F, 0.0F};
  const Axes plain = cs2::control::anti_aim::rebase_axes(shallow, shallow, Axes{1.0F, 0.0F});
  assert(nearly(plain.forward, 1.0F));
  assert(nearly(plain.left, 0.0F));

  // The lateral axis is not inverted: only the forward one flips.
  const Axes lateral = cs2::control::anti_aim::rebase_axes(down, down, Axes{0.0F, 1.0F});
  assert(nearly(lateral.forward, 0.0F));
  assert(nearly(lateral.left, 1.0F));
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

// The engine's discrete set: the server rounds the analog it receives.
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

// --- the source checks over the payload tree ------------------------------------

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file.good()) {
    return {};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

auto source_files(const std::filesystem::path& root) -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    // The **tests** are not the payload: the invariant is about the code that
    // runs in the game.
    if (entry.path().string().find("tests") != std::string::npos) {
      continue;
    }
    const std::string extension = entry.path().extension().string();
    if (extension == ".cpp" || extension == ".hpp" || extension == ".h") {
      files.push_back(entry.path());
    }
  }
  return files;
}

auto lines_of(const std::string& source) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

// The camera reads the **live** view angles (`CCSGOInput + 0x688`); the
// anti-aim writes the **command's** angles. They are different fields, so the
// camera cannot follow the fake angles â€” the split needs no pin.
//
// The check is over the **whole payload tree** and over the **write sites**: no
// line that mentions the camera's field may store to it. A *read* (the
// anti-aim's own reader) and a hoisted address are both fine â€” the check
// forbids the store, not the text.
void test_the_camera_angles_are_never_written(const std::string& root) {
  const auto files = source_files(std::filesystem::path(root) / "payload");
  assert(!files.empty());
  bool seen_the_reader = false;
  for (const auto& file : files) {
    for (const std::string& line : lines_of(read_file(file))) {
      if (line.find("k_input_view_angles_offset") == std::string::npos &&
          line.find("0x688") == std::string::npos) {
        continue;
      }
      if (line.find("read") != std::string::npos) {
        seen_the_reader = true;
      }
      // A store to the camera's field would drag the operator's view onto the
      // fake angles.
      assert(line.find("*reinterpret_cast<") == std::string::npos);
      assert(line.find("*static_cast<") == std::string::npos);
      assert(line.find("write_bytes(") == std::string::npos);
      assert(line.find("memcpy(") == std::string::npos);
    }
  }
  assert(seen_the_reader);
}

// The command's own `viewangles` must have **one** writer: the `look` control
// command and the anti-aim share the field, so a second mechanism for it would
// be two features fighting over the same three floats.
//
// The check walks the **whole payload tree** and counts the *blocks* (the
// repo's `settings-sink` pattern) that both take the command's angles pointer
// and write into the angle triple: there must be exactly one.
void test_the_command_angles_have_one_writer(const std::string& root) {
  const auto files = source_files(std::filesystem::path(root) / "payload");
  int writers = 0;
  for (const auto& file : files) {
    const std::string source = read_file(file);
    std::size_t at = 0;
    while (at < source.size()) {
      const std::size_t next = source.find("\n}\n", at);
      const std::string block =
          source.substr(at, next == std::string::npos ? std::string::npos : next - at);
      const bool takes_the_pointer = block.find("k_base_viewangles_offset") != std::string::npos;
      const bool writes_the_triple = block.find("k_angles_values_offset") != std::string::npos &&
                                     block.find("xyz[0] =") != std::string::npos;
      if (takes_the_pointer && writes_the_triple) {
        ++writers;
      }
      if (next == std::string::npos) {
        break;
      }
      at = next + 3;
    }
  }
  assert(writers == 1);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  assert(argc >= 2);
  test_the_wish_model_is_the_engines_basis();
  test_the_rebase_is_the_identity_without_an_anti_aim();
  test_the_rebase_compensates_a_desync();
  test_the_delivered_axes_are_the_engines_discrete_set();
  test_the_delivered_direction_is_preserved();
  test_the_delivered_direction_is_exact_for_a_multiple_of_45();
  test_the_delivered_direction_is_not_a_per_axis_rounding();
  test_the_saturated_rebase_reports_what_it_gives_up();
  test_the_down_pitch_inversion_flips_the_forward_axis();
  test_the_axes_stay_within_the_engine_unit_range();
  test_the_fake_yaw_is_the_camera_yaw_plus_the_offset();
  test_the_jitter_is_deterministic();
  test_the_pitch_is_clamped_to_the_engine_range();
  test_the_quantized_axes_are_the_engines_discrete_set();
  test_the_camera_angles_are_never_written(argv[1]);
  test_the_command_angles_have_one_writer(argv[1]);
  std::puts("control-anti-aim: ok");
  return 0;
}
