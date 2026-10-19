#include "cs2/features/settings.hpp"

#include <cstring>

namespace cs2::features {

auto to_wire_settings(const EspConfig& config) -> cs2::overlay::wire::Settings {
  cs2::overlay::wire::Settings settings{};
  settings.box = config.box ? 1 : 0;
  settings.health_bar = config.health_bar ? 1 : 0;
  settings.ammo_bar = config.ammo_bar ? 1 : 0;
  settings.name = config.name ? 1 : 0;
  settings.weapon = config.weapon ? 1 : 0;
  settings.flags = config.flags ? 1 : 0;
  settings.only_enemies = config.only_enemies ? 1 : 0;
  settings.show_dead = config.show_dead ? 1 : 0;
  settings.hitboxes = config.hitboxes ? 1 : 0;
  settings.hitbox_mode = static_cast<std::uint8_t>(config.hitbox_mode);
  settings.hitbox_color_mode = config.hitbox_custom_color ? 1 : 0;
  for (int i = 0; i < 4; ++i) {
    settings.hitbox_color[i] = config.hitbox_color[i];
  }
  settings.rage_objective = config.rage_objective;
  settings.rage_hitbox = config.rage_hitbox;
  settings.rage_fov = config.rage_fov;
  settings.rage_min_damage = config.rage_min_damage;
  settings.rage_min_hitchance = config.rage_min_hitchance;
  settings.rage_autostop = config.rage_autostop;
  settings.rage_autoscope = config.rage_autoscope;
  settings.rage_autoreload = config.rage_autoreload;
  settings.rage_only_visible = config.rage_only_visible;
  settings.rage_mode = config.rage_mode;
  settings.rage_per_weapon = config.rage_per_weapon;
  std::memcpy(settings.rage_weapon, config.rage_weapon, sizeof(settings.rage_weapon));
  settings.rage_recoil = config.rage_recoil;
  settings.rage_stop_speed = config.rage_stop_speed;
  settings.rage_backtrack = config.rage_backtrack;
  settings.show_hud = 1;
  return settings;
}

auto from_wire_settings(const cs2::overlay::wire::Settings& settings) -> EspConfig {
  EspConfig config;
  config.box = settings.box != 0;
  config.health_bar = settings.health_bar != 0;
  config.ammo_bar = settings.ammo_bar != 0;
  config.name = settings.name != 0;
  config.weapon = settings.weapon != 0;
  config.flags = settings.flags != 0;
  config.only_enemies = settings.only_enemies != 0;
  config.show_dead = settings.show_dead != 0;
  config.hitboxes = settings.hitboxes != 0;
  config.hitbox_mode = settings.hitbox_mode <= 2
                           ? static_cast<HitboxMode>(settings.hitbox_mode)
                           : HitboxMode::outline;
  config.hitbox_custom_color = settings.hitbox_color_mode != 0;
  for (int i = 0; i < 4; ++i) {
    config.hitbox_color[i] = settings.hitbox_color[i];
  }
  config.rage_objective = settings.rage_objective <= 2 ? settings.rage_objective : 1;
  config.rage_hitbox = settings.rage_hitbox <= 2 ? settings.rage_hitbox : 0;
  config.rage_fov = settings.rage_fov;
  config.rage_min_damage = settings.rage_min_damage;
  config.rage_min_hitchance = settings.rage_min_hitchance;
  config.rage_autostop = settings.rage_autostop <= 2 ? settings.rage_autostop : 0;
  config.rage_autoscope = settings.rage_autoscope <= 2 ? settings.rage_autoscope : 0;
  config.rage_autoreload = settings.rage_autoreload != 0 ? 1 : 0;
  config.rage_only_visible = settings.rage_only_visible != 0 ? 1 : 0;
  config.rage_mode = settings.rage_mode <= 2 ? settings.rage_mode : 0;
  config.rage_per_weapon = settings.rage_per_weapon != 0 ? 1 : 0;
  std::memcpy(config.rage_weapon, settings.rage_weapon, sizeof(config.rage_weapon));
  config.rage_weapon[sizeof(config.rage_weapon) - 1] = '\0';
  config.rage_recoil = settings.rage_recoil != 0 ? 1 : 0;
  config.rage_stop_speed = settings.rage_stop_speed;
  config.rage_backtrack = settings.rage_backtrack != 0 ? 1 : 0;
  return config;
}

auto default_wire_settings() -> cs2::overlay::wire::Settings {
  return to_wire_settings(EspConfig{});
}

}  // namespace cs2::features
