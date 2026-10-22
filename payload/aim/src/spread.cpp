#include "cs2/aim/spread.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cs2::aim {
namespace {

auto rotl32(std::uint32_t value, int count) -> std::uint32_t {
  return (value << count) | (value >> (32 - count));
}

// MD5 (RFC 1321) of a single <= 55-byte block; returns the first digest dword (LE).
// `MD5_PseudoRandom` only ever hashes 4 bytes, so a one-block implementation is exact.
auto md5_first_dword(const std::uint8_t* data, std::size_t length) -> std::uint32_t {
  static constexpr std::uint32_t k_k[64] = {
      0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U,
      0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U,
      0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU,
      0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
      0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
      0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
      0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U,
      0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
      0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU,
      0xeb86d391U};
  static constexpr int k_s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

  std::uint8_t block[64] = {};
  for (std::size_t i = 0; i < length; ++i) {
    block[i] = data[i];
  }
  block[length] = 0x80U;
  const std::uint64_t bits = static_cast<std::uint64_t>(length) * 8U;
  for (int i = 0; i < 8; ++i) {
    block[56 + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xffU);
  }

  std::uint32_t m[16];
  for (int i = 0; i < 16; ++i) {
    m[i] = static_cast<std::uint32_t>(block[i * 4]) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 8) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 3]) << 24);
  }

  std::uint32_t a = 0x67452301U;
  std::uint32_t b = 0xefcdab89U;
  std::uint32_t c = 0x98badcfeU;
  std::uint32_t d = 0x10325476U;
  for (int i = 0; i < 64; ++i) {
    std::uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) & 15;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) & 15;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) & 15;
    }
    f += a + k_k[i] + m[g];
    a = d;
    d = c;
    c = b;
    b += rotl32(f, k_s[i]);
  }
  return 0x67452301U + a;
}

}  // namespace

void UniformRandomStream::set_seed(std::int32_t seed) noexcept {
  // `CUniformRandomStream::SetSeed` stores the negated seed (or the seed when it is
  // already negative); `random_int` re-negates it on the first draw. Storing it
  // positive collapses every seed to 1.
  idum_ = seed < 0 ? seed : -seed;
  iy_ = 0;
}

auto UniformRandomStream::random_int() noexcept -> std::int32_t {
  constexpr std::int32_t k_ia = 16807;
  constexpr std::int32_t k_im = 2147483647;
  constexpr std::int32_t k_iq = 127773;
  constexpr std::int32_t k_ir = 2836;
  constexpr std::int32_t k_ntab = 32;
  constexpr std::int32_t k_ndiv = 1 + (k_im - 1) / k_ntab;

  std::int32_t j;
  std::int32_t k;
  if (idum_ <= 0 || iy_ == 0) {
    if (-(idum_) < 1) {
      idum_ = 1;
    } else {
      idum_ = -(idum_);
    }
    for (j = k_ntab + 7; j >= 0; --j) {
      k = idum_ / k_iq;
      idum_ = k_ia * (idum_ - k * k_iq) - k_ir * k;
      if (idum_ < 0) {
        idum_ += k_im;
      }
      if (j < k_ntab) {
        iv_[j] = idum_;
      }
    }
    iy_ = iv_[0];
  }
  k = idum_ / k_iq;
  idum_ = k_ia * (idum_ - k * k_iq) - k_ir * k;
  if (idum_ < 0) {
    idum_ += k_im;
  }
  j = iy_ / k_ndiv;
  iy_ = iv_[j];
  iv_[j] = idum_;
  return iy_;
}

auto UniformRandomStream::random_float(float low, float high) noexcept -> float {
  constexpr double k_am = 1.0 / 2147483647.0;
  constexpr double k_rnmx = 1.0 - 1.2e-7;  // 1.0 - EPS
  double value = k_am * static_cast<double>(random_int());
  value = std::min(value, k_rnmx);
  return static_cast<float>(value * (static_cast<double>(high) - static_cast<double>(low)) +
                            static_cast<double>(low));
}

auto md5_pseudo_random(std::uint32_t sequence) noexcept -> std::int32_t {
  const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(sequence & 0xffU),
                                 static_cast<std::uint8_t>((sequence >> 8) & 0xffU),
                                 static_cast<std::uint8_t>((sequence >> 16) & 0xffU),
                                 static_cast<std::uint8_t>((sequence >> 24) & 0xffU)};
  return static_cast<std::int32_t>(md5_first_dword(bytes, 4));
}

