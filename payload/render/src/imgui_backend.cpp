#include "cs2/render/imgui_backend.hpp"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <type_traits>
#include <variant>
#include <vector>

#if defined(_WIN32)
#include <d3d11.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#endif

namespace cs2::render {
namespace {

#if defined(_WIN32)
auto to_u32(const Color& color) -> ImU32 {
  return IM_COL32(color.r, color.g, color.b, color.a);
}
#endif

}  // namespace

void ImGuiBackend::load_fonts() {
#if defined(_WIN32)
  ImFontConfig config;
  config.PixelSnapH = true;
  ImGui::GetIO().Fonts->AddFontDefault(&config);
  // Dear ImGui's atlas is antialiased; threshold the coverage so text renders
  // with hard edges.
  ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
  if (atlas.Build() && atlas.TexPixelsRGBA32 != nullptr) {
    auto* pixels = reinterpret_cast<unsigned char*>(atlas.TexPixelsRGBA32);
    for (int i = 0; i < atlas.TexWidth * atlas.TexHeight; ++i) {
      const unsigned char alpha = pixels[i * 4 + 3];
      pixels[i * 4 + 0] = 255;
      pixels[i * 4 + 1] = 255;
      pixels[i * 4 + 2] = 255;
      pixels[i * 4 + 3] = alpha >= 128 ? 255 : 0;
    }
  }
#endif
}

auto ImGuiBackend::initialize(const RenderContext& context) -> bool {
#if defined(_WIN32)
  if (context.device == nullptr || context.device_context == nullptr) {
    std::puts("[cs2-render] ImGui backend: missing device/context");
    return false;
  }
  device_ = context.device;
  device_context_ = context.device_context;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_context_ = ImGui::GetCurrentContext();
  ImGui::StyleColorsDark();
  // The menu is drawn with this font too; no .ini round-trip (the mapped module
  // has no stable working directory).
  ImGui::GetIO().IniFilename = nullptr;
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
auto ImGuiBackend::ensure_render_target(const RenderContext& context) -> bool {
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
        [&](const auto& item) -> auto {
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
            ImFont* font = ImGui::GetFont();
            const float size = font->FontSize;
            ImVec2 pos(item.x, item.y);
            if (item.centered) {
              const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, item.value.c_str());
              pos.x -= extent.x * 0.5F;
            }
            pos.x = std::floor(pos.x);
            pos.y = std::floor(pos.y);
            const ImU32 black = to_u32(Color{0, 0, 0, item.color.a});
            if (item.outline) {
              for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                  if (dx == 0 && dy == 0) {
                    continue;
                  }
                  list->AddText(font, size,
                                ImVec2(pos.x + static_cast<float>(dx), pos.y + static_cast<float>(dy)),
                                black, item.value.c_str());
                }
              }
            } else if (item.shadow) {
              list->AddText(font, size, ImVec2(pos.x + 1.0F, pos.y + 1.0F), black,
                            item.value.c_str());
            }
            list->AddText(font, size, pos, to_u32(item.color), item.value.c_str());
          } else if constexpr (std::is_same_v<T, FilledPoly>) {
            // A filled convex polygon: the solid studio hitbox capsules, the
            // radar's disc and the off-screen arrow's head.
            //
            // This branch used to be missing, so every `FilledPoly` was
            // silently dropped on the D3D11 path — the Vulkan layer renders
            // them (`overlay/src/renderer.cpp`), the payload did not.
            const std::size_t count = item.points.size();
            if (count < 3) {
              return;  // a polygon needs at least a triangle
            }
            // The common case is small (a capsule hull is <= 32 points, the
            // radar's disc 24), so it is filled from a stack buffer and the
            // hot path stays allocation-free.
            constexpr std::size_t k_inline_points = 64;
            if (count <= k_inline_points) {
              std::array<ImVec2, k_inline_points> points{};
              for (std::size_t i = 0; i < count; ++i) {
                points[i] = ImVec2(item.points[i].first, item.points[i].second);
              }
              list->AddConvexPolyFilled(points.data(), static_cast<int>(count), to_u32(item.color));
              return;
            }
            std::vector<ImVec2> points(count);
            for (std::size_t i = 0; i < count; ++i) {
              points[i] = ImVec2(item.points[i].first, item.points[i].second);
            }
            list->AddConvexPolyFilled(points.data(), static_cast<int>(count), to_u32(item.color));
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

  // The window thread's WndProc may have swapped the current context; make ours
  // current again before the frame.
  if (imgui_context_ != nullptr && ImGui::GetCurrentContext() != imgui_context_) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imgui_context_));
  }

  // Display-only: set the frame size directly (the Win32 platform backend is not
  // used). A positive size is required by ImGui_ImplDX11_NewFrame.
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(render_target_width_),
                          static_cast<float>(render_target_height_));
  io.DeltaTime = 1.0F / 60.0F;

  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();

  draw_commands(model.draw);
  if (model.show_hud) {
    draw_hud(model.hud);
  }
  // The in-process menu (workstream A) draws here, inside the frame.
  if (ui_callback_ != nullptr) {
    ui_callback_(ui_context_);
  }

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
    imgui_context_ = nullptr;
    release_render_target();
    initialized_ = false;
  }
#endif
}

}  // namespace cs2::render
