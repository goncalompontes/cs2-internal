// Unit tests for the deterministic spread model (docs/ragebot.md §2/§9).

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
  using cs2::aim::md5_pseudo_random;
  using cs2::aim::pellet_offsets;
  using cs2::aim::SpreadPattern;
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

  // The pellet offsets follow the documented draw order.
  {
    const auto offsets = pellet_offsets(12345, 0.01F, 0.02F, 3);
    expect(offsets.size() == 3, "one offset per pellet");
    UniformRandomStream t(12345);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    const float r2 = t.random_float(0.0F, 1.0F);
    const float a2 = t.random_float(0.0F, k_two_pi);
    const Vec2 expected{0.01F * r1 * std::cos(a1) + 0.02F * r2 * std::cos(a2),
                        0.01F * r1 * std::sin(a1) + 0.02F * r2 * std::sin(a2)};
    expect(near(offsets[0].x, expected.x) && near(offsets[0].y, expected.y),
           "the first pellet follows the recipe");
    expect(offsets[0].x != offsets[1].x || offsets[0].y != offsets[1].y, "pellets differ");
  }

  // A zero-spread weapon fires straight.
  for (const Vec2& offset : pellet_offsets(7, 0.0F, 0.0F, 9)) {
    expect(offset.x == 0.0F && offset.y == 0.0F, "zero spread -> zero offset");
  }

  // The revolver pattern widens the spread radius `r -> 1 - r^2`.
  {
    const auto plain = pellet_offsets(5, 0.1F, 0.0F, 1, SpreadPattern::standard);
    const auto revolver = pellet_offsets(5, 0.1F, 0.0F, 1, SpreadPattern::revolver);
    UniformRandomStream t(5);
    const float r1 = t.random_float(0.0F, 1.0F);
    const float a1 = t.random_float(0.0F, k_two_pi);
    expect(near(plain[0].x, 0.1F * r1 * std::cos(a1)), "plain spread radius is r");
    expect(near(revolver[0].x, 0.1F * (1.0F - r1 * r1) * std::cos(a1)),
           "revolver spread radius is 1 - r^2");
  }

  if (failures == 0) {
    std::printf("aim-spread: ok\n");
    return 0;
  }
  std::printf("aim-spread: %d failure(s)\n", failures);
  return 1;
}
