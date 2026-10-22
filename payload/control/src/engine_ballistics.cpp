#include "cs2/control/engine_ballistics.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/schema.hpp"
#include "cs2/game/pattern.hpp"
#include "offsets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace cs2::control {
namespace {
bool run_trace(const engine_trace::Api* api, engine_trace::Data* data,
               std::uintptr_t local, int team, const aim::Vec3* eye,
               const aim::Vec3* delta, const aim::WeaponProfile* weapon) noexcept {
#if defined(_WIN32)
  __try {
#endif
    engine_trace::Filter filter{};
    api->filter(&filter, local, 0x1c300b, 3, 15);
    api->init(data, eye, delta, &filter, 4, true);
    if (data->hit_count < 0 || data->hit_count > 8 || data->hits != data->hit_elements.data() ||
        data->contacts != data->elements.data()) return false;
    api->bullet(data, weapon->damage, weapon->penetration, weapon->range_modifier, 4, team, 0);
    return true;
#if defined(_WIN32)
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#endif
}
std::uintptr_t resolve_entity(void* context, std::uint32_t handle) {
  const auto* entities = static_cast<game::EntitySystem*>(context);
  const auto identity = entities->identity(static_cast<int>(handle & 0x7fff));
  std::uint32_t actual_handle = 0;
  std::uintptr_t instance = 0;
  if (!identity || !game::read(identity + 0x10, actual_handle) || actual_handle != handle ||
      !game::read(identity, instance)) return 0;
  return instance;
}
}
bool EngineBallistics::initialize() noexcept {
  if (ready()) return true;
#if defined(_WIN32)
  const auto module = game::find_module("client.dll");
  if (!module.valid() || attempted_base_ == module.base) return false;
  attempted_base_ = module.base;
  // Signatures cross-checked against the installed PE. The initializer includes
  // its trace.start (+0x1cf8) write to exclude a superficially similar function.
  const auto filter = game::unique_code_match(module, "48895C24??48897424??574883EC??0FB641??33FF24");
  const auto init = game::unique_code_match(module,
      "48895C24??48896C24??48897424??57415641574883EC??F20F10024D8D7108F20F1181F81C0000");
  const auto bullet = game::unique_code_match(module,
      "40535741564883EC508B8424????????4C8BF14881C1041D0000");
  const auto* offset = sdk::find_offset("entity_list");
  std::uintptr_t blocks = 0;
  if (!filter || !init || !bullet || !offset || !game::read(module.base + offset->rva, blocks) || !blocks) {
    game::append_log("[ballistics] live trace binding unavailable; synthetic shots are blocked");
    return false;
  }
  entities_ = game::EntitySystem(blocks);
  api_ = {reinterpret_cast<decltype(api_.filter)>(filter), reinterpret_cast<decltype(api_.init)>(init),
          reinterpret_cast<decltype(api_.bullet)>(bullet), &resolve_entity, &entities_};
  const auto punch_site = game::unique_code_match(module,
      "498B8E????????488D542420E8????????F30F1015????????");
  const auto services_offset = game::schema_field("C_CSPlayerPawn", "m_pAimPunchServices");
  std::uint32_t operand = 0;
  std::int32_t relative = 0;
  if (punch_site && services_offset && game::read(punch_site + 3, operand) &&
      operand == *services_offset && game::read(punch_site + 13, relative)) {
    const auto target = punch_site + 17 + relative;
    if (module.contains(target)) {
      punch_ = reinterpret_cast<decltype(punch_)>(target);
      punch_services_offset_ = *services_offset;
      game::append_log("[ballistics] recoil query bound with matching live services offset");
    }
  }
  game::append_log("[ballistics] engine bullet trace bound (unique executable signatures)");
#endif
  return ready();
}
bool EngineBallistics::recoil(std::uintptr_t pawn, float out[3]) const noexcept {
  if (!punch_ || !pawn || !out) return false;
  std::uintptr_t services = 0;
  if (!game::read(pawn + punch_services_offset_, services) || !services) return false;
  aim::Vec3 value{};
#if defined(_WIN32)
  __try { punch_(services, &value); }
  __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
  punch_(services, &value);
#endif
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      std::fabs(value.x) > 45 || std::fabs(value.y) > 45) return false;
  out[0] = value.x; out[1] = value.y; out[2] = value.z;
  return true;
}
aim::WorldDamageQuery EngineBallistics::query(std::uintptr_t local, int team, std::uintptr_t target) {
  if (!ready()) return {};
  return [this, local, team, target](aim::Vec3 eye, aim::Vec3 direction, float,
                                   const aim::WeaponProfile& weapon) {
    return evaluate(local, team, target, eye, direction, weapon);
  };
}
aim::WorldDamage EngineBallistics::evaluate(std::uintptr_t local, int team, std::uintptr_t target,
    aim::Vec3 eye, aim::Vec3 direction, const aim::WeaponProfile& weapon) noexcept {
  if (!ready() || !local || !target || !std::isfinite(weapon.damage) || weapon.damage <= 0 ||
      !std::isfinite(weapon.range) || weapon.range <= 0) return {};
  direction = game::normalized(direction);
  if (game::length(direction) < 0.5F) return {};
  const auto delta = direction * weapon.range;
  engine_trace::Data data{};
  data.contacts = data.elements.data(); data.hits = data.hit_elements.data();
  if (!run_trace(&api_, &data, local, team, &eye, &delta, &weapon)) return {};
  if (data.hit_count < 0 || data.hit_count > 8 || data.hits != data.hit_elements.data() ||
      data.contacts != data.elements.data()) return {};
  for (int i = 0; i < data.hit_count; ++i) {
    const auto& hit = data.hits[i];
    if (!std::isfinite(hit.damage) || hit.damage < 0 || hit.damage > 10000 ||
        !std::isfinite(hit.enter) || !std::isfinite(hit.exit)) return {};
    if (hit.damage <= 0) break;
    if (hit.flags & 1) {
      if (hit.exit >= 1.0F) break; // the engine reports failure to exit this surface
      continue;
    }
    // IDA 0x2f1389 selects +0x12 (segment endpoint), not +0x10
    // (the preceding surface). The current engine's non-penetration branch
    // likewise accounts for travel to hit.exit before publishing damage.
    const auto contact = static_cast<std::size_t>(hit.exit_contact & 0x7fff);
    if (contact >= data.elements.size()) return {};
    std::uint32_t handle = 0;
    std::memcpy(&handle, data.contacts[contact].bytes + 0x2c, sizeof(handle));
    if (api_.resolve(api_.context, handle) != target) continue;
    const auto& endpoint = data.contacts[contact];
    // Original dump 0x2f13b3..0x2f13c4: require entity hitbox contact,
    // a hitbox-data pointer, and a hitgroup in the accepted 0..11 domain.
    if (endpoint.bytes[0x33] != std::byte{1}) continue;
    std::uintptr_t hitbox = 0;
    std::memcpy(&hitbox, endpoint.bytes + 0x10, sizeof(hitbox));
    int group = -1;
    if (!hitbox || !game::read(hitbox + 0x38, group) || group < 0 || group > 11) return {};
    return {hit.damage, true, group};
  }
  return {0, true};
}
}
