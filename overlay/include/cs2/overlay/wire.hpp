#pragma once

// The settings block: the one configuration both the menu and the features
// read (ADR-0021). It is a plain POD with a fixed layout, so the host tests,
// the payload and the control channel can all include it.
//
// The block used to be shared with the external Vulkan layer through a mapped
// segment; the layer is gone, so there is no seqlock, no checksum and no other
// writer. The in-process menu owns the block; the control channel reads and
// patches it through the menu (see `settings.hpp`).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cs2::overlay::wire {

#pragma pack(push, 1)
struct Settings {
  uint8_t box;
  uint8_t health_bar;
  uint8_t ammo_bar;
  uint8_t name;
  uint8_t weapon;
  uint8_t flags;
  uint8_t only_enemies;
  uint8_t show_dead;
  uint8_t show_hud;     // draw the debug HUD window
  uint8_t menu_open;    // written by the layer; the payload may suppress input
  uint8_t auto_strafe;  // payload movement features (`docs/re/subtick.md`):
  uint8_t bhop;         //   silent airborne air-strafe / auto-jump
  uint8_t rage;         // ragebot enabled (`docs/ragebot.md`)
  uint8_t rage_key;     //   the virtual-key code held to trigger (0 = none)
  uint8_t rage_fire;    //   auto-fire while aiming
  uint8_t hitboxes;     // draw the real studio hitboxes (`docs/re/aim.md`)
  // Hitbox rendering (menu-owned). `hitbox_mode`: 0 = outline, 1 = solid, 2 = wire.
  // `hitbox_color_mode`: 0 = automatic per-hitgroup scheme, 1 = `hitbox_color`.
  uint8_t hitbox_mode;
  uint8_t hitbox_color_mode;
  uint8_t hitbox_color[4];  // RGBA, used when `hitbox_color_mode == 1`
  // Ragebot tuning (`docs/ragebot.md`), menu-owned like the rest. `rage_fov` is the
  // target cone in degrees (0 = unlimited); `rage_min_damage`/`rage_min_hitchance` gate
  // the aim/fire; `rage_objective` selects the solve objective; `rage_hitbox` filters it.
  uint8_t rage_objective;    // 0 = damage, 1 = hitchance, 2 = expected
  uint8_t rage_hitbox;       // 0 = auto (head???body), 1 = head only, 2 = body only
  float rage_fov;            // degrees; 0 = no limit
  float rage_min_damage;     // don't aim/fire below this damage (0 = any)
  float rage_min_hitchance;  // don't fire below this hitchance (0-1; 0 = any)
  // The common ragebot assists (`docs/ragebot.md`). `rage_autostop`/`rage_autoscope`
  // are modes (0 = off, 1 = always while solving, 2 = only while firing); the rest are
  // flags. `rage_only_visible` fires only when no wall attenuates the shot.
  uint8_t rage_autostop;
  uint8_t rage_autoscope;
  uint8_t rage_autoreload;
  uint8_t rage_only_visible;
  // The trigger (`docs/ragebot.md`): 0 = hold the key, 1 = always on, 2 = toggle on the
  // key. Per-weapon profiles and their persistence (`rage_config.hpp`):
  // `rage_per_weapon` applies the profile matching the active weapon; the weapon key +
  // `rage_config_op` (1 save-to-weapon, 2 load-from-weapon, 3 save file, 4 load file,
  // 5 delete profile) are sent with `rage_config_token` incremented so one click is one
  // action.
  uint8_t rage_mode;
  uint8_t rage_per_weapon;
  uint8_t rage_config_op;
  uint32_t rage_config_token;
  char rage_weapon[24];
  // Recoil compensation (subtract the recoil punch from the silent aim) and the
  // auto-stop speed threshold in units/s (0 = a full stop; > 0 = counter-strafe down to
  // this speed, the fastest movement that does not add spread).
  uint8_t rage_recoil;
  float rage_stop_speed;
  // Lag-record backtracking (`docs/ragebot.md` -?8). **Off by default**: the sampled lag
  // poses cost a full solve each and the record sampling itself is ~0.6 ms/tick, so the
  // feature is kept but disabled until the perf work lands.
  uint8_t rage_backtrack;
  // --- ESP: players -------------------------------------------------------
  uint8_t esp_box_thickness;      // 0..20 -> 0.5..5.0 px
  uint8_t esp_box_cornered;       // 0 = straight box, 1 = corner box
  uint8_t esp_head_box;           // draw a small box on the head
  uint8_t esp_skeleton;           // draw bone lines
  uint8_t esp_tracer;             // line from the screen bottom to the player
  uint8_t esp_tracer_origin;      // 0 = screen bottom, 1 = local eye origin
  uint8_t esp_offscreen;          // off-screen target arrows
  uint8_t esp_glow;               // through-wall highlight (visible-state tint)
  uint8_t esp_visible_only;       // only draw players that are currently visible
  uint8_t esp_distance;           // show the distance in metres
  uint8_t esp_scale_by_distance;  // shrink the box with distance
  uint8_t esp_max_distance_m;     // 0 = unlimited; otherwise cap in metres
  uint8_t esp_radar;              // radar widget
  uint8_t esp_sound;              // sound (footstep) ESP
  uint8_t esp_grenades;           // grenade ESP
  uint8_t esp_c4;                 // planted C4 ESP
  uint8_t esp_weapons;            // dropped-weapon ESP
  uint8_t esp_local_player;       // draw the local player too
  uint8_t esp_show_spectators;    // draw spectators
  uint8_t esp_show_teammates;     // draw teammates
  float esp_max_box_scale;        // 0.25 .. 2.0
  // --- ESP: colours (RGBA bytes) -----------------------------------------
  uint8_t esp_color_enemy[4];
  uint8_t esp_color_team[4];
  uint8_t esp_color_box[4];
  uint8_t esp_color_skeleton[4];
  uint8_t esp_color_tracer[4];
  uint8_t esp_color_offscreen[4];
  uint8_t esp_color_sound[4];
  uint8_t esp_color_grenade[4];
  uint8_t esp_color_c4[4];
  // --- Movement ------------------------------------------------------------
  uint8_t movement_strafe_mode;         // 0 = off, 1 = camera-relative, 2 = directional (WASD)
  uint8_t movement_strafe_ignore_keys;  // 0 = A/D biases the heading, 1 = A/D stands down
  float movement_strafe_wishspeed;
  uint8_t movement_strafe_subticks;
  uint8_t movement_thirdperson;         // force the camera into third person
  float movement_thirdperson_distance;  // 64..512 units
  float movement_thirdperson_angle;     // 0..360 degrees
  float movement_thirdperson_pivot_z;   // 0..64 units
  float movement_thirdperson_fov;       // 0 = keep the game's FOV
  uint8_t movement_thirdperson_side;    // 0 = left shoulder, 1 = right shoulder
  // --- Visuals (chams) -----------------------------------------------------
  uint8_t visuals_chams;
  uint8_t visuals_chams_mode;     // 0 = flat, 1 = glow, 2 = wireframe
  uint8_t visuals_chams_visible;  // 1 = only when the player is visible
  uint8_t visuals_chams_local;    // 1 = also chams the local player
  uint8_t visuals_chams_color[4];
  // --- Menu / config -------------------------------------------------------
  uint8_t menu_key;            // virtual-key code that opens the menu, 0 = Insert
  uint8_t menu_accent[4];      // RGBA accent colour
  uint8_t menu_transparency;   // 0..100 (window alpha)
  uint8_t menu_config_op;      // 1 = save, 2 = load, 3 = reset
  uint32_t menu_config_token;  // bumped by the menu to signal the op
  // The absolute path of the config file (read-only). `MAX_PATH` bytes, so
  // the path the menu shows is the path the payload uses (a longer path is
  // refused rather than truncated to a different file).
  char menu_config_path[260];
};
#pragma pack(pop)

