#include "cs2/features/esp_skeleton.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "cs2/features/esp.hpp"
#include "cs2/game/math.hpp"
#include "cs2/game/matrix.hpp"
#include "cs2/render/draw_list.hpp"

namespace cs2::features {
namespace {

using esp_skeleton::BonePoint;
using esp_skeleton::Segment;

// The height order (ascending).
[[nodiscard]] auto by_height(const BonePoint& a, const BonePoint& b) noexcept -> bool {
  return a.z < b.z;
}

// The squared distance between two points.
[[nodiscard]] auto distance_sq(const BonePoint& a, const BonePoint& b) noexcept -> float {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] auto segment(const BonePoint& from, const BonePoint& to) noexcept -> Segment {
  return Segment{from.x, from.y, from.z, to.x, to.y, to.z};
}

}  // namespace

namespace esp_skeleton {

auto build_chain(const std::vector<BonePoint>& points) -> std::vector<Segment> {
  std::vector<Segment> segments;
  if (points.empty()) {
    return segments;
  }
  segments.reserve(16);

  // Bucket the capsules by their hitgroup.
  std::vector<BonePoint> head;
  std::vector<BonePoint> chest;
  std::vector<BonePoint> stomach;
  std::vector<BonePoint> generic;
  std::vector<BonePoint> left_arm;
  std::vector<BonePoint> right_arm;
  std::vector<BonePoint> left_leg;
  std::vector<BonePoint> right_leg;
  for (const BonePoint& point : points) {
    switch (point.group) {
      case k_hitgroup_head:
        head.push_back(point);
        break;
      case k_hitgroup_chest:
        chest.push_back(point);
        break;
      case k_hitgroup_stomach:
        stomach.push_back(point);
        break;
      case k_hitgroup_left_arm:
        left_arm.push_back(point);
        break;
      case k_hitgroup_right_arm:
        right_arm.push_back(point);
        break;
      case k_hitgroup_left_leg:
        left_leg.push_back(point);
        break;
      case k_hitgroup_right_leg:
        right_leg.push_back(point);
        break;
      case k_hitgroup_generic:
        generic.push_back(point);
        break;
      default:
        break;  // a gear hitbox and anything else the model adds
    }
  }

  // The chest capsules, from the top down: the shoulders come first.
  std::sort(chest.begin(), chest.end(), by_height);
  std::reverse(chest.begin(), chest.end());

  // --- The neck and the pelvis ---------------------------------------------
  //
  // Both carry the "generic" hitgroup, so they are told apart by their
  // **position**:
  //
  //   * the neck is the generic capsule nearest the head — the head's bottom
  //     sits directly above the neck;
  //   * the pelvis is the generic capsule nearest the legs' **highest**
  //     capsule — the thigh's top sits directly below the pelvis.
  //
  // A third generic capsule (a gear box) is away from both, so it never
  // steals either role.
  //
  // The model's own heights are the fallback: the chest's height (the
  // stomach's, when the model has no chest capsules) separates the neck
  // (above it) from the pelvis (below it). The chest alone is not enough:
  // without it the "above the chest" test degenerates — every generic
  // capsule counts as "above" — the pelvis is never identified and the legs
  // vanish.
  const BonePoint* neck = nullptr;
  const BonePoint* pelvis = nullptr;

  // The generic capsule nearest `anchor`, skipping the one already taken.
  const auto nearest_generic = [&generic](const BonePoint& anchor,
                                          const BonePoint* taken) -> const BonePoint* {
    const BonePoint* best = nullptr;
    float best_distance = 0.0F;
    for (const BonePoint& point : generic) {
      if (taken != nullptr && &point == taken) {
        continue;
      }
      const float distance = distance_sq(point, anchor);
      if (best == nullptr || distance < best_distance) {
        best = &point;
        best_distance = distance;
      }
    }
    return best;
  };

  if (!generic.empty()) {
    // The neck: the head's lowest capsule is the one right above it.
    if (!head.empty()) {
      const BonePoint* lowest_head = &*std::min_element(head.begin(), head.end(), by_height);
      neck = nearest_generic(*lowest_head, nullptr);
    }
    // The pelvis: the legs' highest capsule (the thigh's top) is the one
    // right below it.
    const BonePoint* thigh = nullptr;
    for (const BonePoint& point : left_leg) {
      if (thigh == nullptr || point.z > thigh->z) {
        thigh = &point;
      }
    }
    for (const BonePoint& point : right_leg) {
      if (thigh == nullptr || point.z > thigh->z) {
        thigh = &point;
      }
    }
    if (thigh != nullptr) {
      pelvis = nearest_generic(*thigh, neck);
    }
  }

  // The reference height: the chest's own, the stomach's when the model has
  // no chest capsules, the head's as the last resort.
  float reference = 0.0F;
  bool have_reference = false;
  if (!chest.empty()) {
    for (const BonePoint& point : chest) {
      reference += point.z;
    }
    reference /= static_cast<float>(chest.size());
    have_reference = true;
  } else if (!stomach.empty()) {
    reference = stomach.front().z;
    have_reference = true;
  } else if (!head.empty()) {
    reference = std::max_element(head.begin(), head.end(), by_height)->z;
    have_reference = true;
  }

  if (!generic.empty() && (neck == nullptr || pelvis == nullptr)) {
    if (!have_reference) {
      // No height to compare against: the highest generic capsule is the
      // neck and the next one down is the pelvis.
      std::vector<const BonePoint*> ordered;
      ordered.reserve(generic.size());
      for (const BonePoint& point : generic) {
        ordered.push_back(&point);
      }
      std::sort(
          ordered.begin(), ordered.end(),
          [](const BonePoint* a, const BonePoint* b) noexcept -> bool { return a->z > b->z; });
      if (neck == nullptr && !ordered.empty()) {
        neck = ordered.front();
      }
      if (pelvis == nullptr && ordered.size() > 1) {
        pelvis = ordered[1];
      }
    } else {
      for (const BonePoint& point : generic) {
        if (neck != nullptr && &point == neck) {
          continue;
        }
        if (pelvis != nullptr && &point == pelvis) {
          continue;
        }
        if (point.z > reference) {
          // The neck: the closest generic capsule above the reference.
          if (neck == nullptr || (point.z - reference) < (neck->z - reference)) {
            neck = &point;
          }
        } else {
          // The pelvis: the closest generic capsule below the reference.
          if (pelvis == nullptr || (reference - point.z) < (reference - pelvis->z)) {
            pelvis = &point;
          }
        }
      }
    }
  }

  // The torso, in order: head, neck, chest (top down), stomach, pelvis. A
  // missing node drops out and its neighbours join up, so the chain is never
  // broken by an absent capsule.
  std::vector<BonePoint> torso;
  torso.reserve(8);
  if (!head.empty()) {
    torso.push_back(*std::max_element(head.begin(), head.end(), by_height));
  }
  if (neck != nullptr) {
    torso.push_back(*neck);
  }
  for (const BonePoint& point : chest) {
    torso.push_back(point);
  }
  if (!stomach.empty()) {
    torso.push_back(stomach.front());
  }
  if (pelvis != nullptr) {
    torso.push_back(*pelvis);
  }
  for (std::size_t i = 1; i < torso.size(); ++i) {
    segments.push_back(segment(torso[i - 1], torso[i]));
  }

  // The limbs: every capsule of a limb shares one hitgroup, so they are
  // ordered by their distance from the limb's root — the upper arm is the
  // closest to the chest, the hand the farthest.
  const BonePoint* arm_root = nullptr;
  if (!chest.empty()) {
    arm_root = &chest.front();
  } else if (neck != nullptr) {
    arm_root = neck;
  } else if (!stomach.empty()) {
    arm_root = &stomach.front();
  }

  const auto limb_chain = [&segments](std::vector<BonePoint>& limb, const BonePoint* root) -> void {
    if (limb.empty() || root == nullptr) {
      return;
    }
    std::sort(limb.begin(), limb.end(), [root](const BonePoint& a, const BonePoint& b) {
      return distance_sq(*root, a) < distance_sq(*root, b);
    });
    segments.push_back(segment(*root, limb.front()));
    for (std::size_t i = 1; i < limb.size(); ++i) {
      segments.push_back(segment(limb[i - 1], limb[i]));
    }
  };

  limb_chain(left_arm, arm_root);
  limb_chain(right_arm, arm_root);
  limb_chain(left_leg, pelvis);
  limb_chain(right_leg, pelvis);
  return segments;
}

}  // namespace esp_skeleton

namespace esp_detail {

void draw_skeleton(render::DrawList& out, const game::Matrix4x4& view, float width, float height,
                   const game::HitboxCapsule* capsules, int count, const render::Color& color) {
  if (capsules == nullptr || count <= 0) {
    return;
  }
  std::vector<esp_skeleton::BonePoint> points;
  points.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    const game::HitboxCapsule& capsule = capsules[i];
    points.push_back(esp_skeleton::BonePoint{
        .group = capsule.group,
        .x = (capsule.a.x + capsule.b.x) * 0.5F,
        .y = (capsule.a.y + capsule.b.y) * 0.5F,
        .z = (capsule.a.z + capsule.b.z) * 0.5F,
    });
  }
  for (const esp_skeleton::Segment& bone : esp_skeleton::build_chain(points)) {
    const auto a = view.project(game::Vec3{bone.ax, bone.ay, bone.az}, width, height);
    const auto b = view.project(game::Vec3{bone.bx, bone.by, bone.bz}, width, height);
    if (!a.has_value() || !b.has_value()) {
      continue;  // the segment is behind the camera
    }
    out.line(a->x, a->y, b->x, b->y, color, 1.0F);
  }
}

}  // namespace esp_detail
}  // namespace cs2::features
