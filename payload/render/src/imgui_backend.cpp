#include "cs2/render/imgui_backend.hpp"

#include <cfloat>
#include <cstdio>
#include <type_traits>
#include <variant>

#include "cs2/render/esp_font.hpp"

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

ImU32 to_u32(const Color& color) {
  return IM_COL32(color.r, color.g, color.b, color.a);
}

// Pixel size the embedded pixel font is rasterized at (Silkscreen is designed
// on an 8px grid, so 16 keeps the scaling integral).
constexpr float k_esp_font_size = 16.0F;
#endif

}  // namespace

void ImGuiBackend::load_fonts() {
#if defined(_WIN32)
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the byte array is static
  config.PixelSnapH = true;
  config.OversampleH = 1;
  config.OversampleV = 1;
  pixel_font_ = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char*>(k_esp_font_ttf), static_cast<int>(k_esp_font_ttf_size),
      k_esp_font_size, &config);
  if (pixel_font_ == nullptr) {
    std::puts("[cs2-render] failed to load the ESP pixel font");
  }
#endif
}

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
  load_fonts();

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

void ImGuiBackend::draw_commands(const DrawList& draw) {
#if defined(_WIN32)
  if (draw.empty()) {
    return;
  }
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  for (const DrawCommand& command : draw.commands) {
    std::visit(
        [&](const auto& item) {
          using T = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<T, Rect>) {
            list->AddRect(ImVec2(item.x0, item.y0), ImVec2(item.x1, item.y1), to_u32(item.color),
                          0.0F, 0, item.thickness);
          } else if constexpr (std::is_same_v<T, FilledRect>) {
            list->AddRectFilled(ImVec2(item.x0, item.y0), ImVec2(item.x1, item.y1),
                                to_u32(item.color));
          } else if constexpr (std::is_same_v<T, Line>) {
            list->AddLine(ImVec2(item.x0, item.y0), ImVec2(item.x1, item.y1), to_u32(item.color),
                          item.thickness);
          } else if constexpr (std::is_same_v<T, Text>) {
            ImFont* font = (item.font == Font::pixel && pixel_font_ != nullptr)
                               ? static_cast<ImFont*>(pixel_font_)
                               : ImGui::GetFont();
            const float size = font->FontSize;
            ImVec2 pos(item.x, item.y);
            if (item.centered) {
              const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, item.value.c_str());
              pos.x -= extent.x * 0.5F;
            }
            if (item.shadow) {
              list->AddText(font, size, ImVec2(pos.x + 1.0F, pos.y + 1.0F),
                            to_u32(Color{0, 0, 0, item.color.a}), item.value.c_str());
            }
            list->AddText(font, size, pos, to_u32(item.color), item.value.c_str());
          }
        },
        command);
  }
#endif
}

void ImGuiBackend::draw_hud(const HudModel& hud) {
#if defined(_WIN32)
  ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(hud.title.empty() ? "cs2-internal" : hud.title.c_str())) {
    for (const HudSection& section : hud.sections) {
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
#endif
}

void ImGuiBackend::render(const RenderContext& context, const OverlayModel& model) {
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

  draw_commands(model.draw);
  draw_hud(model.hud);

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
    pixel_font_ = nullptr;
    initialized_ = false;
  }
#endif
}

}  // namespace cs2::render
