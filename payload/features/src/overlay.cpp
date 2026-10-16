#include "cs2/features/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
// The wireframe mesh cage is coarser: segments per ring, cap subdivisions.
constexpr int k_wire_segments = 10;
constexpr int k_wire_cap_rings = 2;
constexpr float k_pi = 3.14159265358979323846F;

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

// One hitbox's colour: the menu's custom pick, or the automatic per-hitgroup
// scheme (the head stands out, the rest a dim body colour, brighter when the
// ragebot is aiming at this pawn).
auto hitbox_color(const EspConfig& config, int group, bool active) -> render::Color {
  if (config.hitbox_custom_color) {
    return render::Color{config.hitbox_color[0], config.hitbox_color[1],
                         config.hitbox_color[2], config.hitbox_color[3]};
  }
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

// The screen-space silhouette of a world capsule: a capsule is the convex hull of
// its two end spheres and projection preserves convexity, so the outline is the
// hull of the two projected end-sphere outlines — correct tangents and
// per-endpoint perspective (`docs/re/aim.md`). Empty when degenerate.
auto capsule_silhouette(const game::Matrix4x4& view, float width, float height,
                        const game::Vec3& eye, const game::HitboxCapsule& capsule)
    -> std::vector<game::Vec2> {
  if (capsule.radius <= 0.0F) {
    return {};
  }
  std::vector<game::Vec2> points;
  points.reserve(2 * static_cast<std::size_t>(k_capsule_segments));
  project_sphere_outline(view, width, height, eye, capsule.a, capsule.radius, points);
  project_sphere_outline(view, width, height, eye, capsule.b, capsule.radius, points);
  return convex_hull(std::move(points));
}

// A framing test: an entirely off-screen silhouette draws nothing, so nothing
// visible is ever discarded.
auto outline_on_screen(const std::vector<game::Vec2>& outline, float width,
                       float height) -> bool {
  if (outline.size() < 3) {
    return false;
  }
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
  return !(max_x < 0.0F || min_x > width || max_y < 0.0F || min_y > height);
}

// A zero-radius capsule is a segment: draw its axis.
void draw_capsule_axis(render::DrawList& out, const game::Matrix4x4& view, float width,
                       float height, const game::HitboxCapsule& capsule,
                       const render::Color& color) {
  const auto a = view.project(capsule.a, width, height);
  const auto b = view.project(capsule.b, width, height);
  if (a.has_value() && b.has_value()) {
    out.line(a->x, a->y, b->x, b->y, color, 1.0F);
  }
}

// OUTLINE: the silhouette as a closed polyline.
void draw_capsule_outline(render::DrawList& out, const game::Matrix4x4& view, float width,
                          float height, const game::Vec3& eye,
                          const game::HitboxCapsule& capsule, const render::Color& color) {
  if (capsule.radius <= 0.0F) {
    draw_capsule_axis(out, view, width, height, capsule, color);
    return;
  }
  const std::vector<game::Vec2> outline = capsule_silhouette(view, width, height, eye, capsule);
  if (!outline_on_screen(outline, width, height)) {
    return;
  }
  for (std::size_t i = 0; i < outline.size(); ++i) {
    const game::Vec2& a = outline[i];
    const game::Vec2& b = outline[(i + 1) % outline.size()];
    out.line(a.x, a.y, b.x, b.y, color, 1.0F);
  }
}

// SOLID: the silhouette as a filled convex polygon.
void draw_capsule_solid(render::DrawList& out, const game::Matrix4x4& view, float width,
                        float height, const game::Vec3& eye,
                        const game::HitboxCapsule& capsule, const render::Color& color) {
  if (capsule.radius <= 0.0F) {
    draw_capsule_axis(out, view, width, height, capsule, color);
    return;
  }
  const std::vector<game::Vec2> outline = capsule_silhouette(view, width, height, eye, capsule);
  if (!outline_on_screen(outline, width, height)) {
    return;
  }
  std::vector<std::pair<float, float>> points;
  points.reserve(outline.size());
  for (const game::Vec2& point : outline) {
    points.emplace_back(point.x, point.y);
  }
  out.poly(std::move(points), color);
}

// One sample of the capsule's mesh: position + outward normal.
struct CapsuleSample {
  game::Vec3 position{};
  game::Vec3 normal{};
};

// The capsule's mesh rings, bottom pole -> top pole. A capsule is the Minkowski
// sum of the segment `a`-`b` and a sphere of `radius`: a cylinder with two
// hemispherical caps. Mirrors `scripts/hitbox-view.py`.
void capsule_rings(const game::Vec3& a, const game::Vec3& b, float radius,
                   std::vector<std::vector<CapsuleSample>>& rings) {
  game::Vec3 axis = b - a;
  game::Vec3 end = b;
  if (game::length(axis) < 1e-4F) {
    end = a + game::Vec3{0.0F, 0.0F, 1.0F};  // degenerate: a unit-length capsule
    axis = end - a;
  }
  const game::Vec3 n = game::normalized(axis);
  const game::Vec3 reference = std::fabs(n.z) < 0.9F ? game::Vec3{0.0F, 0.0F, 1.0F}
                                                     : game::Vec3{1.0F, 0.0F, 0.0F};
  const game::Vec3 u = game::normalized(game::cross(n, reference));
  const game::Vec3 v = game::cross(n, u);
  const auto make_ring = [&](const game::Vec3& center, float ring_radius,
                             const game::Vec3& sphere) {
    std::vector<CapsuleSample> points;
    points.reserve(k_wire_segments);
    for (int i = 0; i < k_wire_segments; ++i) {
      const float theta =
          2.0F * k_pi * static_cast<float>(i) / static_cast<float>(k_wire_segments);
      const game::Vec3 position =
          center + (u * std::cos(theta) + v * std::sin(theta)) * ring_radius;
      points.push_back(CapsuleSample{position, game::normalized(position - sphere)});
    }
    return points;
  };
  rings.clear();
  rings.push_back(make_ring(a - n * radius, 0.0F, a));
  for (int k = 1; k <= k_wire_cap_rings; ++k) {
    const float phi =
        (k_pi * 0.5F) * static_cast<float>(k) / static_cast<float>(k_wire_cap_rings);
    rings.push_back(make_ring(a - n * (radius * std::cos(phi)), radius * std::sin(phi), a));
  }
  for (int k = k_wire_cap_rings; k >= 1; --k) {
    const float phi =
        (k_pi * 0.5F) * static_cast<float>(k) / static_cast<float>(k_wire_cap_rings);
    rings.push_back(make_ring(end + n * (radius * std::cos(phi)), radius * std::sin(phi), end));
  }
  rings.push_back(make_ring(end + n * radius, 0.0F, end));
}

// WIREFRAME: the mesh cage — every ring plus longitudinal lines, with
// back-facing edges dimmed (the same look as the Python renderer).
void draw_capsule_wire(render::DrawList& out, const game::Matrix4x4& view, float width,
                       float height, const game::Vec3& eye,
                       const game::HitboxCapsule& capsule, const render::Color& color) {
  if (capsule.radius <= 0.0F) {
    draw_capsule_axis(out, view, width, height, capsule, color);
    return;
  }
  std::vector<std::vector<CapsuleSample>> rings;
  capsule_rings(capsule.a, capsule.b, capsule.radius, rings);

  const auto emit = [&](const CapsuleSample& p, const CapsuleSample& q) {
    const auto pa = view.project(p.position, width, height);
    const auto pb = view.project(q.position, width, height);
    if (!pa.has_value() || !pb.has_value()) {
      return;
    }
    const float facing =
        game::dot(p.normal, p.position - eye) + game::dot(q.normal, q.position - eye);
    const float scale = facing > 0.0F ? 0.45F : 1.0F;  // back-facing edges dimmed
    const auto channel = [scale](std::uint8_t base) {
      return static_cast<std::uint8_t>(static_cast<float>(base) * scale);
    };
    out.line(pa->x, pa->y, pb->x, pb->y,
             render::Color{channel(color.r), channel(color.g), channel(color.b), color.a}, 1.0F);
  };

  for (const std::vector<CapsuleSample>& ring : rings) {
    for (std::size_t j = 0; j < ring.size(); ++j) {
      emit(ring[j], ring[(j + 1) % ring.size()]);
    }
  }
  for (std::size_t i = 0; i + 1 < rings.size(); ++i) {
    for (std::size_t j = 0; j < rings[i].size(); j += 2) {
      emit(rings[i][j], rings[i + 1][j]);
    }
  }
}

// One hitbox in the menu-selected mode (`docs/ragebot.md`).
void draw_hitbox(render::DrawList& out, const game::Matrix4x4& view, float width, float height,
                 const game::Vec3& eye, const game::HitboxCapsule& capsule,
                 const render::Color& color, HitboxMode mode) {
  switch (mode) {
    case HitboxMode::solid:
      draw_capsule_solid(out, view, width, height, eye, capsule, color);
      break;
    case HitboxMode::wireframe:
      draw_capsule_wire(out, view, width, height, eye, capsule, color);
      break;
    case HitboxMode::outline:
    default:
      draw_capsule_outline(out, view, width, height, eye, capsule, color);
      break;
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
      const EspConfig& config = esp_config();
      game::HitboxCapsule capsules[k_max_hitboxes] = {};
      const int count = hitbox_provider_(hitbox_context_, player.pawn, capsules, k_max_hitboxes);
      for (int i = 0; i < count; ++i) {
        draw_hitbox(model_.draw, view, screen_width, screen_height, snapshot.local->eye_origin,
                    capsules[i], hitbox_color(config, capsules[i].group, active),
                    config.hitbox_mode);
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
