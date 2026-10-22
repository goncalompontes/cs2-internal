#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cs2/control/movement_physics.hpp"
namespace {
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
template <class T>
void put(void* p, unsigned offset, T value) {
  std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof(value));
}
}  // namespace
int main() {
  using namespace cs2::control;
  std::array<std::byte, 0x80> root{};
  std::array<std::byte, 64> nodes{};
  std::array<std::array<std::byte, 0x68>, 4> values{};
  const char* names[] = {"sv_airaccelerate", "sv_air_max_wishspeed", "sv_staminarecoveryrate",
                         "sv_quantize_movement_input"};
  put(root.data(), 0x4a, std::uint16_t{4});
  put(root.data(), 0x50, reinterpret_cast<std::uintptr_t>(nodes.data()));
  put(root.data(), 0x58, std::uint16_t{0});
  for (unsigned i = 0; i < 4; ++i) {
    put(nodes.data(), i * 16, reinterpret_cast<std::uintptr_t>(values[i].data()));
    put(nodes.data(), i * 16 + 10, static_cast<std::uint16_t>(i == 3 ? 0xffff : i + 1));
    put(values[i].data(), 0, reinterpret_cast<std::uintptr_t>(names[i]));
    put(values[i].data(), 0x28, static_cast<std::int16_t>(i == 3 ? 0 : 7));
  }
  put(values[0].data(), 0x58, 12.0F);
  put(values[1].data(), 0x58, 30.0F);
  put(values[2].data(), 0x58, 60.0F);
  put(values[3].data(), 0x58, std::uint8_t{1});
  MovementPhysicsSource source;
  source.bind(reinterpret_cast<std::uintptr_t>(root.data()));
  auto state = source.read();
  check(state.valid && state.air_acceleration == 12 && state.air_cap == 30 &&
            state.stamina_recovery == 60 && state.quantized,
        "reference registry values bind by exact name");
  put(values[0].data(), 0x58, 100.0F);
  state = source.read();
  check(state.valid && state.air_acceleration == 100,
        "server value changes are read, not cached as defaults");
  put(values[3].data(), 0x58, std::uint8_t{0});
  check(!source.read().quantized, "quantization status follows its live value");
  put(values[0].data(), 0x58, std::nanf(""));
  check(!source.read().valid, "nonfinite physics does not reach the planner");
  put(values[0].data(), 0x58, 12.0F);
  put(values[1].data(), 0x28, std::int16_t{3});
  check(!source.read().valid, "type mismatch rejects an unverified representation");
  put(values[1].data(), 0x28, std::int16_t{7});
  put(values[3].data(), 0x58, std::uint8_t{2});
  check(!source.read().valid, "invalid bool representation rejected");
  put(values[3].data(), 0x58, std::uint8_t{1});
  source.bind(reinterpret_cast<std::uintptr_t>(root.data()));
  put(nodes.data(), 3 * 16 + 10, std::uint16_t{0});
  check(!source.read().valid, "cycle is bounded and rejected");
  source.bind(reinterpret_cast<std::uintptr_t>(root.data()));
  put(nodes.data(), 3 * 16 + 10, std::uint16_t{8});
  check(!source.read().valid, "out-of-range links fail safely");
  std::puts(
      "PASS: read-only live physics adapter, value updates, exact-name/type checks and bounded "
      "traversal");
}
