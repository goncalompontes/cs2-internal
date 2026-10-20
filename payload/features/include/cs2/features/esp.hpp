#pragma once

#include <array>
#include <cstdint>

#include "cs2/features/esp_cache.hpp"
#include "cs2/game/matrix.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/render/draw_list.hpp"

namespace cs2::features {

// How the real studio hitboxes are drawn (`docs/re/aim.md`).
enum class HitboxMode : std::uint8_t {
  outline = 0,    // the capsule silhouette outline
  solid = 1,      // the silhouette filled
  wireframe = 2,  // the capsule mesh cage (rim/cap rings + longitudinals)
};

// How the player box is drawn.
enum class BoxStyle : std::uint8_t {
  straight = 0,  // a full rectangle
  corner = 1,    // corner brackets
};

// Where a tracer line starts.
enum class TracerOrigin : std::uint8_t {
  screen_bottom = 0,  // the bottom edge of the screen
  eye = 1,            // the local player's eye
};

// How the auto-strafe steers.
enum class StrafeMode : std::uint8_t {
  off = 0,              // no auto-strafe
  camera_relative = 1,  // the wish direction follows the camera
  directional = 2,      // the wish direction follows WASD
};

// How the chams are drawn.
enum class ChamsMode : std::uint8_t {
  flat = 0,
  glow = 1,
  wireframe = 2,
};

// Which shoulder the third-person camera sits over.
enum class ThirdPersonSide : std::uint8_t {
  left = 0,
  right = 1,
};

// An RGBA colour, in the wire's byte order.
struct Color {
  std::uint8_t r = 255;
  std::uint8_t g = 255;
  std::uint8_t b = 255;
  std::uint8_t a = 255;

  // Copy the colour into the wire's four bytes (RGBA order).
  void to_wire(std::uint8_t (&out)[4]) const noexcept {
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = a;
  }

  // Read a colour from the wire's four bytes.
  [[nodiscard]] static auto from_wire(const std::uint8_t (&bytes)[4]) noexcept -> Color {
    return Color{.r = bytes[0], .g = bytes[1], .b = bytes[2], .a = bytes[3]};
  }

  [[nodiscard]] auto operator==(const Color&) const noexcept -> bool = default;
};

// --- The legal range of every numeric setting -------------------------------
//
// The `from_wire_settings` conversion clamps with these, so a stale or corrupt
// shared segment can never push a slider out of the range the menu draws it
// for. The ranges are the ones documented on the wire fields.
inline constexpr std::uint8_t k_esp_box_thickness_max = 20;    // -> 0.5..5.0 px
inline constexpr std::uint8_t k_esp_max_distance_m_max = 255;  // 0 = unlimited
inline constexpr float k_esp_max_box_scale_min = 0.25F;
inline constexpr float k_esp_max_box_scale_max = 2.0F;
inline constexpr float k_movement_strafe_wishspeed_max = 3200.0F;
inline constexpr std::uint8_t k_movement_strafe_subticks_max = 255;
inline constexpr float k_movement_thirdperson_distance_min = 64.0F;
inline constexpr float k_movement_thirdperson_distance_max = 512.0F;
inline constexpr float k_movement_thirdperson_angle_max = 360.0F;
inline constexpr float k_movement_thirdperson_pivot_z_max = 64.0F;
inline constexpr float k_movement_thirdperson_fov_max = 180.0F;
inline constexpr std::uint8_t k_menu_transparency_max = 100;
inline constexpr float k_rage_fov_max = 360.0F;
inline constexpr float k_rage_min_damage_max = 200.0F;
inline constexpr float k_rage_min_hitchance_max = 1.0F;
inline constexpr float k_rage_stop_speed_max = 3200.0F;

// The typed, sectioned settings the features read.
//
// This is the *same* configuration as the flat `wire::Settings` block, but
// with real types: `bool` for the flags, `float` for the numbers, `Color` for
// the RGBA bytes and an `enum class` for each enumerated mode. The mapping is
// one-to-one and exhaustive in both directions — `to_wire_settings` /
// `from_wire_settings` (`cs2/features/settings.hpp`) are the only places that
// translate, and the host test `features-config` proves the two directions
// cover the same field set.
//
// The defaults are conservative: every added element is off, the third-person
// camera and the chams are off, the menu key is Insert and the auto-strafe is
// camera-relative. They mirror `wire::default_settings()`.
struct Config {
  // Which ESP elements to draw, and how.
  struct Esp {
    bool box = true;
    bool health_bar = true;
    bool ammo_bar = true;
    bool name = true;
    bool weapon = true;
    bool flags = true;
    bool only_enemies = true;  // skip the local player
    bool show_dead = false;
    // Draw the real studio hitbox capsules (`docs/re/aim.md`). Enabled from the
    // menu's ESP tab; the mode and colour are menu-owned (`wire::Settings`).
    bool hitboxes = false;
    HitboxMode hitbox_mode = HitboxMode::outline;
    // false = the automatic per-hitgroup scheme; true = `hitbox_color`.
    bool hitbox_custom_color = false;
    Color hitbox_color{90, 255, 130, 220};

