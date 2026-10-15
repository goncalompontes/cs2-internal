#pragma once

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
  Esp esp_{};
  std::uint64_t last_update_ms_ = 0;
  std::uint64_t last_log_ms_ = 0;
  float last_width_ = 0.0F;
  float last_height_ = 0.0F;
  bool initialized_ = false;
  LocalPawnSink local_pawn_sink_ = nullptr;
  void* local_pawn_context_ = nullptr;
  PredictedOriginProvider predicted_origin_provider_ = nullptr;
  void* predicted_origin_context_ = nullptr;
};

}  // namespace cs2::features
