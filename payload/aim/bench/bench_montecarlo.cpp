// The Monte-Carlo hit-chance solver's **benchmark**
// (docs/orchestration/spec-t34-montecarlo.md §2.7).
//
// The published numbers used to live in an out-of-tree harness
// (`%TEMP%\opencode\bench\bench_live.cpp`) — no Meson target, not versioned, not
// reproducible by anyone else. This is that harness, committed.
//
// **What it replays.** The committed saved cell set `artifacts/live-heatmap.json`
// (717 cells, the live heatmap of a four-player scene). The path is relative to the
// **repository root**, so the benchmark is run from there:
//
//   meson compile -C <build>
//   <build>/aim_bench.exe
//
// **What it measures.** The solver's cost, per candidate and per search, for the
// community's sample counts (64 / 128 / 256), next to the existing objective's cost
// on the same field. A 64 Hz tick is **15.6 ms** and the service's own guard is
// `k_backtrack_budget_us = 12000`, so the numbers decide whether the solver fits.
//
// **The hitboxes are a reconstruction.** The saved set stores the field's *chart*
// cells (the projected silhouettes), not the world-space solids they came from, so
// the benchmark rebuilds one world-space capsule per (target, hitgroup) group: the
// group's chart extent, unprojected at the target's recorded distance. The cost is
// what is measured, and the reconstruction preserves the shape of the problem — 24
// hitboxes over three targets, the same order as a live multi-target solve.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/geometry.hpp"
#include "cs2/aim/montecarlo.hpp"
#include "cs2/control/json.hpp"

