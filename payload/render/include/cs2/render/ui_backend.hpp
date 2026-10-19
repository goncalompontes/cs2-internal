#pragma once

// UI backend abstraction (ADR-0007): the renderer (RmlUi primary, ImGui
// fallback) sits behind this so the hook layer stays UI-agnostic.

#include "cs2/render/overlay_model.hpp"

namespace cs2::render {

// Opaque handles to the target's render surface, filled in by the Present hook.
struct RenderContext {
  void* device = nullptr;          // ID3D11Device*
  void* device_context = nullptr;  // ID3D11DeviceContext*
  void* hwnd = nullptr;            // HWND
  void* swapchain = nullptr;       // IDXGISwapChain*
};

class UiBackend {
 public:
  virtual ~UiBackend() = default;

  // Called once when the render surface is ready. Returns false on failure.
  [[nodiscard]] virtual bool initialize(const RenderContext& context) = 0;

  // Called every frame (from the Present hook) with the live render surface and
  // the model to draw. The surface may be recreated between frames, so callers
  // pass it each time rather than the backend caching it.
  virtual void render(const RenderContext& context, const OverlayModel& model) = 0;

  // Called from the `IDXGISwapChain::ResizeBuffers` detour, *before* the game's
  // resize runs. Any held back-buffer view (which keeps a reference and would
  // otherwise fail the resize) must be released here.
  virtual void on_before_resize() {}

  // Called before unload.
  virtual void shutdown() = 0;

  // An optional overlay-UI hook, invoked once per frame between `NewFrame` and
  // `Render` (so it may add ImGui windows). The in-process menu registers here
  // (workstream A); the display-only paths leave it unset.
  using UiCallback = void (*)(void* context);
  virtual void set_ui_callback(UiCallback callback, void* context) {
    (void)callback;
    (void)context;
  }
};

}  // namespace cs2::render