// --- The settings patch -------------------------------------------------------
//
// The settings block has **two writers**: the menu that owns it (the render
// thread) and the control channel's `settings` command (the control thread).
// A whole-block write between them is a data race (441 bytes of plain data
// copied by two threads) and a **lost update**: the control channel reads the
// block, changes a field and writes the whole block back, so every field the
// menu changed in between is clobbered.
//
// The write is therefore **field-wise**: it carries the fields it changes, and
// the owner merges them into its own block under its lock. The patch lives in
// this header because it is the shared contract between the payload's modules
// (the control channel, the menu) and the Vulkan layer's menu.
struct SettingsPatch {
  // The new values: a block whose patched fields hold the requested values.
  const Settings* values = nullptr;
  // The patched fields: their byte offset and their length in the packed
  // block.
  const std::size_t* offsets = nullptr;
  const std::size_t* sizes = nullptr;
  // How many fields the patch carries.
  std::size_t count = 0;
};

// Apply `patch` to `block`: the patched fields take the patch's values, every
// other field of the block is left alone.
inline void apply_settings_patch(Settings& block, const SettingsPatch& patch) noexcept {
  if (patch.values == nullptr || patch.offsets == nullptr || patch.sizes == nullptr) {
    return;
  }
  auto* bytes = reinterpret_cast<unsigned char*>(&block);
  const auto* source = reinterpret_cast<const unsigned char*>(patch.values);
  for (std::size_t i = 0; i < patch.count; ++i) {
    const std::size_t offset = patch.offsets[i];
    const std::size_t size = patch.sizes[i];
    if (offset + size > sizeof(Settings)) {
      continue;  // a field outside the block: ignore it
    }
    std::memcpy(bytes + offset, source + offset, size);
  }
}