namespace {

constexpr const char* SAVED_SET_PATH = "artifacts/live-heatmap.json";

// The sample counts the community uses (velocity: 64 / 128 / 256).
constexpr int k_sample_counts[] = {64, 128, 256};

// The cone radius the existing objective is measured at (`docs/known-issues.md`
// §11.2: `argmax_hit_probability` is 34 ms at sigma = 0.002). The Monte-Carlo kernel
// uses the same cone radius, so the two are comparable.
constexpr float k_cone_radius = 0.002F;

// How many times the whole-search costs are repeated (the per-candidate costs are
// measured once per saved cell).
constexpr int k_search_repeats = 50;

// The cell group's chart extent, accumulated while the saved set is read.
struct Group {
  int target = -1;
  int hitgroup = 0;
  float min_u = 0.0F;
  float max_u = 0.0F;
  float min_v = 0.0F;
  float max_v = 0.0F;
  bool seen = false;
};

struct Stats {
  double min = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double max = 0.0;
  double mean = 0.0;
};

auto summarize(std::vector<double> values) -> Stats {
  Stats stats;
  if (values.empty()) {
    return stats;
  }
  std::sort(values.begin(), values.end());
  const auto count = static_cast<std::size_t>(values.size());
  const auto at = [&](double percentile) -> double {
    auto index = static_cast<std::size_t>(percentile * static_cast<double>(count - 1) / 100.0);
    return values[std::min(index, count - 1)];
  };
  stats.min = values.front();
  stats.max = values.back();
  stats.p50 = at(50.0);
  stats.p95 = at(95.0);
  stats.p99 = at(99.0);
  double total = 0.0;
  for (const double value : values) {
    total += value;
  }
  stats.mean = total / static_cast<double>(count);
  return stats;
}

auto milliseconds(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) -> double {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

auto read_file(const char* path) -> std::string {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

auto number(const cs2::control::Json& node, const char* key, double fallback) -> double {
  const auto* member = node.find(key);
  return member != nullptr && member->is_number() ? member->as_number() : fallback;
}

auto integer(const cs2::control::Json& node, const char* key, std::int64_t fallback)
    -> std::int64_t {
  const auto* member = node.find(key);
  return member != nullptr && member->is_number() ? member->as_int() : fallback;
}

auto vector3(const cs2::control::Json& node) -> cs2::game::Vec3 {
  return cs2::game::Vec3{.x = static_cast<float>(number(node, "x", 0.0)),
                         .y = static_cast<float>(number(node, "y", 0.0)),
                         .z = static_cast<float>(number(node, "z", 0.0))};
}

// A chart point. The saved set writes a polygon's vertices as `[x, y]` pairs; the
// object form `{ "x": …, "y": … }` is accepted too.
auto point2(const cs2::control::Json& node) -> cs2::aim::Vec2 {
  if (node.is_array() && node.items().size() >= 2) {
    const cs2::control::Json& x = node.items()[0];
    const cs2::control::Json& y = node.items()[1];
    return cs2::aim::Vec2{.x = static_cast<float>(x.is_number() ? x.as_number() : 0.0),
                          .y = static_cast<float>(y.is_number() ? y.as_number() : 0.0)};
  }
  return cs2::aim::Vec2{.x = static_cast<float>(number(node, "x", 0.0)),
                        .y = static_cast<float>(number(node, "y", 0.0))};
}

}  // namespace

auto main() -> int {
  using cs2::aim::Basis;
  using cs2::aim::Cell;
  using cs2::aim::Hitbox;
  using cs2::aim::MonteCarloInput;
  using cs2::aim::Polygon;
  using cs2::aim::SeedContext;
  using cs2::aim::SpreadKernel;
  using cs2::aim::TargetHealth;
  using cs2::aim::Vec2;
  using cs2::aim::Vec3;

  const std::string text = read_file(SAVED_SET_PATH);
  if (text.empty()) {
    std::printf("bench: %s not found — run the benchmark from the repository root\n",
                SAVED_SET_PATH);
    return 1;
  }
  const auto parsed = cs2::control::Json::parse(text);
  if (!parsed.has_value()) {
    std::printf("bench: %s is not valid JSON\n", SAVED_SET_PATH);
    return 1;
  }
  const cs2::control::Json& root = *parsed;

  // --- the saved set ---------------------------------------------------------
  const auto* eye_node = root.find("eye");
  const Vec3 eye = eye_node != nullptr ? vector3(*eye_node) : Vec3{};
  const auto* weapon_node = root.find("weapon");
  cs2::aim::WeaponProfile weapon;
  if (weapon_node != nullptr) {
    weapon.damage = static_cast<float>(number(*weapon_node, "damage", 35.0));
    weapon.headshot_multiplier = static_cast<float>(number(*weapon_node, "headshot", 4.0));
    weapon.range_modifier = static_cast<float>(number(*weapon_node, "range_modifier", 0.91));
    weapon.penetration = static_cast<float>(number(*weapon_node, "penetration", 1.0));
  }
  weapon.weapon_type = 3;  // a rifle: the standard pellet pattern is modelled

  // The targets: their index, their remaining health and their distance.
  std::vector<TargetHealth> targets;
  std::vector<float> distances;
  const auto* targets_node = root.find("targets");
  if (targets_node != nullptr) {
    for (const cs2::control::Json& entry : targets_node->items()) {
      targets.push_back(TargetHealth{.index = static_cast<int>(integer(entry, "index", -1)),
                                     .health = static_cast<float>(number(entry, "health", 100.0))});
      distances.push_back(static_cast<float>(number(entry, "distance", 100.0)));
    }
  }

  // The field's cells, and the (target, hitgroup) groups they belong to.
  std::vector<Cell> cells;
  std::vector<Group> groups;
  const auto* cells_node = root.find("cells");
  if (cells_node == nullptr) {
    std::printf("bench: the saved set has no cells\n");
    return 1;
  }
  for (const cs2::control::Json& entry : cells_node->items()) {
    Cell cell;
    cell.value = static_cast<float>(number(entry, "value", 0.0));
    cell.base_value = static_cast<float>(number(entry, "base_value", cell.value));
    const auto* poly = entry.find("poly");
    if (poly == nullptr || !poly->is_array() || poly->items().size() < 3) {
      continue;
    }
    for (const cs2::control::Json& point : poly->items()) {
      cell.polygon.points.push_back(point2(point));
    }
    cells.push_back(std::move(cell));

    const int target = static_cast<int>(integer(entry, "target", -1));
    const int hitgroup = static_cast<int>(integer(entry, "hitgroup", 0));
    Group* group = nullptr;
    for (Group& candidate : groups) {
      if (candidate.target == target && candidate.hitgroup == hitgroup) {
        group = &candidate;
        break;
      }
    }
    if (group == nullptr) {
      groups.push_back(Group{.target = target, .hitgroup = hitgroup});
      group = &groups.back();
    }
    for (const Vec2& point : cells.back().polygon.points) {
      if (!group->seen) {
        group->min_u = point.x;
        group->max_u = point.x;
        group->min_v = point.y;
        group->max_v = point.y;
        group->seen = true;
        continue;
      }
      group->min_u = std::min(group->min_u, point.x);
      group->max_u = std::max(group->max_u, point.x);
      group->min_v = std::min(group->min_v, point.y);
      group->max_v = std::max(group->max_v, point.y);
    }
  }

  // --- the reconstructed hitboxes --------------------------------------------
  //
  // One world-space capsule per (target, hitgroup) group: the group's chart extent
  // (its yaw/pitch silhouette) is unprojected at the target's recorded distance, and
  // the chart's local metric (a yaw span of `du` is `du · cos(v)` of true angle)
  // gives the world sizes.
  const Basis basis{};
  std::vector<Hitbox> hitboxes;
  for (const Group& group : groups) {
    if (!group.seen) {
      continue;
    }
    float distance = 100.0F;
    for (std::size_t i = 0; i < targets.size(); ++i) {
      if (targets[i].index == group.target) {
        distance = distances[i];
        break;
      }
    }
    const float u_centre = 0.5F * (group.min_u + group.max_u);
    const float v_centre = 0.5F * (group.min_v + group.max_v);
    const Vec3 direction = basis.unproject(Vec2{.x = u_centre, .y = v_centre});
    const Vec3 centre = eye + direction * distance;
    const float horizontal = distance * std::cos(v_centre);
    const float half_length = 0.5F * (group.max_v - group.min_v) * horizontal;
    const float radius = 0.5F * (group.max_u - group.min_u) * horizontal;
    Hitbox hitbox;
    hitbox.is_capsule = true;
    hitbox.capsule =
        cs2::aim::Capsule{.a = Vec3{.x = centre.x, .y = centre.y, .z = centre.z - half_length},
                          .b = Vec3{.x = centre.x, .y = centre.y, .z = centre.z + half_length},
                          .radius = std::max(radius, 0.5F)};
    hitbox.multiplier =
        group.hitgroup == cs2::aim::HITGROUP_HEAD ? weapon.headshot_multiplier : 1.0F;
    hitbox.group = group.hitgroup;
    hitbox.target = group.target;
    hitboxes.push_back(hitbox);
  }

  // --- the existing objective's field ----------------------------------------
  cs2::aim::DamageField field;
  for (const Cell& cell : cells) {
    field.add(cell.polygon, cell.value, cell.base_value);
  }
  field.build_index();

  // --- the Monte-Carlo input --------------------------------------------------
  MonteCarloInput input;
  input.eye = eye;
  input.basis = basis;
  input.hitboxes = hitboxes;
  input.targets = targets;
  input.weapon = weapon;
  input.kernel = SpreadKernel{.spread = k_cone_radius * 0.70710678F,
                              .inaccuracy = k_cone_radius * 0.70710678F,
                              .pellets = 1,
                              .pattern = cs2::aim::SpreadPattern::standard};
  input.seed = SeedContext{.sequence = 30596U, .tick = 30596U};

  // The candidate aims: every saved cell's Chebyshev centre (the search's own
  // candidate set is the best cells' centres and centroids).
  std::vector<Vec2> candidates;
  candidates.reserve(cells.size());
  for (const Cell& cell : cells) {
    candidates.push_back(cs2::aim::chebyshev_center(cell.polygon));
  }

  std::printf("bench: the saved set %s\n", SAVED_SET_PATH);
  std::printf("  cells %zu, hitgroups %zu, targets %zu, hitboxes %zu\n", cells.size(),
              groups.size(), targets.size(), hitboxes.size());
  std::printf("  weapon damage %.0f, cone radius %.4f rad\n", weapon.damage, k_cone_radius);
  std::printf("  the tick budget is 15.6 ms (the service's guard is 12 ms)\n\n");

  // --- the per-candidate evaluation ------------------------------------------
  double sink = 0.0;
  for (const int samples : k_sample_counts) {
    input.samples = samples;
    std::vector<double> durations;
    durations.reserve(candidates.size());
    for (const Vec2& candidate : candidates) {
      const auto start = std::chrono::steady_clock::now();
      const auto chance = cs2::aim::evaluate_hit_chance(input, candidate);
      const auto end = std::chrono::steady_clock::now();
      durations.push_back(milliseconds(start, end));
      if (chance.has_value()) {
        sink += chance->probability;
      }
    }
    const Stats stats = summarize(std::move(durations));
    std::printf(
        "evaluate_hit_chance  N=%3d  p50 %7.3f ms  p95 %7.3f ms  p99 %7.3f ms  "
        "min %7.3f  max %7.3f  mean %7.3f\n",
        samples, stats.p50, stats.p95, stats.p99, stats.min, stats.max, stats.mean);
  }

  // --- the single-sample evaluation -------------------------------------------
  {
    std::vector<double> durations;
    durations.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const auto start = std::chrono::steady_clock::now();
      const auto sample = cs2::aim::evaluate_sample(input, candidates[i], 12345);
      const auto end = std::chrono::steady_clock::now();
      durations.push_back(milliseconds(start, end));
      sink += sample.has_value() && sample->hit ? 1.0 : 0.0;
    }
    const Stats stats = summarize(std::move(durations));
    std::printf(
        "evaluate_sample     N=  1  p50 %7.4f ms  p95 %7.4f ms  p99 %7.4f ms  "
        "min %7.4f  max %7.4f  mean %7.4f\n",
        stats.p50, stats.p95, stats.p99, stats.min, stats.max, stats.mean);
  }

  // --- the whole search --------------------------------------------------------
  for (const int samples : k_sample_counts) {
    input.samples = samples;
    std::vector<double> durations;
    durations.reserve(k_search_repeats);
    for (int repeat = 0; repeat < k_search_repeats; ++repeat) {
      const auto start = std::chrono::steady_clock::now();
      const auto choice = cs2::aim::solve_montecarlo(input, field.cells);
      const auto end = std::chrono::steady_clock::now();
      durations.push_back(milliseconds(start, end));
      if (choice.has_value()) {
        sink += choice->chance.probability;
      }
    }
    const Stats stats = summarize(std::move(durations));
    std::printf(
        "solve_montecarlo     N=%3d  p50 %7.3f ms  p95 %7.3f ms  p99 %7.3f ms  "
        "min %7.3f  max %7.3f  mean %7.3f\n",
        samples, stats.p50, stats.p95, stats.p99, stats.min, stats.max, stats.mean);
  }

  // --- the existing objective, on the same field -------------------------------
  {
    std::vector<double> durations;
    durations.reserve(k_search_repeats);
    for (int repeat = 0; repeat < k_search_repeats; ++repeat) {
      const auto start = std::chrono::steady_clock::now();
      const Vec2 aim = field.argmax_hit_probability(k_cone_radius);
      const auto end = std::chrono::steady_clock::now();
      durations.push_back(milliseconds(start, end));
      sink += aim.x;
    }
    const Stats stats = summarize(std::move(durations));
    std::printf(
        "argmax_hit_probability (the existing objective, sigma %.4f)  "
        "p50 %7.3f ms  p95 %7.3f ms  p99 %7.3f ms  min %7.3f  max %7.3f  mean %7.3f\n",
        k_cone_radius, stats.p50, stats.p95, stats.p99, stats.min, stats.max, stats.mean);
  }
  {
    std::vector<double> durations;
    durations.reserve(candidates.size());
    for (const Vec2& candidate : candidates) {
      const auto start = std::chrono::steady_clock::now();
      const float probability = field.hit_probability(candidate, k_cone_radius);
      const auto end = std::chrono::steady_clock::now();
      durations.push_back(milliseconds(start, end));
      sink += probability;
    }
    const Stats stats = summarize(std::move(durations));
    std::printf(
        "hit_probability (one evaluation of the existing objective)  "
        "p50 %7.3f ms  p95 %7.3f ms  p99 %7.3f ms  min %7.3f  max %7.3f  mean %7.3f\n",
        stats.p50, stats.p95, stats.p99, stats.min, stats.max, stats.mean);
  }

  // --- the verdict ---------------------------------------------------------------
  input.samples = 64;
  const auto choice = cs2::aim::solve_montecarlo(input, field.cells);
  if (choice.has_value()) {
    std::printf(
        "\nthe chosen candidate: aim (%.5f, %.5f), hit chance %.3f, expected damage %.1f, "
        "target %d, hitgroup %d, can kill %s\n",
        choice->aim.x, choice->aim.y, choice->chance.probability, choice->chance.expected_damage,
        choice->target, choice->hitgroup, choice->can_kill ? "yes" : "no");
  }
  std::printf("(the checksum %.3f keeps the optimiser honest)\n", sink);
  return 0;
}
