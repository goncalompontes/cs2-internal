#include "cs2/features/menu.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cs2/features/config_store.hpp"
#include "cs2/game/log.hpp"
#include "cs2/hooks/memory.hpp"
#include "cs2/overlay/menu_pages.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h>

#include "imgui.h"
#endif

namespace cs2::features {

#if defined(_WIN32)
namespace {

// --- raw-input block -------------------------------------------------------
// CS2's mouse-look goes through SDL3, which reads raw input with
// `GetRawInputBuffer` in its own loop rather than via the window's WM_INPUT — so
// subclassing the WndProc does not stop it. While the menu is open, return "no
// raw input" from `GetRawInputBuffer`; SDL then sees no motion or buttons and the
// camera stays put. The hook is installed once, on the first open, and forwards
// to the original when the menu is closed.
using GetRawInputBufferFn = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
GetRawInputBufferFn g_get_raw_input_buffer = nullptr;
std::atomic<bool> g_block_raw_input{false};

UINT WINAPI get_raw_input_buffer_detour(PRAWINPUT data, PUINT size, UINT header) {
  if (g_block_raw_input.load(std::memory_order_relaxed)) {
    if (size != nullptr) {
      *size = 0;
    }
    return 0;  // no RAWINPUT entries
  }
  return g_get_raw_input_buffer != nullptr ? g_get_raw_input_buffer(data, size, header) : 0;
}

// Debug aid: a marker file (`<payload dir>\menu-open`) forces the menu open as soon
// as the game is in a map. Unlike an environment variable this cannot be lost to
// Steam's cached environment.
auto menu_marker_exists() noexcept -> bool {
  char base[MAX_PATH] = {};
  char path[MAX_PATH] = {};
  if (GetEnvironmentVariableA("CS2_PAYLOAD_DIR", base, sizeof(base)) != 0) {
    std::snprintf(path, sizeof(path), "%s\\menu-open", base);
  } else if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) != 0) {
    std::snprintf(path, sizeof(path), "%s\\cs2-internal\\menu-open", base);
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
  if (IsWindowVisible(window) == FALSE || static_cast<int>(owns_window(window)) == FALSE) {
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

// Fallback when the swapchain did not provide a window: the foreground window when
// it belongs to this process, else the largest visible window of this process.
auto find_game_window() -> HWND {
  const HWND foreground = GetForegroundWindow();
  if (foreground != nullptr && owns_window(foreground)) {
    return foreground;
  }
  WindowSearch search{};
  EnumWindows(&largest_process_window, reinterpret_cast<LPARAM>(&search));
  return search.best;
}

}  // namespace
#endif  // _WIN32

Menu* Menu::s_active_ = nullptr;

void Menu::install_raw_input_block() noexcept {
#if defined(_WIN32)
  if (raw_input_hook_installed_) {
    return;
  }
  // CS2's input goes through SDL3, which imports `user32!GetRawInputBuffer`. The
  // export is a plain function whose prologue is too long for the inline hook, so
  // patch SDL3's IAT slot instead (one caller, no prologue assumptions). Our own
  // raw-input reads go through the payload's own IAT, so they are unaffected.
  HMODULE module = GetModuleHandleA("SDL3.dll");
  if (module == nullptr) {
    cs2::game::append_log("[cs2-menu] raw-input block: SDL3.dll not loaded");
    return;
  }
  auto* base = reinterpret_cast<unsigned char*>(module);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    cs2::game::append_log("[cs2-menu] raw-input block: bad SDL3 DOS header");
    return;
  }
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    cs2::game::append_log("[cs2-menu] raw-input block: bad SDL3 NT header");
    return;
  }
  const IMAGE_DATA_DIRECTORY& import_dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (import_dir.VirtualAddress == 0) {
    cs2::game::append_log("[cs2-menu] raw-input block: SDL3 has no import directory");
    return;
  }
  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + import_dir.VirtualAddress);
  for (; descriptor->Name != 0; ++descriptor) {
    const auto lookup_rva =
        descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
    auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA*>(base + lookup_rva);
    auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
    for (; lookup->u1.Function != 0; ++lookup, ++iat) {
      if ((lookup->u1.Ordinal & IMAGE_ORDINAL_FLAG) != 0) {
        continue;  // imported by ordinal
      }
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), "GetRawInputBuffer") != 0) {
        continue;
      }
      auto* original = reinterpret_cast<void*>(iat->u1.Function);
      if (original == reinterpret_cast<void*>(&get_raw_input_buffer_detour)) {
        raw_input_hook_installed_ = true;
        return;  // already patched
      }
      if (!cs2::hooks::make_writable(iat, sizeof(void*))) {
        cs2::game::append_log("[cs2-menu] raw-input block: make_writable failed");
        return;
      }
      g_get_raw_input_buffer = reinterpret_cast<GetRawInputBufferFn>(original);
      iat->u1.Function = reinterpret_cast<ULONG_PTR>(&get_raw_input_buffer_detour);
      raw_input_hook_installed_ = true;
      cs2::game::append_log("[cs2-menu] raw-input block installed (SDL3 IAT GetRawInputBuffer)");
      return;
    }
  }
  cs2::game::append_log("[cs2-menu] raw-input block: SDL3 does not import GetRawInputBuffer");
