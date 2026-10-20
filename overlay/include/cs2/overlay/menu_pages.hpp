#pragma once

// The menu's content, defined once for both renderers.
//
// Two renderers draw the menu: the injected payload's in-process menu
// (`payload/features/src/menu.cpp`) and the Vulkan layer's fallback
// (`overlay/src/menu.cpp`). They used to be near-duplicates of each other —
// the same tab strip, the same widget calls, the same `toggle()` helper — and
// they had already drifted (the payload's copy had a "Backtrack" toggle the
// layer's copy lacked). This header is the single definition of the pages,
// the widget toolkit and the keybind capture; each renderer keeps only its
// own open/close plumbing (the window subclass, the raw-input block, the
// toggle-key polling).
//
// The header is ImGui-only and platform-independent: it includes no Windows
// header, so the Vulkan layer's build (and the payload's) stay clean. The
// Win32 specifics (the window procedure, the virtual-key codes' origin) stay
// in the renderers.
//
// The settings block (`wire::Settings`) is the menu's only state: the pages
// read and write it in place, so the renderer that owns the block (the
// payload's own copy, or the layer's copy of the shared segment) owns the
// configuration.
//
// The pages, their labels, their order and their enable/disable logic live
// here and **only** here: a page that is added twice is a page that drifts.

#include <atomic>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"

#include "cs2/overlay/wire.hpp"

