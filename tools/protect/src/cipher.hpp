#pragma once

// The cipher the transform and the virtualizer share.
//
// The transform (T19) encrypts the index of every rewritten call site with a
// key that is split over three globals, so that a scanner cannot grep for it.
// The virtualizer (T20) encrypts its bytecode with a key of the same shape.
// Both derive the key from the module seed the same way, and both use the
// same mixer, so the primitives live here instead of being written twice: a
// cipher in two places is a cipher that drifts, and the drift is a wrong
// call target -- not a crash, which is the failure mode nothing else notices.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config.hpp"

namespace cs2::protect {

/// The number of constants the key is split into.
///
/// The key is split so that it does not appear as a single constant in the
/// image (the specification's design A).
inline constexpr std::size_t k_key_parts = 3;

/// The multiplier of the mixer (the bits of the golden ratio).
inline constexpr std::uint64_t k_mix_multiplier = 0x9E3779B97F4A7C15ULL;

/// The xor constant of the mixer.
inline constexpr std::uint64_t k_mix_xor = 0xBF58476D1CE4E5B9ULL;

/// How far the mixer rotates its result.
inline constexpr unsigned k_mix_rotate = 31;

/// The mixer. The IR that the transform emits computes exactly this, so the
/// value the pass encrypts with is the value the call site decrypts with.
[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t;

/// The next value of a seeded stream (splitmix64).
[[nodiscard]] auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t;

/// The constants the key is split into.
///
/// The three parts must differ: two equal parts cancel each other in the xor,
/// and the split would buy nothing.
[[nodiscard]] auto key_parts(std::uint64_t seed) noexcept -> std::array<std::uint64_t, k_key_parts>;

/// The seed of one module: the build seed mixed with the module id, so that
/// two modules of one build do not share a key and two builds do not either.
[[nodiscard]] auto module_seed(const Config& config, std::string_view module_id) noexcept
    -> std::uint64_t;

}  // namespace cs2::protect
