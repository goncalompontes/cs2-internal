#include "cs2/features/overlay.hpp"

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

  model_.hud.sections.push_back(local_section(snapshot));
  model_.hud.sections.push_back(players_section(snapshot));

  // ESP (screen-space draw list).
  const game::Matrix4x4 view = reader_->view_matrix(0);
  EspStats esp_stats;
  esp_.build(snapshot, view, screen_width, screen_height, model_.draw, &esp_stats);

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