namespace cs2::overlay::menu {

// --- The layout -------------------------------------------------------------

// The label column: every control starts at the same x, so a page reads as a
// table instead of a pile of widgets.
inline constexpr float k_label_column = 168.0F;
// The width of a control.
inline constexpr float k_control_width = 186.0F;
// The sidebar's width.
inline constexpr float k_sidebar_width = 158.0F;
// The smallest the window may become (the sidebar plus a readable page).
inline constexpr float k_min_width = 520.0F;
inline constexpr float k_min_height = 380.0F;
// The product's name, shown in the header bar and on the About page.
inline constexpr const char* k_product_name = "cs2-internal";
// The build's version, shown in the header bar and on the About page.
inline constexpr const char* k_build_info = "0.1.0";
// The branch the build came from, shown on the About page.
//
// The build systems define `CS2_BUILD_BRANCH`; a build that does not say so
// rather than claim a branch it does not know.
#if defined(CS2_BUILD_BRANCH)
inline constexpr const char* k_build_branch = CS2_BUILD_BRANCH;
#else
inline constexpr const char* k_build_branch = "unknown";
#endif
// The menu window's title. The "##" keeps it out of ImGui's window list.
inline constexpr const char* k_window_title = "cs2-internal##cs2-menu";
// The most sub-tick slices the auto-strafe accepts (`k_strafe_max_steps` in
// the control module). The slider must not offer a value the payload clamps
// away behind the user's back.
inline constexpr int k_max_strafe_subticks = 16;
// The length of a path buffer: the wire's path field is `MAX_PATH` wide, and
// the header's own buffers are that wide too.
//
// **Nothing in this header allocates.** The header is compiled into the
// manually mapped payload (no CRT heap) *and* into the Vulkan layer, where an
// allocation failure would abort the game, so every path lives in a fixed
// buffer.
inline constexpr std::size_t k_max_path = 260;

// --- The shared UI state ----------------------------------------------------
//
// ADR-0010 forbids new global mutable state, and this is the one documented
// exception: the menu's UI state has to outlive a frame (the sidebar's
// selection, the window's geometry and the keybind capture are written in one
// frame and read in the next), and this header is the only place the two
// renderers can share it.
//
// The state is reachable only through the accessors below and lives in an
// `inline` function's function-local static — one definition per program, so
// the translation units that include this header share it. Every field that
// two threads can touch (the capture: the window procedure runs on the game's
// window thread while the menu is drawn on the render thread) is an atomic.

// The window's position and size.
struct WindowGeometry {
  float x = 90.0F;
  float y = 90.0F;
  float width = 660.0F;
  float height = 540.0F;
};

// The menu's UI state.
struct UiState {
  WindowGeometry geometry{};
  bool geometry_loaded = false;       // the persisted geometry was read
  bool geometry_applied = false;      // the geometry was pushed to ImGui
  bool geometry_dirty = false;        // the geometry changed and is not written yet
  unsigned save_timer = 0;            // frames until the geometry is written
  int section = 0;                    // the sidebar's selection
  char published_path[k_max_path]{};  // the settings path the block carried
  std::size_t published_length = 0;
  char config_path[k_max_path]{};  // the effective settings path
  std::size_t config_length = 0;
};

[[nodiscard]] inline auto ui_state() noexcept -> UiState& {
  static UiState state{};
  return state;
}

// --- The keybind capture ----------------------------------------------------

// The virtual-key codes the menu names. They are the Win32 codes (the wire
// stores a virtual-key code), spelled out here so the header stays free of
// <windows.h>.
inline constexpr std::uint8_t k_key_escape = 0x1B;
inline constexpr std::uint8_t k_key_delete = 0x2E;
inline constexpr std::uint8_t k_key_insert = 0x2D;

// The capture's state.
//
// The capture is a small state machine: a keybind widget arms it, the game's
// window procedure feeds it the next key, and the widget reads the result a
// frame later. Every access is atomic, because the window procedure runs on
// the game's window thread and the menu is drawn on the render thread.
struct KeybindCapture {
  std::atomic<bool> armed{false};         // a keybind widget waits for a key
  std::atomic<std::uintptr_t> target{0};  // the armed widget's identity
  std::atomic<bool> has_result{false};    // a captured key waits to be read
  std::atomic<std::uint8_t> result{0};    // the captured key (0 = cleared)
  std::atomic<std::uint32_t> epoch{0};    // bumped on every captured key
  // The key the capture swallowed: its character and its key release must not
  // reach ImGui either, or a text field would see a key the game never got.
  std::atomic<std::uint8_t> captured_key{0};
  // A captured key's character is still to come (Windows sends the key down,
  // then the character, then the key up).
  std::atomic<bool> character_pending{false};
};

[[nodiscard]] inline auto keybind_capture() noexcept -> KeybindCapture& {
  static KeybindCapture capture{};
  return capture;
}

// Arm the capture for the widget identified by `target` (the widget's label
// address: two keybinds must have distinct labels).
inline void arm_capture(std::uintptr_t target) noexcept {
  KeybindCapture& capture = keybind_capture();
  capture.target.store(target, std::memory_order_relaxed);
  capture.has_result.store(false, std::memory_order_release);
  capture.armed.store(true, std::memory_order_release);
}

// Cancel the capture: the binding keeps the value it had.
//
// The pending result goes with it: a key captured just before the menu
// closed must not be applied when the menu reopens.
inline void cancel_capture() noexcept {
  KeybindCapture& capture = keybind_capture();
  capture.armed.store(false, std::memory_order_release);
  capture.has_result.store(false, std::memory_order_release);
  capture.target.store(0, std::memory_order_relaxed);
  capture.captured_key.store(0, std::memory_order_relaxed);
  capture.character_pending.store(false, std::memory_order_release);
}

// True while a keybind widget waits for a key.
[[nodiscard]] inline auto capture_armed() noexcept -> bool {
  return keybind_capture().armed.load(std::memory_order_acquire);
}

// The number of keys the capture has consumed.
//
// The menu's toggle key is polled with `GetAsyncKeyState`, which cannot see
// whether the window procedure swallowed the key: the renderer compares this
// counter and skips the toggle for the frame in which the capture took a key.
[[nodiscard]] inline auto capture_epoch() noexcept -> std::uint32_t {
  return keybind_capture().epoch.load(std::memory_order_acquire);
}

// Feed a key press to the capture.
//
// Returns true when the capture consumed the key: **the caller must not let
// the game see it** — the capture is the reason the key does not leak into
// the game while the menu is open. Escape and Delete clear the binding.
//
// A consumed key also takes its character and its key release with it: the
// key never went down in ImGui's queue, so neither may its text nor its
// release reach it.
[[nodiscard]] inline auto capture_key(std::uint8_t key) noexcept -> bool {
  KeybindCapture& capture = keybind_capture();
  if (!capture.armed.load(std::memory_order_acquire)) {
    return false;
  }
  if (!capture.armed.exchange(false, std::memory_order_acq_rel)) {
    return false;  // another key press won the race
  }
  const bool clears = key == k_key_escape || key == k_key_delete;
  capture.result.store(clears ? static_cast<std::uint8_t>(0) : key, std::memory_order_relaxed);
  capture.has_result.store(true, std::memory_order_release);
  capture.epoch.fetch_add(1U, std::memory_order_acq_rel);
  capture.captured_key.store(key, std::memory_order_relaxed);
  capture.character_pending.store(true, std::memory_order_release);
  return true;
}

// The names of the single-character keys (the letters A-Z).
inline constexpr char k_letter_names[26][2] = {
    {'A', 0}, {'B', 0}, {'C', 0}, {'D', 0}, {'E', 0}, {'F', 0}, {'G', 0}, {'H', 0}, {'I', 0},
    {'J', 0}, {'K', 0}, {'L', 0}, {'M', 0}, {'N', 0}, {'O', 0}, {'P', 0}, {'Q', 0}, {'R', 0},
    {'S', 0}, {'T', 0}, {'U', 0}, {'V', 0}, {'W', 0}, {'X', 0}, {'Y', 0}, {'Z', 0},
};

// The names of the digit keys (0-9).
inline constexpr char k_digit_names[10][2] = {
    {'0', 0}, {'1', 0}, {'2', 0}, {'3', 0}, {'4', 0},
    {'5', 0}, {'6', 0}, {'7', 0}, {'8', 0}, {'9', 0},
};

// The names of the function keys (F1-F12).
inline constexpr char k_function_names[12][4] = {
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
};

// A key with a name of its own (neither a letter, a digit nor a function key).
struct NamedKey {
  std::uint8_t key;
  const char* name;
};

// The names of the keys that have one.
//
// The mouse buttons follow the **physical** button, not the virtual-key code:
// the capture turns `WM_XBUTTONDOWN` into `0x05`/`0x06` (the fourth and the
// fifth button), so `0x05` is "Mouse 4" and `0x06` is "Mouse 5" — the
// convention the pre-T2 menu used. `0x04` is the middle button, and `0x03` is
// `VK_CANCEL`, which is not a mouse button at all.
inline constexpr NamedKey k_named_keys[] = {
    {0x01, "Mouse 1"},    {0x02, "Mouse 2"},     {0x04, "Middle Mouse"}, {0x05, "Mouse 4"},
    {0x06, "Mouse 5"},    {0x08, "Backspace"},   {0x09, "Tab"},          {0x0D, "Enter"},
    {0x14, "Caps Lock"},  {0x1B, "Escape"},      {0x20, "Space"},        {0x21, "Page Up"},
    {0x22, "Page Down"},  {0x23, "End"},         {0x24, "Home"},         {0x25, "Left"},
    {0x26, "Up"},         {0x27, "Right"},       {0x28, "Down"},         {0x2D, "Insert"},
    {0x2E, "Delete"},     {0x5B, "Left Super"},  {0x5C, "Right Super"},  {0x5D, "Menu"},
    {0xA0, "Left Shift"}, {0xA1, "Right Shift"}, {0xA2, "Left Ctrl"},    {0xA3, "Right Ctrl"},
    {0xA4, "Left Alt"},   {0xA5, "Right Alt"},
};

// The name of a virtual-key code ("X", "Left Alt", "Mouse 4", "None").
//
// The names are the ones a player would recognise; the codes are the Win32
// virtual-key codes the wire stores.
[[nodiscard]] inline auto key_name(std::uint8_t key) noexcept -> const char* {
  if (key == 0) {
    return "None";
  }
  for (const NamedKey& named : k_named_keys) {
    if (named.key == key) {
      return named.name;
    }
  }
  if (key >= 0x41 && key <= 0x5A) {
    return k_letter_names[key - 0x41];
  }
  if (key >= 0x30 && key <= 0x39) {
    return k_digit_names[key - 0x30];
  }
  if (key >= 0x70 && key <= 0x7B) {
    return k_function_names[key - 0x70];
  }
  return "Unknown";
}

// --- The keyboard -----------------------------------------------------------

// The ImGui key a Win32 virtual-key code maps to.
//
// Dear ImGui's Win32 platform backend is deliberately not used (its window and
// mouse calls from the render thread are the ADR-0015 deadlock class), so the
// renderers map the keys themselves: the menu's text fields and its keyboard
// navigation need the key events, and the keys must still never reach the
// game.
[[nodiscard]] inline auto imgui_key(std::uint8_t key) noexcept -> ImGuiKey {
  if (key >= 'A' && key <= 'Z') {
    return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_A) + (key - 'A'));
  }
  if (key >= '0' && key <= '9') {
    return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_0) + (key - '0'));
  }
  switch (key) {
    case 0x08:
      return ImGuiKey_Backspace;
    case 0x09:
      return ImGuiKey_Tab;
    case 0x0D:
      return ImGuiKey_Enter;
    case 0x10:
      return ImGuiKey_LeftShift;
    case 0x11:
      return ImGuiKey_LeftCtrl;
    case 0x12:
      return ImGuiKey_LeftAlt;
    case k_key_escape:
      return ImGuiKey_Escape;
    case 0x20:
      return ImGuiKey_Space;
    case 0x21:
      return ImGuiKey_PageUp;
    case 0x22:
      return ImGuiKey_PageDown;
    case 0x23:
      return ImGuiKey_End;
    case 0x24:
      return ImGuiKey_Home;
    case 0x25:
      return ImGuiKey_LeftArrow;
    case 0x26:
      return ImGuiKey_UpArrow;
    case 0x27:
      return ImGuiKey_RightArrow;
    case 0x28:
      return ImGuiKey_DownArrow;
    case k_key_insert:
      return ImGuiKey_Insert;
    case k_key_delete:
      return ImGuiKey_Delete;
    default:
      return ImGuiKey_None;
  }
}

