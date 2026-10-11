#pragma once

// UI backend abstraction (ADR-0007): the renderer (RmlUi primary, ImGui
// fallback) sits behind this so the hook layer stays UI-agnostic.

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

  // Called every frame (from the Present hook).
  virtual void render() = 0;

  // Called before unload.
  virtual void shutdown() = 0;
};

}  // namespace cs2::render
