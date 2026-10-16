#include "cs2/features/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cs2/game/log.hpp"
#include "cs2/game/snapshot.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {
namespace {

// A player's hitbox set is 19 boxes; leave headroom for the visualiser buffer.
constexpr int k_max_hitboxes = 40;

// Samples around each capsule end cap; the pill outline is the hull of both.
constexpr int k_capsule_segments = 16;

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

// A screen-space convex hull (Andrew's monotone chain). Collinear points are
// dropped, so a degenerate set collapses below 3 vertices and draws nothing.
auto convex_hull(std::vector<game::Vec2> points) -> std::vector<game::Vec2> {
  if (points.size() < 3) {
    return points;
  }
  std::sort(points.begin(), points.end(), [](const game::Vec2& a, const game::Vec2& b) noexcept {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  });
  const auto turn = [](const game::Vec2& o, const game::Vec2& a, const game::Vec2& b) noexcept {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  std::vector<game::Vec2> hull(points.size() * 2);
  std::size_t k = 0;
  for (const game::Vec2& p : points) {
    while (k >= 2 && turn(hull[k - 2], hull[k - 1], p) <= 0.0F) {
      --k;
    }
    hull[k++] = p;
  }
  for (std::size_t i = points.size() - 1, t = k + 1; i-- > 0;) {
    while (k >= t && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0F) {
      --k;
    }
    hull[k++] = points[i];
  }
  hull.resize(k - 1);  // the last point repeats the first
  return hull;
}

// Append the projected outline of a sphere (`radius` at `center`) to `out`: the
// tangent circle on the sphere as seen from `eye`, sampled and projected. Points
// behind the eye are dropped. This is the true common-tangent silhouette, not a
// screen-space approximation (`docs/re/aim.md`).
void project_sphere_outline(const game::Matrix4x4& view, float width, float height,
                            const game::Vec3& eye, const game::Vec3& center, float radius,
                            std::vector<game::Vec2>& out) {
  const game::Vec3 to_center = center - eye;
  const float dist = game::length(to_center);
  if (radius <= 0.0F || dist <= 1e-3F || radius >= dist) {
    // Degenerate (`radius >= dist`: the eye is inside the sphere) — one point.
    if (const auto point = view.project(center, width, height)) {
      out.push_back(*point);
    }
    return;
  }
  const game::Vec3 axis = to_center * (1.0F / dist);
  const float sin_a = radius / dist;
  const float cos_a = std::sqrt((std::max)(0.0F, 1.0F - sin_a * sin_a));
  // The contact circle sits `radius*sin_a` toward the eye and has radius
  // `radius*cos_a` (Pythagoras on the tangent cone from the eye to the sphere).
  const game::Vec3 circle_center = center - axis * (radius * sin_a);
  const float circle_radius = radius * cos_a;
  const game::Vec3 reference = std::fabs(axis.z) < 0.9F ? game::Vec3{0.0F, 0.0F, 1.0F}
                                                        : game::Vec3{1.0F, 0.0F, 0.0F};
  const game::Vec3 u = game::normalized(game::cross(axis, reference));
  const game::Vec3 v = game::cross(axis, u);
  constexpr float k_tau = 6.28318530717958648F;
  for (int i = 0; i < k_capsule_segments; ++i) {
    const float theta = k_tau * static_cast<float>(i) / static_cast<float>(k_capsule_segments);
    const game::Vec3 point = circle_center + u * (circle_radius * std::cos(theta)) +
                             v * (circle_radius * std::sin(theta));
    if (const auto screen = view.project(point, width, height)) {
      out.push_back(*screen);
    }
  }
}

// Draw a world capsule as its real silhouette: a capsule is the convex hull of
// its two end spheres, and projection preserves convexity, so the outline is the
// hull of the two projected sphere outlines — an honest pill, with the correct
// tangents and per-endpoint perspective (`docs/re/aim.md`).
void draw_capsule(render::DrawList& out, const game::Matrix4x4& view, float width, float height,
                  const game::Vec3& eye, const game::HitboxCapsule& capsule,
                  const render::Color& color) {
  if (capsule.radius <= 0.0F) {
    // A zero-radius capsule is a segment: draw its axis.
    const auto a = view.project(capsule.a, width, height);
    const auto b = view.project(capsule.b, width, height);
    if (a.has_value() && b.has_value()) {
      out.line(a->x, a->y, b->x, b->y, color, 1.0F);
    }
    return;
  }

  std::vector<game::Vec2> points;
  points.reserve(2 * static_cast<std::size_t>(k_capsule_segments));
  project_sphere_outline(view, width, height, eye, capsule.a, capsule.radius, points);
  project_sphere_outline(view, width, height, eye, capsule.b, capsule.radius, points);
  const std::vector<game::Vec2> outline = convex_hull(std::move(points));
  if (outline.size() < 3) {
    return;  // behind the eye / degenerate
  }

  // Cull only the fully off-screen case — a framing test, so nothing visible is
  // discarded (a capsule near the eye draws its own, large but true, silhouette).
  float min_x = outline[0].x;
  float max_x = outline[0].x;
  float min_y = outline[0].y;
  float max_y = outline[0].y;
  for (const game::Vec2& point : outline) {
    min_x = (std::min)(min_x, point.x);
    max_x = (std::max)(max_x, point.x);
    min_y = (std::min)(min_y, point.y);
    max_y = (std::max)(max_y, point.y);
  }
  if (max_x < 0.0F || min_x > width || max_y < 0.0F || min_y > height) {
    return;
  }

  for (std::size_t i = 0; i < outline.size(); ++i) {
    const game::Vec2& a = outline[i];
    const game::Vec2& b = outline[(i + 1) % outline.size()];
    out.line(a.x, a.y, b.x, b.y, color, 1.0F);
  }
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
