#pragma once

// A static AABB bounding-volume hierarchy (docs/ragebot-perf.md §3.7). Built from a
// list of axis-aligned boxes; a box query returns the indices whose box overlaps it in
// O(log n + k). Pure geometry — no engine dependency — so it is host-tested
// (`aim-bvh`), and the payload uses it for the cached world hulls' enumeration.

#include <cstdint>
#include <vector>

#include "cs2/aim/geometry.hpp"

namespace cs2::aim {

struct Aabb {
  Vec3 min{};
  Vec3 max{};
};

// Does `a` overlap `b` (touching counts)?
[[nodiscard]] inline auto aabb_intersects(const Aabb& a, const Aabb& b) noexcept -> bool {
  return a.max.x >= b.min.x && a.min.x <= b.max.x && a.max.y >= b.min.y && a.min.y <= b.max.y &&
         a.max.z >= b.min.z && a.min.z <= b.max.z;
}

// A half-space whose interior is `dot(normal, p) <= dist` (the same convention as
// `OccluderPlane`). Used to describe the eye→hitbox pyramid: a solid can only occlude a
// hitbox point if it meets the segment from the eye to that point, so every occluder
// lies inside the convex hull of the eye and the hitbox — a much tighter volume than the
// eye→hitbox AABB (`docs/ragebot-perf.md` §2.1).
struct HalfSpace {
  Vec3 normal{};
  float dist = 0.0F;
};

class AabbBvh {
 public:
  // Build over `boxes`. The tree is immutable afterwards; `query` is const and appends
  // the overlapping indices (ascending is not guaranteed).
  void build(std::vector<Aabb> boxes);

  // Append every index whose box overlaps `box`.
  void query(const Aabb& box, std::vector<int>& out) const;

  // Append every index whose box **can** lie inside the convex hull of `eye` and `box`
  // (the eye→box pyramid), i.e. whose AABB intersects that hull. This is the exact
  // conservative test for "can this solid be between the eye and that hitbox": a solid
  // outside the pyramid cannot intersect any segment from the eye into the box, so it
  // can never occlude it. Implemented as a node-AABB vs hull SAT (separating-axis), so it
  // is still O(log n + k) and never drops a solid the box query would keep.
  void query_pyramid(const Vec3& eye, const Aabb& box, std::vector<int>& out) const;

  // Does `candidate` intersect the convex hull of `eye` and `box`? The single-AABB form of
  // the `query_pyramid` test (same conservative semantics), used to prove the pyramid kept
  // everything it should on live data.
  [[nodiscard]] static auto aabb_intersects_pyramid(const Vec3& eye, const Aabb& box,
                                                    const Aabb& candidate) noexcept -> bool;

  [[nodiscard]] auto empty() const noexcept -> bool {
    return nodes_.empty();
  }
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return boxes_.size();
  }
  [[nodiscard]] auto boxes() const noexcept -> const std::vector<Aabb>& {
    return boxes_;
  }

 private:
  struct Node {
    Aabb bounds{};
    int start = 0;  // leaf: first index into `order_`
    int count = 0;  // 0 = internal, else the leaf's box count
    int right = 0;  // internal: right child index (left = own index + 1)
  };

  [[nodiscard]] auto build_node(int lo, int hi) -> int;

  std::vector<Aabb> boxes_;
  std::vector<Node> nodes_;
  std::vector<int> order_;  // box indices, leaves contiguous
};

}  // namespace cs2::aim
