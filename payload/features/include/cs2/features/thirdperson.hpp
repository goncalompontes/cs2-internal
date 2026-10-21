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

#include "cs2/features/recursion_guard.hpp"
#include "cs2/game/camera.hpp"
#include "cs2/game/math.hpp"
#include "cs2/hooks/inline.hpp"

namespace cs2::features {

namespace third_person {

// Why the camera hook is installed — or why it is not.
//
// The status is the answer to "the camera did not move": it says whether the
// camera update is hooked, and when it is not, what stopped it.
enum class HookStatus : std::uint8_t {
  off,               // the feature is off
  waiting,           // the client is not up yet (no address to hook)
  unverified_build,  // the client is not the build the offsets came from
  hook_refused,      // the inline hook refused the target
  installed,         // the camera update is hooked
};

// The status, in words (the status line and the log).
[[nodiscard]] auto hook_status_text(HookStatus status) noexcept -> const char*;

}  // namespace third_person

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

  // One step of the feature (the overlay's per-frame callback, the game's
  // render thread in the in-process mode): the console route, the distance
  // knob and the re-arm after a respawn. `view_angles` is the local player's
  // view angles — the base the camera's yaw offset is applied to — and
  // `eye_origin` the local player's eye, the point the engine's camera
  // orbits.
  void update(const game::Angle& view_angles, const game::Vec3& eye_origin) noexcept;

  // The hook's callback: the game's own thread, inside the client's camera
  // update, before the camera is rebuilt for the frame.
  void on_camera_update(void* input, int slot) noexcept;

  // The re-entrancy guard around the camera hook.
  //
  // The detour calls `enter_camera_update()` before it touches the camera
  // state and `leave_camera_update()` after the trampoline has returned. The
  // guard refuses a call that re-enters **the same** thread (the trampoline
  // landing back in the detour) and never refuses another thread's call.
  //
  // The guard is **not** a `thread_local`: the payload is manually mapped and
  // the game's camera thread has no TLS block for it, so a `thread_local`
  // read on that thread faults inside the payload (see
  // `docs/re/thirdperson.md`).
  [[nodiscard]] auto enter_camera_update() noexcept -> bool;
  void leave_camera_update() noexcept;

  // Diagnostics.
  [[nodiscard]] auto hook_installed() const noexcept -> bool {
    return hook_.installed();
  }
  [[nodiscard]] auto original() const noexcept -> void* {
    return hook_.original();
  }
  [[nodiscard]] auto build_verified() const noexcept -> bool {
    return game::client_build_verified();
  }
  // The camera updates the detour saw, and the writes that landed.
  [[nodiscard]] auto updates() const noexcept -> std::uint64_t {
    return updates_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto writes() const noexcept -> std::uint64_t {
    return writes_.load();
  }
  [[nodiscard]] auto commands() const noexcept -> std::uint64_t {
    return commands_.load();
  }
  [[nodiscard]] auto active() const noexcept -> bool {
    return active_.load();
  }
  // Why the camera hook is (not) installed.
  [[nodiscard]] auto hook_status() const noexcept -> third_person::HookStatus {
    return hook_status_.load(std::memory_order_relaxed);
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
  // The per-build facts the detour needs: the client's base, the input
  // object's address and the build gate. They are resolved on the **render
  // thread** (a module lookup and a PE-header read, at most once a second)
  // and cached, so the detour — which runs on the game's camera thread, once
  // per frame — only reads them.
  void refresh_target() noexcept;
  // Record the hook's status, and say it **once** per status: the install path
  // is retried every frame and the log opens, writes and closes a file per
  // call.
  void set_hook_status(third_person::HookStatus status) noexcept;

  hooks::InlineHook hook_;
  // The re-entrancy guard of the camera hook. It is a member, not a global:
  // the detour reaches it through the active instance.
  guard::RecursionGuard camera_guard_{};
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
  // The camera updates the detour saw.
  std::atomic<std::uint64_t> updates_{0};
  // The cached per-build facts (see `refresh_target`).
  std::atomic<std::uintptr_t> client_base_{0};
  std::atomic<std::uintptr_t> input_object_{0};
  std::atomic<bool> build_verified_{false};
  // Set when the detour saw an input object that is not the cached one: the
  // module changed under us and the render thread re-resolves it.
  std::atomic<bool> input_stale_{false};
  // The hook's status, and the statuses that have already been logged.
  std::atomic<third_person::HookStatus> hook_status_{third_person::HookStatus::off};
  std::atomic<std::uint8_t> logged_statuses_{0};
  // The distance the engine's `cam_idealdist` had before the payload changed
  // it, so the feature can restore it. The render thread only.
  float saved_ideal_distance_ = 0.0F;
  bool has_saved_ideal_distance_ = false;
  float last_distance_ = 0.0F;
  std::uint64_t last_log_ms_ = 0;
  // When the per-build facts were last resolved. The install path runs on the
  // render thread and on the settings thread, so the stamp is atomic.
  std::atomic<std::uint64_t> last_refresh_ms_{0};
  // When the console route last re-armed the camera (the re-arm is
  // rate-limited; see `k_rearm_interval_ms`).
  std::uint64_t last_rearm_ms_ = 0;
};

}  // namespace cs2::features
