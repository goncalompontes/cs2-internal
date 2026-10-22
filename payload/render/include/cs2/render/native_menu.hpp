#pragma once
#include <atomic>
#include "cs2/game/rage_debug.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::render {
class NativeMenu {
 public:
  using DebugProvider = bool (*)(void*, game::RageDebugSnapshot&);
  void configure_debug(DebugProvider provider, void* context) noexcept {
    debug_provider_ = provider;
    debug_context_ = context;
  }
  using SettingsSink = void (*)(void*, const overlay::wire::Settings&);
  void configure(overlay::wire::Settings defaults, SettingsSink sink, void* context) noexcept;
  bool initialize(void* window) noexcept;
  void frame() noexcept;
  void shutdown() noexcept;
  void note_focus_lost() noexcept {
    open_.store(false);
  }
  void add_wheel(int delta) noexcept {
    wheel_.fetch_add(delta);
  }
  [[nodiscard]] bool open() const noexcept {
    return open_.load();
  }
  [[nodiscard]] bool show_hud() const noexcept {
    return settings_.show_hud != 0;
  }
  [[nodiscard]] void* previous_proc() const noexcept {
    return previous_proc_;
  }

 private:
  void draw_rage_debug() noexcept;
  DebugProvider debug_provider_{};
  void* debug_context_{};
  game::RageDebugSnapshot debug_snapshot_{};
  bool debug_frozen_{};
  overlay::wire::Settings settings_{};
  SettingsSink sink_ = nullptr;
  void* sink_context_ = nullptr;
  void* window_ = nullptr;
  void* previous_proc_ = nullptr;
  std::atomic<bool> open_{false};
  std::atomic<int> wheel_{0};
  bool previous_toggle_ = false;
  bool mouse_[3]{};
};
}  // namespace cs2::render