auto shotgun_pattern_table(int spread_seed, int num_bullets) noexcept
    -> std::optional<std::array<ShotgunPatternEntry, PATTERN_TABLE_ENTRIES>> {
  // The engine marks the block invalid when the weapon fires fewer than two
  // bullets, and the lookup then falls back to the random cone.
  if (num_bullets < 2) {
    return std::nullopt;
  }
  // The engine clamps the bullet count to the table's length.
  int count = num_bullets;
  if (count > PATTERN_TABLE_ENTRIES) {
    count = PATTERN_TABLE_ENTRIES;
  }
  // **The band bounds are computed in single precision, like the engine's**
  // (`(float)k * (1.0f / (float)N)`, `client.dll` `0x1807cb2b4`/`0x1807cb2ee`).
  // In double the reciprocal and the product round differently: over N = 1…64
  // the two computations disagree on 464 of the 2 144 band boundaries (`k/N`) by
  // **one ulp** (5.96e-8), which moves the drawn radius by one ulp on the bands
  // it touches.
  const float inverse = 1.0F / static_cast<float>(count);
  std::array<ShotgunPatternEntry, PATTERN_TABLE_ENTRIES> table{};
  UniformRandomStream stream(spread_seed);
  int k = 0;
  for (int index = 0; index < PATTERN_TABLE_ENTRIES; ++index) {
    // The engine's draw order: the **angle** first, then the radius.
    const float angle = stream.random_float(0.0F, TWO_PI);
    const float low = static_cast<float>(k) * inverse;
    const float high = static_cast<float>(k + 1) * inverse;
    const float radius = stream.random_float(low, high);
    table[static_cast<std::size_t>(index)] =
        ShotgunPatternEntry{.angle = angle, .radius = radius};
    // The radius band cycles: 0, 1, …, N-1, 0, 1, …
    k = (k + 1) % count;
  }
  return table;
}

auto pellet_offsets(std::int32_t random_seed, float spread, float inaccuracy, int pellets,
                    SpreadPattern pattern, const SpreadRules& rules) -> std::vector<Vec2> {
  std::vector<Vec2> offsets;
  if (pellets <= 0) {
    return offsets;
  }
  UniformRandomStream stream(random_seed);
  offsets.reserve(static_cast<std::size_t>(pellets));

  const RadiusTransform transform = radius_transform(pattern, rules);
  // The shotgun's pattern table: the engine builds it once per weapon and reads
  // the entry the shot's index names. It is only read when the convar is on.
  std::optional<std::array<ShotgunPatternEntry, PATTERN_TABLE_ENTRIES>> table;
  if (rules.shotgun_patterns) {
    table = shotgun_pattern_table(rules.spread_seed, rules.num_bullets);
  }

  // The **spread** radius and angle: drawn once for the shot (pellet 0), and
  // again for every pellet when the shotgun-pattern convar is on. The engine
  // keeps the first pair and reuses it for the later pellets otherwise.
  float r1 = 0.0F;
  float a1 = 0.0F;
  for (int i = 0; i < pellets; ++i) {
    if (i == 0 || rules.shotgun_patterns) {
      r1 = stream.random_float(0.0F, 1.0F);
      a1 = stream.random_float(0.0F, TWO_PI);
      if (transform == RadiusTransform::revolver) {
        r1 = 1.0F - r1 * r1;
      } else if (transform == RadiusTransform::negev) {
        for (int step = 0; step < negev_exponent(rules.recoil_index); ++step) {
          r1 = r1 * r1;
        }
        r1 = 1.0F - r1;
      }
    }
    // The **inaccuracy** radius and angle: drawn per pellet.
    float r2 = 0.0F;
    float a2 = 0.0F;
    if (rules.shotgun_patterns) {
      // The pattern table: the entry the shot's index names, or the random
      // cone when the index is out of the table's range.
      const int index = rules.recoil_index * rules.num_bullets + i;
      if (table.has_value() && index >= 0 && index < PATTERN_TABLE_ENTRIES) {
        const ShotgunPatternEntry& entry = (*table)[static_cast<std::size_t>(index)];
        a2 = entry.angle;
        r2 = entry.radius;
      } else {
        // The engine's fallback draws the **angle** first, then the radius.
        a2 = stream.random_float(0.0F, TWO_PI);
        r2 = stream.random_float(0.0F, 1.0F);
      }
    } else {
      r2 = stream.random_float(0.0F, 1.0F);
      a2 = stream.random_float(0.0F, TWO_PI);
    }
    if (transform == RadiusTransform::revolver) {
      r2 = 1.0F - r2 * r2;
    } else if (transform == RadiusTransform::negev) {
      for (int step = 0; step < negev_exponent(rules.recoil_index); ++step) {
        r2 = r2 * r2;
      }
      r2 = 1.0F - r2;
    }
    offsets.push_back(Vec2{.x = spread * r1 * std::cos(a1) + inaccuracy * r2 * std::cos(a2),
                           .y = spread * r1 * std::sin(a1) + inaccuracy * r2 * std::sin(a2)});
  }
  return offsets;
}

}  // namespace cs2::aim