#endif
}

#if defined(_WIN32)
std::intptr_t __stdcall Menu::window_proc(void* hwnd, unsigned message, std::size_t wparam,
                                          std::intptr_t lparam) {
  HWND window = static_cast<HWND>(hwnd);
  auto* const previous_proc =
      s_active_ != nullptr ? reinterpret_cast<WNDPROC>(s_active_->old_proc_) : nullptr;
  if (s_active_ == nullptr || !s_active_->open_ || previous_proc == nullptr) {
    return CallWindowProcW(previous_proc != nullptr ? previous_proc : DefWindowProcW, window,
                           message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
  }

  // Dear ImGui's input queue is thread-safe, but its event functions need their
  // context current; this thread (the game's window thread) has none.
  ImGuiContext* const previous_context = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(static_cast<ImGuiContext*>(s_active_->imgui_context_));
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
      if (message == WM_XBUTTONDOWN && cs2::overlay::menu::capture_key(key)) {
        break;  // the capture took the key: the game must not see it
      }
      swallow = false;  // the extra buttons belong to the game
      break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      // A keybind capture owns the keyboard while it is armed: the key is
      // consumed here, so it never reaches the game — and it never reaches
      // ImGui either, or the text field the user is editing would see a key
      // the game never got. The menu's text fields and its keyboard
      // navigation need the keys the capture did not take.
      (void)cs2::overlay::menu::feed_key_down(io, static_cast<std::uint8_t>(wparam));
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      cs2::overlay::menu::feed_key_up(io, static_cast<std::uint8_t>(wparam));
      break;
    case WM_CHAR:
      cs2::overlay::menu::feed_character(io, static_cast<std::uint16_t>(lparam));
      break;
    case WM_SYSCHAR:
      // Alt+key: the system menu's character, not text.
      break;
    case WM_INPUT: {
      // Drain the raw-input buffer (so the system stays consistent) but do not
      // forward it: the game's mouse-look must not move the camera behind the menu.
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
      // Do not close the menu on focus loss: an automated/unfocused desktop would
      // otherwise close and reopen it endlessly (the game re-clips the cursor on
      // refocus anyway). The window destruction case is handled in `poll_toggle`.
      swallow = false;
      break;
    default:
      swallow = false;
      break;
  }

  ImGui::SetCurrentContext(previous_context);
  if (swallow) {
    return 0;
  }
  return CallWindowProcW(previous_proc, window, message, static_cast<WPARAM>(wparam),
                         static_cast<LPARAM>(lparam));
}
#endif  // _WIN32

void Menu::set_movement_defaults(bool auto_strafe, bool bhop) noexcept {
  settings_.auto_strafe = auto_strafe ? 1 : 0;
  settings_.bhop = bhop ? 1 : 0;
}

void Menu::set_rage_defaults(bool on, int key, bool fire) noexcept {
  settings_.rage = on ? 1 : 0;
  settings_.rage_key = static_cast<std::uint8_t>(key & 0xFF);
  settings_.rage_fire = fire ? 1 : 0;
}

#if defined(_WIN32)
void Menu::set_open(bool open, void* hwnd) noexcept {
  if (open == open_) {
    return;
  }
  if (open) {
    open_ = true;
    s_active_ = this;
    imgui_context_ = ImGui::GetCurrentContext();
    if (hwnd != nullptr) {
      window_ = hwnd;
    }
    if (window_ == nullptr) {
      window_ = static_cast<void*>(find_game_window());
    }
    if (window_ == nullptr || imgui_context_ == nullptr) {
      window_ = nullptr;
      old_proc_ = nullptr;
      open_ = false;
      cs2::game::append_log("[cs2-menu] open requested but no window/imgui context");
      return;
    }
    old_proc_ = reinterpret_cast<void*>(SetWindowLongPtrW(
        static_cast<HWND>(window_), GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&Menu::window_proc)));
    // Seed the cursor position and focus so the menu is immediately usable.
    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE) {
      ScreenToClient(static_cast<HWND>(window_), &cursor);
      ImGui::GetIO().AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    }
    ImGui::GetIO().AddFocusEvent(true);
    // The key state is dropped with the menu: a key held when the menu closed
    // would otherwise stay "down" in ImGui's queue.
    ImGui::GetIO().ClearInputKeys();
    if (!cursor_shown_) {
      ShowCursor(TRUE);
      cursor_shown_ = true;
    }
    // Release the mouse clip the game holds; without this the OS cursor stays
    // trapped in the window and the user must open the game's own menu first.
    ClipCursor(nullptr);
    // Stop the game's raw-input mouse-look (SDL3 reads raw input outside the
    // WndProc; see the detour above).
    install_raw_input_block();
    g_block_raw_input.store(true, std::memory_order_relaxed);
    cs2::game::append_log("[cs2-menu] opened");
  } else {
    open_ = false;
    g_block_raw_input.store(false, std::memory_order_relaxed);
    // An armed keybind capture belongs to the closed menu: drop it, so the
    // next key press is not swallowed by a capture nobody can see.
    cs2::overlay::menu::cancel_capture();
    if (window_ != nullptr && old_proc_ != nullptr) {
      SetWindowLongPtrW(static_cast<HWND>(window_), GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(old_proc_));
    }
    window_ = nullptr;
    old_proc_ = nullptr;
    if (cursor_shown_) {
      ShowCursor(FALSE);
      cursor_shown_ = false;
    }
    cs2::game::append_log("[cs2-menu] closed");
  }
}
#else
void Menu::set_open(bool open, void*) noexcept {
  open_ = open;
}
#endif

