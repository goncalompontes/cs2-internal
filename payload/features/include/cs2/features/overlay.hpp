#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cs2/features/esp.hpp"
#include "cs2/game/state_reader.hpp"
#include "cs2/render/overlay_model.hpp"

namespace cs2::features {

// Produces the per-frame overlay model: the debug HUD window plus the ESP draw
// list. State is refreshed at ~30 Hz (throttled), and the ESP is rebuilt when
// the state or the screen size changes.
class Overlay {
 public:
  Overlay() = default;
  explicit Overlay(Config esp_config) : esp_(esp_config) {}

  // Resolve the target state reader. Idempotent; retries while the target is
  // not ready. Safe to call off the loader lock (from the install thread).
  void initialize();

  // Refresh (throttled) and return the current model.
  const render::OverlayModel& update(float screen_width, float screen_height);

  // Whether a level is up (the state machine). When false the ESP and the
  // HUD are not built; the menu is drawn by the present hook independently.
  void set_game_active(bool active) noexcept {
    game_active_ = active;
  }

  [[nodiscard]] bool ready() const noexcept {
    return reader_.has_value() && reader_->ready();
  }
  [[nodiscard]] const render::OverlayModel& model() const noexcept {
    return model_;
  }

  // ESP toggles, applied from the menu's shared settings (ADR-0021).
  [[nodiscard]] const Config& esp_config() const noexcept {
    return esp_.config();
  }
  void set_esp_config(const Config& config) noexcept {
    esp_.config() = config;
  }

  // Whether the debug HUD window is drawn. Set by the in-process menu's
  // "Debug HUD window" toggle; the layer gates it from its own settings.
  void set_show_hud(bool show) noexcept {
    show_hud_ = show;
  }
  [[nodiscard]] bool show_hud() const noexcept {
    return show_hud_;
  }

  // Forward the local pawn from each state refresh. The control thread's movement
  // features (strafe/bhop prediction) need it fresh even when no scenario is
  // polling `state` (`Input::note_local_pawn`).
  using LocalPawnSink = void (*)(void* context, std::uintptr_t pawn);
  void set_local_pawn_sink(LocalPawnSink sink, void* context) noexcept {
    local_pawn_sink_ = sink;
    local_pawn_context_ = context;
  }

  // Forward the full snapshot from each state refresh. The ragebot solve runs
  // here (the render thread has the complete snapshot: the local player and every
  // enemy) and stashes the aim for the per-tick `Input::apply()`.
  using SnapshotSink = void (*)(void* context, const game::Snapshot& snapshot);
  void set_snapshot_sink(SnapshotSink sink, void* context) noexcept {
    snapshot_sink_ = sink;
    snapshot_context_ = context;
  }

  // A provider for a pawn's **real studio hitboxes** (world capsules), so the
  // overlay can draw them. Returns the number written (<= `max`).
  //
  // The ESP's head box and its skeleton need the same capsules, so the
  // provider is handed to the `Esp` as well: the model is then read by one
  // reader, not twice (the ESP's own fallback re-reads the model the
  // provider has just read — several hundred scattered reads per pawn).
  using HitboxProvider = int (*)(void* context, std::uintptr_t pawn, game::HitboxCapsule* out,
                                 int max);
  void set_hitbox_provider(HitboxProvider provider, void* context) noexcept {
    hitbox_provider_ = provider;
    hitbox_context_ = context;
    EspProviders providers = esp_.providers();
    providers.hitboxes = provider;
    providers.hitboxes_context = context;
    esp_.set_providers(providers);
  }

  // The entity index the ragebot is currently aiming at (-1 = none); its
  // hitboxes are drawn brighter. `rage_target_entity`.
  using RageTargetProvider = int (*)(void* context);
  void set_rage_target_provider(RageTargetProvider provider, void* context) noexcept {
    rage_target_provider_ = provider;
    rage_target_context_ = context;
  }

  // The **debug view**: the payload's own one-line answers, drawn as text in the
  // top-left corner — a shadowed/outlined column of lines, **not** a window (a
  // window is what the menu is for). The provider writes up to `size` bytes of
  // `\n`-separated lines and returns how many it wrote; returning 0 (or having no
  // provider) hides the column. The control channel's `debug` command turns it on.
  using DebugLinesProvider = std::size_t (*)(void* context, char* out, std::size_t size);
  void set_debug_lines_provider(DebugLinesProvider provider, void* context) noexcept {
    debug_lines_provider_ = provider;
    debug_lines_context_ = context;
  }

private:
  std::optional<game::StateReader> reader_;
  render::OverlayModel model_;
  // The state machine's answer for the last frame (the ESP is not built when
  // there is no level).
  bool game_active_ = true;
  Esp esp_{};
  std::uint64_t last_update_ms_ = 0;
  std::uint64_t last_log_ms_ = 0;
  float last_width_ = 0.0F;
  float last_height_ = 0.0F;
  bool initialized_ = false;
  bool show_hud_ = true;
  LocalPawnSink local_pawn_sink_ = nullptr;
  void* local_pawn_context_ = nullptr;
  SnapshotSink snapshot_sink_ = nullptr;
  void* snapshot_context_ = nullptr;
  HitboxProvider hitbox_provider_ = nullptr;
  void* hitbox_context_ = nullptr;
  RageTargetProvider rage_target_provider_ = nullptr;
  void* rage_target_context_ = nullptr;
  DebugLinesProvider debug_lines_provider_ = nullptr;
  void* debug_lines_context_ = nullptr;
  // Append the provider's lines to the draw list, top-left, outlined.
  void append_debug_lines(render::DrawList& out);
};

}  // namespace cs2::features
