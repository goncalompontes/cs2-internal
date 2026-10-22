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
// **The draw order is the engine's** (docs/re/aim.md §"The spread cone"):
//
//   per shot:  r1, a1 = RandomFloat(0,1), RandomFloat(0,2pi)   (the **spread**)
//   per pellet: r2, a2 = RandomFloat(0,1), RandomFloat(0,2pi)   (the **inaccuracy**)
//
// The spread radius and angle are drawn **once**, for pellet 0, and again for every
// pellet only when the `weapon_accuracy_shotgun_spread_patterns` convar is on. The
// inaccuracy radius and angle are drawn **per pellet**. A single-pellet shot with
// the convar off therefore draws exactly `r1, a1, r2, a2` — the same four numbers,
// in the same order, as before.
//
// **Unvalidated against the live engine.** The RNG is the standard Source
// `CUniformRandomStream` (Park–Miller + Bays–Durham shuffle) and `md5_pseudo_random`
// follows the documented `MD5_PseudoRandom(sequence)`, but the exact input
// construction and the per-weapon spread/inaccuracy magnitudes
// (`FUN_18080b880`) still need a live cross-check (`docs/re/aim.md` §9).

#include <array>
#include <cstdint>
#include <optional>
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

// The engine's **attack type** (`C_CSWeaponBase::m_weaponMode`, weapon + 0x1A00).
//
// `CSWeaponMode`: 0 is the primary attack, 1 the secondary. The engine's revolver
// rule needs the secondary one (`docs/re/aim.md` §"The attack type").
//
// **`ATTACK_TYPE_UNKNOWN` is not "primary".** The field is sticky — one path sets it
// to 1, another consumes and clears it — so a payload that reads it *before* the
// engine fires reads the engine's **previous** decision, and three of the four
// `CSBaseGunFire` call sites do not pass the field at all. The unknown value is
// therefore its own state, and the conservative reading of it is "not the secondary
// attack": the revolver's `1 - r^2` is then **not** applied.
inline constexpr int ATTACK_TYPE_UNKNOWN = -1;
inline constexpr int ATTACK_TYPE_PRIMARY = 0;
inline constexpr int ATTACK_TYPE_SECONDARY = 1;

// The engine's **per-shot recoil counter** (`C_CSWeaponBase::m_flRecoilIndex`,
// weapon + 0x1A28): 0, 1, 2, … after every shot, reset when the weapon stops
// firing. The negev's rule is keyed on it (`docs/re/aim.md` §"The spread cone").
//
// `-1` is the unknown value: the conservative reading of it is **no transform**.
inline constexpr int RECOIL_INDEX_UNKNOWN = -1;

// The weapon's spread pattern (`docs/re/aim.md` §"The spread cone"). The revolver
// transforms the radii `r -> 1 - r^2`; the negev transforms them `r -> 1 - r^(2^k)`
// for the first three shots of a burst. The shotgun's **pattern table** is a
// separate thing (`shotgun_pattern_table` below) and is not a pattern value.
enum class SpreadPattern : std::uint8_t { standard, revolver, negev };

// The full turn, in radians — the engine's `RandomFloat(0, 2*pi)` in the draw
// below. It lives here because the kernel's recipe is the kernel's constant: the
// Monte-Carlo sampler (`cs2/aim/montecarlo.hpp`) draws the same angles and must
// use the same value.
inline constexpr float TWO_PI = 6.28318530717958647F;

// The per-shot **rules** the engine's cone sampler applies.
//
// They are a parameter, not a constant: the shotgun's convar decides whether the
// spread radius is redrawn per pellet, and the attack type and the recoil counter
// decide whether the revolver's and the negev's transforms apply at all.
struct SpreadRules {
  // The VData's own revolver discriminator (`CCSWeaponBaseVData::m_bIsRevolver`,
  // VData + 0x71E). The engine compares the weapon **id**; this is the VData's
  // "this is the revolver" flag, and it is the payload's read of that id.
  bool is_revolver = false;
  // The engine's attack type (`m_weaponMode`): 0 primary, 1 secondary,
  // `ATTACK_TYPE_UNKNOWN` when the payload cannot know it.
  int attack_type = ATTACK_TYPE_UNKNOWN;
  // The engine's per-shot recoil counter (`m_flRecoilIndex`): 0, 1, 2, …,
  // `RECOIL_INDEX_UNKNOWN` when the payload cannot read it.
  int recoil_index = RECOIL_INDEX_UNKNOWN;
  // The `weapon_accuracy_shotgun_spread_patterns` convar: when it is on, the
  // spread radius and angle are drawn **per pellet** and the inaccuracy comes
  // from the weapon's pattern table. It is **off by default**.
  bool shotgun_patterns = false;
  // The weapon's pellet count (`CCSWeaponBaseVData::m_nNumBullets`, +0x730) and
  // its spread seed (`m_nSpreadSeed`, +0x7D8): the pattern table's inputs.
  int num_bullets = 0;
  int spread_seed = 0;
};