// The shared defaults for the settings block.
//
// Both sides derive their defaults from this one function ??? the payload's
// `features::default_wire_settings()` and the Vulkan layer's
// `default_ui_settings()` ??? so the two can never disagree about what a fresh
// segment means. The values mirror `features::Config`'s defaults; the host test
// `features-config` asserts that the two stay in step.
//
// The new elements are **conservative**: every added ESP element, the
// third-person camera and the chams are off, the menu key is Insert (0x2D) and
// the auto-strafe is camera-relative (1).
[[nodiscard]] constexpr auto default_settings() noexcept -> Settings {
  Settings settings{};
  // --- ESP ---
  settings.box = 1;
  settings.health_bar = 1;
  settings.ammo_bar = 1;
  settings.name = 1;
  settings.weapon = 1;
  settings.flags = 1;
  settings.only_enemies = 1;
  settings.show_dead = 0;
  settings.show_hud = 1;
  settings.hitboxes = 0;
  settings.hitbox_mode = 0;  // HitboxMode::outline
  settings.hitbox_color_mode = 0;
  settings.hitbox_color[0] = 90;
  settings.hitbox_color[1] = 255;
  settings.hitbox_color[2] = 130;
  settings.hitbox_color[3] = 220;
  settings.esp_box_thickness = 4;  // -> 1.4 px, the current box width
  settings.esp_box_cornered = 0;   // BoxStyle::straight
  settings.esp_head_box = 0;
  settings.esp_skeleton = 0;
  settings.esp_tracer = 0;
  settings.esp_tracer_origin = 0;  // TracerOrigin::screen_bottom
  settings.esp_offscreen = 0;
  settings.esp_glow = 0;
  settings.esp_visible_only = 0;
  settings.esp_distance = 0;
  settings.esp_scale_by_distance = 0;
  settings.esp_max_distance_m = 0;  // 0 = unlimited
  settings.esp_radar = 0;
  settings.esp_sound = 0;
  settings.esp_grenades = 0;
  settings.esp_c4 = 0;
  settings.esp_weapons = 0;
  settings.esp_local_player = 0;
  settings.esp_show_spectators = 0;
  settings.esp_show_teammates = 0;
  settings.esp_max_box_scale = 1.0F;
  settings.esp_color_enemy[0] = 235;
  settings.esp_color_enemy[1] = 70;
  settings.esp_color_enemy[2] = 70;
  settings.esp_color_enemy[3] = 235;
  settings.esp_color_team[0] = 70;
  settings.esp_color_team[1] = 150;
  settings.esp_color_team[2] = 235;
  settings.esp_color_team[3] = 235;
  settings.esp_color_box[0] = 235;
  settings.esp_color_box[1] = 70;
  settings.esp_color_box[2] = 70;
  settings.esp_color_box[3] = 235;
  settings.esp_color_skeleton[0] = 255;
  settings.esp_color_skeleton[1] = 255;
  settings.esp_color_skeleton[2] = 255;
  settings.esp_color_skeleton[3] = 220;
  settings.esp_color_tracer[0] = 235;
  settings.esp_color_tracer[1] = 70;
  settings.esp_color_tracer[2] = 70;
  settings.esp_color_tracer[3] = 200;
  settings.esp_color_offscreen[0] = 255;
  settings.esp_color_offscreen[1] = 90;
  settings.esp_color_offscreen[2] = 90;
  settings.esp_color_offscreen[3] = 220;
  settings.esp_color_sound[0] = 255;
  settings.esp_color_sound[1] = 200;
  settings.esp_color_sound[2] = 60;
  settings.esp_color_sound[3] = 220;
  settings.esp_color_grenade[0] = 90;
  settings.esp_color_grenade[1] = 200;
  settings.esp_color_grenade[2] = 255;
  settings.esp_color_grenade[3] = 220;
  settings.esp_color_c4[0] = 255;
  settings.esp_color_c4[1] = 120;
  settings.esp_color_c4[2] = 40;
  settings.esp_color_c4[3] = 235;
  // --- Movement ---
  settings.auto_strafe = 0;
  settings.bhop = 0;
  settings.movement_strafe_mode = 2;  // StrafeMode::directional (the WASD heading)
  settings.movement_strafe_ignore_keys = 0;
  settings.movement_strafe_wishspeed = 0.0F;
  settings.movement_strafe_subticks = 8;  // the live-validated slice count
  settings.movement_thirdperson = 0;
  settings.movement_thirdperson_distance = 64.0F;
  settings.movement_thirdperson_angle = 0.0F;
  settings.movement_thirdperson_pivot_z = 0.0F;
  settings.movement_thirdperson_fov = 0.0F;  // 0 = keep the game's FOV
  settings.movement_thirdperson_side = 1;    // ThirdPersonSide::right
  // --- Ragebot ---
  settings.rage = 0;
  settings.rage_key = 0;
  settings.rage_fire = 0;
  settings.rage_objective = 1;  // hitchance
  settings.rage_hitbox = 0;     // auto
  settings.rage_fov = 0.0F;
  settings.rage_min_damage = 0.0F;
  settings.rage_min_hitchance = 0.0F;
  settings.rage_autostop = 0;
  settings.rage_autoscope = 0;
  settings.rage_autoreload = 0;
  settings.rage_only_visible = 0;
  settings.rage_mode = 0;
  settings.rage_per_weapon = 0;
  settings.rage_config_op = 0;
  settings.rage_config_token = 0;
  settings.rage_recoil = 0;
  settings.rage_stop_speed = 0.0F;
  settings.rage_backtrack = 0;
  // --- Visuals ---
  settings.visuals_chams = 0;
  settings.visuals_chams_mode = 0;  // ChamsMode::flat
  settings.visuals_chams_visible = 0;
  settings.visuals_chams_local = 0;
  settings.visuals_chams_color[0] = 120;
  settings.visuals_chams_color[1] = 200;
  settings.visuals_chams_color[2] = 255;
  settings.visuals_chams_color[3] = 160;
  // --- Menu ---
  settings.menu_open = 0;
  settings.menu_key = 0x2D;  // Insert
  settings.menu_accent[0] = 90;
  settings.menu_accent[1] = 160;
  settings.menu_accent[2] = 255;
  settings.menu_accent[3] = 255;
  settings.menu_transparency = 100;
  settings.menu_config_op = 0;
  settings.menu_config_token = 0;
  return settings;
}

}  // namespace cs2::overlay::wire

