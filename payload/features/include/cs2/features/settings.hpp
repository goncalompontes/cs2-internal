#pragma once

// The overlay settings block (`wire::Settings`, ADR-0021) is the canonical,
// menu-editable configuration. The render/ESP path consumes the derived
// `EspConfig`. These helpers bridge the two so the in-process menu (workstream A)
// and the shared-memory publisher stay in sync.

#include "cs2/features/esp.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::features {

// `EspConfig` -> the settings block. Fields the ESP does not carry (movement,
// rage key, `show_hud`, `menu_open`) are left at their struct defaults.
[[nodiscard]] auto to_wire_settings(const EspConfig& config) -> cs2::overlay::wire::Settings;

// The settings block -> `EspConfig` (clamping the enum-like fields).
[[nodiscard]] auto from_wire_settings(const cs2::overlay::wire::Settings& settings) -> EspConfig;

// The menu's defaults: `EspConfig`'s defaults as a settings block (all ESP
// elements on, the debug HUD on, the movement/rage features off).
[[nodiscard]] auto default_wire_settings() -> cs2::overlay::wire::Settings;

}  // namespace cs2::features
