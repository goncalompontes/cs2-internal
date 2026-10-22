#include "cs2/aim/montecarlo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

#include "cs2/aim/ray_span.hpp"

namespace cs2::aim {
namespace {

// The grid the stand-in `angle_grid` hash quantises the probe angles onto (radians).
constexpr float ANGLE_HASH_QUANTUM = 1.0e-4F;

// The tie threshold of the selection rule: two probabilities closer than this are a
// tie (the probabilities are `hits / samples`, so they are exact multiples of
// `1 / samples` and never land this close by accident).
constexpr float TIE_EPSILON = 1.0e-6F;

// The number of candidate targets whose damage the estimator can attribute at once.
// The accumulator is a fixed-size local scratch (the payload is manually mapped —
// no heap on the hot path), so the health list is bounded: more targets than this
// is an error, never a silently wrong attribution.
constexpr int MAX_TRACKED_TARGETS = 32;

// The shot's **spread** radius and angle, carried across the pellets: the engine
// draws them once (for pellet 0) and reuses them for the later pellets.
struct SpreadPair {
  float radius = 0.0F;
  float angle = 0.0F;
};

// The engine's per-pellet draw, in the engine's order (`docs/re/aim.md` §"The spread
// cone"):
//
//   per shot:   r1, a1 = RandomFloat(0, 1), RandomFloat(0, 2pi)   (the spread)
//   per pellet: r2, a2 = RandomFloat(0, 1), RandomFloat(0, 2pi)   (the inaccuracy)
//
// with the revolver's `r -> 1 - r^2` and the negev's `r -> 1 - r^(2^k)` transforms,
// which the engine applies to **both** radii. The draw order is load-bearing: the
// offsets must be the engine's, so the expression order matches `pellet_offsets`
// exactly — and the host test asserts the two are **bit-identical** (exact
// equality, not a tolerance), so the claim is enforced rather than asserted.
//
// This is a **second implementation of the recipe, not a second RNG** — the stream
// itself is the repo's `UniformRandomStream` and the seed is the repo's
// `md5_pseudo_random`. It is, however, the **same expression**: the cross-check
// against `pellet_offsets` is therefore a **drift detector** (if one of the two is
// edited, the test fails) and **not** an independent derivation of the engine's
// recipe. The recipe itself rests on the repo's RE (`docs/re/aim.md`), which
// `spread.hpp` still marks as unvalidated against the live engine.
//
// The **spread** radius and angle are drawn **once**, for pellet 0, and again for
// every pellet only when the shotgun-pattern convar is on; the **inaccuracy**
// radius and angle are drawn **per pellet**. `carried` holds the shot's spread
// pair, so the later pellets reuse the first one.
[[nodiscard]] auto draw_pellet(UniformRandomStream& stream, float spread, float inaccuracy,
                               SpreadPattern pattern, const SpreadRules& rules, int pellet,
                               SpreadPair& carried,
                               const std::optional<std::array<ShotgunPatternEntry,
                                                             PATTERN_TABLE_ENTRIES>>& table)
    -> Vec2 {
  float r1 = carried.radius;
  float a1 = carried.angle;
  if (pellet == 0 || rules.shotgun_patterns) {
    r1 = stream.random_float(0.0F, 1.0F);
    a1 = stream.random_float(0.0F, TWO_PI);
    if (radius_transform(pattern, rules) == RadiusTransform::revolver) {
      r1 = 1.0F - r1 * r1;
    } else if (radius_transform(pattern, rules) == RadiusTransform::negev) {
      const int exponent = negev_exponent(rules.recoil_index);
      for (int step = 0; step < exponent; ++step) {
        r1 = r1 * r1;
      }
      r1 = 1.0F - r1;
    }
    carried.radius = r1;
    carried.angle = a1;
  }
  float r2 = 0.0F;
  float a2 = 0.0F;
  if (rules.shotgun_patterns) {
    // The pattern table: the entry the shot's index names, or the random cone
    // when the index is out of the table's range.
    const int index = rules.recoil_index * rules.num_bullets + pellet;
    if (table.has_value() && index >= 0 && index < PATTERN_TABLE_ENTRIES) {
      const ShotgunPatternEntry& entry = (*table)[static_cast<std::size_t>(index)];
      a2 = entry.angle;
      r2 = entry.radius;
    } else {
      // The engine's fallback draws the angle first, then the radius.
      a2 = stream.random_float(0.0F, TWO_PI);
      r2 = stream.random_float(0.0F, 1.0F);
    }
  } else {
    r2 = stream.random_float(0.0F, 1.0F);
    a2 = stream.random_float(0.0F, TWO_PI);
  }
  if (radius_transform(pattern, rules) == RadiusTransform::revolver) {
    r2 = 1.0F - r2 * r2;
  } else if (radius_transform(pattern, rules) == RadiusTransform::negev) {
    const int exponent = negev_exponent(rules.recoil_index);
    for (int step = 0; step < exponent; ++step) {
      r2 = r2 * r2;
    }
    r2 = 1.0F - r2;
  }
  return Vec2{.x = spread * r1 * std::cos(a1) + inaccuracy * r2 * std::cos(a2),
              .y = spread * r1 * std::sin(a1) + inaccuracy * r2 * std::sin(a2)};
}

// Whether the candidate hit beats the incumbent one.
//
// The **nearest** hit wins; at an exact tie the **hitgroup preference** decides
// (the selection rule's third key), then the damage multiplier, then the target
// index. The tie is therefore broken by **the rule**, not by the caller's array:
// a tie used to keep the first hitbox in the list, so `[head, body]` and
// `[body, head]` reported different hitgroups and different damage (the review's
// m2).
[[nodiscard]] auto nearer_hit(float candidate_distance, const Hitbox& candidate,
                              float incumbent_distance, const Hitbox& incumbent) noexcept -> bool {
  if (candidate_distance != incumbent_distance) {
    return candidate_distance < incumbent_distance;
  }
  const int candidate_preference = hitgroup_preference(candidate.group);
  const int incumbent_preference = hitgroup_preference(incumbent.group);
  if (candidate_preference != incumbent_preference) {
    return candidate_preference < incumbent_preference;
  }
  if (candidate.multiplier != incumbent.multiplier) {
    return candidate.multiplier > incumbent.multiplier;
  }
  return candidate.target < incumbent.target;
}

// The remaining health of `target`, and whether it is known at all.
[[nodiscard]] auto remaining_health(const MonteCarloInput& input, int target) noexcept
    -> std::optional<float> {
  for (const TargetHealth& entry : input.targets) {
    if (entry.index == target) {
      return entry.health;
    }
  }
  return std::nullopt;
}

// Whether the vector's components are all finite.
[[nodiscard]] auto finite(const Vec3& value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Whether the half-extents are positive **and** finite. A NaN fails the comparison
// (every comparison with a NaN is false), an inf fails the finiteness test.
[[nodiscard]] auto positive_finite(const Vec3& value) noexcept -> bool {
  return value.x > 0.0F && value.y > 0.0F && value.z > 0.0F && std::isfinite(value.x) &&
         std::isfinite(value.y) && std::isfinite(value.z);
}

// The hit test, with the hitbox's geometry already checked (`hitbox_valid`). The
// validated path is the hot one: the scene's geometry is checked once per
// evaluation, not once per ray.
[[nodiscard]] auto ray_hitbox_checked(const Vec3& eye, const Vec3& direction,
                                      const Hitbox& hitbox) noexcept -> std::optional<float> {
  const auto span = hitbox.is_capsule ? ray_capsule_span(eye, direction, hitbox.capsule.a,
                                                         hitbox.capsule.b, hitbox.capsule.radius)
                                      : ray_obb_span(eye, direction, hitbox.box);
  if (!span.has_value()) {
    return std::nullopt;
  }
  return span->entry;
}

// Whether every hitbox of the scene is a well-formed solid.
[[nodiscard]] auto geometry_valid(const MonteCarloInput& input) noexcept -> bool {
  for (const Hitbox& hitbox : input.hitboxes) {
    if (!hitbox_valid(hitbox)) {
      return false;
    }
  }
  return true;
}

// The sample evaluation, with the weapon's pattern and the scene's geometry already
// checked. The hit test is the **checked** one: the geometry does not change
// between the samples of an evaluation, so it is validated once per evaluation and
// not once per ray.
[[nodiscard]] auto evaluate_sample_checked(const MonteCarloInput& input, const Vec2& aim,
                                           std::int32_t seed) noexcept
    -> std::expected<Sample, MonteCarloError> {
  Sample sample{};
  std::array<Vec2, MAX_PELLETS> offsets{};
  const auto drawn = draw_pellet_offsets(seed, input.kernel, std::span<Vec2>{offsets});
  if (!drawn.has_value()) {
    return std::unexpected(drawn.error());
  }
  const int pellets = *drawn;
  if (pellets <= 0) {
    return sample;
  }
  sample.offset = offsets[0];
  // The chart's local metric at the aim's pitch: a true angular offset of `r` radians
  // spans `r` in the chart's `v` (pitch) but `r / cos(v)` in its `u` (yaw)
  // (`yaw_scale`, `cs2/aim/geometry.hpp`). The tangent displacement is therefore
  // `(offset.x · yaw_scale, offset.y)` in chart coordinates.
  //
  // **The existing solver places the same pellet elsewhere.** `DamageField::
  // value_at_pellets` (`payload/aim/src/field.cpp`) adds the **raw** offset as a
  // chart displacement, without the yaw scale: at a 45° pitch the two put the
  // pellet 1/cos(v) = 1.41× apart in yaw, at 60° 2× apart. The scaled form is the
  // correct one (every spherical angular quantity has to be scaled — the research's
  // angle-dependent failure), so the two solvers genuinely disagree at a steep
  // aim. The divergence is stated here and in `docs/ragebot.md` §13.2; the
  // existing solver is not this task's to change.
  const float scale = yaw_scale(aim.y);
  bool have_nearest = false;
  for (int pellet = 0; pellet < pellets; ++pellet) {
    const Vec2 offset = offsets[static_cast<std::size_t>(pellet)];
    const Vec2 point{.x = aim.x + offset.x * scale, .y = aim.y + offset.y};
    const Vec3 direction = input.basis.unproject(point);
    // The **nearest** hit wins: the hitboxes overlap, so the sum of their hits would
    // double-count and the best-value hit would ignore the geometry.
    const Hitbox* hit = nullptr;
    float nearest = 0.0F;
    for (const Hitbox& hitbox : input.hitboxes) {
      const auto distance = ray_hitbox_checked(input.eye, direction, hitbox);
      if (!distance.has_value()) {
        continue;
      }
      if (hit == nullptr || nearer_hit(*distance, hitbox, nearest, *hit)) {
        nearest = *distance;
        hit = &hitbox;
      }
    }
    if (hit == nullptr) {
      continue;
    }
    sample.hit = true;
    // The engine's damage model: the weapon's damage, the hitgroup's multiplier and
    // the range falloff over the distance the bullet travelled (`range_falloff`,
    // `cs2/aim/field.hpp`).
    const float pellet_damage =
        input.weapon.damage * hit->multiplier * range_falloff(input.weapon.range_modifier, nearest);
    sample.damage += pellet_damage;
    // The **per-pellet attribution**: the damage belongs to **this** pellet's
    // target, not to the target of the nearest hit. A sample that hit two targets
    // used to give the second one's damage to the first (the review's M2 bug, one
    // level down), so the can-kill key compared one target's health with the sum
    // of two targets' damage.
    if (sample.pellets_hit < MAX_PELLETS) {
      const auto slot = static_cast<std::size_t>(sample.pellets_hit);
      sample.pellet_damage[slot] = pellet_damage;
      sample.pellet_target[slot] = hit->target;
      ++sample.pellets_hit;
    }
    if (!have_nearest || nearest < sample.distance) {
      have_nearest = true;
      sample.distance = nearest;
      sample.target = hit->target;
      sample.hitgroup = hit->group;
    }
  }
  return sample;
}

// Whether the candidate beats the incumbent under the lexicographic rule.
[[nodiscard]] auto better(const HitChance& candidate, bool candidate_kills, const HitChance& best,
                          bool best_kills) noexcept -> bool {
  // 1. can kill first.
  if (candidate_kills != best_kills) {
    return candidate_kills;
  }
  // 2. then the highest probability of any hit.
  if (std::fabs(candidate.probability - best.probability) > TIE_EPSILON) {
    return candidate.probability > best.probability;
  }
  // 3. then the hitgroup preference (head before body).
  const int candidate_preference = hitgroup_preference(candidate.hitgroup);
  const int best_preference = hitgroup_preference(best.hitgroup);
  if (candidate_preference != best_preference) {
    return candidate_preference < best_preference;
  }
  // 4. and the expected damage as the final tie-break.
  return candidate.expected_damage > best.expected_damage;
}

}  // namespace

auto to_string(MonteCarloError error) noexcept -> const char* {
  switch (error) {
    case MonteCarloError::no_hitboxes:
      return "no_hitboxes";
    case MonteCarloError::invalid_samples:
      return "invalid_samples";
    case MonteCarloError::too_many_pellets:
      return "too_many_pellets";
    case MonteCarloError::unsupported_pattern:
      return "unsupported_pattern";
    case MonteCarloError::no_candidates:
      return "no_candidates";
    case MonteCarloError::too_many_targets:
      return "too_many_targets";
    case MonteCarloError::invalid_geometry:
      return "invalid_geometry";
    case MonteCarloError::buffer_too_small:
      return "buffer_too_small";
  }
  return "unknown";
}

auto pattern_supported(const WeaponProfile& weapon) noexcept -> bool {
  // The shotgun's **convar** is not readable by the payload, so the payload
  // cannot know whether the engine draws the fixed pattern table or the random
  // cone; the machinegun cannot be told apart from the negev (the VData carries
  // no flag for it). Both are refused rather than silently given the standard
  // pattern (`cs2/aim/montecarlo.hpp`).
  return weapon.weapon_type != WEAPON_TYPE_SHOTGUN && weapon.weapon_type != WEAPON_TYPE_MACHINEGUN;
}

auto spread_kernel(const WeaponProfile& weapon, int pellets, SpreadPattern pattern) noexcept
    -> std::expected<SpreadKernel, MonteCarloError> {
  if (!pattern_supported(weapon)) {
    return std::unexpected(MonteCarloError::unsupported_pattern);
  }
  if (pellets <= 0) {
    return std::unexpected(MonteCarloError::invalid_samples);
  }
  if (pellets > MAX_PELLETS) {
    return std::unexpected(MonteCarloError::too_many_pellets);
  }
  return SpreadKernel{.spread = weapon.spread,
                      .inaccuracy = weapon.inaccuracy,
                      .pellets = pellets,
                      .pattern = pattern,
                      .rules = SpreadRules{.is_revolver = weapon.is_revolver,
                                           .attack_type = weapon.attack_type,
                                           .recoil_index = weapon.recoil_index,
                                           .shotgun_patterns = weapon.shotgun_patterns,
                                           .num_bullets = weapon.num_bullets,
                                           .spread_seed = weapon.spread_seed}};
}

auto draw_pellet_offsets(std::int32_t seed, const SpreadKernel& kernel,
                         std::span<Vec2> out) noexcept -> std::expected<int, MonteCarloError> {
  if (kernel.pellets <= 0) {
    return 0;
  }
  // A buffer that cannot hold the shot's pellets is an **error**, not a silent
  // truncation: the caller would score a shot with fewer pellets than the weapon
  // fires (the review's m5).
  if (out.size() < static_cast<std::size_t>(kernel.pellets)) {
    return std::unexpected(MonteCarloError::buffer_too_small);
  }
  UniformRandomStream stream(seed);
  // The shotgun's pattern table: the engine builds it once per weapon; it is
  // only read when the convar is on.
  std::optional<std::array<ShotgunPatternEntry, PATTERN_TABLE_ENTRIES>> table;
  if (kernel.rules.shotgun_patterns) {
    table = shotgun_pattern_table(kernel.rules.spread_seed, kernel.rules.num_bullets);
  }
  SpreadPair carried;
  for (int i = 0; i < kernel.pellets; ++i) {
    out[static_cast<std::size_t>(i)] =
        draw_pellet(stream, kernel.spread, kernel.inaccuracy, kernel.pattern, kernel.rules, i,
                    carried, table);
  }
  return kernel.pellets;
}

auto angle_seed_hash(float yaw, float pitch, std::uint32_t tick) noexcept -> std::uint32_t {
  const auto quantise = [](float value) noexcept -> std::uint32_t {
    const auto scaled =
        static_cast<std::int64_t>(std::llround(static_cast<double>(value) / ANGLE_HASH_QUANTUM));
    return static_cast<std::uint32_t>(scaled);
  };
  std::uint32_t hash = quantise(yaw) * 0x27d4eb2dU;
  hash ^= quantise(pitch) * 0x165667b1U;
  hash ^= tick * 0x9e3779b9U;
  // splitmix32's finalizer: a bijection **on the quantised values** — two
  // different grid points never share a hash. The quantisation itself is
  // deliberate: the stand-in models "the same aim gives the same seed", so two
  // probe angles within one quantum (1e-4 rad) **do** share a seed (the hash is
  // not injective over the raw angles — the review's m8). The probe step (0.0087
  // rad by default) is 87 quanta, so two neighbouring probes never collide.
  hash ^= hash >> 16;
  hash *= 0x85ebca6bU;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35U;
  hash ^= hash >> 16;
  return hash;
}

auto sample_seed(const SeedContext& context, const Vec2& aim, int index, int sample_count) noexcept
    -> std::int32_t {
  if (context.source == SeedSource::sequence) {
    return md5_pseudo_random(context.sequence + static_cast<std::uint32_t>(index));
  }
  // The probe angles: a yaw sweep across the candidate's neighbourhood, centred on the
  // candidate (velocity's `find_spread_correction` probes the yaw in half-degree
  // steps). The pitch is the candidate's own.
  //
  // **The candidate's own angle is one of the probes.** The centre index is
  // **integral** (`sample_count / 2`), so the probe at that index is the
  // candidate's own aim and the candidate's own seed is in the sample set. (With
  // a half-integer centre it never was: for N = 64/128/256 the centre was
  // 31.5/63.5/127.5 and the candidate's own seed was never drawn — the review's
  // m1.) The sweep reaches `floor(N / 2)` steps either side, so it is asymmetric
  // by one step for an even count; at the default `probe_step` of 0.0087 rad its
  // half-width is **15.95°** (N = 64), **31.9°** (N = 128) and **63.8°** (N = 256).
  const int count = std::max(sample_count, 1);
  const float centre = static_cast<float>(count / 2);
  const float probe_yaw = aim.x + (static_cast<float>(index) - centre) * context.probe_step;
  const float probe_pitch = aim.y;
  return md5_pseudo_random(angle_seed_hash(probe_yaw, probe_pitch, context.tick));
}

auto hitbox_valid(const Hitbox& hitbox) noexcept -> bool {
  // The **damage multiplier** is checked with the geometry: a NaN one makes every
  // damage product a NaN, and every comparison against a NaN is false — so the
  // selection rule kept the first candidate regardless of its quality and returned
  // a NaN verdict with no error (the review's G1). The multiplier is a number the
  // solver multiplies with, so it has to be a number.
  if (!std::isfinite(hitbox.multiplier)) {
    return false;
  }
  if (hitbox.is_capsule) {
    // A capsule is a segment thickened by its radius: both the endpoints and the
    // radius must be finite, and the radius must be positive (the span code uses
    // `radius * radius`, so a negative radius would behave as a positive one).
    return finite(hitbox.capsule.a) && finite(hitbox.capsule.b) &&
           hitbox.capsule.radius > 0.0F && std::isfinite(hitbox.capsule.radius);
  }
  // An oriented box: a finite centre, finite unit axes and positive half-extents
  // (a zero half-extent is a degenerate box, and a NaN one is not a solid at all).
  return finite(hitbox.box.center) && positive_finite(hitbox.box.half) &&
         finite(hitbox.box.axis_x) && finite(hitbox.box.axis_y) && finite(hitbox.box.axis_z);
}

auto ray_hitbox(const Vec3& eye, const Vec3& direction, const Hitbox& hitbox) noexcept
    -> std::optional<float> {
  // A degenerate hitbox is **rejected**, not turned into a hit: the slab clamp
  // compares against a NaN (and every comparison with a NaN is false), so a NaN
  // half-extent used to degenerate the box into an unbounded solid — a hit at
  // distance 0, and a silent 100 % hit chance on a target that cannot be hit.
  if (!hitbox_valid(hitbox)) {
    return std::nullopt;
  }
  return ray_hitbox_checked(eye, direction, hitbox);
}

auto evaluate_sample(const MonteCarloInput& input, const Vec2& aim, std::int32_t seed) noexcept
    -> std::expected<Sample, MonteCarloError> {
  // The refusal comes **first**: an unmodelled weapon's pattern must never be
  // silently replaced by the standard one (the spec §2.2). A `Sample` for a
  // shotgun would be a number computed from the wrong pattern.
  if (!pattern_supported(input.weapon)) {
    return std::unexpected(MonteCarloError::unsupported_pattern);
  }
  // The geometry is checked **once**, before the samples are drawn: a hitbox that
  // is not a well-formed solid is refused, not silently skipped and not turned
  // into a hit.
  if (!geometry_valid(input)) {
    return std::unexpected(MonteCarloError::invalid_geometry);
  }
  return evaluate_sample_checked(input, aim, seed);
}

auto evaluate_hit_chance(const MonteCarloInput& input, const Vec2& aim) noexcept
    -> std::expected<HitChance, MonteCarloError> {
  // The refusal comes **first**: this is the entry point the next task will call
  // (it is the cheap one), so it must not be the one that silently scores a
  // shotgun with the standard pattern (the spec §2.2).
  if (!pattern_supported(input.weapon)) {
    return std::unexpected(MonteCarloError::unsupported_pattern);
  }
  if (input.hitboxes.empty()) {
    return std::unexpected(MonteCarloError::no_hitboxes);
  }
  if (input.samples <= 0 || input.kernel.pellets <= 0) {
    return std::unexpected(MonteCarloError::invalid_samples);
  }
  if (input.kernel.pellets > MAX_PELLETS) {
    return std::unexpected(MonteCarloError::too_many_pellets);
  }
  if (input.targets.size() > static_cast<std::size_t>(MAX_TRACKED_TARGETS)) {
    return std::unexpected(MonteCarloError::too_many_targets);
  }
  // The geometry is checked **once**, before the samples are drawn: a hitbox that
  // is not a well-formed solid is refused, not silently skipped and not turned
  // into a hit.
  if (!geometry_valid(input)) {
    return std::unexpected(MonteCarloError::invalid_geometry);
  }

  HitChance chance{};
  chance.samples = input.samples;
  chance.source = input.seed.source;
  // The per-target damage accumulator: a sample's damage belongs to the target it
  // hit, so the can-kill key can be judged against **that** target's health. The
  // accumulator is indexed by the position of the target in `input.targets` (the
  // health list) and is a fixed-size local scratch — the payload must not allocate
  // on the hot path.
  std::array<float, MAX_TRACKED_TARGETS> damage_by_target{};
  float damage_total = 0.0F;
  float nearest = 0.0F;
  bool have_nearest = false;
  for (int index = 0; index < input.samples; ++index) {
    const std::int32_t seed = sample_seed(input.seed, aim, index, input.samples);
    const auto sample = evaluate_sample_checked(input, aim, seed);
    if (!sample.has_value()) {
      return std::unexpected(sample.error());
    }
    if (!sample->hit) {
      continue;
    }
    ++chance.hits;
    damage_total += sample->damage;
    // The **per-pellet** attribution: every pellet that hit credited the target it
    // hit, with the damage it did there. The sample's whole damage used to be
    // credited to the target of the nearest hit, so a multi-pellet shot that hit two
    // targets gave the second one's damage to the first (the review's M2 bug, one
    // level down).
    for (int pellet = 0; pellet < sample->pellets_hit; ++pellet) {
      const auto pellet_slot = static_cast<std::size_t>(pellet);
      for (std::size_t slot = 0; slot < input.targets.size(); ++slot) {
        if (input.targets[slot].index == sample->pellet_target[pellet_slot]) {
          damage_by_target[slot] += sample->pellet_damage[pellet_slot];
          break;
        }
      }
    }
    if (!have_nearest || sample->distance < nearest ||
        (sample->distance == nearest &&
         hitgroup_preference(sample->hitgroup) < hitgroup_preference(chance.hitgroup))) {
      have_nearest = true;
      nearest = sample->distance;
      chance.target = sample->target;
      chance.hitgroup = sample->hitgroup;
    }
  }
  chance.probability = static_cast<float>(chance.hits) / static_cast<float>(input.samples);
  chance.expected_damage = damage_total / static_cast<float>(input.samples);
  // The damage **attributed to the target of the nearest hit**: the can-kill key is
  // judged against this number and that target's own health, never against the
  // scene's aggregate.
  for (std::size_t slot = 0; slot < input.targets.size(); ++slot) {
    if (input.targets[slot].index == chance.target) {
      chance.target_damage = damage_by_target[slot] / static_cast<float>(input.samples);
      break;
    }
  }
  return chance;
}

auto solve_montecarlo(const MonteCarloInput& input, std::span<const Cell> cells,
                      const CandidateBudget& budget) noexcept
    -> std::expected<MonteCarloChoice, MonteCarloError> {
  if (!pattern_supported(input.weapon)) {
    return std::unexpected(MonteCarloError::unsupported_pattern);
  }
  if (input.hitboxes.empty()) {
    return std::unexpected(MonteCarloError::no_hitboxes);
  }
  if (input.samples <= 0 || input.kernel.pellets <= 0) {
    return std::unexpected(MonteCarloError::invalid_samples);
  }
  if (input.kernel.pellets > MAX_PELLETS) {
    return std::unexpected(MonteCarloError::too_many_pellets);
  }

  // The candidate set: the field's best cells, by descending value. Each contributes
  // its Chebyshev centre and its centroid — the same two candidates the existing
  // solver's `field_candidates` builds (`payload/aim/src/field.cpp`).
  //
  // **This is a second candidate generator, and it is coarser than the existing
  // one.** `field_candidates` is file-static in a file this task does not own, so
  // the two primitives it uses (`chebyshev_center`, `polygon_centroid`) are reused
  // here instead of the function. The divergence is explicit: the existing
  // generator emits **four** candidates per cell — the Chebyshev centre, the
  // centroid, the polygon's **vertices** and the point **nearest the field
  // centroid** — plus the **field centroid** itself, and it **filters by
  // `value_floor`** (a cell below the floor cannot improve the objective, so its
  // candidates are skipped). This generator emits the centre and the centroid
  // only, and filters by the cell's value rank instead of a floor. The two
  // candidate sets therefore differ: the Monte-Carlo search evaluates fewer
  // candidates, and its "best cell" is not the existing solver's.
  //
  // Opening `payload/aim/src/field.cpp` to expose `field_candidates` would remove
  // the divergence; that file is **not** this task's (the spec §4), so the
  // divergence is reported instead of fixed.
  std::vector<std::size_t> order(cells.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  const std::size_t cell_count =
      std::min<std::size_t>(cells.size(), static_cast<std::size_t>(std::max(budget.max_cells, 0)));
  std::partial_sort(
      order.begin(), order.begin() + static_cast<std::ptrdiff_t>(cell_count), order.end(),
      [&](std::size_t a, std::size_t b) -> bool { return cells[a].value > cells[b].value; });
  std::vector<Vec2> candidates;
  candidates.reserve(cell_count * 2);
  for (std::size_t i = 0; i < cell_count; ++i) {
    const Cell& cell = cells[order[i]];
    candidates.push_back(chebyshev_center(cell.polygon));
    candidates.push_back(polygon_centroid(cell.polygon));
  }
  if (static_cast<int>(candidates.size()) > budget.max_candidates) {
    candidates.resize(static_cast<std::size_t>(std::max(budget.max_candidates, 0)));
  }
  if (candidates.empty()) {
    return std::unexpected(MonteCarloError::no_candidates);
  }

  MonteCarloChoice best;
  for (const Vec2& candidate : candidates) {
    const auto chance = evaluate_hit_chance(input, candidate);
    if (!chance.has_value()) {
      return std::unexpected(chance.error());
    }
    // Can kill: the damage **attributed to the candidate's target** reaches that
    // target's remaining health. The scene's mean damage would say nothing about
    // whether the named target dies — in a multi-target scene it sums the damage of
    // every target a sample hit, so an aggregate could declare a kill that cannot
    // happen (the review's M2). An unknown target's health is not a proof, so it
    // does not kill.
    bool kills = false;
    if (const auto health = remaining_health(input, chance->target); health.has_value()) {
      kills = chance->target_damage >= *health;
    }
    if (!best.valid || better(*chance, kills, best.chance, best.can_kill)) {
      best.valid = true;
      best.aim = candidate;
      best.chance = *chance;
      best.target = chance->target;
      best.hitgroup = chance->hitgroup;
      best.can_kill = kills;
    }
  }
  return best;
}

auto hitgroup_preference(int group) noexcept -> int {
  switch (group) {
    case HITGROUP_HEAD:
      return 0;
    case HITGROUP_CHEST:
      return 1;
    case HITGROUP_STOMACH:
      return 2;
    case HITGROUP_GENERIC:
      return 3;
    case HITGROUP_LEFT_ARM:
    case HITGROUP_RIGHT_ARM:
      return 4;
    case HITGROUP_LEFT_LEG:
    case HITGROUP_RIGHT_LEG:
      return 5;
    default:
      return 6;
  }
}

}  // namespace cs2::aim
