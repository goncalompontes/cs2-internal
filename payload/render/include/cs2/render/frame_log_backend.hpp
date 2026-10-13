#pragma once

#include <cstdint>

#include "cs2/render/ui_backend.hpp"

namespace cs2::render {

// A minimal backend that just counts frames and logs periodically. Stands in
// for the real overlay until RmlUi/ImGui lands.
class FrameLogBackend final : public UiBackend {
 public:
  [[nodiscard]] bool initialize(const RenderContext& context) override;
  void render(const RenderContext& context, const HudModel& model) override;
  void shutdown() override;

  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

 private:
  std::uint64_t frames_ = 0;
};

}  // namespace cs2::render
