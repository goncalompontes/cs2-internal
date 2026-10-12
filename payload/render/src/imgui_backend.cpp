#include "cs2/render/imgui_backend.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
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

  if (!ImGui_ImplWin32_Init(context.hwnd)) {
    std::puts("[cs2-render] ImGui_ImplWin32_Init failed");
    return false;
  }
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
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("cs2-internal");
  ImGui::Text("frames presented: %llu", static_cast<unsigned long long>(frames_));
  ImGui::Text("pid: %lu", static_cast<unsigned long>(GetCurrentProcessId()));
  ImGui::End();

  ImGui::Render();

  // ImGui_ImplDX11_RenderDrawData draws into the *currently bound* render
  // target, so bind the swapchain back buffer first: the game's Present leaves
  // an unrelated target bound, and ImGui would otherwise draw nowhere.
  auto* chain = static_cast<IDXGISwapChain*>(swapchain_);
  auto* device = static_cast<ID3D11Device*>(device_);
  auto* device_context = static_cast<ID3D11DeviceContext*>(device_context_);
  if (render_target_ == nullptr && chain != nullptr && device != nullptr) {
    ID3D11Texture2D* back_buffer = nullptr;
    if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                   reinterpret_cast<void**>(&back_buffer))) &&
        back_buffer != nullptr) {
      device->CreateRenderTargetView(back_buffer, nullptr,
                                     reinterpret_cast<ID3D11RenderTargetView**>(&render_target_));
      back_buffer->Release();
    }
  }
  if (render_target_ != nullptr && device_context != nullptr) {
    device_context->OMSetRenderTargets(
        1, reinterpret_cast<ID3D11RenderTargetView* const*>(&render_target_), nullptr);
  }
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  ++frames_;
#endif
}

void ImGuiBackend::shutdown() {
#if defined(_WIN32)
  if (initialized_) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
  if (render_target_ != nullptr) {
    static_cast<IUnknown*>(render_target_)->Release();
    render_target_ = nullptr;
  }
#endif
}

}  // namespace cs2::render
