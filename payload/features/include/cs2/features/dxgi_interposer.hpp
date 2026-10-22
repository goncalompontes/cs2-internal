#pragma once

// Native Windows (ADR-0023): discover Present/ResizeBuffers implementations
// using a throwaway D3D11 device and install MinHook function hooks. The previous
// method-table path is retained for cross builds (ADR-0015).

#include <memory>

#include "cs2/hooks/vtable.hpp"

namespace cs2::features {

class DxgiInterposer {
 public:
  // Invoked from inside the interposed method, before the original is called.
  using PresentCallback = void (*)(void* user, void* swapchain);
  using ResizeCallback = void (*)(void* user);

  DxgiInterposer() noexcept = default;
  ~DxgiInterposer();

  DxgiInterposer(const DxgiInterposer&) = delete;
  DxgiInterposer& operator=(const DxgiInterposer&) = delete;
  DxgiInterposer(DxgiInterposer&&) = delete;
  DxgiInterposer& operator=(DxgiInterposer&&) = delete;

  // Discover the DXGI method tables and patch the slots. `user` is passed back
  // to the callbacks. Returns false when DXGI/D3D11 are absent or discovery
  // fails (e.g. non-graphical target).
  [[nodiscard]] bool install(PresentCallback on_present, ResizeCallback on_resize,
                             void* user) noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }

 private:
  std::unique_ptr<hooks::VtableHook> present_;
  std::unique_ptr<hooks::VtableHook> resize_buffers_;
  std::unique_ptr<hooks::VtableHook> create_swapchain_;
  std::unique_ptr<hooks::VtableHook> create_swapchain_for_hwnd_;
  void* native_present_ = nullptr;
  void* native_resize_ = nullptr;
  bool installed_ = false;
};

}  // namespace cs2::features
