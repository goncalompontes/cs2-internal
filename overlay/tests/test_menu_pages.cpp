// Host test for the shared menu header's smaller promises
// (`cs2/overlay/menu_pages.hpp`).
//
// The header is the one definition of the menu's content for both renderers,
// and it is the only part of the menu that can be exercised without a game:
// Dear ImGui runs headless, so the real code paths are driven with a real
// ImGui context.
//
// The cases: the mouse buttons' names follow the physical button, the two
// renderers' config-op tokens cannot collide, a cancelled capture drops its
// result, a captured key's character and key release never reach the text
// field, and the menu's settings path is the same file the payload's store
// uses.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "cs2/features/config_store.hpp"
#include "cs2/overlay/wire.hpp"

#include "menu_test_fixture.hpp"

namespace {

using cs2::overlay::menu::Renderer;
using cs2::overlay::wire::Settings;

auto set_env(const char* name, const char* value) -> bool {
#if defined(_WIN32)
  return _putenv_s(name, value) == 0;
#else
  return setenv(name, value, 1) == 0;
#endif
}

auto unset_env(const char* name) -> bool {
#if defined(_WIN32)
  return _putenv_s(name, "") == 0;
#else
  return unsetenv(name) == 0;
#endif
}

// The number of text events ImGui's input queue holds.
//
// Dear ImGui 1.91 queues the characters as **events**, not as a character
// buffer, so the queue is what the text fields read.
auto text_event_count() -> int {
  ImGuiContext* context = ImGui::GetCurrentContext();
  int count = 0;
  for (int index = 0; index < context->InputEventsQueue.Size; ++index) {
    if (context->InputEventsQueue[index].Type == ImGuiInputEventType_Text) {
      ++count;
    }
  }
  return count;
}

// --- the key names ------------------------------------------------------------

// The mouse buttons are named after the **physical** button.
//
// The capture turns `WM_XBUTTONDOWN` into `0x05`/`0x06` (the fourth and the
// fifth button), so `0x05` is "Mouse 4" and `0x06` is "Mouse 5" — the
// convention the pre-T2 menu used. `0x04` is the middle button and `0x03` is
// `VK_CANCEL`, which is not a mouse button at all.
void test_key_names() {
  using cs2::overlay::menu::key_name;
  assert(std::strcmp(key_name(0x05), "Mouse 4") == 0);
  assert(std::strcmp(key_name(0x06), "Mouse 5") == 0);
  assert(std::strcmp(key_name(0x04), "Middle Mouse") == 0);
  assert(std::strcmp(key_name(0x03), "Unknown") == 0);
  assert(std::strcmp(key_name(0x01), "Mouse 1") == 0);
  assert(std::strcmp(key_name(0x02), "Mouse 2") == 0);
  assert(std::strcmp(key_name(0x2D), "Insert") == 0);
  assert(std::strcmp(key_name(0x2E), "Delete") == 0);
  assert(std::strcmp(key_name(0x1B), "Escape") == 0);
  assert(std::strcmp(key_name(0x41), "A") == 0);
  assert(std::strcmp(key_name(0x30), "0") == 0);
  assert(std::strcmp(key_name(0x70), "F1") == 0);
  assert(std::strcmp(key_name(0x00), "None") == 0);
}

// --- the one-shot config op --------------------------------------------------

// The two renderers' config-op tokens never collide.
//
// The payload's in-process menu and the Vulkan layer's menu edit **different**
// blocks, but the payload carries the one-shot op out for both of them, and
// the op's bookkeeping is a single "the last token I acted on". A shared
// counter would let the second renderer's op be swallowed by the first one's.
void test_config_op_tokens_do_not_collide() {
  const std::filesystem::path path =
      cs2::overlay::menu::test::temp_directory() / "cs2-menu-op-test.cfg";
  std::error_code error;
  std::filesystem::remove(path, error);
  assert(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()));

  Settings payload_block = cs2::overlay::wire::default_settings();
  Settings layer_block = cs2::overlay::wire::default_settings();

  // The payload's menu asks for a save.
  cs2::overlay::menu::request_config_op(payload_block, 1, Renderer::payload);
  // The layer's menu asks for a save: the same op, its own token.
  cs2::overlay::menu::request_config_op(layer_block, 1, Renderer::layer);
  assert(payload_block.menu_config_token != layer_block.menu_config_token);
  // The tokens live in disjoint spaces: the payload's are even, the layer's
  // are odd.
  assert((payload_block.menu_config_token & 1U) == 0U);
  assert((layer_block.menu_config_token & 1U) == 1U);

  cs2::features::Config config{};
  cs2::features::ConfigOpState state;
  // The payload's op runs...
  assert(cs2::features::apply_pending_op(config, payload_block, state).ran);
  // ... and the layer's op is **not** swallowed by it.
  assert(cs2::features::apply_pending_op(config, layer_block, state).ran);

  // The same block with the same token does not run the op twice: one click is
  // one op.
  assert(!cs2::features::apply_pending_op(config, layer_block, state).ran);

  // The layer's next op and the payload's next op both run.
  cs2::overlay::menu::request_config_op(layer_block, 2, Renderer::layer);
  assert(cs2::features::apply_pending_op(config, layer_block, state).ran);
  cs2::overlay::menu::request_config_op(payload_block, 2, Renderer::payload);
  assert(cs2::features::apply_pending_op(config, payload_block, state).ran);

