#pragma once

// The in-process overlay menu (workstream A: retire the Vulkan layer's menu).
//
// With the DXGI Present hook the payload renders the ESP + HUD itself, so it also
// owns the menu: this class keeps the settings block (`wire::Settings`, ADR-0021),
// captures input by subclassing the game window's WndProc, releases the cursor
// while open, and draws the menu with ImGui on the game's Present thread.
//
// The layer's `overlay/src/menu.cpp` is the reference; the logic is the same, but
// the settings live in-process (no shared memory) and the ImGui context is the
// payload's own (created by `render::ImGuiBackend`).

#include <cstddef>
#include <cstdint>

#include "cs2/features/overlay.hpp"
#include "cs2/features/settings.hpp"

namespace cs2::features {

class Menu {
 public:
  explicit Menu(Overlay& overlay) noexcept : overlay_(&overlay) {}

  // The payload's env defaults seed the menu's initial state (`CS2_PAYLOAD_MOVEMENT`
  // / `CS2_PAYLOAD_RAGE`). The menu then owns the settings.
  void set_movement_defaults(bool auto_strafe, bool bhop) noexcept;
  void set_rage_defaults(bool on, int key, bool fire) noexcept;

  // Receives the settings block **every frame** so the payload features follow the
  // menu (the menu is the single owner, mirroring the layer's shm sink).
  using SettingsSink = void (*)(void* context, const cs2::overlay::wire::Settings& settings);
  void set_settings_sink(SettingsSink sink, void* context) noexcept {
    settings_sink_ = sink;
    settings_sink_context_ = context;
  }

  // Per-present-frame: poll the toggle key, keep the game's WndProc subclassed,
  // release the cursor while open, and push the settings to the payload features.
  // `hwnd` is the game's swapchain window (from `RenderContext`); `in_game` gates
  // whether the menu may be open. Safe to call before the backend is ready.
  void tick(bool in_game, void* hwnd) noexcept;

  // Draw the menu window during the ImGui frame. Registered as the backend's UI
  // callback; a no-op unless the menu is open.
  void draw() noexcept;
  static void ui_entry(void* context) noexcept;

  void shutdown() noexcept;

  [[nodiscard]] const cs2::overlay::wire::Settings& settings() const noexcept {
    return settings_;
  }
  [[nodiscard]] cs2::overlay::wire::Settings& settings() noexcept { return settings_; }
  [[nodiscard]] bool open() const noexcept { return open_; }

 private:
  void set_open(bool open, void* hwnd) noexcept;
  void poll_toggle(bool in_game) noexcept;
  // Push the settings to the overlay (ESP config + HUD) and the feature sink.
  void apply() noexcept;
  // Block the game's raw input (mouse motion + buttons) while the menu is open by
  // hooking `user32!GetRawInputBuffer` (SDL3, which CS2's input goes through, reads
  // raw input there rather than through the WndProc's WM_INPUT). Installed once.
  void install_raw_input_block() noexcept;
  // The game window's subclassed procedure (the reference is
  // `overlay/src/menu.cpp :: hooked_wndproc`). A static member so it can reach the
  // private input state. `LRESULT CALLBACK (HWND, UINT, WPARAM, LPARAM)` is spelled
  // with fixed-width types to avoid `<windows.h>` in this header.
  static std::intptr_t __stdcall window_proc(void* hwnd, unsigned message, std::size_t wparam,
                                             std::intptr_t lparam);

  // The one live menu; the C-style WndProc has no `this`.
  static Menu* s_active_;

  Overlay* overlay_;
  cs2::overlay::wire::Settings settings_ = default_wire_settings();
  SettingsSink settings_sink_ = nullptr;
  void* settings_sink_context_ = nullptr;
  void* window_ = nullptr;        // HWND
  void* old_proc_ = nullptr;      // WNDPROC (opaque)
  void* imgui_context_ = nullptr; // ImGuiContext* (opaque)
  bool raw_input_hook_installed_ = false;
  bool open_ = false;
  bool cursor_shown_ = false;
  bool previous_toggle_ = false;
  bool force_open_ = false;
  bool marker_opened_ = false;
  bool initialised_ = false;
  unsigned marker_tick_ = 0;
};

}  // namespace cs2::features
