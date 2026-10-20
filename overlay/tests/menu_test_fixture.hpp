#pragma once

// The shared fixture for the menu's host tests.
//
// Dear ImGui runs headless, so the **real** `draw_menu()` can be driven with a
// real ImGui context and a real display size. The fixture owns that context
// and the helpers the cases share.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

#include "cs2/overlay/menu_pages.hpp"

namespace cs2::overlay::menu::test {

// A headless Dear ImGui context: the menu is drawn into it exactly as the
// renderers draw it.
struct ImGuiFixture {
  ImGuiFixture() {
    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.0F, 1080.0F);
    io.DeltaTime = 1.0F / 60.0F;
    // The menu's own settings are not saved to an .ini file (the window uses
    // `ImGuiWindowFlags_NoSavedSettings`), and neither does the test: it would
    // drop an `imgui.ini` into the working directory.
    io.IniFilename = nullptr;
    // The font atlas is built by the render backends; there is none here, so
    // the test builds it (the font is never drawn, but `NewFrame` insists).
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
  }
  ~ImGuiFixture() {
    ImGui::DestroyContext(context);
  }
  ImGuiFixture(const ImGuiFixture&) = delete;
  ImGuiFixture& operator=(const ImGuiFixture&) = delete;

  ImGuiContext* context = nullptr;
};

// The shared UI state outlives the frame, so a test that draws the menu with a
// fresh ImGui context has to reset it: in the program there is one menu, in
// the test there is one per case.
inline void reset_ui_state() noexcept {
  UiState& state = ui_state();
  state.geometry = WindowGeometry{};
  state.geometry_loaded = false;
  state.geometry_applied = false;
  state.geometry_dirty = false;
  state.save_timer = 0;
  state.section = 0;
}

// The menu's own window.
[[nodiscard]] inline auto menu_window() noexcept -> ImGuiWindow* {
  ImGuiContext* context = ImGui::GetCurrentContext();
  for (int index = 0; index < context->Windows.Size; ++index) {
    ImGuiWindow* window = context->Windows[index];
    if (window->ParentWindow == nullptr && std::strcmp(window->Name, k_window_title) == 0) {
      return window;
    }
  }
  return nullptr;
}

// A child of the menu's window.
//
// ImGui names a child "<parent>/<name>_<id>", so the child is found by the
// name the header gives it.
[[nodiscard]] inline auto menu_child(ImGuiWindow* parent, const char* name) noexcept
    -> ImGuiWindow* {
  if (parent == nullptr) {
    return nullptr;
  }
  ImGuiContext* context = ImGui::GetCurrentContext();
  for (int index = 0; index < context->Windows.Size; ++index) {
    ImGuiWindow* window = context->Windows[index];
    if (window->ParentWindow == parent && std::strstr(window->Name, name) != nullptr) {
      return window;
    }
  }
  return nullptr;
}

// Draw one frame of the menu and return its window.
//
// `requested_size` is the size the "user" asks for before the menu is drawn
// (the API call the window's own size is set with); it is null when the size
// is left alone.
[[nodiscard]] inline auto draw_frame(wire::Settings& settings,
                                     Renderer renderer = Renderer::payload,
                                     const ImVec2* requested_size = nullptr) noexcept
    -> ImGuiWindow* {
  ImGui::NewFrame();
  if (requested_size != nullptr) {
    ImGui::SetNextWindowSize(*requested_size, ImGuiCond_Always);
  }
  (void)draw_menu(settings, "host test", renderer);
  ImGui::Render();
  return menu_window();
}

// The temporary directory the cases write their files in.
//
// It is **per process**, not the shared system temp directory: the menu
// persists its window geometry to a side file next to the settings file, and
// `meson test` runs the suites **concurrently** - two suites sharing one
// directory would race on that file (and one of them would write into the
// operator's real configuration directory).
[[nodiscard]] inline auto temp_directory() -> std::filesystem::path {
  static const std::filesystem::path path = [] {
    std::error_code error;
    std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error) {
      base = std::filesystem::path(".");
    }
    // The stamp is taken once, on the first call, so every case in this
    // process shares the directory and no two processes share one.
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::filesystem::path directory = base / ("cs2-menu-test-" + std::to_string(stamp));
    std::filesystem::create_directories(directory, error);
    return directory;
  }();
  return path;
}

}  // namespace cs2::overlay::menu::test
