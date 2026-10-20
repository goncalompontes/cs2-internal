#pragma once

// The material system: the payload's access to CS2's `IMaterial2` objects.
//
// The chams draw the player models with one of the game's **own** pre-compiled
// materials instead of the model's own, so the payload has to find a material
// by its resource path. This header also carries the material system's
// parameter-name hash, which the parameter lookups are keyed by, and the
// entry point that writes a parameter's value.
//
// **Provenance.** All of the addresses below were re-derived on the *live*
// client build (client.dll sha 659940c1..., base 0x180000000); the older
// build in `docs/re/client-dll.md` is stale and must not be used.
//
//   * `CMaterialSystem2*` = `client.dll + 0x279CE48` — the client's
//     interface-name -> global table (`{"VMaterialSystem2_001", ...}`).
//   * `CMaterialSystem2::FindMaterial` = vtable **slot 14** (`+0x70`), the
//     signature `(this, IMaterial2** out, const char* path)`. It writes the
//     material system's *handle* into `out` and returns the out parameter
//     itself. The handle's first word is the material and its reference count
//     is at `+0x20`, so the material the scene system's draw takes is
//     `*handle` and the parameter setter takes the handle. Proved by the
//     client's own call site (`CPhysicsGameSystem::GameActivate`), which
//     increments the reference count at `handle + 0x20`.
//   * the material-parameter **name hash**: MurmurHash2, case-folded, seed
//     `0x31415926`, `h = len ^ seed` — recovered from materialsystem2.dll
//     `FUN_180007f20` and verified against `0x5D700411` = "RepresentativeTexture".
//   * `CMaterialSystem2::SetMaterialVectorParam` = vtable **slot 23** (`+0xB8`)
//     — the "set a parameter by name" entry point (see below).
//   * `CMaterial2::GetVectorParam` = vtable **slot 35** (`+0x118`) — reads a
//     parameter's value.
//
// The material-system addresses were re-derived on the live materialsystem2.dll
// (sha `b4d5c667...`, image base `0x180000000`); see `docs/re/chams.md`.

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

// `CMaterialSystem2::SetMaterialVectorParam` — the material system's "set a
// parameter by name" entry point.
//
// Provenance (materialsystem2.dll sha `b4d5c667...`, image base
// `0x180000000`): `CMaterialSystem2` vtable slot 23 (`+0xB8`) ->
// `FUN_18003bb30`, verified by decompilation. The signature is
//
//   bool SetMaterialVectorParam(void* handle, const char* name, const char* value)
//
// where `handle` is the handle `FindMaterial` returned and `value` is a
// `"r g b a"` string the entry point parses with `sscanf("%f %f %f %f")`. It
// writes the value into the material's own parameter entry, and returns
// **false** when the material has no parameter with that name — which is how
// the payload learns that a material cannot be tinted.
inline constexpr int k_set_material_param_slot = 23;

// `CMaterial2::GetVectorParam` — reads a parameter's value.
//
// Provenance: `CMaterial2` vtable slot 35 (`+0x118`) -> `FUN_180011680`,
// verified by decompilation. The signature is
//
//   void GetVectorParam(float4* out, const char* name, const float4* fallback)
//
// It writes the material's own value into `out`, or the fallback when the
// material has no such parameter.
inline constexpr int k_get_material_param_slot = 35;

// The material's own parameter table (`CMaterial2`):
//
//   +0x18  the number of parameters
//   +0x20  the parameter array, 0x40 bytes per entry
//   entry +0x00  the parameter's float4 value
//   entry +0x28  the parameter's name (a C string)
//
// Verified from the material's own parameter accessors: `FUN_1800117b0` walks
// the array and compares the name at `+0x28` (`V_stricmp_fast`), and
// `FUN_180011680` reads the float4 at `+0x00` of the entry it found.
inline constexpr std::uintptr_t k_material_param_count_offset = 0x18;
inline constexpr std::uintptr_t k_material_param_array_offset = 0x20;
inline constexpr std::uintptr_t k_material_param_entry_size = 0x40;
inline constexpr std::uintptr_t k_material_param_name_offset = 0x28;

// The bounds the payload puts on the material's parameter table before it
// walks it: a material has a handful of parameters, and a name is short.
inline constexpr std::int32_t k_max_material_parameters = 256;
inline constexpr std::size_t k_max_parameter_name_length = 64;

