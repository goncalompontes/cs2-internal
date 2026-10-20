// Unit tests for the AABB BVH (docs/ragebot-perf.md §3.7). The BVH must return
// **exactly** the same set of overlapping boxes as a linear scan — it is a pure
// acceleration of the world-hull enumeration, so a mismatch would drop occluders and
// change the penetration result.

#include <algorithm>
#include <cstdio>
#include <vector>

#include "cs2/aim/bvh.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// A small deterministic LCG so the test is reproducible without <random>.
struct Rng {
  std::uint32_t state = 0x12345678U;
  auto next() -> std::uint32_t {
    state = state * 1664525U + 1013904223U;
    return state;
  }
  auto unit() -> float {
    return static_cast<float>(next() >> 8) / 16777216.0F;
  }
  auto range(float lo, float hi) -> float {
    return lo + (hi - lo) * unit();
  }
};

auto sorted(std::vector<int> v) -> std::vector<int> {
  std::sort(v.begin(), v.end());
  return v;
}

// Does the segment `a`-`b` intersect the box? A slab test (the ray/AABB clip), used only
// by the pyramid test to define "this box could occlude that hitbox".
auto segment_hits_box(const cs2::aim::Vec3& a, const cs2::aim::Vec3& b, const cs2::aim::Aabb& box)
    -> bool {
  float t0 = 0.0F;
  float t1 = 1.0F;
  const float origin[3] = {a.x, a.y, a.z};
  const float direction[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
  const float lo[3] = {box.min.x, box.min.y, box.min.z};
  const float hi[3] = {box.max.x, box.max.y, box.max.z};
  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(direction[axis]) < 1e-6F) {
      if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
        return false;
      }
      continue;
    }
    float ta = (lo[axis] - origin[axis]) / direction[axis];
    float tb = (hi[axis] - origin[axis]) / direction[axis];
    if (ta > tb) {
      std::swap(ta, tb);
    }
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
    if (t0 > t1) {
      return false;
    }
  }
  return true;
}

}  // namespace

