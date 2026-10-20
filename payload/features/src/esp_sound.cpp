#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

#include "cs2/features/esp.hpp"
#include "cs2/features/esp_geometry.hpp"
#include "cs2/game/math.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/render/draw_list.hpp"

#include "esp_detail.hpp"

namespace cs2::features::esp_detail {
namespace {

// How long a sound marker lives, in milliseconds.
constexpr std::uint64_t k_sound_lifetime_ms = 1000;

// The number of segments a sound marker is drawn with.
constexpr int k_sound_segments = 12;

// The marker's radius when the event carries no radius (a footstep).
constexpr float k_sound_default_radius = 24.0F;

auto find_player(const game::Snapshot& snapshot, int index) noexcept
    -> const game::PlayerSnapshot* {
  if (index < 0) {
    return nullptr;
  }
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.index == index) {
      return &player;
    }
  }
  return nullptr;
}

}  // namespace

void build_sounds(Frame& frame) {
  const Config& config = frame.config;
  if (!config.esp.sound) {
    return;
  }
  const EspProviders& providers = frame.providers;
  if (providers.sounds == nullptr) {
    return;  // no event source wired
  }

  // Ingest the events the provider has not reported yet. The provider
  // replays the game's recent events every frame, so the sequence number is
  // what tells the ESP which ones are new.
  SoundEvent events[32];
  const int count =
      providers.sounds(providers.sounds_context, events, static_cast<int>(std::size(events)));
  for (int i = 0; i < count; ++i) {
    if (events[i].sequence != 0 && events[i].sequence <= frame.sounds.last_sequence) {
      continue;
    }
    frame.sounds.push(events[i], frame.now_ms);
  }

  const render::Color base = to_render(config.esp.color_sound);
  for (std::size_t i = 0; i < frame.sounds.count; ++i) {
    const SoundCache::Entry& entry = frame.sounds.entries[i];
    const std::uint64_t age = frame.now_ms - entry.time_ms;
    if (age >= k_sound_lifetime_ms) {
      continue;  // the marker has faded out
    }
    const game::PlayerSnapshot* player = find_player(frame.snapshot, entry.event.player_index);
    if (player == nullptr) {
      continue;
    }
    const auto screen = frame.view.project(player->origin, frame.width, frame.height);
    if (!screen.has_value()) {
      continue;
    }
    // The marker's radius: the sound's radius, projected at the player's
    // depth. A footstep carries no radius, so it gets a fixed one.
    const float depth = clip_depth(frame.view, player->origin);
    const float scale = pixels_per_unit(frame.view, frame.height, depth);
    const float radius_world =
        entry.event.radius > 0.0F ? entry.event.radius : k_sound_default_radius;
    const float radius_px = std::clamp(radius_world * scale, 3.0F, 400.0F);

    // The marker fades out over its lifetime.
    const float fade = 1.0F - static_cast<float>(age) / static_cast<float>(k_sound_lifetime_ms);
    const render::Color color{.r = base.r,
                              .g = base.g,
                              .b = base.b,
                              .a = static_cast<std::uint8_t>(static_cast<float>(base.a) * fade)};
    frame.out.poly(esp_geometry::circle_points(screen->x, screen->y, radius_px, k_sound_segments),
                   color);
    if (frame.stats != nullptr) {
      ++frame.stats->sound_markers;
    }
  }
}

}  // namespace cs2::features::esp_detail
