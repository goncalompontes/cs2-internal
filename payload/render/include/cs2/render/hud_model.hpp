#pragma once

// A UI-agnostic overlay model (ADR-0007): the game/feature layer builds a
// `HudModel`, and any `UiBackend` renders it. Keeps `payload/render` free of
// game types.

#include <string>
#include <vector>

namespace cs2::render {

struct HudLine {
  std::string label;
  std::string value;
};

struct HudSection {
  std::string title;
  std::vector<HudLine> lines;
};

struct HudModel {
  std::string title;
  std::vector<HudSection> sections;

  [[nodiscard]] static const HudModel& empty() noexcept {
    static const HudModel model;
    return model;
  }
};

}  // namespace cs2::render
