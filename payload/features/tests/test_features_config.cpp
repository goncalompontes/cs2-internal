// Host test for the settings foundation: the wire <-> config conversion and the
// config store's round trip.
//
// The settings block is a POD and the conversion is pure, so both can be
// exercised natively (no game, no payload). The test is deliberately
// *exhaustive*: the sentinel block below holds a distinct, legal value in
// every field of `wire::Settings`, and the round trip has to come back
// byte-identical. A field that the conversion forgets — in either direction —
// changes a byte and fails the test.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "cs2/features/config_store.hpp"
#include "cs2/features/settings.hpp"

namespace {

using cs2::features::Config;
using cs2::overlay::wire::Settings;

// The settings block with a distinct, legal value in **every** field.
//
// Every value differs from the field's default (so a dropped field cannot hide
// behind a zero) and stays inside the field's legal range (so the clamping in
// `from_wire_settings` does not change it).
auto sentinel_settings() -> Settings {
  Settings settings{};

  // --- ESP: the elements ---
  settings.box = 0;
  settings.health_bar = 0;
  settings.ammo_bar = 0;
  settings.name = 0;
  settings.weapon = 0;
  settings.flags = 0;
  settings.only_enemies = 0;
  settings.show_dead = 1;
  settings.hitboxes = 1;
  settings.hitbox_mode = 2;
  settings.hitbox_color_mode = 1;
  settings.hitbox_color[0] = 1;
  settings.hitbox_color[1] = 2;
  settings.hitbox_color[2] = 3;
  settings.hitbox_color[3] = 4;

  // --- ESP: the added player elements ---
  settings.esp_box_thickness = 7;
  settings.esp_box_cornered = 1;
  settings.esp_head_box = 1;
  settings.esp_skeleton = 1;
  settings.esp_tracer = 1;
  settings.esp_tracer_origin = 1;
  settings.esp_offscreen = 1;
  settings.esp_glow = 1;
  settings.esp_visible_only = 1;
  settings.esp_distance = 1;
  settings.esp_scale_by_distance = 1;
  settings.esp_max_distance_m = 33;
  settings.esp_radar = 1;
  settings.esp_sound = 1;
  settings.esp_grenades = 1;
  settings.esp_c4 = 1;
  settings.esp_weapons = 1;
  settings.esp_local_player = 1;
  settings.esp_show_spectators = 1;
  settings.esp_show_teammates = 1;
  settings.esp_max_box_scale = 1.75F;
  settings.esp_color_enemy[0] = 11;
  settings.esp_color_enemy[1] = 12;
  settings.esp_color_enemy[2] = 13;
  settings.esp_color_enemy[3] = 14;
  settings.esp_color_team[0] = 21;
  settings.esp_color_team[1] = 22;
  settings.esp_color_team[2] = 23;
  settings.esp_color_team[3] = 24;
  settings.esp_color_box[0] = 31;
  settings.esp_color_box[1] = 32;
  settings.esp_color_box[2] = 33;
  settings.esp_color_box[3] = 34;
  settings.esp_color_skeleton[0] = 41;
  settings.esp_color_skeleton[1] = 42;
  settings.esp_color_skeleton[2] = 43;
  settings.esp_color_skeleton[3] = 44;
  settings.esp_color_tracer[0] = 51;
  settings.esp_color_tracer[1] = 52;
  settings.esp_color_tracer[2] = 53;
  settings.esp_color_tracer[3] = 54;
  settings.esp_color_offscreen[0] = 61;
  settings.esp_color_offscreen[1] = 62;
  settings.esp_color_offscreen[2] = 63;
  settings.esp_color_offscreen[3] = 64;
  settings.esp_color_sound[0] = 71;
  settings.esp_color_sound[1] = 72;
  settings.esp_color_sound[2] = 73;
  settings.esp_color_sound[3] = 74;
  settings.esp_color_grenade[0] = 81;
  settings.esp_color_grenade[1] = 82;
  settings.esp_color_grenade[2] = 83;
  settings.esp_color_grenade[3] = 84;
  settings.esp_color_c4[0] = 91;
  settings.esp_color_c4[1] = 92;
  settings.esp_color_c4[2] = 93;
  settings.esp_color_c4[3] = 94;

  // --- Movement ---
  settings.auto_strafe = 1;
  settings.bhop = 1;
  settings.movement_strafe_mode = 2;
  settings.movement_strafe_ignore_keys = 1;
  settings.movement_strafe_wishspeed = 250.0F;
  settings.movement_strafe_subticks = 8;
  settings.movement_thirdperson = 1;
  settings.movement_thirdperson_distance = 320.0F;
  settings.movement_thirdperson_angle = 45.0F;
  settings.movement_thirdperson_pivot_z = 12.5F;
  settings.movement_thirdperson_fov = 90.0F;
  settings.movement_thirdperson_side = 0;

  // --- Ragebot ---
  settings.rage = 1;
  settings.rage_key = 0x41;
  settings.rage_fire = 1;
  settings.rage_objective = 2;
  settings.rage_hitbox = 2;
  settings.rage_fov = 30.5F;
  settings.rage_min_damage = 42.25F;
  settings.rage_min_hitchance = 0.75F;
  settings.rage_autostop = 2;
  settings.rage_autoscope = 1;
  settings.rage_autoreload = 1;
  settings.rage_only_visible = 1;
  settings.rage_mode = 2;
  settings.rage_per_weapon = 1;
  settings.rage_config_op = 3;
  settings.rage_config_token = 123456U;
  std::memcpy(settings.rage_weapon, "weapon_ak47", sizeof("weapon_ak47"));
  settings.rage_recoil = 1;
  settings.rage_stop_speed = 100.5F;
  settings.rage_backtrack = 1;

  // --- Visuals (chams) ---
  settings.visuals_chams = 1;
  settings.visuals_chams_mode = 2;
  settings.visuals_chams_visible = 1;
  settings.visuals_chams_local = 1;
  settings.visuals_chams_color[0] = 101;
  settings.visuals_chams_color[1] = 102;
  settings.visuals_chams_color[2] = 103;
  settings.visuals_chams_color[3] = 104;

  // --- Menu / config ---
  settings.show_hud = 0;
  settings.menu_open = 1;
  settings.menu_key = 0x41;
  settings.menu_accent[0] = 111;
  settings.menu_accent[1] = 112;
  settings.menu_accent[2] = 113;
  settings.menu_accent[3] = 114;
  settings.menu_transparency = 55;
  settings.menu_config_op = 2;
  settings.menu_config_token = 987654U;
  std::snprintf(settings.menu_config_path, sizeof(settings.menu_config_path), "%s",
                "C:\\Users\\test\\cs2 config.cfg");

  return settings;
}

auto same_settings(const Settings& left, const Settings& right) -> bool {
  return std::memcmp(&left, &right, sizeof(Settings)) == 0;
}

auto set_env(const char* name, const char* value) -> bool {
#if defined(_WIN32)
  return _putenv_s(name, value) == 0;
#else
  return setenv(name, value, 1) == 0;
#endif
}

auto temp_path(const char* name) -> std::filesystem::path {
  std::error_code error;
  auto directory = std::filesystem::temp_directory_path(error);
  if (error) {
    return std::filesystem::path(name);
  }
  return directory / name;
}

// The conversion is exhaustive in both directions: every field of the settings
// block survives a round trip, byte for byte.
auto test_wire_round_trip() -> void {
  const Settings original = sentinel_settings();
  const Config config = cs2::features::from_wire_settings(original);
  const Settings restored = cs2::features::to_wire_settings(config);
  assert(same_settings(original, restored));
}

// The defaults of the typed config and the shared wire defaults agree, so the
// payload and the Vulkan layer can never disagree about a fresh segment.
auto test_defaults_agree() -> void {
  const Settings shared = cs2::overlay::wire::default_settings();
  assert(same_settings(cs2::features::default_wire_settings(), shared));
  assert(same_settings(cs2::features::to_wire_settings(Config{}), shared));
}

// A corrupt or stale shared segment cannot push a value out of its range.
auto test_clamping() -> void {
  Settings settings = cs2::overlay::wire::default_settings();

  settings.esp_max_box_scale = 100.0F;
  settings.esp_box_thickness = 200;
  settings.esp_box_cornered = 9;
  settings.esp_tracer_origin = 9;
  settings.esp_max_distance_m = 250;  // the whole byte range is legal
  settings.movement_strafe_mode = 9;
  settings.movement_strafe_wishspeed = 99999.0F;
  settings.movement_thirdperson = 1;
  settings.movement_thirdperson_distance = 1000.0F;
  settings.movement_thirdperson_angle = 400.0F;
  settings.movement_thirdperson_pivot_z = 100.0F;
  settings.movement_thirdperson_fov = 500.0F;
  settings.movement_thirdperson_side = 9;
  settings.visuals_chams_mode = 9;
  settings.menu_transparency = 200;
  settings.menu_config_op = 9;
  settings.hitbox_mode = 9;
  settings.rage_objective = 9;
  settings.rage_hitbox = 9;
  settings.rage_autostop = 9;
  settings.rage_autoscope = 9;
  settings.rage_mode = 9;
  settings.rage_fov = 1000.0F;
  settings.rage_min_damage = 1000.0F;
  settings.rage_min_hitchance = 5.0F;
  settings.rage_stop_speed = 99999.0F;

  const Config config = cs2::features::from_wire_settings(settings);
  assert(config.esp.max_box_scale == 2.0F);
  assert(config.esp.box_thickness == 20);
  assert(config.esp.box_style == cs2::features::BoxStyle::straight);
  assert(config.esp.tracer_origin == cs2::features::TracerOrigin::screen_bottom);
  assert(config.esp.max_distance_m == 250);
  assert(config.movement.strafe_mode == cs2::features::StrafeMode::camera_relative);
  assert(config.movement.strafe_wishspeed == 3200.0F);
  assert(config.movement.thirdperson_distance == 512.0F);
  assert(config.movement.thirdperson_angle == 360.0F);
  assert(config.movement.thirdperson_pivot_z == 64.0F);
  assert(config.movement.thirdperson_fov == 180.0F);
  assert(config.movement.thirdperson_side == cs2::features::ThirdPersonSide::right);
  assert(config.visuals.chams_mode == cs2::features::ChamsMode::flat);
  assert(config.menu.transparency == 100);
  assert(config.menu.config_op == 0);
  assert(config.esp.hitbox_mode == cs2::features::HitboxMode::outline);
  assert(config.rage.objective == 1);
  assert(config.rage.hitbox == 0);
  assert(config.rage.autostop == 0);
  assert(config.rage.autoscope == 0);
  assert(config.rage.mode == 0);
  assert(config.rage.fov == 360.0F);
  assert(config.rage.min_damage == 200.0F);
  assert(config.rage.min_hitchance == 1.0F);
  assert(config.rage.stop_speed == 3200.0F);

  // The low ends.
  settings.esp_max_box_scale = -5.0F;
  settings.movement_thirdperson_distance = 1.0F;
  const Config low = cs2::features::from_wire_settings(settings);
  assert(low.esp.max_box_scale == 0.25F);
  assert(low.movement.thirdperson_distance == 64.0F);

  // A NaN must not reach a slider.
  settings.esp_max_box_scale = std::nanf("");
  const Config not_a_number = cs2::features::from_wire_settings(settings);
  assert(not_a_number.esp.max_box_scale == 0.25F);
}

// The store round-trips every field of the settings block.
auto test_store_round_trip() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-test.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);

  Config config = cs2::features::from_wire_settings(sentinel_settings());
  assert(cs2::features::save(path, config));

  const auto loaded = cs2::features::load(path);
  assert(loaded.has_value());
  assert(*loaded == config);

  // A missing file is not an error the caller has to handle: it just means
  // "keep the defaults".
  std::filesystem::remove(path, error);
  assert(!cs2::features::load(path).has_value());
  std::filesystem::remove(path, error);
}