// The colour parameter names the engine's own shaders use.
//
// Every shader names its colour differently, so the payload asks the material
// which of these it has. The names come from the engine's own shader
// parameter tables (the compiled shader's table stores each parameter's name
// as the material system's name hash):
//
//   * `"g_vOverrideColor"` — the `tools_wireframe` shader, the one behind
//     `materials/tools/show_collision_geometry_wire.vmat`. Read out of the
//     compiled shader's own parameter table (`shaders_pc_dir.vpk`,
//     `shaders/vfx/tools_wireframe_pc_50_features.vcs`).
//   * `"g_vWireframeColor"` — the material system's own embedded wireframe
//     shader (materialsystem2.dll's shader parameter table).
//   * `"g_vColorTint"` — the `csgo_unlitgeneric` shader, the one behind
//     `materials/engine/singlecolor.vmat`. Read out of the compiled shader's
//     parameter table and out of the material system's table.
inline constexpr std::string_view k_colour_parameter_names[] = {
    "g_vOverrideColor",
    "g_vWireframeColor",
    "g_vColorTint",
    "g_vColor",
};

// The hashes of the names above, with the material system's own name hash.
// The host test pins them, so a change to the hash is caught.
inline constexpr std::uint32_t k_colour_parameter_hashes[] = {
    0xD4BD94B3,  // "g_vOverrideColor"
    0xCE09A55C,  // "g_vWireframeColor"
    0x20A77985,  // "g_vColorTint"
    0xF05CC739,  // "g_vColor"
};

// The materials the chams use. All three are the game's own, already
// precached, so no shader is compiled at runtime.
inline constexpr std::string_view k_flat_material_path = "materials/physics/flat_basic.vmat";
inline constexpr std::string_view k_wire_material_path =
    "materials/tools/show_collision_geometry_wire.vmat";
inline constexpr std::string_view k_nodepth_material_path =
    "materials/tools/show_collision_geometry_wire_nodepth.vmat";

// A resolved material: the `IMaterial2*` the scene system's draw receives.
using Material = std::uintptr_t;

// The material system's handle for a material.
//
// `FindMaterial` returns a *handle*: an object whose first word is the
// material itself and whose reference count is at `+0x20`. The scene system's
// draw takes the material; the parameter setter takes the handle.
using MaterialHandle = std::uintptr_t;

// The outcome of a colour write.
enum class ColourResult {
  applied,       // the material's colour parameter was found and written
  no_parameter,  // the material has no colour parameter: it keeps its own
  no_material,   // the material is not usable (no handle, or no material)
};

// The client's material system.
//
// Every accessor **fails closed**: when the client is not up, the vtable
// cannot be read, or the vtable slot does not point into
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
  //
  // Returns the material — the `IMaterial2*` the scene system's draw takes —
  // or 0 when the lookup failed. The material system's *handle* for it (the
  // object the parameter setter takes) is written to `handle`.
  [[nodiscard]] auto find_material(std::string_view path, MaterialHandle& handle) noexcept
      -> Material;

  // Write `rgba` (each component in 0..1) into the material's own colour
  // parameter.
  //
  // The material's own colour is written to `old`, so it can be put back
  // with `restore_colour`. When the material has no colour parameter the
  // call does nothing and the material keeps its own colour.
  [[nodiscard]] auto set_colour(MaterialHandle handle, Material material, const float rgba[4],
                                float old[4]) noexcept -> ColourResult;

  // Put the material's own colour back.
  [[nodiscard]] auto restore_colour(MaterialHandle handle, Material material,
                                    const float rgba[4]) noexcept -> bool;

 private:
  std::uintptr_t system_ = 0;  // the `CMaterialSystem2*`
};

// --- The chams' colour decision ---------------------------------------------

// What the chams should do with the material's colour.
enum class ColourAction {
  none,     // leave the material alone
  apply,    // write the operator's colour into the material
  restore,  // put the material's own colour back
};

// The decision the chams make about the material's colour.
//
// Pure, so it can be tested without the engine. The inputs are:
//
//   * `enabled`  — the operator has the chams on;
//   * `tintable` — the material is resolved and has a colour parameter the
//                  payload can write;
//   * `applied`  — the operator's colour is currently written into the
//                  material;
//   * `changed`  — the operator's colour is not the one that is applied.
//
// The result is `restore` when the colour has to go back (the chams were
// switched off, or the colour cannot be written any more), `apply` when the
// operator's colour has to be written, and `none` when nothing is to be done.
[[nodiscard]] constexpr auto colour_action(bool enabled, bool tintable, bool applied,
                                           bool changed) noexcept -> ColourAction {
  if (!enabled || !tintable) {
    return applied ? ColourAction::restore : ColourAction::none;
  }
  if (changed || !applied) {
    return ColourAction::apply;
  }
  return ColourAction::none;
}

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
