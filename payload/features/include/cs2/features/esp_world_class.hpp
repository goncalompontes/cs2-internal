#pragma once

// The world entity classes the ESP's entity walk recognises.
//
// The name is a class name ("smokegrenade_projectile", "planted_c4",
// "weapon_ak47"), so the **prefix** identifies the class without a class
// pointer — the game updates the classes, the prefixes stay.
//
// The classification is pure, so the host test can reach it: the walk itself
// needs the engine's entity list.

#include <cstddef>
#include <cstdint>

namespace cs2::features::esp_world {

// The classes the walk recognises.
enum class WorldClass : std::uint8_t {
  none,
  smoke,
  flash,
  molotov,
  incendiary,
  he,
  decoy,
  sensor,    // the motion grenade
  bumpmine,  // the proximity mine
  c4,
  weapon,
};

// The number of characters the walk reads to classify a class name: seven
// characters plus the terminating NUL.
inline constexpr std::size_t k_class_prefix_length = 8;

// The bytes after the "weapon_" prefix that name the deployed incendiary
// (`weapon_taggrenade`): it is a grenade, not a dropped weapon, and the
// seven-character prefix cannot tell the two apart.
inline constexpr std::size_t k_deployed_incendiary_length = 9;

[[nodiscard]] auto matches(const char* text, const char* prefix, std::size_t length) noexcept
    -> bool;

[[nodiscard]] auto classify(const char* prefix) noexcept -> WorldClass;

// Whether the bytes after the "weapon_" prefix name the deployed incendiary.
[[nodiscard]] auto is_deployed_incendiary(const char* rest) noexcept -> bool;

}  // namespace cs2::features::esp_world
