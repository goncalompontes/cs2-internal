// Host test for the settings foundation: the wire <-> config conversion and the
// config store's round trip.
//
// The settings block is a POD and the conversion is pure, so both can be
// exercised natively (no game, no payload). The test is deliberately
// *exhaustive* and *pairing-sensitive*:
//
// - the differential table below holds one case per field of `wire::Settings`:
//   the wire field and the config field it feeds. Mutating exactly one wire
//   field has to change exactly the config field named in the case, so a
//   **consistent swap** of two fields — invisible to a round trip — is caught;
// - the sentinel block is the default block with every case's sentinel
//   applied, so the block and the table cannot drift apart, and the round trip
//   has to come back byte-identical. A field that the conversion forgets — in
//   either direction — changes a byte and fails the test.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build. Meson's `b_ndebug` default
// is `if-release`, so `NDEBUG` is defined in the configuration
// `scripts/release.sh` builds and every `assert` would be compiled out — a
// test that only bites in the debug build is not a test.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "cs2/features/config_store.hpp"
#include "cs2/features/settings.hpp"

namespace {

using cs2::features::Config;
using cs2::overlay::wire::Settings;

// The number of failed checks. The test's exit code is the verdict, so a
// release build (where `assert` is compiled out) still reports the failure.
int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// A differential case: the wire field and the config field it feeds.
//
// The wire setter writes a sentinel that differs from the field's default and
// stays inside its legal range (so the clamping in `from_wire_settings` does
// not change it). The config setter writes the value the conversion has to
// produce.
struct FieldCase {
  const char* name;
  void (*set_wire)(Settings&);
  void (*set_config)(Config&);
};

// The cases. The arguments are parenthesised because a colour holds commas.
#define CS2_FIELD(name, wire_set, config_set) \
  {name, [](Settings& s) { wire_set; }, [](Config& c) { config_set; }}

auto field_cases() -> std::vector<FieldCase> {
  return {
      // --- ESP: the elements ---
      CS2_FIELD("esp.box", (s.box = 0), (c.esp.box = false)),
      CS2_FIELD("esp.health_bar", (s.health_bar = 0), (c.esp.health_bar = false)),
      CS2_FIELD("esp.ammo_bar", (s.ammo_bar = 0), (c.esp.ammo_bar = false)),
      CS2_FIELD("esp.name", (s.name = 0), (c.esp.name = false)),
      CS2_FIELD("esp.weapon", (s.weapon = 0), (c.esp.weapon = false)),
      CS2_FIELD("esp.flags", (s.flags = 0), (c.esp.flags = false)),
      CS2_FIELD("esp.only_enemies", (s.only_enemies = 0), (c.esp.only_enemies = false)),
      CS2_FIELD("esp.show_dead", (s.show_dead = 1), (c.esp.show_dead = true)),
      CS2_FIELD("esp.hitboxes", (s.hitboxes = 1), (c.esp.hitboxes = true)),
      CS2_FIELD("esp.hitbox_mode", (s.hitbox_mode = 2),
                (c.esp.hitbox_mode = cs2::features::HitboxMode::wireframe)),
      CS2_FIELD("esp.hitbox_color_mode", (s.hitbox_color_mode = 1),
                (c.esp.hitbox_custom_color = true)),
      CS2_FIELD("esp.hitbox_color",
                (s.hitbox_color[0] = 1, s.hitbox_color[1] = 2, s.hitbox_color[2] = 3,
                 s.hitbox_color[3] = 4),
                (c.esp.hitbox_color = cs2::features::Color{1, 2, 3, 4})),

      // --- ESP: the added player elements ---
      CS2_FIELD("esp.esp_box_thickness", (s.esp_box_thickness = 7), (c.esp.box_thickness = 7)),
      CS2_FIELD("esp.esp_box_cornered", (s.esp_box_cornered = 1),
                (c.esp.box_style = cs2::features::BoxStyle::corner)),
      CS2_FIELD("esp.esp_head_box", (s.esp_head_box = 1), (c.esp.head_box = true)),
      CS2_FIELD("esp.esp_skeleton", (s.esp_skeleton = 1), (c.esp.skeleton = true)),
      CS2_FIELD("esp.esp_tracer", (s.esp_tracer = 1), (c.esp.tracer = true)),
      CS2_FIELD("esp.esp_tracer_origin", (s.esp_tracer_origin = 1),
                (c.esp.tracer_origin = cs2::features::TracerOrigin::eye)),
      CS2_FIELD("esp.esp_offscreen", (s.esp_offscreen = 1), (c.esp.offscreen = true)),
      CS2_FIELD("esp.esp_glow", (s.esp_glow = 1), (c.esp.glow = true)),
      CS2_FIELD("esp.esp_visible_only", (s.esp_visible_only = 1), (c.esp.visible_only = true)),
      CS2_FIELD("esp.esp_distance", (s.esp_distance = 1), (c.esp.distance = true)),
      CS2_FIELD("esp.esp_scale_by_distance", (s.esp_scale_by_distance = 1),
                (c.esp.scale_by_distance = true)),
      CS2_FIELD("esp.esp_max_distance_m", (s.esp_max_distance_m = 33), (c.esp.max_distance_m = 33)),
      CS2_FIELD("esp.esp_radar", (s.esp_radar = 1), (c.esp.radar = true)),
      CS2_FIELD("esp.esp_sound", (s.esp_sound = 1), (c.esp.sound = true)),
      CS2_FIELD("esp.esp_grenades", (s.esp_grenades = 1), (c.esp.grenades = true)),
      CS2_FIELD("esp.esp_c4", (s.esp_c4 = 1), (c.esp.c4 = true)),
      CS2_FIELD("esp.esp_weapons", (s.esp_weapons = 1), (c.esp.weapons = true)),
      CS2_FIELD("esp.esp_local_player", (s.esp_local_player = 1), (c.esp.local_player = true)),
      CS2_FIELD("esp.esp_show_spectators", (s.esp_show_spectators = 1),
                (c.esp.show_spectators = true)),
      CS2_FIELD("esp.esp_show_teammates", (s.esp_show_teammates = 1),
                (c.esp.show_teammates = true)),
      CS2_FIELD("esp.esp_max_box_scale", (s.esp_max_box_scale = 1.75F),
                (c.esp.max_box_scale = 1.75F)),

      // --- ESP: the colours ---
      CS2_FIELD("esp.esp_color_enemy",
                (s.esp_color_enemy[0] = 11, s.esp_color_enemy[1] = 12, s.esp_color_enemy[2] = 13,
                 s.esp_color_enemy[3] = 14),
                (c.esp.color_enemy = cs2::features::Color{11, 12, 13, 14})),
      CS2_FIELD("esp.esp_color_team",
                (s.esp_color_team[0] = 21, s.esp_color_team[1] = 22, s.esp_color_team[2] = 23,
                 s.esp_color_team[3] = 24),
                (c.esp.color_team = cs2::features::Color{21, 22, 23, 24})),
      CS2_FIELD("esp.esp_color_box",
                (s.esp_color_box[0] = 31, s.esp_color_box[1] = 32, s.esp_color_box[2] = 33,
                 s.esp_color_box[3] = 34),
                (c.esp.color_box = cs2::features::Color{31, 32, 33, 34})),
      CS2_FIELD("esp.esp_color_skeleton",
                (s.esp_color_skeleton[0] = 41, s.esp_color_skeleton[1] = 42,
                 s.esp_color_skeleton[2] = 43, s.esp_color_skeleton[3] = 44),
                (c.esp.color_skeleton = cs2::features::Color{41, 42, 43, 44})),
      CS2_FIELD("esp.esp_color_tracer",
                (s.esp_color_tracer[0] = 51, s.esp_color_tracer[1] = 52, s.esp_color_tracer[2] = 53,
                 s.esp_color_tracer[3] = 54),
                (c.esp.color_tracer = cs2::features::Color{51, 52, 53, 54})),
      CS2_FIELD("esp.esp_color_offscreen",
                (s.esp_color_offscreen[0] = 61, s.esp_color_offscreen[1] = 62,
                 s.esp_color_offscreen[2] = 63, s.esp_color_offscreen[3] = 64),
                (c.esp.color_offscreen = cs2::features::Color{61, 62, 63, 64})),
      CS2_FIELD("esp.esp_color_sound",
                (s.esp_color_sound[0] = 71, s.esp_color_sound[1] = 72, s.esp_color_sound[2] = 73,
                 s.esp_color_sound[3] = 74),
                (c.esp.color_sound = cs2::features::Color{71, 72, 73, 74})),
      CS2_FIELD("esp.esp_color_grenade",
                (s.esp_color_grenade[0] = 81, s.esp_color_grenade[1] = 82,
                 s.esp_color_grenade[2] = 83, s.esp_color_grenade[3] = 84),
                (c.esp.color_grenade = cs2::features::Color{81, 82, 83, 84})),
      CS2_FIELD("esp.esp_color_c4",
                (s.esp_color_c4[0] = 91, s.esp_color_c4[1] = 92, s.esp_color_c4[2] = 93,
                 s.esp_color_c4[3] = 94),
                (c.esp.color_c4 = cs2::features::Color{91, 92, 93, 94})),

      // --- Movement ---
      CS2_FIELD("movement.auto_strafe", (s.auto_strafe = 1), (c.movement.auto_strafe = true)),
      CS2_FIELD("movement.bhop", (s.bhop = 1), (c.movement.bhop = true)),
      CS2_FIELD("movement.movement_strafe_mode", (s.movement_strafe_mode = 2),
                (c.movement.strafe_mode = cs2::features::StrafeMode::directional)),
      CS2_FIELD("movement.movement_strafe_ignore_keys", (s.movement_strafe_ignore_keys = 1),
                (c.movement.strafe_ignore_keys = true)),
      CS2_FIELD("movement.movement_strafe_wishspeed", (s.movement_strafe_wishspeed = 250.0F),
                (c.movement.strafe_wishspeed = 250.0F)),
      CS2_FIELD("movement.movement_strafe_subticks", (s.movement_strafe_subticks = 8),
                (c.movement.strafe_subticks = 8)),
      CS2_FIELD("movement.movement_thirdperson", (s.movement_thirdperson = 1),
                (c.movement.thirdperson = true)),
      CS2_FIELD("movement.movement_thirdperson_distance",
                (s.movement_thirdperson_distance = 320.0F),
                (c.movement.thirdperson_distance = 320.0F)),
      CS2_FIELD("movement.movement_thirdperson_angle", (s.movement_thirdperson_angle = 45.0F),
                (c.movement.thirdperson_angle = 45.0F)),
      CS2_FIELD("movement.movement_thirdperson_pivot_z", (s.movement_thirdperson_pivot_z = 12.5F),
                (c.movement.thirdperson_pivot_z = 12.5F)),
      CS2_FIELD("movement.movement_thirdperson_fov", (s.movement_thirdperson_fov = 90.0F),
                (c.movement.thirdperson_fov = 90.0F)),
      CS2_FIELD("movement.movement_thirdperson_side", (s.movement_thirdperson_side = 0),
                (c.movement.thirdperson_side = cs2::features::ThirdPersonSide::left)),

      // --- Ragebot ---
      CS2_FIELD("rage.rage", (s.rage = 1), (c.rage.enabled = true)),
      CS2_FIELD("rage.rage_key", (s.rage_key = 0x41), (c.rage.key = 0x41)),
      CS2_FIELD("rage.rage_fire", (s.rage_fire = 1), (c.rage.fire = true)),
      CS2_FIELD("rage.rage_objective", (s.rage_objective = 2), (c.rage.objective = 2)),
      CS2_FIELD("rage.rage_hitbox", (s.rage_hitbox = 2), (c.rage.hitbox = 2)),
      CS2_FIELD("rage.rage_fov", (s.rage_fov = 30.5F), (c.rage.fov = 30.5F)),
      CS2_FIELD("rage.rage_min_damage", (s.rage_min_damage = 42.25F), (c.rage.min_damage = 42.25F)),
      CS2_FIELD("rage.rage_min_hitchance", (s.rage_min_hitchance = 0.75F),
                (c.rage.min_hitchance = 0.75F)),
      CS2_FIELD("rage.rage_autostop", (s.rage_autostop = 2), (c.rage.autostop = 2)),
      CS2_FIELD("rage.rage_autoscope", (s.rage_autoscope = 1), (c.rage.autoscope = 1)),
      CS2_FIELD("rage.rage_autoreload", (s.rage_autoreload = 1), (c.rage.autoreload = 1)),
      CS2_FIELD("rage.rage_only_visible", (s.rage_only_visible = 1), (c.rage.only_visible = 1)),
      CS2_FIELD("rage.rage_mode", (s.rage_mode = 2), (c.rage.mode = 2)),
      CS2_FIELD("rage.rage_per_weapon", (s.rage_per_weapon = 1), (c.rage.per_weapon = 1)),
      CS2_FIELD("rage.rage_config_op", (s.rage_config_op = 3), (c.rage.config_op = 3)),
      CS2_FIELD("rage.rage_config_token", (s.rage_config_token = 123456U),
                (c.rage.config_token = 123456U)),
      CS2_FIELD("rage.rage_weapon",
                (std::memcpy(s.rage_weapon, "weapon_ak47", sizeof("weapon_ak47"))),
                (std::memcpy(c.rage.weapon.data(), "weapon_ak47", sizeof("weapon_ak47")))),
      CS2_FIELD("rage.rage_recoil", (s.rage_recoil = 1), (c.rage.recoil = 1)),
      CS2_FIELD("rage.rage_stop_speed", (s.rage_stop_speed = 100.5F), (c.rage.stop_speed = 100.5F)),
      CS2_FIELD("rage.rage_backtrack", (s.rage_backtrack = 1), (c.rage.backtrack = 1)),

      // --- Visuals (chams) ---
      CS2_FIELD("visuals.visuals_chams", (s.visuals_chams = 1), (c.visuals.chams = true)),
      CS2_FIELD("visuals.visuals_chams_mode", (s.visuals_chams_mode = 2),
                (c.visuals.chams_mode = cs2::features::ChamsMode::wireframe)),
      CS2_FIELD("visuals.visuals_chams_visible", (s.visuals_chams_visible = 1),
                (c.visuals.chams_visible = true)),
      CS2_FIELD("visuals.visuals_chams_local", (s.visuals_chams_local = 1),
                (c.visuals.chams_local = true)),
      CS2_FIELD("visuals.visuals_chams_color",
                (s.visuals_chams_color[0] = 101, s.visuals_chams_color[1] = 102,
                 s.visuals_chams_color[2] = 103, s.visuals_chams_color[3] = 104),
                (c.visuals.chams_color = cs2::features::Color{101, 102, 103, 104})),

      // --- Menu / config ---
      CS2_FIELD("menu.show_hud", (s.show_hud = 0), (c.menu.show_hud = false)),
      CS2_FIELD("menu.menu_open", (s.menu_open = 1), (c.menu.open = true)),
      CS2_FIELD("menu.menu_key", (s.menu_key = 0x41), (c.menu.key = 0x41)),
      CS2_FIELD("menu.menu_accent",
                (s.menu_accent[0] = 111, s.menu_accent[1] = 112, s.menu_accent[2] = 113,
                 s.menu_accent[3] = 114),
                (c.menu.accent = cs2::features::Color{111, 112, 113, 114})),
      CS2_FIELD("menu.menu_transparency", (s.menu_transparency = 55), (c.menu.transparency = 55)),
      CS2_FIELD("menu.menu_config_op", (s.menu_config_op = 2), (c.menu.config_op = 2)),
      CS2_FIELD("menu.menu_config_token", (s.menu_config_token = 987654U),
                (c.menu.config_token = 987654U)),
      CS2_FIELD("menu.menu_config_path",
                (std::snprintf(s.menu_config_path, sizeof(s.menu_config_path), "%s",
                               "C:\\Users\\test\\cs2 config.cfg")),
                (std::memcpy(c.menu.config_path.data(), "C:\\Users\\test\\cs2 config.cfg",
                             sizeof("C:\\Users\\test\\cs2 config.cfg")))),
  };
}

// The settings block with a sentinel in **every** field.
//
// The sentinels come from the differential table, so the block and the table
// cannot drift apart.
auto sentinel_settings() -> Settings {
  Settings settings = cs2::overlay::wire::default_settings();
  for (const FieldCase& entry : field_cases()) {
    entry.set_wire(settings);
  }
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
  expect(same_settings(original, restored), "the wire round trip is byte-identical");
}

// The conversion is **pairing-sensitive**: mutating exactly one wire field
// changes exactly the config field it feeds.
//
// A round trip only proves `to ∘ from = id`, and a *consistent* swap of two
// fields (in both directions) is invisible to it whenever the two sentinels
// are equal — which is the case for every boolean. This test compares the
// whole configuration after a one-field mutation, so a swap fails.
auto test_field_pairing() -> void {
  const Settings defaults = cs2::overlay::wire::default_settings();
  const Config default_config = cs2::features::from_wire_settings(defaults);
  for (const FieldCase& entry : field_cases()) {
    Settings mutated = defaults;
    entry.set_wire(mutated);
    const Config actual = cs2::features::from_wire_settings(mutated);
    Config expected = default_config;
    entry.set_config(expected);
    // The field's name is the failure's description: it names the pair that
    // does not line up.
    expect(actual == expected, entry.name);
  }
}

// The defaults of the typed config and the shared wire defaults agree, so the
// payload and the Vulkan layer can never disagree about a fresh segment.
auto test_defaults_agree() -> void {
  const Settings shared = cs2::overlay::wire::default_settings();
  expect(same_settings(cs2::features::default_wire_settings(), shared),
         "the typed defaults are the shared defaults");
  expect(same_settings(cs2::features::to_wire_settings(Config{}), shared),
         "a default config converts back to the shared defaults");
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
  expect(config.esp.max_box_scale == 2.0F, "the box scale is clamped to its maximum");
  expect(config.esp.box_thickness == 20, "the box thickness is clamped to its maximum");
  expect(config.esp.box_style == cs2::features::BoxStyle::straight,
         "an out-of-range box style falls back to the default");
  expect(config.esp.tracer_origin == cs2::features::TracerOrigin::screen_bottom,
         "an out-of-range tracer origin falls back to the default");
  expect(config.esp.max_distance_m == 250, "the distance cap keeps the whole byte range");
  expect(config.movement.strafe_mode == cs2::features::StrafeMode::directional,
         "an out-of-range strafe mode falls back to the default");
  expect(config.movement.strafe_wishspeed == 3200.0F, "the wish speed is clamped");
  expect(config.movement.thirdperson_distance == 512.0F, "the camera distance is clamped");
  expect(config.movement.thirdperson_angle == 360.0F, "the camera angle is clamped");
  expect(config.movement.thirdperson_pivot_z == 64.0F, "the camera pivot is clamped");
  expect(config.movement.thirdperson_fov == 180.0F, "the camera fov is clamped");
  expect(config.movement.thirdperson_side == cs2::features::ThirdPersonSide::right,
         "an out-of-range camera side falls back to the default");
  expect(config.visuals.chams_mode == cs2::features::ChamsMode::flat,
         "an out-of-range chams mode falls back to the default");
  expect(config.menu.transparency == 100, "the transparency is clamped");
  expect(config.menu.config_op == 0, "an out-of-range config op is dropped");
  expect(config.esp.hitbox_mode == cs2::features::HitboxMode::outline,
         "an out-of-range hitbox mode falls back to the default");
  expect(config.rage.objective == 1, "an out-of-range objective falls back to the default");
  expect(config.rage.hitbox == 0, "an out-of-range hitbox falls back to the default");
  expect(config.rage.autostop == 0, "an out-of-range auto-stop falls back to the default");
  expect(config.rage.autoscope == 0, "an out-of-range auto-scope falls back to the default");
  expect(config.rage.mode == 0, "an out-of-range rage mode falls back to the default");
  expect(config.rage.fov == 360.0F, "the rage fov is clamped");
  expect(config.rage.min_damage == 200.0F, "the minimum damage is clamped");
  expect(config.rage.min_hitchance == 1.0F, "the minimum hitchance is clamped");
  expect(config.rage.stop_speed == 3200.0F, "the auto-stop speed is clamped");

  // The low ends.
  settings.esp_max_box_scale = -5.0F;
  settings.movement_thirdperson_distance = 1.0F;
  const Config low = cs2::features::from_wire_settings(settings);
  expect(low.esp.max_box_scale == 0.25F, "the box scale is clamped to its minimum");
  expect(low.movement.thirdperson_distance == 64.0F,
         "the camera distance is clamped to its minimum");

  // A NaN must not reach a slider.
  settings.esp_max_box_scale = std::nanf("");
  const Config not_a_number = cs2::features::from_wire_settings(settings);
  expect(not_a_number.esp.max_box_scale == 0.25F, "a NaN box scale is clamped away");
}

// The store round-trips every field of the settings block.
auto test_store_round_trip() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-test.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);

  Config config = cs2::features::from_wire_settings(sentinel_settings());
  expect(cs2::features::save(path.string().c_str(), config), "the settings file is written");

  const auto loaded = cs2::features::load(path.string().c_str());
  expect(loaded.has_value(), "the settings file is read back");
  expect(loaded.has_value() && *loaded == config, "the settings file round trips");

  // A missing file is not an error the caller has to handle: it just means
  // "keep the defaults".
  std::filesystem::remove(path, error);
  expect(!cs2::features::load(path.string().c_str()).has_value(),
         "a missing settings file is not an error");
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
    expect(fopen_s(&file, path.string().c_str(), "wb") == 0 && file != nullptr,
           "the test file is opened");