// A corrupt file is rejected (the caller keeps its defaults); an unknown key is
// ignored and a missing key keeps the default.
auto test_store_rejects_garbage() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-garbage.cfg");
  std::error_code error;

  auto write = [&](const char* text) -> void {
#if defined(_WIN32)
    std::FILE* file = nullptr;
    assert(fopen_s(&file, path.string().c_str(), "wb") == 0 && file != nullptr);
#else
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    assert(file != nullptr);
#endif
    const std::size_t written = std::fwrite(text, 1, std::strlen(text), file);
    assert(written == std::strlen(text));
    std::fclose(file);
  };

  write(
      "# a comment\n"
      "esp.box = 0\n"
      "esp.box = not-a-number\n");
  assert(!cs2::features::load(path).has_value());

  write("this line has no equals sign\n");
  assert(!cs2::features::load(path).has_value());

  write(
      "esp.box = 0\n"
      "esp.unknown_setting = 5\n"
      "unknown_section.box = 5\n");
  const auto loaded = cs2::features::load(path);
  assert(loaded.has_value());
  assert(!loaded->esp.box);
  assert(loaded->esp.health_bar);  // the default, the file did not name it

  std::filesystem::remove(path, error);
}

// The settings block's checksum.
//
// The block has two writers (the layer's menu and the payload), so a reader
// has to be able to tell a whole block from a torn one. The checksum is
// computed in the shared header, so the payload and the layer cannot disagree
// about it; the golden value in the header pins the two builds together.
auto test_settings_checksum() -> void {
  using cs2::overlay::wire::default_settings;
  using cs2::overlay::wire::k_default_settings_checksum;
  using cs2::overlay::wire::settings_checksum;

  assert(settings_checksum(default_settings()) == k_default_settings_checksum);

  // One changed byte changes the checksum.
  const Settings original = default_settings();
  Settings changed = original;
  changed.esp_color_enemy[0] = static_cast<std::uint8_t>(changed.esp_color_enemy[0] + 1);
  assert(settings_checksum(changed) != settings_checksum(original));

  // A clean round trip through the segment keeps the checksum.
  // The segment is 3 MB, so it lives in the static storage, not on the stack.
  static cs2::overlay::wire::Shared shared{};
  shared.magic = cs2::overlay::wire::k_magic;
  shared.version = cs2::overlay::wire::k_version;
  cs2::overlay::wire::write_settings(shared, changed);
  Settings copy{};
  assert(cs2::overlay::wire::read_settings(shared, copy));
  assert(settings_checksum(copy) == settings_checksum(changed));
  assert(std::memcmp(&copy, &changed, sizeof(Settings)) == 0);
}

