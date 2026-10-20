#pragma once

// The third-person camera.
//
// The feature drives the **engine's own** third-person camera — the one the
// `thirdperson` convar switches on — with the parameters the operator owns.
//
// **Two write routes, both used:**
//
//   * the **console** (the engine's own command buffer, `Cbuf_AddText`):
//     `thirdperson 1` runs the client's change callback, which initialises
//     the camera state correctly (the switch, the mode, the current view
//     angles), and `cam_idealdist <distance>` is the engine's own distance
//     knob — the camera's distance is smoothed toward it. The command is
//     queued and runs on the game's thread, so this route never writes the
//     engine's memory from another thread.
//   * the **direct write** into the input object's camera block, from the
//     client's own per-frame camera update (`FUN_180b62a20`). The engine
//     clears the switch on death, respawn and round-state changes, so it has
//     to be re-asserted every frame; the write lands *before* the camera is
//     rebuilt for the frame, on the game's own thread.
//
// The view struct is never written: it is rebuilt every frame by the view
// builder.
//
// **What the engine supports** (reverse-engineered on client.dll build
// 25947352):
//
//   * the switch (`CCSGOInput + 0x229`) — supported, re-asserted every frame;
//   * the distance (`cam_idealdist`) — supported;
//   * the camera yaw — supported only as a yaw offset that the engine's
//     smoothing damps (the camera yaw is smoothed toward the *view* yaw).
//
// **What the engine does not support**:
//
//   * a shoulder/side offset — the `c_thirdpersonshoulder*` convars are
//     registered but never read, so `right_shoulder` only shapes the
//     payload's own camera model;
//   * a pivot — the camera orbits the player's eye, so `pivot_z` only shapes
//     the payload's own camera model;
//   * a field of view — the view setup's FOV field is unconfirmed, so
//     `fov` is recorded and reported but never written.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cs2/game/camera.hpp"
#include "cs2/game/math.hpp"
#include "cs2/hooks/inline.hpp"

namespace cs2::features {

class ThirdPerson {
 public:
  ThirdPerson() = default;
  ~ThirdPerson();
  ThirdPerson(const ThirdPerson&) = delete;
  ThirdPerson& operator=(const ThirdPerson&) = delete;

  // The operator's parameters (the menu's shared settings block).
  void configure(const game::ThirdPersonParams& params) noexcept;
  [[nodiscard]] auto params() const noexcept -> game::ThirdPersonParams;

  // Install the game-thread hook (the client's per-frame camera update).
  // Idempotent; safe to call on every frame. Fails when the running client
  // build is not the one the offsets were verified on.
  [[nodiscard]] bool install() noexcept;

  // One step of the feature (the payload's own thread, ~30 Hz): the console
  // route, the distance knob and the re-arm after a respawn. `view_angles` is
  // the local player's view angles — the base the camera's yaw offset is
  // applied to — and `eye_origin` the local player's eye, the point the
  // engine's camera orbits.
  void update(const game::Angle& view_angles, const game::Vec3& eye_origin) noexcept;

  // The hook's callback: the game's own thread, inside the client's camera
  // update, before the camera is rebuilt for the frame.
  void on_camera_update(void* input, int slot) noexcept;

  // Diagnostics.
  [[nodiscard]] auto hook_installed() const noexcept {
    return hook_.installed();
  }
  [[nodiscard]] auto original() const noexcept {
    return hook_.original();
  }
  [[nodiscard]] auto build_verified() const noexcept {
    return game::client_build_verified();
  }
  [[nodiscard]] auto writes() const noexcept {
    return writes_.load();
  }
  [[nodiscard]] auto commands() const noexcept {
    return commands_.load();
  }
  [[nodiscard]] auto active() const noexcept {
    return active_.load();
  }

 private:
  // The parameters as the game thread sees them.
  [[nodiscard]] auto current() const noexcept -> game::ThirdPersonParams;
  // The console route: the enable transition, the distance knob and the
  // re-arm.
  void enable(const game::ThirdPersonParams& params) noexcept;
  void disable() noexcept;
  void push_command(const std::string& line) noexcept;
  void push_ideal_distance(float distance) noexcept;
  // The periodic state line (the engine's camera state, the camera it
  // published, and the payload's model of it).
  void report(const game::Angle& view_angles, const game::Vec3& eye_origin,
              const game::ThirdPersonParams& params) noexcept;

  hooks::InlineHook hook_;
  std::atomic<bool> enabled_{false};
  std::atomic<bool> right_shoulder_{true};
  std::atomic<float> distance_{64.0F};
  std::atomic<float> angle_{0.0F};
  std::atomic<float> pivot_z_{0.0F};
  std::atomic<float> fov_{0.0F};
  // The view angles the camera write uses, packed so the pair is consistent.
  std::atomic<std::uint64_t> view_angles_{0};
  std::atomic<bool> active_{false};
  std::atomic<bool> install_failed_{false};
  std::atomic<std::uint64_t> writes_{0};
  std::atomic<std::uint64_t> commands_{0};
  // The distance the engine's `cam_idealdist` had before the payload changed
  // it, so the feature can restore it. The payload's own thread only.
  float saved_ideal_distance_ = 0.0F;
  bool has_saved_ideal_distance_ = false;
  float last_distance_ = 0.0F;
  std::uint64_t last_log_ms_ = 0;
};

}  // namespace cs2::features
