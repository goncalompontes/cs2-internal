// Unit tests for the third-person camera model.
//
// The model is the engine's own (reverse-engineered on client.dll build
// 25947352): the camera orbits the player's **eye origin** at `distance`
// behind it, along the view forward vector. The payload's own extension —
// the pivot and the shoulder — has no engine counterpart (CS2's client has no
// shoulder camera and no pivot), so the test pins the *composition* the
// payload uses for its own camera model.
//
// A sign error in the forward vector or in the shoulder offset is invisible
// in a screenshot but moves the camera to the wrong side of the player, so
// the model is pinned here.

#include <cmath>
#include <cstdio>

#include "cs2/game/camera.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

auto near(const cs2::game::Vec3& a, const cs2::game::Vec3& b, float tolerance = 1e-3F) -> bool {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

}  // namespace

auto main() -> int {
  using cs2::game::Angle;
  using cs2::game::ThirdPersonParams;
  using cs2::game::Vec3;

  // --- The forward vector (the engine's `AngleVectors`) --------------------
  {
    const auto forward = cs2::game::view_forward(Angle{0.0F, 0.0F, 0.0F});
    expect(near(forward, Vec3{1.0F, 0.0F, 0.0F}), "yaw 0 looks along +X");
    const auto yaw_90 = cs2::game::view_forward(Angle{0.0F, 90.0F, 0.0F});
    expect(near(yaw_90, Vec3{0.0F, 1.0F, 0.0F}), "yaw 90 looks along +Y");
    const auto pitch_90 = cs2::game::view_forward(Angle{90.0F, 0.0F, 0.0F});
    expect(near(pitch_90, Vec3{0.0F, 0.0F, 1.0F}), "pitch 90 looks along +Z");
  }

  // --- The camera sits behind the player ------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 64.0F;
    params.pivot_z = 0.0F;
    const Vec3 eye{100.0F, 200.0F, 50.0F};
    const Angle view{0.0F, 0.0F, 0.0F};
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    // The player looks along +X, so the camera is 64 units behind it.
    expect(near(origin.x, 36.0F), "the camera is behind the player (x)");
    expect(near(origin.y, 200.0F - cs2::game::k_third_person_shoulder_offset),
           "the right shoulder offsets the camera");
    expect(near(origin.z, 50.0F), "the camera keeps the eye's height");
  }

  // --- The angle is a yaw offset around the player --------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 64.0F;
    params.angle = 90.0F;
    const Vec3 eye{0.0F, 0.0F, 0.0F};
    const Angle view{0.0F, 0.0F, 0.0F};
    expect(near(cs2::game::third_person_yaw(view, params), 90.0F), "the camera yaw is the offset");
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    // A 90 degree offset puts the camera 64 units along -Y (the player looks
    // along +X, the camera along +Y).
    expect(near(origin.x, cs2::game::k_third_person_shoulder_offset),
           "the 90 degree camera is offset on the shoulder axis (x)");
    expect(near(origin.y, -64.0F), "the 90 degree camera is 64 units along -Y");
  }

  // --- The shoulder side mirrors -------------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 64.0F;
    const Vec3 eye{0.0F, 0.0F, 0.0F};
    const Angle view{0.0F, 0.0F, 0.0F};
    params.right_shoulder = true;
    const auto right = cs2::game::third_person_origin(eye, view, params);
    params.right_shoulder = false;
    const auto left = cs2::game::third_person_origin(eye, view, params);
    expect(near(right.x, left.x), "the shoulder sides keep the same x");
    expect(near(right.y, -left.y), "the shoulder sides mirror about the camera axis");
    // At yaw 0 the engine's right vector is (0, -1, 0), so the right
    // shoulder sits at -Y and the left one at +Y.
    expect(near(left.y - right.y, 2.0F * cs2::game::k_third_person_shoulder_offset),
           "the shoulder offset is applied to both sides");
  }

  // --- The pivot lifts the camera ------------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 64.0F;
    params.pivot_z = 24.0F;
    const Vec3 eye{0.0F, 0.0F, 50.0F};
    const Angle view{0.0F, 0.0F, 0.0F};
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    expect(near(origin.z, 74.0F), "the pivot lifts the camera by pivot_z");
  }

  // --- The distance scales the offset ---------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 0.0F;
    const Vec3 eye{10.0F, 20.0F, 30.0F};
    const Angle view{0.0F, 0.0F, 0.0F};
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    expect(near(origin.x, 10.0F), "a zero distance keeps the camera on the pivot (x)");
    expect(near(origin.z, 30.0F), "a zero distance keeps the camera on the pivot (z)");
  }

  // --- The FOV override applies only when it is non-zero --------------------
  {
    expect(near(cs2::game::resolve_fov(0.0F, 90.0F), 90.0F), "a zero FOV keeps the game's FOV");
    expect(near(cs2::game::resolve_fov(105.0F, 90.0F), 105.0F), "a non-zero FOV overrides it");
  }

  if (failures == 0) {
    std::printf("game-camera: ok\n");
    return 0;
  }
  return 1;
}
