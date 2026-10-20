#include "cs2/features/thirdperson.hpp"

#include <cstring>
#include <string>

#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {
namespace {

// The active instance: the inline hook's detour has no context parameter (the
// accepted exception to "no global mutable state": a C-ABI entry point, see
// `game_present_hook.cpp`).
ThirdPerson* g_active = nullptr;

// The client's per-frame camera update (`FUN_180b62a20`). Its return value
// is unknown (the reverse-engineering pass did not pin it), so the detour
// forwards whatever the original leaves in the return register.
using CameraUpdateFn = std::uint64_t(__fastcall*)(void* input, int slot);

// The trampoline: the function the detour replaced. It is captured at
// install time, so the detour always calls the game's own camera update —
// even when the feature is uninstalled or the active pointer is gone.
// Skipping it would skip the game's camera update entirely.
CameraUpdateFn g_original = nullptr;

std::uint64_t __fastcall camera_update_detour(void* input, int slot) {
  ThirdPerson* active = g_active;
  if (active != nullptr) {
    active->on_camera_update(input, slot);
  }
  if (g_original != nullptr) {
    return g_original(input, slot);
  }
  return 0;
}

// How often the console route may re-arm the camera, in milliseconds.
//
// The engine's own change callback resets the camera's distance field to the
// engine's default every time it fires, so a re-arm per frame would fight
// the operator's distance setting. A second is short enough that the camera
// is back in third person before the operator notices it dropped out.
constexpr std::uint64_t k_rearm_interval_ms = 1000;

// The view angles, packed into one atomic so the pitch/yaw pair is consistent
// for the game thread.
[[nodiscard]] auto pack_angles(const game::Angle& angles) noexcept -> std::uint64_t {
  std::uint32_t pitch = 0;
  std::uint32_t yaw = 0;
  std::memcpy(&pitch, &angles.pitch, sizeof(pitch));
  std::memcpy(&yaw, &angles.yaw, sizeof(yaw));
  return (static_cast<std::uint64_t>(pitch) << 32) | static_cast<std::uint64_t>(yaw);
}

[[nodiscard]] auto unpack_angles(std::uint64_t packed) noexcept -> game::Angle {
  game::Angle angles;
  const auto pitch = static_cast<std::uint32_t>(packed >> 32);
  const auto yaw = static_cast<std::uint32_t>(packed & 0xffffffffu);
  std::memcpy(&angles.pitch, &pitch, sizeof(angles.pitch));
  std::memcpy(&angles.yaw, &yaw, sizeof(angles.yaw));
  return angles;
}

// The monotonic clock, in milliseconds.
[[nodiscard]] auto now_ms() noexcept -> std::uint64_t {
#if defined(_WIN32)
  return GetTickCount64();
#else
  return 0;
#endif
}

}  // namespace

ThirdPerson::~ThirdPerson() {
  if (hook_.installed()) {
    hook_.uninstall();
    // The trampoline belongs to the hook: it is invalid once the hook is
    // gone.
    g_original = nullptr;
  }
  if (g_active == this) {
    g_active = nullptr;
  }
}

void ThirdPerson::configure(const game::ThirdPersonParams& params) noexcept {
  enabled_.store(params.enabled);
  distance_.store(params.distance);
  angle_.store(params.angle);
  pivot_z_.store(params.pivot_z);
  fov_.store(params.fov);
  right_shoulder_.store(params.right_shoulder);
}

auto ThirdPerson::params() const noexcept -> game::ThirdPersonParams {
  return current();
}

auto ThirdPerson::current() const noexcept -> game::ThirdPersonParams {
  game::ThirdPersonParams params;
  params.enabled = enabled_.load(std::memory_order_relaxed);
  params.distance = distance_.load(std::memory_order_relaxed);
  params.angle = angle_.load(std::memory_order_relaxed);
  params.pivot_z = pivot_z_.load(std::memory_order_relaxed);
  params.fov = fov_.load(std::memory_order_relaxed);
  params.right_shoulder = right_shoulder_.load(std::memory_order_relaxed);
  return params;
}

