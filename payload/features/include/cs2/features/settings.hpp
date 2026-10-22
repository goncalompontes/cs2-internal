#pragma once

// The overlay settings block (`wire::Settings`, ADR-0021) is the canonical,
// menu-editable configuration. The render/ESP path consumes the derived
// `Config`. These helpers bridge the two so the in-process menu (workstream A)
// and the shared-memory publisher stay in sync.

#include <cstdint>

#include "cs2/features/esp.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::features {

// `Config` -> the settings block.
//
// **Every** field of the block is written: the ESP elements, the movement
// features, the ragebot's tuning, the visuals and the menu's own state. The
// mapping is one-to-one, so the block and the config carry the same
// information.
[[nodiscard]] auto to_wire_settings(const Config& config) -> cs2::overlay::wire::Settings;

// The settings block -> `Config`.
//
// Every field of the block is read, and every numeric field is clamped into
// its legal range (the enum-like fields fall back to their documented
// default): the shared segment is written by another process and may hold a
// stale, truncated or corrupt value, and a slider must never be pushed out of
// the range it was drawn for.
[[nodiscard]] auto from_wire_settings(const cs2::overlay::wire::Settings& settings) -> Config;

// The menu's defaults: `Config`'s defaults as a settings block (all ESP
// elements on, the debug HUD on, the movement/rage features off).
[[nodiscard]] auto default_wire_settings() -> cs2::overlay::wire::Settings;

}  // namespace cs2::features
