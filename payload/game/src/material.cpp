#include "cs2/game/material.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"

namespace cs2::game {
namespace {

// The case-folded byte of a name character. The material system lower-cases
// the name with `ToLowerFast` before hashing it, so the hash is over the
// folded bytes.
[[nodiscard]] auto fold(std::string_view name, std::size_t index) noexcept -> std::uint8_t {
  const auto value = static_cast<std::uint8_t>(name[index]);
  return (value >= 'A' && value <= 'Z') ? static_cast<std::uint8_t>(value + 32) : value;
}

}  // namespace

auto material_parameter_hash(std::string_view name) noexcept -> std::uint32_t {
  // MurmurHash2, case-folded. The constants and the mixing are the material
  // system's own (`materialsystem2.dll` `FUN_180007f20`): the multiplier
  // 0x5BD1E995, the finalisation shift 24, `h = len ^ seed`.
  constexpr std::uint32_t k_multiplier = 0x5BD1E995;
  constexpr std::uint32_t k_seed = 0x31415926;

  const std::size_t length = name.size();
  std::uint32_t hash = static_cast<std::uint32_t>(length) ^ k_seed;
  std::size_t index = 0;

  for (std::size_t block = 0; block < length / 4; ++block) {
    std::uint32_t key = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
      key |= static_cast<std::uint32_t>(fold(name, index + byte)) << (8 * byte);
    }
    index += 4;
    key *= k_multiplier;
    key ^= key >> 24;
    key *= k_multiplier;
    hash *= k_multiplier;
    hash ^= key;
  }

  const std::size_t rest = length & 3;
  if (rest == 3) {
    hash ^= static_cast<std::uint32_t>(fold(name, index + 2)) << 16;
  }
  if (rest >= 2) {
    hash ^= static_cast<std::uint32_t>(fold(name, index + 1)) << 8;
  }
  if (rest >= 1) {
    hash ^= static_cast<std::uint32_t>(fold(name, index));
    hash *= k_multiplier;
  }

  hash ^= hash >> 13;
  hash *= k_multiplier;
  hash ^= hash >> 15;
  return hash;
}

auto MaterialSystem::attach() noexcept -> MaterialSystem {
  MaterialSystem result;
  const ModuleInfo client = find_module("client.dll");
  if (!client.valid()) {
    return result;
  }
  std::uintptr_t system = 0;
  if (!read(client.base + k_material_system_global_rva, system) || system == 0) {
    return result;
  }
  // The vtable must be readable and the `FindMaterial` slot must point into
  // `materialsystem2.dll`; otherwise the client is a build the offsets were
  // not derived on and the call could land anywhere.
  std::uintptr_t vtable = 0;
  if (!read(system, vtable) || vtable == 0) {
    return result;
  }
  std::uintptr_t slot = 0;
  if (!read(vtable + static_cast<std::uintptr_t>(k_find_material_slot) * sizeof(void*), slot) ||
      slot == 0) {
    return result;
  }
  const ModuleInfo materials = find_module("materialsystem2.dll");
  if (!materials.valid() || !materials.contains(slot)) {
    return result;
  }
  result.system_ = system;
  return result;
}

auto MaterialSystem::find_material(std::string_view path) noexcept -> Material {
  if (system_ == 0 || path.empty()) {
    return 0;
  }
  std::uintptr_t vtable = 0;
  if (!read(system_, vtable) || vtable == 0) {
    return 0;
  }
  std::uintptr_t slot = 0;
  if (!read(vtable + static_cast<std::uintptr_t>(k_find_material_slot) * sizeof(void*), slot) ||
      slot == 0) {
    return 0;
  }
  const ModuleInfo materials = find_module("materialsystem2.dll");
  if (!materials.valid() || !materials.contains(slot)) {
    return 0;
  }

  // `FindMaterial(this, out, path)` returns a handle to the material *slot*;
  // the material itself is `*handle` (the client's own usage).
  using find_material_fn = std::uintptr_t (*)(void*, void**, const char*);
  const auto find = reinterpret_cast<find_material_fn>(slot);
  const std::string terminated(path);
  void* out = nullptr;
  const std::uintptr_t handle = find(reinterpret_cast<void*>(system_), &out, terminated.c_str());
  if (handle == 0) {
    return 0;
  }
  std::uintptr_t material = 0;
  if (!read(handle, material) || material == 0) {
    return 0;
  }
  return material;
}

auto wants_chams(const ChamsEntity& entity, const ChamsPolicy& policy) noexcept -> bool {
  if (!policy.enabled) {
    return false;
  }
  // The view model is the weapon in front of the camera. It is never chamsed:
  // the operator asked for the *players*, and the view model is the player's
  // own weapon.
  if (entity.is_viewmodel) {
    return false;
  }
  // A dead player is not drawn at all.
  if (entity.life_state != 0) {
    return false;
  }
  // Only real players (team 2 = T, team 3 = CT) are chamsed; everything else
  // (a dropped weapon, a grenade, a prop) is left alone.
  if (entity.team != 2 && entity.team != 3) {
    return false;
  }
  // The local player is chamsed only on request.
  if (entity.is_local) {
    return policy.include_local;
  }
  // With "only enemies", a teammate keeps its own material.
  if (policy.only_enemies && policy.local_team != 0 && entity.team == policy.local_team) {
    return false;
  }
  return true;
}

}  // namespace cs2::game