#else
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    expect(file != nullptr, "the test file is opened");
#endif
    const std::size_t written = std::fwrite(text, 1, std::strlen(text), file);
    expect(written == std::strlen(text), "the test file is written");
    std::fclose(file);
  };

  write(
      "# a comment\n"
      "esp.box = 0\n"
      "esp.box = not-a-number\n");
  expect(!cs2::features::load(path.string().c_str()).has_value(),
         "a non-numeric value is rejected");

  write("this line has no equals sign\n");
  expect(!cs2::features::load(path.string().c_str()).has_value(),
         "a line without an equals sign is rejected");

  write(
      "esp.box = 0\n"
      "esp.unknown_setting = 5\n"
      "unknown_section.box = 5\n");
  const auto loaded = cs2::features::load(path.string().c_str());
  expect(loaded.has_value(), "an unknown key is ignored");
  expect(loaded.has_value() && !loaded->esp.box, "the named setting is applied");
  expect(loaded.has_value() && loaded->esp.health_bar, "an unnamed setting keeps its default");

  // A UTF-8 BOM (what Notepad adds by default) must not make the file
  // corrupt: the first line is still a `key = value` line.
  write(
      "\xEF\xBB\xBF"
      "esp.box = 0\n");
  const auto with_bom = cs2::features::load(path.string().c_str());
  expect(with_bom.has_value(), "a byte-order mark is not a corrupt file");
  expect(with_bom.has_value() && !with_bom->esp.box,
         "the setting after a byte-order mark is applied");

  // A colour with the wrong number of components is corrupt, not a colour.
  write("esp.color_enemy = 1,2,3,4,5\n");
  expect(!cs2::features::load(path.string().c_str()).has_value(),
         "a colour with five components is rejected");
  write("esp.color_enemy = 1,2,3\n");
  expect(!cs2::features::load(path.string().c_str()).has_value(),
         "a colour with three components is rejected");
  write("esp.color_enemy = 1,2,3,4\n");
  const auto with_color = cs2::features::load(path.string().c_str());
  expect(with_color.has_value(), "a four-component colour is accepted");
  expect(with_color.has_value() && with_color->esp.color_enemy.g == 2,
         "the colour's components are applied");

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

  expect(settings_checksum(default_settings()) == k_default_settings_checksum,
         "the default block carries the golden checksum");

  // One changed byte changes the checksum.
  const Settings original = default_settings();
  Settings changed = original;
  changed.esp_color_enemy[0] = static_cast<std::uint8_t>(changed.esp_color_enemy[0] + 1);
  expect(settings_checksum(changed) != settings_checksum(original),
         "one changed byte changes the checksum");

  // A clean round trip through the segment keeps the checksum.
  // The segment is 3 MB, so it lives in the static storage, not on the stack.
  static cs2::overlay::wire::Shared shared{};
  shared.magic = cs2::overlay::wire::k_magic;
  shared.version = cs2::overlay::wire::k_version;
  cs2::overlay::wire::write_settings(shared, changed);
  Settings copy{};
  expect(cs2::overlay::wire::read_settings(shared, copy), "a whole block is read");
  expect(settings_checksum(copy) == settings_checksum(changed),
         "the round trip keeps the checksum");
  expect(std::memcmp(&copy, &changed, sizeof(Settings)) == 0, "the round trip keeps every byte");
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
  expect(!cs2::overlay::wire::read_settings(shared, copy, &read_sequence),
         "a torn block is rejected");

  // A block that was not torn is read back unchanged.
  cs2::overlay::wire::write_settings(shared, first);
  expect(cs2::overlay::wire::read_settings(shared, copy, &read_sequence), "a whole block is read");
  expect(std::memcmp(&copy, &first, sizeof(Settings)) == 0, "the whole block comes back unchanged");
}

