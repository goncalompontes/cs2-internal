// Host test for the menu window's **geometry** (`cs2/overlay/menu_pages.hpp`).
//
// Dear ImGui runs headless, so the **real** `draw_menu()` is driven here with
// a real ImGui context and a real display size.
//
// The regression this file exists for: the window's size constraint passed a
// zero maximum. Dear ImGui reads the second argument of
// `SetNextWindowSizeConstraints` as a **real** maximum
// (`ImClamp(size, min, max)` in `imgui.cpp`), so `ImClamp(700, 520, 0) == 0`
// and the window was clamped to ImGui's 32x32 floor: the whole menu was
// invisible and unclickable, and the persisted geometry was poisoned with
// "32.0 32.0".

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "cs2/overlay/wire.hpp"

#include "menu_test_fixture.hpp"

namespace {

using cs2::overlay::menu::test::draw_frame;
using cs2::overlay::menu::test::ImGuiFixture;
using cs2::overlay::menu::test::menu_child;
using cs2::overlay::menu::test::reset_ui_state;
using cs2::overlay::menu::test::temp_directory;
using cs2::overlay::wire::Settings;

constexpr float k_epsilon = 0.01F;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

// Write the side file the geometry is persisted in.
auto write_geometry(const std::filesystem::path& path, float x, float y, float width, float height)
    -> void {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  assert(file != nullptr);
  std::fprintf(file, "%.1f %.1f %.1f %.1f\n", x, y, width, height);
  std::fclose(file);
}

// Read the side file the geometry is persisted in.
auto read_geometry(const std::filesystem::path& path, float values[4]) -> bool {
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  const int read = std::fscanf(file, "%f %f %f %f", &values[0], &values[1], &values[2], &values[3]);
  std::fclose(file);
  return read == 4;
}

// The settings block the menu is drawn with: its published path points at the
// temporary directory, so the geometry's side file lands there.
auto settings_for(const std::filesystem::path& config) -> Settings {
  Settings settings = cs2::overlay::wire::default_settings();
  std::snprintf(settings.menu_config_path, sizeof(settings.menu_config_path), "%s",
                config.string().c_str());
  return settings;
}

// The menu opens at the size the side file holds.
void test_window_opens_at_the_persisted_size() {
  const std::filesystem::path directory = temp_directory();
  const std::filesystem::path config = directory / "cs2-menu-geometry-test.cfg";
  const std::filesystem::path geometry = directory / "menu-window.cfg";
  std::error_code error;
  std::filesystem::remove(geometry, error);
  write_geometry(geometry, 300.0F, 200.0F, 700.0F, 500.0F);

  Settings settings = settings_for(config);
  reset_ui_state();
  ImGuiFixture fixture;
  ImGuiWindow* window = draw_frame(settings);
  assert(window != nullptr);
  // The persisted size...
  assert(nearly(window->Size.x, 700.0F));
  assert(nearly(window->Size.y, 500.0F));
  assert(nearly(window->SizeFull.x, 700.0F));
  assert(nearly(window->SizeFull.y, 500.0F));
  // ... and the persisted position.
  assert(nearly(window->Pos.x, 300.0F));
  assert(nearly(window->Pos.y, 200.0F));

  // The content is not collapsed: the sidebar and the page have room.
  ImGuiWindow* sidebar = menu_child(window, "##sidebar");
  ImGuiWindow* content = menu_child(window, "##content");
  assert(sidebar != nullptr);
  assert(content != nullptr);
  assert(sidebar->Size.x > 0.0F);
  assert(sidebar->Size.y > 0.0F);
  assert(content->Size.x > 0.0F);
  assert(content->Size.y > 0.0F);

  std::filesystem::remove(geometry, error);
}

// The window can be made bigger than its minimum: the constraint is a
// **minimum**, not a maximum.
void test_window_can_be_resized() {
  const std::filesystem::path directory = temp_directory();
  const std::filesystem::path config = directory / "cs2-menu-resize-test.cfg";
  const std::filesystem::path geometry = directory / "menu-window.cfg";
  std::error_code error;
  std::filesystem::remove(geometry, error);
  write_geometry(geometry, 300.0F, 200.0F, 700.0F, 500.0F);

  Settings settings = settings_for(config);
  reset_ui_state();
  ImGuiFixture fixture;
  ImGuiWindow* window = draw_frame(settings);
  assert(window != nullptr);
  assert(nearly(window->Size.x, 700.0F));

  // The user drags the window bigger.
  const ImVec2 bigger(900.0F, 700.0F);
  window = draw_frame(settings, cs2::overlay::menu::Renderer::payload, &bigger);
  assert(window != nullptr);
  assert(nearly(window->Size.x, 900.0F));
  assert(nearly(window->Size.y, 700.0F));

  // The new geometry is written to the side file once the drag has settled.
  for (int frame = 0; frame < 60; ++frame) {
    window = draw_frame(settings);
  }
  assert(window != nullptr);
  float values[4] = {};
  assert(read_geometry(geometry, values));
  assert(nearly(values[0], 300.0F));
  assert(nearly(values[1], 200.0F));
  assert(nearly(values[2], 900.0F));
  assert(nearly(values[3], 700.0F));

  std::filesystem::remove(geometry, error);
}

// The window is never smaller than the minimum.
void test_window_keeps_the_minimum() {
  const std::filesystem::path directory = temp_directory();
  const std::filesystem::path config = directory / "cs2-menu-minimum-test.cfg";
  const std::filesystem::path geometry = directory / "menu-window.cfg";
  std::error_code error;
  std::filesystem::remove(geometry, error);
  write_geometry(geometry, 300.0F, 200.0F, 100.0F, 100.0F);

  Settings settings = settings_for(config);
  reset_ui_state();
  ImGuiFixture fixture;
  ImGuiWindow* window = draw_frame(settings);
  assert(window != nullptr);
  assert(window->Size.x >= cs2::overlay::menu::k_min_width);
  assert(window->Size.y >= cs2::overlay::menu::k_min_height);

  std::filesystem::remove(geometry, error);
}

}  // namespace

auto main() -> int {
  test_window_opens_at_the_persisted_size();
  test_window_can_be_resized();
  test_window_keeps_the_minimum();
  std::puts("menu-geometry: ok");
  return 0;
}