    // The line width of the player box: 0..20 maps to 0.5..5.0 px.
    std::uint8_t box_thickness = 4;
    BoxStyle box_style = BoxStyle::straight;
    bool head_box = false;
    bool skeleton = false;
    bool tracer = false;
    TracerOrigin tracer_origin = TracerOrigin::screen_bottom;
    bool offscreen = false;
    bool glow = false;
    bool visible_only = false;
    bool distance = false;
    bool scale_by_distance = false;
    std::uint8_t max_distance_m = 0;  // 0 = unlimited
    bool radar = false;
    bool sound = false;
    bool grenades = false;
    bool c4 = false;
    bool weapons = false;  // dropped weapons
    bool local_player = false;
    bool show_spectators = false;
    bool show_teammates = false;
    float max_box_scale = 1.0F;

    Color color_enemy{235, 70, 70, 235};
    Color color_team{70, 150, 235, 235};
    // The box colour for an *enemy*; a teammate's box follows `color_team`. It
    // defaults to the enemy accent so the classic red-foe / blue-friend look
    // is preserved, and it can be set independently to make the box stand out
    // from the name.
    Color color_box{235, 70, 70, 235};
    Color color_skeleton{255, 255, 255, 220};
    Color color_tracer{235, 70, 70, 200};
    Color color_offscreen{255, 90, 90, 220};
    Color color_sound{255, 200, 60, 220};
    Color color_grenade{90, 200, 255, 220};
    Color color_c4{255, 120, 40, 235};

    [[nodiscard]] auto operator==(const Esp&) const noexcept -> bool = default;
  } esp;

  // The movement features (`docs/re/subtick.md`).
  struct Movement {
    bool auto_strafe = false;
    bool bhop = false;
    // Directional by default: the travel heading comes from the held W/A/S/D.
    // `camera_relative` is the pre-T3 behaviour (the goal is the camera's
    // forward/back and a held A/D stands the feature down).
    StrafeMode strafe_mode = StrafeMode::directional;
    bool strafe_ignore_keys = false;  // true = A/D stands down
    float strafe_wishspeed = 0.0F;    // 0 = the game's own wish speed
    // 8 sub-tick slices is the live-validated setting (+85.1 median / +104.9
    // max u/s per hop, `docs/handoff.md`); 0 is the camera-frame analog, which
    // measures as "nearly flat" because the {-1,0,1} snap loses the exact
    // perpendicular angle.
    std::uint8_t strafe_subticks = 8;
    bool thirdperson = false;
    float thirdperson_distance = 64.0F;
    float thirdperson_angle = 0.0F;
    float thirdperson_pivot_z = 0.0F;
    float thirdperson_fov = 0.0F;  // 0 = keep the game's FOV
    ThirdPersonSide thirdperson_side = ThirdPersonSide::right;

    [[nodiscard]] auto operator==(const Movement&) const noexcept -> bool = default;
  } movement;

  // The ragebot's tuning (`docs/ragebot.md`).
  struct Rage {
    bool enabled = false;
    std::uint8_t key = 0;        // the virtual-key code held to trigger (0 = none)
    bool fire = false;           // auto-fire while aiming
    std::uint8_t objective = 1;  // 0 = damage, 1 = hitchance, 2 = expected
    std::uint8_t hitbox = 0;     // 0 = auto (head->body), 1 = head, 2 = body
    float fov = 0.0F;            // target cone (degrees; 0 = unlimited)
    float min_damage = 0.0F;     // don't aim/fire below this damage
    float min_hitchance = 0.0F;  // don't fire below this hitchance
    // The assists: auto-stop / auto-scope modes (0 off, 1 always while solving,
    // 2 only while firing), auto-reload, only-visible.
    std::uint8_t autostop = 0;
    std::uint8_t autoscope = 0;
    std::uint8_t autoreload = 0;
    std::uint8_t only_visible = 0;
    // The trigger mode (0 hold / 1 always / 2 toggle) and the per-weapon profile state.
    std::uint8_t mode = 0;
    std::uint8_t per_weapon = 0;
    std::uint8_t config_op = 0;      // one-shot op, see `config_store.hpp`
    std::uint32_t config_token = 0;  // bumped by the menu to signal the op
    std::array<char, 24> weapon{};   // the designer name the profile is keyed on
    // Recoil compensation, and the auto-stop speed threshold in units/s (0 = full stop).
    std::uint8_t recoil = 0;
    float stop_speed = 0.0F;
    // Lag-record backtracking (`docs/ragebot.md` §8). Off by default: each pose is a
    // full solve and record sampling is ~0.6 ms/tick, so it is disabled until the
    // perf work lands. The feature is retained and can be re-enabled here.
    std::uint8_t backtrack = 0;

