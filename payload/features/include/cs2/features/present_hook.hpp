#pragma once

#include <cstdint>

#include "cs2/features/debug_hud.hpp"
#include "cs2/render/ui_backend.hpp"

namespace cs2::features {

// Hooks `IDXGISwapChain::Present` and drives a `UiBackend` every frame.
//
// The swapchain method table is discovered by kiero, and the Present entry is
// hooked with a MinHook inline hook (kiero::bind), matching the reference
// overlay's approach rather than patching the shared vtable slot.
class PresentHook {
 public:
  PresentHook(render::UiBackend& backend, DebugHud& hud) noexcept;
  ~PresentHook();

  PresentHook(const PresentHook&) = delete;
  PresentHook& operator=(const PresentHook&) = delete;

  bool install() noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

  // Invoked by the detour before the original Present.
  void on_present(void* swapchain) noexcept;

 private:
  render::UiBackend* backend_;
  DebugHud* hud_;
  render::RenderContext context_{};
  bool backend_ready_ = false;
  std::uint64_t frames_ = 0;
  bool installed_ = false;
};

}  // namespace cs2::features
