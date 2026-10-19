#pragma once

#include <cstdint>

#include "cs2/render/ui_backend.hpp"

namespace cs2::render {

// ImGui overlay backend (ADR-0007 fallback): renders into the target's D3D11
// device via the vendored `imgui_impl_dx11` backend. A render target for the
// swapchain back buffer is cached and rebuilt when the swapchain is resized;
// `on_before_resize` releases it so the game's `ResizeBuffers` can proceed.
//
// The Win32 platform backend and its `WndProc` hook are deliberately not used:
// its window/mouse calls from the render thread deadlock under Wine, and the
// overlay is display-only (ADR-0015).
//
// One font is used everywhere (menu/UI and ESP labels): the stock Dear ImGui
// default. Its atlas is thresholded so glyphs render without antialiasing.
class ImGuiBackend final : public UiBackend {
 public:
  [[nodiscard]] bool initialize(const RenderContext& context) override;
  void render(const RenderContext& context, const OverlayModel& model) override;
  void on_before_resize() override;
  void shutdown() override;
  void set_ui_callback(UiCallback callback, void* context) override {
    ui_callback_ = callback;
    ui_context_ = context;
  }

  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

 private:
  void load_fonts();
  void release_render_target();
  [[nodiscard]] bool ensure_render_target(const RenderContext& context);
  void draw_commands(const DrawList& draw);
  static void draw_hud(const HudModel& hud);

  bool initialized_ = false;
  std::uint64_t frames_ = 0;
  void* device_ = nullptr;          // ID3D11Device*
  void* device_context_ = nullptr;  // ID3D11DeviceContext*
  void* render_target_ = nullptr;   // ID3D11RenderTargetView* (back buffer)
  void* imgui_context_ = nullptr;   // ImGuiContext* we created
  unsigned render_target_width_ = 0;
  unsigned render_target_height_ = 0;
  UiCallback ui_callback_ = nullptr;
  void* ui_context_ = nullptr;
};

}  // namespace cs2::render
