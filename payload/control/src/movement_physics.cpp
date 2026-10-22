#include "cs2/control/movement_physics.hpp"
#include <windows.h>
#include <cmath>
#include <vector>
#include "cs2/game/memory.hpp"
namespace cs2::control {
namespace {
constexpr const char* names[] = {"sv_airaccelerate", "sv_air_max_wishspeed",
                                 "sv_staminarecoveryrate", "sv_quantize_movement_input"};
std::uintptr_t registry() noexcept {
  __try {
    const auto module = GetModuleHandleW(L"tier0.dll");
    if (!module) return 0;
    using Create = void* (*)(const char*, int*);
    const auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateInterface"));
    return create ? reinterpret_cast<std::uintptr_t>(create("VEngineCvar007", nullptr)) : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}
}  // namespace
bool MovementPhysicsSource::locate() noexcept {
  if (!registry_) registry_ = registry();
  std::uint16_t allocation = 0, head = 0;
  std::uintptr_t container = 0;
  if (!registry_ || !game::read(registry_ + 0x4a, allocation) ||
      !game::read(registry_ + 0x50, container) || !game::read(registry_ + 0x58, head))
    return false;
  const auto capacity = static_cast<unsigned>(allocation & 0x7fff);
  if (!capacity || !container || head >= capacity) return false;
  struct Entry {
    std::uintptr_t variable;
    std::uint16_t generation, next;
    std::uint32_t links;
  };
  static_assert(sizeof(Entry) == 0x10);
  std::vector<Entry> entries(capacity);
  if (!game::read_bytes(container, entries.data(), entries.size() * sizeof(Entry))) return false;
  unsigned found = 0;
  for (unsigned visited = 0; head != 0xffff && visited < capacity; ++visited) {
    if (head >= capacity) return false;
    const auto& entry = entries[head];
    std::uintptr_t name = 0;
    if (entry.variable && game::read(entry.variable, name)) {
      const auto text = game::read_cstring(name, 96);
      for (unsigned i = 0; i < 4; ++i)
        if (text == names[i]) {
          if (variables_[i] && variables_[i] != entry.variable) return false;
          if (!variables_[i]) ++found;
          variables_[i] = entry.variable;
        }
    }
    head = entry.next;
  }
  return head == 0xffff && found == 4;
}
MovementPhysics MovementPhysicsSource::read() noexcept {
  MovementPhysics out;
  if (!variables_[0] || !variables_[1] || !variables_[2] || !variables_[3]) {
    for (auto& p : variables_) p = 0;
    if (!locate()) {
      for (auto& p : variables_) p = 0;
      return out;
    }
  }
  std::int16_t types[4]{};
  for (unsigned i = 0; i < 4; ++i) {
    std::uintptr_t name = 0;
    if (!game::read(variables_[i], name) || game::read_cstring(name, 96) != names[i] ||
        !game::read(variables_[i] + 0x28, types[i])) {
      for (auto& pointer : variables_) pointer = 0;
      return out;
    }
  }
  float acceleration = 0, cap = 0, recovery = 0;
  std::uint8_t quantized = 2;
  if (types[0] != types[1] || types[0] != types[2] || types[0] == types[3] ||
      !game::read(variables_[0] + 0x58, acceleration) || !game::read(variables_[1] + 0x58, cap) ||
      !game::read(variables_[2] + 0x58, recovery) || !game::read(variables_[3] + 0x58, quantized) ||
      !std::isfinite(acceleration) || !std::isfinite(cap) || !std::isfinite(recovery) ||
      acceleration < .01F || acceleration > 10000 || cap < .01F || cap > 1000 || recovery < 0 ||
      recovery > 1000 || quantized > 1)
    return out;
  return {acceleration, cap, recovery, quantized != 0, true};
}
}  // namespace cs2::control
