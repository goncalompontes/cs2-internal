#pragma once

// Small value types shared by the introspection layer.

#include <cmath>

namespace cs2::game {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

// A Source 2 `QAngle` (degrees).
struct Angle {
  float pitch = 0.0F;
  float yaw = 0.0F;
  float roll = 0.0F;
};

[[nodiscard]] inline Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline float distance(const Vec3& a, const Vec3& b) noexcept {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace cs2::game
