#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include "cs2/game/math.hpp"
#include "cs2/game/module.hpp"
namespace cs2::control {
struct MovementTraceApi {
  void (*filter)(void*, std::uintptr_t, std::uint64_t, int){};
  bool (*trace)(void*, void*, const game::Vec3*, const game::Vec3*, const void*, void*){};
  std::uintptr_t manager_global{};
};
struct LandingQuery {
  std::uintptr_t pawn{};
  std::uint64_t mask{};
  game::Vec3 origin{}, velocity{}, mins{}, maxs{};
  float gravity{800}, interval{1.0F / 64};
};
class MovementTrace {
 public:
  bool prepare(game::ModuleInfo module) noexcept;
  void bind(MovementTraceApi api) noexcept {
    api_ = api;
  }
  bool ready() const noexcept {
    return api_.filter && api_.trace && api_.manager_global;
  }
  std::optional<float> landing(const LandingQuery& query) const noexcept;

 private:
  MovementTraceApi api_{};
};
}  // namespace cs2::control
