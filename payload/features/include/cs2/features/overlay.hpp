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

 private:
  std::optional<game::StateReader> reader_;
  render::OverlayModel model_;
  Esp esp_{};
  std::uint64_t last_update_ms_ = 0;
  std::uint64_t last_log_ms_ = 0;
  float last_width_ = 0.0F;
  float last_height_ = 0.0F;
  bool initialized_ = false;
};

}  // namespace cs2::features