// A stale binary refuses a segment it does not understand: the version is
// checked before the block is read, so a layer that does not know the
// checksum never touches the block.
auto test_segment_version() -> void {
  using cs2::overlay::wire::k_magic;
  using cs2::overlay::wire::k_version;
  using cs2::overlay::wire::segment_is_compatible;

  expect(segment_is_compatible(k_magic, k_version), "the current segment is compatible");
  expect(!segment_is_compatible(k_magic, k_version - 1), "an older segment is refused");
  expect(!segment_is_compatible(k_magic, k_version + 1), "a newer segment is refused");
  expect(!segment_is_compatible(0x12345678U, k_version), "a foreign segment is refused");
}

// A block published back has to be adopted by the next read.
//
// The regression: the publisher recorded the sequence it had just written, so
// the next read reported "no change" and the published block — the loaded
// configuration — never reached the ESP.
auto test_published_block_is_adopted() -> void {
  using cs2::features::SettingsProtocol;
  using cs2::features::SettingsStatus;

  // The segment is 3 MB, so it lives in the static storage, not on the stack.
  static cs2::overlay::wire::Shared shared{};
  shared.magic = cs2::overlay::wire::k_magic;
  shared.version = cs2::overlay::wire::k_version;

  SettingsProtocol protocol;
  Settings copy{};

  // The layer's menu writes the block.
  Settings menu_block = cs2::overlay::wire::default_settings();
  menu_block.esp_skeleton = 1;
  cs2::overlay::wire::write_settings(shared, menu_block);
  expect(protocol.adopt(shared, copy) == SettingsStatus::adopted, "the menu's block is adopted");
  expect(copy.esp_skeleton == 1, "the menu's block reaches the copy");

  // The payload publishes a loaded configuration back into the segment.
  Settings loaded = cs2::overlay::wire::default_settings();
  loaded.esp_max_box_scale = 1.75F;
  protocol.publish(shared, loaded);

  // The next read has to notice the change and adopt the published block: that
  // is the only path from the published block to the ESP's configuration.
  expect(protocol.adopt(shared, copy) == SettingsStatus::adopted, "the published block is adopted");
  expect(copy.esp_max_box_scale == 1.75F, "the published block reaches the copy");
  expect(cs2::features::from_wire_settings(copy).esp.max_box_scale == 1.75F,
         "the published block reaches the configuration");

  // A frame that reads the same block again does not re-apply it.
  expect(protocol.adopt(shared, copy) == SettingsStatus::unchanged,
         "an unchanged block is not re-applied");
}

