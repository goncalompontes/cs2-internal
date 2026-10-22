// Exercise the actual Input adapter with bounded fake game memory/step allocation.
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>
#include "cs2/aim/autostop.hpp"
#include "cs2/game/schema.hpp"
#include "cs2/control/input.hpp"

namespace {
struct Range {
  std::uintptr_t begin;
  std::size_t size;
};
std::vector<Range> ranges;
bool allocation_failure = false;
int failures = 0;
void expect(bool value, const char* text) {
  if (!value) {
    std::printf("FAIL: %s\n", text);
    ++failures;
  }
}
template <typename T>
void put(void* base, std::size_t at, T value) {
  std::memcpy(static_cast<unsigned char*>(base) + at, &value, sizeof(value));
}
template <typename T>
auto get(const void* base, std::size_t at) -> T {
  T value{};
  std::memcpy(&value, static_cast<const unsigned char*>(base) + at, sizeof(value));
  return value;
}
template <typename T>
void track(T& storage) {
  ranges.push_back({reinterpret_cast<std::uintptr_t>(&storage), sizeof(storage)});
}
#include "autostop_ground_fixtures.hpp"
}  // namespace
namespace cs2::game {
// The host fixture has no live schema system; all game memory is explicitly tracked below.
std::optional<std::uint32_t> schema_field(std::string_view, std::string_view) { return {}; }
auto read_bytes(std::uintptr_t a, void* out, std::size_t size) noexcept -> bool {
  for (auto r : ranges)
    if (a >= r.begin && a - r.begin <= r.size && size <= r.size - (a - r.begin)) {
      std::memcpy(out, reinterpret_cast<void*>(a), size);
      return true;
    }
  return false;
}
}  // namespace cs2::game
namespace cs2::control {
struct AutostopInputTestAccess {
  static void setup(Input& input, std::uintptr_t pawn) {
    input.flags_offset_ = 0x10;
    input.velocity_offset_ = 0x20;
    input.local_pawn_.store(pawn);
    input.rage_active_.store(true);
  }
  static auto apply(Input& input, std::uint8_t* cmd, std::uint8_t* base) -> bool {
    return input.autostop_tick(cmd, base);
  }
  static void deactivate(Input& input) {
    input.rage_active_.store(false);
  }
};
Input::~Input() = default;
auto Input::alloc_step(void* base) noexcept -> void* {
  if (allocation_failure) return nullptr;
  auto* field = static_cast<std::uint8_t*>(base) + k_base_subtick_moves_offset;
  const int count = get<int>(field, k_field_size_offset);
  auto rep = get<std::uintptr_t>(field, k_field_rep_offset);
  if (count < 0 || count >= get<int>(reinterpret_cast<void*>(rep), k_rep_allocated_offset))
    return nullptr;
  put(field, k_field_size_offset, count + 1);
  return get<void*>(reinterpret_cast<void*>(rep), k_rep_elements_offset + count * 8);
}
auto Input::add_analog_step(void* base, float forward, float side, float when) noexcept -> void* {
  void* step = alloc_step(base);
  if (step == nullptr) return nullptr;
  std::memset(step, 0, 0x40);
  put(step, k_step_has_bits_offset, k_step_bits);
  put(step, k_step_when_offset, when);
  put(step, k_step_forward_offset, forward);
  put(step, k_step_left_offset, side);
  return step;
}
auto Input::add_yaw_step(void* base, float yaw, float when) noexcept -> void* {
  void* step = alloc_step(base);
  if (step == nullptr) return nullptr;
  std::memset(step, 0, 0x40);
  put(step, k_step_has_bits_offset, k_step_angle_bits);
  put(step, k_step_when_offset, when);
  put(step, k_step_yaw_offset, yaw);
  return step;
}
}  // namespace cs2::control