// Which transform the engine applies to a drawn radius.
enum class RadiusTransform : std::uint8_t {
  none,      ///< the radius is used as drawn
  revolver,  ///< `r -> 1 - r^2`
  negev,     ///< `r -> 1 - r^(2^k)`, `k = 3 - recoil index`
};

// The transform the engine's cone sampler applies to the drawn radii.
//
// **The revolver's rule** (`docs/re/aim.md` §"The attack type"): the weapon id —
// the VData's `m_bIsRevolver` — **and** the attack type == 1. The designer's name
// is **not** the engine's discriminator, so the name alone cannot put the
// transform on: `is_revolver` and the attack type are both required.
//
// **The negev's rule**: the weapon id **and** the recoil counter below three. The
// id has no VData flag, so the *class* is the payload's only discriminator for it
// — and the class comes from the caller, which knows the weapon's name.
//
// **An unknown attack type or recoil counter applies no transform**: the payload
// cannot know the engine's state, and the conservative reading of "unknown" is
// the standard cone.
[[nodiscard]] constexpr auto radius_transform(SpreadPattern pattern,
                                               const SpreadRules& rules) noexcept
    -> RadiusTransform {
  if (rules.is_revolver && rules.attack_type == ATTACK_TYPE_SECONDARY) {
    return RadiusTransform::revolver;
  }
  if (pattern == SpreadPattern::negev && rules.recoil_index >= 0 &&
      rules.recoil_index < 3) {
    return RadiusTransform::negev;
  }
  return RadiusTransform::none;
}

// The negev's exponent: `k = 3 - recoil index` (the recoil index 0, 1, 2 → k 3, 2,
// 1). The engine computes `r^(2^k)` by squaring `k` times.
[[nodiscard]] constexpr auto negev_exponent(int recoil_index) noexcept -> int {
  return 3 - recoil_index;
}

// The shotgun's **pattern table** (docs/re/aim.md §"The pattern table").
//
// One entry of the engine's region-2 table: the direction and the radius
// fraction of one pellet.
struct ShotgunPatternEntry {
  float angle = 0.0F;    ///< the direction, in radians
  float radius = 0.0F;  ///< the fraction of the inaccuracy magnitude
};

// The number of entries in the engine's pattern block (`0x40`).
inline constexpr int PATTERN_TABLE_ENTRIES = 64;

// The engine's region-2 pattern table for one weapon.
//
// The values come from the weapon's VData: `angle = U(0, 2*pi)` and
// `radius = U(k/N, (k+1)/N)` with `N = num_bullets` and `k` the entry's index
// modulo `N`, drawn from a `CUniformRandomStreamImpl` seeded with the VData's
// `m_nSpreadSeed`.
//
// **Nullopt when the weapon fires fewer than two bullets**: the engine then marks
// the block invalid and falls back to the random cone.
//
// The table is drawn **in order**, so the entry at index `j` is only reachable
// after `j + 1` draws — the table is built whole, exactly as the engine builds it.
[[nodiscard]] auto shotgun_pattern_table(int spread_seed, int num_bullets) noexcept
    -> std::optional<std::array<ShotgunPatternEntry, PATTERN_TABLE_ENTRIES>>;

// The `K` pellet offsets in the chart (= the `(right, up)` tangent frame the engine
// applies them in), following the engine's recipe:
//
//   per shot:   r1,a1 = RandomFloat(0,1),RandomFloat(0,2pi)   (the spread)
//   per pellet: r2,a2 = RandomFloat(0,1),RandomFloat(0,2pi)   (the inaccuracy)
//   o = spread*r1*(cos a1,sin a1) + inacc*r2*(cos a2,sin a2)
//
// The spread radius and angle are drawn **once** (for pellet 0) unless the
// `weapon_accuracy_shotgun_spread_patterns` convar is on, in which case they are
// drawn per pellet too — and the inaccuracy then comes from the weapon's pattern
// table instead of the stream.
//
// The rules (`SpreadRules`) carry the engine's per-shot conditions: the revolver's
// and the negev's transforms, and the convar.
[[nodiscard]] auto pellet_offsets(std::int32_t random_seed, float spread, float inaccuracy,
                                  int pellets, SpreadPattern pattern = SpreadPattern::standard,
                                  const SpreadRules& rules = SpreadRules{}) -> std::vector<Vec2>;

}  // namespace cs2::aim
