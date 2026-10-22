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

// The owner token: the OS thread id.
//
// The re-entrancy guard needs a value that is unique per thread, and it must
// be readable **without** the payload's TLS block: a `thread_local` is exactly
// what is broken on the game's threads (the payload is manually mapped, and
// the camera thread never had a TLS block installed for it — the crash the
// operator saw, see `docs/re/thirdperson.md`). `GetCurrentThreadId()` reads
// the thread's own block, so it is safe on any thread, the game's included.
[[nodiscard]] auto current_owner() noexcept -> guard::OwnerId {
#if defined(_WIN32)
  return static_cast<guard::OwnerId>(GetCurrentThreadId());
#else
  return 1;
#endif
}

std::uint64_t __fastcall camera_update_detour(void* input, int slot) {
  ThirdPerson* active = g_active;
  if (active == nullptr) {
    // The feature is off. The hook is uninstalled together with the active
    // pointer, so the detour cannot be reached here — but if it is, the
    // game's own camera update still has to run.
    return g_original != nullptr ? g_original(input, slot) : 0;
  }
  // The re-entrancy guard.
  //
  // The trampoline is the game's own prologue plus a jump back; if the
  // prologue were patched wrongly the trampoline would land **in this
  // detour** again, and the chain (detour -> original -> detour -> ...) would
  // recurse until the stack overflowed (0xc00000fd) and took the game with
  // it. The guard breaks the cycle: the re-entrant call returns without
  // touching the camera state, so the frame's camera update is dropped
  // rather than crashing.
  //
  // The guard is held **across the trampoline call**, because that is the
  // cycle it exists to break. It is not a `thread_local`: this detour runs on
  // the game's camera thread, which has no TLS block for the manually mapped
  // image, so a `thread_local` read here faults inside the payload.
  if (!active->enter_camera_update()) {
    // The trampoline came back here: this is the recursion, and it is broken
    // here rather than a thousand frames later.
    return 0;
  }
  active->on_camera_update(input, slot);
  const std::uint64_t result = g_original != nullptr ? g_original(input, slot) : 0;
  active->leave_camera_update();
  return result;
}

// How often the console route may re-arm the camera, in milliseconds.
//
// The engine's own change callback resets the camera's distance field to the
// engine's default every time it fires, so a re-arm per frame would fight
// the operator's distance setting. A second is short enough that the camera
// is back in third person before the operator notices it dropped out.
constexpr std::uint64_t k_rearm_interval_ms = 1000;

// How often the per-build facts (the client's base, the input object's
// address and the build gate) are resolved, in milliseconds.
//
// They are resolved on the **render thread** — the overlay's per-frame
// callback, which is the game's render thread in the in-process mode — and
// never on the game's camera thread: a module lookup and a PE-header read
// per frame are work the game does not need, and the facts cannot change
// while the client is up.
//
// The rate limit is **hard**: the resolution happens at most once a second,
// and *not* at all while the facts are already known. (An earlier version
// also refreshed whenever the target could not be resolved, which made the
// lookup run on **every** frame for as long as the client was not up — the
// one window in which the camera update does not run at all.)
constexpr std::uint64_t k_refresh_interval_ms = 1000;

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
  const auto yaw = static_cast<std::uint32_t>(packed & 0xffffffffU);
  std::memcpy(&angles.pitch, &pitch, sizeof(pitch));
  std::memcpy(&angles.yaw, &yaw, sizeof(yaw));
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

auto third_person::hook_status_text(third_person::HookStatus status) noexcept -> const char* {
  switch (status) {
    case third_person::HookStatus::off:
      return "off";
    case third_person::HookStatus::waiting:
      return "waiting-for-client";
    case third_person::HookStatus::unverified_build:
      return "unverified-build";
    case third_person::HookStatus::hook_refused:
      return "hook-refused";
    case third_person::HookStatus::installed:
      return "installed";
    default:
      return "unknown";
  }
}

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
  const bool was_enabled = enabled_.load(std::memory_order_relaxed);
  enabled_.store(params.enabled);
  distance_.store(params.distance);
  angle_.store(params.angle);
  pivot_z_.store(params.pivot_z);
  fov_.store(params.fov);
  right_shoulder_.store(params.right_shoulder);

  // The hook follows the switch: a feature that is off must not patch the
  // game's code. The camera update is in the game's per-frame path, so an
  // inline hook there is a crash risk for no benefit while the feature is off.
  // The hook is installed when the operator turns the camera on, and removed
  // when they turn it off.
  if (params.enabled) {
    if (!was_enabled) {
      // The feature is being turned **on**: the status latch is cleared, so
      // a failure of the same kind is reported again. (The latch is a
      // one-shot per status; without the reset, the operator would turn the
      // feature off and on again and the second refusal would be silent.)
      logged_statuses_.store(0, std::memory_order_relaxed);
    }
    (void)install();
  } else {
    if (hook_.installed()) {
      hook_.uninstall();
      if (g_active == this) {
        g_active = nullptr;
      }
      g_original = nullptr;
    }
    // The feature is off: the status says so, whatever it said before (the
    // hook may never have been installed at all).
    set_hook_status(third_person::HookStatus::off);
  }
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

