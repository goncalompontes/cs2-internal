#pragma once

// The aim solver: build the damage field from hitbox silhouettes and occluders,
// then pick the aim (docs/ragebot.md, ADR-0022).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/hitbox.hpp"
#include "cs2/aim/montecarlo.hpp"
#include "cs2/aim/occlusion.hpp"
#include "cs2/aim/penetration.hpp"

namespace cs2::aim {

// An occluder silhouette half-plane in the chart (outward normal + offset):
// keeps `dot(normal, p) <= offset`.
struct Occluder {
  Vec2 normal;
  float offset = 0.0F;
};

enum class AimObjective {
  damage,     // nearest point of the best cell
  hitchance,  // Chebyshev centre of the best cell
  expected,   // pose-blurred field maximum
  pellets,    // deterministic pellet-sum maximum (K>1 spread, docs/ragebot.md §3)
  // The Monte-Carlo hit-chance solver (`cs2/aim/montecarlo.hpp`): the engine's
  // own spread kernel, sampled, with a **nearest-hit** ray test against the
  // targets' hitboxes.
  //
  // **It is a value, not a behaviour change**: the four objectives above keep
  // their exact behaviour, and a `SolveInput` that does not set the
  // Monte-Carlo parameters is solved exactly as it was before this value
  // existed.
  monte_carlo,
};

struct SolveInput {
  Vec3 eye{};
  Basis basis{};
  std::vector<Hitbox> hitboxes;
  std::vector<Occluder> occluders;
  // The world solids between the eye and the target, projected into the chart
  // (`docs/ragebot.md` §3, `payload/control/src/occlusion_query.cpp`). When non-empty
  // the solver splits each hitbox silhouette by them and attenuates with the chosen
  // penetration model — the same field the heatmap renders, so the rage aim is
  // wallbang-aware instead of aiming at a fully-occluded hitbox.
  std::vector<OccluderCell> occlusion;
  PenetrationModel penetration = PenetrationModel::engine;
  PenetrationParams penetration_params{};
  WeaponProfile weapon{};
  AimObjective objective = AimObjective::damage;
  float pose_sigma = 0.0F;  // pose-uncertainty blur (0 = none)
  // The weapon's cone std-dev (inaccuracy + spread, radians): the hitchance objective
  // maximizes the probability the cone lands on the target (`hit_probability`). 0
  // falls back to the Chebyshev-centre margin heuristic.
  float spread_sigma = 0.0F;
  // The deterministic pellet offsets (chart space) for `AimObjective::pellets`; the
  // engine's exact offsets come from `RandomSeed(cmd_seed)` (docs/re/aim.md).
  std::vector<Vec2> pellet_offsets;
  std::uintptr_t pawn = 0;  // the target pawn (diagnostics / hitbox read)

