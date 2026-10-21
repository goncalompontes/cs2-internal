// Host tests for the Monte-Carlo hit-chance solver
// (docs/orchestration/spec-t34-montecarlo.md §2.6).
//
// The harness is the aim tests' convention: an `expect(bool, const char*)` plus a
// `failures` counter (the project keeps `b_ndebug=false`, so a test that only bites
// in the debug build is not a test — `meson.build`).

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
    for (int index = 0; index < 8; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(1000 + index));
      const auto engine = cs2::aim::pellet_offsets(seed, kernel.spread, kernel.inaccuracy,
                                                   kernel.pellets, kernel.pattern);
      const int count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{mine});
      expect(count == kernel.pellets, "the sampler draws every pellet");
      expect(engine.size() == mine.size(), "the offsets have the same count");
      for (std::size_t i = 0; i < engine.size(); ++i) {
        expect(near(engine[i].x, mine[i].x, 1e-6F) && near(engine[i].y, mine[i].y, 1e-6F),
               "the sampler's offsets are the engine's offsets");
      }
    }

    // The revolver's `r -> 1 - r^2` transform is part of the draw.
    const SpreadKernel revolver{.spread = 0.02F,
                                .inaccuracy = 0.01F,
                                .pellets = 2,
                                .pattern = cs2::aim::SpreadPattern::revolver};
    const auto seed = cs2::aim::md5_pseudo_random(4242U);
    const auto engine = cs2::aim::pellet_offsets(seed, revolver.spread, revolver.inaccuracy,
                                                 revolver.pellets, revolver.pattern);
    std::vector<Vec2> revolver_offsets(2);
    const int revolver_count =
        cs2::aim::draw_pellet_offsets(seed, revolver, std::span<Vec2>{revolver_offsets});
    expect(revolver_count == 2, "the revolver shot draws every pellet");
    for (std::size_t i = 0; i < engine.size(); ++i) {
      expect(near(engine[i].x, revolver_offsets[i].x, 1e-6F) &&
                 near(engine[i].y, revolver_offsets[i].y, 1e-6F),
             "the revolver pattern is drawn the same way");
    }

    // A golden vector: the exact offsets of one seed, so a change in the RNG itself
    // cannot pass unnoticed by the cross-check above (both sides use the same
    // stream).
    const SpreadKernel golden_kernel{.spread = 0.02F, .inaccuracy = 0.01F, .pellets = 1};
    std::vector<Vec2> golden(1);
    const int golden_count = cs2::aim::draw_pellet_offsets(cs2::aim::md5_pseudo_random(7U),
                                                           golden_kernel, std::span<Vec2>{golden});
    expect(golden_count == 1, "the golden shot draws its pellet");
    expect(near(golden[0].x, 0.00568924F, 1e-6F) && near(golden[0].y, -0.00373996F, 1e-6F),
           "the golden offset is the engine's");
  }

  // --- 2. the support is exact ----------------------------------------------
  {
    const SpreadKernel kernel{.spread = 0.03F, .inaccuracy = 0.02F, .pellets = 1};
    const float limit = kernel.spread + kernel.inaccuracy;
    std::vector<Vec2> offsets(1);
    for (int index = 0; index < 20000; ++index) {
      const auto seed = cs2::aim::md5_pseudo_random(static_cast<std::uint32_t>(index));
      const int count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
      expect(count == 1, "the support probe draws its pellet");
      const float radius = std::sqrt(offsets[0].x * offsets[0].x + offsets[0].y * offsets[0].y);
      expect(radius <= limit + 1e-6F, "every sampled offset is inside the support");
    }
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
      const int count = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
      expect(count == 1, "the radial-law probe draws its pellet");
      const float radius = std::sqrt(offsets[0].x * offsets[0].x + offsets[0].y * offsets[0].y);
      total += radius;
      largest = std::max(largest, radius);
    }
    const double mean = total / static_cast<double>(k_samples);
    expect(near(static_cast<float>(mean), 0.5F, 0.02F),
           "the mean radius is c/2 — the law is uniform in radius");
    expect(!near(static_cast<float>(mean), 2.0F / 3.0F, 0.05F),
           "the mean radius is not the area-uniform 2c/3");
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
      const int count = cs2::aim::draw_pellet_offsets(seed, pair, std::span<Vec2>{offsets});
      expect(count == 1, "the two-term probe draws its pellet");
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
    const int drawn = cs2::aim::draw_pellet_offsets(seed, kernel, std::span<Vec2>{offsets});
    expect(drawn == 2, "the union case draws both pellets");
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

    // The stand-in hash is a deterministic function of the angles and the tick.
    expect(cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U) ==
               cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U),
           "the stand-in hash is deterministic");
    expect(cs2::aim::angle_seed_hash(0.1F, -0.05F, 42U) !=
               cs2::aim::angle_seed_hash(0.1F, -0.05F, 43U),
           "the stand-in hash depends on the tick");
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
    expect(!cs2::aim::spread_kernel(shotgun, 8).has_value(),
           "the shotgun is refused instead of using the standard pattern");
    cs2::aim::WeaponProfile machinegun;
    machinegun.weapon_type = cs2::aim::WEAPON_TYPE_MACHINEGUN;
    expect(!cs2::aim::pattern_supported(machinegun), "the machinegun's pattern is not modelled");
    cs2::aim::WeaponProfile rifle;
    rifle.weapon_type = 3;
    expect(cs2::aim::pattern_supported(rifle), "a rifle's pattern is modelled");
    expect(cs2::aim::spread_kernel(rifle, 1).has_value(), "a rifle's kernel is built");
  }

  if (failures == 0) {
    std::printf("aim-montecarlo: ok\n");
    return 0;
  }
  return 1;
}
