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

std::string format_vec3(const game::Vec3& value) {
  char buffer[80];
  std::snprintf(buffer, sizeof(buffer), "%.1f %.1f %.1f", static_cast<double>(value.x),
                static_cast<double>(value.y), static_cast<double>(value.z));
  return buffer;
}

std::string format_angle(const game::Angle& value) {
  char buffer[80];
  std::snprintf(buffer, sizeof(buffer), "pitch %.1f yaw %.1f", static_cast<double>(value.pitch),
                static_cast<double>(value.yaw));
  return buffer;
}

std::string format_long(long long value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld", value);
  return buffer;
}

std::string format_float(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

std::string team_name(int team) {
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

std::uint64_t now_ms() noexcept {
#if defined(_WIN32)
  return GetTickCount64();
#else
  return 0;
#endif
}

render::HudSection local_section(const game::Snapshot& snapshot) {
  render::HudSection section;
  section.title = "Local player";
  if (!snapshot.local.has_value()) {
    section.lines.push_back({"status", "not found"});
    return section;
  }
  const game::PlayerSnapshot& p = *snapshot.local;
  section.lines.push_back({"name", p.name.empty() ? "(unknown)" : p.name});
  section.lines.push_back({"team", team_name(p.team)});
  section.lines.push_back({"health / armor",
                           format_long(p.health) + " / " + format_long(p.armor)});
  section.lines.push_back({"alive", p.alive ? "yes" : "no"});
  section.lines.push_back({"weapon", p.weapon.empty() ? "(unknown)" : p.weapon});
  section.lines.push_back(
      {"ammo (clip / reserve)", format_long(p.clip) + " / " + format_long(p.reserve)});
  section.lines.push_back({"origin", format_vec3(p.origin)});
  section.lines.push_back({"eye origin", format_vec3(p.eye_origin)});
  section.lines.push_back({"velocity", format_vec3(p.velocity)});
  section.lines.push_back({"eye angles", format_angle(p.eye_angles)});
  section.lines.push_back({"entity index", format_long(p.index)});
  return section;
}

render::HudSection players_section(const game::Snapshot& snapshot) {
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
    section.lines.push_back({p.name.empty() ? "(unknown)" : p.name, std::move(value)});
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

const render::OverlayModel& Overlay::update(float screen_width, float screen_height) {
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
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  // Native Windows builds are untested; advertise that on the HUD itself so it
  // can never be mistaken for a validated build (see docs/handoff.md).
  model_.hud.title += "  [NATIVE WINDOWS - UNTESTED]";
#endif
  if (!reader_ || !reader_->ready()) {
    model_.hud.title += "  [waiting for client.dll]";
    return model_;
  }

  const game::Snapshot snapshot = reader_->snapshot();
  if (!snapshot.valid) {
    model_.hud.title += "  [no local player]";
    return model_;
  }

  model_.hud.sections.push_back(local_section(snapshot));
  model_.hud.sections.push_back(players_section(snapshot));

  // ESP (screen-space draw list).
  const game::Matrix4x4 view = reader_->view_matrix(0);
  EspStats esp_stats;
  esp_.build(snapshot, view, screen_width, screen_height, model_.draw, &esp_stats);

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
