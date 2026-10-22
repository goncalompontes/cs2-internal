#include <cmath>
#include <cstdio>
#include <limits>

#include "cs2/aim/solver.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* what) {
  if (!value) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
auto near(float a, float b, float tolerance = .001F) -> bool {
  return std::abs(a - b) < tolerance;
}
using namespace cs2::aim;
auto box(Vec3 lo, Vec3 hi, float multiplier = 1) -> Hitbox {
  Hitbox h;
  h.box = aabb_obb(lo, hi);
  h.multiplier = multiplier;
  h.group = 2;
  return h;
}
auto base() -> SolveInput {
  SolveInput in;
  in.basis = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  in.weapon.damage = 100;
  in.objective = AimObjective::expected;
  in.spread_known = true;
  in.world_complete = true;
  in.hitboxes.push_back(box({9, -1, -1}, {11, 1, 1}));
  return in;
}
}  // namespace
auto main() -> int {
  auto input = base();
  // Overlapping projections are NOT overlapping damage contributions.
  input.hitboxes.push_back(box({12, -1, -1}, {14, 1, 1}, 4));
  auto metrics = evaluate_aim(input, {0, 0});
  expect(near(metrics.nominal_damage, 100) && metrics.nominal_hitbox == 0,
         "near body prevents shooting through it to a hidden high-damage hitbox");
  auto ray = evaluate_ray({}, {1, 0, 0}, input.hitboxes, input.weapon, {}, {}, true);
  expect(near(ray.distance, 9), "range falloff uses actual first-surface distance");

  input = base();
  input.world.push_back({aabb_obb({20, -5, -5}, {22, 5, 5}), 0});
  expect(near(evaluate_aim(input, {0, 0}).expected_damage, 100), "wall behind target ignored");
  input.world[0].box = aabb_obb({4, -5, -5}, {6, 5, 5});
  expect(evaluate_aim(input, {0, 0}).expected_damage == 0 && !solve(input).valid,
         "opaque world geometry blocks target damage");
  input.world[0].retained_per_unit = .5F;
  expect(near(evaluate_aim(input, {0, 0}).expected_damage, 25),
         "supplied transmission uses full interval thickness");
  const auto oblique = cs2::game::normalized(Vec3{1, .1F, 0});
  ray = evaluate_ray({}, oblique, input.hitboxes, input.weapon, {}, input.world, true);
  expect(near(ray.damage, 100 * std::pow(.5F, 2 / oblique.x)), "oblique wall thickness is longer");
  input.world_complete = false;
  input.world_query = [](Vec3, Vec3, float, const WeaponProfile&) { return WorldDamage{37, true}; };
  expect(near(evaluate_aim(input, {0, 0}).expected_damage, 37) &&
             evaluate_aim(input, {0, 0}).world_known,
         "authoritative world callback replaces the approximate material model");
  input.world_query = [](Vec3, Vec3, float, const WeaponProfile&) { return WorldDamage{37, true, 1}; };
  expect(near(evaluate_aim(input, {0, 0}).expected_damage, 37 * input.weapon.headshot_multiplier),
         "verified engine hitgroup overrides the proposal hitgroup");
  input.world_query = [](Vec3, Vec3, float, const WeaponProfile&) { return WorldDamage{37, true}; };
  input.target_query = [](float base, const Hitbox&, const WeaponProfile&, const TargetDefense&) {
    return base * .5F;
  };
  expect(near(evaluate_aim(input, {0, 0}).expected_damage, 18.5F),
         "target damage callback replaces the armor/hitgroup fallback");
  input.target_query = {};
  input.world_query = [](Vec3, Vec3, float, const WeaponProfile&) {
    return WorldDamage{999, false};
  };
  expect(!solve(input).valid, "unresolved world callback cannot invent damage");

  Capsule capsule{{10, -2, 0}, {10, 2, 0}, 1};
  auto hit = intersect_capsule({}, {1, 0, 0}, capsule, 100);
  expect(hit && near(*hit, 9), "capsule cylinder intersection");
  hit = intersect_capsule({10, 0, 0}, {0, 1, 0}, capsule, 100);
  expect(hit && *hit == 0, "ray starting inside capsule");
  expect(!intersect_capsule({}, {0, 1, 0}, capsule, 100), "parallel capsule miss");
  expect(!intersect_capsule({}, {1, 0, 0}, capsule, 8), "capsule range cutoff");
  Capsule sphere{{10, 0, 0}, {10, 0, 0}, 1};
  hit = intersect_capsule({}, {1, 0, 0}, sphere, 100);
  expect(hit && near(*hit, 9), "zero-length capsule becomes sphere");

  Capsule distant{{10000, 2, -2}, {10000, 2, 2}, 1};
  expect(!intersect_capsule({}, {1, 0, 0}, distant, 20000),
         "long-range capsule miss is not rounded into a false hit");
  Capsule distant_sphere{{10000, 2, 0}, {10000, 2, 0}, 1};
  expect(!intersect_capsule({}, {1, 0, 0}, distant_sphere, 20000),
         "long-range sphere discriminant retains precision");
  input = base();
  input.hitboxes.push_back(box({4, -2, -2}, {6, 2, 2}, 0));
  expect(!solve(input).valid,
         "intervening player geometry blocks the target without adding damage");

  input = base();
  input.kernel = {{{0, 0}, 3}, {{.5F, 0}, 1}};
  metrics = evaluate_aim(input, {0, 0});
  expect(near(metrics.expected_damage, 75) && near(metrics.hit_probability, .75F),
         "normalized weights produce distinct expected damage and hit probability");
  input.policy.minimum_damage = 150;
  expect(evaluate_aim(input, {0, 0}).damage_probability == 0,
         "minimum-damage probability is distinct from hit probability");
  input.policy.minimum_damage = 1;
  input.policy.minimum_probability = .8F;
  expect(!solve(input).valid, "infeasible probability constraint rejects aim");

  input = base();
  input.hitboxes[0] = box({9, -2, -2}, {11, 2, 2});
  input.hitboxes.push_back(box({9.9F, 4.95F, -.05F}, {10.1F, 5.05F, .05F}, 4));
  input.kernel = {{{0, 0}, 1}, {{.05F, 0}, 1}, {{-.05F, 0}, 1}, {{.1F, 0}, 1}, {{-.1F, 0}, 1}};
  auto result = solve(input);
  expect(result.valid && result.metrics.expected_damage > 95 && result.aim.x < .25F,
         "broad body beats narrow higher raw damage after convolution");
  input.objective = AimObjective::hitchance;
  result = solve(input);
  expect(result.valid && result.metrics.damage_probability > .99F,
         "hitchance objective evaluates all regions");

  input = base();
  input.hitboxes[0] = box({9.99F, 9.99F, -.01F}, {10.01F, 10.01F, .01F});
  input.kernel = {{{.1F, 0}, 1}};
  input.policy.minimum_probability = 1;
  input.policy.require_known_spread = true;
  input.policy.require_known_world = true;
  metrics = evaluate_aim(input, {9.0F / 11.0F, 0});
  expect(near(metrics.expected_damage, 100) && metrics.nominal_damage == 0,
         "kernel rotates with off-axis aim instead of translating fixed chart offsets");
  result = solve(input);
  expect(result.valid && near(result.aim.x, 9.0F / 11.0F, .003F),
         "known spread compensation can select aim outside the silhouette");

  const auto silhouette = project_sphere({}, {10, 10, 0}, 1, input.basis, 256);
  float maximum = -100;
  for (auto point : silhouette.points) maximum = std::max(maximum, point.x);
  expect(near(maximum, (100 + std::sqrt(199.0F)) / 99.0F, .0002F),
         "off-axis sphere projection follows the tangent conic");

  input = base();
  input.hitboxes[0] = box({9, -2, -2}, {11, 2, 2});
  input.world = {{aabb_obb({4.99F, -5, -5}, {5.01F, .64F, 5}), 0},
                 {aabb_obb({4.99F, .66F, -5}, {5.01F, 5, 5}), 0}};
  result = solve(input);
  expect(result.valid && result.aim.x > .128F && result.aim.x < .132F,
         "world-silhouette partition finds a narrow visible opening");

  input = base();
  input.world_complete = false;
  input.policy.require_known_world = true;
  expect(!solve(input).valid, "unknown world is rejected when coverage is required");
  input.world_complete = true;
  input.spread_known = false;
  input.policy.require_known_spread = true;
  expect(!solve(input).valid, "unmeasured spread is not verified zero spread");
  input.kernel = {{{0, 0}, 0}};
  expect(!solve(input).valid, "zero-weight kernel rejected");
  const auto kernel = spread_kernel(1, 0, 256);
  float square = 0;
  for (const auto& s : kernel) square += cs2::game::dot(s.offset, s.offset);
  expect(near(square / static_cast<float>(kernel.size()), 1.0F / 3.0F, .001F),
         "uniform radial spread component is not a uniform disk");
  expect(spread_kernel(-1, 0).empty(), "negative spread rejected");
  input = base();
  input.kernel = spread_kernel(.03F, .01F, 64);
  result = solve(input);
  metrics = evaluate_aim(input, result.aim);
  expect(result.valid && result.metrics.rays == 64 &&
             near(result.metrics.expected_damage, metrics.expected_damage) &&
             near(result.metrics.damage_probability, metrics.damage_probability),
         "coarse proposal search still accepts and reports all64 samples");
  input = base();
  Hitbox bound_capsule;
  bound_capsule.is_capsule = true;
  bound_capsule.capsule = {{10, 3, -1}, {10, 3, 1}, .5F};
  input.hitboxes.push_back(bound_capsule);
  for (int i = 0; i < 200; ++i) {
    const Vec2 point{static_cast<float>((i * 37) % 101 - 50) / 100.0F,
                     static_cast<float>((i * 61) % 103 - 51) / 100.0F};
    const auto direct = evaluate_ray(input.eye, input.basis.unproject(point), input.hitboxes,
                                     input.weapon, input.defense, {}, true);
    const auto accelerated = evaluate_aim(input, point);
    expect(near(direct.damage, accelerated.nominal_damage),
           "conservative angular bounds agree with uncullled3D intersections");
  }
  input.pose_sigma = .1F;
  expect(evaluate_aim(input, {0, 0}).rays == 16, "pose kernel remains separate from a delta shot");
  input.hitboxes[0].box.center.x = std::numeric_limits<float>::quiet_NaN();
  expect(!solve(input).valid, "corrupt target geometry rejected");
  std::printf("spherical heatmap: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
