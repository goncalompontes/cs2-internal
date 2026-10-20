#pragma once

// The in-process overlay menu (workstream A: retire the Vulkan layer's menu).
//
// With the DXGI Present hook the payload renders the ESP + HUD itself, so it also
// owns the menu: this class keeps the settings block (`wire::Settings`, ADR-0021),
// captures input by subclassing the game window's WndProc, releases the cursor
// while open, and draws the menu with ImGui on the game's Present thread.
//
// The menu's *content* is not written here: the pages, the widgets and the
// keybind capture live in the shared header `cs2/overlay/menu_pages.hpp`, which
// the Vulkan layer's fallback menu (`overlay/src/menu.cpp`) draws too. This
// class owns the plumbing only: the window subclass, the raw-input block, the
// toggle key and the settings block.

#include <cstddef>
#include <cstdint>
#include <mutex>

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

  [[nodiscard]] bool open() const noexcept { return open_; }

  // --- The live settings block ------------------------------------------------
  //
  // The block has **two writers**: the menu's own widgets (the render thread)
  // and the control channel's `settings` command (the control thread). Every
  // access therefore goes through this class's lock: a plain 441-byte copy
  // from two threads is a data race, and a whole-block replacement loses the
  // changes the other thread made in between.
  //
  // There is deliberately **no** accessor that hands out a reference to the
  // block: a reference cannot be handed out while another thread may write it.

  // A copy of the block as it stands.
  [[nodiscard]] auto settings_snapshot() const noexcept -> cs2::overlay::wire::Settings;

  // Merge the fields a control-side change carries into the live block, and
  // return the block as it stands afterwards.
  [[nodiscard]] auto merge_settings(const cs2::overlay::wire::SettingsPatch& patch) noexcept
      -> cs2::overlay::wire::Settings;

  // Replace the whole block (a configuration load or a reset: those *are* a
  // replacement, not a merge).
  void replace_settings(const cs2::overlay::wire::Settings& values) noexcept;

  // The settings file's path (the payload publishes where the file lives; the
  // menu shows it read-only).
  void set_config_path(const char* path) noexcept;

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
  // The lock around `settings_`: the menu's widgets write it on the render
  // thread and the control channel merges a change into it on the control
  // thread.
  mutable std::mutex settings_mutex_;
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
  // The keybind capture's counter when the toggle key was last polled: a
  // capture that took a key owns it, so the menu must not toggle as well.
  std::uint32_t capture_epoch_ = 0;
};

}  // namespace cs2::features
