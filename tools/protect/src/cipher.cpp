#include "cipher.hpp"

#include "selection.hpp"

namespace cs2::protect {

namespace {

/// Rotates a 64-bit value to the left.
[[nodiscard]] auto rotate_left(std::uint64_t value, unsigned amount) noexcept -> std::uint64_t {
  return (value << amount) | (value >> (64U - amount));
}

/// The salt of the mixer's derivation.
///
/// The module's seed is not enough on its own: the mixer and the parts of
/// the key are different things, and a stream that is used for both would
/// make them correlated.
constexpr std::uint64_t k_mixer_salt = 0x4D495845525F5345ULL;

}  // namespace

auto Mixer::mix64(std::uint64_t value) const noexcept -> std::uint64_t {
  value *= multiplier;
  value ^= xor_constant;
  return rotate_left(value, rotate);
}

auto mixer_for(std::uint64_t seed) noexcept -> Mixer {
  std::uint64_t state = seed ^ k_mixer_salt;

  Mixer mixer;
  // The multiplier is odd: an even one would drop the low bit of the value
  // it multiplies, and the mixer would not be a bijection.
  mixer.multiplier = splitmix64(state) | 1ULL;
  mixer.xor_constant = splitmix64(state);
  if (mixer.xor_constant == 0) {
    mixer.xor_constant = 1;  // a zero constant would make the xor a no-op
  }
  // The rotation is in [1, 63]: a rotation of 0 would make the `lshr` by 64
  // of the transform's decode undefined behaviour.
  mixer.rotate = 1U + static_cast<unsigned>(splitmix64(state) % 63ULL);
  return mixer;
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
  if (const auto seed = build_seed(config); seed.has_value()) {
    base = *seed;
  } else {
    // The seed is mandatory: `load_config()` refuses to run without one, so
    // the plugin never gets here. The fallback is here so that the pure
    // functions have a defined answer -- and it is exactly the constant of
    // the payload that the mandatory seed exists to ban.
    base = derive_seed(module_id, "cs2.protect");
  }
  std::uint64_t state = base ^ derive_seed(module_id, "cs2.protect.module");
  return splitmix64(state);
}

}  // namespace cs2::protect
