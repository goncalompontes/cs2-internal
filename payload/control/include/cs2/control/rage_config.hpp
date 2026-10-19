#pragma once

// Per-weapon ragebot configs, persisted to disk (docs/ragebot.md).
//
// The ragebot's tuning is generic; a **profile** is the same tuning keyed by the active
// weapon's designer name (`weapon_ak47`, …) so the solve can switch objective / hitbox /
// FOV / gates / assists per weapon. The store is a fixed POD table (the payload has no
// CRT static initialisers); persistence is a small line-based text file in the payload's
// data directory (`cs2-rage.cfg`), so configs survive restarts.

#include <cstdint>

namespace cs2::control {

// One weapon's ragebot tuning. `weapon` is the designer name (NUL-terminated, no
// "weapon_" prefix required — it is normalised on load/save).
struct RageProfile {
  char weapon[24] = {};
  std::uint8_t objective = 1;      // 0 = damage, 1 = hitchance, 2 = expected
  std::uint8_t hitbox = 0;         // 0 = auto, 1 = head, 2 = body
  std::uint8_t mode = 0;           // 0 = hold key, 1 = always on, 2 = toggle
  std::uint8_t autostop = 0;       // 0 off / 1 always / 2 when firing
  std::uint8_t autoscope = 0;      // 0 off / 1 always / 2 when firing
  std::uint8_t autoreload = 0;
  std::uint8_t only_visible = 0;
  std::uint8_t fire = 0;
  std::int32_t key = 0;            // virtual-key code
  float fov = 0.0F;
  float min_damage = 0.0F;
  float min_hitchance = 0.0F;
};

inline constexpr int k_max_rage_profiles = 32;

struct RageConfigStore {
  int count = 0;
  RageProfile profiles[k_max_rage_profiles] = {};
};

// Load/save the whole store from/to the payload data file. False on absence/failure
// (the caller keeps its defaults); a save creates the directory as needed.
[[nodiscard]] auto rage_config_load(RageConfigStore& store) noexcept -> bool;
[[nodiscard]] auto rage_config_save(const RageConfigStore& store) noexcept -> bool;

// The profile for `weapon` (normalised), or nullptr. When `create` is set, a missing
// profile is appended (up to the cap). Matching is case-insensitive and ignores a
// leading `weapon_`.
[[nodiscard]] auto rage_profile_find(RageConfigStore& store, const char* weapon, bool create)
    -> RageProfile*;

// Copy `weapon` into `out` (normalised, lower-case, with the `weapon_` prefix). Always
// NUL-terminates; used for the diagnostics/UI.
void rage_weapon_key(const char* weapon, char* out, int size) noexcept;

}  // namespace cs2::control
