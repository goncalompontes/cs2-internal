#include "cs2/features/menu.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cs2/game/log.hpp"
#include "cs2/hooks/memory.hpp"

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

void toggle(const char* label, std::uint8_t& value) {
  bool on = value != 0;
  if (ImGui::Checkbox(label, &on)) {
    value = on ? 1 : 0;
  }
}

// Ragebot trigger-key choices (Win32 virtual-key codes).
const char* const k_rage_key_names[] = {"X",   "C",    "V",        "Z",       "F",
                                        "E",   "Q",    "Left Alt", "Left Shift",
                                        "Mouse 4", "Mouse 5"};
const int k_rage_key_vks[] = {0x58, 0x43,     0x56, 0x5A, 0x46,
                              0x45, 0x51,     0xA4, 0xA0, 0x05,  0x06};

auto rage_key_index(std::uint8_t vk) -> int {
  for (int i = 0; i < IM_ARRAYSIZE(k_rage_key_vks); ++i) {
    if (k_rage_key_vks[i] == vk) {
      return i;
    }
  }
  return 0;
}

// Draws the menu; returns false when the user closed it with the window button.
auto draw_window(cs2::overlay::wire::Settings& settings) -> bool {
  bool open = true;
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("cs2-internal menu", &open)) {
    if (ImGui::BeginTabBar("##tabs")) {
      if (ImGui::BeginTabItem("ESP")) {
        toggle("Box", settings.box);
        toggle("Health bar", settings.health_bar);
        toggle("Ammo bar", settings.ammo_bar);
        ImGui::Separator();
        toggle("Name", settings.name);
        toggle("Weapon", settings.weapon);
        toggle("Flags", settings.flags);
        ImGui::Separator();
        toggle("Only enemies", settings.only_enemies);
        toggle("Show dead", settings.show_dead);
        ImGui::Separator();
        toggle("Hitboxes (studio)", settings.hitboxes);
        if (settings.hitboxes != 0) {
          static const char* const k_modes[] = {"Outline", "Solid", "Wireframe"};
          int mode = settings.hitbox_mode <= 2 ? settings.hitbox_mode : 0;
          ImGui::SetNextItemWidth(160.0F);
          if (ImGui::Combo("Mode", &mode, k_modes, IM_ARRAYSIZE(k_modes))) {
            settings.hitbox_mode = static_cast<std::uint8_t>(mode);
          }
          static const char* const k_colors[] = {"Automatic (hitgroup)", "Custom"};
          int color_mode = settings.hitbox_color_mode != 0 ? 1 : 0;
          ImGui::SetNextItemWidth(160.0F);
          if (ImGui::Combo("Colour", &color_mode, k_colors, IM_ARRAYSIZE(k_colors))) {
            settings.hitbox_color_mode = static_cast<std::uint8_t>(color_mode);
          }
          if (settings.hitbox_color_mode != 0) {
            float rgba[4] = {settings.hitbox_color[0] / 255.0F, settings.hitbox_color[1] / 255.0F,
                             settings.hitbox_color[2] / 255.0F, settings.hitbox_color[3] / 255.0F};
            if (ImGui::ColorEdit4("Colour pick", rgba,
                                  ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
              for (int i = 0; i < 4; ++i) {
                const float clamped = rgba[i] < 0.0F ? 0.0F : (rgba[i] > 1.0F ? 1.0F : rgba[i]);
                settings.hitbox_color[i] = static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
              }
            }
          }
          ImGui::TextDisabled("Outline: silhouette. Solid: filled. Wireframe: mesh cage.");
        }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Interface")) {
        toggle("Debug HUD window", settings.show_hud);
        ImGui::TextDisabled("Insert or Delete toggles this menu");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Movement")) {
        toggle("Auto-strafe (silent)", settings.auto_strafe);
        toggle("Bunnyhop", settings.bhop);
        ImGui::Separator();
        ImGui::TextDisabled("Applied by the payload per tick while airborne (strafe)");
        ImGui::TextDisabled("or grounded (bhop). The camera does not move.");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Ragebot")) {
        toggle("Ragebot", settings.rage);
        // The trigger (`docs/ragebot.md`): hold the key, always on, or toggle the key.
        static const char* const k_trigger_modes[] = {"Hold key", "Always on", "Toggle key"};
        int trigger = settings.rage_mode <= 2 ? settings.rage_mode : 0;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Trigger", &trigger, k_trigger_modes, IM_ARRAYSIZE(k_trigger_modes))) {
          settings.rage_mode = static_cast<std::uint8_t>(trigger);
        }
        if (settings.rage_mode == 0) {
          int key_index = rage_key_index(settings.rage_key);
          if (ImGui::Combo("Trigger key", &key_index, k_rage_key_names,
                           IM_ARRAYSIZE(k_rage_key_names))) {
            settings.rage_key = static_cast<std::uint8_t>(k_rage_key_vks[key_index]);
          }
        }
        toggle("Auto fire", settings.rage_fire);
        ImGui::Separator();
        // Objective: what the solve maximizes.
        static const char* const k_objectives[] = {"Damage", "Hitchance", "Expected damage"};
        int objective = settings.rage_objective <= 2 ? settings.rage_objective : 0;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Objective", &objective, k_objectives, IM_ARRAYSIZE(k_objectives))) {
          settings.rage_objective = static_cast<std::uint8_t>(objective);
        }
        // Hitbox: which boxes the solve may pick.
        static const char* const k_boxes[] = {"Auto (head first)", "Head only", "Body only"};
        int hitbox = settings.rage_hitbox <= 2 ? settings.rage_hitbox : 0;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Hitbox", &hitbox, k_boxes, IM_ARRAYSIZE(k_boxes))) {
          settings.rage_hitbox = static_cast<std::uint8_t>(hitbox);
        }
        ImGui::SetNextItemWidth(160.0F);
        ImGui::SliderFloat("FOV", &settings.rage_fov, 0.0F, 180.0F, "%.0f deg");
        ImGui::SetNextItemWidth(160.0F);
        ImGui::SliderFloat("Min damage", &settings.rage_min_damage, 0.0F, 200.0F, "%.0f");
        ImGui::SetNextItemWidth(160.0F);
        ImGui::SliderFloat("Min hitchance", &settings.rage_min_hitchance, 0.0F, 1.0F, "%.2f");
        ImGui::Separator();
        // The common assists (`docs/ragebot.md`).
        static const char* const k_assist_modes[] = {"Off", "Always", "When firing"};
        int autostop = settings.rage_autostop <= 2 ? settings.rage_autostop : 0;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Auto-stop", &autostop, k_assist_modes, IM_ARRAYSIZE(k_assist_modes))) {
          settings.rage_autostop = static_cast<std::uint8_t>(autostop);
        }
        int autoscope = settings.rage_autoscope <= 2 ? settings.rage_autoscope : 0;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Auto-scope", &autoscope, k_assist_modes, IM_ARRAYSIZE(k_assist_modes))) {
          settings.rage_autoscope = static_cast<std::uint8_t>(autoscope);
        }
        toggle("Auto-reload (empty clip)", settings.rage_autoreload);
        toggle("Only visible (no wallbang)", settings.rage_only_visible);
        toggle("Recoil compensation", settings.rage_recoil);
        toggle("Backtrack (lag records)", settings.rage_backtrack);
        ImGui::TextDisabled("Off by default: each lag pose is a full solve.");
        ImGui::SetNextItemWidth(160.0F);
        ImGui::SliderFloat("Auto-stop speed", &settings.rage_stop_speed, 0.0F, 250.0F, "%.0f u/s");
        ImGui::TextDisabled("Counter-strafe down to this speed. 0 = default (34%% of max).");
        ImGui::Separator();
        // Per-weapon profiles (`rage_config.hpp`): the live sliders above are the
        // *global* tuning; save/load a weapon's copy, and `Per-weapon` applies the
        // profile matching the active weapon at solve time.
        toggle("Per-weapon config", settings.rage_per_weapon);
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputText("Weapon key", settings.rage_weapon, sizeof(settings.rage_weapon));
        if (ImGui::Button("Save to weapon")) {
          settings.rage_config_op = 1;
          ++settings.rage_config_token;
        }
        ImGui::SameLine();
        if (ImGui::Button("Load from weapon")) {
          settings.rage_config_op = 2;
          ++settings.rage_config_token;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
          settings.rage_config_op = 5;
          ++settings.rage_config_token;
        }
        if (ImGui::Button("Save file")) {
          settings.rage_config_op = 3;
          ++settings.rage_config_token;
        }
        ImGui::SameLine();
        if (ImGui::Button("Load file")) {
          settings.rage_config_op = 4;
          ++settings.rage_config_token;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Hold the key to aim at the best enemy in FOV (silent),");
        ImGui::TextDisabled("through walls with the real penetration model.");
        ImGui::TextDisabled("Fires while held when Auto fire is on.");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("About")) {
        ImGui::TextWrapped("cs2-internal overlay. Rendered in-process from the game's Present.");
        ImGui::TextDisabled("font: Dear ImGui default, antialiasing off");
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
  return open;
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
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
      // Keys belong to the menu while it is open: the game sees none of them.
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
  // Two keys, because many keyboards need Fn for Insert.
  const bool insert_down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
  const bool delete_down = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
  const bool down = insert_down || delete_down;
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
  if (!draw_window(settings_)) {
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