// Feed a key press (or its release) to ImGui's input queue.
//
// The renderers call this from their window procedure. The queue is
// thread-safe, so the game's window thread may feed it while the menu is
// drawn on the render thread.
inline void feed_key(ImGuiIO& io, std::uint8_t key, bool down) noexcept {
  const ImGuiKey mapped = imgui_key(key);
  if (mapped == ImGuiKey_None) {
    return;
  }
  io.AddKeyEvent(mapped, down);
}

// The symbol a digit key produces with Shift held (the US layout).
[[nodiscard]] inline auto shifted_digit(std::uint8_t key) noexcept -> std::uint16_t {
  static constexpr std::uint16_t k_shifted[] = {
      ')', '!', '@', '#', '$', '%', '^', '&', '*', '(',
  };
  const std::size_t index = static_cast<std::size_t>(key) - 0x30U;
  return index < sizeof(k_shifted) / sizeof(k_shifted[0]) ? k_shifted[index] : 0;
}

// True when `character` is the text the key produced.
//
// The capture swallows a key press, but Windows still sends the key's
// character: it would land in whatever text field has the focus. A character
// carries no key code, so the match is by the key's own text: a letter (in
// either case), a digit (or the symbol a shifted digit key produces) or the
// space bar.
[[nodiscard]] inline auto character_matches_key(std::uint16_t character, std::uint8_t key) noexcept
    -> bool {
  if (character < 0x20 || character > 0x7E) {
    return false;  // not a printable ASCII character
  }
  if (key >= 0x41 && key <= 0x5A) {
    const auto upper = static_cast<std::uint16_t>(key);
    return character == upper || character == upper + 0x20;
  }
  if (key >= 0x30 && key <= 0x39) {
    return character == key || character == shifted_digit(key);
  }
  return key == 0x20 && character == 0x20;
}

// Feed a key press to ImGui, unless the capture swallowed it.
//
// Returns true when the capture took the key: **the caller must not let the
// game see it** either.
[[nodiscard]] inline auto feed_key_down(ImGuiIO& io, std::uint8_t key) noexcept -> bool {
  if (capture_key(key)) {
    return true;
  }
  // A key the capture did not take closes the window in which a captured
  // key's character could still arrive.
  keybind_capture().character_pending.store(false, std::memory_order_release);
  feed_key(io, key, true);
  return false;
}

// Feed a key release to ImGui, unless the capture swallowed the key's press.
//
// A key the capture took never went down in ImGui's queue, so its release
// must not arrive alone: ImGui would remember the key as stuck.
inline void feed_key_up(ImGuiIO& io, std::uint8_t key) noexcept {
  KeybindCapture& capture = keybind_capture();
  if (capture.captured_key.load(std::memory_order_acquire) == key && key != 0) {
    capture.captured_key.store(0, std::memory_order_relaxed);
    capture.character_pending.store(false, std::memory_order_release);
    return;
  }
  feed_key(io, key, false);
}

// Feed the character a key produced to ImGui (the text fields need it).
//
// The character of a key the capture swallowed is dropped: the key never
// reached the text field, so its text must not either.
inline void feed_character(ImGuiIO& io, std::uint16_t character) noexcept {
  KeybindCapture& capture = keybind_capture();
  if (capture.character_pending.load(std::memory_order_acquire)) {
    capture.character_pending.store(false, std::memory_order_release);
    const std::uint8_t captured = capture.captured_key.load(std::memory_order_relaxed);
    if (character_matches_key(character, captured)) {
      return;  // the character of a swallowed key
    }
  }
  if (character < 0x20 || character == 0x7F) {
    return;  // a control character: the key event carries it
  }
  io.AddInputCharacterUTF16(static_cast<ImWchar16>(character));
}

// --- The widget toolkit -----------------------------------------------------

// Draw the label and move the cursor to the control column, so every control
// on the page lines up.
inline void control_row(const char* label) noexcept {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(k_label_column);
}

// A check box bound to a bool.
inline void toggle(const char* label, bool& value) noexcept {
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  ImGui::Checkbox("##toggle", &value);
  ImGui::PopID();
}

// A check box bound to the wire's flag byte.
inline void toggle(const char* label, std::uint8_t& value) noexcept {
  bool on = value != 0;
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  if (ImGui::Checkbox("##toggle", &on)) {
    value = on ? 1U : 0U;
  }
  ImGui::PopID();
}

