// Unit tests for the third-person camera model and for the build guard on
// the camera's build-specific reads.
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
//
// The second half of the test drives the build-specific reads against a
// **fake** client: every read succeeds and answers with garbage, exactly as
// a different build's `.data` does. The build guard must refuse the read
// *before* the engine is touched — otherwise the ESP's capsule silhouettes
// are drawn from a garbage camera origin (wrong silhouettes, no crash,
// silently wrong).

#include <cmath>
#include <cstddef>
#include <cstdint>
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

// --- The fake client --------------------------------------------------------

// The fake client: every read succeeds and answers with a deterministic
// garbage pattern. That is what a *different* build looks like from the
// camera's point of view: the address is still inside `client.dll`'s
// `.data`, so the read succeeds and the floats are nonsense.
struct FakeClient {
  // The image's base (the reads are addressed relative to it).
  std::uintptr_t base = 0x7ff000000000ULL;
  // The input object's address.
  std::uintptr_t input = 0x7ff000100000ULL;
  // How many reads were served: the guard must serve **none** on an
  // unverified build.
  int reads = 0;
};

auto fake_read(void* context, std::uintptr_t address, void* out, std::size_t count) -> bool {
  auto* client = static_cast<FakeClient*>(context);
  ++client->reads;
  auto* bytes = static_cast<unsigned char*>(out);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<unsigned char>((address >> 3) + i * 31U);
  }
  return true;
}

// The engine access bound to the fake client.
auto fake_engine(FakeClient& client, bool build_verified) -> cs2::game::CameraEngine {
  cs2::game::CameraEngine engine;
  engine.client_base = client.base;
  engine.build_verified = build_verified;
  engine.input_object = client.input;
  engine.read_bytes = &fake_read;
  engine.read_context = &client;
  return engine;
}

}  // namespace

auto main() -> int {
  using cs2::game::Angle;
  using cs2::game::ThirdPersonParams;
  using cs2::game::Vec3;

  // --- The forward vector (the engine's `AngleVectors`) --------------------
  {
    const auto forward = cs2::game::view_forward(Angle{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F});
    expect(near(forward, Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F}), "yaw 0 looks along +X");
    const auto yaw_90 = cs2::game::view_forward(Angle{.pitch = 0.0F, .yaw = 90.0F, .roll = 0.0F});
    expect(near(yaw_90, Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F}), "yaw 90 looks along +Y");
    const auto pitch_90 = cs2::game::view_forward(Angle{.pitch = 90.0F, .yaw = 0.0F, .roll = 0.0F});
    expect(near(pitch_90, Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F}), "pitch 90 looks along +Z");
  }

  // --- The camera sits behind the player ------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 64.0F;
    params.pivot_z = 0.0F;
    const Vec3 eye{.x = 100.0F, .y = 200.0F, .z = 50.0F};
    const Angle view{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F};
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
    const Vec3 eye{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const Angle view{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F};
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
    const Vec3 eye{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const Angle view{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F};
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
    const Vec3 eye{.x = 0.0F, .y = 0.0F, .z = 50.0F};
    const Angle view{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F};
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    expect(near(origin.z, 74.0F), "the pivot lifts the camera by pivot_z");
  }

  // --- The distance scales the offset ---------------------------------------
  {
    ThirdPersonParams params;
    params.enabled = true;
    params.distance = 0.0F;
    const Vec3 eye{.x = 10.0F, .y = 20.0F, .z = 30.0F};
    const Angle view{.pitch = 0.0F, .yaw = 0.0F, .roll = 0.0F};
    const auto origin = cs2::game::third_person_origin(eye, view, params);
    expect(near(origin.x, 10.0F), "a zero distance keeps the camera on the pivot (x)");
    expect(near(origin.z, 30.0F), "a zero distance keeps the camera on the pivot (z)");
  }

  // --- The FOV override applies only when it is non-zero --------------------
  {
    expect(near(cs2::game::resolve_fov(0.0F, 90.0F), 90.0F), "a zero FOV keeps the game's FOV");
    expect(near(cs2::game::resolve_fov(105.0F, 90.0F), 105.0F), "a non-zero FOV overrides it");
  }

  // --- The build guard ------------------------------------------------------
  {
    expect(!cs2::game::camera_reads_allowed(false),
           "the build-specific reads are refused on an unverified build");
    expect(cs2::game::camera_reads_allowed(true),
           "the build-specific reads run on the verified build");
  }

  // --- The reads on the verified build read whatever is there ---------------
  //
  // The fake is the "different build": every read succeeds and answers with
  // garbage. This proves the fake is wired, so the checks below are not
  // vacuous.
  {
    FakeClient client;
    const auto engine = fake_engine(client, /*build_verified=*/true);
    const auto published = cs2::game::read_published_view(engine, 0);
    expect(published.valid, "the verified build reads the published view");
    expect(client.reads > 0, "the verified build touches the engine");
    const auto state = cs2::game::read_third_person_state(engine);
    expect(state.valid, "the verified build reads the camera block");
  }

  // --- The reads on an unverified build are refused -------------------------
  {
    FakeClient client;
    const auto engine = fake_engine(client, /*build_verified=*/false);

    const auto published = cs2::game::read_published_view(engine, 0);
    expect(!published.valid, "the published view is refused on an unverified build");

    const auto setup = cs2::game::read_view_setup_origin(engine, 0);
    expect(near(setup, Vec3{.x = 0.0F, .y = 0.0F, .z = 0.0F}),
           "the view setup origin is refused on an unverified build");

    const auto fov = cs2::game::read_view_setup_fov(engine, 0);
    expect(!fov.valid, "the view setup's FOV is refused on an unverified build");

    const auto state = cs2::game::read_third_person_state(engine);
    expect(!state.valid, "the camera block is refused on an unverified build");
    expect(!state.switch_on, "the refused camera block reports no switch");

    // The fallback the caller owns is what the renderer gets — never a
    // garbage camera origin.
    const Vec3 fallback{.x = 11.0F, .y = 22.0F, .z = 33.0F};
    const auto origin = cs2::game::camera_origin(engine, 0, fallback);
    expect(near(origin, fallback), "the camera origin falls back on an unverified build");

    // The engine was never touched: the guard refuses *before* the address
    // is read, so there is no garbage to mistake for a camera.
    expect(client.reads == 0, "the refused reads never touch the engine");
  }

  // --- The re-arm trusts an unknown switch never ---------------------------
  {
    cs2::game::ThirdPersonState unknown;
    unknown.switch_on = true;  // an arbitrary byte from an unverified build
    expect(cs2::game::third_person_needs_rearm(unknown),
           "an unknown switch is re-armed, never trusted");
    expect(cs2::game::third_person_needs_rearm(cs2::game::ThirdPersonState{}),
           "an unreadable camera block is re-armed");

    cs2::game::ThirdPersonState off;
    off.valid = true;
    off.switch_on = false;
    expect(cs2::game::third_person_needs_rearm(off), "a switch that is off is re-armed");

    cs2::game::ThirdPersonState on;
    on.valid = true;
    on.switch_on = true;
    expect(!cs2::game::third_person_needs_rearm(on), "a switch that is on is left alone");
  }

  if (failures == 0) {
    std::printf("game-camera: ok\n");
    return 0;
  }
  return 1;
}
