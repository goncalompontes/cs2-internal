#pragma once

// Reads local-player and player state from a running target (Phase 4).

#include <cstdint>
#include <optional>

#include "cs2/game/entity_system.hpp"
#include "cs2/game/matrix.hpp"
#include "cs2/game/snapshot.hpp"

namespace cs2::game {

// Snapshot reader for the target's player state.
//
// Field offsets come from the generated schema (`payload/sdk`, ADR-0001) with
// literals as fallbacks; global addresses come from the dumper's resolved
// offsets. Players are found by walking the entity identity table and matching
// the local pawn's vtable, so no per-build vtable RVA is hard-coded.
class StateReader {
 public:
  // Resolve the target modules and globals. Returns nullopt when `client.dll`
  // or the entity system is not up yet.
  [[nodiscard]] static std::optional<StateReader> attach() noexcept;

  // Read the current state.
  [[nodiscard]] Snapshot snapshot() const;

  // World-to-screen (P x V) matrix for a player slot (stride 0x40). Slot 0 is
  // the primary view. Returns a zero matrix when unavailable.
  [[nodiscard]] Matrix4x4 view_matrix(int slot = 0) const;

  [[nodiscard]] std::uintptr_t client_base() const noexcept { return client_base_; }
  [[nodiscard]] bool ready() const noexcept { return client_base_ != 0 && entities_.valid(); }

 private:
  // Field offsets used by the reader, resolved once at attach time.
  struct FieldOffsets {
    // C_BaseEntity
    std::uint32_t health = 0x34c;
    std::uint32_t max_health = 0x348;
    std::uint32_t life_state = 0x354;
    std::uint32_t team = 0x3e7;
    std::uint32_t scene_node = 0x330;
    std::uint32_t collision = 0x340;
    std::uint32_t velocity = 0x3f8;
    // CCollisionProperty (m_pCollision ->)
    std::uint32_t collision_mins = 0x40;
    std::uint32_t collision_maxs = 0x4c;
    // C_BaseModelEntity
    std::uint32_t view_offset = 0xf60;
    // C_CSPlayerPawn
    std::uint32_t eye_angles = 0x3600;
    std::uint32_t armor = 0x1ecc;
    std::uint32_t scoped = 0x1ea0;
    std::uint32_t defusing = 0x1ea2;
    std::uint32_t in_buy_zone = 0x15e0;
    // C_CSPlayerPawnBase
    std::uint32_t flash_duration = 0x1510;
    // C_BasePlayerPawn
    std::uint32_t weapon_services = 0x12f0;
    std::uint32_t controller = 0x14bc;
    // CBasePlayerController
    std::uint32_t is_local_controller = 0x790;
    std::uint32_t pawn = 0x6bc;
    std::uint32_t player_name = 0x6fc;
    std::uint32_t ping = 0x838;
    // CCSPlayerController
    std::uint32_t player_pawn = 0x92c;
    std::uint32_t pawn_has_defuser = 0x940;
    std::uint32_t pawn_has_helmet = 0x941;
    std::uint32_t money_services = 0x818;
    // CCSPlayerController_InGameMoneyServices (m_pInGameMoneyServices ->)
    std::uint32_t account = 0x40;
    // CPlayer_WeaponServices
    std::uint32_t active_weapon = 0x60;
    // C_BasePlayerWeapon
    std::uint32_t clip1 = 0x1928;
    // `m_pReserveAmmo` (client element/population unverified; clip is reliable).
    std::uint32_t reserve_ammo = 0x1930;
    // CGameSceneNode
    std::uint32_t abs_origin = 0xc8;

    [[nodiscard]] static FieldOffsets resolve();
  };

  [[nodiscard]] PlayerSnapshot read_player(int index, std::uintptr_t controller) const;

  std::uintptr_t client_base_ = 0;
  EntitySystem entities_;
  FieldOffsets offsets_{};
};

}  // namespace cs2::game