// The config file's path is published into the live block.
//
// The path is a read-only display field: the payload owns it and publishes it
// so the menu can show where the file lives. It is only written when it
// changes, so the publication never disturbs the menu's in-flight edits.
auto test_config_path_is_published() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-path.cfg");
  expect(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()),
         "the config path environment variable is set");

  Settings settings{};
  expect(cs2::features::publish_config_path(settings), "the config path is published");
  expect(std::string(settings.menu_config_path) == path.string(),
         "the published config path is the file's path");

  // The path is already published: nothing changes, nothing is disturbed.
  Settings untouched = settings;
  expect(!cs2::features::publish_config_path(settings), "the config path is published once");
  expect(std::memcmp(&settings, &untouched, sizeof(Settings)) == 0,
         "the second publication disturbs nothing");
}

// The one-shot op protocol: the menu sets the op and bumps the token, and the
// payload carries the op out once per token.
auto test_op_protocol() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-op.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);
  expect(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()),
         "the config path environment variable is set");
  char published[cs2::features::k_max_config_path] = {};
  expect(cs2::features::default_config_path(published, sizeof(published)) > 0,
         "the default config path is published");
  expect(std::string(published) == path.string(), "the default config path is the file's path");

  // The payload publishes the file's path in the settings block.
  Config published_config{};
  cs2::features::set_config_path(published_config);
  expect(std::string(published_config.menu.config_path.data()) == path.string(),
         "the configuration carries the file's path");

  Config config = cs2::features::from_wire_settings(sentinel_settings());
  std::snprintf(config.menu.config_path.data(), config.menu.config_path.size(), "%s",
                path.string().c_str());

  // The live block the menu would hold: the op runs against it, so the file
  // ends up with **its** values.
  Settings settings = sentinel_settings();
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::save);
  settings.menu_config_token = 1;
  cs2::features::ConfigOpState state;
  const auto saved = cs2::features::apply_pending_op(config, settings, state);
  expect(saved.ran, "the save op runs");
  expect(!saved.changed, "a save changes nothing");
  expect(std::filesystem::exists(path, error), "the save writes the file");

  // The same token does not run the op twice.
  expect(!cs2::features::apply_pending_op(config, settings, state).ran,
         "the op runs once per token");

  // Load: the config comes back from the file.
  config = Config{};
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::load);
  settings.menu_config_token = 2;
  const auto loaded = cs2::features::apply_pending_op(config, settings, state);
  expect(loaded.ran, "the load op runs");
  expect(loaded.changed, "the load op changes the configuration");
  expect(config.esp.skeleton, "the loaded configuration carries the skeleton");
  expect(config.esp.max_box_scale == 1.75F, "the loaded configuration carries the box scale");
  expect(config.rage.weapon[0] == 'w', "the loaded configuration carries the weapon");

  // Reset: the defaults are back.
  settings.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::reset);
  settings.menu_config_token = 3;
  const auto reset = cs2::features::apply_pending_op(config, settings, state);
  expect(reset.ran, "the reset op runs");
  expect(reset.changed, "the reset op changes the configuration");
  // The defaults are back. The config path is the payload's own, so it is not
  // part of the defaults.
  Config defaults{};
  cs2::features::set_config_path(defaults);
  expect(config == defaults, "the reset restores the defaults");

  // No op: nothing happens.
  settings.menu_config_op = 0;
  settings.menu_config_token = 4;
  expect(!cs2::features::apply_pending_op(config, settings, state).ran, "an op of 0 does nothing");

  std::filesystem::remove(path, error);
}