bool ThirdPerson::install() noexcept {
  if (hook_.installed()) {
    return true;
  }
  if (install_failed_.load()) {
    return false;  // the attempt already failed; do not retry it every frame
  }
  // The camera block's offsets are build-specific, and so is the address of
  // the camera update itself: the hook is only installed on the build the
  // reverse-engineering pass verified. The console route still works on any
  // build.
  if (!game::client_build_verified()) {
    return false;
  }
  const std::uintptr_t target = game::camera_update_address();
  if (target == 0) {
    return false;
  }
  g_active = this;
  const auto error = hook_.install(reinterpret_cast<void*>(target),
                                   reinterpret_cast<void*>(&camera_update_detour));
  if (error.has_value()) {
    g_active = nullptr;
    install_failed_.store(true);
    cs2::game::append_log("[cs2-features] third person: the camera hook failed: " +
                          std::string(hooks::to_string(*error)));
    return false;
  }
  // The trampoline is captured now: the detour calls it on every frame, and
  // it must be the game's own camera update even if the feature is torn
  // down later.
  g_original = reinterpret_cast<CameraUpdateFn>(hook_.original());
  cs2::game::append_log("[cs2-features] third person: the camera update is hooked");
  return true;
}

void ThirdPerson::update(const game::Angle& view_angles, const game::Vec3& eye_origin) noexcept {
  view_angles_.store(pack_angles(view_angles), std::memory_order_relaxed);
  if (!hook_.installed()) {
    (void)install();  // the client may not have been up when the payload booted
  }
  const game::ThirdPersonParams params = current();
  if (!params.enabled) {
    if (active_.exchange(false)) {
      disable();
    }
    return;
  }
  if (!active_.exchange(true)) {
    enable(params);
  }
  // The distance: the engine's own knob. The camera's distance is smoothed
  // toward `cam_idealdist`, so the knob — not the payload's write — is what
  // actually holds the distance.
  if (params.distance != last_distance_) {
    last_distance_ = params.distance;
    push_ideal_distance(params.distance);
  }
  // The engine clears the switch on death, respawn and round-state changes.
  // With the game-thread hook the switch is re-asserted every frame; without
  // it, the console route re-arms it.
  //
  // The state is **unknown** when the client build is not the one the
  // offsets were verified on: the switch's address is build-specific, so the
  // byte read from it on another build is arbitrary. An unknown switch is
  // re-armed, never trusted — trusting it would drop the camera out of third
  // person on the next respawn, exactly the failure the feature exists to
  // prevent.
  if (!hook_.installed()) {
    const std::uint64_t now = now_ms();
    const bool due =
        last_rearm_ms_ == 0 || (now != 0 && now - last_rearm_ms_ >= k_rearm_interval_ms);
    if (due && game::third_person_needs_rearm(game::read_third_person_state())) {
      last_rearm_ms_ = now != 0 ? now : 1;
      // The convar's change callback only fires on a *value change*, so the
      // command toggles the value and puts it back: the callback then
      // re-initialises the camera state. Two commands, not one
      // `;`-separated line: the engine's command buffer takes one command per
      // entry, and nothing else in the tree assumes a separator.
      push_command("thirdperson 0");
      push_command("thirdperson 1");
      // The callback resets the camera's distance field to the engine's own
      // default, so the distance knob is re-asserted right after: the engine
      // then eases the camera back to the operator's distance.
      push_ideal_distance(params.distance);
    }
  }
  report(view_angles, eye_origin, params);
}