    [[nodiscard]] auto operator==(const Rage&) const noexcept -> bool = default;
  } rage;

  // The material overrides (chams).
  struct Visuals {
    bool chams = false;
    ChamsMode chams_mode = ChamsMode::flat;
    bool chams_visible = false;  // only when the player is visible
    bool chams_local = false;    // also chams the local player
    Color chams_color{120, 200, 255, 160};

    [[nodiscard]] auto operator==(const Visuals&) const noexcept -> bool = default;
  } visuals;

  // The menu's own state and the config file's one-shot op.
  struct Menu {
    bool show_hud = true;     // the debug HUD window
    bool open = false;        // written by the layer; the payload may suppress input
    std::uint8_t key = 0x2D;  // the virtual-key code that opens the menu (Insert)
    Color accent{90, 160, 255, 255};
    std::uint8_t transparency = 100;  // the window alpha, 0..100
    std::uint8_t config_op = 0;       // 1 = save, 2 = load, 3 = reset
    std::uint32_t config_token = 0;   // bumped by the menu to signal the op
    // The config file's path (read-only). `MAX_PATH` bytes, the same width as
    // the wire field, so the path the menu shows is the path the payload uses.
    std::array<char, 260> config_path{};

    [[nodiscard]] auto operator==(const Menu&) const noexcept -> bool = default;
  } menu;

  [[nodiscard]] auto operator==(const Config&) const noexcept -> bool = default;
};

// Copy a configuration colour into the render module's colour.
[[nodiscard]] constexpr auto to_render(const Color& color) noexcept -> render::Color {
  return render::Color{color.r, color.g, color.b, color.a};
}
// Per-build counters, for diagnostics/logging (never used for rendering).
struct EspStats {
  int players = 0;
  int skipped_local = 0;
  int skipped_dead = 0;
  int skipped_box = 0;    // a projected corner was behind the camera
  int skipped_small = 0;  // projected box was degenerate
  int drawn = 0;
  // The expanded suite.
  int arrows = 0;         // off-screen target arrows
  int skeletons = 0;      // players with a skeleton drawn
  int head_boxes = 0;     // head boxes drawn
  int world_markers = 0;  // grenades / the bomb / dropped weapons
  int sound_markers = 0;  // recent sounds drawn
  int radar_dots = 0;     // the players on the radar
  int entity_walks = 0;   // the entity-list walks performed
};

// Builds the screen-space ESP draw list from a snapshot + the world-to-screen
// matrix. All text uses the embedded pixel font.
class Esp {
 public:
  Esp() = default;
  explicit Esp(Config config) : config_(config) {}

  void build(const game::Snapshot& snapshot, const game::Matrix4x4& view, float width, float height,
             render::DrawList& out, EspStats* stats = nullptr) const;

  [[nodiscard]] const Config& config() const noexcept {
    return config_;
  }
  Config& config() noexcept {
    return config_;
  }

  // The out-of-module data sources (the studio hitboxes and the sound
  // events). The owner of the `Esp` wires them; without them the head box
  // and the skeleton fall back to the model reader and the sound ESP stays
  // empty.
  void set_providers(const EspProviders& providers) noexcept {
    providers_ = providers;
  }
  [[nodiscard]] const EspProviders& providers() const noexcept {
    return providers_;
  }

 private:
  Config config_{};
  EspProviders providers_{};
  // The per-frame caches. `build` is const (the ESP is rebuilt in place every
  // frame), so they are mutable; they belong to the instance, not to a
  // global (ADR-0010).
  mutable HitboxCache hitboxes_{};
  mutable WorldCache world_{};
  mutable SoundCache sounds_{};
};

}  // namespace cs2::features
