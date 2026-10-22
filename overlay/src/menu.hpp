#pragma once

// In-game menu: a Dear ImGui window with tabs + toggles, opened with Insert (or
// Delete, for keyboards where Insert needs Fn), that edits the shared
// `wire::Settings` the payload's ESP honours. See ADR-0021.
//
// Input is captured on Windows by subclassing the game window's `WndProc` and
// feeding Dear ImGui's thread-safe input queue. The Win32 *platform backend* is
// deliberately not used: its window/mouse calls from the render thread are the
// ADR-0015 deadlock class, and the overlay otherwise stays display-only.

#include "cs2/overlay/wire.hpp"

namespace cs2::overlay::menu {

// Call between `ImGui::NewFrame()` and `ImGui::Render()`. Polls the toggle key,
// draws the menu, and updates `settings` (ESP toggles / HUD visibility).
// The menu is available whenever the game window is foreground, independently
// of payload player discovery. `in_game` is retained for caller compatibility.
void frame(wire::Settings& settings, bool in_game) noexcept;

// Drop the input hook and restore the cursor (called when ImGui shuts down).
void shutdown() noexcept;

}  // namespace cs2::overlay::menu