// The per-build facts, resolved on the render thread.
//
// The client's base, the input object and the build gate are per-build facts:
// the input object is a singleton whose address is fixed while the client is
// up, and the build gate is the client's PE timestamp. Resolving them means a
// module lookup and a PE-header read, so they are resolved **here** and
// cached; the detour — which runs on the game's camera thread, once per frame
// — only reads the cached values.
//
// The cache is refreshed when it is empty, when it is stale (the detour saw an
// input object that is not the cached one, so the module changed under us) and
// at most once per `k_refresh_interval_ms` otherwise. The stale flag is
// cleared **after** the new values are stored, so a mismatch the detour
// signals in between is not lost.
//
// This runs on the render thread and on the settings thread (the install path
// calls it), so the stamp is atomic.
void ThirdPerson::refresh_target() noexcept {
  const std::uint64_t now = now_ms();
  const std::uint64_t last = last_refresh_ms_.load(std::memory_order_relaxed);
  const bool stale = input_stale_.load(std::memory_order_relaxed);
  const bool due = last == 0 || stale || (now != 0 && now - last >= k_refresh_interval_ms);
  if (!due) {
    return;
  }
  last_refresh_ms_.store(now != 0 ? now : 1, std::memory_order_relaxed);
  // The three facts come out of **one** module lookup.
  const game::CameraEngine engine = game::camera_engine();
  client_base_.store(engine.client_base, std::memory_order_relaxed);
  build_verified_.store(engine.build_verified, std::memory_order_relaxed);
  input_object_.store(engine.input_object, std::memory_order_relaxed);
  input_stale_.store(false, std::memory_order_relaxed);
}

// The status of the camera hook, and the log line that goes with it.
//
// `append_log` opens, writes and closes the log file **per call**, and the
// install path is retried every frame, so a status is said **once**: the
// bitmask remembers which statuses have already been reported.
void ThirdPerson::set_hook_status(third_person::HookStatus status) noexcept {
  hook_status_.store(status, std::memory_order_relaxed);
  if (status == third_person::HookStatus::installed) {
    // The install path says this itself, with the address it hooked.
    return;
  }
  const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(status));
  std::uint8_t seen = logged_statuses_.load(std::memory_order_relaxed);
  while ((seen & bit) == 0) {
    if (logged_statuses_.compare_exchange_weak(seen, static_cast<std::uint8_t>(seen | bit),
                                               std::memory_order_relaxed)) {
      cs2::game::append_log(std::string("[cs2-features] third person: the camera hook is not "
                                        "installed: ") +
                            third_person::hook_status_text(status));
      return;
    }
  }
}

