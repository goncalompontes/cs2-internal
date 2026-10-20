#pragma once

// Portable shared-memory wire format for the overlay model (ADR-0016).
//
// Written by the injected Windows payload (`features::ModelPublisher`), read by
// the Vulkan present layer. POD only — no Vulkan, no platform types — and the
// same header is compiled by both the MSVC-ABI payload (clang-cl) and the Linux
// layer (gcc), so the layouts must agree. Only fixed-size arrays and 4-byte
// scalars are used to keep the layout identical across ABIs.
//
// Synchronization: `sequence` is a seqlock around `model`. The payload writes
// an odd value, updates `model`, then writes the next even value. The layer
// retries while the sequence is odd or changed during the copy. `screen_width`/
// `screen_height` are written by the layer so the payload knows the presented
// size to project world coordinates against.

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace cs2::overlay::wire {

inline constexpr uint32_t k_magic = 0x324F5343U;  // 'CSO2'
inline constexpr uint32_t k_version = 12;

inline constexpr int k_max_sections = 16;
inline constexpr int k_max_lines = 64;
inline constexpr int k_max_draws = 12288;
inline constexpr int k_max_vertices = 16384;  // xy pairs for filled polygons
inline constexpr int k_max_title = 64;
inline constexpr int k_max_label = 48;
inline constexpr int k_max_value = 200;
inline constexpr int k_max_text = 192;

// Force byte-identical layout across the MSVC-ABI payload and the gcc-built
// layer (their default padding for these structs differs).
#pragma pack(push, 1)

struct Line {
  char label[k_max_label];
  char value[k_max_value];
};

struct Section {
  char title[k_max_title];
  uint32_t line_count;
  Line lines[k_max_lines];
};

enum class DrawKind : uint32_t {
  rect = 0,
  filled_rect = 1,
  line = 2,
  text = 3,
  // A filled convex polygon; `vertex_offset`/`vertex_count` index `Model.vertices`
  // (xy pairs, screen space). Used for the solid hitbox capsules.
  filled_poly = 4,
};

struct Draw {
  DrawKind kind;
  float x0;
  float y0;
  float x1;
  float y1;
  float thickness;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  uint8_t centered;
  uint8_t shadow;   // 1px drop shadow
  uint8_t outline;  // 1px outline on every side
  uint8_t reserved;
  uint32_t vertex_offset;  // filled_poly: index into Model.vertices (xy pairs)
  uint32_t vertex_count;   // filled_poly: number of points
  char text[k_max_text];
};

// Menu-editable configuration (ADR-0021). Written by the layer's menu, read by
// the payload that builds the ESP, under `settings_sequence` (seqlock, same
// convention as `model`).
struct Settings {
  uint8_t box;
  uint8_t health_bar;
  uint8_t ammo_bar;
  uint8_t name;
  uint8_t weapon;
  uint8_t flags;
  uint8_t only_enemies;
  uint8_t show_dead;
  uint8_t show_hud;       // draw the debug HUD window
  uint8_t menu_open;      // written by the layer; the payload may suppress input
  uint8_t auto_strafe;    // payload movement features (`docs/re/subtick.md`):
  uint8_t bhop;           //   silent airborne air-strafe / auto-jump
  uint8_t rage;           // ragebot enabled (`docs/ragebot.md`)
  uint8_t rage_key;       //   the virtual-key code held to trigger (0 = none)
  uint8_t rage_fire;      //   auto-fire while aiming
  uint8_t hitboxes;       // draw the real studio hitboxes (`docs/re/aim.md`)
  // Hitbox rendering (menu-owned). `hitbox_mode`: 0 = outline, 1 = solid, 2 = wire.
  // `hitbox_color_mode`: 0 = automatic per-hitgroup scheme, 1 = `hitbox_color`.
  uint8_t hitbox_mode;
  uint8_t hitbox_color_mode;
  uint8_t hitbox_color[4];  // RGBA, used when `hitbox_color_mode == 1`
  // Ragebot tuning (`docs/ragebot.md`), menu-owned like the rest. `rage_fov` is the
  // target cone in degrees (0 = unlimited); `rage_min_damage`/`rage_min_hitchance` gate
  // the aim/fire; `rage_objective` selects the solve objective; `rage_hitbox` filters it.
  uint8_t rage_objective;    // 0 = damage, 1 = hitchance, 2 = expected
  uint8_t rage_hitbox;       // 0 = auto (head→body), 1 = head only, 2 = body only
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
  // Lag-record backtracking (`docs/ragebot.md` §8). **Off by default**: the sampled lag
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
  char menu_config_path[128];  // absolute path of the config file (read-only)
};

struct Model {
  char title[k_max_title];
  uint32_t section_count;
  Section sections[k_max_sections];
  uint32_t draw_count;
  Draw draws[k_max_draws];
  // Pool for `filled_poly` vertices: xy pairs, indexed by `Draw.vertex_offset`.
  uint32_t vertex_count;
  float vertices[k_max_vertices * 2];
  uint32_t in_game;  // 1 when a local player is present (menu may interact)
};

struct Shared {
  uint32_t magic;
  uint32_t version;
  std::atomic<uint32_t> screen_width;   // written by the layer
  std::atomic<uint32_t> screen_height;  // written by the layer
  std::atomic<uint32_t> sequence;       // seqlock, written by the payload
  std::atomic<uint32_t> settings_sequence;  // seqlock, written by both sides
  Settings settings;
  Model model;
};

#pragma pack(pop)

// The settings block is memcpy'd across an ABI boundary between two separately
// compiled translation units (the MSVC-ABI payload and the gcc-built layer), so
// it has to stay a plain, copyable POD: anything else would let the layout
// depend on the compiler's ABI choices.
static_assert(std::is_standard_layout_v<Settings>,
              "wire::Settings must be standard-layout: it is shared across an ABI boundary");
static_assert(std::is_trivially_copyable_v<Settings>,
              "wire::Settings must be trivially copyable: it is shared across an ABI boundary");

// The shared defaults for the settings block.
//
// Both sides derive their defaults from this one function — the payload's
// `features::default_wire_settings()` and the Vulkan layer's
// `default_ui_settings()` — so the two can never disagree about what a fresh
// segment means. The values mirror `features::Config`'s defaults; the host test
// `features-config` asserts that the two stay in step.
//
// The new elements are **conservative**: every added ESP element, the
// third-person camera and the chams are off, the menu key is Insert (0x2E) and
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
  settings.esp_color_box[0] = 255;
  settings.esp_color_box[1] = 255;
  settings.esp_color_box[2] = 255;
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
  settings.movement_strafe_mode = 1;  // StrafeMode::camera_relative
  settings.movement_strafe_ignore_keys = 0;
  settings.movement_strafe_wishspeed = 0.0F;
  settings.movement_strafe_subticks = 0;
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
  settings.menu_key = 0x2E;  // Insert
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
