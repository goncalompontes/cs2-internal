#include "cs2/features/esp.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <string_view>

namespace cs2::features {
namespace {

using game::PlayerSnapshot;
using game::Vec2;
using game::Vec3;

constexpr render::Color k_color_enemy{.r = 235, .g = 70, .b = 70, .a = 235};
constexpr render::Color k_color_team{.r = 70, .g = 150, .b = 235, .a = 235};
constexpr render::Color k_color_health_high{.r = 80, .g = 210, .b = 80, .a = 255};
constexpr render::Color k_color_health_mid{.r = 230, .g = 200, .b = 60, .a = 255};
constexpr render::Color k_color_health_low{.r = 230, .g = 70, .b = 70, .a = 255};
constexpr render::Color k_color_ammo{.r = 235, .g = 175, .b = 40, .a = 255};
constexpr render::Color k_color_text{.r = 240, .g = 240, .b = 240, .a = 255};
constexpr render::Color k_color_flag{.r = 205, .g = 205, .b = 205, .a = 255};
constexpr render::Color k_color_flag_alert{.r = 240, .g = 90, .b = 90, .a = 255};
constexpr render::Color k_color_track{.r = 0, .g = 0, .b = 0, .a = 150};
constexpr render::Color k_color_border{.r = 0, .g = 0, .b = 0, .a = 200};

// ESP text is rasterized at this pixel size (matches the backend font size);
// used to lay out stacked labels.
constexpr float k_text_height = 16.0F;

// "weapon_ak47" -> "ak47"
auto weapon_label(std::string_view designer) -> std::string {
  constexpr std::string_view prefix = "weapon_";
  if (designer.starts_with(prefix)) {
    return std::string(designer.substr(prefix.size()));
  }
  return std::string(designer);
}

// Project the player's world AABB (origin + collision extents) to a screen box.
// Uses extents so it works whether the collision bounds are local or absolute.
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

auto health_color(float fraction) -> render::Color {
  if (fraction > 0.5F) {
    return k_color_health_high;
  }
  if (fraction > 0.25F) {
    return k_color_health_mid;
  }
  return k_color_health_low;
}

}  // namespace

void Esp::build(const game::Snapshot& snapshot, const game::Matrix4x4& view, float width,
                float height, render::DrawList& out, EspStats* stats) const {
  if (stats != nullptr) {
    *stats = EspStats{};
  }
  if (!snapshot.valid || width <= 0.0F || height <= 0.0F) {
    return;
  }
  const int local_team = snapshot.local.has_value() ? snapshot.local->team : 0;

  for (const PlayerSnapshot& player : snapshot.players) {
    if (stats != nullptr) {
      ++stats->players;
    }
    if (config_.esp.only_enemies && player.is_local) {
      if (stats != nullptr) {
        ++stats->skipped_local;
      }
      continue;
    }
    if (!config_.esp.show_dead && !player.alive) {
      if (stats != nullptr) {
        ++stats->skipped_dead;
      }
      continue;
    }
    Vec2 min{};
    Vec2 max{};
    if (!project_box(view, player, width, height, min, max)) {
      if (stats != nullptr) {
        ++stats->skipped_box;
      }
      continue;
    }
    const float box_w = max.x - min.x;
    const float box_h = max.y - min.y;
    if (box_h < 3.0F || box_w < 1.0F) {
      if (stats != nullptr) {
        ++stats->skipped_small;
      }
      continue;
    }
    if (stats != nullptr) {
      ++stats->drawn;
    }
    const render::Color accent =
        (local_team != 0 && player.team != local_team) ? k_color_enemy : k_color_team;

    if (config_.esp.box) {
      // 1px black border so the box reads against any background.
      out.outline(min.x - 1.0F, min.y - 1.0F, max.x + 1.0F, max.y + 1.0F, k_color_border, 1.0F);
      out.outline(min.x, min.y, max.x, max.y, accent, 1.5F);
    }

    if (config_.esp.health_bar && player.max_health > 0) {
      const float fraction =
          std::clamp(static_cast<float>(player.health) / static_cast<float>(player.max_health),
                     0.0F, 1.0F);
      const float bar_w = 3.0F;
      const float x0 = min.x - bar_w - 3.0F;
      out.fill(x0, min.y, x0 + bar_w, max.y, k_color_track);
      out.fill(x0, max.y - box_h * fraction, x0 + bar_w, max.y, health_color(fraction));
      out.outline(x0 - 1.0F, min.y - 1.0F, x0 + bar_w + 1.0F, max.y + 1.0F, k_color_border, 1.0F);
    }

    if (config_.esp.ammo_bar && player.clip >= 0) {
      // Magazine size is not exposed by the schema; 30 is a reasonable ceiling
      // for the bar fraction.
      const float fraction = std::clamp(static_cast<float>(player.clip) / 30.0F, 0.0F, 1.0F);
      const float bar_h = 3.0F;
      const float y0 = max.y + 3.0F;
      out.fill(min.x, y0, max.x, y0 + bar_h, k_color_track);
      out.fill(min.x, y0, min.x + box_w * fraction, y0 + bar_h, k_color_ammo);
      out.outline(min.x - 1.0F, y0 - 1.0F, max.x + 1.0F, y0 + bar_h + 1.0F, k_color_border, 1.0F);
    }

    if (config_.esp.name && !player.name.empty()) {
      // The name is the one ESP label that is *not* outlined.
      out.text((min.x + max.x) * 0.5F, min.y - k_text_height - 2.0F, k_color_text, player.name,
               true);
    }

    if (config_.esp.weapon && !player.weapon.empty()) {
      out.text((min.x + max.x) * 0.5F, max.y + 8.0F, k_color_text, weapon_label(player.weapon),
               true, true);
    }

    if (config_.esp.flags) {
      float y = min.y;
      const auto flag = [&](std::string value, render::Color color) -> void {
        out.text(max.x + 4.0F, y, color, std::move(value), false, true);
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
      if (!player.is_local) {
        flag(std::to_string(static_cast<int>(std::lround(player.distance))) + "m", k_color_flag);
      }
    }
  }
}

}  // namespace cs2::features