void Menu::poll_toggle(bool in_game) noexcept {
#if defined(_WIN32)
  if (open_) {
    if (window_ == nullptr || IsWindow(static_cast<HWND>(window_)) == FALSE) {
      cs2::game::append_log("[cs2-menu] close: window invalid");
      set_open(false, nullptr);  // the game recreated its window
    } else if (reinterpret_cast<void*>(GetWindowLongPtrW(static_cast<HWND>(window_), GWLP_WNDPROC)) !=
               reinterpret_cast<void*>(&Menu::window_proc)) {
      // The game (or a level change) replaced the window procedure after we
      // subclassed it: re-hook against the current one.
      old_proc_ = reinterpret_cast<void*>(SetWindowLongPtrW(
          static_cast<HWND>(window_), GWLP_WNDPROC,
          reinterpret_cast<LONG_PTR>(&Menu::window_proc)));
    }
  }
  if (!in_game) {
    if (open_) {
      cs2::game::append_log("[cs2-menu] close: left map");
      set_open(false, nullptr);  // left the map: give input back to the game
    }
    previous_toggle_ = false;
    return;
  }
  if (force_open_ && !open_) {
    set_open(true, window_);
    if (open_) {
      force_open_ = false;
    }
  }
  // The toggle key is the menu's own setting (`wire::Settings::menu_key`).
  // Insert and Delete stay toggle keys as well, because many keyboards need
  // Fn for Insert — and the pre-T2 menu polled both.
  const int key = settings_.menu_key != 0 ? static_cast<int>(settings_.menu_key) : VK_INSERT;
  const bool key_down = (GetAsyncKeyState(key) & 0x8000) != 0;
  const bool insert_down = key != VK_INSERT && (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
  const bool delete_down = key != VK_DELETE && (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
  const bool down = key_down || insert_down || delete_down;
  // A keybind capture owns the keyboard while it is armed, and for the frame in
  // which it took a key: the menu must not toggle at the same time (the
  // capture's key press would otherwise close the menu behind the user's back).
  const std::uint32_t epoch = cs2::overlay::menu::capture_epoch();
  if (cs2::overlay::menu::capture_armed() || epoch != capture_epoch_) {
    capture_epoch_ = epoch;
    previous_toggle_ = down;
    return;
  }
  if (down && !previous_toggle_) {
    set_open(!open_, window_);
  }
  previous_toggle_ = down;
#else
  (void)in_game;
#endif
}

void Menu::tick(bool in_game, void* hwnd) noexcept {
#if defined(_WIN32)
  s_active_ = this;
  if (hwnd != nullptr && window_ == nullptr) {
    window_ = hwnd;
  }
  // `CS2_PAYLOAD_MENU=1` opens the menu as soon as the game is up (useful for
  // validating the UI without a keyboard, and for screenshots).
  if (!initialised_) {
    initialised_ = true;
    force_open_ = std::getenv("CS2_PAYLOAD_MENU") != nullptr;
    // The settings file's path is the payload's to publish: the menu shows it
    // on the Config page (read-only) and keeps the window's geometry next to
    // it.
    cs2::features::Config config{};
    cs2::features::set_config_path(config);
    std::memcpy(settings_.menu_config_path, config.menu.config_path.data(),
                sizeof(settings_.menu_config_path));
    cs2::game::append_log(std::string("[cs2-menu] init (force_open=") +
                          (force_open_ ? "1" : "0") + ")");
  }
  // The marker file is a one-shot debug aid (`<payload dir>\menu-open`): it opens
  // the menu once as soon as the game is in a map (headless screenshots), then
  // never again, so a stale marker cannot fight the toggle key.
  if (!marker_opened_ && in_game && !open_ && (marker_tick_++ % 30U) == 0U &&
      menu_marker_exists()) {
    marker_opened_ = true;
    force_open_ = true;
  }
  poll_toggle(in_game);
  if (open_) {
    // The game re-clips the cursor every frame while it has focus; keep it
    // unclipped so the menu is usable.
    ClipCursor(nullptr);
    ImGui::GetIO().MouseDrawCursor = true;
  }
  settings_.menu_open = open_ ? 1 : 0;
#endif
  apply();
}

void Menu::apply() noexcept {
  overlay_->set_esp_config(from_wire_settings(settings_));
  overlay_->set_show_hud(settings_.show_hud != 0);
  if (settings_sink_ != nullptr) {
    settings_sink_(settings_sink_context_, settings_);
  }
}

void Menu::draw() noexcept {
#if defined(_WIN32)
  if (!open_) {
    return;
  }
  // The pages, the widgets and the keybind capture are shared with the Vulkan
  // layer's menu (`cs2/overlay/menu_pages.hpp`); this file only owns the
  // window plumbing.
  if (!cs2::overlay::menu::draw_menu(settings_, "in-process (DXGI Present hook)",
                                     cs2::overlay::menu::Renderer::payload)) {
    cs2::game::append_log("[cs2-menu] close: window close button");
    set_open(false, window_);
  }
#endif
}

void Menu::ui_entry(void* context) noexcept {
  if (context != nullptr) {
    static_cast<Menu*>(context)->draw();
  }
}

void Menu::shutdown() noexcept {
#if defined(_WIN32)
  set_open(false, window_);
#endif
}

}  // namespace cs2::features
