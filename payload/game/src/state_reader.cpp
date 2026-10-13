#include "cs2/game/state_reader.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "offsets.hpp"
#include "schemas/client_dll.hpp"

namespace cs2::game {
namespace {

using namespace cs2::sdk;

// Resolve a field offset from the generated client schema (ADR-0001). The
// fallback covers a build whose schema dump is older than the field.
std::uint32_t field(std::string_view class_target, std::string_view field_target,
                    std::uint32_t fallback) {
  const std::uint32_t offset =
      schemas::client_dll::field_offset_by_target(class_target, field_target);
  return offset != 0 ? offset : fallback;
}

// Read a resolved global pointer by offset name (e.g. "entity_list").
std::uintptr_t global(std::uintptr_t client_base, std::string_view name) {
  const OffsetInfo* info = find_offset(name);
  if (info == nullptr || info->module != "client.dll") {
    return 0;
  }
  std::uintptr_t pointer = 0;
  if (!read(client_base + info->rva, pointer)) {
    return 0;
  }
  return pointer;
}

template <typename T>
[[nodiscard]] T read_or(std::uintptr_t address, T fallback) noexcept {
  T value = fallback;
  if (!read(address, value)) {
    return fallback;
  }
  return value;
}

}  // namespace

StateReader::FieldOffsets StateReader::FieldOffsets::resolve() {
  FieldOffsets o;
  o.health = field("C_BaseEntity", "m_iHealth", o.health);
  o.max_health = field("C_BaseEntity", "m_iMaxHealth", o.max_health);
  o.life_state = field("C_BaseEntity", "m_lifeState", o.life_state);
  o.team = field("C_BaseEntity", "m_iTeamNum", o.team);
  o.scene_node = field("C_BaseEntity", "m_pGameSceneNode", o.scene_node);
  o.velocity = field("C_BaseEntity", "m_vecAbsVelocity", o.velocity);
  o.view_offset = field("C_BaseModelEntity", "m_vecViewOffset", o.view_offset);
  o.eye_angles = field("C_CSPlayerPawn", "m_angEyeAngles", o.eye_angles);
  o.armor = field("C_CSPlayerPawn", "m_ArmorValue", o.armor);
  o.weapon_services = field("C_BasePlayerPawn", "m_pWeaponServices", o.weapon_services);
  o.controller = field("C_BasePlayerPawn", "m_hController", o.controller);
  o.is_local_controller =
      field("CBasePlayerController", "m_bIsLocalPlayerController", o.is_local_controller);
  o.pawn = field("CBasePlayerController", "m_hPawn", o.pawn);
  o.player_name = field("CBasePlayerController", "m_iszPlayerName", o.player_name);
  o.player_pawn = field("CCSPlayerController", "m_hPlayerPawn", o.player_pawn);
  o.active_weapon = field("CPlayer_WeaponServices", "m_hActiveWeapon", o.active_weapon);
  o.clip1 = field("C_BasePlayerWeapon", "m_iClip1", o.clip1);
  o.reserve_ammo = field("C_BasePlayerWeapon", "m_pReserveAmmo", o.reserve_ammo);
  // CGameSceneNode::m_vecAbsOrigin — the class lives in the server schema.
  o.abs_origin = 0xc8;
  return o;
}

std::optional<StateReader> StateReader::attach() noexcept {
  const ModuleInfo client = find_module("client.dll");
  if (!client.valid()) {
    return std::nullopt;
  }
  StateReader reader;
  reader.client_base_ = client.base;
  reader.offsets_ = FieldOffsets::resolve();
  reader.entities_ = EntitySystem(global(client.base, "entity_list"));
  return reader;
}

PlayerSnapshot StateReader::read_player(int index, std::uintptr_t controller) const {
  const FieldOffsets& o = offsets_;
  PlayerSnapshot player;
  player.index = index;
  player.controller = controller;

  // Controller first: it carries the identity (name, team, local flag) even
  // when the pawn is dead or not yet spawned.
  player.name = read_inline_string(controller + o.player_name, 128);
  std::uint8_t team = 0;
  (void)read(controller + o.team, team);
  player.team = team;
  std::uint8_t is_local = 0;
  (void)read(controller + o.is_local_controller, is_local);
  player.is_local = is_local != 0;

  std::uint32_t handle = read_or<std::uint32_t>(controller + o.player_pawn, 0);
  if (handle == 0) {
    handle = read_or<std::uint32_t>(controller + o.pawn, 0);
  }
  const std::uintptr_t pawn = entities_.resolve(handle);
  if (pawn == 0) {
    return player;
  }
  player.pawn = pawn;

  std::uint8_t life_state = 0;
  (void)read(pawn + o.life_state, life_state);
  player.life_state = life_state;
  player.health = read_or<std::int32_t>(pawn + o.health, 0);
  player.max_health = read_or<std::int32_t>(pawn + o.max_health, 0);
  player.armor = read_or<std::int32_t>(pawn + o.armor, 0);
  player.alive = life_state == 0 && player.health > 0;

  (void)read(pawn + o.eye_angles, player.eye_angles);
  (void)read(pawn + o.velocity, player.velocity);

  const std::uintptr_t scene = read_or<std::uintptr_t>(pawn + o.scene_node, 0);
  Vec3 view_offset{};
  if (scene != 0) {
    (void)read(scene + o.abs_origin, player.origin);
  }
  (void)read(pawn + o.view_offset, view_offset);
  player.eye_origin = player.origin + view_offset;

  // Active weapon: name + ammo.
  const std::uintptr_t services = read_or<std::uintptr_t>(pawn + o.weapon_services, 0);
  if (services != 0) {
    const std::uint32_t weapon_handle = read_or<std::uint32_t>(services + o.active_weapon, 0);
    const std::uintptr_t weapon = entities_.resolve(weapon_handle);
    if (weapon != 0) {
      player.clip = read_or<std::int32_t>(weapon + o.clip1, 0);
      player.reserve = read_or<std::int32_t>(weapon + o.reserve_ammo, 0);
      player.weapon = entities_.designer_name(static_cast<int>(weapon_handle & 0x7fffU));
    }
  }
  return player;
}

Snapshot StateReader::snapshot() const {
  Snapshot snapshot;
  if (!ready()) {
    return snapshot;
  }

  const std::uintptr_t local_controller = global(client_base_, "local_player_controllers");
  if (local_controller == 0) {
    return snapshot;
  }
  snapshot.valid = true;

  // All player controllers share the local controller's vtable; enumerate them
  // from the entity table by matching it (no per-build vtable RVA is
  // hard-coded).
  const std::uintptr_t controller_vtable = read_or<std::uintptr_t>(local_controller, 0);
  for (int page = 0; controller_vtable != 0 && page < EntitySystem::k_page_count; ++page) {
    const std::uintptr_t page_base = entities_.page(page);
    if (page_base == 0) {
      continue;
    }
    for (int slot = 0; slot < EntitySystem::k_slots_per_page; ++slot) {
      const std::uintptr_t entry =
          page_base + EntitySystem::k_identity_stride * static_cast<std::uintptr_t>(slot);
      const std::uintptr_t controller = read_or<std::uintptr_t>(entry, 0);
      if (controller == 0 || read_or<std::uintptr_t>(controller, 0) != controller_vtable) {
        continue;
      }
      const int index = (page << EntitySystem::k_page_shift) | slot;
      PlayerSnapshot player = read_player(index, controller);
      player.is_local = player.is_local || controller == local_controller;
      if (player.is_local) {
        snapshot.local = player;
        snapshot.local_pawn = player.pawn;
      }
      snapshot.players.push_back(std::move(player));
    }
  }

  // Measure everyone against the local player.
  if (snapshot.local.has_value()) {
    for (PlayerSnapshot& player : snapshot.players) {
      player.distance = distance(player.origin, snapshot.local->origin);
    }
  }
  return snapshot;
}

}  // namespace cs2::game