// A float slider.
inline void slider_float(const char* label, float& value, float min, float max,
                         const char* fmt) noexcept {
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  ImGui::SliderFloat("##value", &value, min, max, fmt);
  ImGui::PopID();
}

// An integer slider.
inline void slider_int(const char* label, int& value, int min, int max) noexcept {
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  ImGui::SliderInt("##value", &value, min, max);
  ImGui::PopID();
}

// An integer slider bound to the wire's byte. An out-of-range value (a
// corrupt block) is pulled back into the range the widget draws.
inline void slider_int(const char* label, std::uint8_t& value, int min, int max) noexcept {
  int current = value;
  slider_int(label, current, min, max);
  value = static_cast<std::uint8_t>(current);
}

// A drop-down.
inline void combo(const char* label, int& index, const char* const* items, int count) noexcept {
  if (items == nullptr || count <= 0) {
    return;
  }
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  ImGui::Combo("##value", &index, items, count);
  ImGui::PopID();
}

// A drop-down bound to the wire's byte. An out-of-range value (a corrupt
// block) is pulled back into the range the widget draws.
inline void combo(const char* label, std::uint8_t& index, const char* const* items,
                  int count) noexcept {
  if (items == nullptr || count <= 0) {
    return;
  }
  int current = index < count ? static_cast<int>(index) : 0;
  combo(label, current, items, count);
  index = static_cast<std::uint8_t>(current);
}

