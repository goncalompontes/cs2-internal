// Unit tests for the deterministic spread model (docs/ragebot.md §2/§9,
// docs/re/aim.md §"The spread cone").

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "cs2/aim/spread.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-6F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

constexpr float k_two_pi = 6.28318530717958647F;

}  // namespace

auto main() -> int {
  using cs2::aim::ATTACK_TYPE_PRIMARY;
  using cs2::aim::ATTACK_TYPE_SECONDARY;
  using cs2::aim::ATTACK_TYPE_UNKNOWN;
  using cs2::aim::md5_pseudo_random;
  using cs2::aim::pellet_offsets;
  using cs2::aim::RECOIL_INDEX_UNKNOWN;
  using cs2::aim::shotgun_pattern_table;
  using cs2::aim::SpreadPattern;
  using cs2::aim::SpreadRules;
  using cs2::aim::UniformRandomStream;
  using cs2::aim::Vec2;

  // MD5_PseudoRandom — cross-checked against Python `hashlib`:
  //   md5(struct.pack('<I', seed)).digest()[:4] as a little-endian int.
  expect(md5_pseudo_random(1) == static_cast<std::int32_t>(0x8ad85243U), "md5 seed 1");
  expect(md5_pseudo_random(2) == static_cast<std::int32_t>(0xed0dddf2U), "md5 seed 2");
  expect(md5_pseudo_random(3) == static_cast<std::int32_t>(0x98aecfedU), "md5 seed 3");
  expect(md5_pseudo_random(1000) == static_cast<std::int32_t>(0x1d8b4e0cU), "md5 seed 1000");
  expect(md5_pseudo_random(12345) == static_cast<std::int32_t>(0x399c1e40U), "md5 seed 12345");
  expect(md5_pseudo_random(0xdeadbeefU) == static_cast<std::int32_t>(0xb9395ac0U),
         "md5 seed 0xdeadbeef");

  // Park–Miller + Bays–Durham shuffle — the first draws pinned against an
  // independent Python model (guards against a transcription slip).
  {
    UniformRandomStream a(12345);
    expect(a.random_int() == 1982386332, "RNG first int (seed 12345)");
    UniformRandomStream b(7);
    expect(b.random_int() == 263789375, "RNG first int (seed 7)");
    UniformRandomStream c(999);
    expect(near(c.random_float(0.0F, 1.0F), 0.61710943F, 1e-6F), "RNG first float (seed 999)");
  }

  // The stream is deterministic and stays in range.
  {
    UniformRandomStream a(12345);
    UniformRandomStream b(12345);
    bool in_range = true;
    bool same = true;
    for (int i = 0; i < 128; ++i) {
      const float fa = a.random_float(0.0F, 1.0F);
      const float fb = b.random_float(0.0F, 1.0F);
      in_range = in_range && fa >= 0.0F && fa < 1.0F;
      same = same && fa == fb;
    }
    expect(in_range, "random_float stays in range");
    expect(same, "the stream is deterministic");

    UniformRandomStream other(999);
    UniformRandomStream first(12345);
    expect(other.random_float(0.0F, 1.0F) != first.random_float(0.0F, 1.0F),
           "different seeds differ");
  }

  // The **single-pellet** case is bit-identical to the recipe before the draw
  // order was fixed: the shot draws `r1, a1, r2, a2` in that order and nothing
  // else. The comparison is **exact** — the claim is bit-identity, not a
  // tolerance.
  {
    const auto offsets = pellet_offsets(12345, 0.01F, 0.02F, 1);
    expect(offsets.size() == 1, "one offset per pellet");
    UniformRandomStream t(12345);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    const float r2 = t.random_float(0.0F, 1.0F);
    const float a2 = t.random_float(0.0F, k_two_pi);
    const Vec2 expected{.x = 0.01F * r1 * std::cos(a1) + 0.02F * r2 * std::cos(a2),
                        .y = 0.01F * r1 * std::sin(a1) + 0.02F * r2 * std::sin(a2)};
    expect(offsets[0].x == expected.x && offsets[0].y == expected.y,
           "the single pellet is bit-identical to the four-draw recipe");
  }

  // The **multi-pellet** case follows the engine's rule: the spread radius and
  // angle are drawn **once** (pellet 0) and the inaccuracy radius and angle are
  // drawn **per pellet**. The old recipe drew all four per pellet, so the stream
  // desynchronised from pellet 1 onward.
  {
    const auto offsets = pellet_offsets(12345, 0.01F, 0.02F, 3);
    expect(offsets.size() == 3, "one offset per pellet");
    UniformRandomStream t(12345);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    const float r2 = t.random_float(0.0F, 1.0F);
    const float a2 = t.random_float(0.0F, k_two_pi);
    const float r3 = t.random_float(0.0F, 1.0F);
    const float a3 = t.random_float(0.0F, k_two_pi);
    const float r4 = t.random_float(0.0F, 1.0F);
    const float a4 = t.random_float(0.0F, k_two_pi);
    // Pellet 0: the first four draws.
    expect(offsets[0].x == 0.01F * r1 * std::cos(a1) + 0.02F * r2 * std::cos(a2) &&
               offsets[0].y == 0.01F * r1 * std::sin(a1) + 0.02F * r2 * std::sin(a2),
           "the first pellet draws the spread pair and the inaccuracy pair");
    // Pellet 1: the **same** spread pair, the next inaccuracy pair.
    expect(offsets[1].x == 0.01F * r1 * std::cos(a1) + 0.02F * r3 * std::cos(a3) &&
               offsets[1].y == 0.01F * r1 * std::sin(a1) + 0.02F * r3 * std::sin(a3),
           "the second pellet reuses the spread pair and draws its own inaccuracy");
    // Pellet 2: the same again.
    expect(offsets[2].x == 0.01F * r1 * std::cos(a1) + 0.02F * r4 * std::cos(a4) &&
               offsets[2].y == 0.01F * r1 * std::sin(a1) + 0.02F * r4 * std::sin(a4),
           "the third pellet reuses the spread pair and draws its own inaccuracy");
    // The old (wrong) recipe would have drawn a second spread pair for pellet 1,
    // so its offsets differ from the engine's.
    const Vec2 old_recipe{.x = 0.01F * r3 * std::cos(a3) + 0.02F * r4 * std::cos(a4),
                          .y = 0.01F * r3 * std::sin(a3) + 0.02F * r4 * std::sin(a4)};
    expect(offsets[1].x != old_recipe.x || offsets[1].y != old_recipe.y,
           "the second pellet is not the old four-draws-per-pellet recipe");
  }

  // A zero-spread weapon fires straight.
  for (const Vec2& offset : pellet_offsets(7, 0.0F, 0.0F, 9)) {
    expect(offset.x == 0.0F && offset.y == 0.0F, "zero spread -> zero offset");
  }

  // The revolver's `1 - r^2` needs the weapon id **and** the attack type == 1.
  // The engine applies it to **both** radii.
  {
    const SpreadRules secondary{.is_revolver = true, .attack_type = ATTACK_TYPE_SECONDARY};
    const SpreadRules primary{.is_revolver = true, .attack_type = ATTACK_TYPE_PRIMARY};
    const SpreadRules unknown{.is_revolver = true, .attack_type = ATTACK_TYPE_UNKNOWN};
    const SpreadRules deagle{.is_revolver = false, .attack_type = ATTACK_TYPE_SECONDARY};
    UniformRandomStream t(5);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    const float r2 = t.random_float(0.0F, 1.0F);
    const float a2 = t.random_float(0.0F, k_two_pi);
    const float plain = 0.1F * r1 * std::cos(a1) + 0.05F * r2 * std::cos(a2);
    const float plain_y = 0.1F * r1 * std::sin(a1) + 0.05F * r2 * std::sin(a2);
    const float turned = 0.1F * (1.0F - r1 * r1) * std::cos(a1) +
                         0.05F * (1.0F - r2 * r2) * std::cos(a2);
    const float turned_y = 0.1F * (1.0F - r1 * r1) * std::sin(a1) +
                           0.05F * (1.0F - r2 * r2) * std::sin(a2);

    const auto standard = pellet_offsets(5, 0.1F, 0.05F, 1, SpreadPattern::standard);
    expect(standard[0].x == plain && standard[0].y == plain_y,
           "the standard pattern uses both radii as drawn");

    const auto revolver =
        pellet_offsets(5, 0.1F, 0.05F, 1, SpreadPattern::revolver, secondary);
    expect(revolver[0].x == turned && revolver[0].y == turned_y,
           "the revolver turns both radii to 1 - r^2 in the secondary mode");

    const auto primary_shot =
        pellet_offsets(5, 0.1F, 0.05F, 1, SpreadPattern::revolver, primary);
    expect(primary_shot[0].x == plain && primary_shot[0].y == plain_y,
           "the revolver's transform needs the secondary attack type");

    const auto unknown_shot =
        pellet_offsets(5, 0.1F, 0.05F, 1, SpreadPattern::revolver, unknown);
    expect(unknown_shot[0].x == plain && unknown_shot[0].y == plain_y,
           "an unknown attack type applies no transform");

    const auto deagle_shot =
        pellet_offsets(5, 0.1F, 0.05F, 1, SpreadPattern::revolver, deagle);
    expect(deagle_shot[0].x == plain && deagle_shot[0].y == plain_y,
           "a weapon that is not the revolver applies no transform");
  }

  // The negev's `1 - r^(2^k)`: `k = 3 - the recoil index`, for the first three
  // shots of a burst, and on **both** radii.
  {
    UniformRandomStream t(9);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    const float r2 = t.random_float(0.0F, 1.0F);
    const float a2 = t.random_float(0.0F, k_two_pi);
    const auto turned = [](float value, int exponent) -> float {
      for (int step = 0; step < exponent; ++step) {
        value = value * value;
      }
      return 1.0F - value;
    };
    const auto shot = [&](int recoil_index) {
      return pellet_offsets(9, 0.1F, 0.05F, 1, SpreadPattern::negev,
                            SpreadRules{.recoil_index = recoil_index});
    };
    // The spread radius is turned with `r1`, the inaccuracy radius with `r2`.
    const auto expect_radius = [&](int exponent) {
      return 0.1F * turned(r1, exponent) * std::cos(a1) +
             0.05F * turned(r2, exponent) * std::cos(a2);
    };
    // The recoil index 0, 1, 2: the exponents 3, 2, 1.
    expect(shot(0)[0].x == expect_radius(3),
           "the negev's first shot turns the radii to 1 - r^8");
    expect(shot(1)[0].x == expect_radius(2),
           "the negev's second shot turns the radii to 1 - r^4");
    expect(shot(2)[0].x == expect_radius(1),
           "the negev's third shot turns the radii to 1 - r^2");
    // The recoil index 3 and above: no transform.
    const float plain = 0.1F * r1 * std::cos(a1) + 0.05F * r2 * std::cos(a2);
    expect(shot(3)[0].x == plain, "the negev's fourth shot applies no transform");
    expect(shot(9)[0].x == plain, "a later shot applies no transform");
    // An unknown recoil counter: no transform.
    const auto unknown_shot = pellet_offsets(9, 0.1F, 0.05F, 1, SpreadPattern::negev,
                                              SpreadRules{.recoil_index = RECOIL_INDEX_UNKNOWN});
    expect(unknown_shot[0].x == plain, "an unknown recoil index applies no transform");
  }

  // The shotgun's **pattern table**: the angle first, then the radius, and the
  // radius band cycles 0/N, 1/N, …, N-1/N, 0/N, …
  {
    expect(!shotgun_pattern_table(1234, 1).has_value(),
           "a weapon that fires one bullet has no pattern table");
    expect(!shotgun_pattern_table(1234, 0).has_value(),
           "a weapon that fires no bullets has no pattern table");
    const auto table = shotgun_pattern_table(1234, 8);
    expect(table.has_value(), "the pattern table is built");
    if (table.has_value()) {
      UniformRandomStream t(1234);
      const float angle0 = t.random_float(0.0F, k_two_pi);
      const float radius0 = t.random_float(0.0F, 0.125F);
      expect((*table)[0].angle == angle0 && (*table)[0].radius == radius0,
             "the first entry draws the angle and then the radius of the first band");
      const float angle1 = t.random_float(0.0F, k_two_pi);
      const float radius1 = t.random_float(0.125F, 0.25F);
      expect((*table)[1].angle == angle1 && (*table)[1].radius == radius1,
             "the second entry draws the second band");
      // The ninth entry cycles back to the first band: the entries 2..7 sit in
      // between.
      for (int i = 0; i < 6; ++i) {
        (void)t.random_float(0.0F, k_two_pi);
        (void)t.random_float(static_cast<float>(i + 2) / 8.0F,
                             static_cast<float>(i + 3) / 8.0F);
      }
      const float angle8 = t.random_float(0.0F, k_two_pi);
      const float radius8 = t.random_float(0.0F, 0.125F);
      expect((*table)[8].angle == angle8 && (*table)[8].radius == radius8,
             "the radius band cycles back to the first band");
    }
  }

  if (failures == 0) {
    std::printf("aim-spread: ok\n");
    return 0;
  }
  std::printf("aim-spread: %d failure(s)\n", failures);
  return 1;
}