// A torn read is always detected.
//
// The settings block is written by two processes that share no lock, so a
// reader can see a block that is half of one write and half of the other. The
// seqlock alone cannot catch that — the sequence is even and stable when the
// read happens — so the block carries its own checksum.
auto test_torn_read_is_detected() -> void {
  using cs2::overlay::wire::k_magic;
  using cs2::overlay::wire::k_version;
  using cs2::overlay::wire::settings_checksum;

  // The segment is 3 MB, so it lives in the static storage, not on the stack.
  static cs2::overlay::wire::Shared shared{};
  shared.magic = k_magic;
  shared.version = k_version;
  Settings first = cs2::overlay::wire::default_settings();
  first.esp_color_enemy[0] = 11;
  first.esp_color_offscreen[0] = 33;
  Settings second = cs2::overlay::wire::default_settings();
  second.esp_color_enemy[0] = 22;
  second.esp_color_offscreen[0] = 44;

  auto* bytes = reinterpret_cast<unsigned char*>(&shared.settings);
  // The split falls inside the ESP colour block, so the two writers differ on
  // **both** sides of it: the torn block is neither of them.
  constexpr std::size_t k_split = 120;
  std::uint32_t sequence = shared.settings_sequence.load();

  // Writer A: the first half of its block, with its checksum.
  shared.settings_sequence.store(sequence + 1);
  std::memcpy(bytes, &first, k_split);
  shared.settings_checksum.store(settings_checksum(first));
  shared.settings_sequence.store(sequence + 2);

  // Writer B: the whole block, with its own checksum.
  shared.settings_sequence.store(sequence + 3);
  std::memcpy(bytes, &second, sizeof(Settings));
  shared.settings_checksum.store(settings_checksum(second));
  shared.settings_sequence.store(sequence + 4);

  // Writer A: the second half of its block lands after B's write. The block is
  // now torn, but the sequence is even and stable: only the checksum can tell.
  shared.settings_sequence.store(sequence + 5);
  std::memcpy(bytes + k_split, reinterpret_cast<const unsigned char*>(&first) + k_split,
              sizeof(Settings) - k_split);
  shared.settings_sequence.store(sequence + 6);

  Settings copy{};
  std::uint32_t read_sequence = 0;
  assert(!cs2::overlay::wire::read_settings(shared, copy, &read_sequence));

  // A block that was not torn is read back unchanged.
  cs2::overlay::wire::write_settings(shared, first);
  assert(cs2::overlay::wire::read_settings(shared, copy, &read_sequence));
  assert(std::memcmp(&copy, &first, sizeof(Settings)) == 0);
}

