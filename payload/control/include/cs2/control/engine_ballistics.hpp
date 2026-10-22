#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "cs2/aim/ballistics.hpp"
#include "cs2/game/entity_system.hpp"
namespace cs2::control {
namespace engine_trace {
struct Filter { alignas(8) std::byte bytes[0x48]{}; };
struct Contact { std::byte bytes[0x38]{}; };
struct Hit {
  float enter{}, exit{}, damage{};
  int remaining_penetrations{};
  std::uint16_t contact{}, exit_contact{};
  std::uint8_t flags{};
  std::byte pad[3]{};
};
struct Data {
  int unknown1{}; float unknown2{52}; Contact* contacts{};
  int capacity{128}; int flags{static_cast<int>(0x80000000u)};
  std::array<Contact, 128> elements{};
  std::byte pad0[8]{};
  int hit_count{}, unknown5{};
  Hit* hits{};
  int hit_capacity{8}, hit_flags{static_cast<int>(0x80000000u)};
  std::array<Hit, 8> hit_elements{};
  aim::Vec3 start{}, direction{};
  float fraction{1}; bool unknown6{}; std::byte pad1[0x4b]{};
};
static_assert(sizeof(Hit) == 24 && sizeof(Data) == 0x1d60);
static_assert(offsetof(Data, hit_count) == 0x1c20 && offsetof(Data, start) == 0x1cf8);
struct Api {
  void (*filter)(Filter*, std::uintptr_t, std::uint64_t, std::uint8_t, int){};
  void (*init)(Data*, const aim::Vec3*, const aim::Vec3*, Filter*, int, bool){};
  void (*bullet)(Data*, float, float, float, int, int, std::uintptr_t){};
  std::uintptr_t (*resolve)(void*, std::uint32_t){};
  void* context{};
};
}
class EngineBallistics {
 public:
  bool initialize() noexcept;
  // Explicit API binding is also the test seam for buffer/identity validation.
  void bind(engine_trace::Api api) noexcept { api_ = api; }
  [[nodiscard]] bool ready() const noexcept { return api_.filter && api_.init && api_.bullet && api_.resolve; }
  bool recoil(std::uintptr_t pawn, float out[3]) const noexcept;
  bool recoil_ready() const noexcept { return punch_ != nullptr; }
  aim::WorldDamageQuery query(std::uintptr_t local, int team, std::uintptr_t target);
  aim::WorldDamage evaluate(std::uintptr_t local, int team, std::uintptr_t target,
                           aim::Vec3 eye, aim::Vec3 direction, const aim::WeaponProfile& weapon) noexcept;
 private:
  engine_trace::Api api_{};
  void (*punch_)(std::uintptr_t, aim::Vec3*) = nullptr;
  std::uint32_t punch_services_offset_ = 0;
  std::uintptr_t attempted_base_{};
  game::EntitySystem entities_;
};
}
