#include "cs2/features/settings.hpp"

#include <algorithm>
#include <cstring>

namespace cs2::features {
namespace {

// A NaN-safe clamp. `std::clamp` lets a NaN through (`v < low` and `high < v`
// are both false for a NaN), and a corrupt shared segment must not be able to
// put a NaN into a slider.
auto clamp_value(float value, float low, float high) noexcept -> float {
  if (!(value == value)) {
    return low;
  }
  return std::clamp(value, low, high);
}

// The enumerated settings travel as small integers. An unknown value (a newer
// menu, a corrupt segment) falls back to the documented default.
template <typename Enum>
auto clamp_enum(std::uint8_t value, std::uint8_t max, Enum fallback) noexcept -> Enum {
  return value <= max ? static_cast<Enum>(value) : fallback;
}

}  // namespace

// The `Config` -> the settings block.
//
// Every field of the settings block is written here, one section at a time;
// the mapping is one-to-one and mirrors `from_wire_settings` exactly. The
// host test `features-config` proves that the two directions cover the same
// field set (a field added to one and forgotten in the other is the classic
// bug here).
auto to_wire_settings(const Config& config) -> cs2::overlay::wire::Settings {
  cs2::overlay::wire::Settings settings{};

  // --- ESP: the elements ---------------------------------------------------
  settings.box = config.esp.box ? 1 : 0;
  settings.health_bar = config.esp.health_bar ? 1 : 0;
  settings.ammo_bar = config.esp.ammo_bar ? 1 : 0;
  settings.name = config.esp.name ? 1 : 0;
  settings.weapon = config.esp.weapon ? 1 : 0;
  settings.flags = config.esp.flags ? 1 : 0;
  settings.only_enemies = config.esp.only_enemies ? 1 : 0;
  settings.show_dead = config.esp.show_dead ? 1 : 0;
  settings.hitboxes = config.esp.hitboxes ? 1 : 0;
  settings.hitbox_mode = static_cast<std::uint8_t>(config.esp.hitbox_mode);
  settings.hitbox_color_mode = config.esp.hitbox_custom_color ? 1 : 0;
  config.esp.hitbox_color.to_wire(settings.hitbox_color);

  // --- ESP: the added player elements --------------------------------------
  settings.esp_box_thickness = config.esp.box_thickness;
  settings.esp_box_cornered = static_cast<std::uint8_t>(config.esp.box_style);
  settings.esp_head_box = config.esp.head_box ? 1 : 0;
  settings.esp_skeleton = config.esp.skeleton ? 1 : 0;
  settings.esp_tracer = config.esp.tracer ? 1 : 0;
  settings.esp_tracer_origin = static_cast<std::uint8_t>(config.esp.tracer_origin);
  settings.esp_offscreen = config.esp.offscreen ? 1 : 0;
  settings.esp_glow = config.esp.glow ? 1 : 0;
  settings.esp_visible_only = config.esp.visible_only ? 1 : 0;
  settings.esp_distance = config.esp.distance ? 1 : 0;
  settings.esp_scale_by_distance = config.esp.scale_by_distance ? 1 : 0;
  settings.esp_max_distance_m = config.esp.max_distance_m;
  settings.esp_radar = config.esp.radar ? 1 : 0;
  settings.esp_sound = config.esp.sound ? 1 : 0;
  settings.esp_grenades = config.esp.grenades ? 1 : 0;
  settings.esp_c4 = config.esp.c4 ? 1 : 0;
  settings.esp_weapons = config.esp.weapons ? 1 : 0;
  settings.esp_local_player = config.esp.local_player ? 1 : 0;
  settings.esp_show_spectators = config.esp.show_spectators ? 1 : 0;
  settings.esp_show_teammates = config.esp.show_teammates ? 1 : 0;
  settings.esp_max_box_scale = config.esp.max_box_scale;

  // --- ESP: the colours ----------------------------------------------------
  config.esp.color_enemy.to_wire(settings.esp_color_enemy);
  config.esp.color_team.to_wire(settings.esp_color_team);
  config.esp.color_box.to_wire(settings.esp_color_box);
  config.esp.color_skeleton.to_wire(settings.esp_color_skeleton);
  config.esp.color_tracer.to_wire(settings.esp_color_tracer);
  config.esp.color_offscreen.to_wire(settings.esp_color_offscreen);
  config.esp.color_sound.to_wire(settings.esp_color_sound);
  config.esp.color_grenade.to_wire(settings.esp_color_grenade);
  config.esp.color_c4.to_wire(settings.esp_color_c4);

  // --- Movement ------------------------------------------------------------
  settings.auto_strafe = config.movement.auto_strafe ? 1 : 0;
  settings.bhop = config.movement.bhop ? 1 : 0;
  settings.movement_strafe_mode = static_cast<std::uint8_t>(config.movement.strafe_mode);
  settings.movement_strafe_ignore_keys = config.movement.strafe_ignore_keys ? 1 : 0;
  settings.movement_strafe_wishspeed = config.movement.strafe_wishspeed;
  settings.movement_strafe_subticks = config.movement.strafe_subticks;
  settings.movement_thirdperson = config.movement.thirdperson ? 1 : 0;
  settings.movement_thirdperson_distance = config.movement.thirdperson_distance;
  settings.movement_thirdperson_angle = config.movement.thirdperson_angle;
  settings.movement_thirdperson_pivot_z = config.movement.thirdperson_pivot_z;
  settings.movement_thirdperson_fov = config.movement.thirdperson_fov;
  settings.movement_thirdperson_side = static_cast<std::uint8_t>(config.movement.thirdperson_side);

  // --- Ragebot -------------------------------------------------------------
  settings.rage = config.rage.enabled ? 1 : 0;
  settings.rage_key = config.rage.key;
  settings.rage_fire = config.rage.fire ? 1 : 0;
  settings.rage_objective = config.rage.objective;
  settings.rage_hitbox = config.rage.hitbox;
  settings.rage_fov = config.rage.fov;
  settings.rage_min_damage = config.rage.min_damage;
  settings.rage_min_hitchance = config.rage.min_hitchance;
  settings.rage_autostop = config.rage.autostop;
  settings.rage_autoscope = config.rage.autoscope;
  settings.rage_autoreload = config.rage.autoreload;
  settings.rage_only_visible = config.rage.only_visible;
  settings.rage_mode = config.rage.mode;
  settings.rage_per_weapon = config.rage.per_weapon;
  settings.rage_config_op = config.rage.config_op;
  settings.rage_config_token = config.rage.config_token;
  std::memcpy(settings.rage_weapon, config.rage.weapon.data(), sizeof(settings.rage_weapon));
  settings.rage_recoil = config.rage.recoil;
  settings.rage_stop_speed = config.rage.stop_speed;
  settings.rage_backtrack = config.rage.backtrack;

  // --- Visuals (chams) -----------------------------------------------------
  settings.visuals_chams = config.visuals.chams ? 1 : 0;
  settings.visuals_chams_mode = static_cast<std::uint8_t>(config.visuals.chams_mode);
  settings.visuals_chams_visible = config.visuals.chams_visible ? 1 : 0;
  settings.visuals_chams_local = config.visuals.chams_local ? 1 : 0;
  config.visuals.chams_color.to_wire(settings.visuals_chams_color);

  // --- Menu / config -------------------------------------------------------
  settings.show_hud = config.menu.show_hud ? 1 : 0;
  settings.menu_open = config.menu.open ? 1 : 0;
  settings.menu_key = config.menu.key;
  config.menu.accent.to_wire(settings.menu_accent);
  settings.menu_transparency = config.menu.transparency;
  settings.menu_config_op = config.menu.config_op;
  settings.menu_config_token = config.menu.config_token;
  std::memcpy(settings.menu_config_path, config.menu.config_path.data(),
              sizeof(settings.menu_config_path));

  return settings;
}

// The settings block -> the `Config`.
//
// Every field of the settings block is read here, one section at a time, and
// every numeric field is clamped into its legal range: the shared segment is
// written by another process and may hold a stale, truncated or corrupt value,
// and a slider must never be pushed out of the range it was drawn for.
auto from_wire_settings(const cs2::overlay::wire::Settings& settings) -> Config {
  Config config;

  // --- ESP: the elements ---------------------------------------------------
  config.esp.box = settings.box != 0;
  config.esp.health_bar = settings.health_bar != 0;
  config.esp.ammo_bar = settings.ammo_bar != 0;
  config.esp.name = settings.name != 0;
  config.esp.weapon = settings.weapon != 0;
  config.esp.flags = settings.flags != 0;
  config.esp.only_enemies = settings.only_enemies != 0;
  config.esp.show_dead = settings.show_dead != 0;
  config.esp.hitboxes = settings.hitboxes != 0;
  config.esp.hitbox_mode = clamp_enum(settings.hitbox_mode, 2, HitboxMode::outline);
  config.esp.hitbox_custom_color = settings.hitbox_color_mode != 0;
  config.esp.hitbox_color = Color::from_wire(settings.hitbox_color);

  // --- ESP: the added player elements --------------------------------------
  config.esp.box_thickness = std::min(settings.esp_box_thickness, k_esp_box_thickness_max);
  config.esp.box_style = clamp_enum(settings.esp_box_cornered, 1, BoxStyle::straight);
  config.esp.head_box = settings.esp_head_box != 0;
  config.esp.skeleton = settings.esp_skeleton != 0;
  config.esp.tracer = settings.esp_tracer != 0;
  config.esp.tracer_origin = clamp_enum(settings.esp_tracer_origin, 1, TracerOrigin::screen_bottom);
  config.esp.offscreen = settings.esp_offscreen != 0;
  config.esp.glow = settings.esp_glow != 0;
  config.esp.visible_only = settings.esp_visible_only != 0;
  config.esp.distance = settings.esp_distance != 0;
  config.esp.scale_by_distance = settings.esp_scale_by_distance != 0;
  config.esp.max_distance_m = std::min(settings.esp_max_distance_m, k_esp_max_distance_m_max);
  config.esp.radar = settings.esp_radar != 0;
  config.esp.sound = settings.esp_sound != 0;
  config.esp.grenades = settings.esp_grenades != 0;
  config.esp.c4 = settings.esp_c4 != 0;
  config.esp.weapons = settings.esp_weapons != 0;
  config.esp.local_player = settings.esp_local_player != 0;
  config.esp.show_spectators = settings.esp_show_spectators != 0;
  config.esp.show_teammates = settings.esp_show_teammates != 0;
  config.esp.max_box_scale =
      clamp_value(settings.esp_max_box_scale, k_esp_max_box_scale_min, k_esp_max_box_scale_max);

  // --- ESP: the colours ----------------------------------------------------
  config.esp.color_enemy = Color::from_wire(settings.esp_color_enemy);
  config.esp.color_team = Color::from_wire(settings.esp_color_team);
  config.esp.color_box = Color::from_wire(settings.esp_color_box);
  config.esp.color_skeleton = Color::from_wire(settings.esp_color_skeleton);
  config.esp.color_tracer = Color::from_wire(settings.esp_color_tracer);
  config.esp.color_offscreen = Color::from_wire(settings.esp_color_offscreen);
  config.esp.color_sound = Color::from_wire(settings.esp_color_sound);
  config.esp.color_grenade = Color::from_wire(settings.esp_color_grenade);
  config.esp.color_c4 = Color::from_wire(settings.esp_color_c4);

  // --- Movement ------------------------------------------------------------
  config.movement.auto_strafe = settings.auto_strafe != 0;
  config.movement.bhop = settings.bhop != 0;
  config.movement.strafe_mode =
      clamp_enum(settings.movement_strafe_mode, 2, StrafeMode::camera_relative);
  config.movement.strafe_ignore_keys = settings.movement_strafe_ignore_keys != 0;
  config.movement.strafe_wishspeed =
      clamp_value(settings.movement_strafe_wishspeed, 0.0F, k_movement_strafe_wishspeed_max);
  config.movement.strafe_subticks =
      std::min(settings.movement_strafe_subticks, k_movement_strafe_subticks_max);
  config.movement.thirdperson = settings.movement_thirdperson != 0;
  config.movement.thirdperson_distance =
      clamp_value(settings.movement_thirdperson_distance, k_movement_thirdperson_distance_min,
                  k_movement_thirdperson_distance_max);
  config.movement.thirdperson_angle =
      clamp_value(settings.movement_thirdperson_angle, 0.0F, k_movement_thirdperson_angle_max);
  config.movement.thirdperson_pivot_z =
      clamp_value(settings.movement_thirdperson_pivot_z, 0.0F, k_movement_thirdperson_pivot_z_max);
  config.movement.thirdperson_fov =
      clamp_value(settings.movement_thirdperson_fov, 0.0F, k_movement_thirdperson_fov_max);
  config.movement.thirdperson_side =
      clamp_enum(settings.movement_thirdperson_side, 1, ThirdPersonSide::right);

  // --- Ragebot -------------------------------------------------------------
  config.rage.enabled = settings.rage != 0;
  config.rage.key = settings.rage_key;
  config.rage.fire = settings.rage_fire != 0;
  config.rage.objective = settings.rage_objective <= 2 ? settings.rage_objective : 1;
  config.rage.hitbox = settings.rage_hitbox <= 2 ? settings.rage_hitbox : 0;
  config.rage.fov = clamp_value(settings.rage_fov, 0.0F, k_rage_fov_max);
  config.rage.min_damage = clamp_value(settings.rage_min_damage, 0.0F, k_rage_min_damage_max);
  config.rage.min_hitchance =
      clamp_value(settings.rage_min_hitchance, 0.0F, k_rage_min_hitchance_max);
  config.rage.autostop = settings.rage_autostop <= 2 ? settings.rage_autostop : 0;
  config.rage.autoscope = settings.rage_autoscope <= 2 ? settings.rage_autoscope : 0;
  config.rage.autoreload = settings.rage_autoreload != 0 ? 1 : 0;
  config.rage.only_visible = settings.rage_only_visible != 0 ? 1 : 0;
  config.rage.mode = settings.rage_mode <= 2 ? settings.rage_mode : 0;
  config.rage.per_weapon = settings.rage_per_weapon != 0 ? 1 : 0;
  config.rage.config_op = settings.rage_config_op;
  config.rage.config_token = settings.rage_config_token;
  std::memcpy(config.rage.weapon.data(), settings.rage_weapon, sizeof(config.rage.weapon));
  config.rage.weapon[config.rage.weapon.size() - 1] = '\0';
  config.rage.recoil = settings.rage_recoil != 0 ? 1 : 0;
  config.rage.stop_speed = clamp_value(settings.rage_stop_speed, 0.0F, k_rage_stop_speed_max);
  config.rage.backtrack = settings.rage_backtrack != 0 ? 1 : 0;

  // --- Visuals (chams) -----------------------------------------------------
  config.visuals.chams = settings.visuals_chams != 0;
  config.visuals.chams_mode = clamp_enum(settings.visuals_chams_mode, 2, ChamsMode::flat);
  config.visuals.chams_visible = settings.visuals_chams_visible != 0;
  config.visuals.chams_local = settings.visuals_chams_local != 0;
  config.visuals.chams_color = Color::from_wire(settings.visuals_chams_color);

  // --- Menu / config -------------------------------------------------------
  config.menu.show_hud = settings.show_hud != 0;
  config.menu.open = settings.menu_open != 0;
  config.menu.key = settings.menu_key;
  config.menu.accent = Color::from_wire(settings.menu_accent);
  config.menu.transparency = std::min(settings.menu_transparency, k_menu_transparency_max);
  config.menu.config_op = settings.menu_config_op <= 3 ? settings.menu_config_op : 0;
  config.menu.config_token = settings.menu_config_token;
  std::memcpy(config.menu.config_path.data(), settings.menu_config_path,
              sizeof(config.menu.config_path));
  config.menu.config_path[config.menu.config_path.size() - 1] = '\0';

  return config;
}

// The menu's defaults: the shared defaults of the settings block. The Vulkan
// layer's `default_ui_settings()` uses the same function, so the two can never
// disagree about what a fresh segment means.
auto default_wire_settings() -> cs2::overlay::wire::Settings {
  return cs2::overlay::wire::default_settings();
}

void SettingsProtocol::publish(cs2::overlay::wire::Shared& shared,
                               const cs2::overlay::wire::Settings& settings) noexcept {
  cs2::overlay::wire::write_settings(shared, settings);
  // The latch is deliberately **not** updated: the reader has to notice the
  // published block. A publisher that recorded the sequence it had just
  // written made the next read report "no change", and the published block
  // (a loaded configuration) never reached the ESP.
}

auto SettingsProtocol::adopt(cs2::overlay::wire::Shared& shared,
                             cs2::overlay::wire::Settings& out) noexcept -> SettingsStatus {
  std::uint32_t sequence = 0;
  if (!cs2::overlay::wire::read_settings(shared, out, &sequence)) {
    return SettingsStatus::failed;  // a torn read; the caller keeps its copy
  }
  if (adopted_ && sequence == adopted_sequence_) {
    return SettingsStatus::unchanged;
  }
  adopted_ = true;
  adopted_sequence_ = sequence;
  return SettingsStatus::adopted;
}

}  // namespace cs2::features
