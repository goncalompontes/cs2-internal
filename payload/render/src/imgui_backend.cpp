#include "cs2/render/imgui_backend.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// Declared out-of-band by imgui_impl_win32.h (it is `#if 0`'d there to avoid
// dragging <windows.h> into the header).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam,
                                                            LPARAM lparam);
#endif

namespace cs2::render {
namespace {

#if defined(_WIN32)
WNDPROC g_original_wndproc = nullptr;

LRESULT __stdcall wndproc_detour(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) {
    return 1;
  }
  return CallWindowProc(g_original_wndproc, hwnd, message, wparam, lparam);
}
#endif

}  // namespace

bool ImGuiBackend::initialize(const RenderContext& context) {
#if defined(_WIN32)
  if (context.device == nullptr || context.device_context == nullptr ||
      context.swapchain == nullptr) {
    std::puts("[cs2-render] ImGui backend: missing device/context/swapchain");
    return false;
  }
  device_ = context.device;
  device_context_ = context.device_context;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplWin32_Init(context.hwnd);
  if (!ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(context.device),
                           static_cast<ID3D11DeviceContext*>(context.device_context))) {
    std::puts("[cs2-render] ImGui_ImplDX11_Init failed");
    return false;
  }

  // Create a render target for the swapchain back buffer (once), as the
  // reference overlay does.
  auto* chain = static_cast<IDXGISwapChain*>(context.swapchain);
  ID3D11Texture2D* back_buffer = nullptr;
  if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                 reinterpret_cast<void**>(&back_buffer))) &&
      back_buffer != nullptr) {
    static_cast<ID3D11Device*>(context.device)
        ->CreateRenderTargetView(back_buffer, nullptr,
                                 reinterpret_cast<ID3D11RenderTargetView**>(&render_target_));
    back_buffer->Release();
  }
  if (render_target_ == nullptr) {
    std::puts("[cs2-render] failed to create the back-buffer render target");
    return false;
  }

  // Hook the window procedure so ImGui receives mouse/keyboard input.
  if (context.hwnd != nullptr && g_original_wndproc == nullptr) {
    g_original_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(static_cast<HWND>(context.hwnd), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&wndproc_detour)));
  }

  initialized_ = true;
  std::puts("[cs2-render] ImGui backend initialized");
  return true;
#else
  (void)context;
  return false;
#endif
}

void ImGuiBackend::render(const RenderContext& context, const HudModel& model) {
#if defined(_WIN32)
  if (!initialized_ || render_target_ == nullptr) {
    return;
  }
  auto* device_context = static_cast<ID3D11DeviceContext*>(context.device_context);
  if (device_context == nullptr) {
    return;
  }

  DXGI_SWAP_CHAIN_DESC desc{};
  if (context.swapchain != nullptr &&
      static_cast<IDXGISwapChain*>(context.swapchain)->GetDesc(&desc) >= 0) {
    render_target_width_ = desc.BufferDesc.Width;
    render_target_height_ = desc.BufferDesc.Height;
  }

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(model.title.empty() ? "cs2-internal" : model.title.c_str())) {
    for (const HudSection& section : model.sections) {
      if (section.title.empty()) {
        for (const HudLine& line : section.lines) {
          ImGui::Text("%s: %s", line.label.c_str(), line.value.c_str());
        }
        continue;
      }
      if (ImGui::CollapsingHeader(section.title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const HudLine& line : section.lines) {
          ImGui::Text("%s: %s", line.label.c_str(), line.value.c_str());
        }
      }
    }
  }
  ImGui::End();

  ImGui::Render();

  // ImGui_ImplDX11_RenderDrawData draws into the currently bound render target.
  device_context->OMSetRenderTargets(
      1, reinterpret_cast<ID3D11RenderTargetView* const*>(&render_target_), nullptr);
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
    if (render_target_ != nullptr) {
      static_cast<ID3D11RenderTargetView*>(render_target_)->Release();
      render_target_ = nullptr;
    }
    initialized_ = false;
  }
#endif
}

}  // namespace cs2::render