bool ThirdPerson::install() noexcept {
  if (hook_.installed()) {
    hook_status_.store(third_person::HookStatus::installed, std::memory_order_relaxed);
    return true;
  }
  if (install_failed_.load()) {
    // The attempt already failed; do not retry it every frame.
    set_hook_status(third_person::HookStatus::hook_refused);
    return false;
  }
  // **A feature that is off must not patch the game's code.** The camera
  // update is in the game's per-frame path, so an inline hook there is a crash
  // risk for no benefit while the feature is off. The hook is installed when
  // the operator turns the camera on, and removed when they turn it off.
  if (!enabled_.load(std::memory_order_relaxed)) {
    set_hook_status(third_person::HookStatus::off);
    return false;
  }
  // The camera block's offsets are build-specific, and so is the address of
  // the camera update itself: the hook is only installed on the build the
  // reverse-engineering pass verified. The console route still works on any
  // build.
  refresh_target();
  if (!build_verified_.load(std::memory_order_relaxed)) {
    set_hook_status(third_person::HookStatus::unverified_build);
    return false;
  }
  const std::uintptr_t target = game::camera_update_address();
  if (target == 0) {
    set_hook_status(third_person::HookStatus::waiting);
    return false;
  }
  g_active = this;
  const auto error = hook_.install(reinterpret_cast<void*>(target),
                                   reinterpret_cast<void*>(&camera_update_detour));
  if (error.has_value()) {
    g_active = nullptr;
    install_failed_.store(true);
    set_hook_status(third_person::HookStatus::hook_refused);
    cs2::game::append_log("[cs2-features] third person: the camera hook failed: " +
                          std::string(hooks::to_string(*error)));
    return false;
  }
  // The trampoline is captured now: the detour calls it on every frame, and
  // it must be the game's own camera update even if the feature is torn
  // down later.
  g_original = reinterpret_cast<CameraUpdateFn>(hook_.original());
  hook_status_.store(third_person::HookStatus::installed, std::memory_order_relaxed);
  cs2::game::append_log("[cs2-features] third person: the camera update is hooked");
  return true;
}

void ThirdPerson::update(const game::Angle& view_angles, const game::Vec3& eye_origin) noexcept {
  view_angles_.store(pack_angles(view_angles), std::memory_order_relaxed);
  // The per-build facts the detour needs are refreshed here, on the render
  // thread — never on the game's camera thread.
  refresh_target();
  if (!hook_.installed()) {
    (void)install();  // the client may not have been up when the payload booted
  }
  game::ThirdPersonParams params = current();
  // The state machine gates the camera: a death, freeze time or the main
  // screen releases the engine switch instead of holding a camera the player
  // cannot use.
  params.enabled = params.enabled && game_active_.load(std::memory_order_relaxed);
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
//
// The line is built **here**, on the render thread: the detour's path never
// logs and never allocates.
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
  line += " status=" + std::string(third_person::hook_status_text(hook_status()));
  line += " updates=" + std::to_string(updates_.load(std::memory_order_relaxed));
  line += " writes=" + std::to_string(writes_.load(std::memory_order_relaxed));
  line += " cmds=" + std::to_string(commands_.load(std::memory_order_relaxed));
  cs2::game::append_log(line);
}

bool ThirdPerson::enter_camera_update() noexcept {
  return camera_guard_.try_enter(current_owner());
}

void ThirdPerson::leave_camera_update() noexcept {
  camera_guard_.leave(current_owner());
}

void ThirdPerson::on_camera_update(void* input, int slot) noexcept {
  updates_.fetch_add(1, std::memory_order_relaxed);
  const game::ThirdPersonParams params = current();
  if (!params.enabled || slot != 0) {
    return;
  }
  // The build gate: the camera block's offsets are build-specific, so a write
  // on another build would land in the wrong field of the input object. The
  // gate is the **cached** one — resolved on the render thread — so the
  // detour does not pay for a module lookup per frame.
  if (!build_verified_.load(std::memory_order_relaxed)) {
    return;
  }
  // The identity check: the hook must be the camera update of *our* input
  // object, or the offsets would be applied to the wrong object.
  //
  // The input object is a singleton whose address is fixed while the client
  // is up, so it is resolved once and cached (see `refresh_target`). A
  // mismatch means the module changed under us: the render thread
  // re-resolves it.
  const std::uintptr_t expected = input_object_.load(std::memory_order_relaxed);
  if (expected == 0 || input != reinterpret_cast<void*>(expected)) {
    input_stale_.store(true, std::memory_order_relaxed);
    return;
  }
  // The engine access, built from the **cached** facts: the write does no
  // module lookup and no allocation, so the game's camera thread pays
  // nothing for it.
  const game::CameraEngine engine =
      game::live_camera_engine(client_base_.load(std::memory_order_relaxed),
                               build_verified_.load(std::memory_order_relaxed), expected);
  const game::Angle view = unpack_angles(view_angles_.load(std::memory_order_relaxed));
  const float yaw = game::third_person_yaw(view, params);
  if (game::write_third_person_state(engine, params, view.pitch, yaw)) {
    writes_.fetch_add(1, std::memory_order_relaxed);
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
