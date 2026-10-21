#pragma once

// The exact ray/solid span arithmetic, shared by the occluders and the Monte-Carlo
// hit test.
//
// **Why it is a header.** The occluder path (`payload/aim/src/occlusion.cpp`) and the
// Monte-Carlo solver (`payload/aim/src/montecarlo.cpp`) both need the same question
// answered: *"where does this ray lie inside this convex solid?"* The arithmetic was
// private to the occluder translation unit; it is extracted here so the hit test
// **reuses** it instead of duplicating it (`docs/orchestration/spec-t34-montecarlo.md`
// §2.3). The arithmetic itself is unchanged — the occluder tests (`aim-occlusion`) are
// the regression for that.
//
// **The spans are exact.** A sphere, a capsule and an oriented box are convex, and a
// line meets a convex set in an interval, so the entry/exit pair below *is* the
// solid's intersection with the ray — no sampling, no approximation.

#include <algorithm>
#include <cmath>
#include <optional>

#include "cs2/aim/occlusion.hpp"

namespace cs2::aim {

// The degeneracy threshold of the span arithmetic (the occluders' `k_epsilon`).
inline constexpr float RAY_SPAN_EPSILON = 1.0e-6F;

// The ray interval inside a sphere `center`,`radius` (exact; `direction` unit).
[[nodiscard]] inline auto ray_sphere_span(const Vec3& eye, const Vec3& direction,
                                          const Vec3& center, float radius) noexcept
    -> std::optional<RaySpan> {
  const Vec3 oc = eye - center;
  const float b = game::dot(oc, direction);
  const float c = game::dot(oc, oc) - radius * radius;
  const float disc = b * b - c;
  if (disc < 0.0F) {
    return std::nullopt;
  }
  const float s = std::sqrt(disc);
  const float t_exit = -b + s;
  if (t_exit < 0.0F) {
    return std::nullopt;  // entirely behind the eye
  }
  return RaySpan{.entry = std::max(-b - s, 0.0F), .exit = t_exit};
}

// Where the ray is inside the *infinite* cylinder of `radius` about the axis through
// `a` in unit direction `axis`, clipped to the segment's axial extent [0, length].
[[nodiscard]] inline auto ray_cylinder_span(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                            const Vec3& axis, float length, float radius) noexcept
    -> std::optional<RaySpan> {
  const Vec3 oc = eye - a;
  const float d_par = game::dot(direction, axis);
  const float o_par = game::dot(oc, axis);
  const Vec3 d_perp = direction - axis * d_par;
  const Vec3 o_perp = oc - axis * o_par;
  const float quad_a = game::dot(d_perp, d_perp);
  const float quad_b = 2.0F * game::dot(o_perp, d_perp);
  const float quad_c = game::dot(o_perp, o_perp) - radius * radius;
  float t_enter = -1.0e30F;
  float t_exit = 1.0e30F;
  if (quad_a > RAY_SPAN_EPSILON) {
    const float disc = quad_b * quad_b - 4.0F * quad_a * quad_c;
    if (disc < 0.0F) {
      return std::nullopt;
    }
    const float s = std::sqrt(disc);
    t_enter = (-quad_b - s) / (2.0F * quad_a);
    t_exit = (-quad_b + s) / (2.0F * quad_a);
  } else if (quad_c > 0.0F) {
    return std::nullopt;  // parallel to the axis and outside the radius
  }
  // Clip to the slab where the axial coordinate stays within the segment.
  if (std::fabs(d_par) <= RAY_SPAN_EPSILON) {
    if (o_par < 0.0F || o_par > length) {
      return std::nullopt;
    }
  } else {
    float s0 = (0.0F - o_par) / d_par;
    float s1 = (length - o_par) / d_par;
    if (s0 > s1) {
      std::swap(s0, s1);
    }
    t_enter = std::max(t_enter, s0);
    t_exit = std::min(t_exit, s1);
  }
  if (t_enter > t_exit) {
    return std::nullopt;
  }
  return RaySpan{.entry = t_enter, .exit = t_exit};
}

// The ray interval inside a capsule (`a`-`b` thickened by `radius`). A capsule is the
// union of the two cap spheres and the side cylinder, all convex sub-parts of one
// convex solid, so the union of their intervals is the solid's single interval.
[[nodiscard]] inline auto ray_capsule_span(const Vec3& eye, const Vec3& direction, const Vec3& a,
                                           const Vec3& b, float radius) noexcept
    -> std::optional<RaySpan> {
  float entry = 1.0e30F;
  float exit = -1.0e30F;
  bool hit = false;
  const auto consider = [&](const std::optional<RaySpan>& span) -> void {
    if (span.has_value()) {
      entry = std::min(entry, span->entry);
      exit = std::max(exit, span->exit);
      hit = true;
    }
  };
  consider(ray_sphere_span(eye, direction, a, radius));
  consider(ray_sphere_span(eye, direction, b, radius));
  const Vec3 ab = b - a;
  const float length = game::length(ab);
  if (length > RAY_SPAN_EPSILON) {
    consider(ray_cylinder_span(eye, direction, a, ab * (1.0F / length), length, radius));
  }
  if (!hit || exit < 0.0F) {
    return std::nullopt;
  }
  return RaySpan{.entry = std::max(entry, 0.0F), .exit = exit};
}

// The ray interval inside an **oriented box** — the slab test in the box's own frame
// (`Obb`'s axes are unit and mutually orthogonal, `cs2/aim/geometry.hpp`). Exact for
// a box, and the shape the non-capsule hitboxes use.
[[nodiscard]] inline auto ray_obb_span(const Vec3& eye, const Vec3& direction,
                                       const Obb& box) noexcept -> std::optional<RaySpan> {
  const Vec3 axes[3] = {box.axis_x, box.axis_y, box.axis_z};
  const float halves[3] = {box.half.x, box.half.y, box.half.z};
  const Vec3 offset = eye - box.center;
  float t_enter = -1.0e30F;
  float t_exit = 1.0e30F;
  for (int i = 0; i < 3; ++i) {
    const float d = game::dot(direction, axes[i]);
    const float o = game::dot(offset, axes[i]);
    if (std::fabs(d) < RAY_SPAN_EPSILON) {
      if (std::fabs(o) > halves[i]) {
        return std::nullopt;  // parallel to this slab and outside it
      }
      continue;
    }
    const float inv = 1.0F / d;
    float t1 = (-halves[i] - o) * inv;
    float t2 = (halves[i] - o) * inv;
    if (t1 > t2) {
      std::swap(t1, t2);
    }
    t_enter = std::max(t_enter, t1);
    t_exit = std::min(t_exit, t2);
    if (t_enter > t_exit) {
      return std::nullopt;
    }
  }
  if (t_exit < 0.0F) {
    return std::nullopt;  // entirely behind the eye
  }
  return RaySpan{.entry = std::max(t_enter, 0.0F), .exit = t_exit};
}

}  // namespace cs2::aim
