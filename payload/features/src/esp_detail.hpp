#pragma once

// The ESP's internal per-frame context.
//
// The element builders — the players, the world markers, the radar and the
// sounds — share one frame: the snapshot, the projection, the draw list and
// the per-frame caches. This header is private to the features module.
//
// It stays free of `<windows.h>` so the host test can compile the chain
// builder against it; the clock lives in `esp.cpp`.

#include <cmath>
#include <cstdint>
#include <string>

#include "cs2/features/esp.hpp"
#include "cs2/features/esp_cache.hpp"
#include "cs2/features/esp_geometry.hpp"
#include "cs2/game/matrix.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/render/draw_list.hpp"

namespace cs2::features::esp_detail {

// The milliseconds since the system started (the payload's clock).
[[nodiscard]] auto now_ms() noexcept -> std::uint64_t;

// The view-space depth of a world point (the projection's clip w).
[[nodiscard]] inline auto clip_depth(const game::Matrix4x4& view, const game::Vec3& world) noexcept
    -> float {
  return world.x * view.m[12] + world.y * view.m[13] + world.z * view.m[14] + view.m[15];
}

// How many pixels one world unit covers at `depth`.
//
// The projection's `m[5]` is the vertical scale of the perspective divide
// (`1/tan(fov/2)`), so a world offset of one unit in the view's vertical axis
// covers `0.5 * height * m[5] / depth` pixels.
[[nodiscard]] inline auto pixels_per_unit(const game::Matrix4x4& view, float height,
                                          float depth) noexcept -> float {
  if (depth <= 1e-3F) {
    return 0.0F;
  }
  return 0.5F * height * view.m[5] / depth;
}

// The player's accent: the enemy colour or the team colour.
[[nodiscard]] inline auto accent_color(const Config& config, const game::Snapshot& snapshot,
                                       const game::PlayerSnapshot& player) noexcept
    -> render::Color {
  const int local_team = snapshot.local.has_value() ? snapshot.local->team : 0;
  const bool teammate = local_team != 0 && player.team == local_team;
  return teammate ? to_render(config.esp.color_team) : to_render(config.esp.color_enemy);
}

// A player's visibility state.
//
// Without a provider the state is `unknown`, and the visibility toggles
// treat it as "visible": the ESP never hides a player it cannot judge, and
// the glow never fires on a guess.
[[nodiscard]] inline auto visibility_for(const EspProviders& providers, int player_index) noexcept
    -> Visibility {
  if (providers.visibility == nullptr) {
    return Visibility::unknown;
  }
  return providers.visibility(providers.visibility_context, player_index);
}

// The distance readout, in metres. The snapshot's distance is in world units
// (Source 2's unit is an inch).
[[nodiscard]] inline auto distance_text(const game::PlayerSnapshot& player) -> std::string {
  const float metres = esp_geometry::units_to_metres(player.distance);
  return std::to_string(static_cast<int>(std::lround(metres))) + "m";
}

// What the element builders share for one frame.
struct Frame {
  const game::Snapshot& snapshot;
  const game::Matrix4x4& view;
  float width = 0.0F;
  float height = 0.0F;
  render::DrawList& out;
  const Config& config;
  // The merged providers: the `Esp`'s own ones, falling back to the
  // payload's registry.
  EspProviders providers;
  EspStats* stats = nullptr;
  std::uint64_t now_ms = 0;
  // The per-frame caches (owned by the `Esp`).
  HitboxCache& hitboxes;
  WorldCache& world;
  SoundCache& sounds;
};

// The element builders.
void build_players(Frame& frame);
void build_world(Frame& frame);
void build_radar(Frame& frame);
void build_sounds(Frame& frame);

// The pawn's studio hitbox capsules: the provider's, or the model reader's
// when no provider is wired.
[[nodiscard]] auto read_capsules(const EspProviders& providers, std::uintptr_t pawn,
                                 game::HitboxCapsule* out, int max) -> int;

// The pawn's cached studio hitbox capsules. `count` receives the number of
// capsules (0 when the model could not be read).
[[nodiscard]] auto capsules_for(Frame& frame, std::uintptr_t pawn, int& count)
    -> const game::HitboxCapsule*;

// Draw a player's skeleton from its studio hitbox capsules.
void draw_skeleton(render::DrawList& out, const game::Matrix4x4& view, float width, float height,
                   const game::HitboxCapsule* capsules, int count, const render::Color& color);

}  // namespace cs2::features::esp_detail
