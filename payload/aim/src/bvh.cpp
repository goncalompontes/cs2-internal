#include "cs2/aim/bvh.hpp"

#include <algorithm>
#include <cmath>

namespace cs2::aim {
namespace {

constexpr int k_leaf_size = 8;

[[nodiscard]] auto box_centroid(const Aabb& box) noexcept -> Vec3 {
  return Vec3{.x = (box.min.x + box.max.x) * 0.5F,
              .y = (box.min.y + box.max.y) * 0.5F,
              .z = (box.min.z + box.max.z) * 0.5F};
}

}  // namespace

void AabbBvh::build(std::vector<Aabb> boxes) {
  boxes_ = std::move(boxes);
  nodes_.clear();
  order_.clear();
  if (boxes_.empty()) {
    return;
  }
  order_.resize(boxes_.size());
  for (std::size_t i = 0; i < boxes_.size(); ++i) {
    order_[i] = static_cast<int>(i);
  }
  nodes_.reserve(boxes_.size() * 2);
  build_node(0, static_cast<int>(boxes_.size()));
}

auto AabbBvh::build_node(int lo, int hi) -> int {
  const auto index = static_cast<int>(nodes_.size());
  nodes_.emplace_back();

  Aabb bounds = boxes_[order_[lo]];
  Vec3 cmin = box_centroid(bounds);
  Vec3 cmax = cmin;
  for (int i = lo + 1; i < hi; ++i) {
    const Aabb& box = boxes_[order_[i]];
    bounds.min.x = std::min(bounds.min.x, box.min.x);
    bounds.min.y = std::min(bounds.min.y, box.min.y);
    bounds.min.z = std::min(bounds.min.z, box.min.z);
    bounds.max.x = std::max(bounds.max.x, box.max.x);
    bounds.max.y = std::max(bounds.max.y, box.max.y);
    bounds.max.z = std::max(bounds.max.z, box.max.z);
    const Vec3 c = box_centroid(box);
    cmin.x = std::min(cmin.x, c.x);
    cmin.y = std::min(cmin.y, c.y);
    cmin.z = std::min(cmin.z, c.z);
    cmax.x = std::max(cmax.x, c.x);
    cmax.y = std::max(cmax.y, c.y);
    cmax.z = std::max(cmax.z, c.z);
  }
  nodes_[index].bounds = bounds;

  if (hi - lo <= k_leaf_size) {
    nodes_[index].start = lo;
    nodes_[index].count = hi - lo;
    return index;
  }

  const float extent[3] = {cmax.x - cmin.x, cmax.y - cmin.y, cmax.z - cmin.z};
  const int axis =
      extent[0] >= extent[1] && extent[0] >= extent[2] ? 0 : (extent[1] >= extent[2] ? 1 : 2);
  const auto key = [&](int box) -> float {
    const Vec3 c = box_centroid(boxes_[box]);
    return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
  };
  const int mid = lo + (hi - lo) / 2;
  std::nth_element(order_.begin() + lo, order_.begin() + mid, order_.begin() + hi,
                   [&](int a, int b) -> bool { return key(a) < key(b); });

  build_node(lo, mid);  // the left child is `index + 1`
  const int right = build_node(mid, hi);
  nodes_[index].count = 0;
  nodes_[index].right = right;
  return index;
}

void AabbBvh::query(const Aabb& box, std::vector<int>& out) const {
  if (nodes_.empty() || !aabb_intersects(nodes_[0].bounds, box)) {
    return;
  }
  int stack[128];
  int depth = 0;
  stack[depth++] = 0;
  while (depth > 0) {
    const int index = stack[--depth];
    const Node& node = nodes_[index];
    if (!aabb_intersects(node.bounds, box)) {
      continue;
    }
    if (node.count > 0) {
      for (int i = 0; i < node.count; ++i) {
        const int leaf = order_[node.start + i];
        if (aabb_intersects(boxes_[leaf], box)) {
          out.push_back(leaf);
        }
      }
      continue;
    }
    if (depth + 2 <= 128) {
      stack[depth++] = index + 1;
      stack[depth++] = node.right;
    }
  }
}

namespace {

// The hull's inward face planes (`dot(normal, p) <= dist`), brute-forced over the 9 points
// of the eye→box pyramid (n is tiny, and this runs once per tick, not per node). A triple
// is a face when every other point is on one side of it; the winding is arbitrary, so the
// plane is oriented so the hull's remaining points are inside.
struct PyramidPlane {
  Vec3 normal{};
  float dist = 0.0F;
};

auto build_pyramid_planes(const Vec3& eye, const Aabb& box, PyramidPlane* planes, int max_planes)
    -> int {
  const Vec3 lo = box.min;
  const Vec3 hi = box.max;
  const Vec3 points[9] = {{.x = eye.x, .y = eye.y, .z = eye.z}, {.x = lo.x, .y = lo.y, .z = lo.z},
                          {.x = hi.x, .y = lo.y, .z = lo.z},    {.x = hi.x, .y = hi.y, .z = lo.z},
                          {.x = lo.x, .y = hi.y, .z = lo.z},    {.x = lo.x, .y = lo.y, .z = hi.z},
                          {.x = hi.x, .y = lo.y, .z = hi.z},    {.x = hi.x, .y = hi.y, .z = hi.z},
                          {.x = lo.x, .y = hi.y, .z = hi.z}};
  int plane_count = 0;
  for (int i = 0; i < 9 && plane_count < max_planes; ++i) {
    for (int j = i + 1; j < 9 && plane_count < max_planes; ++j) {
      for (int k = j + 1; k < 9 && plane_count < max_planes; ++k) {
        Vec3 normal = game::cross(points[j] - points[i], points[k] - points[i]);
        const float len = game::length(normal);
        if (len <= 1e-6F) {
          continue;  // a collinear triple
        }
        normal = normal * (1.0F / len);
        float dist = game::dot(normal, points[i]);
        bool positive = false;
        bool negative = false;
        for (const Vec3& p : points) {
          const float side = game::dot(normal, p) - dist;
          if (side > 1e-3F) {
            positive = true;
          } else if (side < -1e-3F) {
            negative = true;
          }
        }
        if (positive && negative) {
          continue;  // an interior plane, not a face
        }
        if (!positive && !negative) {
          continue;  // a degenerate (coplanar) triple
        }
        if (positive) {
          normal = normal * -1.0F;
          dist = -dist;
        }
        // Deduplicate a coplanar face reached through a different triple.
        bool duplicate = false;
        for (int m = 0; m < plane_count; ++m) {
          if (game::dot(planes[m].normal, normal) > 0.9999F &&
              std::fabs(planes[m].dist - dist) < 1e-2F) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          planes[plane_count++] = PyramidPlane{.normal = normal, .dist = dist};
        }
      }
    }
  }
  return plane_count;
}

// Is the AABB **entirely** outside one of the hull's face planes? The AABB's minimum
// projection along the inward normal is its closest point to the interior, so `min > dist`
// means every point of the AABB is outside that half-space.
auto outside_any_plane(const PyramidPlane* planes, int count, const Aabb& bounds) -> bool {
  for (int i = 0; i < count; ++i) {
    const PyramidPlane& plane = planes[i];
    const Vec3 nearest{.x = plane.normal.x > 0.0F ? bounds.min.x : bounds.max.x,
                       .y = plane.normal.y > 0.0F ? bounds.min.y : bounds.max.y,
                       .z = plane.normal.z > 0.0F ? bounds.min.z : bounds.max.z};
    if (game::dot(plane.normal, nearest) > plane.dist + 1e-2F) {
      return true;
    }
  }
  return false;
}

}  // namespace

auto AabbBvh::aabb_intersects_pyramid(const Vec3& eye, const Aabb& box,
                                      const Aabb& candidate) noexcept -> bool {
  PyramidPlane planes[32];
  const int count = build_pyramid_planes(eye, box, planes, 32);
  if (count == 0) {
    return true;  // a degenerate hull: keep the candidate
  }
  return !outside_any_plane(planes, count, candidate);
}

void AabbBvh::query_pyramid(const Vec3& eye, const Aabb& box, std::vector<int>& out) const {
  if (nodes_.empty()) {
    return;
  }
  // The pyramid is the convex hull of the eye and the hitbox's 8 corners. Every solid
  // that can occlude *any* point of the hitbox must meet a segment from the eye into the
  // box, so it lies inside that hull. Rejecting a node whose AABB is entirely outside one
  // of the hull's face planes is exact-as-a-conservative-test: it can keep a solid that
  // turns out not to occlude (a false positive), never drop one that does.
  PyramidPlane planes[32];
  const int plane_count = build_pyramid_planes(eye, box, planes, 32);
  if (plane_count == 0) {
    return;  // a degenerate hull: fall back to nothing (the caller keeps the box query)
  }

  const auto inside = [&](const Aabb& bounds) -> bool {
    return !outside_any_plane(planes, plane_count, bounds);
  };

  if (!inside(nodes_[0].bounds)) {
    return;
  }
  int stack[128];
  int depth = 0;
  stack[depth++] = 0;
  while (depth > 0) {
    const int index = stack[--depth];
    const Node& node = nodes_[index];
    if (!inside(node.bounds)) {
      continue;
    }
    if (node.count > 0) {
      for (int i = 0; i < node.count; ++i) {
        const int leaf = order_[node.start + i];
        if (inside(boxes_[leaf])) {
          out.push_back(leaf);
        }
      }
      continue;
    }
    if (depth + 2 <= 128) {
      stack[depth++] = index + 1;
      stack[depth++] = node.right;
    }
  }
}

}  // namespace cs2::aim
