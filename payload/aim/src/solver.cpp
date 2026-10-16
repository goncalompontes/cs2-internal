#include "cs2/aim/solver.hpp"

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
    field.add_hitbox(polygon, hitbox.multiplier, distance, input.weapon);
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
      aim = field.argmax_hitchance();
      break;
    case AimObjective::expected:
      aim = field.argmax_expected(input.pose_sigma);
      break;
    case AimObjective::damage:
    default:
      aim = field.argmax_damage();
      break;
  }

  result.valid = true;
  result.aim = aim;
  result.direction = input.basis.unproject(aim);
  result.value = field.value_at(aim);
  result.cells = field.cells.size();
  return result;
}

}  // namespace cs2::aim