// A save persists the **live** settings, not the defaults.
//
// The regression: the payload's configuration copy was never synced with the
// live block, so the op saved the defaults and the write-back reset the
// user's toggles in the same frame.
auto test_save_persists_live_settings() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-save.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);
  expect(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()),
         "the config path environment variable is set");

  // The live block: the user has toggled a few things.
  Settings live = cs2::overlay::wire::default_settings();
  live.esp_skeleton = 1;
  live.esp_max_box_scale = 1.75F;
  live.box = 0;
  live.rage_per_weapon = 1;
  live.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::save);
  live.menu_config_token = 7;

  // The payload's own copy still holds the defaults: the op has to bring it
  // up to date with the live block before it runs.
  Config config{};
  cs2::features::ConfigOpState state;
  const auto outcome = cs2::features::apply_pending_op(config, live, state);
  expect(outcome.ran, "the save op runs");
  expect(!outcome.changed, "a save changes nothing");

  // The file holds the **live** values, not the defaults.
  const auto loaded = cs2::features::load(path.string().c_str());
  expect(loaded.has_value(), "the saved file is read back");
  expect(loaded.has_value() && loaded->esp.skeleton, "the file holds the live skeleton setting");
  expect(loaded.has_value() && loaded->esp.max_box_scale == 1.75F,
         "the file holds the live box scale");
  expect(loaded.has_value() && !loaded->esp.box, "the file holds the live box setting");
  expect(loaded.has_value() && (loaded->rage.per_weapon != 0u),
         "the file holds the live per-weapon setting");

  // The live block is untouched: the user's toggles survive the save.
  expect(live.esp_skeleton == 1, "the live skeleton setting survives the save");
  expect(live.esp_max_box_scale == 1.75F, "the live box scale survives the save");
  expect(live.box == 0, "the live box setting survives the save");
  std::filesystem::remove(path, error);
}

