// Host tests for the Monte-Carlo hit-chance solver
// (docs/orchestration/spec-t34-montecarlo.md §2.6).
//
// The harness is the aim tests' convention: an `expect(bool, const char*)` plus a
// `failures` counter (the project keeps `b_ndebug=false`, so a test that only bites
// in the debug build is not a test — `meson.build`).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "cs2/aim/montecarlo.hpp"
#include "cs2/aim/spread.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

// A sphere: a capsule with a zero-length axis.
auto sphere(const cs2::aim::Vec3& centre, float radius) -> cs2::aim::Capsule {
  return cs2::aim::Capsule{.a = centre, .b = centre, .radius = radius};
}

// The world point where the ray of `offset` crosses the plane x = `x`.
auto ray_point_at(const cs2::aim::Vec2& offset, float x) -> cs2::aim::Vec3 {
  const cs2::aim::Vec3 direction =
      cs2::aim::Basis{}.unproject(cs2::aim::Vec2{.x = offset.x, .y = offset.y});
  const float t = x / direction.x;
  return cs2::aim::Vec3{.x = x, .y = direction.y * t, .z = direction.z * t};
}

// A small convex cell around a chart point (counter-clockwise, so the Chebyshev
// centre and the centroid are the square's centre).
auto cell_at(const cs2::aim::Vec2& centre, float value) -> cs2::aim::Cell {
  constexpr float k_half = 0.002F;
  cs2::aim::Cell cell;
  cell.value = value;
  cell.polygon.points = {cs2::aim::Vec2{.x = centre.x - k_half, .y = centre.y - k_half},
                         cs2::aim::Vec2{.x = centre.x + k_half, .y = centre.y - k_half},
                         cs2::aim::Vec2{.x = centre.x + k_half, .y = centre.y + k_half},
                         cs2::aim::Vec2{.x = centre.x - k_half, .y = centre.y + k_half}};
  return cell;
}

}  // namespace