// A stale binary refuses a segment it does not understand: the version is
// checked before the block is read, so a layer that does not know the
// checksum never touches the block.
auto test_segment_version() -> void {
  using cs2::overlay::wire::k_magic;
  using cs2::overlay::wire::k_version;
  using cs2::overlay::wire::segment_is_compatible;

  assert(segment_is_compatible(k_magic, k_version));
  assert(!segment_is_compatible(k_magic, k_version - 1));
  assert(!segment_is_compatible(k_magic, k_version + 1));
  assert(!segment_is_compatible(0x12345678U, k_version));
}

// The one-shot op protocol: the menu sets the op and bumps the token, and the
// payload carries the op out once per token.
auto test_op_protocol() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-op.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);
  assert(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()));
  assert(cs2::features::default_config_path().filename() == path.filename());

  // The payload publishes the file's path in the settings block.
  Config published{};
  cs2::features::set_config_path(published);
  assert(std::string(published.menu.config_path.data()) == path.string());

  Config config = cs2::features::from_wire_settings(sentinel_settings());
  std::snprintf(config.menu.config_path.data(), config.menu.config_path.size(), "%s",
                path.string().c_str());

  Settings settings{};
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::save);
  settings.menu_config_token = 1;
  assert(cs2::features::apply_pending_op(config, settings));
  assert(std::filesystem::exists(path, error));

  // The same token does not run the op twice.
  assert(!cs2::features::apply_pending_op(config, settings));

  // Load: the config comes back from the file.
  config = Config{};
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::load);
  settings.menu_config_token = 2;
  assert(cs2::features::apply_pending_op(config, settings));
  assert(config.esp.skeleton);
  assert(config.esp.max_box_scale == 1.75F);
  assert(config.rage.weapon[0] == 'w');

  // Reset: the defaults are back.
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::reset);
  settings.menu_config_token = 3;
  assert(cs2::features::apply_pending_op(config, settings));
  assert(config == Config{});

  // No op: nothing happens.
  settings.menu_config_op = 0;
  settings.menu_config_token = 4;
  assert(!cs2::features::apply_pending_op(config, settings));

  std::filesystem::remove(path, error);
}

}  // namespace

auto main() -> int {
  test_wire_round_trip();
  test_defaults_agree();
  test_clamping();
  test_store_round_trip();
  test_store_rejects_garbage();
  test_settings_checksum();
  test_torn_read_is_detected();
  test_segment_version();
  test_op_protocol();
  std::puts("features-config: ok");
  return 0;
}