auto main() -> int {
  using cs2::aim::Aabb;
  using cs2::aim::aabb_intersects;
  using cs2::aim::AabbBvh;
  using cs2::aim::Vec3;

  Rng rng;
  std::vector<Aabb> boxes;
  constexpr int k_count = 4000;
  boxes.reserve(k_count);
  for (int i = 0; i < k_count; ++i) {
    const float x = rng.range(-2000.0F, 2000.0F);
    const float y = rng.range(-2000.0F, 2000.0F);
    const float z = rng.range(-500.0F, 500.0F);
    const float ex = rng.range(1.0F, 60.0F);
    const float ey = rng.range(1.0F, 60.0F);
    const float ez = rng.range(1.0F, 60.0F);
    boxes.push_back(
        Aabb{.min = {.x = x, .y = y, .z = z}, .max = {.x = x + ex, .y = y + ey, .z = z + ez}});
  }

  AabbBvh bvh;
  bvh.build(boxes);
  expect(!bvh.empty() && bvh.size() == k_count, "the BVH builds over every box");

  // A query must return exactly the linear-scan set (compare as sorted index sets).
  int queries = 0;
  for (int q = 0; q < 200; ++q) {
    const float x = rng.range(-2200.0F, 2200.0F);
    const float y = rng.range(-2200.0F, 2200.0F);
    const float z = rng.range(-600.0F, 600.0F);
    const float ex = rng.range(1.0F, 400.0F);
    const float ey = rng.range(1.0F, 400.0F);
    const float ez = rng.range(1.0F, 400.0F);
    const Aabb query{.min = {.x = x, .y = y, .z = z},
                     .max = {.x = x + ex, .y = y + ey, .z = z + ez}};

    std::vector<int> linear;
    for (int i = 0; i < k_count; ++i) {
      if (aabb_intersects(boxes[static_cast<std::size_t>(i)], query)) {
        linear.push_back(i);
      }
    }
    std::vector<int> via_bvh;
    bvh.query(query, via_bvh);
    expect(sorted(linear) == sorted(via_bvh), "the BVH query equals the linear scan");
    ++queries;
  }
  expect(queries == 200, "all query cases ran");

  // A box fully outside the cloud returns nothing; a box covering everything returns
  // every index.
  {
    std::vector<int> none;
    bvh.query(Aabb{.min = {.x = 100000.0F, .y = 100000.0F, .z = 100000.0F},
                   .max = {.x = 100001.0F, .y = 100001.0F, .z = 100001.0F}},
              none);
    expect(none.empty(), "a far query returns nothing");
    std::vector<int> all;
    bvh.query(
        Aabb{.min = {.x = -1e6F, .y = -1e6F, .z = -1e6F}, .max = {.x = 1e6F, .y = 1e6F, .z = 1e6F}},
        all);
    expect(all.size() == static_cast<std::size_t>(k_count), "a covering query returns every box");
  }

  // An empty BVH is valid and returns nothing.
  {
    AabbBvh empty_bvh;
    empty_bvh.build({});
    std::vector<int> out;
    empty_bvh.query(
        Aabb{.min = {.x = -1.0F, .y = -1.0F, .z = -1.0F}, .max = {.x = 1.0F, .y = 1.0F, .z = 1.0F}},
        out);
    expect(empty_bvh.empty() && out.empty(), "an empty BVH has no hits");
  }

  // The eye→hitbox pyramid query must be **conservative**: every solid that can occlude
  // *any* point of the hitbox must survive. A solid occludes a point when the segment from
  // the eye to that point passes through it, so the test samples the hitbox volume (the 8
  // corners, the centre and random interior points) and requires every box any of those
  // rays crosses to be kept. False positives are allowed; a miss is not.
  {
    int kept_total = 0;
    int missed = 0;
    int rays = 0;
    for (int q = 0; q < 40; ++q) {
      const Vec3 eye{.x = rng.range(-100.0F, 100.0F),
                     .y = rng.range(-100.0F, 100.0F),
                     .z = rng.range(-100.0F, 100.0F)};
      const Vec3 c{.x = rng.range(-400.0F, 400.0F),
                   .y = rng.range(-400.0F, 400.0F),
                   .z = rng.range(-400.0F, 400.0F)};
      const float ex = rng.range(4.0F, 30.0F);
      const float ey = rng.range(4.0F, 30.0F);
      const float ez = rng.range(4.0F, 30.0F);
      const Aabb hitbox{.min = {.x = c.x - ex, .y = c.y - ey, .z = c.z - ez},
                        .max = {.x = c.x + ex, .y = c.y + ey, .z = c.z + ez}};

      std::vector<int> via_pyramid;
      bvh.query_pyramid(eye, hitbox, via_pyramid);
      kept_total += static_cast<int>(via_pyramid.size());

      // Sample the hitbox: its centre, 8 corners and a few random interior points.
      std::vector<Vec3> samples = {c};
      for (int corner = 0; corner < 8; ++corner) {
        samples.push_back(Vec3{.x = (corner & 1) != 0 ? hitbox.max.x : hitbox.min.x,
                               .y = (corner & 2) != 0 ? hitbox.max.y : hitbox.min.y,
                               .z = (corner & 4) != 0 ? hitbox.max.z : hitbox.min.z});
      }
      for (int i = 0; i < 4; ++i) {
        samples.push_back(Vec3{.x = rng.range(hitbox.min.x, hitbox.max.x),
                               .y = rng.range(hitbox.min.y, hitbox.max.y),
                               .z = rng.range(hitbox.min.z, hitbox.max.z)});
      }
      for (const Vec3& point : samples) {
        ++rays;
        for (int i = 0; i < k_count; ++i) {
          if (!segment_hits_box(eye, point, boxes[static_cast<std::size_t>(i)])) {
            continue;
          }
          if (std::find(via_pyramid.begin(), via_pyramid.end(), i) == via_pyramid.end()) {
            ++missed;
          }
        }
      }
    }
    expect(rays > 0, "the pyramid test sampled rays");
    expect(missed == 0, "the pyramid query keeps every box an eye->hitbox ray crosses");
    expect(kept_total > 0, "the pyramid query keeps some geometry");
  }

  if (failures == 0) {
    std::printf("aim-bvh: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
