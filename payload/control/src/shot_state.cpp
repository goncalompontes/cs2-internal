#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2/control/shot_state.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "cs2/control/input.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/pattern.hpp"
#include "cs2/game/schema.hpp"
namespace cs2::control {
const char* shot_block_name(ShotBlock b) noexcept {
  switch (b) {
    case ShotBlock::ready:
      return "ready";
    case ShotBlock::no_data:
      return "weapon data";
    case ShotBlock::no_target:
      return "no target";
    case ShotBlock::cooldown:
      return "cooldown";
    case ShotBlock::reload:
      return "reload";
    case ShotBlock::empty:
      return "empty";
    case ShotBlock::accuracy:
      return "accuracy/damage";
    case ShotBlock::history:
      return "history";
    case ShotBlock::recoil:
      return "recoil";
    case ShotBlock::duplicate:
      return "duplicate";
    case ShotBlock::stale:
      return "stale proposal";
  }
  return "unknown";
}
ShotBlock ShotGate::check(const ShotState& s) noexcept {
  if (!s.valid || !s.pawn || !s.weapon || s.tick_base <= 0 || s.client_tick <= 0)
    return ShotBlock::no_data;
  if (s.pawn != pawn_ || s.weapon != weapon_ || s.tick_base < last_tick_) {
    pawn_ = s.pawn;
    weapon_ = s.weapon;
    last_tick_ = std::numeric_limits<int>::min();
    last_command_ = -1;
  }
  if (s.reloading) return ShotBlock::reload;
  if (s.clip <= 0) return ShotBlock::empty;
  if (s.command == last_command_) return ShotBlock::duplicate;
  if (s.client_tick < s.next_primary || static_cast<std::int64_t>(s.tick_base) - last_tick_ < 2)
    return ShotBlock::cooldown;
  return ShotBlock::ready;
}
void ShotGate::commit(const ShotState& s) noexcept {
  pawn_ = s.pawn;
  weapon_ = s.weapon;
  last_tick_ = s.tick_base;
  last_command_ = s.command;
}
bool NativeShotSource::prepare(game::ModuleInfo module) noexcept {
  if (spread_ && inaccuracy_ && calculate_ && shoot_position_ && module.base == module_.base)
    return true;
  if (!module.valid() || attempted_base_ == module.base) return false;
  attempted_base_ = module.base;
  const auto spread = game::unique_code_match(
      module, "486391????????488B81????????85D278104883FA02730AF30F10849050070000C3");
  const auto inaccuracy =
      game::unique_code_match(module, "48895C24??5556574881EC????????440F298424");
  // Current weapon-service vector getter: delegates to owner pawn EyePosition,
  // returns the supplied vector pointer in RAX. Locate it, then resolve its live slot.
  const auto shoot_position = game::unique_code_match(
      module, "40534883EC20488B4938488BDA488B01FF9068050000488BC34883C4205BC3");
  const auto site = game::unique_code_match(module, "28F3440F11442420E8????????488D85B0000000");
  std::int32_t rel = 0;
  if (!spread || !inaccuracy || !shoot_position || !site || !game::read(site + 9, rel) ||
      !module.contains(site + 13 + rel))
    return false;
  module_ = module;
  shoot_position_ = shoot_position;
  spread_ = reinterpret_cast<decltype(spread_)>(spread);
  inaccuracy_ = reinterpret_cast<decltype(inaccuracy_)>(inaccuracy);
  calculate_ = reinterpret_cast<decltype(calculate_)>(site + 13 + rel);
  return true;
}
std::optional<unsigned> NativeShotSource::shooting_position_slot(
    std::uintptr_t table) const noexcept {
  if (!shoot_position_ || !table) return {};
  for (unsigned i = 0; i < 48; ++i) {
    std::uintptr_t function = 0;
    if (!game::read(table + i * sizeof(std::uintptr_t), function)) return {};
    if (function == shoot_position_) return i;
  }
  return {};
}
bool NativeShotSource::schema_bindings_match(std::uint32_t spread,
                                             std::uint32_t mode) const noexcept {
  std::uint32_t spread_operand = 0, mode_operand = 0;
  const auto code = reinterpret_cast<std::uintptr_t>(spread_);
  return code && game::read(code + 0x1d, spread_operand) && game::read(code + 3, mode_operand) &&
         spread_operand == spread && mode_operand == mode;
}
bool NativeShotSource::fields() noexcept {
  if (fields_ready_) return true;
  if (GetTickCount64() < fields_retry_ms_) return false;
  fields_retry_ms_ = GetTickCount64() + 1000;
  auto field = [this](const char* t, const char* f, std::uint32_t& out) {
    auto v = game::schema_field(t, f);
    if (!v) {
      std::snprintf(diagnostic_, sizeof(diagnostic_), "missing schema %s::%s", t, f);
      return false;
    }
    out = *v;
    return true;
  };
  fields_ready_ = field("C_BasePlayerWeapon", "m_iClip1", clip_) &&
                  field("C_CSWeaponBase", "m_bInReload", reload_) &&
                  field("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick", next_) &&
                  field("CBasePlayerController", "m_nTickBase", tick_) &&
                  field("C_BasePlayerPawn", "m_pWeaponServices", services_) &&
                  field("C_CSWeaponBase", "m_flRecoilIndex", recoil_) &&
                  field("CCSWeaponBaseVData", "m_nNumBullets", pellets_) &&
                  field("C_EconEntity", "m_AttributeManager", attributes_) &&
                  field("C_AttributeContainer", "m_Item", item_) &&
                  field("C_EconItemView", "m_iItemDefinitionIndex", definition_) &&
                  field("CCSWeaponBaseVData", "m_WeaponType", type_) &&
                  field("C_CSWeaponBase", "m_flTurningInaccuracyDelta", accuracy_begin_) &&
                  field("C_CSWeaponBase", "m_flRecoilIndex", accuracy_end_);
  fields_ready_ = fields_ready_ && accuracy_end_ >= accuracy_begin_ &&
                  accuracy_end_ - accuracy_begin_ + 4 <= 0x100;
  const auto live_spread = game::schema_field("CCSWeaponBaseVData", "m_flSpread");
  const auto live_mode = game::schema_field("C_CSWeaponBase", "m_weaponMode");
  if (fields_ready_) {
    fields_ready_ = live_spread && live_mode &&
                    schema_bindings_match(live_spread.value_or(0), live_mode.value_or(0));
    if (!fields_ready_)
      std::snprintf(diagnostic_, sizeof(diagnostic_),
                    "spread getter/schema mismatch (spread=%x mode=%x)", live_spread.value_or(0),
                    live_mode.value_or(0));
  }
  if (fields_ready_)
    game::append_log("[rage-cycle] live weapon timing and spread schema operands verified");
  return fields_ready_;
}
namespace {
bool sample_engine(float (*spread)(std::uintptr_t),
                   float (*inaccuracy)(std::uintptr_t, float*, float*),
                   void (*calculate)(std::int16_t, int, int, std::uint32_t, float, float, float,
                                     float*, float*),
                   std::uintptr_t weapon, int item, int pellets, float recoil, float* x, float* y,
                   float* spread_out, float* accuracy_out) noexcept {
  __try {
    const float s = spread(weapon), a = inaccuracy(weapon, nullptr, nullptr);
    *spread_out = s;
    *accuracy_out = a;
    if (!std::isfinite(s) || !std::isfinite(a) || s < 0 || a < 0 || s > 2 || a > 2) return false;
    // Spread output uses the engine's left basis; the heatmap uses right.
    for (unsigned i = 0; i < 32; ++i)
      calculate(static_cast<std::int16_t>(item), pellets, 0, i + 1, a, s, recoil, &x[i], &y[i]);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool shoot_eye(std::uintptr_t services, std::uintptr_t function, game::Vec3* eye) noexcept {
  __try {
    game::Vec3 storage{};
    const auto result = reinterpret_cast<game::Vec3* (*)(std::uintptr_t, game::Vec3*)>(function)(
        services, &storage);
    return result && game::read(reinterpret_cast<std::uintptr_t>(result), *eye);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool writable_message(std::uintptr_t p, std::size_t bytes) noexcept {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info)) != sizeof(info) ||
      info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      !(info.Protect &
        (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
    return false;
  return p >= reinterpret_cast<std::uintptr_t>(info.BaseAddress) && p + bytes >= p &&
         p + bytes <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}
}  // namespace
bool NativeShotSource::sample(const game::PlayerSnapshot& local, void* command, ShotState& state,
                              game::Vec3& eye, std::vector<aim::KernelSample>& kernel) noexcept {
  state = {};
  debug_ = {};
  debug_.local_eye = local.eye_origin;
  debug_.bindings = spread_ && inaccuracy_ && calculate_;
  kernel.clear();
  if (command) (void)game::read(reinterpret_cast<std::uintptr_t>(command) + 8, state.command);
  state.pawn = local.pawn;
  state.weapon = local.weapon_entity;
  if (!debug_.bindings) return false;
  debug_.schema = fields();
  if (!debug_.schema) return false;
  if (!command || !local.alive || !local.weapon_entity || !local.weapon_vdata) {
    std::snprintf(diagnostic_, sizeof(diagnostic_), "local weapon/controller unavailable");
    return false;
  }
  const auto cmd = reinterpret_cast<std::uintptr_t>(command);
  std::uintptr_t base = 0, services = 0, vtable = 0, eye_fn = 0;
  std::uint8_t reload = 1;
  int pellets = 0, type = 0;
  float recoil = 0;
  std::uint16_t item = 0;
  state.pawn = local.pawn;
  state.weapon = local.weapon_entity;
  std::snprintf(diagnostic_, sizeof(diagnostic_), "weapon fields/type/pellet count");
  if (!game::read(cmd + 8, state.command) || !game::read(cmd + 0x40, base) ||
      !game::read(base + 0x54, state.client_tick) ||
      !game::read(local.controller + tick_, state.tick_base) ||
      !game::read(state.weapon + next_, state.next_primary) ||
      !game::read(state.weapon + clip_, state.clip) ||
      !game::read(state.weapon + reload_, reload) || !game::read(state.weapon + recoil_, recoil) ||
      !game::read(state.weapon + attributes_ + item_ + definition_, item) ||
      !game::read(local.weapon_vdata + pellets_, pellets) ||
      !game::read(local.weapon_vdata + type_, type))
    return false;
  debug_.type = type;
  debug_.item = item;
  debug_.pellets = pellets;
  debug_.recoil = recoil;
  state.reloading = reload != 0;
  if (type < 1 || type > 6 || pellets < 1 || pellets > 32 || !std::isfinite(recoil)) {
    std::snprintf(diagnostic_, sizeof(diagnostic_),
                  "unsupported/invalid weapon: type=%d item=%u pellets=%d clip=%d recoil=%.3f",
                  type, item, pellets, state.clip, recoil);
    return false;
  }
  debug_.weapon = true;
  std::snprintf(diagnostic_, sizeof(diagnostic_), "weapon-service shooting position");
  if (!game::read(local.pawn + services_, services) || !services) {
    std::snprintf(diagnostic_, sizeof(diagnostic_), "weapon services missing/unreadable");
    return false;
  }
  debug_.services = services;
  if (!game::read(services, vtable)) {
    std::snprintf(diagnostic_, sizeof(diagnostic_), "weapon-service vtable unreadable");
    return false;
  }
  const auto slot = shooting_position_slot(vtable);
  if (!slot || !game::read(vtable + *slot * 8, eye_fn)) {
    std::snprintf(diagnostic_, sizeof(diagnostic_),
                  "verified shoot-position getter absent from weapon-service vtable");
    return false;
  }
  debug_.eye_slot = static_cast<int>(*slot);
  std::uintptr_t owner = 0;
  if (!game::read(services + 0x38, owner) || owner != local.pawn) {
    std::snprintf(diagnostic_, sizeof(diagnostic_),
                  "shoot-position getter owner is not the local pawn");
    return false;
  }
  debug_.eye_function = eye_fn;
  debug_.eye_function_in_client = module_.contains(eye_fn);
  if (!debug_.eye_function_in_client) {
    std::snprintf(diagnostic_, sizeof(diagnostic_), "shoot-eye function is outside client module");
    return false;
  }
  eye = {};
  debug_.eye_call = shoot_eye(services, eye_fn, &eye);
  debug_.returned_eye = eye;
  debug_.eye_distance = game::distance(eye, local.eye_origin);
  if (!debug_.eye_call) {
    std::snprintf(diagnostic_, sizeof(diagnostic_),
                  "shoot-eye call failed or returned an unreadable vector");
    return false;
  }
  if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z) ||
      debug_.eye_distance > 128) {
    std::snprintf(diagnostic_, sizeof(diagnostic_),
                  "shoot-eye result invalid: (%.1f %.1f %.1f), local delta %.1f (limit 128)", eye.x,
                  eye.y, eye.z, debug_.eye_distance);
    return false;
  }
  debug_.eye = true;
  // Prefer a command's own shooting position when it also supplies a valid player clock.
  // Otherwise keep the engine weapon-service eye and preserve per-entry player timing.
  unsigned history_count = 0, history_capacity = 0, history_bits = 0;
  std::uintptr_t history_rep = 0, entry = 0, position = 0;
  int player_tick = 0;
  float player_fraction = 0;
  game::Vec3 history_eye{};
  if (game::read(cmd + 0x30, history_count) && history_count > 0 && history_count <= 64 &&
      game::read(cmd + 0x38, history_rep) && game::read(history_rep, history_capacity) &&
      history_capacity >= history_count &&
      game::read(history_rep + 8 + (history_count - 1) * 8, entry) &&
      game::read(entry + 0x10, history_bits) && (history_bits & 0x820u) == 0x820u &&
      game::read(entry + 0x40, position) && game::read(position + 0x18, history_eye) &&
      game::read(entry + 0x68, player_tick) && game::read(entry + 0x6c, player_fraction) &&
      player_tick > 0 && player_tick <= state.tick_base + 1 &&
      player_tick >= state.tick_base - 64 && std::isfinite(player_fraction) &&
      player_fraction >= 0 && player_fraction < 1 && std::isfinite(history_eye.x) &&
      std::isfinite(history_eye.y) && std::isfinite(history_eye.z) &&
      game::distance(history_eye, eye) <= 16) {
    eye = history_eye;
    state.history_eye = true;
  }
  // GetInaccuracy can touch this accuracy block. Restore it even when the guarded call faults.
  std::snprintf(diagnostic_, sizeof(diagnostic_), "engine spread sampling/accuracy restore");
  std::array<std::byte, 0x100> backup{};
  const auto size = accuracy_end_ - accuracy_begin_ + 4;
  if (!game::read_bytes(state.weapon + accuracy_begin_, backup.data(), size)) return false;
  float x[32]{}, y[32]{};
  const bool sampled = sample_engine(spread_, inaccuracy_, calculate_, state.weapon, item, pellets,
                                     recoil, x, y, &debug_.spread_value, &debug_.inaccuracy);
  SIZE_T restored = 0;
  if (!WriteProcessMemory(GetCurrentProcess(),
                          reinterpret_cast<void*>(state.weapon + accuracy_begin_), backup.data(),
                          size, &restored) ||
      restored != size || !sampled)
    return false;
  for (unsigned i = 0; i < 32; ++i) {
    if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || std::fabs(x[i]) > 4 ||
        std::fabs(y[i]) > 4) {
      kernel.clear();
      return false;
    }
    kernel.push_back({{-x[i], y[i]}, 1.0F / 32});
  }
  state.reloading = reload != 0;
  debug_.spread = true;
  debug_.samples = static_cast<int>(kernel.size());
  debug_.history_eye = state.history_eye;
  state.valid = true;
  std::snprintf(diagnostic_, sizeof(diagnostic_), "ready");
  return true;
}
ShotTiming command_timing(void* command, float simulation_time) noexcept {
  ShotTiming t;
  if (!command || !std::isfinite(simulation_time) || simulation_time <= 0 ||
      simulation_time > 10000000)
    return t;
  t.render_tick = static_cast<int>(std::floor(simulation_time * 64 + 0.5F)) + 1;
  t.valid = true;
  const auto cmd = reinterpret_cast<std::uintptr_t>(command);
  unsigned count = 0;
  std::uintptr_t rep = 0, entry = 0;
  if (game::read(cmd + 0x30, count) && count > 0 && count <= 64 && game::read(cmd + 0x38, rep) &&
      game::read(rep + 8 + (count - 1) * 8, entry) && game::read(entry + 0x68, t.player_tick) &&
      game::read(entry + 0x6c, t.player_fraction))
    t.player_valid = false;  // Only an explicitly matched shoot-eye may replace player timing.
  return t;
}
bool stamp_shot_timing(void* command, const ShotTiming& t) noexcept {
  if (!t.valid) return true;  // explicit synthetic/offline adapters may omit server timing
  if (t.render_tick <= 0 || !std::isfinite(t.render_fraction) || t.render_fraction < 0 ||
      t.render_fraction >= 1 ||
      (t.player_valid && (t.player_tick <= 0 || !std::isfinite(t.player_fraction) ||
                          t.player_fraction < 0 || t.player_fraction >= 1)))
    return false;
  const auto cmd = reinterpret_cast<std::uintptr_t>(command);
  unsigned count = 0, capacity = 0;
  std::uintptr_t rep = 0;
  if (!game::read(cmd + 0x30, count) || count == 0 || count > 64 || !game::read(cmd + 0x38, rep) ||
      !game::read(rep, capacity) || capacity < count)
    return false;
  for (unsigned i = 0; i < count; ++i) {
    std::uintptr_t entry = 0;
    if (!game::read(rep + 8 + i * 8, entry) || !writable_message(entry, 0x70)) return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    std::uintptr_t entry = 0;
    if (!game::read(rep + 8 + i * 8, entry)) return false;
    std::memcpy(reinterpret_cast<void*>(entry + 0x60), &t.render_tick, 4);
    std::memcpy(reinterpret_cast<void*>(entry + 0x64), &t.render_fraction, 4);
    *reinterpret_cast<unsigned*>(entry + 0x10) |= 0x200u | 0x400u;
    if (t.player_valid) {
      std::memcpy(reinterpret_cast<void*>(entry + 0x68), &t.player_tick, 4);
      std::memcpy(reinterpret_cast<void*>(entry + 0x6c), &t.player_fraction, 4);
      *reinterpret_cast<unsigned*>(entry + 0x10) |= 0x800u | 0x1000u;
    }
  }
  return true;
}
}  // namespace cs2::control
