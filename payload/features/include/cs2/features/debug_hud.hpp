#pragma once

#include <cstdint>
#include <optional>

#include "cs2/game/state_reader.hpp"
#include "cs2/render/hud_model.hpp"

namespace cs2::features {

// Builds the debug HUD model from a live `game::Snapshot`, refreshed at ~10 Hz.
class DebugHud {
 public:
  // Resolve the target state reader. Idempotent; retries while the target is
  // not ready. Safe to call off the loader lock (from the install thread).
  void initialize();

  // Refresh (throttled) and return the current model.
  const render::HudModel& update();

  [[nodiscard]] const render::HudModel& model() const noexcept { return model_; }
  [[nodiscard]] bool ready() const noexcept {
    return reader_.has_value() && reader_->ready();
  }

 private:
  std::optional<game::StateReader> reader_;
  render::HudModel model_;
  std::uint64_t last_update_ms_ = 0;
  std::uint64_t last_log_ms_ = 0;
  bool initialized_ = false;
};

}  // namespace cs2::features
