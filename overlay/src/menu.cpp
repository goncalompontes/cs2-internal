#include "menu.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include <imgui.h>

#include "cs2/overlay/menu_pages.hpp"
#include "log.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>
#endif

namespace cs2::overlay::menu {
namespace {

#if defined(_WIN32)

std::atomic<bool> g_menu_open{false};
HWND g_window = nullptr;
WNDPROC g_old_proc = nullptr;
bool g_cursor_shown = false;
bool g_previous_toggle = false;
bool g_force_open = false;
ImGuiContext* g_context = nullptr;
// The keybind capture's counter when the toggle key was last polled: a
// capture that took a key owns it, so the menu must not toggle as well.
std::uint32_t g_capture_epoch = 0;

// Debug aid: a marker file (`<payload dir>\overlay-menu-open`) forces the menu
// open as soon as the game is in a map. Unlike an environment variable this
// cannot be lost to Steam's cached environment.
auto menu_marker_exists() noexcept -> bool {
  char base[MAX_PATH] = {};
  char path[MAX_PATH] = {};
  if (GetEnvironmentVariableA("CS2_PAYLOAD_DIR", base, sizeof(base)) != 0) {
    std::snprintf(path, sizeof(path), "%s\\overlay-menu-open", base);
  } else if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) != 0) {
    std::snprintf(path, sizeof(path), "%s\\cs2-internal\\overlay-menu-open", base);
  } else {
    return false;
  }
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

auto owns_window(HWND window) -> bool {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  return pid == GetCurrentProcessId();
}

struct WindowSearch {
  HWND best = nullptr;
  LONG area = 0;
};

BOOL CALLBACK largest_process_window(HWND window, LPARAM param) {
  auto* search = reinterpret_cast<WindowSearch*>(param);
  if (IsWindowVisible(window) == FALSE || owns_window(window) == FALSE) {
    return TRUE;
  }
  RECT rect{};
  if (GetClientRect(window, &rect) == FALSE) {
    return TRUE;
  }
  const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
  if (area <= 0) {
    return TRUE;
  }
  if (search->best == nullptr || area > search->area) {
    search->best = window;
    search->area = area;
  }
  return TRUE;
}

// The window we draw into and capture input from: the foreground window when it
// belongs to this process, else the largest visible window of this process.
// (In exclusive fullscreen `GetForegroundWindow` can be null, and the game's
// main window may have an owner, so neither test is required.)
auto find_game_window() -> HWND {
  const HWND foreground = GetForegroundWindow();
  if (foreground != nullptr && owns_window(foreground)) {
    return foreground;
  }
  WindowSearch search{};
  EnumWindows(&largest_process_window, reinterpret_cast<LPARAM>(&search));
  return search.best;
}

LRESULT CALLBACK hooked_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

void set_menu_open(bool open) {
  if (open == g_menu_open.load(std::memory_order_relaxed)) {
    return;
  }
  if (open) {
    // Draw the menu regardless; input capture is best-effort and reported.
    g_menu_open.store(true, std::memory_order_relaxed);
    g_context = ImGui::GetCurrentContext();
    g_window = find_game_window();
    if (g_window == nullptr || g_context == nullptr) {
      g_window = nullptr;
      g_old_proc = nullptr;
      overlay_log("cs2-overlay: menu open, but no game window found (no input capture)");
      return;
    }
    g_old_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooked_wndproc)));
    // Seed the cursor position and focus so the menu is immediately usable.
    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE) {
      ScreenToClient(g_window, &cursor);
      ImGui::GetIO().AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    }
    ImGui::GetIO().AddFocusEvent(true);
    // The key state is dropped with the menu: a key held when the menu closed
    // would otherwise stay "down" in ImGui's queue.
    ImGui::GetIO().ClearInputKeys();
    if (!g_cursor_shown) {
      ShowCursor(TRUE);
      g_cursor_shown = true;
    }
    // Release the mouse clip the game holds; without this the OS cursor stays trapped in
    // the window and the user must open the game's own menu first.
    ClipCursor(nullptr);
    g_menu_open.store(true, std::memory_order_relaxed);
    overlay_log("cs2-overlay: menu opened (hwnd %p)", static_cast<void*>(g_window));
  } else {
    g_menu_open.store(false, std::memory_order_relaxed);
    // An armed keybind capture belongs to the closed menu: drop it, so the
    // next key press is not swallowed by a capture nobody can see.
    cancel_capture();
    if (g_window != nullptr && g_old_proc != nullptr) {
      SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_old_proc));
    }
    g_window = nullptr;
    g_old_proc = nullptr;
    if (g_cursor_shown) {
      ShowCursor(FALSE);
      g_cursor_shown = false;
    }
    overlay_log("cs2-overlay: menu closed");
  }
}

