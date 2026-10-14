#pragma once

// JSON serialization for the `state`/`wait_for` responses (ADR-0020). Pure
// (no Windows/game-process dependency) so it has a host regression test.

#include "cs2/control/json.hpp"
#include "cs2/game/snapshot.hpp"

namespace cs2::control {

[[nodiscard]] Json to_json(const game::Vec3& value);
[[nodiscard]] Json to_json(const game::Angle& value);
[[nodiscard]] Json to_json(const game::PlayerSnapshot& player);
[[nodiscard]] Json to_json(const game::Snapshot& snapshot);

}  // namespace cs2::control
