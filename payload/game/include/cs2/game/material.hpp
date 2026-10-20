#pragma once

// The material system: the payload's access to CS2's `IMaterial2` objects.
//
// The chams draw the player models with one of the game's **own** pre-compiled
// materials instead of the model's own, so the payload has to find a material
// by its resource path. This header also carries the material system's
// parameter-name hash, which the parameter lookups are keyed by.
//
// **Provenance.** All of the addresses below were re-derived on the *live*
// client build (client.dll sha 659940c1..., base 0x180000000); the older
// build in `docs/re/client-dll.md` is stale and must not be used.
//
//   * `CMaterialSystem2*` = `client.dll + 0x279CE48` — the client's
//     interface-name -> global table (`{"VMaterialSystem2_001", ...}`).
//   * `CMaterialSystem2::FindMaterial` = vtable **slot 14** (`+0x70`), the
//     signature `(this, IMaterial2** out, const char* path)`. The returned
//     handle is a *pointer to* the material slot, so the material itself is
//     `*handle`; the material's reference count is at `+0x20`. Proved by the
//     client's own call site (`CPhysicsGameSystem::GameActivate`).
//   * the material-parameter **name hash**: MurmurHash2, case-folded, seed
//     `0x31415926`, `h = len ^ seed` — recovered from materialsystem2.dll
//     `FUN_180007f20` and verified against `0x5D700411` = "RepresentativeTexture".

#include <cstdint>
#include <string_view>

namespace cs2::game {

// --- The material-parameter name hash --------------------------------------

// The material system's parameter-name hash: MurmurHash2 over the
// **case-folded** name, seed `0x31415926`, `h = len ^ seed`.
//
// The material system lower-cases the name before hashing it, so this does
// too: "Color" and "color" hash the same. The host test pins the function
// against the engine's own constant (`0x5D700411` = "RepresentativeTexture"),
// so a refactor cannot silently change the hash.
[[nodiscard]] auto material_parameter_hash(std::string_view name) noexcept -> std::uint32_t;

// The two hashed names the material binds as *textures* (materialsystem2.dll,
// `CMaterial2` vtable slot 22 -> `FUN_18000b700`). They are **not** colour
// parameters — the slot resolves texture handles with a default, the way
// `CMaterial2` slot 21 resolves the "RepresentativeTexture" handle.
inline constexpr std::uint32_t k_material_texture_a_hash = 0xCF382AB;
inline constexpr std::uint32_t k_material_texture_b_hash = 0x28C67ADC;

// The hashed name of the material's representative texture
// ("RepresentativeTexture"), the engine's own constant.
inline constexpr std::uint32_t k_material_representative_texture_hash = 0x5D700411;

// --- The engine's material-system access -----------------------------------

// The client's global that holds the `CMaterialSystem2*`.
inline constexpr std::uintptr_t k_material_system_global_rva = 0x279CE48;

// `CMaterialSystem2::FindMaterial` — the vtable slot (`+0x70`).
inline constexpr int k_find_material_slot = 14;

// The materials the chams use. All three are the game's own, already
// precached, so no shader is compiled at runtime.
inline constexpr std::string_view k_flat_material_path = "materials/physics/flat_basic.vmat";
inline constexpr std::string_view k_wire_material_path =
    "materials/tools/show_collision_geometry_wire.vmat";
inline constexpr std::string_view k_nodepth_material_path =
    "materials/tools/show_collision_geometry_wire_nodepth.vmat";

// A resolved material: the `IMaterial2*` the scene system's draw receives.
using Material = std::uintptr_t;

// The client's material system.
//
// Every accessor **fails closed**: when the client is not up, the vtable
// cannot be read, or the `FindMaterial` slot does not point into
// `materialsystem2.dll` (a stale build), the lookup returns 0 and the caller
// leaves the draw alone. The payload never calls through a pointer it has not
// validated.
class MaterialSystem {
 public:
  // Attach to the running client's material system. The result is invalid
  // (and every lookup on it returns 0) when the client is not up yet.
  [[nodiscard]] static auto attach() noexcept -> MaterialSystem;

  [[nodiscard]] auto valid() const noexcept -> bool {
    return system_ != 0;
  }

  // Find a material by its resource path (e.g. `k_flat_material_path`).
  // Returns 0 when the lookup could not be resolved or the material does not
  // exist.
  [[nodiscard]] auto find_material(std::string_view path) noexcept -> Material;

 private:
  std::uintptr_t system_ = 0;  // the `CMaterialSystem2*`
};

// --- The chams' decision ---------------------------------------------------

// The four facts about a drawn entity the chams decision depends on.
//
// They are read from the engine at the draw (see `cs2/features/chams.hpp`);
// the decision itself is a pure function of them, so it is host-testable.
struct ChamsEntity {
  int team = 0;               // `C_BaseEntity::m_iTeamNum` (2 = T, 3 = CT)
  int life_state = 0;         // `C_BaseEntity::m_lifeState` (0 = alive)
  bool is_local = false;      // the entity is the local player's pawn
  bool is_viewmodel = false;  // the entity is drawn as the view model
};

// The chams' policy: what the operator asked for.
struct ChamsPolicy {
  bool enabled = false;
  bool only_enemies = true;    // only the players on the other team
  bool include_local = false;  // also chams the local player
  int local_team = 0;          // the local player's team (2 = T, 3 = CT)
};

// Should this entity be drawn with the chams material?
//
// The rules, in order:
//   * the view model (the weapon in front of the camera) is **never** chamsed;
//   * a dead player is not drawn;
//   * only real players (team 2 or 3) are chamsed, never a prop;
//   * the local player is chamsed only on request;
//   * with `only_enemies`, a teammate is not chamsed.
[[nodiscard]] auto wants_chams(const ChamsEntity& entity, const ChamsPolicy& policy) noexcept
    -> bool;

}  // namespace cs2::game
