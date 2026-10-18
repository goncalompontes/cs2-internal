#include "cs2/aim/solver.hpp"

#include "cs2/aim/damage.hpp"

namespace cs2::aim {

auto solve(const SolveInput& input) -> SolveResult {
  SolveResult result;

  DamageField field;
  for (const Hitbox& hitbox : input.hitboxes) {
    Polygon polygon;
    float distance = 0.0F;
    if (hitbox.is_capsule) {
      polygon = project_capsule(input.eye, hitbox.capsule.a, hitbox.capsule.b,
                                hitbox.capsule.radius, input.basis);
      const Vec3 center = (hitbox.capsule.a + hitbox.capsule.b) * 0.5F;
      distance = game::distance(input.eye, center);
    } else {
      polygon = project_obb(input.eye, hitbox.box, input.basis);
      distance = game::distance(input.eye, hitbox.box.center);
    }
    if (polygon.empty()) {
      continue;
    }
    if (!input.occlusion.empty()) {
      // Occlusion-aware: split the silhouette by the world solids and value each
      // fragment with the engine penetration model (the wallbang-aware field, the
      // same as the heatmap render).
      const std::vector<DamageFragment> fragments =
          occlude_polygon(input.eye, input.basis, polygon, hitbox.multiplier, distance,
                          input.occlusion, input.weapon, input.penetration,
                          input.penetration_params);
      for (const DamageFragment& fragment : fragments) {
        field.add(fragment.polygon, fragment.value);
      }
    } else {
      field.add_hitbox(polygon, hitbox.multiplier, distance, input.weapon);
    }
  }
  for (const Occluder& occluder : input.occluders) {
    field.clip(occluder.normal, occluder.offset);
  }
  if (field.empty()) {
    return result;
  }

  Vec2 aim{};
  switch (input.objective) {
    case AimObjective::hitchance:
      // The real hitchance when a cone is given (maximize the hit probability);
      // otherwise fall back to the Chebyshev-centre margin heuristic.
      aim = input.spread_sigma > 0.0F ? field.argmax_hit_probability(input.spread_sigma)
                                      : field.argmax_hitchance();
      break;
    case AimObjective::expected:
      aim = field.argmax_expected(input.pose_sigma);
      break;
    case AimObjective::pellets:
      aim = field.argmax_pellets(input.pellet_offsets);
      break;
    case AimObjective::damage:
    default:
      aim = field.argmax_damage();
      break;
  }

  result.valid = true;
  result.aim = aim;
  result.direction = input.basis.unproject(aim);
  result.value = input.objective == AimObjective::pellets
                     ? field.value_at_pellets(aim, input.pellet_offsets)
                     : field.value_at(aim);
  result.hitchance = field.hit_probability(aim, input.spread_sigma);
  result.cells = field.cells.size();
  return result;
}

auto solve_backtrack(const SolveInput& base, const std::vector<PoseCandidate>& poses)
    -> BacktrackResult {
  BacktrackResult best;
  for (const PoseCandidate& pose : poses) {
    if (pose.hitboxes.empty()) {
      continue;
    }
    SolveInput input = base;
    input.hitboxes = pose.hitboxes;
    const SolveResult result = solve(input);
    if (!result.valid) {
      continue;
    }
    // Strict `>` keeps the earlier pose on a tie, so the list must be newest-first
    // (live, then records) for the live aim to win ties.
    if (!best.valid || result.value > best.solve.value) {
      best.valid = true;
      best.solve = result;
      best.record = pose.record;
      best.age = pose.age;
    }
  }
  return best;
}

}  // namespace cs2::aim
