#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "cs2/game/math.hpp"
#include "cs2/game/module.hpp"
namespace cs2::control {
struct MoveCrcBytes {
  std::array<std::byte, 64> data{};
  std::size_t size{};
};
// Native move_crc is a protobuf encoding of buttons + viewangles, not CRC32.
MoveCrcBytes encode_move_crc(const std::uint64_t buttons[3], const float angles[3]) noexcept;
// Current client helpers consume a 32-byte MSVC basic_string object. Borrowing
// the input storage avoids the reference's undersized temporary and heap leak.
struct EngineStringView {
  std::array<std::byte, 16> storage{};
  std::uint64_t size{};
  std::uint64_t capacity{15};
};
static_assert(sizeof(EngineStringView) == 0x20);
struct NativeCommandApi {
  void* (*command_base)(void*){};
  void* (*command)(void*, std::uint32_t){};
  void (*assign_crc)(std::uintptr_t*, const EngineStringView*, void*){};
  void* (*allocate_buttons)(void*){};
  game::Vec3* (*get_view)(void*, int){};
  void (*set_view)(void*, int, const game::Vec3*){};
  void* handle_view{};
};
class NativeCommandBridge {
 public:
  bool initialize(game::ModuleInfo module) noexcept;
  void bind(NativeCommandApi api) noexcept { api_ = api; }
  bool ready() const noexcept { return api_.command_base && api_.command && api_.assign_crc &&
      api_.allocate_buttons && api_.get_view && api_.set_view && api_.handle_view; }
  void* current(void* controller) const noexcept;
  bool finalize(void* command) const noexcept;
  bool view(void* input, game::Vec3& out) const noexcept;
  void restore_view(void* input, const game::Vec3& view) const noexcept;
  void* view_handler() const noexcept { return api_.handle_view; }
 private:
  NativeCommandApi api_{};
  std::uintptr_t attempted_base_{};
};
}
