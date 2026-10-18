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
                               int target_index = -1, bool weapon_from_game = true)
    -> std::optional<SolveInput>;

// Read a pawn's model studio hitbox set (real capsules, bone-transformed to world
// space), for the exact-hitbox path and for validation. Empty when the model /
// hitbox set cannot be read. `read_names` fills `ModelHitbox::name` (a string read
// per box); pass false where the caller does not need it (the per-frame record
// sampling) to skip that cost.
[[nodiscard]] auto read_model_hitboxes(std::uintptr_t pawn, bool read_names = true)
    -> std::vector<ModelHitbox>;

// A pawn's hitboxes as the solver consumes them: the real studio capsules valued
// by the weapon's headshot multiplier. Empty when the model cannot be resolved
// (the caller may then fall back to the collision-AABB proxy). This is the shared
// read for both the single-target solve and the per-tick record sampling.
[[nodiscard]] auto read_solver_hitboxes(std::uintptr_t pawn, const WeaponProfile& weapon)
    -> std::vector<Hitbox>;

// Override `profile` with a weapon's real `CCSWeaponBaseVData` values (damage,
// headshot multiplier, penetration, range modifier). False when the weapon /
// VData cannot be read (the caller's profile stands).
[[nodiscard]] auto read_weapon_profile(std::uintptr_t weapon_vdata, WeaponProfile& profile) noexcept
    -> bool;

}  // namespace cs2::aim
