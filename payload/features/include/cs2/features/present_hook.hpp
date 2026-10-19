#pragma once

#include <cstdint>

#include "cs2/features/dxgi_interposer.hpp"
#include "cs2/features/overlay.hpp"
#include "cs2/render/ui_backend.hpp"

namespace cs2::features {

class Menu;

// Drives a `UiBackend` every frame from an interposed `IDXGISwapChain::Present`.
//
// The swapchain is not inline-hooked: `DxgiInterposer` replaces the Present and
// ResizeBuffers slots in the discovered swapchain method table (ADR-0015). The
// device/context/HWND are derived from the swapchain on the first frame.
//
// When a `Menu` is supplied it owns the in-process menu/WndProc (workstream A):
// it is ticked each frame and drawn inside the ImGui frame via the backend's UI
// callback.
class PresentHook {
 public:
  PresentHook(render::UiBackend& backend, Overlay& overlay, Menu* menu = nullptr) noexcept;
  ~PresentHook();

  PresentHook(const PresentHook&) = delete;
  PresentHook& operator=(const PresentHook&) = delete;

  bool install() noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

  // Invoked by the interposer before the original Present.
  void on_present(void* swapchain) noexcept;

  // Invoked by the interposer before the original ResizeBuffers.
  void on_resize() noexcept;

 private:
  static void present_callback(void* user, void* swapchain) noexcept;
  static void resize_callback(void* user) noexcept;

  render::UiBackend* backend_;
  Overlay* overlay_;
  Menu* menu_;
  DxgiInterposer interposer_{};
  render::RenderContext context_{};
  bool backend_ready_ = false;
  std::uint64_t frames_ = 0;
  bool installed_ = false;
};

}  // namespace cs2::features
