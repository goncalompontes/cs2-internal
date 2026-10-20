#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cs2/features/esp.hpp"
#include "cs2/features/esp_geometry.hpp"
#include "cs2/features/esp_skeleton.hpp"
#include "cs2/game/math.hpp"
#include "cs2/game/matrix.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/render/draw_list.hpp"

#include "esp_detail.hpp"

namespace cs2::features::esp_detail {
namespace {

using game::PlayerSnapshot;
using game::Vec2;
using game::Vec3;

// The neutral label colours: the settings have no entry for them.
constexpr render::Color k_color_text{240, 240, 240, 255};
constexpr render::Color k_color_flag{205, 205, 205, 255};
constexpr render::Color k_color_flag_alert{240, 90, 90, 255};
constexpr render::Color k_color_track{0, 0, 0, 150};
constexpr render::Color k_color_border{0, 0, 0, 200};
// The health bar's ramp and the ammo bar's colour: the spec lets them keep
// their ramps, and there is no setting for them.
constexpr render::Color k_color_health_high{80, 210, 80, 255};
constexpr render::Color k_color_health_mid{230, 200, 60, 255};
constexpr render::Color k_color_health_low{230, 70, 70, 255};
constexpr render::Color k_color_ammo{235, 175, 40, 255};

// The ESP text is rasterized at this pixel size (matches the backend font
// size); used to lay out stacked labels.
constexpr float k_text_height = 16.0F;

// The head box's fallback half-extent, in world units (0.2 m).
constexpr float k_head_half = 8.0F;

// The head box's fallback forward offset, in world units: the pawn's eyes sit
// inside its head, so the estimate is the eye origin nudged along the look
// direction.
constexpr float k_head_forward = 2.0F;

// The off-screen arrow's size, in pixels.
constexpr float k_arrow_size = 9.0F;

// "weapon_ak47" -> "ak47"
auto weapon_label(std::string_view designer) -> std::string {
  constexpr std::string_view prefix = "weapon_";
  if (designer.starts_with(prefix)) {
    return std::string(designer.substr(prefix.size()));
  }
  return std::string(designer);
}

auto health_color(float fraction) -> render::Color {
  if (fraction > 0.5F) {
    return k_color_health_high;
  }
  if (fraction > 0.25F) {
    return k_color_health_mid;
  }
  return k_color_health_low;
}

// The player's world AABB (origin + collision extents) projected to a screen
// box. Uses extents so it works whether the collision bounds are local or
// absolute.
auto project_box(const game::Matrix4x4& view, const PlayerSnapshot& player, float width,
                 float height, Vec2& min, Vec2& max) -> bool {
  float half_x = (player.maxs.x - player.mins.x) * 0.5F;
  float half_y = (player.maxs.y - player.mins.y) * 0.5F;
  float box_height = player.maxs.z - player.mins.z;
  if (half_x <= 0.5F || half_x >= 200.0F) {
    half_x = 16.0F;
  }
  if (half_y <= 0.5F || half_y >= 200.0F) {
    half_y = 16.0F;
  }
  if (box_height <= 1.0F || box_height >= 200.0F) {
    box_height = 72.0F;
  }

  min = Vec2{.x = FLT_MAX, .y = FLT_MAX};
  max = Vec2{.x = -FLT_MAX, .y = -FLT_MAX};
  for (int i = 0; i < 8; ++i) {
    const Vec3 corner{.x = player.origin.x + ((i & 1) != 0 ? half_x : -half_x),
                      .y = player.origin.y + ((i & 2) != 0 ? half_y : -half_y),
                      .z = player.origin.z + ((i & 4) != 0 ? box_height : 0.0F)};
    const auto screen = view.project(corner, width, height);
    if (!screen.has_value()) {
      return false;  // a corner is behind the camera
    }
    min.x = std::min(min.x, screen->x);
    min.y = std::min(min.y, screen->y);
    max.x = std::max(max.x, screen->x);
    max.y = std::max(max.y, screen->y);
  }
  return max.x > min.x && max.y > min.y;
}

// A world AABB projected to a screen box (the head box).
auto project_world_box(const game::Matrix4x4& view, const Vec3& world_min, const Vec3& world_max,
                       float width, float height, Vec2& min, Vec2& max) -> bool {
  min = Vec2{.x = FLT_MAX, .y = FLT_MAX};
  max = Vec2{.x = -FLT_MAX, .y = -FLT_MAX};
  for (int i = 0; i < 8; ++i) {
    const Vec3 corner{.x = world_min.x + ((i & 1) != 0 ? world_max.x - world_min.x : 0.0F),
                      .y = world_min.y + ((i & 2) != 0 ? world_max.y - world_min.y : 0.0F),
                      .z = world_min.z + ((i & 4) != 0 ? world_max.z - world_min.z : 0.0F)};
    const auto screen = view.project(corner, width, height);
    if (!screen.has_value()) {
      return false;  // a corner is behind the camera
    }
    min.x = std::min(min.x, screen->x);
    min.y = std::min(min.y, screen->y);
    max.x = std::max(max.x, screen->x);
    max.y = std::max(max.y, screen->y);
  }
  return max.x > min.x && max.y > min.y;
}

// The unit vector the player is looking along.
auto look_direction(const game::Angle& angles) -> Vec3 {
  constexpr float k_deg_to_rad = 0.017453292519943295F;
  const float pitch = angles.pitch * k_deg_to_rad;
  const float yaw = angles.yaw * k_deg_to_rad;
  const float cos_pitch = std::cos(pitch);
  return Vec3{.x = cos_pitch * std::cos(yaw), .y = cos_pitch * std::sin(yaw), .z = std::sin(pitch)};
}

// The filters: the local player, the teammates, the spectators and the
// distance cap.
auto passes_filters(const Config& config, const game::Snapshot& snapshot,
                    const PlayerSnapshot& player) -> bool {
  const int local_team = snapshot.local.has_value() ? snapshot.local->team : 0;
  const bool teammate = local_team != 0 && player.team == local_team;
  if (player.is_local) {
    // `only_enemies` (the legacy switch) drops the local player; the explicit
    // `local_player` switch puts it back.
    if (!config.esp.local_player && config.esp.only_enemies) {
      return false;
    }
  } else if (teammate && !config.esp.show_teammates && config.esp.only_enemies) {
    return false;
  }
  // A player who is not alive is dead or watching the game: the spectator
  // filter. `show_dead` is the legacy switch for the same set.
  if (!player.alive && !config.esp.show_dead && !config.esp.show_spectators) {
    return false;
  }
  if (config.esp.max_distance_m > 0) {
    const float metres = esp_geometry::units_to_metres(player.distance);
    if (metres > static_cast<float>(config.esp.max_distance_m)) {
      return false;
    }
  }
  return true;
}

// The box, straight or cornered, with the 1px black border that keeps it
// readable against any background.
void draw_box(Frame& frame, const Vec2& min, const Vec2& max, float thickness,
              const render::Color& color) {
  const bool cornered = frame.config.esp.box_style == BoxStyle::corner;
  if (cornered) {
    for (const esp_geometry::Segment& segment : esp_geometry::box_corner_segments(
             min.x - 1.0F, min.y - 1.0F, max.x + 1.0F, max.y + 1.0F)) {
      frame.out.line(segment.x0, segment.y0, segment.x1, segment.y1, k_color_border, 1.0F);
    }
    for (const esp_geometry::Segment& segment :
         esp_geometry::box_corner_segments(min.x, min.y, max.x, max.y)) {
      frame.out.line(segment.x0, segment.y0, segment.x1, segment.y1, color, thickness);
    }
    return;
  }
  frame.out.outline(min.x - 1.0F, min.y - 1.0F, max.x + 1.0F, max.y + 1.0F, k_color_border, 1.0F);
  frame.out.outline(min.x, min.y, max.x, max.y, color, thickness);
}

// The off-screen arrow: a triangle at the viewport edge, pointing at the
// target, with the distance next to it.
void draw_offscreen_arrow(Frame& frame, const PlayerSnapshot& player,
                          const esp_geometry::Direction& direction) {
  const esp_geometry::ArrowPlacement placement =
      esp_geometry::place_arrow(direction, frame.width, frame.height);
  if (!placement.valid) {
    return;
  }
  const std::vector<std::pair<float, float>> triangle =
      esp_geometry::arrow_triangle(placement, k_arrow_size);
  frame.out.poly(triangle, to_render(frame.config.esp.color_offscreen));
  frame.out.text(placement.x + k_arrow_size + 2.0F, placement.y - k_text_height * 0.5F,
                 accent_color(frame.config, frame.snapshot, player), distance_text(player), false,
                 true);
  if (frame.stats != nullptr) {
    ++frame.stats->arrows;
  }
}

// The head box: the studio hitbox with the head hitgroup, or — when the model
// cannot be read — an estimate around the pawn's eye origin.
void draw_head_box(Frame& frame, const PlayerSnapshot& player, const render::Color& color) {
  Vec3 world_min{};
  Vec3 world_max{};
  if (player.pawn != 0) {
    int count = 0;
    const game::HitboxCapsule* capsules = capsules_for(frame, player.pawn, count);
    for (int i = 0; i < count; ++i) {
      const game::HitboxCapsule& capsule = capsules[i];
      if (capsule.group != esp_skeleton::k_hitgroup_head) {
        continue;
      }
      const float radius = capsule.radius > 0.0F ? capsule.radius : 1.0F;
      world_min = Vec3{.x = std::min(capsule.a.x, capsule.b.x) - radius,
                       .y = std::min(capsule.a.y, capsule.b.y) - radius,
                       .z = std::min(capsule.a.z, capsule.b.z) - radius};
      world_max = Vec3{.x = std::max(capsule.a.x, capsule.b.x) + radius,
                       .y = std::max(capsule.a.y, capsule.b.y) + radius,
                       .z = std::max(capsule.a.z, capsule.b.z) + radius};
      break;
    }
  }
  if (world_max.x <= world_min.x) {
    // The fallback: the eye origin (the pawn's eyes sit inside its head),
    // nudged along the look direction.
    const Vec3 forward = look_direction(player.eye_angles);
    const Vec3 centre{.x = player.eye_origin.x + forward.x * k_head_forward,
                      .y = player.eye_origin.y + forward.y * k_head_forward,
                      .z = player.eye_origin.z + forward.z * k_head_forward};
    world_min =
        Vec3{.x = centre.x - k_head_half, .y = centre.y - k_head_half, .z = centre.z - k_head_half};
    world_max =
        Vec3{.x = centre.x + k_head_half, .y = centre.y + k_head_half, .z = centre.z + k_head_half};
  }
  Vec2 min{};
  Vec2 max{};
  if (!project_world_box(frame.view, world_min, world_max, frame.width, frame.height, min, max)) {
    return;
  }
  frame.out.outline(min.x, min.y, max.x, max.y, color, 1.0F);
  if (frame.stats != nullptr) {
    ++frame.stats->head_boxes;
  }
}

}  // namespace

void build_players(Frame& frame) {
  const Config& config = frame.config;
  const game::Snapshot& snapshot = frame.snapshot;

  for (const PlayerSnapshot& player : snapshot.players) {
    if (frame.stats != nullptr) {
      ++frame.stats->players;
    }
    if (player.is_local && !config.esp.local_player && config.esp.only_enemies) {
      if (frame.stats != nullptr) {
        ++frame.stats->skipped_local;
      }
      continue;
    }
    if (!player.alive && !config.esp.show_dead && !config.esp.show_spectators) {
      if (frame.stats != nullptr) {
        ++frame.stats->skipped_dead;
      }
      continue;
    }
    if (!passes_filters(config, snapshot, player)) {
      continue;
    }

    // The visibility state. `visible_only` drops the players that are not
    // visible; `glow` tints them instead (the "through-wall" highlight).
    // Without a visibility provider the state is unknown and both toggles
    // leave the player alone.
    const Visibility visibility = visibility_for(frame.providers, player.index);
    if (config.esp.visible_only && visibility == Visibility::hidden) {
      continue;
    }
    const bool glow = config.esp.glow && visibility == Visibility::hidden;

    Vec2 min{};
    Vec2 max{};
    const bool projected = project_box(frame.view, player, frame.width, frame.height, min, max);
    if (!projected) {
      // The box is not projectable: the target is behind the camera (or
      // straddles the near plane). The off-screen arrow is the fallback.
      if (config.esp.offscreen) {
        esp_geometry::Direction direction{};
        if (snapshot.local.has_value()) {
          direction = esp_geometry::screen_direction(player.origin.x - snapshot.local->origin.x,
                                                     player.origin.y - snapshot.local->origin.y,
                                                     snapshot.local->eye_angles.yaw);
        }
        draw_offscreen_arrow(frame, player, direction);
      }
      if (frame.stats != nullptr) {
        ++frame.stats->skipped_box;
      }
      continue;
    }

    // The distance scale shrinks the box about its centre.
    if (config.esp.scale_by_distance) {
      const float scale = esp_geometry::distance_scale(
          esp_geometry::units_to_metres(player.distance), config.esp.max_box_scale);
      if (scale != 1.0F) {
        const float cx = (min.x + max.x) * 0.5F;
        const float cy = (min.y + max.y) * 0.5F;
        const float half_w = (max.x - min.x) * 0.5F * scale;
        const float half_h = (max.y - min.y) * 0.5F * scale;
        min = Vec2{.x = cx - half_w, .y = cy - half_h};
        max = Vec2{.x = cx + half_w, .y = cy + half_h};
      }
    }

    const float box_w = max.x - min.x;
    const float box_h = max.y - min.y;
    if (box_h < 3.0F || box_w < 1.0F) {
      if (frame.stats != nullptr) {
        ++frame.stats->skipped_small;
      }
      continue;
    }

    // The target is off screen when its box misses the viewport.
    const bool on_screen =
        max.x >= 0.0F && min.x <= frame.width && max.y >= 0.0F && min.y <= frame.height;
    if (!on_screen) {
      if (config.esp.offscreen) {
        const float cx = (min.x + max.x) * 0.5F;
        const float cy = (min.y + max.y) * 0.5F;
        const esp_geometry::Direction direction{.x = cx - frame.width * 0.5F,
                                                .y = cy - frame.height * 0.5F};
        draw_offscreen_arrow(frame, player, direction);
      }
      if (frame.stats != nullptr) {
        ++frame.stats->skipped_box;
      }
      continue;
    }

    if (frame.stats != nullptr) {
      ++frame.stats->drawn;
    }
    const render::Color accent = accent_color(config, snapshot, player);
    // The glow tints a player that is not visible with the enemy/team
    // accent, so it stands out from the ordinary box colour.
    const render::Color box_color = glow ? accent : to_render(config.esp.color_box);
    const float thickness = esp_geometry::box_thickness_px(config.esp.box_thickness);

    if (config.esp.box) {
      draw_box(frame, min, max, thickness, box_color);
    }

    if (config.esp.health_bar && player.max_health > 0) {
      const float fraction = std::clamp(
          static_cast<float>(player.health) / static_cast<float>(player.max_health), 0.0F, 1.0F);
      const float bar_w = 3.0F;
      const float x0 = min.x - bar_w - 3.0F;
      frame.out.fill(x0, min.y, x0 + bar_w, max.y, k_color_track);
      frame.out.fill(x0, max.y - box_h * fraction, x0 + bar_w, max.y, health_color(fraction));
      frame.out.outline(x0 - 1.0F, min.y - 1.0F, x0 + bar_w + 1.0F, max.y + 1.0F, k_color_border,
                        1.0F);
    }

    if (config.esp.ammo_bar && player.clip >= 0) {
      // Magazine size is not exposed by the schema; 30 is a reasonable ceiling
      // for the bar fraction.
      const float fraction = std::clamp(static_cast<float>(player.clip) / 30.0F, 0.0F, 1.0F);
      const float bar_h = 3.0F;
      const float y0 = max.y + 3.0F;
      frame.out.fill(min.x, y0, max.x, y0 + bar_h, k_color_track);
      frame.out.fill(min.x, y0, min.x + box_w * fraction, y0 + bar_h, k_color_ammo);
      frame.out.outline(min.x - 1.0F, y0 - 1.0F, max.x + 1.0F, y0 + bar_h + 1.0F, k_color_border,
                        1.0F);
    }

    if (config.esp.name && !player.name.empty()) {
      // The name is the one ESP label that is *not* outlined; it carries the
      // enemy/team accent so friend and foe are told apart at a glance.
      frame.out.text((min.x + max.x) * 0.5F, min.y - k_text_height - 2.0F, accent, player.name,
                     true);
    }

    if (config.esp.distance && !player.is_local) {
      // The distance, in metres, next to the name.
      frame.out.text(max.x + 4.0F, min.y - k_text_height - 2.0F, accent, distance_text(player),
                     false, true);
    }

    if (config.esp.weapon && !player.weapon.empty()) {
      frame.out.text((min.x + max.x) * 0.5F, max.y + 8.0F, k_color_text,
                     weapon_label(player.weapon), true, true);
    }

    if (config.esp.flags) {
      float y = min.y;
      const auto flag = [&](std::string value, render::Color color) -> void {
        frame.out.text(max.x + 4.0F, y, color, std::move(value), false, true);
        y += k_text_height;
      };
      if (player.money > 0) {
        flag("$" + std::to_string(player.money), k_color_flag);
      }
      if (player.armor > 0) {
        flag("A", k_color_flag);
      }
      if (player.helmet) {
        flag("H", k_color_flag);
      }
      if (player.defuser) {
        flag("K", k_color_flag);
      }
      if (player.scoped) {
        flag("Z", k_color_flag);
      }
      if (player.defusing) {
        flag("D", k_color_flag_alert);
      }
      if (player.has_bomb) {
        flag("B", k_color_flag_alert);
      }
      if (player.flash > 0.0F) {
        flag("F", k_color_flag);
      }
      if (player.ping > 0) {
        flag(std::to_string(player.ping) + "ms", k_color_flag);
      }
      // The distance is shown next to the name when the setting asks for it;
      // the flag column carries it otherwise, so it appears exactly once.
      if (!config.esp.distance && !player.is_local) {
        flag(distance_text(player), k_color_flag);
      }
    }

    if (config.esp.tracer) {
      float origin_x = frame.width * 0.5F;
      float origin_y = frame.height;
      if (config.esp.tracer_origin == TracerOrigin::eye && snapshot.local.has_value()) {
        if (const auto eye =
                frame.view.project(snapshot.local->eye_origin, frame.width, frame.height)) {
          origin_x = eye->x;
          origin_y = eye->y;
        }
      }
      frame.out.line(origin_x, origin_y, (min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F,
                     to_render(config.esp.color_tracer), 1.0F);
    }

    if (config.esp.head_box) {
      draw_head_box(frame, player, box_color);
    }

    if (config.esp.skeleton && player.pawn != 0) {
      int count = 0;
      const game::HitboxCapsule* capsules = capsules_for(frame, player.pawn, count);
      if (count > 0) {
        draw_skeleton(frame.out, frame.view, frame.width, frame.height, capsules, count,
                      to_render(config.esp.color_skeleton));
        if (frame.stats != nullptr) {
          ++frame.stats->skeletons;
        }
      }
    }
  }
}

}  // namespace cs2::features::esp_detail
