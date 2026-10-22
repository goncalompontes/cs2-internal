#pragma once

// Game bridge: build a solver input from the current game snapshot
// (docs/ragebot.md, docs/re/aim.md). Reads the pawn's **real studio hitbox set**
// (bone-local capsule endpoints transformed by the bone) and falls back to the
// collision-AABB proxy only when the model cannot be resolved.

#include <cstdint>
#include <optional>
#include <vector>

#include "cs2/aim/solver.hpp"
#include "cs2/game/snapshot.hpp"

namespace cs2::aim {

// The local player's aim input from a snapshot. `target_index` selects an enemy
// by entity index; when negative the nearest live enemy is used. Returns
// std::nullopt when there is no usable local player / target.
[[nodiscard]] auto build_input(const game::Snapshot& snapshot, const WeaponProfile& weapon,
                               int target_index = -1, bool weapon_from_game = true,
                               bool include_teammates = false) -> std::optional<SolveInput>;

// Read a pawn's model studio hitbox set (real capsules, bone-transformed to world
// space), for the exact-hitbox path and for validation. Empty when the model /
// hitbox set cannot be read.
[[nodiscard]] auto read_model_hitboxes(std::uintptr_t pawn) -> std::vector<ModelHitbox>;

}  // namespace cs2::aim
