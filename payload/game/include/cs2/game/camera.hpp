#pragma once

// The client's third-person camera.
//
// The camera's **durable** state lives in the client's input object
// (`CCSGOInput`), not in the view struct: the view setup is rebuilt every
// frame by the view builder (`FUN_180b9c970`) and finalised by
// `FUN_180c1a710`, so a write into it is pointless. The switch the camera
// code gates on is a single byte, `CCSGOInput + 0x229`, and the engine clears
// it on death, respawn and round-state changes — so it has to be re-asserted
// every frame.
//
// **Offsets** (client.dll build 25947352, Steam; PE timestamp 0x6ad01314) were
// re-derived by the reverse-engineering pass on the live build. They are
// build-specific — `docs/re/client-dll.md` documents an older build and none of
// its addresses may be reused — so every *read* and every *write* of them is
// guarded by `client_build_verified()`.
//
// This header carries the **pure** camera model (the origin/distance/angle
// composition) so it can be regression-tested on the host without the engine;
// the engine access lives in `camera.cpp`.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cs2/game/math.hpp"

namespace cs2::game {

// --- The operator's third-person parameters --------------------------------

// The values the menu owns (`wire::Settings::movement_thirdperson*`).
struct ThirdPersonParams {
  bool enabled = false;
  float distance = 64.0F;  // units behind the player
  float angle = 0.0F;      // degrees: the camera's yaw offset around the player
  float pivot_z = 0.0F;    // units above the point the camera orbits
  float fov = 0.0F;        // 0 = keep the game's FOV
  bool right_shoulder = true;
};

// --- The camera model (pure) -----------------------------------------------
//
// The engine's own model (reverse-engineered on client.dll build 25947352):
// the camera orbits the player's **eye origin** at `distance` behind it,
// along the view forward vector. There is **no** shoulder offset and **no**
// pivot in the client's implementation (the `c_thirdpersonshoulder*` convars
// are registered but never read), and the camera yaw is smoothed toward the
// *view* yaw, so a fixed yaw offset is damped rather than held.
//
// `pivot_z` and `right_shoulder` are therefore the **payload's own**
// extension of that model: the engine cannot express them, so the payload
// composes them into the camera position it *predicts*. The game never sees
// them — see the report for what that means for the operator.

// The payload's own shoulder offset, in units. There is no engine counterpart
// (CS2's client has no shoulder camera), so this is a constant: the side
// setting chooses which shoulder, not how far.
inline constexpr float k_third_person_shoulder_offset = 16.0F;

inline constexpr float k_degrees_to_radians = 0.01745329251994329577F;

// The unit forward vector of a view angle — the engine's `AngleVectors`:
// `forward = (cos(pitch)cos(yaw), cos(pitch)sin(yaw), sin(pitch))`.
[[nodiscard]] inline auto view_forward(const Angle& view) noexcept -> Vec3 {
  const float pitch = view.pitch * k_degrees_to_radians;
  const float yaw = view.yaw * k_degrees_to_radians;
  const float cos_pitch = std::cos(pitch);
  return Vec3{cos_pitch * std::cos(yaw), cos_pitch * std::sin(yaw), std::sin(pitch)};
}

// The unit right vector of a view angle — the engine's `right(yaw)`:
// `(sin(yaw), -cos(yaw), 0)`.
[[nodiscard]] inline auto view_right(const Angle& view) noexcept -> Vec3 {
  const float yaw = view.yaw * k_degrees_to_radians;
  return Vec3{std::sin(yaw), -std::cos(yaw), 0.0F};
}

// The camera's yaw: the view yaw plus the operator's offset. The engine
// smooths the camera yaw toward the *view* yaw, so the offset survives only
// in part (the smoothing factor is `cam_ideallag`, which the
// reverse-engineering pass did not quantify).
[[nodiscard]] inline auto third_person_yaw(const Angle& view,
                                           const ThirdPersonParams& params) noexcept -> float {
  return view.yaw + params.angle;
}

// The camera's position, per the engine's model: the camera orbits the pivot
// at `distance` behind it, along the camera's forward vector, offset to the
// requested shoulder.
//
// The engine's pivot **is** the player's eye and the engine has no shoulder;
// `params.pivot_z` and `params.right_shoulder` are the payload's extension of
// the model (see the note above).
[[nodiscard]] inline auto third_person_origin(const Vec3& eye, const Angle& view,
                                              const ThirdPersonParams& params) noexcept -> Vec3 {
  const Angle camera_angles{view.pitch, third_person_yaw(view, params), 0.0F};
  const Vec3 forward = view_forward(camera_angles);
  const Vec3 right = view_right(camera_angles);
  const float shoulder = (params.right_shoulder ? 1.0F : -1.0F) * k_third_person_shoulder_offset;
  const Vec3 pivot{eye.x, eye.y, eye.z + params.pivot_z};
  return Vec3{pivot.x - forward.x * params.distance + right.x * shoulder,
              pivot.y - forward.y * params.distance + right.y * shoulder,
              pivot.z - forward.z * params.distance + right.z * shoulder};
}

// The field of view the camera would use: the operator's override when it is
// non-zero, otherwise the game's own. (The payload does not write a FOV —
// the view setup's FOV field is unconfirmed — so this only describes the
// request; see the report.)
[[nodiscard]] inline auto resolve_fov(float requested, float current) noexcept -> float {
  return requested != 0.0F ? requested : current;
}

// --- The build guard --------------------------------------------------------

// Whether the build-specific camera reads may run.
//
// Every address the camera module reads (the published view, the view setup,
// the input object's camera block) was derived on one client build. On
// another build the same address is still inside `client.dll`'s `.data`, so
// the read **succeeds** — with floats that are indistinguishable from real
// ones. The ESP's capsule silhouettes are then drawn from a garbage camera
// origin: wrong silhouettes, no crash, silently wrong.
//
// The guard is therefore not "is the address readable" but "is this the build
// the offsets came from".
[[nodiscard]] constexpr auto camera_reads_allowed(bool build_verified) noexcept -> bool {
  return build_verified;
}

// --- The engine's camera state ---------------------------------------------

// The camera the engine rendered the frame with: the origin and the angles of
// the published per-slot view state.
struct CameraState {
  Vec3 origin{};
  Angle angles{};
  bool valid = false;
};

// The camera block of the input object (slot 0).
struct ThirdPersonState {
  bool switch_on = false;  // +0x229: the third-person switch
  float pitch = 0.0F;      // +0x230: the camera pitch
  float yaw = 0.0F;        // +0x234: the camera yaw
  float distance = 0.0F;   // +0x238: the camera distance
  int mode = 0;            // +0x6a8: the camera mode (0 = third person)
  bool valid = false;
};

// Whether the third-person switch has to be re-armed.
//
// The engine clears the switch on death, respawn and round-state changes, so
// the camera has to be re-asserted. When the state cannot be read — an
// unverified build, or a read that failed — the switch is **unknown**, and an
// unknown switch is re-armed rather than trusted: the switch's address is
// build-specific, so a byte read from it on another build is arbitrary, and
// trusting it would drop the camera out of third person on the next
// respawn — exactly the failure the feature exists to prevent.
[[nodiscard]] constexpr auto third_person_needs_rearm(const ThirdPersonState& state) noexcept
    -> bool {
  return !state.valid || !state.switch_on;
}

// --- The engine access ------------------------------------------------------

// The engine access the camera's build-specific reads need.
//
// The production build binds it to the live `client.dll`; the host test binds
// it to a fake, so the build guard can be proven without a game (the fake
// answers every read with garbage, exactly as a different build's `.data`
// does).
struct CameraEngine {
  // The client's image base (0 when the client is not loaded).
  std::uintptr_t client_base = 0;
  // True when the running client is the build the offsets were verified on.
  bool build_verified = false;
  // The input object's address (0 when it is unknown).
  std::uintptr_t input_object = 0;
  // Read `count` bytes at `address`; false when the address is not readable.
  using ReadBytes = bool (*)(void* context, std::uintptr_t address, void* out, std::size_t count);
  ReadBytes read_bytes = nullptr;
  void* read_context = nullptr;
};

// The client's input object (`CCSGOInput`), or 0 when the client is not up.
[[nodiscard]] auto input_object() noexcept -> std::uintptr_t;

// True when the running client.dll is the build the offsets were verified on.
// Every read and every write of a build-specific address is gated on this: a
// stale offset would read or write the wrong field of the wrong object.
[[nodiscard]] auto client_build_verified() noexcept -> bool;

// The client.dll PE timestamp (the build guard's raw value).
[[nodiscard]] auto client_build_timestamp() noexcept -> std::uint32_t;

// The live client's engine access (the payload's entry point).
[[nodiscard]] auto camera_engine() noexcept -> CameraEngine;

// The published per-slot view state (`client + 0x256af48 + slot*0xc`): the
// origin (12 bytes) then the angles (12 bytes), written by the view-setup
// finaliser every frame. This is the camera's **real** position.
//
// Refused unless the client build is the verified one: on another build the
// address is still inside `.data`, so the read would succeed with garbage.
[[nodiscard]] auto read_published_view(const CameraEngine& engine, int slot) noexcept
    -> CameraState;

// The live client's published view state.
[[nodiscard]] auto read_published_view(int slot) noexcept -> CameraState;

// The view setup's own origin (`CViewRender + 0x10 + slot*0x990 + 0x4a0`) —
// the fallback when the published state is not readable. Refused unless the
// client build is the verified one.
[[nodiscard]] auto read_view_setup_origin(const CameraEngine& engine, int slot) noexcept -> Vec3;

// The live client's view setup origin.
[[nodiscard]] auto read_view_setup_origin(int slot) noexcept -> Vec3;

// The view setup's two **FOV candidates**: `+0x4cc` (the builder writes
// `1.0f` there) and `+0x4d0` (the value `CViewRender`'s vfunc `+0x78`
// returns). Which one is the field of view is **unconfirmed** — the FOV
// convar is engine-side, so `cl_fov` is not in the client and the field could
// not be pinned by name. They are exposed read-only, so a live run can settle
// it; the payload never writes either of them.
struct FovCandidates {
  float scale = 0.0F;      // +0x4cc
  float candidate = 0.0F;  // +0x4d0
  bool valid = false;
};

// The view setup's FOV candidates. Refused unless the client build is the
// verified one.
[[nodiscard]] auto read_view_setup_fov(const CameraEngine& engine, int slot) noexcept
    -> FovCandidates;

// The live client's view setup FOV candidates.
[[nodiscard]] auto read_view_setup_fov(int slot) noexcept -> FovCandidates;

// The camera's origin for the **rendering** maths: the published state, then
// the view setup, then `fallback` (the caller's own default). On an
// unverified build the build-specific reads are refused, so the fallback is
// what the caller gets — never a garbage origin.
[[nodiscard]] auto camera_origin(const CameraEngine& engine, int slot,
                                 const Vec3& fallback) noexcept -> Vec3;

// The live client's camera origin.
[[nodiscard]] auto camera_origin(int slot, const Vec3& fallback) noexcept -> Vec3;

// The camera block of the input object, read. Refused unless the client
// build is the verified one.
[[nodiscard]] auto read_third_person_state(const CameraEngine& engine) noexcept -> ThirdPersonState;

// The live client's camera block.
[[nodiscard]] auto read_third_person_state() noexcept -> ThirdPersonState;

// The engine's third-person switch (`CCSGOInput + 0x229`).
[[nodiscard]] auto read_third_person_switch() noexcept -> bool;

// The direct write (route B): the switch, the camera mode, the distance and
// the camera's angles. Refused unless the client build is the verified one.
[[nodiscard]] auto write_third_person_state(const ThirdPersonParams& params, float pitch,
                                            float yaw) noexcept -> bool;

// Clear the third-person switch (the restore path when the feature is turned
// off). Refused unless the client build is the verified one.
[[nodiscard]] auto clear_third_person_switch() noexcept -> bool;

// The engine's own command buffer (`Cbuf_AddText`): the console route. The
// command is queued and runs on the game's thread when the buffer is drained,
// so this is the race-free way to drive the camera from another thread.
[[nodiscard]] auto exec_engine_command(std::string_view line) noexcept -> bool;

// The `cam_idealdist` convar's value — the distance the engine's camera lerps
// toward (the engine's own distance knob).
[[nodiscard]] auto read_ideal_distance() noexcept -> float;

// The client's per-frame camera update (`FUN_180b62a20`): the function that
// reads the switch, smooths the camera angles and the distance, and computes
// the camera's position. It is the game thread's own camera step, and the
// place the payload's per-frame write belongs (the write must land *before*
// the camera is rebuilt for the frame).
[[nodiscard]] auto camera_update_address() noexcept -> std::uintptr_t;

}  // namespace cs2::game
