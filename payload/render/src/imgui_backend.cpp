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
#endif

namespace cs2::render {
namespace {

#if defined(_WIN32)
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
  if (context.device == nullptr || context.device_context == nullptr) {
    std::puts("[cs2-render] ImGui backend: missing device/context");
    return false;
  }
  device_ = context.device;
  device_context_ = context.device_context;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  load_fonts();

  // The Win32 platform backend is deliberately NOT used: its window/mouse calls
  // from the render thread can deadlock under Wine, and the overlay is
  // display-only. The frame size is fed to ImGui directly instead.
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

void ImGuiBackend::release_render_target() {
#if defined(_WIN32)
  if (render_target_ != nullptr) {
    static_cast<IUnknown*>(render_target_)->Release();
    render_target_ = nullptr;
  }
  render_target_width_ = 0;
  render_target_height_ = 0;
#endif
}

// Recreate the back-buffer render target when the swapchain size changes (or
// after a resize). Returning a non-null `render_target_` means it is current.
bool ImGuiBackend::ensure_render_target(const RenderContext& context) {
#if defined(_WIN32)
  if (context.swapchain == nullptr || context.device == nullptr) {
    return render_target_ != nullptr;
  }
  auto* chain = static_cast<IDXGISwapChain*>(context.swapchain);
  DXGI_SWAP_CHAIN_DESC desc{};
  if (chain->GetDesc(&desc) < 0) {
    return render_target_ != nullptr;
  }
  const unsigned width = desc.BufferDesc.Width;
  const unsigned height = desc.BufferDesc.Height;
  if (render_target_ != nullptr && width == render_target_width_ &&
      height == render_target_height_) {
    return true;  // still valid
  }
  release_render_target();

  ID3D11Texture2D* back_buffer = nullptr;
  if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                 reinterpret_cast<void**>(&back_buffer))) &&
      back_buffer != nullptr) {
    static_cast<ID3D11Device*>(context.device)
        ->CreateRenderTargetView(back_buffer, nullptr,
                                 reinterpret_cast<ID3D11RenderTargetView**>(&render_target_));
    back_buffer->Release();
    render_target_width_ = width;
    render_target_height_ = height;
  }
  return render_target_ != nullptr;
#else
  (void)context;
  return false;
#endif
}

void ImGuiBackend::on_before_resize() {
  // Called from the ResizeBuffers detour before the game's resize: release the
  // back-buffer reference so ResizeBuffers can proceed.
  release_render_target();
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
  if (!initialized_) {
    return;
  }
  auto* device_context = static_cast<ID3D11DeviceContext*>(context.device_context);
  if (device_context == nullptr || !ensure_render_target(context)) {
    return;
  }
  auto* render_target = static_cast<ID3D11RenderTargetView*>(render_target_);

  // Display-only: set the frame size directly (the Win32 platform backend is not
  // used). A positive size is required by ImGui_ImplDX11_NewFrame.
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(render_target_width_),
                          static_cast<float>(render_target_height_));
  io.DeltaTime = 1.0F / 60.0F;

  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();

  draw_commands(model.draw);
  draw_hud(model.hud);

  ImGui::Render();

  // ImGui_ImplDX11_RenderDrawData draws into the currently bound render target,
  // so bind the swapchain back buffer and restore the game's target afterwards.
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
  ++frames_;
#endif
}

void ImGuiBackend::shutdown() {
#if defined(_WIN32)
  if (initialized_) {
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    release_render_target();
    pixel_font_ = nullptr;
    initialized_ = false;
  }
#endif
}

}  // namespace cs2::render
