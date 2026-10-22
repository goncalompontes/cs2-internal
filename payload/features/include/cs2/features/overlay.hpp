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
  explicit Overlay(EspConfig esp_config) : esp_(esp_config) {}

  // Resolve the target state reader. Idempotent; retries while the target is
  // not ready. Safe to call off the loader lock (from the install thread).
  void initialize();

  // Refresh (throttled) and return the current model.
  const render::OverlayModel& update(float screen_width, float screen_height);

  [[nodiscard]] bool ready() const noexcept {
    return reader_.has_value() && reader_->ready();
  }
  [[nodiscard]] const render::OverlayModel& model() const noexcept { return model_; }

  // ESP toggles, applied from the menu's shared settings (ADR-0021).
  [[nodiscard]] const EspConfig& esp_config() const noexcept { return esp_.config(); }
  void set_esp_config(const EspConfig& config) noexcept { esp_.config() = config; }

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

  // A provider for the ragebot's one-line status, added to the HUD when it
  // returns true (so the operator can see the key/aim state while testing).
  using RageStatusProvider = bool (*)(void* context, char* out, std::size_t size);
  void set_rage_status_provider(RageStatusProvider provider, void* context) noexcept {
    rage_status_provider_ = provider;
    rage_status_context_ = context;
  }

  // A provider for a pawn's **real studio hitboxes** (world capsules), so the
  // overlay can draw them. Returns the number written (<= `max`).
  using HitboxProvider = int (*)(void* context, std::uintptr_t pawn, game::HitboxCapsule* out,
                                 int max);
  void set_hitbox_provider(HitboxProvider provider, void* context) noexcept {
    hitbox_provider_ = provider;
    hitbox_context_ = context;
  }

  // The entity index the ragebot is currently aiming at (-1 = none); its
  // hitboxes are drawn brighter. `rage_target_entity`.
  using RageTargetProvider = int (*)(void* context);
  void set_rage_target_provider(RageTargetProvider provider, void* context) noexcept {
    rage_target_provider_ = provider;
    rage_target_context_ = context;
  }

  // A provider for the bhop prediction's current position; when it returns one,
  // the overlay draws a box there so the engine simulation is visible.
  using PredictedOriginProvider = bool (*)(void* context, float out[3]);
  void set_predicted_origin_provider(PredictedOriginProvider provider, void* context) noexcept {
    predicted_origin_provider_ = provider;
    predicted_origin_context_ = context;
  }

 private:
  std::optional<game::StateReader> reader_;
  render::OverlayModel model_;
  game::Snapshot snapshot_;
  std::uint64_t last_hud_ms_ = 0;
  Esp esp_{};
  std::uint64_t last_update_ms_ = 0;
  std::uint64_t last_log_ms_ = 0;
  float last_width_ = 0.0F;
  float last_height_ = 0.0F;
  bool initialized_ = false;
  LocalPawnSink local_pawn_sink_ = nullptr;
  void* local_pawn_context_ = nullptr;
  SnapshotSink snapshot_sink_ = nullptr;
  void* snapshot_context_ = nullptr;
  RageStatusProvider rage_status_provider_ = nullptr;
  void* rage_status_context_ = nullptr;
  HitboxProvider hitbox_provider_ = nullptr;
  void* hitbox_context_ = nullptr;
  RageTargetProvider rage_target_provider_ = nullptr;
  void* rage_target_context_ = nullptr;
  PredictedOriginProvider predicted_origin_provider_ = nullptr;
  void* predicted_origin_context_ = nullptr;
};

}  // namespace cs2::features