LRESULT CALLBACK hooked_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  const WNDPROC previous_proc = g_old_proc;
  if (!g_menu_open.load(std::memory_order_relaxed) || previous_proc == nullptr) {
    return CallWindowProcW(previous_proc != nullptr ? previous_proc : DefWindowProcW, window, message,
                           wparam, lparam);
  }

  // Dear ImGui's input queue is thread-safe, but its event functions need their
  // context current; this thread (the game's window thread) has none.
  ImGuiContext* const previous_context = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(g_context);
  ImGuiIO& io = ImGui::GetIO();

  bool swallow = true;
  switch (message) {
    case WM_MOUSEMOVE:
      io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(lparam)),
                          static_cast<float>(GET_Y_LPARAM(lparam)));
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      io.AddMouseButtonEvent(0, true);
      break;
    case WM_LBUTTONUP:
      io.AddMouseButtonEvent(0, false);
      break;
    case WM_RBUTTONDOWN:
      io.AddMouseButtonEvent(1, true);
      break;
    case WM_RBUTTONUP:
      io.AddMouseButtonEvent(1, false);
      break;
    case WM_MBUTTONDOWN:
      io.AddMouseButtonEvent(2, true);
      break;
    case WM_MBUTTONUP:
      io.AddMouseButtonEvent(2, false);
      break;
    case WM_MOUSEWHEEL:
      io.AddMouseWheelEvent(0.0F, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA);
      break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
      // The mouse's extra buttons are bindable keys: the wire's "Mouse 4" and
      // "Mouse 5" are the virtual-key codes 0x05 and 0x06.
      const auto key = static_cast<std::uint8_t>(0x04 + GET_XBUTTON_WPARAM(wparam));
      if (message == WM_XBUTTONDOWN && capture_key(key)) {
        break;  // the capture took the key: the game must not see it
      }
      swallow = false;  // the extra buttons belong to the game
      break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const auto key = static_cast<std::uint8_t>(wparam);
      // A keybind capture owns the keyboard while it is armed: the key is
      // consumed here, so it never reaches the game.
      if (capture_key(key)) {
        break;
      }
      // The menu's text fields and its keyboard navigation need the key.
      feed_key(io, key, true);
      break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
      feed_key(io, static_cast<std::uint8_t>(wparam), false);
      break;
    case WM_CHAR:
      feed_character(io, static_cast<std::uint16_t>(lparam));
      break;
    case WM_SYSCHAR:
      // Alt+key: the system menu's character, not text.
      break;
    case WM_INPUT: {
      // Drain the raw-input buffer (so the system stays consistent) but do not
      // forward it: the game's mouse-look must not move the camera behind the
      // menu. A real fix lands with the usercmd input path, which will suppress
      // look/move from the payload while `settings.menu_open` is set.
      UINT size = 0;
      GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size,
                      sizeof(RAWINPUTHEADER));
      if (size > 0 && size <= 1024) {
        alignas(RAWINPUT) unsigned char buffer[1024];
        UINT used = size;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buffer, &used,
                        sizeof(RAWINPUTHEADER));
      }
      break;
    }
    case WM_KILLFOCUS:
      // Alt-tabbed away: drop the capture so the game is not left with a
      // subclassed window and a hidden cursor.
      ImGui::SetCurrentContext(previous_context);
      set_menu_open(false);
      return CallWindowProcW(previous_proc, window, message, wparam, lparam);
    default:
      swallow = false;
      break;
  }

  ImGui::SetCurrentContext(previous_context);
  if (swallow) {
    return 0;
  }
  return CallWindowProcW(previous_proc, window, message, wparam, lparam);
}

