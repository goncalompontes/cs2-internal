#pragma once

#include <cstdint>
#include <memory>

#include "cs2/hooks/vtable.hpp"
#include "cs2/render/ui_backend.hpp"

namespace cs2::features {

// Hooks `IDXGISwapChain::Present` (vtable index 8) and drives a `UiBackend`
// every frame. The swapchain vtable is discovered via a throwaway D3D11 device;
// hooking that vtable slot affects every swapchain of the class.
class PresentHook {
 public:
  explicit PresentHook(render::UiBackend& backend) noexcept;
  ~PresentHook();

  PresentHook(const PresentHook&) = delete;
  PresentHook& operator=(const PresentHook&) = delete;

  bool install() noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

  // Invoked by the detour before the original Present.
  void on_present() noexcept;

 private:
  render::UiBackend* backend_;
  std::unique_ptr<hooks::VtableHook> hook_;
  std::uint64_t frames_ = 0;
  bool installed_ = false;
};

}  // namespace cs2::features
