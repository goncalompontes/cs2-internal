#pragma once

#include "cs2/render/draw_list.hpp"
#include "cs2/render/hud_model.hpp"

namespace cs2::render {

// Everything a `UiBackend` draws in one frame: the debug HUD window plus the
// screen-space ESP draw list.
struct OverlayModel {
  HudModel hud;
  DrawList draw;
};

}  // namespace cs2::render
