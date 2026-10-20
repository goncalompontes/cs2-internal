#include "cs2/game/material.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

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

// The material system's parameter names are compared case-insensitively
// (`V_stricmp_fast`), so the payload compares them the same way.
[[nodiscard]] auto equals_ignore_case(std::string_view left, std::string_view right) noexcept
    -> bool {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (fold(left, index) != fold(right, index)) {
      return false;
    }
  }
  return true;
}

// Does the name mention a colour? The engine names its texture parameters
// "g_t...", so those are excluded: a texture parameter's value is a handle,
// not a colour.
[[nodiscard]] auto mentions_colour(std::string_view name) noexcept -> bool {
  if (name.size() > 2 && name[1] == '_' && (name[0] == 'g' || name[0] == 'm') && name[2] == 't') {
    return false;
  }
  static constexpr std::string_view k_words[] = {"color", "colour", "tint"};
  for (const std::string_view word : k_words) {
    if (name.size() < word.size()) {
      continue;
    }
    for (std::size_t offset = 0; offset + word.size() <= name.size(); ++offset) {
      bool same = true;
      for (std::size_t index = 0; index < word.size(); ++index) {
        const auto left = fold(name, offset + index);
        const auto right = fold(word, index);
        if (left != right) {
          same = false;
          break;
        }
      }
      if (same) {
        return true;
      }
    }
  }
  return false;
}

// Copy a name into a caller-owned buffer. Returns false when it does not fit.
[[nodiscard]] auto copy_name(char* out, std::size_t size, std::string_view name) noexcept -> bool {
  if (out == nullptr || size == 0 || name.size() + 1 > size) {
    return false;
  }
  for (std::size_t index = 0; index < name.size(); ++index) {
    out[index] = name[index];
  }
  out[name.size()] = '\0';
  return true;
}

// The material's parameter table: the array pointer and the number of
// parameters. Both reads go through the fault-safe helper, so a material
// whose layout is not what the engine's is simply "not there".
[[nodiscard]] auto parameter_array(Material material, std::uintptr_t& array,
                                   std::int32_t& count) noexcept -> bool {
  if (material == 0) {
    return false;
  }
  if (!read(material + k_material_param_count_offset, count) || count <= 0 ||
      count > k_max_material_parameters) {
    return false;
  }
  if (!read(material + k_material_param_array_offset, array) || array == 0) {
    return false;
  }
  return true;
}

// The name of the parameter at `index`, or the empty string.
[[nodiscard]] auto parameter_name(std::uintptr_t array, std::size_t index) noexcept -> std::string {
  const std::uintptr_t entry =
      array + static_cast<std::uintptr_t>(index) * k_material_param_entry_size;
  std::uintptr_t name_pointer = 0;
  if (!read(entry + k_material_param_name_offset, name_pointer) || name_pointer == 0) {
    return {};
  }
  return read_cstring(name_pointer, k_max_parameter_name_length);
}

// Does the material have a parameter with this name?
[[nodiscard]] auto has_parameter(Material material, std::string_view name) noexcept -> bool {
  std::uintptr_t array = 0;
  std::int32_t count = 0;
  if (!parameter_array(material, array, count)) {
    return false;
  }
  for (std::int32_t index = 0; index < count; ++index) {
    const std::string entry_name = parameter_name(array, static_cast<std::size_t>(index));
    if (!entry_name.empty() && equals_ignore_case(entry_name, name)) {
      return true;
    }
  }
  return false;
}

// The material's colour parameter.
//
// The engine's own names are tried first. When the material has none of them
// its own parameter names are scanned for a colour-like one — but only when
// there is exactly one, because a material with several colour-ish
// parameters would be tinted with the wrong one.
[[nodiscard]] auto find_colour_parameter(Material material, char* out, std::size_t size) noexcept
    -> bool {
  for (const std::string_view candidate : k_colour_parameter_names) {
    if (has_parameter(material, candidate)) {
      return copy_name(out, size, candidate);
    }
  }

  std::uintptr_t array = 0;
  std::int32_t count = 0;
  if (!parameter_array(material, array, count)) {
    return false;
  }
  std::string found;
  int matches = 0;
  for (std::int32_t index = 0; index < count; ++index) {
    const std::string entry_name = parameter_name(array, static_cast<std::size_t>(index));
    if (entry_name.empty() || !mentions_colour(entry_name)) {
      continue;
    }
    found = entry_name;
    ++matches;
  }
  if (matches != 1) {
    return false;
  }
  return copy_name(out, size, found);
}

