#include "cipher.hpp"

#include "selection.hpp"

namespace cs2::protect {

namespace {

/// Rotates a 64-bit value to the left.
[[nodiscard]] auto rotate_left(std::uint64_t value, unsigned amount) noexcept -> std::uint64_t {
  return (value << amount) | (value >> (64U - amount));
}

}  // namespace

auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value *= k_mix_multiplier;
  value ^= k_mix_xor;
  return rotate_left(value, k_mix_rotate);
}

auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

auto key_parts(std::uint64_t seed) noexcept -> std::array<std::uint64_t, k_key_parts> {
  std::uint64_t state = seed;
  std::array<std::uint64_t, k_key_parts> parts{};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    parts[i] = splitmix64(state);
    if (parts[i] == 0) {
      parts[i] = 1;  // a zero part would drop out of the xor
    }
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    for (std::size_t j = i + 1; j < parts.size(); ++j) {
      while (parts[i] == parts[j]) {
        parts[j] = splitmix64(state);
        if (parts[j] == 0) {
          parts[j] = 1;
        }
      }
    }
  }
  return parts;
}

auto module_seed(const Config& config, std::string_view module_id) noexcept -> std::uint64_t {
  std::uint64_t base = 0;
  if (config.seed.has_value()) {
    base = *config.seed;
  } else if (config.policy.has_value() && config.policy->seed.has_value()) {
    base = *config.policy->seed;
  } else {
    base = derive_seed(module_id, "cs2.protect");
  }
  std::uint64_t state = base ^ derive_seed(module_id, "cs2.protect.module");
  return splitmix64(state);
}

}  // namespace cs2::protect
