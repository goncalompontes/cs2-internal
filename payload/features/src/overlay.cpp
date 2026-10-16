#include "cs2/features/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "cs2/game/log.hpp"
#include "cs2/game/snapshot.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {
namespace {

// A player's hitbox set is 19 boxes; leave headroom for the visualiser buffer.
constexpr int k_max_hitboxes = 40;

auto format_vec3(const game::Vec3& value) -> std::string {
  char buffer[80];
  std::snprintf(buffer, sizeof(buffer), "%.1f %.1f %.1f", static_cast<double>(value.x),
                static_cast<double>(value.y), static_cast<double>(value.z));
  return buffer;
}

auto format_angle(const game::Angle& value) -> std::string {
  char buffer[80];
  std::snprintf(buffer, sizeof(buffer), "pitch %.1f yaw %.1f", static_cast<double>(value.pitch),
                static_cast<double>(value.yaw));
  return buffer;
}

auto format_long(long long value) -> std::string {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld", value);
  return buffer;
}

auto format_float(double value) -> std::string {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

// One hitbox's colour: the head stands out, the rest are a dim body colour.
auto hitbox_color(int group, bool active) -> render::Color {
  if (group == 1) {  // HITGROUP_HEAD
    return active ? render::Color{255, 70, 70, 235} : render::Color{255, 96, 96, 150};
  }
  return active ? render::Color{90, 255, 130, 190} : render::Color{90, 210, 140, 95};
}

// A screen-space circle, approximated by a polygon (the overlay has only lines).
void draw_circle(render::DrawList& out, const game::Vec2& centre, float radius,
                 const render::Color& color, int segments = 12) {
  if (radius < 0.5F) {
    return;
  }
  constexpr float k_tau = 6.28318530717958648F;
  game::Vec2 previous{centre.x + radius, centre.y};
  for (int i = 1; i <= segments; ++i) {
    const float theta = k_tau * static_cast<float>(i) / static_cast<float>(segments);
    const game::Vec2 point{centre.x + radius * std::cos(theta),
                           centre.y + radius * std::sin(theta)};
    out.line(previous.x, previous.y, point.x, point.y, color, 1.0F);
    previous = point;
  }
}

// Draw a world capsule as its screen-space stadium (two end circles + the two
// tangent lines). `eye` is used to pick a screen-perpendicular for the radius.
void draw_capsule(render::DrawList& out, const game::Matrix4x4& view, float width, float height,
                  const game::Vec3& eye, const game::HitboxCapsule& capsule,
                  const render::Color& color) {
  const auto pa = view.project(capsule.a, width, height);
  const auto pb = view.project(capsule.b, width, height);
  if (!pa.has_value() || !pb.has_value()) {
    return;  // an endpoint is behind the eye
  }
  // The capsule radius in screen pixels: project a point offset from an endpoint
  // perpendicular to both the capsule axis and the view ray.
  game::Vec3 axis = capsule.b - capsule.a;
  const float axis_len = game::length(axis);
  game::Vec3 perpendicular{0.0F, 0.0F, 1.0F};
  if (axis_len > 1e-3F) {
    axis = axis * (1.0F / axis_len);
    perpendicular = game::cross(axis, capsule.a - eye);
    if (game::length(perpendicular) < 1e-3F) {
      perpendicular = game::Vec3{0.0F, 0.0F, 1.0F};
    }
  }
  float radius = 1.0F;
  if (capsule.radius > 0.0F) {
    const auto offset =
        view.project(capsule.a + game::normalized(perpendicular) * capsule.radius, width, height);
    if (offset.has_value()) {
      radius = game::length(*offset - *pa);
    }
  }
  radius = std::clamp(radius, 0.75F, 90.0F);

  // Cull a capsule that is entirely off screen, or one so close to the eye that
  // it would smear a huge arc across the viewport (a player standing at the
  // camera). `pa`/`pb` are in front here, so this is purely a framing test.
  const float min_x = (std::min)(pa->x, pb->x) - radius;
  const float max_x = (std::max)(pa->x, pb->x) + radius;
  const float min_y = (std::min)(pa->y, pb->y) - radius;
  const float max_y = (std::max)(pa->y, pb->y) + radius;
  if (max_x < 0.0F || min_x > width || max_y < 0.0F || min_y > height ||
      radius > 0.3F * height) {
    return;
  }

  const float dx = pb->x - pa->x;
  const float dy = pb->y - pa->y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length <= radius * 1.5F) {
    // A short, fat capsule (a joint): one circle is the honest, readable shape
    // (drawing the two end circles + edges just overloads the view).
    draw_circle(out, game::Vec2{(pa->x + pb->x) * 0.5F, (pa->y + pb->y) * 0.5F}, radius, color);
    return;
  }
  // The two tangent edges: offset the spine by +/- the screen perpendicular.
  const float nx = -dy / length * radius;
  const float ny = dx / length * radius;
  out.line(pa->x + nx, pa->y + ny, pb->x + nx, pb->y + ny, color, 1.0F);
  out.line(pa->x - nx, pa->y - ny, pb->x - nx, pb->y - ny, color, 1.0F);
  draw_circle(out, *pa, radius, color, 14);
  draw_circle(out, *pb, radius, color, 14);
}

auto team_name(int team) -> std::string {
  switch (team) {
    case 2:
      return "T";
    case 3:
      return "CT";
    case 0:
      return "none";
    default:
      return "?";
  }
}

auto now_ms() noexcept -> std::uint64_t {
#if defined(_WIN32)
  return GetTickCount64();
#else
  return 0;
#endif
}

auto local_section(const game::Snapshot& snapshot) -> render::HudSection {
  render::HudSection section;
  section.title = "Local player";
  if (!snapshot.local.has_value()) {
    section.lines.push_back({.label = "status", .value = "not found"});
    return section;
  }
  const game::PlayerSnapshot& p = *snapshot.local;
  section.lines.push_back({.label = "name", .value = p.name.empty() ? "(unknown)" : p.name});
  section.lines.push_back({.label = "team", .value = team_name(p.team)});
  section.lines.push_back(
      {.label = "health / armor", .value = format_long(p.health) + " / " + format_long(p.armor)});
  section.lines.push_back({.label = "alive", .value = p.alive ? "yes" : "no"});
  section.lines.push_back({.label = "weapon", .value = p.weapon.empty() ? "(unknown)" : p.weapon});
  section.lines.push_back({.label = "ammo (clip / reserve)",
                           .value = format_long(p.clip) + " / " + format_long(p.reserve)});
  section.lines.push_back({.label = "origin", .value = format_vec3(p.origin)});
  section.lines.push_back({.label = "eye origin", .value = format_vec3(p.eye_origin)});
  section.lines.push_back({.label = "velocity", .value = format_vec3(p.velocity)});
  section.lines.push_back({.label = "eye angles", .value = format_angle(p.eye_angles)});
  section.lines.push_back({.label = "entity index", .value = format_long(p.index)});
  return section;
}

auto players_section(const game::Snapshot& snapshot) -> render::HudSection {
  render::HudSection section;
  section.title =
      "Players (" + format_long(static_cast<long long>(snapshot.players.size())) + ")";
  for (const game::PlayerSnapshot& p : snapshot.players) {
    std::string value = team_name(p.team);
    value += p.is_local ? " (you)" : "";
    value += "  hp " + format_long(p.health);
    value += "  " + (p.weapon.empty() ? std::string("?") : p.weapon);
    if (!p.is_local) {
      value += "  d " + format_float(static_cast<double>(p.distance));
    }
    value += "  " + format_vec3(p.origin);
    section.lines.push_back(
        {.label = p.name.empty() ? "(unknown)" : p.name, .value = std::move(value)});
  }
  return section;
}

}  // namespace

