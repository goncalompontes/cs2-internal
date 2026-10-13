#pragma once

// World-to-screen projection.
//
// Source 2 stores the per-slot P x V matrix row-major and multiplies as a
// column vector (`clip = M * world`, i.e. `clip.i = sum_j m[i*4+j] * world.j`).
// Verified live: `matrix@view_matrix == matrix@projection * matrix@view` under
// this convention, the camera eye maps to clip ~= (0, 0, 0) and a point `d`
// units ahead maps to `clip.w ~= d`.

#include <array>
#include <optional>

#include "cs2/game/math.hpp"

namespace cs2::game {

struct Matrix4x4 {
  std::array<float, 16> m{};

  // Project a world point to screen pixels (origin top-left). Returns nullopt
  // when the point is behind the camera (clip w <= 0).
  [[nodiscard]] std::optional<Vec2> project(const Vec3& world, float width,
                                            float height) const noexcept {
    const float clip_x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
    const float clip_y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
    const float clip_w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
    if (clip_w <= 0.001F) {
      return std::nullopt;
    }
    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    return Vec2{(ndc_x * 0.5F + 0.5F) * width, (0.5F - ndc_y * 0.5F) * height};
  }
};

}  // namespace cs2::game
