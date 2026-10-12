#pragma once

#include <cstdint>

#include "cs2/render/ui_backend.hpp"

namespace cs2::render {

// ImGui overlay backend (ADR-0007 fallback): renders into the target's D3D11
// device via the vendored imgui_impl_dx11 / imgui_impl_win32 backends.
class ImGuiBackend final : public UiBackend {
 public:
  [[nodiscard]] bool initialize(const RenderContext& context) override;
  void render() override;
  void shutdown() override;

  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

 private:
  bool initialized_ = false;
  std::uint64_t frames_ = 0;
  void* device_ = nullptr;          // ID3D11Device*
  void* device_context_ = nullptr;  // ID3D11DeviceContext*
  void* swapchain_ = nullptr;       // IDXGISwapChain*
  unsigned render_target_width_ = 0;
  unsigned render_target_height_ = 0;
};

}  // namespace cs2::render
