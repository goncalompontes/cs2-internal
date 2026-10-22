#include "cs2/game/state_reader.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "offsets.hpp"
#if __has_include("schemas/client_dll.hpp")
#include "schemas/client_dll.hpp"
#define CS2_GAME_HAVE_CLIENT_SCHEMA 1
#endif

namespace cs2::game {
namespace {

using namespace cs2::sdk;

// Resolve a field offset from the generated client schema (ADR-0001). The
// fallback covers a build whose schema dump is older than the field, and a
// fresh checkout with no schema dump at all.
auto field(std::string_view class_target, std::string_view field_target, std::uint32_t fallback)
    -> std::uint32_t {
#if defined(CS2_GAME_HAVE_CLIENT_SCHEMA)
  const std::uint32_t offset =
      schemas::client_dll::field_offset_by_target(class_target, field_target);
  return offset != 0 ? offset : fallback;
#else
  (void)class_target;
  (void)field_target;
  return fallback;
#endif
}

// The same lookup for the weapon's data block (`CCSWeaponBaseVData`).
auto vdata_field(std::string_view field_target, std::uint32_t fallback) -> std::uint32_t {
  return field("CCSWeaponBaseVData", field_target, fallback);
}

// Read a resolved global pointer by offset name (e.g. "entity_list").
auto global(std::uintptr_t client_base, std::string_view name) -> std::uintptr_t {
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

// Address of a resolved global (e.g. a data array like "view_matrix").
auto global_address(std::uintptr_t client_base, std::string_view name) -> std::uintptr_t {
  const OffsetInfo* info = find_offset(name);
  if (info == nullptr || info->module != "client.dll") {
    return 0;
  }
  return client_base + info->rva;
}

template <typename T>
[[nodiscard]] auto read_or(std::uintptr_t address, T fallback) noexcept -> T {
  T value = fallback;
  if (!read(address, value)) {
    return fallback;
  }
  return value;
}

// The weapon's **data block** (`CCSWeaponBaseVData`): the fire mode
// (`m_bIsFullAuto`), the fire interval (`m_flCycleTime`) and the weapon type.
//
// The offsets come from the generated client schema (ADR-0001) with this
// build's values as the fallbacks. They are resolved **once**: the lookup
// walks the schema's class table, and the reader runs for every player on
// every frame.
//
// The holder is a **function-local static**: the payload is manually mapped, so
// a namespace-scope object would need the CRT's static-init pass, which a
// mapped image never gets. A function-local static is initialised on first
// use, on the thread that gets there first.
struct VDataOffsets {
  std::uint32_t weapon_type = 0x520;
  std::uint32_t full_auto = 0x72d;
  std::uint32_t cycle_time = 0x738;
};

// Returned **by value**. This used to return `const VDataOffsets&` bound to a
// function-local, i.e. a reference to a destroyed object: every caller then read
// whatever the stack slot had become. Observed live — `m_WeaponType` read
// `642683896`, so `is_rage_weapon` refused **every** weapon as "not a weapon" and
// the ragebot never solved (the same object also feeds the fire mode and the cycle
// time). Three `uint32`s cost nothing to copy, and a local is what the mapped image
// can have (no TLS guard, no `static`).
[[nodiscard]] auto vdata_offsets() noexcept -> VDataOffsets {
  return VDataOffsets{
      .weapon_type = vdata_field("m_WeaponType", 0x520),
      .full_auto = vdata_field("m_bIsFullAuto", 0x72d),
      .cycle_time = vdata_field("m_flCycleTime", 0x738),
  };
}

}  // namespace

auto StateReader::FieldOffsets::resolve() -> StateReader::FieldOffsets {
  FieldOffsets o;
  o.health = field("C_BaseEntity", "m_iHealth", o.health);
  o.max_health = field("C_BaseEntity", "m_iMaxHealth", o.max_health);
  o.life_state = field("C_BaseEntity", "m_lifeState", o.life_state);
  o.team = field("C_BaseEntity", "m_iTeamNum", o.team);
  o.flags = field("C_BaseEntity", "m_fFlags", o.flags);
  o.scene_node = field("C_BaseEntity", "m_pGameSceneNode", o.scene_node);
  o.collision = field("C_BaseEntity", "m_pCollision", o.collision);
  o.velocity = field("C_BaseEntity", "m_vecAbsVelocity", o.velocity);
  o.simulation_time = field("C_BaseEntity", "m_flSimulationTime", o.simulation_time);
  o.collision_mins = field("CCollisionProperty", "m_vecMins", o.collision_mins);
  o.collision_maxs = field("CCollisionProperty", "m_vecMaxs", o.collision_maxs);
  o.view_offset = field("C_BaseModelEntity", "m_vecViewOffset", o.view_offset);
  o.eye_angles = field("C_CSPlayerPawn", "m_angEyeAngles", o.eye_angles);
  o.armor = field("C_CSPlayerPawn", "m_ArmorValue", o.armor);
  o.scoped = field("C_CSPlayerPawn", "m_bIsScoped", o.scoped);
  o.defusing = field("C_CSPlayerPawn", "m_bIsDefusing", o.defusing);
  o.in_buy_zone = field("C_CSPlayerPawn", "m_bInBuyZone", o.in_buy_zone);
  o.aim_punch_services = field("C_CSPlayerPawn", "m_pAimPunchServices", o.aim_punch_services);
  o.max_speed = field("C_BaseEntity", "m_flMaxspeed", o.max_speed);
  o.flash_duration = field("C_CSPlayerPawnBase", "m_flFlashDuration", o.flash_duration);
  o.weapon_services = field("C_BasePlayerPawn", "m_pWeaponServices", o.weapon_services);
  o.controller = field("C_BasePlayerPawn", "m_hController", o.controller);
  o.is_local_controller =
      field("CBasePlayerController", "m_bIsLocalPlayerController", o.is_local_controller);
  o.connected = field("CBasePlayerController", "m_iConnected", o.connected);
  o.tick_base = field("CBasePlayerController", "m_nTickBase", o.tick_base);
  o.pawn = field("CBasePlayerController", "m_hPawn", o.pawn);
  o.player_name = field("CBasePlayerController", "m_iszPlayerName", o.player_name);
  o.ping = field("CCSPlayerController", "m_iPing", o.ping);
  o.player_pawn = field("CCSPlayerController", "m_hPlayerPawn", o.player_pawn);
  o.pawn_has_defuser = field("CCSPlayerController", "m_bPawnHasDefuser", o.pawn_has_defuser);
  o.pawn_has_helmet = field("CCSPlayerController", "m_bPawnHasHelmet", o.pawn_has_helmet);
  o.money_services = field("CCSPlayerController", "m_pInGameMoneyServices", o.money_services);
  o.account =
      field("CCSPlayerController_InGameMoneyServices", "m_iAccount", o.account);
  o.active_weapon = field("CPlayer_WeaponServices", "m_hActiveWeapon", o.active_weapon);
  o.clip1 = field("C_BasePlayerWeapon", "m_iClip1", o.clip1);
  o.reserve_ammo = field("C_BasePlayerWeapon", "m_pReserveAmmo", o.reserve_ammo);
  // CGameSceneNode::m_vecAbsOrigin — the class lives in the server schema.
  o.abs_origin = 0xc8;
  // C_CSGameRules — the authoritative round/game phase (see `read_rules`).
  o.rules_freeze_period = field("C_CSGameRules", "m_bFreezePeriod", o.rules_freeze_period);
  o.rules_warmup_period = field("C_CSGameRules", "m_bWarmupPeriod", o.rules_warmup_period);
  o.rules_freeze_time = field("C_CSGameRules", "m_iFreezeTime", o.rules_freeze_time);
  o.rules_round_time = field("C_CSGameRules", "m_iRoundTime", o.rules_round_time);
  o.rules_round_start_time = field("C_CSGameRules", "m_fRoundStartTime", o.rules_round_start_time);
  o.rules_restart_round_time =
      field("C_CSGameRules", "m_flRestartRoundTime", o.rules_restart_round_time);
  o.rules_game_restart = field("C_CSGameRules", "m_bGameRestart", o.rules_game_restart);
  o.rules_time_until_next_phase =
      field("C_CSGameRules", "m_timeUntilNextPhaseStarts", o.rules_time_until_next_phase);
  o.rules_game_phase = field("C_CSGameRules", "m_gamePhase", o.rules_game_phase);
  o.rules_total_rounds_played =
      field("C_CSGameRules", "m_totalRoundsPlayed", o.rules_total_rounds_played);
  o.rules_has_match_started =
      field("C_CSGameRules", "m_bHasMatchStarted", o.rules_has_match_started);
  o.rules_bomb_planted = field("C_CSGameRules", "m_bBombPlanted", o.rules_bomb_planted);
  o.rules_bomb_dropped = field("C_CSGameRules", "m_bBombDropped", o.rules_bomb_dropped);
  o.rules_round_win_status = field("C_CSGameRules", "m_iRoundWinStatus", o.rules_round_win_status);
  o.rules_round_win_reason = field("C_CSGameRules", "m_eRoundWinReason", o.rules_round_win_reason);
  o.rules_overtime_playing = field("C_CSGameRules", "m_nOvertimePlaying", o.rules_overtime_playing);
  o.rules_game_paused = field("CGameRules", "m_bGamePaused", o.rules_game_paused);
  o.rules_t_cant_buy = field("C_CSGameRules", "m_bTCantBuy", o.rules_t_cant_buy);
  o.rules_ct_cant_buy = field("C_CSGameRules", "m_bCTCantBuy", o.rules_ct_cant_buy);
  o.rules_proxy_game_rules = field("C_CSGameRulesProxy", "m_pGameRules", o.rules_proxy_game_rules);
  return o;
}

auto StateReader::attach() noexcept -> std::optional<StateReader> {
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

auto StateReader::view_matrix(int slot) const -> Matrix4x4 {
  Matrix4x4 matrix;
  const std::uintptr_t base = global_address(client_base_, "view_matrix");
  if (base == 0 || slot < 0) {
    return matrix;
  }
  (void)read_bytes(base + static_cast<std::uintptr_t>(slot) * 0x40, matrix.m.data(),
                   matrix.m.size() * sizeof(float));
  return matrix;
}

auto StateReader::read_player(int index, std::uintptr_t controller) const -> PlayerSnapshot {
  const FieldOffsets& o = offsets_;
  PlayerSnapshot player;
  player.index = index;
  player.controller = controller;

  // Controller first: it carries the identity (name, team, flags, local flag)
  // even when the pawn is dead or not yet spawned.
  player.name = read_inline_string(controller + o.player_name, 128);
  std::uint8_t team = 0;
  (void)read(controller + o.team, team);
  player.team = team;
  std::uint8_t is_local = 0;
  (void)read(controller + o.is_local_controller, is_local);
  player.is_local = is_local != 0;

  std::uint8_t defuser = 0;
  std::uint8_t helmet = 0;
  (void)read(controller + o.pawn_has_defuser, defuser);
  (void)read(controller + o.pawn_has_helmet, helmet);
  player.connected = read_or<std::int32_t>(controller + o.connected, 0);
  player.tick_base = read_or<std::int32_t>(controller + o.tick_base, 0);
  player.defuser = defuser != 0;
  player.helmet = helmet != 0;
  player.ping = static_cast<int>(read_or<std::uint32_t>(controller + o.ping, 0));
  const auto money = read_or<std::uintptr_t>(controller + o.money_services, 0);
  if (money != 0) {
    player.money = read_or<std::int32_t>(money + o.account, 0);
  }

  auto handle = read_or<std::uint32_t>(controller + o.player_pawn, 0);
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
  player.flags = read_or<std::uint32_t>(pawn + o.flags, 0);
  player.on_ground = (player.flags & 1U) != 0;  // FL_ONGROUND
  player.health = read_or<std::int32_t>(pawn + o.health, 0);
  player.max_health = read_or<std::int32_t>(pawn + o.max_health, 0);
  player.armor = read_or<std::int32_t>(pawn + o.armor, 0);
  player.flash = read_or<float>(pawn + o.flash_duration, 0.0F);
  player.alive = life_state == 0 && player.health > 0;

  std::uint8_t scoped = 0;
  std::uint8_t defusing = 0;
  std::uint8_t in_buy_zone = 0;
  (void)read(pawn + o.scoped, scoped);
  (void)read(pawn + o.defusing, defusing);
  (void)read(pawn + o.in_buy_zone, in_buy_zone);
  player.scoped = scoped != 0;
  player.defusing = defusing != 0;
  player.in_buy_zone = in_buy_zone != 0;

  (void)read(pawn + o.eye_angles, player.eye_angles);
  (void)read(pawn + o.velocity, player.velocity);
  player.simulation_time = read_or<float>(pawn + o.simulation_time, 0.0F);
  player.max_speed = read_or<float>(pawn + o.max_speed, 0.0F);

  // Recoil punch: `m_pAimPunchServices -> m_predictableBaseAngle` (QAngle, degrees).
  {
    const auto punch = read_or<std::uintptr_t>(pawn + o.aim_punch_services, 0);
    if (punch != 0) {
      float angles[3] = {0.0F, 0.0F, 0.0F};
      if (read(punch + 0x50, angles)) {
        player.aim_punch_pitch = angles[0];
        player.aim_punch_yaw = angles[1];
      }
    }
  }

  const auto scene = read_or<std::uintptr_t>(pawn + o.scene_node, 0);
  Vec3 view_offset{};
  if (scene != 0) {
    (void)read(scene + o.abs_origin, player.origin);
  }
  (void)read(pawn + o.view_offset, view_offset);
  player.eye_origin = player.origin + view_offset;

  // Collision bounds (local space; the ESP projects origin + bounds).
  const auto collision = read_or<std::uintptr_t>(pawn + o.collision, 0);
  if (collision != 0) {
    (void)read(collision + o.collision_mins, player.mins);
    (void)read(collision + o.collision_maxs, player.maxs);
  }

  // Active weapon: name + ammo.
  const auto services = read_or<std::uintptr_t>(pawn + o.weapon_services, 0);
  if (services != 0) {
    const auto weapon_handle = read_or<std::uint32_t>(services + o.active_weapon, 0);
    const std::uintptr_t weapon = entities_.resolve(weapon_handle);
    if (weapon != 0) {
      player.weapon_entity = weapon;
      player.clip = read_or<std::int32_t>(weapon + o.clip1, 0);
      player.reserve = read_or<std::int32_t>(weapon + o.reserve_ammo, 0);
      player.weapon = entities_.designer_name(static_cast<int>(weapon_handle & 0x7fffU));
      player.has_bomb = player.weapon == "weapon_c4";
      // `C_BaseEntity::m_nSubclassID` (+0x08) is the weapon's
      // `CCSWeaponBaseVData*` (`docs/re/aim.md`; the ragebot's damage model).
      player.weapon_vdata = read_or<std::uintptr_t>(weapon + 0x388, 0);
      // The weapon's **fire capability** (`docs/ragebot.md` §"the fire path").
      if (player.weapon_vdata != 0) {
        const VDataOffsets& vdata = vdata_offsets();
        // `CCSWeaponBaseVData::m_WeaponType` — the ragebot excludes non-firearms.
        player.weapon_type = read_or<std::int32_t>(player.weapon_vdata + vdata.weapon_type, -1);
        // `m_bIsFullAuto` — the weapon's fire mode. The mode is only **known**
        // when the field could be read: a VData that could not be read leaves
        // the mode "unknown", and the fire path then treats the weapon as
        // **semi-automatic** (the conservative mode).
        std::uint8_t full_auto = 0;
        player.weapon_fire_mode_known =
            cs2::game::read(player.weapon_vdata + vdata.full_auto, full_auto);
        player.weapon_full_auto = full_auto != 0;
        // `m_flCycleTime` — the weapon's fire interval in seconds. The field is
        // a **firing-mode** float *pair* (`FiringModeFloat`: `0x738` and
        // `0x73c`), and the schema dump does not describe the struct's members,
        // so which of the two the engine uses for the weapon's mode is **not**
        // established by the repo. The payload reads the **first** one — that
        // is the payload's reading, not a claim about the engine's. The value
        // is only kept when it is plausible: no weapon fires slower than
        // 20 ms or faster than two seconds.
        float cycle = 0.0F;
        if (cs2::game::read(player.weapon_vdata + vdata.cycle_time, cycle)) {
          player.weapon_cycle_time = (cycle > 0.02F && cycle < 2.0F) ? cycle : 0.0F;
        }
      }
    }
  }
  return player;
}

auto StateReader::read_rules() const -> RulesSnapshot {
  const FieldOffsets& o = offsets_;
  RulesSnapshot rules;
  // `*(game_rules)` is the level's `C_CSGameRules`; the pointer is null at the
  // main menu and until a level constructs its rules (`docs/re/client-dll.md`
  // §"`game_rules`"), which is exactly the "a level is up" signal.
  const std::uintptr_t base = global(client_base_, "game_rules");
  if (base == 0) {
    return rules;
  }
  rules.loaded = true;
  const auto flag = [base](std::uint32_t offset) -> bool {
    return read_or<std::uint8_t>(base + offset, 0) != 0;
  };
  rules.has_match_started = flag(o.rules_has_match_started);
  rules.freeze_period = flag(o.rules_freeze_period);
  rules.warmup_period = flag(o.rules_warmup_period);
  rules.game_restart = flag(o.rules_game_restart);
  rules.bomb_planted = flag(o.rules_bomb_planted);
  rules.bomb_dropped = flag(o.rules_bomb_dropped);
  rules.t_cant_buy = flag(o.rules_t_cant_buy);
  rules.ct_cant_buy = flag(o.rules_ct_cant_buy);
  rules.game_phase = read_or<std::int32_t>(base + o.rules_game_phase, 0);
  rules.round_time = read_or<std::int32_t>(base + o.rules_round_time, 0);
  rules.freeze_time = read_or<std::int32_t>(base + o.rules_freeze_time, 0);
  rules.round_win_status = read_or<std::int32_t>(base + o.rules_round_win_status, 0);
  rules.round_win_reason = read_or<std::int32_t>(base + o.rules_round_win_reason, 0);
  rules.total_rounds_played = read_or<std::int32_t>(base + o.rules_total_rounds_played, 0);
  rules.overtime_playing = read_or<std::int32_t>(base + o.rules_overtime_playing, 0);
  rules.game_paused = flag(o.rules_game_paused);
  rules.match_start_time = read_or<float>(base + o.rules_match_start_time, 0.0F);
  rules.round_start_time = read_or<float>(base + o.rules_round_start_time, 0.0F);
  rules.restart_round_time = read_or<float>(base + o.rules_restart_round_time, 0.0F);
  rules.game_start_time = read_or<float>(base + o.rules_game_start_time, 0.0F);
  rules.warmup_period_start = read_or<float>(base + o.rules_warmup_period_start, 0.0F);
  rules.time_until_next_phase = read_or<float>(base + o.rules_time_until_next_phase, 0.0F);
  return rules;
}

auto StateReader::snapshot() const -> Snapshot {
  Snapshot snapshot;
  if (!ready()) {
    return snapshot;
  }

  // The rules are readable even when there is no local player yet (a level that
  // is still connecting), so read them before the local-controller bail-outs.
  const std::uintptr_t rules_ptr = global(client_base_, "game_rules");
  snapshot.rules = read_rules();
  snapshot.level.rules = rules_ptr;

  const std::uintptr_t local_controller = global(client_base_, "local_player_controllers");
  const auto controller_vtable =
      local_controller != 0 ? read_or<std::uintptr_t>(local_controller, 0) : 0;
  snapshot.valid = local_controller != 0;

  // All player controllers share the local controller's vtable; enumerate them
  // from the entity table by matching it (no per-build vtable RVA is
  // hard-coded). The same walk answers the main-screen rule: the level's
  // `C_CSGameRulesProxy` entity (whose `m_pGameRules` is `rules_ptr`) exists only
  // in a level, while the `game_rules` global is non-null at the main menu too.
  for (int page = 0; page < EntitySystem::k_page_count; ++page) {
    const std::uintptr_t page_base = entities_.page(page);
    if (page_base == 0) {
      continue;
    }
    for (int slot = 0; slot < EntitySystem::k_slots_per_page; ++slot) {
      const std::uintptr_t entry =
          page_base + EntitySystem::k_identity_stride * static_cast<std::uintptr_t>(slot);
      const auto instance = read_or<std::uintptr_t>(entry, 0);
      if (instance == 0) {
        continue;
      }
      snapshot.level.identities += 1;
      if (!snapshot.level.up && rules_ptr != 0 &&
          read_or<std::uintptr_t>(instance + offsets_.rules_proxy_game_rules, 0) == rules_ptr) {
        snapshot.level.up = true;
        snapshot.level.proxy = instance;
      }
      if (controller_vtable == 0 || read_or<std::uintptr_t>(instance, 0) != controller_vtable) {
        continue;
      }
      const int index = (page << EntitySystem::k_page_shift) | slot;
      PlayerSnapshot player = read_player(index, instance);
      player.is_local = player.is_local || instance == local_controller;
      if (player.is_local) {
        snapshot.local = player;
        snapshot.local_pawn = player.pawn;
      }
      snapshot.players.push_back(std::move(player));
    }
  }

  // Measure everyone against the local player.
  if (snapshot.local.has_value()) {
    snapshot.level.local_spawned = snapshot.local->connected;
    for (PlayerSnapshot& player : snapshot.players) {
      player.distance = distance(player.origin, snapshot.local->origin);
    }
  }
  return snapshot;
}

}  // namespace cs2::game
