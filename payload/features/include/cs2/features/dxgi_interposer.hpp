#pragma once

// DXGI swapchain interposition (ADR-0015).
//
// Instead of inline-patching the `IDXGISwapChain::Present` function *body* with
// MinHook (DXVK only conditionally reserves a hot-patchable `Present` entry, and
// the project saw stalls with the inline hook), this interposes at the
// method-table level (see docs/decisions.md ADR-0015): the swapchain and factory
// class method tables are discovered from a throwaway device/swapchain, and the
// relevant slots are replaced with our detours. That is ReShade's `vtable_hook`
// mechanism and it never decodes a target prologue.
//
// Interposed slots:
//   - `IDXGISwapChain::Present`        (index 8)  -> frame callback
//   - `IDXGISwapChain::ResizeBuffers`  (index 13) -> resize callback
//   - `IDXGIFactory::CreateSwapChain`  (index 10) -> forwards (observes swaps)
//   - `IDXGIFactory2::CreateSwapChainForHwnd` (index 15) -> forwards
//
// The discovered class method tables are shared by every instance of their COM
// class, so a swapchain created before *or* after install is covered.

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
  bool installed_ = false;
};

}  // namespace cs2::features
