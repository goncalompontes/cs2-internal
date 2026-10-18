#pragma once

// The aim solver: build the damage field from hitbox silhouettes and occluders,
// then pick the aim (docs/ragebot.md, ADR-0022).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cs2/aim/field.hpp"
#include "cs2/aim/hitbox.hpp"
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
  // The deterministic pellet offsets (chart space) for `AimObjective::pellets`; the
  // engine's exact offsets come from `RandomSeed(cmd_seed)` (docs/re/aim.md).
  std::vector<Vec2> pellet_offsets;
  std::uintptr_t pawn = 0;  // the target pawn (diagnostics / hitbox read)
};

struct SolveResult {
  bool valid = false;
  Vec2 aim{};          // chart point
  Vec3 direction{};    // world direction
  float value = 0.0F;  // field value at the aim
  std::size_t cells = 0;
};

[[nodiscard]] auto solve(const SolveInput& input) -> SolveResult;

// One candidate pose for the backtrack aim (docs/ragebot.md §8): the live pose
// (`record == -1`) or a sampled lag record the server could rewind to.
struct PoseCandidate {
  int record = -1;   // the lag record's tick, or -1 for the live pose
  float age = 0.0F;  // how many ticks behind the newest pose (0 = live)
  std::vector<Hitbox> hitboxes;
};

struct BacktrackResult {
  bool valid = false;
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
[[nodiscard]] auto solve_backtrack(const SolveInput& base, const std::vector<PoseCandidate>& poses)
    -> BacktrackResult;

}  // namespace cs2::aim
