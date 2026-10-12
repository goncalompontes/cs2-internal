#include "cs2/render/imgui_backend.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#endif

namespace cs2::render {

bool ImGuiBackend::initialize(const RenderContext& context) {
#if defined(_WIN32)
  if (context.device == nullptr || context.device_context == nullptr) {
    std::puts("[cs2-render] ImGui backend: no device/context");
    return false;
  }
  device_ = context.device;
  device_context_ = context.device_context;
  swapchain_ = context.swapchain;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  if (!ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(context.device),
                           static_cast<ID3D11DeviceContext*>(context.device_context))) {
    std::puts("[cs2-render] ImGui_ImplDX11_Init failed");
    return false;
  }
  initialized_ = true;
  std::puts("[cs2-render] ImGui backend initialized");
  return true;
#else
  (void)context;
  return false;
#endif
}

void ImGuiBackend::render() {
#if defined(_WIN32)
  if (!initialized_) {
    return;
  }
  auto* chain = static_cast<IDXGISwapChain*>(swapchain_);
  auto* device = static_cast<ID3D11Device*>(device_);
  auto* device_context = static_cast<ID3D11DeviceContext*>(device_context_);
  if (chain == nullptr || device == nullptr || device_context == nullptr) {
    return;
  }

  // Acquire the back buffer for this frame only. Holding a back-buffer
  // reference across frames (a cached RTV) blocks the game's ResizeBuffers /
  // present, which is what froze the game on a UI state change.
  ID3D11Texture2D* back_buffer = nullptr;
  if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&back_buffer))) ||
      back_buffer == nullptr) {
    return;
  }
  ID3D11RenderTargetView* render_target = nullptr;
  const HRESULT create = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
  DXGI_SWAP_CHAIN_DESC desc{};
  if (render_target_width_ == 0 && chain->GetDesc(&desc) >= 0) {
    render_target_width_ = desc.BufferDesc.Width;
    render_target_height_ = desc.BufferDesc.Height;
  }
  back_buffer->Release();
  if (FAILED(create) || render_target == nullptr) {
    return;
  }

  // Display-only overlay: set the frame size directly instead of using the
  // Win32 platform backend (whose window/mouse calls from the render thread can
  // deadlock under Wine).
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize =
      ImVec2(static_cast<float>(render_target_width_), static_cast<float>(render_target_height_));
  io.DeltaTime = 1.0f / 60.0f;

  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("cs2-internal");
  ImGui::Text("frames presented: %llu", static_cast<unsigned long long>(frames_));
  ImGui::Text("pid: %lu", static_cast<unsigned long>(GetCurrentProcessId()));
  ImGui::End();

  ImGui::Render();

  // ImGui_ImplDX11_RenderDrawData draws into the *currently bound* render
  // target, so bind the back buffer and restore the game's target afterwards.
  ID3D11RenderTargetView* saved_rtv = nullptr;
  ID3D11DepthStencilView* saved_dsv = nullptr;
  device_context->OMGetRenderTargets(1, &saved_rtv, &saved_dsv);
  device_context->OMSetRenderTargets(1, &render_target, nullptr);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  device_context->OMSetRenderTargets(1, &saved_rtv, saved_dsv);
  if (saved_rtv != nullptr) {
    saved_rtv->Release();
  }
  if (saved_dsv != nullptr) {
    saved_dsv->Release();
  }
  render_target->Release();
  ++frames_;
#endif
}

void ImGuiBackend::shutdown() {
#if defined(_WIN32)
  if (initialized_) {
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
#endif
}

}  // namespace cs2::render