// Read the material's own value of a named parameter.
//
// `CMaterial2::GetVectorParam` is vtable slot 35; the slot has to point into
// materialsystem2.dll, or the material is not the one the offsets were
// derived on.
[[nodiscard]] auto read_parameter(Material material, const char* name, float out[4]) noexcept
    -> bool {
  if (material == 0 || name == nullptr || out == nullptr) {
    return false;
  }
  std::uintptr_t vtable = 0;
  if (!read(material, vtable) || vtable == 0) {
    return false;
  }
  std::uintptr_t slot = 0;
  if (!read(vtable + static_cast<std::uintptr_t>(k_get_material_param_slot) * sizeof(void*),
            slot) ||
      slot == 0) {
    return false;
  }
  const ModuleInfo materials = find_module("materialsystem2.dll");
  if (!materials.valid() || !materials.contains(slot)) {
    return false;
  }
  using get_param_fn = void (*)(void*, float*, const char*, const float*);
  const auto get = reinterpret_cast<get_param_fn>(slot);
  const float fallback[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  get(reinterpret_cast<void*>(material), out, name, fallback);
  return true;
}

// Write a value into a named parameter of the material the handle refers to.
//
// `CMaterialSystem2::SetMaterialVectorParam` is vtable slot 23; the value is
// handed over as a string, because that is what the entry point parses.
[[nodiscard]] auto write_parameter(std::uintptr_t system, MaterialHandle handle, const char* name,
                                   const float value[4]) noexcept -> bool {
  if (system == 0 || handle == 0 || name == nullptr || value == nullptr) {
    return false;
  }
  std::uintptr_t vtable = 0;
  if (!read(system, vtable) || vtable == 0) {
    return false;
  }
  std::uintptr_t slot = 0;
  if (!read(vtable + static_cast<std::uintptr_t>(k_set_material_param_slot) * sizeof(void*),
            slot) ||
      slot == 0) {
    return false;
  }
  const ModuleInfo materials = find_module("materialsystem2.dll");
  if (!materials.valid() || !materials.contains(slot)) {
    return false;
  }
  char text[64];
  const int written =
      std::snprintf(text, sizeof(text), "%f %f %f %f", value[0], value[1], value[2], value[3]);
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(text)) {
    return false;
  }
  using set_param_fn = bool (*)(void*, void*, const char*, const char*);
  const auto set = reinterpret_cast<set_param_fn>(slot);
  return set(reinterpret_cast<void*>(system), reinterpret_cast<void*>(handle), name, text);
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

auto MaterialSystem::find_material(std::string_view path, MaterialHandle& handle) noexcept
    -> Material {
  handle = 0;
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

  // `FindMaterial(this, out, path)` writes the material system's *handle*
  // into `out` and returns the out parameter itself. The handle's first word
  // is the material and its reference count is at `+0x20`, so the material
  // the scene system's draw needs is `*handle` — and the parameter setter
  // wants the handle.
  using find_material_fn = std::uintptr_t (*)(void*, void**, const char*);
  const auto find = reinterpret_cast<find_material_fn>(slot);
  const std::string terminated(path);
  void* out = nullptr;
  const std::uintptr_t result = find(reinterpret_cast<void*>(system_), &out, terminated.c_str());
  if (result == 0) {
    return 0;
  }
  std::uintptr_t material_handle = 0;
  if (!read(result, material_handle) || material_handle == 0) {
    return 0;
  }
  std::uintptr_t material = 0;
  if (!read(material_handle, material) || material == 0) {
    return 0;
  }
  // The material's first word is its vtable pointer; it has to point into
  // materialsystem2.dll, or the pointer is not a material at all.
  std::uintptr_t material_vtable = 0;
  if (!read(material, material_vtable) || material_vtable == 0 ||
      !materials.contains(material_vtable)) {
    return 0;
  }
  handle = material_handle;
  return material;
}

auto MaterialSystem::set_colour(MaterialHandle handle, Material material, const float rgba[4],
                                float old[4]) noexcept -> ColourResult {
  if (system_ == 0 || handle == 0 || material == 0 || rgba == nullptr || old == nullptr) {
    return ColourResult::no_material;
  }

  // The parameter to write: the material's own table says which colour
  // parameter it has.
  char name[k_max_parameter_name_length];
  if (!find_colour_parameter(material, name, sizeof(name))) {
    return ColourResult::no_parameter;
  }

  // The material's own colour, so it can be put back later.
  if (!read_parameter(material, name, old)) {
    return ColourResult::no_parameter;
  }

  if (!write_parameter(system_, handle, name, rgba)) {
    return ColourResult::no_parameter;
  }
  return ColourResult::applied;
}

auto MaterialSystem::restore_colour(MaterialHandle handle, Material material,
                                    const float rgba[4]) noexcept -> bool {
  if (system_ == 0 || handle == 0 || material == 0 || rgba == nullptr) {
    return false;
  }
  char name[k_max_parameter_name_length];
  if (!find_colour_parameter(material, name, sizeof(name))) {
    return false;
  }
  return write_parameter(system_, handle, name, rgba);
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
