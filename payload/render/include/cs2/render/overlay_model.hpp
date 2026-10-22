#pragma once

#include "cs2/render/draw_list.hpp"
#include "cs2/render/hud_model.hpp"

namespace cs2::render {

// Everything a `UiBackend` draws in one frame: the debug HUD window plus the
// screen-space ESP draw list.
struct OverlayModel {
  HudModel hud;
  DrawList draw;
  // True when a local player is present. The layer uses it to decide whether the
  // menu may take input (never during engine boot).
  bool in_game = false;
};

}  // namespace cs2::render