  // --- The Monte-Carlo objective's parameters --------------------------------
  //
  // **They are optional.** A `SolveInput` that does not set them is solved
  // exactly as it was before the objective existed, and the four other
  // objectives never read them.
  //
  // The **sample count** is 64 by default: it is the only count that fits the
  // frame budget at a live-sized hitbox set (19 hitboxes on a player, so 57 for
  // three targets — `docs/re/aim.md`: `m_nHitBoxes (19 on a player)`), where 128
  // is over the 12 ms guard and 256 is over the tick.
  int monte_carlo_samples = 64;
  // The seed context: the command's sequence number, the prediction tick and the
  // policy that turns them into seeds (`cs2/aim/montecarlo.hpp`).
  SeedContext monte_carlo_seed{};
  // The candidate budget: the bounded number of aims the search evaluates.
  CandidateBudget monte_carlo_budget{};
  // The remaining health of **every** candidate target, for the can-kill test.
  // The rule's first key is judged **per target** — a target's own damage
  // against that target's own health — so the list must cover every target the
  // solve considered, not only the one that was finally chosen.
  std::vector<TargetHealth> monte_carlo_targets;
  // The weapon's **pellet pattern**. The core has **no default** for it: a
  // default would silently drop the revolver's `1 - r^2` transform.
  SpreadPattern monte_carlo_pattern = SpreadPattern::standard;
  // The weapon's pellet count. The multi-pellet weapons are exactly the ones
  // `pattern_supported` refuses, so the live path fires a single pellet.
  int monte_carlo_pellets = 1;
};

// The Monte-Carlo objective's verdict: the numbers the Monte-Carlo solver
// produced, and the policy that produced them.
//
// **The numbers mean something different from the field's.** `probability` is the
// **measured** chance that the shot hits (the fraction of the samples that hit),
// where the field's `hitchance` is a Gaussian mass whose sigma is too wide
// (`docs/research/aim-objective-math.md` §"the sqrt(6) sigma"); and
// `expected_damage` is the **expected** damage (a miss contributes 0), where the
// field's `value` is the damage of a shot that hits.
struct MonteCarloVerdict {
  float probability = 0.0F;      // the measured chance that the shot hits
  float expected_damage = 0.0F;  // the mean damage over **all** the samples
  int samples = 0;               // how many samples were drawn
  int hits = 0;                  // how many of them hit
  // The seed policy that produced the number: under `sequence` it is the
  // expectation over the next N shots, under `angle_grid` the probability over
  // the shot that would be taken (`cs2/aim/montecarlo.hpp`).
  SeedSource seed_source = SeedSource::sequence;
  int target = -1;        // the target of the nearest hit
  int hitgroup = 0;       // the hitgroup of the nearest hit
  bool can_kill = false;  // does the attributed damage reach the target's health?
  // How many hitboxes the **filter** dropped for the core.
  //
  // The reader does not validate the shape it reads, so a malformed hitbox
  // (a capsule with a radius of zero) reaches the solver; the core refuses a
  // scene that holds one, so the objective filters the set for the core
  // (`docs/ragebot.md` §13.3). **The number is published**, because a dropped
  // hitbox makes the measured probability a **lower bound** on the truth — and
  // a silent lower bound is a number the operator would read as exact.
  int filtered_hitboxes = 0;
};

struct SolveResult {
  bool valid = false;
  bool occlusion_candidates_complete = true;  // false when the per-hitbox cap truncated input
  Vec2 aim{};                                 // chart point
  Vec3 direction{};                           // world direction
  float value = 0.0F;                         // field value at the aim
  // The probability the shot lands on the target for the chosen aim (the cone
  // `spread_sigma`): 1 when the whole cone is inside a hitbox, 0 when it misses.
  float hitchance = 0.0F;
  // True when the chosen aim's damage is reduced by a wall in front (its unoccluded
  // value is larger) — the "only visible / no wallbang" fire gate.
  bool penetrated = false;
  std::size_t cells = 0;
  // The Monte-Carlo objective's verdict — present when the objective **was** the
  // Monte-Carlo one and the solver produced a number. It is absent when the
  // objective was another one, and when the Monte-Carlo solver **refused** (the
  // reason is in `monte_carlo_refusal`, and the aim came from the `damage`
  // objective instead).
  std::optional<MonteCarloVerdict> monte_carlo{};
  // The Monte-Carlo solver's **refusal**: why it produced no number.
  //
  // **A fallback is never silent.** The objective falls back to `damage` — the
  // cheapest, always-available one — and the reason lands here, so the caller
  // can tell the operator that the aim is not the Monte-Carlo's.
  std::optional<MonteCarloError> monte_carlo_refusal{};
};

// Optional per-solve breakdown for the ragebot instrumentation (`docs/ragebot-perf.md`).
// All times are microseconds; `solve`/`solve_backtrack` accumulate into it when a
// non-null pointer is passed (the default keeps the hot path unmeasured). This is
// how the pipeline's cost is attributed without a profiler.
struct SolveStats {
  std::uint64_t project_us = 0;  // hitbox silhouette projection
  std::uint64_t occlude_us = 0;  // occlude_polygon (fragment split + penetration)
  std::uint64_t argmax_us = 0;   // the objective's optimum search
  std::uint64_t total_us = 0;
  std::uint32_t solves = 0;     // number of `solve` calls
  std::uint32_t hitboxes = 0;   // hitbox silhouettes projected
  std::uint32_t fragments = 0;  // damage fragments emitted
  std::uint32_t cells = 0;      // field cells after occlusion
};

[[nodiscard]] auto solve(const SolveInput& input, SolveStats* stats = nullptr) -> SolveResult;

// One candidate pose for the backtrack aim (docs/ragebot.md §8): the live pose
// (`record == -1`) or a sampled lag record the server could rewind to.
struct PoseCandidate {
  int record = -1;   // the lag record's tick, or -1 for the live pose
  float age = 0.0F;  // how many ticks behind the newest pose (0 = live)
  std::vector<Hitbox> hitboxes;
};

struct BacktrackResult {
  bool valid = false;
  bool occlusion_candidates_complete = true;
  SolveResult solve{};  // the winning pose's aim / value / cells
  int record = -1;      // the winning pose's record (-1 = live)
  float age = 0.0F;
};

// Evaluate `base` against every candidate pose and return the one maximizing the
// objective's value — the backtracking selection (`docs/ragebot.md` §8: the discrete
// record set replaces the continuous pose distribution; the server rewinds the
// target to a past pose within the lag-comp window, so a record can beat the live
// pose). Poses are tried in order, so ties keep the earlier (newest / live) pose:
// the live aim wins unless a record is strictly better.
[[nodiscard]] auto solve_backtrack(const SolveInput& base, const std::vector<PoseCandidate>& poses,
                                   SolveStats* stats = nullptr) -> BacktrackResult;

}  // namespace cs2::aim