  // The ragebot's per-weapon op carries the renderer the same way.
  Settings payload_rage = cs2::overlay::wire::default_settings();
  Settings layer_rage = cs2::overlay::wire::default_settings();
  cs2::overlay::menu::request_rage_config_op(payload_rage, 1, Renderer::payload);
  cs2::overlay::menu::request_rage_config_op(layer_rage, 1, Renderer::layer);
  assert(payload_rage.rage_config_token != layer_rage.rage_config_token);
  assert((payload_rage.rage_config_token & 1U) == 0U);
  assert((layer_rage.rage_config_token & 1U) == 1U);

  std::filesystem::remove(path, error);
}

// --- the capture --------------------------------------------------------------

// A cancelled capture drops its result: a key captured just before the menu
// closed must not be applied when the menu reopens.
void test_cancelled_capture_drops_its_result() {
  auto& capture = cs2::overlay::menu::keybind_capture();
  cs2::overlay::menu::arm_capture(0x1234U);
  assert(capture.armed.load());
  assert(cs2::overlay::menu::capture_key('A'));
  assert(capture.has_result.load());
  cs2::overlay::menu::cancel_capture();
  assert(!capture.armed.load());
  assert(!capture.has_result.load());
  assert(capture.target.load() == 0U);
}

// A captured key's character and its key release never reach ImGui: the key
// was swallowed, so the text field the user is editing must not see it.
void test_captured_key_does_not_reach_the_text_field() {
  const cs2::overlay::menu::test::ImGuiFixture fixture;
  ImGuiIO& io = ImGui::GetIO();

  // The key is captured: its character is dropped.
  cs2::overlay::menu::arm_capture(0x1234U);
  assert(cs2::overlay::menu::capture_key('A'));
  const int before = text_event_count();
  cs2::overlay::menu::feed_character(io, 'A');
  assert(text_event_count() == before);

  // Its key release is dropped too: the key never went down.
  cs2::overlay::menu::feed_key_up(io, 'A');

  // A key the capture did not take reaches the text field.
  cs2::overlay::menu::feed_character(io, 'B');
  assert(text_event_count() == before + 1);
}

// --- the settings path --------------------------------------------------------

// The menu's path is the file the payload's store uses.
//
// The header's fallback and `features::default_config_path()` resolve the
// path independently, so they have to agree: the menu shows the path the
// payload writes.
void test_config_path_matches_the_store() {
  const std::filesystem::path directory = cs2::overlay::menu::test::temp_directory();
  const std::filesystem::path config = directory / "cs2-config.cfg";
  const std::filesystem::path override_dir = directory / "cs2-menu-dir";

  Settings settings = cs2::overlay::wire::default_settings();
  // The published path wins: it is the payload's own.
  std::snprintf(settings.menu_config_path, sizeof(settings.menu_config_path), "%s",
                config.string().c_str());
  assert(std::strcmp(cs2::overlay::menu::config_path(settings), config.string().c_str()) == 0);

  // The fallback: the store's resolution, case by case.
  struct Case {
    const char* variable;
    const char* value;
  };
  const Case cases[] = {
      {"CS2_PAYLOAD_CONFIG", config.string().c_str()},
      {"CS2_PAYLOAD_DIR", override_dir.string().c_str()},
  };
  for (const Case& entry : cases) {
    // The published path is dropped, so the fallback is resolved.
    settings.menu_config_path[0] = '\0';
    cs2::overlay::menu::ui_state().published_length = 0;
    cs2::overlay::menu::ui_state().published_path[0] = '\0';
    cs2::overlay::menu::ui_state().config_length = 0;
    cs2::overlay::menu::ui_state().config_path[0] = '\0';
    assert(set_env(entry.variable, entry.value));
    char store_path[cs2::features::k_max_config_path] = {};
    assert(cs2::features::default_config_path(store_path, sizeof(store_path)) > 0);
    const char* const menu_path = cs2::overlay::menu::config_path(settings);
    assert(std::strcmp(menu_path, store_path) == 0);
  }

  // Without any override the store falls back to the local app data.
  settings.menu_config_path[0] = '\0';
  cs2::overlay::menu::ui_state().published_length = 0;
  cs2::overlay::menu::ui_state().published_path[0] = '\0';
  cs2::overlay::menu::ui_state().config_length = 0;
  cs2::overlay::menu::ui_state().config_path[0] = '\0';
  assert(unset_env("CS2_PAYLOAD_CONFIG"));
  assert(unset_env("CS2_PAYLOAD_DIR"));
  char store_path[cs2::features::k_max_config_path] = {};
  const std::size_t store_length =
      cs2::features::default_config_path(store_path, sizeof(store_path));
  if (store_length > 0) {
    const char* const menu_path = cs2::overlay::menu::config_path(settings);
    assert(std::strcmp(menu_path, store_path) == 0);
  }
}

}  // namespace

auto main() -> int {
  test_key_names();
  test_config_op_tokens_do_not_collide();
  test_cancelled_capture_drops_its_result();
  test_captured_key_does_not_reach_the_text_field();
  test_config_path_matches_the_store();
  std::puts("menu-pages: ok");
  return 0;
}
