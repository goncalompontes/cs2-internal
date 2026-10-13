// Unit tests for the world-to-screen projection convention.
//
// Source 2 stores the per-slot P x V matrix row-major and applies it to a
// column vector: `clip.i = sum_j m[i*4+j] * world.j`. This guards that
// convention, because the row-vector form silently produces plausible-looking
// but wrong output (verified against the live game matrix).

#include <cmath>
#include <cstdio>

#include "cs2/game/matrix.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

bool near(float a, float b, float tolerance = 1e-3F) {
  return std::fabs(a - b) <= tolerance;
}

}  // namespace

int main() {
  using cs2::game::Matrix4x4;
  using cs2::game::Vec3;

  constexpr float k_width = 1600.0F;
  constexpr float k_height = 900.0F;

  // Camera at the world origin looking down +Y, up = +Z, fov 90, aspect 16:9.
  // Row-major, column-vector: clip = M * world.
  Matrix4x4 view{};
  view.m = {
      0.5625F, 0.0F, 0.0F, 0.0F,  // clip.x = 0.5625 * x
      0.0F,    0.0F, 1.0F, 0.0F,  // clip.y = z
      0.0F,    0.0F, 0.0F, 0.0F,  // clip.z (unused)
      0.0F,    1.0F, 0.0F, 0.0F,  // clip.w = y (depth)
  };

  // A point on the view axis projects to the screen centre.
  const auto centre = view.project(Vec3{0.0F, 100.0F, 0.0F}, k_width, k_height);
  expect(centre.has_value(), "on-axis point projects");
  if (centre.has_value()) {
    expect(near(centre->x, k_width * 0.5F), "on-axis x == centre");
    expect(near(centre->y, k_height * 0.5F), "on-axis y == centre");
  }

  // An offset point maps to the expected pixel.
  const auto right_up = view.project(Vec3{10.0F, 100.0F, 20.0F}, k_width, k_height);
  expect(right_up.has_value(), "offset point projects");
  if (right_up.has_value()) {
    expect(near(right_up->x, 845.0F), "offset x");
    expect(near(right_up->y, 360.0F), "offset y");
  }

  // Points at or behind the camera are rejected.
  expect(!view.project(Vec3{0.0F, -1.0F, 0.0F}, k_width, k_height).has_value(),
         "behind-camera point rejected");
  expect(!view.project(Vec3{0.0F, 0.0F, 0.0F}, k_width, k_height).has_value(),
         "zero-depth point rejected");

  if (failures == 0) {
    std::printf("game-matrix: ok\n");
    return 0;
  }
  return 1;
}
