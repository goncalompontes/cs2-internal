#pragma once

// The deterministic bullet-spread model (docs/ragebot.md §2/§9, docs/re/aim.md).
//
// A shot fires `K` pellets; each pellet direction is `f(a) + o_k`, and `o_k` is drawn
// from the engine's `CUniformRandomStream` seeded by the command's `random_seed`
// (`MD5_PseudoRandom(sequence)`). There is no random cone to sample — given the seed
// and the weapon's spread/inaccuracy magnitudes the offsets are exact, so the aim
// objective is the pellet *sum* (`DamageField::argmax_pellets`), not a Monte-Carlo
// integral.
//
// **Unvalidated against the live engine.** The RNG is the standard Source
// `CUniformRandomStream` (Park–Miller + Bays–Durham shuffle) and `md5_pseudo_random`
// follows the documented `MD5_PseudoRandom(sequence)`, but the exact input
// construction and the per-weapon spread/inaccuracy magnitudes
// (`FUN_18080b880`) still need a live cross-check (`docs/re/aim.md` §9).

#include <cstdint>
#include <vector>

#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

// The engine's `CUniformRandomStream` (`RandomSeed` / `GenerateRandomNumber` /
// `RandomFloat`).
class UniformRandomStream {
 public:
  explicit UniformRandomStream(std::int32_t seed = 0) noexcept {
    set_seed(seed);
  }

  void set_seed(std::int32_t seed) noexcept;
  [[nodiscard]] auto random_int() noexcept -> std::int32_t;
  [[nodiscard]] auto random_float(float low, float high) noexcept -> float;

 private:
  std::int32_t idum_ = 0;
  std::int32_t iy_ = 0;
  std::int32_t iv_[32] = {};
};

// `MD5_PseudoRandom(sequence)` — the command's `random_seed` (`docs/re/aim.md`): the
// MD5 of the 4 little-endian seed bytes, first digest dword as a little-endian int.
[[nodiscard]] auto md5_pseudo_random(std::uint32_t sequence) noexcept -> std::int32_t;

// The weapon's spread pattern (`docs/re/aim.md` §"The spread cone"). The revolver
// transforms the spread radius `r -> 1 - r^2`; the shotgun/negev table
// (`FUN_1807cd1b0`) is not modelled.
enum class SpreadPattern : std::uint8_t { standard, revolver };

// The full turn, in radians — the engine's `RandomFloat(0, 2*pi)` in the draw
// below. It lives here because the kernel's recipe is the kernel's constant: the
// Monte-Carlo sampler (`cs2/aim/montecarlo.hpp`) draws the same angles and must
// use the same value.
inline constexpr float TWO_PI = 6.28318530717958647F;

// The `K` pellet offsets in the chart (= the `(right, up)` tangent frame the engine
// applies them in), following the engine's recipe:
//   per pellet:  r1,a1 = RandomFloat(0,1),RandomFloat(0,2pi)   (spread)
//                r2,a2 = RandomFloat(0,1),RandomFloat(0,2pi)   (inaccuracy)
//                o = spread*r1*(cos a1,sin a1) + inacc*r2*(cos a2,sin a2)
// The draw order is exactly as above so the offsets match the engine.
[[nodiscard]] auto pellet_offsets(std::int32_t random_seed, float spread, float inaccuracy,
                                  int pellets, SpreadPattern pattern = SpreadPattern::standard)
    -> std::vector<Vec2>;

}  // namespace cs2::aim
