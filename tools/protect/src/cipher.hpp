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

/// The mixer of the protection: the multiplier, the xor constant and the
/// rotation.
///
/// The three values are **per build**: they come from the module's seed, the
/// way the parts of the key already do. They were fixed constants until
/// T24, and they were the protection's only fixed bytes: sixteen bytes at
/// the same four addresses in every build and in every version, which is a
/// signature an analyst reads in one instruction.
///
/// The **multiplier** and the **xor constant** live in globals and are read
/// with volatile loads. That is not a style choice: the X86 backend of this
/// LLVM mis-materialises a 64-bit immediate in the shape the transform emits
/// (see the README §12), and a constant that reaches the image as a
/// different value is a wrong key, which is a wrong call target -- not a
/// crash.
///
/// The **rotation** is the one value that is in the instruction stream
/// (`rolq $imm8`), and that is deliberate: it is what makes the `.text` of
/// two builds with two seeds differ.
struct Mixer {
  /// The multiplier of the mix. It is odd, so that the mixer is a bijection.
  std::uint64_t multiplier{1};
  /// The xor constant of the mix. It is not zero: a zero constant would
  /// make the xor a no-op.
  std::uint64_t xor_constant{1};
  /// How far the mixer rotates its result.
  ///
  /// It is in [1, 63]: a rotation of 0 would make the `lshr` by 64 of the
  /// transform's decode undefined behaviour, and a rotation of 64 is not a
  /// rotation at all.
  unsigned rotate{31};

  /// The mixer itself.
  ///
  /// The IR that the transform emits computes exactly this, so the value the
  /// pass encrypts with is the value the call site decrypts with.
  [[nodiscard]] auto mix64(std::uint64_t value) const noexcept -> std::uint64_t;
};

/// The mixer of a build: the multiplier, the xor constant and the rotation,
/// derived from the seed.
///
/// The seed is the **module's** seed, so that two modules of one build do
/// not share a mixer and two builds do not either.
[[nodiscard]] auto mixer_for(std::uint64_t seed) noexcept -> Mixer;

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