void poll_toggle(bool in_game, const wire::Settings& settings) {
  const bool open = g_menu_open.load(std::memory_order_relaxed);
  if (open) {
    if (g_window == nullptr || IsWindow(g_window) == FALSE) {
      set_menu_open(false);  // the game recreated its window
    } else if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_window, GWLP_WNDPROC)) !=
               hooked_wndproc) {
      // The game (or a level change) replaced the window procedure after we
      // subclassed it: re-hook against the current one.
      g_old_proc = reinterpret_cast<WNDPROC>(
          SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooked_wndproc)));
      overlay_log("cs2-overlay: menu re-hooked the window procedure");
    }
  }
  if (!in_game) {
    if (open) {
      set_menu_open(false);  // left the map: give input back to the game
    }
    g_previous_toggle = false;
    return;
  }
  if (g_force_open && !open) {
    set_menu_open(true);
    if (g_menu_open.load(std::memory_order_relaxed)) {
      g_force_open = false;
    }
  }
  // The toggle key is the menu's own setting (`wire::Settings::menu_key`).
  // Insert stays a second toggle key, because many keyboards need Fn for it.
  const int key = settings.menu_key != 0 ? static_cast<int>(settings.menu_key) : VK_INSERT;
  const bool key_down = (GetAsyncKeyState(key) & 0x8000) != 0;
  const bool insert_down = key != VK_INSERT && (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
  const bool down = key_down || insert_down;
  // A keybind capture owns the keyboard while it is armed, and for the frame in
  // which it took a key: the menu must not toggle at the same time (the
  // capture's key press would otherwise close the menu behind the user's back).
  const std::uint32_t epoch = capture_epoch();
  if (capture_armed() || epoch != g_capture_epoch) {
    g_capture_epoch = epoch;
    g_previous_toggle = down;
    return;
  }
  if (down != g_previous_toggle) {
    overlay_log("cs2-overlay: menu key %s (in_game=%d, window=%p, open=%d)", down ? "down" : "up",
                in_game ? 1 : 0, static_cast<void*>(g_window),
                g_menu_open.load(std::memory_order_relaxed) ? 1 : 0);
  }
  if (down && !g_previous_toggle) {
    set_menu_open(!g_menu_open.load(std::memory_order_relaxed));
  }
  g_previous_toggle = down;
}

#endif  // _WIN32

}  // namespace

void frame(wire::Settings& settings, bool in_game) noexcept {
#if defined(_WIN32)
  // `CS2_OVERLAY_MENU=1` opens the menu as soon as the game is up (useful for
  // validating the UI without a keyboard, and for screenshots).
  static bool initialised = false;
  if (!initialised) {
    initialised = true;
    g_force_open = std::getenv("CS2_OVERLAY_MENU") != nullptr;
    overlay_log("cs2-overlay: menu init (force_open=%d)", g_force_open ? 1 : 0);
  }
  // The marker file is polled cheaply (every ~0.5 s) and only ever opens the
  // menu, so a stale marker cannot trap the user.
  static unsigned marker_tick = 0;
  if (in_game && !g_menu_open.load(std::memory_order_relaxed) && (marker_tick++ % 30U) == 0U &&
      menu_marker_exists()) {
    g_force_open = true;
  }
  poll_toggle(in_game, settings);
  if (g_menu_open.load(std::memory_order_relaxed)) {
    // The game re-clips the cursor every frame while it has focus; keep it unclipped so
    // the menu is usable without the game's own mouse-release.
    ClipCursor(nullptr);
    // The pages, the widgets and the keybind capture are shared with the
    // payload's in-process menu (`cs2/overlay/menu_pages.hpp`); this file only
    // owns the window plumbing.
    if (!draw_menu(settings, "Vulkan implicit layer")) {
      set_menu_open(false);
    }
  }
  settings.menu_open = g_menu_open.load(std::memory_order_relaxed) ? 1 : 0;
#else
  (void)in_game;
  // No input path off Windows: the menu stays closed and the ESP settings the
  // payload published are left untouched.
  settings.menu_open = 0;
#endif
}

void shutdown() noexcept {
#if defined(_WIN32)
  set_menu_open(false);
#endif
}

}  // namespace cs2::overlay::menu