// A periodic state line, so the camera is observable without the overlay: the
// engine's own camera state, the engine's distance knob, the camera the
// engine published and the payload's model of it. The difference between the
// last two is what the engine does *not* implement (the pivot, the shoulder
// and the part of the yaw offset the smoothing damps away).
void ThirdPerson::report(const game::Angle& view_angles, const game::Vec3& eye_origin,
                         const game::ThirdPersonParams& params) noexcept {
  const std::uint64_t now = now_ms();
  if (now == 0 || (last_log_ms_ != 0 && now - last_log_ms_ < 2000)) {
    return;
  }
  last_log_ms_ = now;
  const game::ThirdPersonState state = game::read_third_person_state();
  const game::CameraState published = game::read_published_view(0);
  const game::FovCandidates fov_candidates = game::read_view_setup_fov(0);
  const game::Vec3 model = game::third_person_origin(eye_origin, view_angles, params);
  const float ideal = game::read_ideal_distance();
  const float fov = game::resolve_fov(params.fov, 0.0F);
  std::string line = "[cs2-features] third person: switch=";
  line += state.switch_on ? "1" : "0";
  line += " mode=" + std::to_string(state.mode);
  line += " dist=" + std::to_string(state.distance);
  line += " ideal=" + std::to_string(ideal);
  line += " yaw=" + std::to_string(state.yaw);
  line += " pitch=" + std::to_string(state.pitch);
  line += " cam=(" + std::to_string(published.origin.x) + "," + std::to_string(published.origin.y) +
          "," + std::to_string(published.origin.z) + ")";
  line += " model=(" + std::to_string(model.x) + "," + std::to_string(model.y) + "," +
          std::to_string(model.z) + ")";
  line += " fov=" + std::to_string(fov);
  line += " fovcand=" + std::to_string(fov_candidates.scale) + "/" +
          std::to_string(fov_candidates.candidate);
  line += " hook=" + std::string(hook_.installed() ? "1" : "0");
  line += " writes=" + std::to_string(writes_.load());
  line += " cmds=" + std::to_string(commands_.load());
  cs2::game::append_log(line);
}

void ThirdPerson::on_camera_update(void* input, int slot) noexcept {
  const game::ThirdPersonParams params = current();
  if (!params.enabled || slot != 0) {
    return;
  }
  // The identity check: the hook must be the camera update of *our* input
  // object, or the offsets would be applied to the wrong object.
  if (input != reinterpret_cast<void*>(game::input_object())) {
    return;
  }
  const game::Angle view = unpack_angles(view_angles_.load(std::memory_order_relaxed));
  const float yaw = game::third_person_yaw(view, params);
  if (game::write_third_person_state(params, view.pitch, yaw)) {
    writes_.fetch_add(1);
  }
}

void ThirdPerson::enable(const game::ThirdPersonParams& params) noexcept {
  // The engine's own change callback initialises the camera state: the
  // switch, the mode (third person) and the current view angles.
  push_command("thirdperson 1");
  // The distance knob: the value the engine had before the payload touched
  // it, so the feature can restore it when it is turned off.
  saved_ideal_distance_ = game::read_ideal_distance();
  has_saved_ideal_distance_ = true;
  push_ideal_distance(params.distance);
  last_distance_ = params.distance;
  cs2::game::append_log("[cs2-features] third person: enabled (distance " +
                        std::to_string(params.distance) + ", angle " +
                        std::to_string(params.angle) + ", fov " + std::to_string(params.fov) +
                        " — the angle is damped by the engine, the fov is not applied)");
}

void ThirdPerson::disable() noexcept {
  // Restore what the payload changed. The camera's angles and its distance in
  // the input object are rewritten by the engine every frame, so they need no
  // restore; the switch and the distance knob do.
  if (has_saved_ideal_distance_) {
    push_ideal_distance(saved_ideal_distance_);
    has_saved_ideal_distance_ = false;
  }
  // The engine's own way back to the first-person camera.
  push_command("firstperson 1");
  (void)game::clear_third_person_switch();
  cs2::game::append_log("[cs2-features] third person: disabled");
}

void ThirdPerson::push_command(const std::string& line) noexcept {
  if (game::exec_engine_command(line)) {
    commands_.fetch_add(1);
  }
}

void ThirdPerson::push_ideal_distance(float distance) noexcept {
  push_command("cam_idealdist " + std::to_string(distance));
}

}  // namespace cs2::features