// An RGBA colour picker. The wire stores the four channels as bytes.
inline void color(const char* label, std::uint8_t rgba[4]) noexcept {
  if (rgba == nullptr) {
    return;
  }
  float channels[4] = {
      static_cast<float>(rgba[0]) / 255.0F,
      static_cast<float>(rgba[1]) / 255.0F,
      static_cast<float>(rgba[2]) / 255.0F,
      static_cast<float>(rgba[3]) / 255.0F,
  };
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  if (ImGui::ColorEdit4("##value", channels,
                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
    for (int index = 0; index < 4; ++index) {
      const float clamped =
          channels[index] < 0.0F ? 0.0F : (channels[index] > 1.0F ? 1.0F : channels[index]);
      rgba[index] = static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
    }
  }
  ImGui::PopID();
}

// A key binding.
//
// The button shows the binding's name; clicking it arms the capture and the
// next key press is captured by the window procedure (which swallows it, so
// the game never sees it). Escape and Delete clear the binding.
inline void keybind(const char* label, std::uint8_t& key) noexcept {
  KeybindCapture& capture = keybind_capture();
  const std::uintptr_t id = reinterpret_cast<std::uintptr_t>(label);
  // A key captured by the window procedure arrives on the game's window
  // thread, so it is picked up here, a frame later.
  if (capture.has_result.load(std::memory_order_acquire) &&
      capture.target.load(std::memory_order_relaxed) == id) {
    capture.has_result.store(false, std::memory_order_release);
    key = capture.result.load(std::memory_order_relaxed);
  }
  const bool armed = capture.armed.load(std::memory_order_acquire) &&
                     capture.target.load(std::memory_order_relaxed) == id;
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  if (ImGui::Button(armed ? "press a key..." : key_name(key), ImVec2(k_control_width, 0.0F))) {
    if (armed) {
      cancel_capture();  // clicking the armed button again gives up the capture
    } else {
      arm_capture(id);
    }
  }
  ImGui::PopID();
}

// A single-line text field.
inline void text_input(const char* label, char* buffer, std::size_t size) noexcept {
  if (buffer == nullptr || size == 0) {
    return;
  }
  control_row(label);
  ImGui::SetNextItemWidth(k_control_width);
  ImGui::PushID(label);
  ImGui::InputText("##value", buffer, size);
  ImGui::PopID();
}

// A button. The caller places it: the page decides whether the button sits
// in the control column or next to another button.
[[nodiscard]] inline auto button(const char* label, float width = 0.0F) noexcept -> bool {
  ImGui::PushID(label);
  const bool pressed =
      width > 0.0F ? ImGui::Button(label, ImVec2(width, 0.0F)) : ImGui::Button(label);
  ImGui::PopID();
  return pressed;
}

// --- The one-shot config op -------------------------------------------------

// The renderer that owns the settings block.
//
// The two renderers are the payload's in-process menu
// (`payload/features/src/menu.cpp`) and the Vulkan layer's fallback
// (`overlay/src/menu.cpp`). They edit **different** blocks (the payload's own
// copy and the shared segment's), but the payload carries the one-shot config
// op out for both of them, so the op's token has to be unique across the
// renderers: a plain counter would let one renderer's op swallow the other's
// (the payload would see the same token twice and skip the second one).
enum class Renderer : std::uint8_t {
  payload = 0,  // the in-process menu
  layer = 1,    // the Vulkan layer's menu
};

// Raise a one-shot config op (save / load / reset) on the block this renderer
// owns.
//
// The token's low bit carries the renderer and the counter moves in steps of
// two, so the payload's tokens are even and the layer's are odd: the two can
// never be the same value, and the payload's op bookkeeping cannot mistake one
// renderer's op for the other's.
inline void request_config_op(wire::Settings& settings, std::uint8_t op,
                              Renderer renderer) noexcept {
  const std::uint32_t source = static_cast<std::uint32_t>(renderer) & 1U;
  settings.menu_config_op = op;
  settings.menu_config_token = ((settings.menu_config_token & ~1U) | source) + 2U;
}

// Raise a one-shot ragebot op (save to / load from / delete a weapon profile,
// save the profile file, load the profile file) on the block this renderer
// owns.
//
// The token carries the renderer, exactly as the config op's does: the two
// renderers edit different blocks and the payload carries the op out for
// both of them, so their tokens must not collide.
inline void request_rage_config_op(wire::Settings& settings, std::uint8_t op,
                                   Renderer renderer) noexcept {
  const std::uint32_t source = static_cast<std::uint32_t>(renderer) & 1U;
  settings.rage_config_op = op;
  settings.rage_config_token = ((settings.rage_config_token & ~1U) | source) + 2U;
}

// --- The settings file and the window's geometry ----------------------------

// Copy `text` into `out` (at most `size` bytes, always NUL-terminated).
//
// Returns the length, or 0 when the text does not fit: a path that does not
// fit is **refused**, never truncated to a different file.
[[nodiscard]] inline auto copy_path(char* out, std::size_t size, const char* text) noexcept
    -> std::size_t {
  if (out == nullptr || size == 0 || text == nullptr) {
    return 0;
  }
  const std::size_t length = std::strlen(text);
  if (length >= size) {
    out[0] = '\0';
    return 0;
  }
  std::memcpy(out, text, length + 1);
  return length;
}

// Join a directory and a file name into `out` ("<directory>\<name>").
[[nodiscard]] inline auto join_path(char* out, std::size_t size, const char* directory,
                                    const char* name) noexcept -> std::size_t {
  if (out == nullptr || size == 0 || directory == nullptr || name == nullptr) {
    return 0;
  }
  const int written = std::snprintf(out, size, "%s\\%s", directory, name);
  if (written <= 0 || static_cast<std::size_t>(written) >= size) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<std::size_t>(written);
}

// The settings file's path: the one the payload published, or the documented
// default when the block carries none.
//
// The fallback resolves the path **exactly** as the payload's store does
// (`cs2::features::default_config_path`): `CS2_PAYLOAD_CONFIG`, then
// `CS2_PAYLOAD_DIR`, then `LOCALAPPDATA`, then `TEMP`, the last two under
// `cs2-internal`. The menu's path and the store's path have to be the same
// file, or the menu shows a path the payload does not use.
//
// The returned buffer belongs to the shared UI state and stays valid until
// the next call.
[[nodiscard]] inline auto config_path(const wire::Settings& settings) noexcept -> const char* {
  UiState& state = ui_state();
  const char* const published = settings.menu_config_path;
  std::size_t count = 0;
  while (count < sizeof(settings.menu_config_path) && published[count] != '\0') {
    ++count;
  }
  if (count != state.published_length ||
      (count != 0 && std::memcmp(state.published_path, published, count) != 0)) {
    state.published_length = count;
    std::memcpy(state.published_path, published, count);
    state.published_path[count] = '\0';
    state.config_length = 0;  // recomputed below
    state.config_path[0] = '\0';
  }
  if (state.config_length != 0) {
    return state.config_path;
  }
  if (state.published_length != 0) {
    state.config_length = copy_path(state.config_path, k_max_path, state.published_path);
    return state.config_path;
  }
  if (const char* const override_path = std::getenv("CS2_PAYLOAD_CONFIG");
      override_path != nullptr && override_path[0] != '\0') {
    state.config_length = copy_path(state.config_path, k_max_path, override_path);
    return state.config_path;
  }
  if (const char* const override_dir = std::getenv("CS2_PAYLOAD_DIR");
      override_dir != nullptr && override_dir[0] != '\0') {
    state.config_length = join_path(state.config_path, k_max_path, override_dir, "cs2-config.cfg");
    return state.config_path;
  }
  const char* base = std::getenv("LOCALAPPDATA");
  if (base == nullptr || base[0] == '\0') {
    base = std::getenv("TEMP");
  }
  if (base != nullptr && base[0] != '\0') {
    char directory[k_max_path] = {};
    const int written = std::snprintf(directory, sizeof(directory), "%s\\cs2-internal", base);
    if (written > 0 && static_cast<std::size_t>(written) < sizeof(directory)) {
      state.config_length = join_path(state.config_path, k_max_path, directory, "cs2-config.cfg");
    }
  }
  return state.config_path;
}

// The side file the window's geometry is persisted in: the settings file's
// directory plus "menu-window.cfg".
//
// The settings block has no geometry fields (`wire.hpp` belongs to the
// settings workstream), so the geometry lives here and is written next to the
// settings file.
//
// The path is written into `out` (at most `size` bytes); false when it cannot
// be resolved.
[[nodiscard]] inline auto geometry_path(const wire::Settings& settings, char* out,
                                        std::size_t size) noexcept -> bool {
  if (out == nullptr || size == 0) {
    return false;
  }
  out[0] = '\0';
  const char* const config = config_path(settings);
  const char* slash = nullptr;
  for (const char* cursor = config; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      slash = cursor;
    }
  }
  if (slash == nullptr) {
    return false;
  }
  const std::size_t prefix = static_cast<std::size_t>(slash - config) + 1;
  const int written =
      std::snprintf(out, size, "%.*smenu-window.cfg", static_cast<int>(prefix), config);
  if (written <= 0 || static_cast<std::size_t>(written) >= size) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// Read the persisted geometry. A missing or unreadable file keeps the
// defaults.
inline void load_geometry(const wire::Settings& settings) noexcept {
  UiState& state = ui_state();
  if (state.geometry_loaded) {
    return;
  }
  state.geometry_loaded = true;
  char path[k_max_path] = {};
  if (!geometry_path(settings, path, sizeof(path))) {
    return;
  }
  std::FILE* const file = std::fopen(path, "rb");
  if (file == nullptr) {
    return;
  }
  float values[4] = {};
  const int read = std::fscanf(file, "%f %f %f %f", &values[0], &values[1], &values[2], &values[3]);
  std::fclose(file);
  if (read != 4) {
    return;
  }
  state.geometry.x = values[0];
  state.geometry.y = values[1];
  state.geometry.width = values[2];
  state.geometry.height = values[3];
}

// Write the geometry. The file is a convenience, not a requirement: a failed
// write is ignored.
inline void save_geometry(const wire::Settings& settings) noexcept {
  char path[k_max_path] = {};
  if (!geometry_path(settings, path, sizeof(path))) {
    return;
  }
  const WindowGeometry& geometry = ui_state().geometry;
  std::FILE* const file = std::fopen(path, "wb");
  if (file == nullptr) {
    return;
  }
  std::fprintf(file, "%.1f %.1f %.1f %.1f\n", geometry.x, geometry.y, geometry.width,
               geometry.height);
  std::fclose(file);
}

// --- The pages --------------------------------------------------------------

// The sections, in the order the sidebar lists them.
enum class Section : std::uint8_t {
  players = 0,
  world,
  ragebot,
  movement,
  visuals,
  config,
  about,
};

// One sidebar entry: the section's name, the subtitle shown for the current
// section and the key hints shown in the footer.
struct SectionEntry {
  Section section;
  const char* name;
  const char* subtitle;
  const char* hint;
};

inline constexpr SectionEntry k_sections[] = {
    {Section::players, "Players", "Player boxes, labels and colours",
     "The colours apply to what is drawn; the sliders are live."},
    {Section::world, "World", "Radar, sounds, grenades and dropped items",
     "The world ESP draws the radar, the sounds and the dropped items."},
    {Section::ragebot, "Ragebot", "The aim solver and its assists",
     "The trigger key is captured by the window: the game never sees it."},
    {Section::movement, "Movement", "Auto-strafe, bunnyhop and the camera",
     "The movement features run per tick in the payload."},
    {Section::visuals, "Visuals", "Chams and the menu's own look",
     "The accent and the transparency apply to this window."},
    {Section::config, "Config", "The settings file",
     "Save writes the settings file, Load reads it, Reset restores the "
     "defaults."},
    {Section::about, "About", "What this build is",
     "Insert or Delete toggles the menu while the game is in a map."},
};

inline constexpr int k_section_count = static_cast<int>(sizeof(k_sections) / sizeof(k_sections[0]));

// The Players page: the player ESP.
inline void draw_players(wire::Settings& settings) noexcept {
  ImGui::SeparatorText("Elements");
  toggle("Box", settings.box);
  toggle("Health bar", settings.health_bar);
  toggle("Ammo bar", settings.ammo_bar);
  toggle("Name", settings.name);
  toggle("Weapon", settings.weapon);
  toggle("Flags", settings.flags);

  ImGui::SeparatorText("Selection");
  toggle("Only enemies", settings.only_enemies);
  toggle("Show dead", settings.show_dead);
  toggle("Visible only", settings.esp_visible_only);
  toggle("Local player", settings.esp_local_player);
  toggle("Spectators", settings.esp_show_spectators);
  toggle("Teammates", settings.esp_show_teammates);
  // The switches are not independent: "Only enemies" is the master that
  // hides the local player and the teammates, and the two switches below it
  // bring them back. The relationship is stated here so a switch that does
  // not do what its label says is not a surprise.
  ImGui::TextDisabled("Only enemies hides the local player and the teammates;");
  ImGui::TextDisabled("Local player and Teammates bring them back.");

  ImGui::SeparatorText("Extras");
  toggle("Head box", settings.esp_head_box);
  toggle("Skeleton", settings.esp_skeleton);
  toggle("Tracer", settings.esp_tracer);
  if (settings.esp_tracer != 0) {
    static const char* const k_tracer_origins[] = {"Screen bottom", "Eye"};
    combo("Tracer origin", settings.esp_tracer_origin, k_tracer_origins, 2);
  }
  toggle("Off-screen arrows", settings.esp_offscreen);
  toggle("Glow (through wall)", settings.esp_glow);
  toggle("Distance", settings.esp_distance);
  toggle("Scale by distance", settings.esp_scale_by_distance);

  ImGui::SeparatorText("Box");
  slider_int("Thickness (0.5-5 px)", settings.esp_box_thickness, 0, 20);
  static const char* const k_box_styles[] = {"Straight", "Corner"};
  combo("Style", settings.esp_box_cornered, k_box_styles, 2);
  slider_float("Scale", settings.esp_max_box_scale, 0.25F, 2.0F, "%.2f x");

  ImGui::SeparatorText("Colours");
  color("Enemy", settings.esp_color_enemy);
  color("Team", settings.esp_color_team);
  color("Box", settings.esp_color_box);
  color("Skeleton", settings.esp_color_skeleton);
  color("Tracer", settings.esp_color_tracer);
  color("Off-screen", settings.esp_color_offscreen);
}

// The World page: everything that is not a player.
inline void draw_world(wire::Settings& settings) noexcept {
  ImGui::SeparatorText("Elements");
  toggle("Radar", settings.esp_radar);
  toggle("Sound", settings.esp_sound);
  toggle("Grenades", settings.esp_grenades);
  toggle("C4", settings.esp_c4);
  toggle("Dropped weapons", settings.esp_weapons);

  ImGui::SeparatorText("Range");
  slider_int("Max distance (m)", settings.esp_max_distance_m, 0, 255);
  ImGui::TextDisabled("0 = unlimited.");

  ImGui::SeparatorText("Colours");
  color("Sound", settings.esp_color_sound);
  color("Grenade", settings.esp_color_grenade);
  color("C4", settings.esp_color_c4);
}

// The Ragebot page: the aim solver and its assists.
inline void draw_ragebot(wire::Settings& settings, Renderer renderer) noexcept {
  toggle("Ragebot", settings.rage);
  // The trigger (`docs/ragebot.md`): hold the key, always on, or toggle it.
  static const char* const k_trigger_modes[] = {"Hold key", "Always on", "Toggle key"};
  combo("Trigger", settings.rage_mode, k_trigger_modes, 3);
  if (settings.rage_mode == 0) {
    keybind("Trigger key", settings.rage_key);
  }
  toggle("Auto fire", settings.rage_fire);

  ImGui::SeparatorText("Aim");
  // Objective: what the solve maximizes.
  static const char* const k_objectives[] = {"Damage", "Hitchance", "Expected damage"};
  combo("Objective", settings.rage_objective, k_objectives, 3);
  // Hitbox: which boxes the solve may pick.
  static const char* const k_hitboxes[] = {"Auto (head first)", "Head only", "Body only"};
  combo("Hitbox", settings.rage_hitbox, k_hitboxes, 3);
  slider_float("FOV", settings.rage_fov, 0.0F, 360.0F, "%.0f deg");
  slider_float("Min damage", settings.rage_min_damage, 0.0F, 200.0F, "%.0f");
  slider_float("Min hitchance", settings.rage_min_hitchance, 0.0F, 1.0F, "%.2f");

  ImGui::SeparatorText("Assists");
  // The common assists (`docs/ragebot.md`).
  static const char* const k_assist_modes[] = {"Off", "Always", "When firing"};
  combo("Auto-stop", settings.rage_autostop, k_assist_modes, 3);
  combo("Auto-scope", settings.rage_autoscope, k_assist_modes, 3);
  toggle("Auto-reload", settings.rage_autoreload);
  toggle("Only visible", settings.rage_only_visible);
  toggle("Recoil compensation", settings.rage_recoil);
  toggle("Backtrack", settings.rage_backtrack);
  slider_float("Auto-stop speed", settings.rage_stop_speed, 0.0F, 250.0F, "%.0f u/s");

  ImGui::SeparatorText("Per-weapon profile");
  // Per-weapon profiles (`rage_config.hpp`): the live sliders above are the
  // *global* tuning; save/load a weapon's copy, and `Per-weapon` applies the
  // profile matching the active weapon at solve time.
  toggle("Per-weapon config", settings.rage_per_weapon);
  text_input("Weapon key", settings.rage_weapon, sizeof(settings.rage_weapon));
  control_row("Profile");
  if (button("Save", 58.0F)) {
    request_rage_config_op(settings, 1, renderer);
  }
  ImGui::SameLine();
  if (button("Load", 58.0F)) {
    request_rage_config_op(settings, 2, renderer);
  }
  ImGui::SameLine();
  if (button("Delete", 58.0F)) {
    request_rage_config_op(settings, 5, renderer);
  }
  control_row("File");
  if (button("Save file", 80.0F)) {
    request_rage_config_op(settings, 3, renderer);
  }
  ImGui::SameLine();
  if (button("Load file", 80.0F)) {
    request_rage_config_op(settings, 4, renderer);
  }
}

// The Movement page: the movement features and the camera.
inline void draw_movement(wire::Settings& settings) noexcept {
  ImGui::SeparatorText("Auto-strafe");
  toggle("Auto-strafe", settings.auto_strafe);
  static const char* const k_strafe_modes[] = {"Off", "Camera-relative", "Directional"};
  combo("Mode", settings.movement_strafe_mode, k_strafe_modes, 3);
  slider_float("Wish speed", settings.movement_strafe_wishspeed, 0.0F, 3200.0F, "%.0f u/s");
  slider_int("Sub-ticks", settings.movement_strafe_subticks, 0, k_max_strafe_subticks);
  toggle("A/D stands down", settings.movement_strafe_ignore_keys);

  ImGui::SeparatorText("Bunnyhop");
  toggle("Bunnyhop", settings.bhop);

  ImGui::SeparatorText("Third person");
  toggle("Third person", settings.movement_thirdperson);
  slider_float("Distance", settings.movement_thirdperson_distance, 64.0F, 512.0F, "%.0f u");
  slider_float("Angle", settings.movement_thirdperson_angle, 0.0F, 360.0F, "%.0f deg");
  // The three settings below travel in the wire (its format is versioned and
  // other code reads them), but the game never sees them: the client has no
  // shoulder camera, the camera orbits the player's eye (so there is no
  // pivot) and the view setup's FOV field is unconfirmed. They are greyed
  // out with the reason, so a slider that does nothing does not look like a
  // broken feature.
  ImGui::BeginDisabled(true);
  slider_float("Pivot height", settings.movement_thirdperson_pivot_z, 0.0F, 64.0F, "%.0f u");
  slider_float("FOV", settings.movement_thirdperson_fov, 0.0F, 180.0F, "%.0f deg");
  static const char* const k_sides[] = {"Left shoulder", "Right shoulder"};
  combo("Shoulder", settings.movement_thirdperson_side, k_sides, 2);
  ImGui::EndDisabled();
  ImGui::TextDisabled("Pivot, FOV and shoulder are inert: no shoulder camera,");
  ImGui::TextDisabled("the camera orbits the eye, and the FOV field is unconfirmed.");
}

// The Visuals page: the chams and the menu's own look.
inline void draw_visuals(wire::Settings& settings) noexcept {
  ImGui::SeparatorText("Chams");
  toggle("Chams", settings.visuals_chams);
  static const char* const k_chams_modes[] = {"Flat", "Glow", "Wireframe"};
  combo("Chams mode", settings.visuals_chams_mode, k_chams_modes, 3);
  color("Chams colour", settings.visuals_chams_color);
  toggle("Visible only", settings.visuals_chams_visible);
  toggle("Local player", settings.visuals_chams_local);

  ImGui::SeparatorText("Menu");
  keybind("Open key", settings.menu_key);
  color("Accent", settings.menu_accent);
  slider_int("Transparency", settings.menu_transparency, 0, 100);
  toggle("Debug HUD", settings.show_hud);
  ImGui::TextDisabled("Insert or Delete also toggles the menu.");
}

// The Config page: the settings file and its one-shot op protocol.
//
// The buttons only *ask*: the op code and the token travel in the settings
// block and the payload carries the op out (ADR-0021), so the file is written
// by the one owner of the configuration.
//
// `renderer` is the renderer that raised the op: the token carries it, so the
// payload's op bookkeeping cannot mistake this renderer's op for the other
// renderer's (the two edit different blocks).
inline void draw_config(wire::Settings& settings, Renderer renderer) noexcept {
  const char* const path = config_path(settings);
  ImGui::TextUnformatted("Settings file");
  if (path[0] == '\0') {
    ImGui::TextDisabled("the payload has not published a path");
  } else {
    ImGui::TextWrapped("%s", path);
  }

  ImGui::SeparatorText("Actions");
  control_row("File");
  if (button("Save", 58.0F)) {
    request_config_op(settings, 1, renderer);
  }
  ImGui::SameLine();
  if (button("Load", 58.0F)) {
    request_config_op(settings, 2, renderer);
  }
  ImGui::SameLine();
  if (button("Reset", 58.0F)) {
    request_config_op(settings, 3, renderer);
  }
  ImGui::TextDisabled("The payload writes the file; the menu only asks.");
}

// The About page: what this build is and which keys do what.
inline void draw_about(const char* renderer_label) noexcept {
  ImGui::TextUnformatted(k_product_name);
  ImGui::TextDisabled("build %s", k_build_info);
  ImGui::TextDisabled("branch %s", k_build_branch);
  if (renderer_label != nullptr) {
    ImGui::TextDisabled("renderer: %s", renderer_label);
  }
  ImGui::TextDisabled("one menu, two renderers: the payload and the Vulkan layer");

  ImGui::SeparatorText("Keys");
  ImGui::BulletText("Insert or Delete: toggle the menu (the game must be in a map)");
  ImGui::BulletText("Escape or Delete while capturing: clear the binding");
  ImGui::BulletText("The captured key is swallowed: the game never sees it");
}

// --- The window -------------------------------------------------------------

// Draw the menu's content: the header bar, the sidebar, the selected page and
// the footer.
//
// `renderer_label` names the renderer that draws the menu (the About page
// shows it); it may be null. `renderer` is that renderer's identity: the
// config op's token carries it.
inline void draw_pages(wire::Settings& settings, const char* renderer_label = nullptr,
                       Renderer renderer = Renderer::payload) noexcept {
  UiState& state = ui_state();
  if (state.section < 0 || state.section >= k_section_count) {
    state.section = 0;
  }
  const SectionEntry& current = k_sections[state.section];

  // --- The header bar ---
  ImGui::TextUnformatted(k_product_name);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", k_build_info);
  if (renderer_label != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", renderer_label);
  }
  ImGui::Separator();

  // --- The sidebar and the page ---
  const float footer_height = ImGui::GetFrameHeightWithSpacing();
  const float row_height = ImGui::GetContentRegionAvail().y - footer_height;
  // The current section is highlighted in the accent colour.
  const ImVec4 accent = ImVec4(static_cast<float>(settings.menu_accent[0]) / 255.0F,
                               static_cast<float>(settings.menu_accent[1]) / 255.0F,
                               static_cast<float>(settings.menu_accent[2]) / 255.0F, 0.35F);
  if (ImGui::BeginChild("##sidebar", ImVec2(k_sidebar_width, row_height),
                        ImGuiChildFlags_Borders)) {
    for (int index = 0; index < k_section_count; ++index) {
      const bool selected = index == state.section;
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Header, accent);
      }
      if (ImGui::Selectable(k_sections[index].name, selected)) {
        state.section = index;
      }
      if (selected) {
        ImGui::PopStyleColor();
      }
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", current.subtitle);
  }
  ImGui::EndChild();
  ImGui::SameLine();
  if (ImGui::BeginChild("##content", ImVec2(0.0F, row_height), ImGuiChildFlags_Borders)) {
    switch (static_cast<Section>(state.section)) {
      case Section::players:
        draw_players(settings);
        break;
      case Section::world:
        draw_world(settings);
        break;
      case Section::ragebot:
        draw_ragebot(settings, renderer);
        break;
      case Section::movement:
        draw_movement(settings);
        break;
      case Section::visuals:
        draw_visuals(settings);
        break;
      case Section::config:
        draw_config(settings, renderer);
        break;
      case Section::about:
        draw_about(renderer_label);
        break;
    }
  }
  ImGui::EndChild();

  // --- The footer: the current page's key hints ---
  ImGui::Separator();
  ImGui::TextDisabled("%s", current.hint);
}

