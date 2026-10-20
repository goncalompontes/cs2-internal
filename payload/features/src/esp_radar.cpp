#include <cmath>
#include <utility>
#include <vector>

#include "cs2/features/esp.hpp"
#include "cs2/features/esp_geometry.hpp"
#include "cs2/game/math.hpp"
#include "cs2/render/draw_list.hpp"

#include "esp_detail.hpp"

namespace cs2::features::esp_detail {
namespace {

// The radar's radius on screen, in pixels.
constexpr float k_radar_radius_px = 80.0F;

// The clearance between the radar and the screen's edge, in pixels.
constexpr float k_radar_margin_px = 24.0F;

// Where the radar sits. There is no setting for it, so it is a constant; the
// enum keeps the position a mode rather than a magic int.
constexpr RadarPosition k_radar_position = RadarPosition::bottom_left;

// The radar's own colours. The player's dots use the enemy/team accent, so
// they follow the settings.
constexpr render::Color k_radar_background{0, 0, 0, 90};
constexpr render::Color k_radar_ring{255, 255, 255, 80};
constexpr render::Color k_radar_axis{255, 255, 255, 55};
constexpr render::Color k_radar_self{255, 255, 255, 235};

// The local player's marker: a small triangle pointing up (the view
// direction).
constexpr float k_radar_self_size = 5.0F;

// A player's dot, in pixels.
constexpr float k_radar_dot_size = 3.0F;

}  // namespace

void build_radar(Frame& frame) {
  const Config& config = frame.config;
  if (!config.esp.radar) {
    return;
  }
  const auto local = frame.snapshot.local;
  if (!local.has_value()) {
    return;
  }

  // The radar's centre: the corner of the viewport the position names.
  float centre_x = 0.0F;
  float centre_y = 0.0F;
  switch (k_radar_position) {
    case RadarPosition::bottom_right:
      centre_x = frame.width - k_radar_margin_px - k_radar_radius_px;
      centre_y = frame.height - k_radar_margin_px - k_radar_radius_px;
      break;
    case RadarPosition::top_left:
      centre_x = k_radar_margin_px + k_radar_radius_px;
      centre_y = k_radar_margin_px + k_radar_radius_px;
      break;
    case RadarPosition::top_right:
      centre_x = frame.width - k_radar_margin_px - k_radar_radius_px;
      centre_y = k_radar_margin_px + k_radar_radius_px;
      break;
    case RadarPosition::bottom_left:
    default:
      centre_x = k_radar_margin_px + k_radar_radius_px;
      centre_y = frame.height - k_radar_margin_px - k_radar_radius_px;
      break;
  }

  // The disc, and the ring around it. The wire has no circle primitive, so
  // the disc is a filled polygon and the ring a run of line segments.
  const std::vector<std::pair<float, float>> disc =
      esp_geometry::circle_points(centre_x, centre_y, k_radar_radius_px);
  frame.out.poly(disc, k_radar_background);
  for (std::size_t i = 0; i < disc.size(); ++i) {
    const std::pair<float, float>& a = disc[i];
    const std::pair<float, float>& b = disc[(i + 1) % disc.size()];
    frame.out.line(a.first, a.second, b.first, b.second, k_radar_ring, 1.0F);
  }

  // The world axes: the view direction (up) and the cross axis.
  frame.out.line(centre_x, centre_y - k_radar_radius_px, centre_x, centre_y + k_radar_radius_px,
                 k_radar_axis, 1.0F);
  frame.out.line(centre_x - k_radar_radius_px, centre_y, centre_x + k_radar_radius_px, centre_y,
                 k_radar_axis, 1.0F);

  // The local player, at the centre, pointing along the view direction.
  frame.out.poly({{centre_x, centre_y - k_radar_self_size},
                  {centre_x + k_radar_self_size * 0.8F, centre_y + k_radar_self_size * 0.6F},
                  {centre_x - k_radar_self_size * 0.8F, centre_y + k_radar_self_size * 0.6F}},
                 k_radar_self);

  // The other players, as dots coloured by team.
  for (const game::PlayerSnapshot& player : frame.snapshot.players) {
    if (player.is_local) {
      continue;
    }
    const float dx = esp_geometry::units_to_metres(player.origin.x - local->origin.x);
    const float dy = esp_geometry::units_to_metres(player.origin.y - local->origin.y);
    const esp_geometry::RadarPoint point =
        esp_geometry::radar_offset(dx, dy, local->eye_angles.yaw, k_radar_radius_px);
    if (std::abs(point.x) > k_radar_radius_px || std::abs(point.y) > k_radar_radius_px) {
      continue;  // outside the radar's radius
    }
    const render::Color color = accent_color(frame.config, frame.snapshot, player);
    const float x = centre_x + point.x;
    const float y = centre_y + point.y;
    frame.out.fill(x - k_radar_dot_size * 0.5F, y - k_radar_dot_size * 0.5F,
                   x + k_radar_dot_size * 0.5F, y + k_radar_dot_size * 0.5F, color);
    if (frame.stats != nullptr) {
      ++frame.stats->radar_dots;
    }
  }
}

}  // namespace cs2::features::esp_detail
