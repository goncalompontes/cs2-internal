#pragma once

#include <cstdint>

#include "cs2/game/matrix.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/render/draw_list.hpp"

namespace cs2::features {

// How the real studio hitboxes are drawn (`docs/re/aim.md`).
enum class HitboxMode : std::uint8_t {
  outline = 0,    // the capsule silhouette outline
  solid = 1,      // the silhouette filled
  wireframe = 2,  // the capsule mesh cage (rim/cap rings + longitudinals)
};

// Which ESP elements to draw.
struct EspConfig {
  bool box = true;
  bool health_bar = true;
  bool ammo_bar = true;
  bool name = true;
  bool weapon = true;
  bool flags = true;
  bool only_enemies = true;  // skip the local player
  bool show_dead = false;
  // Draw the real studio hitbox capsules (`docs/re/aim.md`). Enabled from the
  // menu's ESP tab; the mode and colour are menu-owned (`wire::Settings`).
  bool hitboxes = false;
  HitboxMode hitbox_mode = HitboxMode::outline;
  // false = the automatic per-hitgroup scheme; true = `hitbox_color`.
  bool hitbox_custom_color = false;
  std::uint8_t hitbox_color[4] = {90, 255, 130, 220};
};

// Per-build counters, for diagnostics/logging (never used for rendering).
struct EspStats {
  int players = 0;
  int skipped_local = 0;
  int skipped_dead = 0;
  int skipped_box = 0;    // a projected corner was behind the camera
  int skipped_small = 0;  // projected box was degenerate
  int drawn = 0;
};

// Builds the screen-space ESP draw list from a snapshot + the world-to-screen
// matrix. All text uses the embedded pixel font.
class Esp {
 public:
  Esp() = default;
  explicit Esp(EspConfig config) : config_(config) {}

  void build(const game::Snapshot& snapshot, const game::Matrix4x4& view, float width,
             float height, render::DrawList& out, EspStats* stats = nullptr) const;

  [[nodiscard]] const EspConfig& config() const noexcept { return config_; }
  EspConfig& config() noexcept { return config_; }

 private:
  EspConfig config_{};
};

}  // namespace cs2::features
