#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2/control/movement_trace.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include "cs2/game/memory.hpp"
#include "cs2/game/pattern.hpp"
namespace cs2::control {
namespace {
struct HullRay {
  game::Vec3 mins{}, maxs{};
  std::byte pad[0x10]{};
  std::uint8_t type{2};
  std::byte tail[7]{};
};
static_assert(sizeof(HullRay) == 0x30 && offsetof(HullRay, type) == 0x28);
bool finite(game::Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool run(const MovementTraceApi* api, const LandingQuery* q, const game::Vec3* end,
         std::byte* result) noexcept {
  std::uintptr_t manager = 0, physics = 0;
  if (!game::read(api->manager_global, manager) || !manager || !game::read(manager, physics) ||
      !physics)
    return false;
  HullRay ray;
  ray.mins = q->mins;
  ray.maxs = q->maxs;
  alignas(8) std::byte filter[0x48]{};
  __try {
    api->filter(filter, q->pawn, q->mask, 11);
    api->trace(reinterpret_cast<void*>(manager), &ray, &q->origin, end, filter, result);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
}  // namespace
bool MovementTrace::prepare(game::ModuleInfo module) noexcept {
  const auto site = game::unique_code_match(module, "000041B800010000E8????????4C397F38");
  const auto trace = game::unique_code_match(
      module, "48895424??48894C24??55535657415441564157488DAC24????????B810250000");
  const auto manager = game::unique_code_match(module, "488B0D????????488D3452");
  std::int32_t filter_rel = 0, manager_rel = 0;
  if (!site || !trace || !manager || !game::read(site + 9, filter_rel) ||
      !game::read(manager + 3, manager_rel))
    return false;
  const auto filter = site + 13 + filter_rel;
  const auto global = manager + 7 + manager_rel;
  if (!module.contains(filter) || !module.contains(global)) return false;
  api_ = {reinterpret_cast<decltype(api_.filter)>(filter),
          reinterpret_cast<decltype(api_.trace)>(trace), global};
  return true;
}
std::optional<float> MovementTrace::landing(const LandingQuery& q) const noexcept {
  if (!ready() || !q.pawn || !q.mask || !finite(q.origin) || !finite(q.velocity) ||
      !finite(q.mins) || !finite(q.maxs) || !std::isfinite(q.gravity) || q.gravity <= 0 ||
      !std::isfinite(q.interval) || q.interval <= 0 || q.interval > .1F || q.velocity.z > 0 ||
      q.mins.x > q.maxs.x || q.mins.y > q.maxs.y || q.mins.z > q.maxs.z)
    return {};
  // Sweep the current (including crouch) hull over one gravity-integrated tick.
  // No extra downward padding: it would move the predicted press before contact.
  auto velocity = q.velocity;
  velocity.z -= q.gravity * q.interval * .5F;
  const auto end = q.origin + velocity * q.interval;
  alignas(8) std::array<std::byte, 0x200> result{};
  const float clear = 1;
  std::memcpy(result.data() + 0xac, &clear, 4);
  if (!run(&api_, &q, &end, result.data())) return {};
  float fraction = 1;
  game::Vec3 normal{};
  std::uint8_t solid = 0;
  std::memcpy(&fraction, result.data() + 0xac, 4);
  std::memcpy(&normal, result.data() + 0x90, sizeof(normal));
  std::memcpy(&solid, result.data() + 0xb6, 1);
  if (solid || !std::isfinite(fraction) || fraction <= 0 || fraction >= 1 || !finite(normal) ||
      normal.z < .7F)
    return {};
  return fraction;
}
}  // namespace cs2::control