// Draw the whole menu: the window around the content.
//
// Returns false when the user closed the window with its close button.
[[nodiscard]] inline auto draw_menu(wire::Settings& settings, const char* renderer_label = nullptr,
                                    Renderer renderer = Renderer::payload) noexcept -> bool {
  UiState& state = ui_state();
  load_geometry(settings);

  // The geometry is only pushed when it changed, so the user can still move
  // and resize the window.
  if (!state.geometry_applied) {
    ImGui::SetNextWindowPos(ImVec2(state.geometry.x, state.geometry.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(state.geometry.width, state.geometry.height), ImGuiCond_Always);
    state.geometry_applied = true;
  }
  // The window is at least the minimum and at most the screen.
  //
  // Dear ImGui reads the second argument as a **real** maximum
  // (`ImClamp(size, min, max)` in `imgui.cpp`), so "no maximum" is `FLT_MAX`:
  // a zero maximum clamps the window to `ImClamp(size, 520, 0) == 0` and then
  // to ImGui's 32x32 floor, which hides the whole menu.
  // The window is at least the minimum and at most the screen.
  //
  // Dear ImGui reads the second argument as a **real** maximum
  // (`ImClamp(size, min, max)` in `imgui.cpp`), so "no maximum" is `FLT_MAX`:
  // a zero maximum clamps the window to `ImClamp(size, 520, 0) == 0` and then
  // to ImGui's 32x32 floor, which hides the whole menu.
  ImGui::SetNextWindowSizeConstraints(ImVec2(k_min_width, k_min_height), ImVec2(FLT_MAX, FLT_MAX));

  // The accent and the transparency are the menu's own settings.
  const ImVec4 accent = ImVec4(static_cast<float>(settings.menu_accent[0]) / 255.0F,
                               static_cast<float>(settings.menu_accent[1]) / 255.0F,
                               static_cast<float>(settings.menu_accent[2]) / 255.0F,
                               static_cast<float>(settings.menu_accent[3]) / 255.0F);
  ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  const float opacity = 0.25F + 0.75F * (static_cast<float>(settings.menu_transparency) / 100.0F);
  background.w *= opacity;

  bool open = true;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, accent);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accent);
  if (ImGui::Begin(k_window_title, &open, ImGuiWindowFlags_NoSavedSettings)) {
    draw_pages(settings, renderer_label, renderer);
    // The window's geometry follows the window (the user may have moved or
    // resized it).
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 position = ImGui::GetWindowPos();
    if (size.x != state.geometry.width || size.y != state.geometry.height ||
        position.x != state.geometry.x || position.y != state.geometry.y) {
      state.geometry.width = size.x;
      state.geometry.height = size.y;
      state.geometry.x = position.x;
      state.geometry.y = position.y;
      state.geometry_dirty = true;
    }
    // The geometry is written once the drag has settled: a resize changes it
    // every frame and the file only needs the final value.
    if (state.geometry_dirty) {
      if (state.save_timer == 0) {
        state.save_timer = 30U;
      } else if (--state.save_timer == 0U) {
        save_geometry(settings);
        state.geometry_dirty = false;
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleColor();
  ImGui::PopStyleColor();
  ImGui::PopStyleColor();
  return open;
}

}  // namespace cs2::overlay::menu
