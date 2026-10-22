#pragma once

// Small value types shared by the introspection and aim layers.

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

// A world-space capsule hitbox (a real studio hitbox): the segment `a`-`b`
// thickened by `radius`, tagged with its hitgroup (`docs/re/aim.md`). Used to
// hand hitboxes from the payload to the overlay's visualiser without a
// features->aim dependency.
struct HitboxCapsule {
  Vec3 a{};
  Vec3 b{};
  float radius = 0.0F;
  int group = 0;
};

[[nodiscard]] inline Vec2 operator+(const Vec2& a, const Vec2& b) noexcept {
  return Vec2{a.x + b.x, a.y + b.y};
}

[[nodiscard]] inline Vec2 operator-(const Vec2& a, const Vec2& b) noexcept {
  return Vec2{a.x - b.x, a.y - b.y};
}

[[nodiscard]] inline Vec2 operator*(const Vec2& a, float s) noexcept {
  return Vec2{a.x * s, a.y * s};
}

[[nodiscard]] inline float dot(const Vec2& a, const Vec2& b) noexcept {
  return a.x * b.x + a.y * b.y;
}

// The z component of the 3-D cross product of two planar vectors.
[[nodiscard]] inline float cross(const Vec2& a, const Vec2& b) noexcept {
  return a.x * b.y - a.y * b.x;
}

[[nodiscard]] inline float length(const Vec2& v) noexcept {
  return std::sqrt(v.x * v.x + v.y * v.y);
}

[[nodiscard]] inline Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vec3 operator-(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline Vec3 operator*(const Vec3& a, float s) noexcept {
  return Vec3{a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] inline float dot(const Vec3& a, const Vec3& b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline float length(const Vec3& v) noexcept {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] inline Vec3 normalized(const Vec3& v) noexcept {
  const float len = length(v);
  if (len <= 0.0F) {
    return Vec3{};
  }
  return Vec3{v.x / len, v.y / len, v.z / len};
}

[[nodiscard]] inline float distance(const Vec3& a, const Vec3& b) noexcept {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace cs2::game