auto main() -> int {
  using cs2::aim::Basis;
  using cs2::aim::Capsule;
  using cs2::aim::Hitbox;
  using cs2::aim::MonteCarloInput;
  using cs2::aim::SeedContext;
  using cs2::aim::SeedSource;
  using cs2::aim::SpreadKernel;
  using cs2::aim::TargetHealth;
  using cs2::aim::Vec2;
  using cs2::aim::Vec3;

  const Basis basis{};

  // --- 1. the kernel is the engine's draw -----------------------------------
  {
    const SpreadKernel kernel{.spread = 0.02F, .inaccuracy = 0.01F, .pellets = 3};
    std::vector<Vec2> mine(3);
    int mismatches = 0;
    for (int index = 0; index < 8; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(1000 + index));
      const auto engine = cs2::aim::pellet_offsets(seed, kernel.spread, kernel.inaccuracy,
                                                   kernel.pellets, kernel.pattern);
      const auto count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{mine});
      expect(count.has_value() && *count == kernel.pellets, "the sampler draws every pellet");
      expect(engine.size() == mine.size(), "the offsets have the same count");
      for (std::size_t i = 0; i < engine.size(); ++i) {
        // **Exact equality**: the two assemblies are the same expression, so the
        // cross-check is a **drift detector** — and it can only be one if the
        // bit-identical claim is enforced. A tolerance of 1e-6 would let a
        // rounding-only regrouping through (the review's m12: the mutation
        // `spread * (r1 * cos(a1))` left the suite green).
        if (engine[i].x != mine[i].x || engine[i].y != mine[i].y) {
          ++mismatches;
        }
      }
    }
    expect(mismatches == 0, "the sampler's offsets are bit-identical to the engine's");

    // The revolver's `r -> 1 - r^2` transform is part of the draw.
    const SpreadKernel revolver{.spread = 0.02F,
                                .inaccuracy = 0.01F,
                                .pellets = 2,
                                .pattern = cs2::aim::SpreadPattern::revolver};
    const auto seed = cs2::aim::md5_pseudo_random(4242U);
    const auto engine = cs2::aim::pellet_offsets(seed, revolver.spread, revolver.inaccuracy,
                                                 revolver.pellets, revolver.pattern);
    std::vector<Vec2> revolver_offsets(2);
    const auto revolver_count =
        cs2::aim::draw_pellet_offsets(seed, revolver, std::span<Vec2>{revolver_offsets});
    expect(revolver_count.has_value() && *revolver_count == 2,
           "the revolver shot draws every pellet");
    int revolver_mismatches = 0;
    for (std::size_t i = 0; i < engine.size(); ++i) {
      if (engine[i].x != revolver_offsets[i].x || engine[i].y != revolver_offsets[i].y) {
        ++revolver_mismatches;
      }
    }
    expect(revolver_mismatches == 0, "the revolver pattern is drawn the same way");

    // A golden vector: the exact offsets of one seed, so a change in the RNG itself
    // cannot pass unnoticed by the cross-check above (both sides use the same
    // stream). The literal is a **decimal**, so it is compared with a tolerance:
    // a magic constant cannot be bit-exact across compilers (the FMA contraction
    // and the libm's `cos`/`sin` differ) — the bit-exactness is enforced by the
    // cross-check above, this pins the RNG's *value*.
    const SpreadKernel golden_kernel{.spread = 0.02F, .inaccuracy = 0.01F, .pellets = 1};
    std::vector<Vec2> golden(1);
    const auto golden_count =
        cs2::aim::draw_pellet_offsets(cs2::aim::md5_pseudo_random(7U), golden_kernel,
                                      std::span<Vec2>{golden});
    expect(golden_count.has_value() && *golden_count == 1, "the golden shot draws its pellet");
    expect(near(golden[0].x, 0.00568924F, 1e-6F) && near(golden[0].y, -0.00373996F, 1e-6F),
           "the golden offset is the engine's");
  }

  // --- 2. the support is exact ----------------------------------------------
  {
    const SpreadKernel kernel{.spread = 0.03F, .inaccuracy = 0.02F, .pellets = 1};
    const float limit = kernel.spread + kernel.inaccuracy;
    std::vector<Vec2> offsets(1);
    int outside = 0;
    for (int index = 0; index < 20000; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(index));
      const auto count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
      expect(count.has_value() && *count == 1, "the support probe draws its pellet");
      const float radius = std::sqrt(offsets[0].x * offsets[0].x + offsets[0].y * offsets[0].y);
      if (radius > limit + 1e-6F) {
        ++outside;
      }
    }
    expect(outside == 0, "every sampled offset is inside the support");
  }

  // --- 3. the radial law is uniform in radius, not area-uniform --------------
  {
    // One term (the inaccuracy is 0): the offset's radius is `spread · r` with
    // `r ~ U[0, 1]` — **uniform in radius**, so the mean radius is `spread / 2`. An
    // area-uniform disc would give `2 · spread / 3`.
    const SpreadKernel kernel{.spread = 1.0F, .inaccuracy = 0.0F, .pellets = 1};
    std::vector<Vec2> offsets(1);
    double total = 0.0;
    float largest = 0.0F;
    constexpr int k_samples = 40000;
    for (int index = 0; index < k_samples; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(index));
      const auto count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
      expect(count.has_value() && *count == 1, "the radial-law probe draws its pellet");
      const float radius = std::sqrt(offsets[0].x * offsets[0].x + offsets[0].y * offsets[0].y);
      total += radius;
      largest = std::max(largest, radius);
    }
    const double mean = total / static_cast<double>(k_samples);
    // **The measured law, not the ideal one.** The repo's `UniformRandomStream`
    // has a biased **first** output — the sampler uses it for `r1`, the spread
    // magnitude, on *every* sample: over 200 000 MD5 seeds its mean is 0.486220
    // (-21.3 sigma), while draws 2-8 are unbiased. The measured mean radius is
    // therefore **0.485476**, not 0.5: the law is uniform in radius *as the repo's
    // RNG draws it*, and the model's cone is ~3 % tighter than the ideal law
    // (`docs/research/aim-objective-math.md` §11.1).
    //
    // The test pins the **measured** band, so it can no longer hide the bias (the
    // old +/-0.02 window around 0.5 spent 73 % of its budget on the bias alone),
    // and the `sqrt(r)` variant (a mean radius of 1/3) still fails it loudly. If
    // the RNG's first draw is ever fixed, this band fails on purpose: the number
    // and the published addendum have to be updated with it.
    expect(mean > 0.480 && mean < 0.491,
           "the mean radius is the measured one - the law is uniform in radius");
    expect(!near(static_cast<float>(mean), 2.0F / 3.0F, 0.05F),
           "the mean radius is not the area-uniform 2c/3");

    // The bias itself, pinned: the first draw of a fresh stream is the low one.
    {
      double first_total = 0.0;
      for (int index = 0; index < k_samples; ++index) {
        const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(index));
        cs2::aim::UniformRandomStream stream(seed);
        first_total += stream.random_float(0.0F, 1.0F);
      }
      const double first_mean = first_total / static_cast<double>(k_samples);
      expect(first_mean > 0.480 && first_mean < 0.491,
             "the stream's first draw is the biased one (the measured 0.485476)");
    }
    // The law is uniform in radius, so the rim is sampled: the largest radius over
    // many draws reaches the support (a Gaussian would put ~0.25 % of its mass
    // beyond it, and its rim would be a tail event).
    expect(largest > 0.99F, "the support's rim is sampled");

    // Two equal terms: the support radius is `2c` and the mean radius is at most
    // `2 · (c/2) = c` (the triangle inequality), while an area-uniform disc of the
    // same support would give `4c/3`.
    const SpreadKernel pair{.spread = 1.0F, .inaccuracy = 1.0F, .pellets = 1};
    total = 0.0;
    for (int index = 0; index < k_samples; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(index));
      const auto count = cs2::aim::draw_pellet_offsets(seed, pair, std::span<Vec2>{offsets});
      expect(count.has_value() && *count == 1, "the two-term probe draws its pellet");
      total += std::sqrt(offsets[0].x * offsets[0].x + offsets[0].y * offsets[0].y);
    }
    const double pair_mean = total / static_cast<double>(k_samples);
    expect(pair_mean < 1.05, "the two-term mean radius stays below the area-uniform 4c/3");
  }

  // --- 4. the hit test is exact ----------------------------------------------
  {
    Hitbox capsule;
    capsule.is_capsule = true;
    capsule.capsule = Capsule{.a = Vec3{.x = 10.0F, .y = -3.0F, .z = 0.0F},
                              .b = Vec3{.x = 10.0F, .y = 3.0F, .z = 0.0F},
                              .radius = 1.0F};
    const Vec3 along{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const auto hit = cs2::aim::ray_hitbox(Vec3{}, along, capsule);
    expect(hit.has_value() && near(*hit, 9.0F, 1e-4F),
           "a ray through a capsule's axis hits at the near surface");

    // A ray that misses by more than the radius.
    const Vec3 off = cs2::game::normalized(Vec3{.x = 1.0F, .y = 0.0F, .z = 0.5F});
    expect(!cs2::aim::ray_hitbox(Vec3{}, off, capsule).has_value(),
           "a ray that misses by more than the radius does not hit");

    // An oriented box: the ray hits its near face at the known distance.
    Hitbox box;
    box.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                 Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F});
    const auto box_hit = cs2::aim::ray_hitbox(Vec3{}, along, box);
    expect(box_hit.has_value() && near(*box_hit, 9.0F, 1e-4F),
           "the oriented box reports the distance of its near face");

    // A hitbox entirely behind the eye.
    Hitbox behind;
    behind.is_capsule = true;
    behind.capsule = sphere(Vec3{.x = -10.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    expect(!cs2::aim::ray_hitbox(Vec3{}, along, behind).has_value(),
           "a hitbox behind the eye does not hit");

    // **Degenerate geometry is rejected, not turned into a hit** (the review's M5).
    // A NaN in a half-extent used to degenerate the box into an unbounded solid: the
    // slab clamp compares against a NaN, and every comparison with a NaN is false.
    Hitbox nan_box;
    nan_box.is_capsule = false;
    nan_box.box = cs2::aim::aabb_obb(Vec3{.x = 100.0F, .y = -1.0F, .z = -1.0F},
                                      Vec3{.x = 102.0F, .y = 1.0F, .z = 1.0F});
    nan_box.box.half.x = std::nanf("");
    expect(!cs2::aim::hitbox_valid(nan_box), "a NaN half-extent is not a well-formed solid");
    expect(!cs2::aim::ray_hitbox(Vec3{}, along, nan_box).has_value(),
           "an oriented box with a NaN half-extent does not hit");

    // A negative radius behaves as a positive one in the span code (`radius *
    // radius`), so it has to be rejected at the hit test.
    Hitbox negative;
    negative.is_capsule = true;
    negative.capsule = sphere(Vec3{.x = 10.0F, .y = 0.0F, .z = 0.0F}, -1.0F);
    expect(!cs2::aim::hitbox_valid(negative), "a negative radius is not a capsule");
    expect(!cs2::aim::ray_hitbox(Vec3{}, along, negative).has_value(),
           "a capsule with a negative radius does not hit");

    // A zero radius and a zero-size box are degenerate too.
    Hitbox zero_radius;
    zero_radius.is_capsule = true;
    zero_radius.capsule = sphere(Vec3{.x = 10.0F, .y = 0.0F, .z = 0.0F}, 0.0F);
    expect(!cs2::aim::ray_hitbox(Vec3{}, along, zero_radius).has_value(),
           "a capsule with a zero radius does not hit");
    Hitbox zero_box;
    zero_box.is_capsule = false;
    zero_box.box = cs2::aim::aabb_obb(Vec3{.x = 10.0F, .y = 0.0F, .z = 0.0F},
                                      Vec3{.x = 10.0F, .y = 0.0F, .z = 0.0F});
    expect(!cs2::aim::ray_hitbox(Vec3{}, along, zero_box).has_value(),
           "a zero-size oriented box does not hit");
  }

  // --- 4b. a corrupt hitbox cannot poison a verdict -------------------------
  {
    // The reviewer's measured failure: one OBB with a NaN half-extent in the list
    // made the whole hit chance 1.000 (the corrupt box "hit" at distance 0 and won
    // every nearest-hit contest), where the clean set gives 0.453.
    Hitbox good;
    good.is_capsule = true;
    good.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 2.0F);
    good.multiplier = 1.0F;
    good.target = 1;
    good.group = cs2::aim::HITGROUP_CHEST;

    Hitbox corrupt = good;
    corrupt.is_capsule = false;
    corrupt.target = 2;
    corrupt.box = cs2::aim::aabb_obb(Vec3{.x = 100.0F, .y = -1.0F, .z = -1.0F},
                                      Vec3{.x = 102.0F, .y = 1.0F, .z = 1.0F});
    corrupt.box.half.x = std::nanf("");

    MonteCarloInput input;
    input.weapon.damage = 35.0F;
    input.weapon.range_modifier = 0.91F;
    input.kernel = SpreadKernel{.spread = 0.05F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 64;

    const std::vector<Hitbox> clean{good};
    input.hitboxes = clean;
    const auto clean_chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(clean_chance.has_value(), "the clean scene evaluates");
    expect(near(clean_chance->probability, 0.453F, 0.02F),
           "the clean scene's hit chance is the measured one");

    const std::vector<Hitbox> poisoned{good, corrupt};
    input.hitboxes = poisoned;
    const auto poisoned_chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(!poisoned_chance.has_value(), "a scene with a corrupt hitbox is refused");
    expect(!poisoned_chance.has_value() &&
               poisoned_chance.error() == cs2::aim::MonteCarloError::invalid_geometry,
           "the refusal names the corrupt geometry");
    const auto poisoned_sample = cs2::aim::evaluate_sample(input, Vec2{}, 1234);
    expect(!poisoned_sample.has_value(), "a sample of a corrupt scene is refused too");
  }

  // --- 5. the nearest hit wins ----------------------------------------------
  {
    Hitbox near_box;
    near_box.is_capsule = true;
    near_box.capsule = sphere(Vec3{.x = 10.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    near_box.multiplier = 1.0F;
    near_box.target = 1;
    near_box.group = cs2::aim::HITGROUP_CHEST;
    Hitbox far_box = near_box;
    far_box.capsule = sphere(Vec3{.x = 20.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    far_box.multiplier = 4.0F;
    far_box.target = 2;
    far_box.group = cs2::aim::HITGROUP_HEAD;
    const std::vector<Hitbox> hitboxes{near_box, far_box};

    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.weapon.damage = 100.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = SpreadKernel{.spread = 0.0F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 16;

    const auto chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(chance.has_value(), "the nearest-hit case evaluates");
    expect(chance->source == SeedSource::sequence,
           "the verdict reports the seed policy that produced it");
    expect(chance->target == 1, "the nearest hit's target wins over the best-value hit");
    expect(chance->hitgroup == cs2::aim::HITGROUP_CHEST,
           "the nearest hit's hitgroup wins over the best-value hit");
    expect(near(chance->expected_damage, 100.0F, 1e-3F),
           "the nearest hit's damage wins over the best-value hit");
  }

  // --- 6. the union over the pellets ----------------------------------------
  {
    const SpreadKernel kernel{.spread = 0.05F, .inaccuracy = 0.0F, .pellets = 2};
    const auto seed = cs2::aim::md5_pseudo_random(4242U);
    std::vector<Vec2> offsets(2);
    const auto drawn = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
    expect(drawn.has_value() && *drawn == 2, "the union case draws both pellets");
    expect(std::fabs(offsets[0].x - offsets[1].x) > 1e-5F ||
               std::fabs(offsets[0].y - offsets[1].y) > 1e-5F,
           "the two pellets have different offsets");

    const Vec3 first = ray_point_at(offsets[0], 100.0F);
    const Vec3 second = ray_point_at(offsets[1], 100.0F);
    const float separation = cs2::game::distance(first, second);
    expect(separation > 1e-3F, "the two pellet rays are apart");
    MonteCarloInput input;
    input.weapon.damage = 40.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = kernel;
    input.samples = 1;
    input.seed = SeedContext{.sequence = 4242U};

    // A target both pellets hit: the sample is ONE hit, and the damage is the sum
    // over the pellets.
    Hitbox wide;
    wide.is_capsule = true;
    wide.capsule = sphere(
        Vec3{.x = 100.0F, .y = (first.y + second.y) * 0.5F, .z = (first.z + second.z) * 0.5F},
        separation);
    wide.multiplier = 1.0F;
    wide.target = 3;
    const std::vector<Hitbox> wide_hitboxes{wide};
    input.hitboxes = wide_hitboxes;
    const auto both = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(both.has_value(), "the union case evaluates");
    expect(both->hits == 1, "two pellets that hit are one hit");
    expect(near(both->probability, 1.0F, 1e-6F),
           "the probability is the fraction of samples with any hit");
    expect(near(both->expected_damage, 80.0F, 1e-3F), "the damage sums the pellets");

    // A target only the first pellet hits: still one hit.
    Hitbox narrow;
    narrow.is_capsule = true;
    narrow.capsule = sphere(first, separation * 0.25F);
    narrow.multiplier = 1.0F;
    narrow.target = 3;
    const std::vector<Hitbox> narrow_hitboxes{narrow};
    input.hitboxes = narrow_hitboxes;
    const auto one = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(one.has_value(), "the single-pellet case evaluates");
    expect(one->hits == 1, "one pellet that hit is one hit");
    expect(near(one->probability, 1.0F, 1e-6F), "the probability is one, not the pellet sum");
    expect(near(one->expected_damage, 40.0F, 1e-3F), "the damage is the one pellet's damage");
  }

  // --- 7. the seed policies --------------------------------------------------
  {
    const Vec2 aim{.x = 0.1F, .y = -0.05F};
    const Vec2 other{.x = 0.2F, .y = -0.05F};
    SeedContext sequence{.sequence = 777U, .tick = 42U, .source = SeedSource::sequence};
    const auto first = cs2::aim::sample_seed(sequence, aim, 0, 64);
    expect(first == cs2::aim::sample_seed(sequence, aim, 0, 64),
           "the sequence policy is deterministic");
    expect(first != cs2::aim::sample_seed(sequence, aim, 1, 64),
           "the sequence policy varies with the sample index");
    expect(first == cs2::aim::sample_seed(sequence, other, 0, 64),
           "the sequence policy does not depend on the aim");

    SeedContext grid = sequence;
    grid.source = SeedSource::angle_grid;
    const auto probe = cs2::aim::sample_seed(grid, aim, 0, 64);
    expect(probe == cs2::aim::sample_seed(grid, aim, 0, 64),
           "the angle_grid policy is deterministic");
    expect(probe != cs2::aim::sample_seed(grid, aim, 1, 64),
           "the angle_grid policy varies with the probe index");
    expect(probe != cs2::aim::sample_seed(grid, other, 0, 64),
           "the angle_grid policy varies with the aim");
    expect(probe != first, "the two policies differ");

    // The verdict carries the policy that produced it.
    Hitbox box;
    box.is_capsule = true;
    box.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 2.0F);
    box.target = 5;
    const std::vector<Hitbox> hitboxes{box};
    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.kernel = SpreadKernel{.spread = 0.01F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 8;
    input.seed = grid;
    const auto verdict = cs2::aim::evaluate_hit_chance(input, aim);
    expect(verdict.has_value(), "the angle_grid verdict evaluates");
    expect(verdict->source == SeedSource::angle_grid, "the verdict reports the angle_grid policy");

    // The stand-in hash is a deterministic function of the angles and the tick.
    expect(cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U) ==
               cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U),
           "the stand-in hash is deterministic");
    expect(cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U) !=
               cs2::aim::angle_seed_hash(0.1F, -0.05F, 43U),
           "the stand-in hash depends on the tick");
    // The hash is injective on the **grid**, not on the raw angles: the
    // quantisation is deliberate (the stand-in models "the same aim gives the same
    // seed"), so two angles within one quantum share a seed — and two neighbouring
    // probes never do.
    expect(cs2::aim::angle_seed_hash(0.3F, -0.1F, 55U) ==
               cs2::aim::angle_seed_hash(0.30001F, -0.1F, 55U),
           "the stand-in hash is quantised: angles within one quantum share a seed");
    expect(cs2::aim::angle_seed_hash(0.3F, -0.1F, 55U) !=
               cs2::aim::angle_seed_hash(0.3F + grid.probe_step, -0.1F, 55U),
           "two neighbouring probes never share a seed");

    // **The candidate's own seed is in the sample set** (the review's m1: with a
    // half-integer centre the centre index was 31.5/63.5/127.5 and the
    // candidate's own seed was never drawn).
    for (const int count : {64, 128, 256}) {
      const auto own =
          cs2::aim::md5_pseudo_random(cs2::aim::angle_seed_hash(aim.x, aim.y, grid.tick));
      bool found = false;
      for (int index = 0; index < count; ++index) {
        if (cs2::aim::sample_seed(grid, aim, index, count) == own) {
          found = true;
        }
      }
      expect(found, "the angle_grid policy samples the candidate's own seed");
    }
  }

  // --- 7b. a tied nearest hit is broken by the rule, not by the input order ----
  {
    // Two coincident hitboxes: the head (multiplier 4) and the chest (multiplier
    // 1). The rule's third key (the hitgroup preference) decides, whichever order
    // the caller lists them in (the review's m2: `[head, body]` and
    // `[body, head]` used to report different hitgroups and different damage).
    Hitbox head;
    head.is_capsule = true;
    head.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 2.0F);
    head.multiplier = 4.0F;
    head.target = 1;
    head.group = cs2::aim::HITGROUP_HEAD;
    Hitbox body = head;
    body.multiplier = 1.0F;
    body.group = cs2::aim::HITGROUP_CHEST;

    MonteCarloInput input;
    input.weapon.damage = 35.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = SpreadKernel{.spread = 0.0F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 4;

    const std::vector<Hitbox> head_first{head, body};
    input.hitboxes = head_first;
    const auto first = cs2::aim::evaluate_hit_chance(input, Vec2{});
    const std::vector<Hitbox> body_first{body, head};
    input.hitboxes = body_first;
    const auto second = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(first.has_value() && second.has_value(), "the tied case evaluates");
    expect(first->hitgroup == cs2::aim::HITGROUP_HEAD &&
               second->hitgroup == cs2::aim::HITGROUP_HEAD,
           "the tie is broken by the hitgroup preference, not by the input order");
    expect(near(first->expected_damage, second->expected_damage, 1e-6F),
           "the tied case reports the same damage in both orders");
    expect(near(first->expected_damage, 140.0F, 1e-3F),
           "the tied case reports the head's damage");
  }

  // --- 8. the selection rule -------------------------------------------------
  {
    // The eye at the origin; a small head sphere at (100, 0, 0) and a fat body
    // capsule below it.
    //
    // The kernel is the **spread-only** regime (the inaccuracy is 0), so the offset's
    // radius is uniform in `[0, spread]` and the head's hit probability is exactly
    // `head_radius / distance / spread` — the head subtends half the cone, so half
    // the samples hit it; the body is wider than the cone, so every sample hits it.
    Hitbox head;
    head.is_capsule = true;
    head.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 1.0F);
    head.multiplier = 4.0F;
    head.target = 7;
    head.group = cs2::aim::HITGROUP_HEAD;
    Hitbox body = head;
    body.capsule = Capsule{.a = Vec3{.x = 100.0F, .y = 0.0F, .z = -38.0F},
                           .b = Vec3{.x = 100.0F, .y = 0.0F, .z = -22.0F},
                           .radius = 8.0F};
    body.multiplier = 1.0F;
    body.group = cs2::aim::HITGROUP_CHEST;
    const std::vector<Hitbox> hitboxes{head, body};

    const auto head_chart = basis.project(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F});
    const auto body_chart = basis.project(Vec3{.x = 100.0F, .y = 0.0F, .z = -30.0F});
    expect(head_chart.has_value() && body_chart.has_value(), "the targets project");
    const std::vector<cs2::aim::Cell> cells{cell_at(*head_chart, 100.0F),
                                            cell_at(*body_chart, 25.0F)};

    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.weapon.damage = 25.0F;
    input.weapon.range_modifier = 1.0F;
    input.weapon.weapon_type = 3;  // a rifle: the standard pattern is modelled
    input.kernel = SpreadKernel{.spread = 0.02F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 64;
    const TargetHealth health{.index = 7, .health = 30.0F};
    input.targets = std::span<const TargetHealth>{&health, 1};

    // The head candidate: a lower hit probability than the body, but it can kill.
    const auto head_chance = cs2::aim::evaluate_hit_chance(input, *head_chart);
    const auto body_chance = cs2::aim::evaluate_hit_chance(input, *body_chart);
    expect(head_chance.has_value() && body_chance.has_value(), "the candidates evaluate");
    expect(head_chance->probability < body_chance->probability, "the head is the harder target");
    expect(head_chance->expected_damage >= 30.0F, "the head's expected damage kills");
    expect(body_chance->expected_damage < 30.0F, "the body's expected damage does not kill");

    const auto choice = cs2::aim::solve_montecarlo(input, cells);
    expect(choice.has_value(), "the selection solves");
    expect(choice->can_kill, "the winner can kill");
    expect(near(choice->aim.x, head_chart->x, 1e-3F) && near(choice->aim.y, head_chart->y, 1e-3F),
           "the candidate that can kill wins over the one with the higher probability");

    // With no spread both candidates hit for certain, so the hitgroup preference
    // decides (and neither can kill).
    input.kernel = SpreadKernel{.spread = 0.0F, .inaccuracy = 0.0F, .pellets = 1};
    const TargetHealth unreachable{.index = 7, .health = 1000.0F};
    input.targets = std::span<const TargetHealth>{&unreachable, 1};
    const auto tie = cs2::aim::solve_montecarlo(input, cells);
    expect(tie.has_value(), "the tie case solves");
    expect(!tie->can_kill, "neither candidate can kill");
    expect(tie->hitgroup == cs2::aim::HITGROUP_HEAD, "the head is preferred over the body");
    expect(near(tie->aim.x, head_chart->x, 1e-3F) && near(tie->aim.y, head_chart->y, 1e-3F),
           "the head candidate wins the tie");
  }

  // --- 9. the weapons whose pattern is not modelled are refused -------------
  {
    cs2::aim::WeaponProfile shotgun;
    shotgun.weapon_type = cs2::aim::WEAPON_TYPE_SHOTGUN;
    expect(!cs2::aim::pattern_supported(shotgun), "the shotgun's pattern table is not modelled");
    expect(!cs2::aim::spread_kernel(shotgun, 8, cs2::aim::SpreadPattern::standard).has_value(),
           "the shotgun is refused instead of using the standard pattern");
    cs2::aim::WeaponProfile machinegun;
    machinegun.weapon_type = cs2::aim::WEAPON_TYPE_MACHINEGUN;
    expect(!cs2::aim::pattern_supported(machinegun), "the machinegun's pattern is not modelled");
    cs2::aim::WeaponProfile rifle;
    rifle.weapon_type = 3;
    expect(cs2::aim::pattern_supported(rifle), "a rifle's pattern is modelled");
    expect(cs2::aim::spread_kernel(rifle, 1, cs2::aim::SpreadPattern::standard).has_value(),
           "a rifle's kernel is built");

    // **Every** entry point that can produce a number refuses the unmodelled
    // weapons — not only the ones that build a kernel (the review's M1: the
    // cheap entry point was the one that leaked the standard pattern).
    Hitbox box;
    box.is_capsule = true;
    box.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 2.0F);
    box.multiplier = 1.0F;
    box.target = 1;
    const std::vector<Hitbox> hitboxes{box};
    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.weapon = shotgun;
    input.weapon.damage = 35.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = SpreadKernel{.spread = 0.05F, .inaccuracy = 0.0F, .pellets = 8};
    input.samples = 16;
    const auto chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(!chance.has_value(), "evaluate_hit_chance refuses the shotgun");
    expect(!chance.has_value() && chance.error() == cs2::aim::MonteCarloError::unsupported_pattern,
           "the refusal names the unmodelled pattern");
    const auto sample = cs2::aim::evaluate_sample(input, Vec2{}, 1234);
    expect(!sample.has_value(), "evaluate_sample refuses the shotgun");
    expect(!sample.has_value() && sample.error() == cs2::aim::MonteCarloError::unsupported_pattern,
           "the sample's refusal names the unmodelled pattern");
    const std::vector<cs2::aim::Cell> cells{cell_at(Vec2{}, 1.0F)};
    const auto choice = cs2::aim::solve_montecarlo(input, cells);
    expect(!choice.has_value(), "the search refuses the shotgun");

    // The same input with a modelled weapon evaluates (the guard is the weapon's,
    // not the input's).
    input.weapon = rifle;
    expect(cs2::aim::evaluate_hit_chance(input, Vec2{}).has_value(),
           "the modelled weapon still evaluates");
    expect(cs2::aim::evaluate_sample(input, Vec2{}, 1234).has_value(),
           "the modelled weapon still draws its samples");
  }

  // --- 10. the can-kill key is judged per target ----------------------------
  {
    // Two targets: A is a chest at 100 units (35 damage, 40 hp left) and B is a
    // head at 100.4 units (140 damage, 100 hp). The cone covers both, so the
    // scene's mean damage (45.25) is the mixture of A's 35 and B's 140 — but **A
    // can never die** from a 35-damage chest hit. The rule's first key must be
    // judged against A's **own** damage (the review's M2: the aggregate was
    // attributed to one target, and the key beat every honest candidate).
    Hitbox a;
    a.is_capsule = true;
    a.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 3.0F);
    a.multiplier = 1.0F;
    a.target = 1;
    a.group = cs2::aim::HITGROUP_CHEST;
    Hitbox b;
    b.is_capsule = true;
    b.capsule = sphere(Vec3{.x = 100.4F, .y = 2.4F, .z = 0.0F}, 3.0F);
    b.multiplier = 4.0F;
    b.target = 2;
    b.group = cs2::aim::HITGROUP_HEAD;
    const std::vector<Hitbox> hitboxes{a, b};
    const std::vector<TargetHealth> health{TargetHealth{.index = 1, .health = 40.0F},
                                           TargetHealth{.index = 2, .health = 100.0F}};
    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.targets = health;
    input.weapon.damage = 35.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = SpreadKernel{.spread = 0.03F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 256;

    const auto chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(chance.has_value(), "the two-target case evaluates");
    expect(chance->target == 1, "the nearest hit names the chest target");
    expect(near(chance->expected_damage, 45.25F, 0.05F),
           "the scene's mean damage is the mixture of the two targets");
    expect(chance->target_damage <= 35.0F,
           "the damage attributed to the chest target is at most one chest hit");
    expect(chance->target_damage < chance->expected_damage,
           "the attributed damage is not the scene's aggregate");
    expect(chance->target_damage < 40.0F, "the chest target cannot die from that damage");

    const std::vector<cs2::aim::Cell> cells{cell_at(Vec2{}, 1.0F)};
    const auto choice = cs2::aim::solve_montecarlo(input, cells);
    expect(choice.has_value(), "the two-target search solves");
    expect(!choice->can_kill, "a target that cannot die is not a kill");

    // The single-target case is unchanged: every sample's damage belongs to the
    // one target, so the attributed damage is the scene's mean.
    const std::vector<Hitbox> one_target{a};
    input.hitboxes = one_target;
    const TargetHealth only{.index = 1, .health = 40.0F};
    input.targets = std::span<const TargetHealth>{&only, 1};
    const auto single = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(single.has_value(), "the single-target case evaluates");
    expect(near(single->target_damage, single->expected_damage, 1e-3F),
           "one target: the attributed damage is the scene's mean");
  }

  // --- 11. a too-small buffer is an error, not a silent truncation ------------
  {
    const SpreadKernel kernel{.spread = 0.02F, .inaccuracy = 0.01F, .pellets = 3};
    std::vector<Vec2> small(2);
    const auto truncated = cs2::aim::draw_pellet_offsets(1234, kernel, std::span<Vec2>{small});
    expect(!truncated.has_value(), "a two-slot buffer cannot hold three pellets");
    expect(!truncated.has_value() &&
               truncated.error() == cs2::aim::MonteCarloError::buffer_too_small,
           "the refusal names the too-small buffer");
    std::vector<Vec2> enough(3);
    const auto drawn = cs2::aim::draw_pellet_offsets(1234, kernel, std::span<Vec2>{enough});
    expect(drawn.has_value() && *drawn == 3, "a buffer that fits draws every pellet");
  }

  // --- 12. G1: a NaN damage multiplier is refused ---------------------------
  {
    // The geometry was validated and the **multiplier** was not, so a malformed
    // hitbox slipped past the "the solver refuses a malformed hitbox" guarantee:
    // a NaN multiplier made every damage product a NaN, and *every comparison
    // against a NaN is false* — so the selection rule kept the **first**
    // candidate regardless of its quality and returned a NaN verdict **with no
    // error** (the review's G1).
    Hitbox box;
    box.is_capsule = true;
    box.capsule = sphere(Vec3{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 2.0F);
    box.multiplier = 1.0F;
    box.target = 1;
    const std::vector<Hitbox> clean{box};
    MonteCarloInput input;
    input.hitboxes = clean;
    input.weapon.damage = 35.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = SpreadKernel{.spread = 0.02F, .inaccuracy = 0.0F, .pellets = 1};
    input.samples = 16;
    const auto clean_chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(clean_chance.has_value(), "the clean scene evaluates");
    expect(clean_chance.has_value() && std::isfinite(clean_chance->expected_damage),
           "the clean scene's damage is a number");

    Hitbox corrupt = box;
    corrupt.multiplier = std::nanf("");
    expect(!cs2::aim::hitbox_valid(corrupt), "a NaN multiplier is not a well-formed hitbox");
    const std::vector<Hitbox> poisoned{corrupt};
    input.hitboxes = poisoned;
    const auto poisoned_chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(!poisoned_chance.has_value(), "a scene with a NaN multiplier is refused");
    expect(!poisoned_chance.has_value() &&
               poisoned_chance.error() == cs2::aim::MonteCarloError::invalid_geometry,
           "the refusal names the malformed hitbox");
    const auto poisoned_sample = cs2::aim::evaluate_sample(input, Vec2{}, 1234);
    expect(!poisoned_sample.has_value(), "a sample of a NaN-multiplier scene is refused too");
    const auto poisoned_search =
        cs2::aim::solve_montecarlo(input, std::span<const cs2::aim::Cell>{});
    expect(!poisoned_search.has_value(), "the search refuses a NaN-multiplier scene");
  }

  // --- 13. G2: the attribution is per pellet, not per sample -----------------
  {
    // Two pellets, two targets: one pellet hits the **near** target (a chest, 40
    // damage) and the other the **far** one (a head, 160 damage). The sample's
    // whole damage used to be credited to the target of the **nearest** hit, so
    // the near target's slot carried 200 — the M2 bug one level down, and the
    // can-kill key then compared one target's health with two targets' damage.
    const SpreadKernel kernel{.spread = 0.05F, .inaccuracy = 0.0F, .pellets = 2};
    const auto seed = cs2::aim::md5_pseudo_random(4242U);
    std::vector<Vec2> offsets(2);
    const auto drawn = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
    expect(drawn.has_value() && *drawn == 2, "the attribution case draws both pellets");
    const Vec3 first = ray_point_at(offsets[0], 100.0F);
    const Vec3 second = ray_point_at(offsets[1], 100.0F);
    const float separation = cs2::game::distance(first, second);
    expect(separation > 1e-3F, "the two pellet rays are apart");
    // The nearer ray's target is the one the sample's nearest hit names.
    const bool first_is_nearer =
        cs2::game::distance(first, Vec3{}) < cs2::game::distance(second, Vec3{});
    const Vec3 near_point = first_is_nearer ? first : second;
    const Vec3 far_point = first_is_nearer ? second : first;

    Hitbox near_box;
    near_box.is_capsule = true;
    near_box.capsule = sphere(near_point, separation * 0.25F);
    near_box.multiplier = 1.0F;
    near_box.target = 1;
    near_box.group = cs2::aim::HITGROUP_CHEST;
    Hitbox far_box = near_box;
    far_box.capsule = sphere(far_point, separation * 0.25F);
    far_box.multiplier = 4.0F;
    far_box.target = 2;
    far_box.group = cs2::aim::HITGROUP_HEAD;
    const std::vector<Hitbox> hitboxes{near_box, far_box};
    const std::vector<TargetHealth> health{TargetHealth{.index = 1, .health = 40.0F},
                                           TargetHealth{.index = 2, .health = 1000.0F}};
    MonteCarloInput input;
    input.hitboxes = hitboxes;
    input.targets = health;
    input.weapon.damage = 40.0F;
    input.weapon.range_modifier = 1.0F;
    input.kernel = kernel;
    input.samples = 1;
    input.seed = SeedContext{.sequence = 4242U};

    const auto chance = cs2::aim::evaluate_hit_chance(input, Vec2{});
    expect(chance.has_value(), "the two-target, two-pellet case evaluates");
    expect(chance.has_value() && chance->hits == 1, "the sample hit");
    // The scene's mean: the sum over the pellets that hit.
    expect(chance.has_value() && near(chance->expected_damage, 200.0F, 1e-3F),
           "the scene's mean damage is the sum over the pellets");
    // The near target's **own** damage: 40, not the 200 the old attribution gave it.
    expect(chance.has_value() && chance->target == 1, "the nearest hit names the near target");
    expect(chance.has_value() && near(chance->target_damage, 40.0F, 1e-3F),
           "the damage attributed to a target is the damage that target took");
    expect(chance.has_value() && chance->target_damage < chance->expected_damage,
           "the attributed damage is not the scene's aggregate");

    // The per-pellet attribution is also visible in the sample itself: each pellet
    // that hit carries its own damage and its own target.
    const auto sample = cs2::aim::evaluate_sample(input, Vec2{}, seed);
    expect(sample.has_value(), "the sample of the two-target case evaluates");
    expect(sample.has_value() && sample->pellets_hit == 2,
           "both pellets are attributed separately");
    expect(sample.has_value() && sample->pellet_target[0] != sample->pellet_target[1],
           "the two pellets hit two different targets");
    expect(sample.has_value() && near(sample->pellet_damage[0] + sample->pellet_damage[1],
                                       sample->damage, 1e-3F),
           "the per-pellet damage sums to the sample's damage");
  }

  if (failures == 0) {
    std::printf("aim-montecarlo: ok\n");
    return 0;
  }
  return 1;
}