auto main() -> int {
  using namespace cs2::control;
  std::array<unsigned char, 0x100> pawn{}, base{}, cmd{};
  std::array<unsigned char, 0x40> angles{}, buttons{};
  std::array<unsigned char, 8 + 40 * 8> rep{};
  std::array<std::array<unsigned char, 0x40>, 40> steps{};
  track(pawn);
  track(base);
  track(cmd);
  track(angles);
  track(buttons);
  track(rep);
  track(steps);
  put(base.data(), k_base_viewangles_offset, reinterpret_cast<std::uintptr_t>(angles.data()));
  put(base.data(), k_base_buttons_pb_offset, reinterpret_cast<std::uintptr_t>(buttons.data()));
  put(base.data(), k_base_subtick_moves_offset + k_field_rep_offset,
      reinterpret_cast<std::uintptr_t>(rep.data()));
  put(rep.data(), k_rep_allocated_offset, 40);
  for (int i = 0; i < 40; ++i)
    put(rep.data(), k_rep_elements_offset + i * 8, static_cast<void*>(steps[i].data()));
  put(cmd.data(), 0x8, 1u);
  put(base.data(), 0x54, 100);
  put(pawn.data(), 0x10, 1u);
  put(cmd.data(), k_cmd_buttons_offset, button::k_attack | button::k_jump);
  put(base.data(), k_base_move_offset, 1.0F);
  const unsigned* fixture = nullptr;
  for (const auto& row : k_ground_fixtures)
    if (row[5]) {
      fixture = row;
      break;
    }
  expect(fixture != nullptr, "fractional fixture exists");
  if (fixture == nullptr) return 1;
  put(pawn.data(), 0x20, std::bit_cast<float>(fixture[1]));
  Input input;
  AutostopInputTestAccess::setup(input, reinterpret_cast<std::uintptr_t>(pawn.data()));
  input.set_stop_physics(std::bit_cast<float>(fixture[0]), std::bit_cast<float>(fixture[2]),
                         std::bit_cast<float>(fixture[3]), 12, 80, 800);
  // Preserve both a later jump step and a mixed attack+analog step.
  put(steps[0].data(), k_step_has_bits_offset, k_step_button_bits);
  put(steps[0].data(), k_step_button_offset, button::k_jump);
  put(steps[0].data(), k_step_when_offset, .9F);
  put(steps[1].data(), k_step_has_bits_offset, k_step_button_bits | k_step_bits);
  put(steps[1].data(), k_step_button_offset, button::k_attack);
  put(steps[1].data(), k_step_when_offset, .8F);
  put(steps[1].data(), k_step_forward_offset, .5F);
  put(steps[2].data(), k_step_has_bits_offset, k_step_bits);
  put(steps[2].data(), k_step_when_offset, .7F);
  put(base.data(), k_base_subtick_moves_offset + k_field_size_offset, 3);
  for (int repeat = 0; repeat < 100; ++repeat) {
    expect(AutostopInputTestAccess::apply(input, cmd.data(), base.data()),
           "adapter applies stopping");
    expect(get<int>(base.data(), k_base_subtick_moves_offset + k_field_size_offset) == 3,
           "repeated hooks do not accumulate steps");
    float previous = -1;
    for (int i = 0; i < 3; ++i) {
      const auto* step = get<void*>(rep.data(), k_rep_elements_offset + i * 8);
      const float when = get<float>(step, k_step_when_offset);
      expect(previous <= when, "active protobuf steps remain sorted");
      previous = when;
    }
    for (int i = 0; i < 40; ++i)
      for (int j = i + 1; j < 40; ++j)
        expect(get<void*>(rep.data(), k_rep_elements_offset + i * 8) !=
                   get<void*>(rep.data(), k_rep_elements_offset + j * 8),
               "protobuf spare pointers stay unique");
  }
  expect((get<std::uint64_t>(cmd.data(), k_cmd_buttons_offset) & button::k_attack) != 0,
         "autostop preserves rage attack");
  expect((get<std::uint64_t>(cmd.data(), k_cmd_buttons_offset) & button::k_jump) != 0,
         "jump stays held when cancellation is off");
  expect((get<std::uint32_t>(steps[1].data(), k_step_has_bits_offset) & 1u) != 0 &&
             get<float>(steps[1].data(), k_step_forward_offset) == 0,
         "mixed button survives analog replacement");

  // Full table of replaceable analog steps must still admit the desired event.
  put(cmd.data(), 0x8, 2u);
  put(base.data(), k_base_move_offset, 1.0F);
  for (int i = 0; i < 32; ++i) {
    auto* step = get<void*>(rep.data(), k_rep_elements_offset + i * 8);
    std::memset(step, 0, 0x40);
    put(step, k_step_has_bits_offset, k_step_bits);
  }
  put(base.data(), k_base_subtick_moves_offset + k_field_size_offset, 32);
  expect(AutostopInputTestAccess::apply(input, cmd.data(), base.data()),
         "replacement fits a full analog table");
  expect(get<int>(base.data(), k_base_subtick_moves_offset + k_field_size_offset) == 1,
         "obsolete analog steps removed");

  // Failed allocation leaves the original command and array untouched.
  put(cmd.data(), 0x8, 3u);
  put(base.data(), k_base_move_offset, 1.0F);
  const auto old_base = base;
  const auto old_cmd = cmd;
  const auto old_angles = angles;
  allocation_failure = true;
  expect(!AutostopInputTestAccess::apply(input, cmd.data(), base.data()),
         "allocation failure reported");
  expect(base == old_base && cmd == old_cmd && angles == old_angles,
         "allocation failure is atomic");
  allocation_failure = false;
  AutostopInputTestAccess::deactivate(input);
  expect(!AutostopInputTestAccess::apply(input, cmd.data(), base.data()),
         "inactive rage releases movement ownership");
  // Observe an airborne tick even while rage is inactive, then verify the
  // recovered >=17 cutoff only changes jump clearing, not stopping itself.
  put(pawn.data(), 0x10, 0u);
  put(base.data(), 0x54, 100);
  (void)AutostopInputTestAccess::apply(input, cmd.data(), base.data());
  AutostopInputTestAccess::setup(input, reinterpret_cast<std::uintptr_t>(pawn.data()));
  input.set_rage_autostop(true, true, true, true);
  put(pawn.data(), 0x10, 1u);
  for (int tick : {101, 118}) {
    put(base.data(), 0x54, tick);
    put(cmd.data(), 0x8, static_cast<unsigned>(tick));
    put(cmd.data(), k_cmd_buttons_offset, button::k_attack | button::k_jump);
    put(base.data(), k_base_move_offset, 1.0F);
    auto* step = get<void*>(rep.data(), k_rep_elements_offset);
    std::memset(step, 0, 0x40);
    put(step, k_step_has_bits_offset, k_step_button_bits);
    put(step, k_step_button_offset, button::k_jump);
    put(step, k_step_when_offset, .9F);
    put(base.data(), k_base_subtick_moves_offset + k_field_size_offset, 1);
    expect(AutostopInputTestAccess::apply(input, cmd.data(), base.data()),
           "age cutoff keeps stopping active");
    const bool held = (get<std::uint64_t>(cmd.data(), k_cmd_buttons_offset) & button::k_jump) != 0;
    expect(held == (tick == 118), "17-tick age only suppresses jump clearing");
  }
  std::printf(
      "autostop adapter: %d failures (100 repeated hooks, ordering, ownership, full table, "
      "failure)\n",
      failures);
  return failures == 0 ? 0 : 1;
}
