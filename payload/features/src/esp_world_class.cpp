#include "cs2/features/esp_world_class.hpp"

#include <cstring>

namespace cs2::features::esp_world {

auto matches(const char* text, const char* prefix, std::size_t length) noexcept -> bool {
  return std::memcmp(text, prefix, length) == 0;
}

auto classify(const char* name) noexcept -> WorldClass {
  if (matches(name, "smokegr", 7)) {
    return WorldClass::smoke;
  }
  if (matches(name, "flashba", 7)) {
    return WorldClass::flash;
  }
  if (matches(name, "molotov", 7)) {
    return WorldClass::molotov;
  }
  if (matches(name, "incendi", 7)) {
    return WorldClass::incendiary;
  }
  if (matches(name, "hegrena", 7)) {
    return WorldClass::he;
  }
  if (matches(name, "decoy_p", 7)) {
    return WorldClass::decoy;
  }
  // The two grenades the list did not name: without them they got no marker
  // at all.
  if (matches(name, "sensorgr", 8)) {
    return WorldClass::sensor;
  }
  if (matches(name, "bumpmine", 8)) {
    return WorldClass::bumpmine;
  }
  if (matches(name, "planted", 7)) {
    return WorldClass::c4;
  }
  if (matches(name, "c4", 2)) {
    return WorldClass::c4;
  }
  if (matches(name, "weapon_", 7)) {
    return WorldClass::weapon;
  }
  return WorldClass::none;
}

auto is_deployed_incendiary(const char* rest) noexcept -> bool {
  return matches(rest, "taggrenad", k_deployed_incendiary_length);
}

}  // namespace cs2::features::esp_world
