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
//
// The **settings** block has two writers (the layer's menu edits it, the
// payload publishes a loaded configuration back into it) and the two share no
// lock, so its seqlock alone cannot rule out a torn read: the block carries
// its own checksum (`settings_checksum`) and both readers verify it.

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cs2::overlay::wire {

inline constexpr uint32_t k_magic = 0x324F5343U;  // 'CSO2'
// v13: the settings block gained a checksum (it has two writers) and the
// config path field grew to `MAX_PATH`. A stale binary refuses the segment
// (`segment_is_compatible`) instead of reading a block it cannot verify.
inline constexpr uint32_t k_version = 13;

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
  // The absolute path of the config file (read-only). `MAX_PATH` bytes, so
  // the path the menu shows is the path the payload uses (a longer path is
  // refused rather than truncated to a different file).
  char menu_config_path[260];
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
  std::atomic<uint32_t> screen_width;       // written by the layer
  std::atomic<uint32_t> screen_height;      // written by the layer
  std::atomic<uint32_t> sequence;           // seqlock, written by the payload
  std::atomic<uint32_t> settings_sequence;  // seqlock, written by both sides
  // The checksum of `settings` (FNV-1a over its bytes), written inside the
  // settings seqlock window. It is an atomic so the readers never tear it,
  // and it sits **before** the block so it keeps a 4-byte aligned offset —
  // `std::atomic` requires its natural alignment.
  std::atomic<uint32_t> settings_checksum;
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
// The settings block is packed, so `Settings` has no padding: every byte of
// the block carries a setting and the checksum below is well defined.
static_assert(std::is_standard_layout_v<Shared>,
              "wire::Shared must be standard-layout: it is shared across an ABI boundary");

// The checksum lives in an atomic, and an atomic needs its natural alignment.
static_assert(offsetof(Shared, settings_checksum) % alignof(std::atomic<uint32_t>) == 0,
              "wire::Shared::settings_checksum must stay naturally aligned");

// The settings block's checksum.
//
// The block is written by **two** processes — the layer's menu edits it, and
// the payload publishes a loaded configuration back into it — and the two
// share no lock, so a reader can see a torn block: a colour byte from one
// write, a flag from the other. A seqlock cannot catch that on its own: it
// only sees the sequence, and two writers interleave their sequence updates
// as well.
//
// The block is therefore self-verifying: the writer computes the checksum
// inside the seqlock window, the reader recomputes it over the copy it took
// and rejects a mismatch, so a torn read is always detected.
//
// FNV-1a over the block's bytes. The function lives in this header — the one
// both sides compile — so the payload and the layer can never compute
// different values for the same block.
[[nodiscard]] constexpr auto settings_checksum(const Settings& settings) noexcept -> uint32_t {
  const auto bytes = std::bit_cast<std::array<unsigned char, sizeof(Settings)>>(settings);
  uint32_t hash = 2166136261U;  // the FNV-1a offset basis
  for (const unsigned char byte : bytes) {
    hash ^= static_cast<uint32_t>(byte);
    hash *= 16777619U;  // the FNV-1a prime
  }
  return hash;
}

// The segment is usable only when the magic and the version match.
//
// A stale binary must **refuse** a segment it does not understand rather than
// read a block whose layout — and whose checksum — it does not know. Both
// sides check this before they touch the block.
[[nodiscard]] constexpr auto segment_is_compatible(uint32_t magic, uint32_t version) noexcept
    -> bool {
  return magic == k_magic && version == k_version;
}

// Write the settings block into the segment under the settings seqlock.
//
// The checksum is written **inside** the window, right after the block, so a
// reader that takes the block and the checksum under the same window either
// sees a consistent pair or rejects it.
inline void write_settings(Shared& shared, const Settings& in) noexcept {
  const uint32_t sequence = shared.settings_sequence.load(std::memory_order_relaxed);
  shared.settings_sequence.store(sequence + 1, std::memory_order_relaxed);  // odd: writing
  std::memcpy(&shared.settings, &in, sizeof(Settings));
  shared.settings_checksum.store(settings_checksum(in), std::memory_order_relaxed);
  shared.settings_sequence.store(sequence + 2, std::memory_order_release);  // even: stable
}

// Read the settings block out of the segment.
//
// The copy is taken under the settings seqlock and **verified**: the checksum
// that was written with the block has to match the copy, or the read was torn
// (the block has two writers) and the caller has to try again.
//
// `sequence` (optional) receives the seqlock sequence the copy was taken at,
// so the caller can tell whether the block changed since the last read.
[[nodiscard]] inline auto read_settings(const Shared& shared, Settings& out,
                                        uint32_t* sequence = nullptr) noexcept -> bool {
  for (int attempt = 0; attempt < 3; ++attempt) {
    const uint32_t first = shared.settings_sequence.load(std::memory_order_acquire);
    if ((first & 1U) != 0U) {
      continue;  // a write is in progress
    }
    Settings copy{};
    std::memcpy(&copy, &shared.settings, sizeof(Settings));
    const uint32_t checksum = shared.settings_checksum.load(std::memory_order_relaxed);
    if (shared.settings_sequence.load(std::memory_order_acquire) != first) {
      continue;  // the block changed under the copy
    }
    if (checksum != settings_checksum(copy)) {
      continue;  // a torn read: two writers share the block
    }
    out = copy;
    if (sequence != nullptr) {
      *sequence = first;
    }
    return true;
  }
  return false;
}

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

// The checksum of the default settings block.
//
// The payload and the layer compile this header, so the constant pins the two
// builds to the same checksum: a change to the block's layout or to the
// checksum function breaks the build on **both** sides instead of producing a
// block the other side cannot verify.
//
// **Changing a default in `default_settings()` changes this constant.** The
// assertion below then fires in all three builds (the host suite, the payload
// and the Vulkan layer); recompute it and say so in the commit. The failure is
// deliberate: a default that moves without the constant moving would let the
// two binaries disagree about what the block's checksum is.
inline constexpr uint32_t k_default_settings_checksum = 3884274149U;

static_assert(settings_checksum(default_settings()) == k_default_settings_checksum,
              "the settings checksum changed: the payload and the layer must agree on it");

}  // namespace cs2::overlay::wire