void Overlay::initialize() {
  if (initialized_) {
    return;
  }
  initialized_ = true;
  reader_ = game::StateReader::attach();
  // Read once immediately, so state is observable even before the first
  // Present (and independently of the overlay).
  update(0.0F, 0.0F);
}

auto Overlay::update(float screen_width, float screen_height) -> const render::OverlayModel& {
  if (!initialized_) {
    initialize();
  }
  if (!reader_ || !reader_->ready()) {
    reader_ = game::StateReader::attach();  // retry until the target is up
  }

  const std::uint64_t now = now_ms();
  const bool size_changed = screen_width != last_width_ || screen_height != last_height_;
  if (!size_changed && now != 0 && now - last_update_ms_ < 33) {
    return model_;  // ~30 Hz
  }
  last_update_ms_ = now;
  last_width_ = screen_width;
  last_height_ = screen_height;

  model_ = render::OverlayModel{};
  model_.hud.title = "cs2-internal";
  if (!reader_ || !reader_->ready()) {
    model_.hud.title += "  [waiting for client.dll]";
    return model_;
  }

  const game::Snapshot snapshot = reader_->snapshot();
  if (!snapshot.valid) {
    model_.hud.title += "  [no local player]";
    return model_;
  }
  model_.in_game = true;  // a local player exists: the menu may take input
  if (local_pawn_sink_ != nullptr) {
    local_pawn_sink_(local_pawn_context_, snapshot.local_pawn);
  }
  // The ragebot solve (docs/ragebot.md): runs on this (render) thread with the
  // full snapshot and stashes the aim for the per-tick silent write.
  if (snapshot_sink_ != nullptr) {
    snapshot_sink_(snapshot_context_, snapshot);
  }

  model_.hud.sections.push_back(local_section(snapshot));
  model_.hud.sections.push_back(players_section(snapshot));
  if (rage_status_provider_ != nullptr) {
    char status[128] = {};
    if (rage_status_provider_(rage_status_context_, status, sizeof(status))) {
      render::HudSection section;
      section.title = "rage";
      section.lines.push_back(render::HudLine{"state", status});
      model_.hud.sections.push_back(std::move(section));
    }
  }

  // ESP (screen-space draw list).
  const game::Matrix4x4 view = reader_->view_matrix(0);
  EspStats esp_stats;
  esp_.build(snapshot, view, screen_width, screen_height, model_.draw, &esp_stats);

  // The real studio hitboxes of each live enemy (and of the ragebot's current
  // target) as screen-space capsules (`docs/re/aim.md`). Opt-in from the menu.
  if (esp_config().hitboxes && hitbox_provider_ != nullptr && snapshot.local.has_value()) {
    const int rage_target =
        rage_target_provider_ != nullptr ? rage_target_provider_(rage_target_context_) : -1;
    for (const game::PlayerSnapshot& player : snapshot.players) {
      if (!player.alive || player.is_local || player.pawn == 0) {
        continue;
      }
      // Only real combatants on the other side (a team-0/unassigned identity
      // sitting on the camera would smear arcs across the whole screen).
      if (player.team != 2 && player.team != 3) {
        continue;
      }
      if (esp_config().only_enemies && player.team == snapshot.local->team) {
        continue;
      }
      const bool active = player.index == rage_target;
      game::HitboxCapsule capsules[k_max_hitboxes] = {};
      const int count = hitbox_provider_(hitbox_context_, player.pawn, capsules, k_max_hitboxes);
      for (int i = 0; i < count; ++i) {
        draw_capsule(model_.draw, view, screen_width, screen_height, snapshot.local->eye_origin,
                     capsules[i], hitbox_color(capsules[i].group, active));
      }
    }
  }

  // The bhop prediction's current position, drawn as a box — a live demonstration
  // that the engine simulation is running (docs/re/prediction.md).
  if (predicted_origin_provider_ != nullptr) {
    float predicted[3] = {0.0F, 0.0F, 0.0F};
    if (predicted_origin_provider_(predicted_origin_context_, predicted)) {
      const game::Vec3 centre{.x = predicted[0], .y = predicted[1], .z = predicted[2] + 36.0F};
      const auto screen = view.project(centre, screen_width, screen_height);
      if (screen.has_value()) {
        constexpr render::Color k_predicted{.r = 0, .g = 255, .b = 255, .a = 235};
        model_.draw.outline(screen->x - 18.0F, screen->y - 38.0F, screen->x + 18.0F,
                            screen->y + 38.0F, k_predicted, 2.0F);
        model_.draw.text(screen->x, screen->y - 52.0F, k_predicted, "PRED", true, true);
      }
    }
  }

  // Periodic compact state dump, so the data is verifiable without the overlay.
  if (now == 0 || now - last_log_ms_ >= 2000) {
    last_log_ms_ = now;
    std::string summary = "[state] local=";
    if (snapshot.local.has_value()) {
      const game::PlayerSnapshot& p = *snapshot.local;
      summary += p.name.empty() ? "(unknown)" : p.name;
      summary += " team=" + team_name(p.team);
      summary += " hp=" + format_long(p.health) + "/" + format_long(p.max_health);
      summary += " armor=" + format_long(p.armor);
      summary += " weapon=" + (p.weapon.empty() ? std::string("?") : p.weapon);
      summary += " ammo=" + format_long(p.clip) + "/" + format_long(p.reserve);
      summary += " pos=" + format_vec3(p.origin);
      summary += " eye=" + format_vec3(p.eye_origin);
      summary += " vel=" + format_vec3(p.velocity);
      summary += " ang=" + format_angle(p.eye_angles);
    } else {
      summary += "(none)";
    }
    summary += " players=" + format_long(static_cast<long long>(snapshot.players.size()));
    summary += " wh=" + format_long(static_cast<long long>(screen_width)) + "x" +
               format_long(static_cast<long long>(screen_height));
    summary += " esp=" + format_long(static_cast<long long>(model_.draw.commands.size()));
    summary += " drawn=" + format_long(esp_stats.drawn);
    game::append_log(summary);
  }
  return model_;
}

}  // namespace cs2::features