// The write-back preserves the live protocol state.
//
// The op/token fields and the menu's open state belong to the menu, not to
// the configuration: a write-back that carried the file's values would
// re-trigger the op (the file's token is an old one) and drop a ragebot op
// the menu set in the same frame.
auto test_write_back_preserves_protocol_state() -> void {
  const std::filesystem::path path = temp_path("cs2-internal-config-protocol.cfg");
  std::error_code error;
  std::filesystem::remove(path, error);
  expect(set_env("CS2_PAYLOAD_CONFIG", path.string().c_str()),
         "the config path environment variable is set");

  // Save a configuration. The file records the op and the token of the save.
  Settings live = cs2::overlay::wire::default_settings();
  live.esp_skeleton = 1;
  live.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::save);
  live.menu_config_token = 3;
  Config config{};
  cs2::features::ConfigOpState state;
  expect(cs2::features::apply_pending_op(config, live, state).ran, "the save op runs");

  // The menu now asks for a load with a new token and sets a ragebot op in
  // the same frame. The menu is open, too: the open state is the menu's, not
  // the file's (the file recorded it closed).
  Settings request = live;
  request.esp_skeleton = 0;  // the user turned the skeleton off
  request.menu_open = 1;
  request.rage_config_op = 1;  // save-to-weapon
  request.rage_config_token = 99;
  request.menu_config_op = static_cast<std::uint8_t>(cs2::features::ConfigOp::load);
  request.menu_config_token = 4;

  const auto loaded = cs2::features::apply_pending_op(config, request, state);
  expect(loaded.ran, "the load op runs");
  expect(loaded.changed, "the load op changes the configuration");
  // The loaded configuration reached the block...
  expect(loaded.block.esp_skeleton == 1, "the loaded configuration reached the block");
  // ...but the live protocol state did not.
  expect(loaded.block.menu_open == 1, "the menu's open state survives the write-back");
  expect(loaded.block.menu_config_op == request.menu_config_op,
         "the menu's op survives the write-back");
  expect(loaded.block.menu_config_token == request.menu_config_token,
         "the menu's token survives the write-back");
  expect(loaded.block.rage_config_op == request.rage_config_op,
         "the menu's ragebot op survives the write-back");
  expect(loaded.block.rage_config_token == request.rage_config_token,
         "the menu's ragebot token survives the write-back");

  // The op did not re-trigger: the block still carries the live token, not
  // the file's (an old) one.
  expect(loaded.block.menu_config_token != 3, "the write-back does not re-trigger the op");

  std::filesystem::remove(path, error);
}

}  // namespace

auto main() -> int {
  test_wire_round_trip();
  test_field_pairing();
  test_defaults_agree();
  test_clamping();
  test_store_round_trip();
  test_store_rejects_garbage();
  test_settings_checksum();
  test_torn_read_is_detected();
  test_segment_version();
  test_published_block_is_adopted();
  test_config_path_is_published();
  test_op_protocol();
  test_save_persists_live_settings();
  test_write_back_preserves_protocol_state();
  if (failures != 0) {
    std::printf("features-config: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-config: ok");
  return 0;
}
