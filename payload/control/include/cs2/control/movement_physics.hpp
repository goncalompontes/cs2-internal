#pragma once
#include <cstdint>
namespace cs2::control {
struct MovementPhysics {
  float air_acceleration{12}, air_cap{30}, stamina_recovery{60};
  bool quantized{true}, valid{};
};
// Read-only VEngineCvar007 container adapter, checked against reference layout,
// exact names, bounded traversal, consistent float types and finite value ranges.
class MovementPhysicsSource {
 public:
  void bind(std::uintptr_t registry) noexcept {
    registry_ = registry;
    for (auto& p : variables_) p = 0;
  }
  MovementPhysics read() noexcept;

 private:
  bool locate() noexcept;
  std::uintptr_t registry_{}, variables_[4]{};
};
}  // namespace cs2::control
