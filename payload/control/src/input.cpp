#include "cs2/control/input.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "cs2/game/log.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "cs2/hooks/memory.hpp"
#include "cs2/tls/bootstrap.hpp"
#include "offsets.hpp"
#if __has_include("schemas/client_dll.hpp")
#include "schemas/client_dll.hpp"
#define CS2_CONTROL_HAVE_CLIENT_SCHEMA 1
#endif

namespace cs2::control {
namespace {

using namespace cs2::sdk;

// How far into the `CCSGOInput` vtable to look for the `create_move` entry.
// The slot is index 25 in build 25815307; scan rather than hard-code so a
// reordered vtable still resolves.
constexpr std::size_t k_max_vtable_slots = 96;

// The one Input the payload owns, for the static hook detour.
Input* g_active = nullptr;

// The animfix's clock: a monotonic millisecond counter. The capture's age
// is measured with it (`cs2/control/animfix.hpp`'s validity rule).
[[nodiscard]] auto animfix_now_ms() noexcept -> std::uint64_t {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

auto flags_offset() noexcept -> std::uint32_t {
#if defined(CS2_CONTROL_HAVE_CLIENT_SCHEMA)
  const std::uint32_t offset =
      schemas::client_dll::field_offset_by_target("C_BaseEntity", "m_fFlags");
  return offset != 0 ? offset : 0x3f4;
#else
  return 0x3f4;
#endif
}

// `C_BaseEntity::m_vecAbsVelocity` — the local pawn's velocity, for the
// auto-strafe's wish angle.
auto velocity_offset() noexcept -> std::uint32_t {
#if defined(CS2_CONTROL_HAVE_CLIENT_SCHEMA)
  const std::uint32_t offset =
      schemas::client_dll::field_offset_by_target("C_BaseEntity", "m_vecAbsVelocity");
  return offset != 0 ? offset : 0x3f8;
#else
  return 0x3f8;
#endif
}

// True when `[address, address + size)` is mapped, probed through the safe
// in-process reader so a stale/garbage pointer cannot fault the game thread.
[[nodiscard]] auto writable(std::uintptr_t address, std::size_t size) noexcept -> bool {
  if (address == 0 || size == 0) {
    return false;
  }
  unsigned char probe = 0;
  return cs2::game::read_bytes(address, &probe, 1) &&
         cs2::game::read_bytes(address + size - 1, &probe, 1);
}

// `sv_quantize_movement_input` (on by default on Valve servers — UC 729264 /
// 753644) rounds `forwardmove`/`leftmove` to integers, so only the nine
// `{-1, 0, 1}²` move states survive. Snap a request into that set:
// `round(v / scale) * scale`, clamped to one step. A fractional request is
// therefore *not* silently rounded to zero by the server — its residual belongs
// on the sub-tick channel (`Input::write_move_steps`). The rule itself lives in
// the pure anti-aim module, so the anti-aim's re-based axes and the other
// movement features snap the same way.
[[nodiscard]] auto valid_set(float value) noexcept -> float {
  return cs2::control::anti_aim::quantize_axis(value / k_move_scale) * k_move_scale;
}

// The axes actually handed to the engine: clamped to ±`k_move_scale` and, in
// `quantize` mode, snapped to the valid set above. Applied identically to the
// raw axes, the input entry and the command's move floats so every consumer
// agrees.
auto resolve_axes(float forward, float side, float up, bool quantize, float out[3]) noexcept
    -> void {
  const auto axis = [quantize](float value) -> float {
    const float clamped = std::clamp(value, -k_move_scale, k_move_scale);
    return quantize ? valid_set(clamped) : clamped;
  };
  out[0] = axis(forward);
  out[1] = axis(side);
  out[2] = axis(up);
}

// Engine prediction (`docs/re/prediction.md`): the movement-services object and
// the modern-jump blob the simulation reads, plus the movement-services vtable
// slots the UC recipe calls (`RunCommand` 29, `SetPredictionCommand` 43,
// `ResetPredictionCommand` 44).
constexpr std::size_t k_movement_services_offset = 0x1330;  // C_BasePlayerPawn
constexpr std::size_t k_movement_services_size = 0xfe0;     // CCSPlayer_MovementServices
constexpr std::size_t k_modern_jump_offset = 0x6c8;
constexpr std::size_t k_modern_jump_landed_tick_offset = 0x20;
constexpr std::size_t k_modern_jump_landed_frac_offset = 0x24;
constexpr std::size_t k_pawn_flags_offset = 0x3f4;     // C_BaseEntity::m_fFlags
constexpr std::size_t k_pawn_velocity_offset = 0x3f8;  // C_BaseEntity::m_vecAbsVelocity
constexpr std::size_t k_tick_base_offset = 0x6b8;      // CBasePlayerController::m_nTickBase
constexpr std::size_t k_scene_node_offset = 0x330;     // C_BaseEntity::m_pGameSceneNode
constexpr std::size_t k_abs_origin_offset = 0xc8;      // CGameSceneNode::m_vecAbsOrigin
constexpr std::size_t k_run_command_slot = 29;
constexpr std::size_t k_set_prediction_command_slot = 43;
constexpr std::size_t k_reset_prediction_command_slot = 44;

// The record-based bunnyhop (`docs/re/research-bhop.md`; UC 771389 / 774033),
// re-derived on client.dll 25815307:
//   * `DECIDE` = `FUN_1808c2630(svc + 0x6c8, movedata)`; it computes
//     `NOW = {movedata->tickbase - 1, movedata->frac}` and compares the press
//     latches (`m_ModernJump + 0x18/0x1c`) and the landing record
//     (`+0x20/0x24`) against `sv_bhop_time_window * 0.5` (`DAT_182529040`,
//     default `0x3c000000` = 0.0078125 s) and `sv_jump_spam_penalty_time`
//     (`DAT_182529130`, `0x3c800000` = 0.015625 s = one tick, inclusive).
//   * `DoJump` = `FUN_1808e6fb0`; its alpha path overwrites `vel.xy` with the
//     recorded `m_flLastLandedVelocityX/Y` (`+0x28/0x2c`) when the press lands in
//     the window — that is the speed-preserving takeoff.
//   * the press edge is `FUN_1804c0f10(&svc->m_nButtonState, bit)`: a three-word
//     machine, `edge = C || (A && B)` (A = held now, B = changed since the
//     command start, C = pressed and released). With the key held at command
//     start a lone press leaves `B == 0` and produces no edge, so the primitive
//     is a **release+press pair sharing one `when`**.
constexpr std::size_t k_services_button_offset = 0x58;  // CInButtonState word 0 (held now)
// `C_CSPlayerPawn::m_pAimPunchServices` -> `m_predictableBaseAngle` (QAngle, degrees):
// the recoil punch the shot inherits, subtracted for recoil compensation.
constexpr std::size_t k_pawn_aim_punch_offset = 0x1598;
constexpr std::size_t k_aim_punch_angle_offset = 0x50;                // m_predictableBaseAngle
constexpr std::size_t k_aim_punch_angle_offset_unpredictable = 0xa4;  // m_unpredictableBaseAngle
constexpr std::size_t k_cmd_number_offset = 0x8;                      // CUserCmd::nSequenceNumber
constexpr std::size_t k_base_client_tick_offset = 0x54;               // CBaseUserCmdPB::client_tick
// Sub-tick placement: the press pair sits a few grid steps past the recorded
// contact, inside the engine's half-tick window; the grid is 1/64.
constexpr float k_subtick_grid = 1.0F / 64.0F;
constexpr float k_bhop_offset = 3.0F / 64.0F;
constexpr float k_bhop_first_when = 1.0F / 64.0F;
constexpr float k_bhop_last_when = 62.0F / 64.0F;
// How many ticks past the landing the beta catch-up (placement B) may still fire.
constexpr int k_bhop_catchup_ticks = 5;
// The admission gate discards the *whole* sub-tick array above this many steps.
constexpr std::size_t k_bhop_step_capacity = 32;
// Snap a fraction to the engine's 1/64 sub-tick grid.
[[nodiscard]] auto quantize64(float value) noexcept -> float {
  return std::floor(value * 64.0F + 0.5F) * k_subtick_grid;
}

// The anti-aim's "why" codes (`Input::anti_aim_reason`): why it did or did not
// write this tick.
constexpr int k_anti_aim_reason_off = 0;
constexpr int k_anti_aim_reason_acted = 1;
constexpr int k_anti_aim_reason_no_camera = 2;
constexpr int k_anti_aim_reason_no_angles = 3;
constexpr int k_anti_aim_reason_no_rebase = 4;
constexpr int k_anti_aim_reason_strafe = 5;
constexpr int k_anti_aim_reason_look = 6;

using run_command_fn = void(__fastcall*)(void* self, void* cmd);
using set_prediction_command_fn = void(__fastcall*)(void* self, void* cmd);
using reset_prediction_command_fn = void(__fastcall*)(void* self);

// The sub-tick `when` of one `CSubtickMoveStep` (`+0x24`), read safely.
[[nodiscard]] auto step_when(const void* step) noexcept -> float {
  float value = 0.0F;
  if (step == nullptr ||
      !cs2::game::read(reinterpret_cast<std::uintptr_t>(step) + k_step_when_offset, value)) {
    return 0.0F;
  }
  return value;
}

// Read-only probe of a 3-float move vector at `address` (engine units).
[[nodiscard]] auto read_move_sample(std::uintptr_t address) noexcept -> MoveSample {
  MoveSample sample;
  float values[3] = {0.0F, 0.0F, 0.0F};
  if (address == 0 || !cs2::game::read(address, values)) {
    return sample;
  }
  sample.forward = values[0];
  sample.side = values[1];
  sample.up = values[2];
  sample.valid = true;
  return sample;
}

}  // namespace

Input::~Input() {
  if (installed_ && slot_ != nullptr) {
    *slot_ = original_;
  }
  if (installed_ && slot5_ != nullptr) {
    *slot5_ = original5_;
  }
  if (g_active == this) {
    g_active = nullptr;
  }
}

auto Input::backend_name(Backend backend) noexcept -> const char* {
  return backend == Backend::usercmd ? "usercmd" : "console";
}

void Input::press(std::uint64_t bits) noexcept {
  set_mask_.fetch_or(bits);
  clear_mask_.fetch_and(~bits);
}

void Input::release(std::uint64_t bits) noexcept {
  clear_mask_.fetch_or(bits);
  set_mask_.fetch_and(~bits);
}

void Input::press_feature(std::uint64_t bits) noexcept {
  feature_mask_.fetch_or(bits);
}

void Input::release_feature(std::uint64_t bits) noexcept {
  feature_mask_.fetch_and(~bits);
}

void Input::reset_buttons() noexcept {
  set_mask_.store(0);
  clear_mask_.store(0);
}

void Input::set_view_angles(float pitch, float yaw, float roll) noexcept {
  angle_pitch_.store(pitch);
  angle_yaw_.store(yaw);
  angle_roll_.store(roll);
  angles_enabled_.store(true);
}

void Input::clear_view_angles() noexcept {
  angles_enabled_.store(false);
}

void Input::set_command_angles(float pitch, float yaw, float roll) noexcept {
  command_angle_pitch_.store(pitch);
  command_angle_yaw_.store(yaw);
  command_angle_roll_.store(roll);
  command_angles_enabled_.store(true);
}

void Input::set_step_yaw(float yaw_delta_degrees) noexcept {
  step_yaw_.store(yaw_delta_degrees);
  step_yaw_enabled_.store(true);
}

void Input::clear_step_yaw() noexcept {
  step_yaw_enabled_.store(false);
}

void Input::set_strafe(bool on) noexcept {
  strafe_enabled_.store(on);
}

void Input::set_rage_aim(float pitch, float yaw, int target, int hitbox) noexcept {
  rage_pitch_.store(pitch);
  rage_yaw_.store(yaw);
  rage_target_.store(target);
  rage_hitbox_.store(hitbox);
  rage_active_.store(true);
}

void Input::clear_rage_aim() noexcept {
  rage_active_.store(false);
  rage_stop_.store(false);
  rage_scope_.store(false);
  rage_reload_.store(false);
}

void Input::write_silent_angles(void* cmd, float pitch, float yaw, float roll) noexcept {
  if (cmd == nullptr) {
    return;
  }
  auto* bytes = static_cast<std::uint8_t*>(cmd);
  const std::uintptr_t pb = reinterpret_cast<std::uintptr_t>(bytes) + k_cmd_pb_offset;
  const auto size = *reinterpret_cast<std::uint32_t*>(pb + k_pb_history_size_offset);
  const auto rep = *reinterpret_cast<std::uintptr_t*>(pb + k_pb_history_rep_offset);
  if (rep == 0 || size == 0 || size >= 64) {
    return;
  }
  for (std::uint32_t i = 0; i < size; ++i) {
    const auto entry = *reinterpret_cast<std::uintptr_t*>(rep + 8 + i * 8);
    if (!writable(entry, k_history_entry_angles_offset + 8)) {
      continue;
    }
    const auto angles = *reinterpret_cast<std::uintptr_t*>(entry + k_history_entry_angles_offset);
    if (!writable(angles, k_angles_values_offset + 12)) {
      continue;
    }
    auto* xyz = reinterpret_cast<float*>(angles + k_angles_values_offset);
    xyz[0] = pitch;
    xyz[1] = yaw;
    xyz[2] = roll;
    *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
    angle_writes_.fetch_add(1);
  }
}

// Write the command's **own** `viewangles` (`base + 0x40`) — the field the
// server reads the player's angles from, and the field the movement's wish
// basis is built from (`SetupMove` copies it into the `CMoveData`'s angles;
// `AirMove` builds the wish from it).
//
// This is the **one** place that field is written: the `look` control command
// and the anti-aim share it.
auto Input::write_command_angles(std::uint8_t* base, float pitch, float yaw, float roll) noexcept
    -> bool {
  if (base == nullptr) {
    return false;
  }
  if (!writable(reinterpret_cast<std::uintptr_t>(base), k_base_viewangles_offset + sizeof(void*))) {
    return false;
  }
  const auto angles = *reinterpret_cast<std::uintptr_t*>(base + k_base_viewangles_offset);
  if (!writable(angles, k_angles_values_offset + 3 * sizeof(float))) {
    return false;
  }
  auto* xyz = reinterpret_cast<float*>(angles + k_angles_values_offset);
  xyz[0] = pitch;
  xyz[1] = yaw;
  xyz[2] = roll;
  *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
  *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_viewangles_bit;
  return true;
}

// Write the command's analog axes — the three places the engine's analog
// lives:
//
//   * the command's own floats (`base + 0x58/+0x5c`),
//   * the **raw** axes (`CCSGOInput + 0x270/0x274`) — the slot-5 builder
//     re-derives the command's floats from them, so a command-only write is
//     overwritten (`docs/re/angle-split.md` §5.3, step 1b),
//   * the **button bits** (the command's legacy mask and the `buttons_pb`'s two
//     states) — `SetupMove`'s sanitizer turns the held buttons into the same
//     axes (`FUN_180a780a0`), so a command whose buttons disagree with its
//     analog is re-derived from the buttons.
//
// Every pointer is read **inside** its `writable()` guard: a stale or garbage
// pointer must never fault the game thread (the payload is manually mapped
// without registered exception tables).
//
// Returns false when the wish carries no move button at all — there is
// nothing to write.
auto Input::write_move_axes(std::uint8_t* base, void* cmd, float forward, float left) noexcept
    -> bool {
  if (base == nullptr) {
    return false;
  }
  constexpr std::uint64_t move_bits =
      button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
  const std::uint64_t held = cs2::control::engine_move_buttons(forward, left, k_move_epsilon);
  if ((held & move_bits) == 0) {
    return false;  // nothing to write
  }
  if (writable(reinterpret_cast<std::uintptr_t>(base), k_base_move_offset + 12)) {
    auto* move = reinterpret_cast<float*>(base + k_base_move_offset);
    move[0] = forward;
    move[1] = left;
    move[2] = 0.0F;
    *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_move_bits;
  }
  if (ccsgo_input_ != 0) {
    auto* raw = reinterpret_cast<float*>(ccsgo_input_ + k_input_move_offset);
    if (writable(reinterpret_cast<std::uintptr_t>(raw), 3 * sizeof(float))) {
      raw[0] = forward;
      raw[1] = left;
      raw[2] = 0.0F;
    }
  }
  if (cmd != nullptr && writable(reinterpret_cast<std::uintptr_t>(cmd) + k_cmd_buttons_offset,
                                 sizeof(std::uint64_t))) {
    auto* legacy =
        reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(cmd) + k_cmd_buttons_offset);
    *legacy = (*legacy & ~move_bits) | held;
  }
  // The `buttons_pb` pointer lives in the command's base — read it **inside**
  // the guard, so an unmapped base cannot fault the game thread.
  if (writable(reinterpret_cast<std::uintptr_t>(base), k_base_buttons_pb_offset + 8)) {
    const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
    if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
      auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
      state[0] = (state[0] & ~move_bits) | held;
      state[1] = (state[1] & ~move_bits) | held;
      *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3U;
    }
  }
  return true;
}

// True when the command's own `viewangles` already carry the given angles.
//
// The anti-aim's write is **idempotent** (the jitter is keyed on the command's
// number, so both writes of a tick carry the same values), and the hook can
// run more than once for the same command (`docs/re/subtick.md` §"the hook
// runs more than once"). This is the content-based guard that keeps the write
// to **once per command** without any state of its own: a write that failed
// left the old angles in place, and the next call writes again.
auto Input::command_angles_are(std::uint8_t* base, float pitch, float yaw) const noexcept -> bool {
  if (base == nullptr ||
      !writable(reinterpret_cast<std::uintptr_t>(base), k_base_viewangles_offset + sizeof(void*))) {
    return false;
  }
  const auto angles = *reinterpret_cast<std::uintptr_t*>(base + k_base_viewangles_offset);
  if (!writable(angles, k_angles_values_offset + 3 * sizeof(float))) {
    return false;
  }
  const auto* xyz = reinterpret_cast<const float*>(angles + k_angles_values_offset);
  return std::fabs(xyz[0] - pitch) <= 1.0e-3F && std::fabs(xyz[1] - yaw) <= 1.0e-3F;
}

// One tick of the anti-aim.
//
// The write point is the **command's own** `viewangles` (`base + 0x40`) — the
// channel the server reads the player's angles from, and the channel the
// movement's wish basis is built from (`docs/re/angle-split.md` §5, W1). The
// camera reads a **different** field (the live view angles,
// `CCSGOInput + 0x688`), which this code only ever *reads*: the camera cannot
// follow the fake angles, so the split needs no pin (§5.2, §7 step 7).
//
// The write happens in the **command path** (the game thread, once per
// command), never per frame: the engine writes the command's `viewangles`
// exactly once per tick, and a higher-frequency writer fights its own
// write-back (§7 step 6).
void Input::anti_aim_tick(void* cmd, std::uint8_t* base) noexcept {
  anti_aim_acted_.store(false);
  anti_aim_reason_.store(k_anti_aim_reason_off);
  if (!anti_aim_enabled_.load() || base == nullptr) {
    return;
  }
  // --- The composition rules ---------------------------------------------------
  // The anti-aim must not destroy another feature's work:
  //   * the auto-strafe's **sub-tick** mode rotates the movement basis with
  //     `yaw_delta` steps — the same basis the anti-aim fakes, so the two
  //     rotations would add up and the strafe would steer off;
  //   * the `look` command writes the **same field** (`base + 0x40`).
  // The auto-strafe's *analog* mode and the ragebot's auto-stop compose: they
  // write the camera frame's axes, and the re-base re-expresses whatever the
  // command carries.
  if (strafe_enabled_.load() && strafe_subticks_.load() > 0) {
    anti_aim_reason_.store(k_anti_aim_reason_strafe);
    anti_aim_rebase_skipped_.fetch_add(1);
    return;
  }
  if (command_angles_enabled_.load()) {
    anti_aim_reason_.store(k_anti_aim_reason_look);
    anti_aim_rebase_skipped_.fetch_add(1);
    return;
  }
  // --- The camera's own angles --------------------------------------------------
  // The **live** view angles (`CCSGOInput + 0x688`). The anti-aim reads them
  // and never writes them — the camera reads the same field, so a write here
  // would drag the operator's view onto the fake angles.
  float camera[3] = {0.0F, 0.0F, 0.0F};
  if (ccsgo_input_ == 0 || !cs2::game::read(ccsgo_input_ + k_input_view_angles_offset, camera)) {
    anti_aim_reason_.store(k_anti_aim_reason_no_camera);
    return;
  }
  const anti_aim::Angles camera_angles{camera[0], camera[1], camera[2]};
  anti_aim_camera_yaw_.store(camera_angles.yaw);
  anti_aim_camera_pitch_.store(camera_angles.pitch);

  // --- The fake angles ---------------------------------------------------------
  // The command's own number: the tick the jitter is keyed on. The jitter is
  // re-rolled once per **command** — the same command always gets the same
  // offset, so the two writes of a tick (the slot-5 builder and the slot-25
  // finaliser) agree and the write stays idempotent.
  std::uint32_t number = 0;
  if (cmd != nullptr && writable(reinterpret_cast<std::uintptr_t>(cmd) + k_cmd_number_offset,
                                 sizeof(std::uint32_t))) {
    number = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(cmd) +
                                                     k_cmd_number_offset);
  }
  const auto side = static_cast<anti_aim::Side>(anti_aim_side_.load());
  const float fake = anti_aim::fake_yaw(camera_angles.yaw, anti_aim_yaw_offset_.load(), side,
                                        anti_aim_seed_, number, anti_aim_jitter_.load());
  const float pitch = anti_aim::clamp_pitch(anti_aim_pitch_.load());
  anti_aim_fake_yaw_.store(fake);
  anti_aim_fake_pitch_.store(pitch);
  anti_aim_basis_cos_.store(std::cos(pitch * k_deg_to_rad));

  // --- The write ---------------------------------------------------------------
  // The fake angles into the command's own `viewangles` ...
  if (command_angles_are(base, pitch, fake)) {
    // The command already carries the fake angles (the hook ran twice for the
    // same command): the anti-aim has acted for it and the write is not
    // repeated. The guard is **before** the re-base below on purpose — the
    // anti-aim's own write puts the anti-aim frame's axes on the command, and
    // re-basing those again would rotate them twice.
    anti_aim_acted_.store(true);
    anti_aim_reason_.store(k_anti_aim_reason_acted);
    return;
  }
  // --- The movement re-base ----------------------------------------------------
  // The command's **current** axes: whatever wrote them — the engine's own
  // input, the auto-strafe's wish, the ragebot's auto-stop — are in the
  // **camera** frame, and the re-base re-expresses them in the anti-aim frame.
  anti_aim::Axes current{};
  if (writable(reinterpret_cast<std::uintptr_t>(base), k_base_move_offset + 12)) {
    const auto* move = reinterpret_cast<const float*>(base + k_base_move_offset);
    current.forward = move[0];
    current.left = move[1];
  }
  // The **delivered** axes: a member of the engine's discrete set, chosen so
  // the walk direction stays as close to the camera's as the set allows. The
  // cost is reported: the direction error and the walk-speed factor.
  const auto delivery =
      anti_aim::deliver_axes(camera_angles, anti_aim::Angles{pitch, fake, 0.0F}, current);
  anti_aim_forward_.store(delivery.axes.forward);
  anti_aim_left_.store(delivery.axes.left);
  anti_aim_direction_error_.store(delivery.direction_error_deg);
  anti_aim_wish_factor_.store(delivery.wish_factor);
  anti_aim_saturated_.store(delivery.saturated);

  if (!write_command_angles(base, pitch, fake, 0.0F)) {
    anti_aim_reason_.store(k_anti_aim_reason_no_angles);
    return;
  }
  if (anti_aim_move_rebase_.load()) {
    // ... and the delivered axes into the command's floats, the raw axes and
    // the button bits.
    write_move_axes(base, cmd, delivery.axes.forward, delivery.axes.left);
    anti_aim_rebase_applied_.fetch_add(1);
  } else {
    // The re-base is off: the fake angles are in place and the movement's wish
    // basis follows them — a diagnostic, not a mode to play in.
    anti_aim_reason_.store(k_anti_aim_reason_no_rebase);
    anti_aim_rebase_skipped_.fetch_add(1);
  }
  anti_aim_writes_.fetch_add(1);
  anti_aim_acted_.store(true);
  anti_aim_reason_.store(k_anti_aim_reason_acted);
}

auto Input::read_aim_punch(float out[3]) const noexcept -> bool {
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return false;
  }
  std::uintptr_t punch = 0;
  if (!cs2::game::read(pawn + k_pawn_aim_punch_offset, punch) || punch == 0) {
    return false;
  }
  // The full view punch the shot inherits is predictable + unpredictable (the
  // accumulated spray pattern in the unpredictable buffer).
  float predictable[3] = {0.0F, 0.0F, 0.0F};
  float unpredictable[3] = {0.0F, 0.0F, 0.0F};
  const bool has_predictable = cs2::game::read(punch + k_aim_punch_angle_offset, predictable);
  const bool has_unpredictable =
      cs2::game::read(punch + k_aim_punch_angle_offset_unpredictable, unpredictable);
  if (!has_predictable && !has_unpredictable) {
    return false;
  }
  out[0] = predictable[0] + unpredictable[0];
  out[1] = predictable[1] + unpredictable[1];
  out[2] = predictable[2] + unpredictable[2];
  return true;
}

// The compensated silent aim: the desired view minus the recoil punch (which the shot
// would otherwise inherit).
void Input::rage_silent_angles(float& pitch, float& yaw) const noexcept {
  pitch = rage_pitch_.load();
  yaw = rage_yaw_.load();
  if (!rage_recoil_.load()) {
    return;
  }
  float punch[3] = {0.0F, 0.0F, 0.0F};
  if (read_aim_punch(punch)) {
    pitch -= punch[0];
    yaw -= punch[1];
  }
}

// Write the silent aim into the **raw** per-sub-tick angle history (`CCSGOInput + 0xbd0`)
// which the engine copies into the command's `input_history` *inside* slot 5 — so it
// must land before the original. This is the channel the shot's history entry is built
// from; the engine's own view (the camera) is a separate global and is not touched.
void Input::write_raw_silent_angles(float pitch, float yaw, float roll) noexcept {
  if (ccsgo_input_ == 0) {
    return;
  }
  std::int32_t count = 0;
  std::uintptr_t rep = 0;
  if (!cs2::game::read(ccsgo_input_ + k_input_history_count_offset, count) ||
      !cs2::game::read(ccsgo_input_ + k_input_history_rep_offset, rep)) {
    return;
  }
  if (count <= 0 || count > 64 || rep == 0) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    const auto entry = rep + static_cast<std::uintptr_t>(i) * k_input_history_stride;
    if (!writable(entry + k_input_history_angles_offset, 3 * sizeof(float))) {
      break;
    }
    auto* xyz = reinterpret_cast<float*>(entry + k_input_history_angles_offset);
    xyz[0] = pitch;
    xyz[1] = yaw;
    xyz[2] = roll;
  }
}

void Input::write_engine_buttons(std::uint64_t set_bits, std::uint64_t clear_bits) noexcept {
  if (set_bits == 0 && clear_bits == 0) {
    return;
  }
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return;
  }
  std::uintptr_t services = 0;
  if (!cs2::game::read(pawn + k_movement_services_offset, services) || services == 0) {
    return;
  }
  auto* state = reinterpret_cast<std::uint64_t*>(services + k_services_button_offset);
  if (!writable(reinterpret_cast<std::uintptr_t>(state), sizeof(std::uint64_t))) {
    return;
  }
  *state = (*state | set_bits) & ~clear_bits;
}

void Input::rage_tick(std::uint8_t* cmd, const std::uint64_t* buttons) noexcept {
  if (!rage_active_.load()) {
    return;
  }
  float pitch = 0.0F;
  float yaw = 0.0F;
  // Recoil compensation (predictable + unpredictable punch) is folded into the silent
  // aim. Read fresh each tick.
  rage_silent_angles(pitch, yaw);
  write_silent_angles(cmd, pitch, yaw, 0.0F);
  rage_writes_.fetch_add(1);
  if (buttons == nullptr) {
    return;
  }
  // Mirror the (persistently asserted) attack, zoom and reload into the command's
  // `CInButtonStatePB` so the sub-tick records agree with the legacy mask. A changed
  // word is needed for the engine to see a fresh press edge (zoom/reload act on the
  // edge), exactly as the attack does.
  const auto base = *reinterpret_cast<std::uintptr_t*>(cmd + k_cmd_pb_offset + k_pb_base_offset);
  const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
  std::uint64_t extra = *buttons & button::k_attack;
  if (rage_scope_.load()) {
    // `IN_ATTACK2` (the right mouse) is what *triggers* the scope; `IN_ZOOM` is
    // the engine's own zoom bit (`1 << 34`, see `strafe_geometry.hpp`'s
    // `button` namespace). The engine's action table has no action that
    // produces `IN_ZOOM` — it is in the "mod specific" range the engine
    // populates itself — so the zoom bit is a state assertion, and the scope
    // itself rides on `IN_ATTACK2`.
    extra |= button::k_attack2 | button::k_zoom;
  }
  if (rage_reload_.load()) {
    extra |= button::k_reload;
  }
  if (extra != 0 && writable(buttons_pb, k_buttonstate_values_offset + 16)) {
    auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
    state[0] = (state[0] & ~extra) | extra;  // held
    state[1] = (state[1] & ~extra) | extra;  // changed (a press edge)
    *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3U;
  }
  // Also assert the extra buttons in the engine's own held-button word
  // (`CInButtonState` at the movement services), which some weapon actions read
  // instead of the command mask.
  write_engine_buttons(extra, 0);
  if ((*buttons & button::k_attack) != 0) {
    rage_shots_.fetch_add(1);
  }
}

void Input::set_strafe_config(float wishspeed, int subticks) noexcept {
  if (wishspeed > 0.0F) {
    strafe_wishspeed_.store(wishspeed);
  }
  strafe_subticks_.store(std::clamp(subticks, 0, 16));
}

void Input::set_strafe_mode(int mode) noexcept {
  strafe_mode_.store(std::clamp(mode, static_cast<int>(StrafeMode::off),
                                static_cast<int>(StrafeMode::directional)));
}

void Input::set_strafe_ignore_keys(bool ignore) noexcept {
  strafe_ignore_keys_.store(ignore);
}

auto Input::velocity(float& x, float& y) const noexcept -> bool {
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return false;
  }
  float values[3] = {0.0F, 0.0F, 0.0F};
  if (!cs2::game::read(pawn + velocity_offset_, values)) {
    return false;
  }
  x = values[0];
  y = values[1];
  return true;
}

// The auto-strafe (`set_strafe`): one tick of a **normal, silent** air-strafe.
// Airborne only (`AirMove` is the air path; the ground is `WalkMove` + friction).
// The goal heading is the player's **W/A/S/D intent** read in the *view* frame —
// `W` (or nothing) is the camera's forward, `S` is behind, `D` is 90 degrees to
// the right, `A` is 90 degrees to the left and `W`+`D` is 45 degrees to the
// right. A held `A`/`D` no longer switches the feature off; in the legacy modes
// (the camera-relative goal, or `strafe_ignore_keys`) it still stands the
// feature down, so the player's own air control wins.
//
// The wish is then pushed perpendicular to the **current** horizontal velocity
// on the goal's side, at the `AirAccelerate` optimum `theta*`
// (`docs/re/strafing.md`), recomputed every tick so it re-perpendicularises
// instead of decaying (a *held* side with a fixed basis stops accelerating after
// ~3 ticks). The side decision carries a **dead zone and a hysteresis**: the
// goal's offset from the velocity heading only decides the side when it is at
// or beyond `k_strafe_side_deadzone_deg`; inside it the side of the last
// decisive tick is kept. Holding `W` alone puts the goal exactly on the
// velocity heading, and the tie-break there would flip the curve every tick.
//
// The **legacy** modes (the camera-relative goal, or `strafe_ignore_keys`)
// take the old decision instead: the side follows the sign of the goal's
// offset from the velocity heading, with no dead zone and no hysteresis, so
// they restore the old behaviour exactly.
//
// The maths is in `strafe_geometry.hpp` (pure, host-tested); this function only
// reads the engine and hands it over. Nothing writes a view angle, so neither
// the camera nor the pawn's eye angles move (`write_strafe_wish` documents the
// two silent encodings).
void Input::strafe_tick(void* base, void* cmd) noexcept {
  strafe_airborne_.store(false);
  strafe_delta_.store(0.0F);
  strafe_goal_.store(0.0F);
  strafe_wish_.store(0.0F);
  strafe_rot_.store(0.0F);
  strafe_side_.store(0);
  strafe_steps_.store(0);
  strafe_clamped_.store(false);
  strafe_side_locked_.store(0);
  strafe_intent_f_.store(0.0F);
  strafe_intent_l_.store(0.0F);
  strafe_reason_.store(0);
  if (base == nullptr || !strafe_enabled_.load()) {
    strafe_side_last_.store(0);
    // The feature is off: there is no airtime to measure and no sample to
    // compare against. The gain is the **last airtime the feature acted in**,
    // so it goes back to zero rather than reporting a stale one.
    airtime_.abandon();
    strafe_vel_yaw_valid_.store(false);
    strafe_gain_.store(0.0F);
    return;
  }
  const int mode = strafe_mode_.load();
  if (mode == static_cast<int>(StrafeMode::off)) {
    strafe_reason_.store(8);  // the settings turned the feature off
    strafe_side_last_.store(0);
    airtime_.abandon();
    strafe_vel_yaw_valid_.store(false);
    strafe_gain_.store(0.0F);
    return;
  }
  if (on_ground()) {
    strafe_reason_.store(1);  // grounded: leave the player's own input alone
    strafe_side_last_.store(0);
    // The airtime just ended: the speed the player carried out of it is the
    // feature's **outcome** — the gain the operator sees (the working encoding
    // gains ~+85 u/s per hop; a server that ignores the sub-tick view angles
    // gains nothing). The velocity is read here, in the hook, before the
    // engine simulates this command's ground movement — so it is still the
    // speed the player had in the air.
    if (airtime_.airborne()) {
      float gx = 0.0F;
      float gy = 0.0F;
      if (velocity(gx, gy)) {
        const float landing_speed = std::sqrt(gx * gx + gy * gy);
        strafe_gain_.store(airtime_.end(landing_speed));
      } else {
        airtime_.abandon();
      }
    }
    // A new jump starts from a clean sample: the first airborne tick's turn is
    // the effect of the *take-off* tick's write, and there was none.
    strafe_vel_yaw_valid_.store(false);
    return;
  }
  float vx = 0.0F;
  float vy = 0.0F;
  if (!velocity(vx, vy)) {
    strafe_reason_.store(2);
    strafe_side_last_.store(0);
    strafe_vel_yaw_valid_.store(false);  // no sample: the next turn is not comparable
    return;
  }
  const float speed = std::sqrt(vx * vx + vy * vy);
  strafe_speed_.store(speed);
  if (speed < k_strafe_min_speed) {
    strafe_reason_.store(2);
    strafe_side_last_.store(0);
    strafe_vel_yaw_valid_.store(false);  // no sample: the next turn is not comparable
    return;
  }
  strafe_airborne_.store(true);
  // The take-off speed of this airtime: the gain is measured against it.
  if (!airtime_.airborne()) {
    airtime_.begin(speed);
    // The counters describe **this** airtime: a later phase (a fall down a
    // wall, where the wish cannot turn the velocity) must not poison the
    // verdict of the airtime that worked.
    strafe_steered_ticks_.store(0);
    strafe_ignored_.store(0);
  }

  auto* bytes_base = static_cast<std::uint8_t*>(base);
  auto* bytes_cmd = static_cast<std::uint8_t*>(cmd);
  const auto angles_ptr = *reinterpret_cast<std::uintptr_t*>(bytes_base + k_base_viewangles_offset);
  float view_yaw = 0.0F;
  if (angles_ptr == 0 ||
      !cs2::game::read(angles_ptr + k_angles_values_offset + sizeof(float), view_yaw)) {
    strafe_reason_.store(3);
    strafe_airborne_.store(false);
    strafe_side_last_.store(0);
    return;
  }

  // The player's travel intent, read from the command's own mask *before* it is
  // overwritten. The diagnostics report the **raw** W/A/S/D reading, so the
  // operator can see what the feature read; in the legacy modes a held strafe
  // key (A/D) stands the assist down — the player is already air-controlling —
  // and the goal is the camera's forward/back.
  std::uint64_t buttons = 0;
  const auto cmd_ptr = reinterpret_cast<std::uintptr_t>(cmd);
  if (cmd != nullptr && writable(cmd_ptr + k_cmd_buttons_offset, sizeof(std::uint64_t))) {
    buttons = *reinterpret_cast<std::uint64_t*>(cmd_ptr + k_cmd_buttons_offset);
  }
  const StrafeIntent intent = intent_from_buttons(buttons);
  strafe_intent_f_.store(intent.forward);
  strafe_intent_l_.store(intent.right);
  const bool legacy =
      mode == static_cast<int>(StrafeMode::camera_relative) || strafe_ignore_keys_.load();
  constexpr std::uint64_t side_bits = button::k_moveleft | button::k_moveright;
  if (legacy && (buttons & side_bits) != 0) {
    strafe_reason_.store(7);
    strafe_airborne_.store(false);
    strafe_side_last_.store(0);
    return;
  }
  // The legacy goal ignores the lateral keys (and lets `S` win over `W`).
  const StrafeIntent goal_intent = legacy ? legacy_intent_from_buttons(buttons) : intent;

  // The `AirAccelerate` optimum: with the per-call budget
  // `A = wishspeed * sv_airaccelerate * frametime` and the air cap
  // `W = sv_air_max_wishspeed` (30), the wish sits at `theta* = acos((W - A)/s)`
  // off the velocity; `A >= W` collapses it to exactly 90 degrees (the usual case
  // for a full analog, `A = 250 * 12 / 64 = 46.875`). The engine re-runs
  // `AirAccelerate` once per slice with `frametime = (1/64)/slices`
  // (`docs/re/strafing.md` §2.5), so a slice's `A` is the full-tick `A` divided by
  // the slice count.
  const int steps = std::clamp(strafe_subticks_.load(), 0, k_strafe_max_steps);
  const float cap = k_strafe_air_wishspeed;
  const float wishspeed = strafe_wishspeed_.load();
  const float accel_tick = wishspeed * k_strafe_airaccelerate * k_tick_interval;

  const auto vel_yaw = heading_of(vx, vy);
  // The legacy modes take the **old** decision: the side follows the sign of
  // the goal's offset from the velocity heading, with no dead zone and no
  // hysteresis. The dead zone and the hysteresis belong to the new WASD goal
  // (where `W` alone puts the goal exactly on the velocity heading); in the
  // legacy modes they would change the old behaviour, which the spec requires
  // them to restore exactly.
  const StrafePlan decision =
      legacy ? legacy_plan(goal_intent, view_yaw, vel_yaw, speed, accel_tick, cap)
             : plan(goal_intent, view_yaw, vel_yaw, speed, accel_tick, cap,
                    strafe_side_last_.load(), k_strafe_side_deadzone_deg);
  strafe_goal_.store(decision.goal_yaw);
  strafe_side_.store(decision.side);
  strafe_side_locked_.store(decision.decisive ? 0 : 1);
  if (decision.decisive) {
    // The hysteresis state: the side of the last decisive tick. It is reset
    // when the strafe stops acting (above), so a fresh jump starts from the
    // default tie-break.
    strafe_side_last_.store(decision.side);
  }
  const auto side = static_cast<float>(decision.side);
  const float wish_yaw = decision.wish_yaw;
  const float rot = normalize_angle(wish_yaw - view_yaw);
  strafe_wish_.store(wish_yaw);
  strafe_delta_.store(rot);

  // The command's own number: **the hook runs more than once per command**
  // (the engine builds the command more than once, and the payload's own
  // `strafe_steps_present` exists because of it). The velocity reading is the
  // same on every one of those runs, so a turn measured between two of them
  // reads as zero — and a zero turn is exactly what a **gated** rotation looks
  // like. The verdict is therefore made **once per command**, on the run whose
  // command number changed (the same signal the bhop's phase correction uses).
  const auto command_number = cmd != nullptr
                                  ? *reinterpret_cast<const std::uint32_t*>(
                                        static_cast<std::uint8_t*>(cmd) + k_cmd_number_offset)
                                  : 0U;
  const bool fresh_command = command_number != strafe_cmd_number_;
  strafe_cmd_number_ = command_number;

  // **Did the engine apply the rotation?** The turn of the velocity heading
  // between this tick's read and the previous one is the effect of the
  // rotation written in between: the engine turns the velocity toward the
  // wish when it honours the sub-tick view angles, and it does not turn it at
  // all when the server ignores them (`sv_subtick_movement_view_angles 0` —
  // `docs/re/strafing.md` §"The gate"). The sample is the one the **previous**
  // tick left behind, together with the side it pushed the wish on, so a side
  // that flipped in between is judged against the side that was actually
  // written.
  //
  // The verdict is made **after** the write (below), and only when the write
  // succeeded: a tick that asked for a rotation and did not write one (no
  // room in the sub-tick array) asked for nothing, and judging it would
  // count an "ignored" rotation the payload never sent.

  // The per-slice basis rotations for the sub-tick mode. The engine **accumulates**
  // each step's `yaw_delta` onto the basis, so splitting the whole rotation evenly
  // ramps the wish and only the last slice lands on `theta*` (≈ 1/N of the gain).
  // Instead simulate the tick slice by slice and set each step's delta so slice `k`'s
  // basis is the wish `theta_k*` from the **simulated** velocity at slice `k` — the
  // community's `ref_air_accel_sim` recipe. That puts the bulk of the rotation on the
  // first step and small deltas after, and makes every slice gain.
  float deltas[k_strafe_max_steps] = {};
  int count = 0;
  if (steps >= 1) {
    const float accel_slice = accel_tick / static_cast<float>(steps);
    float sx = vx;
    float sy = vy;
    float basis = view_yaw;
    for (int k = 0; k < steps && count < k_strafe_max_steps; ++k) {
      const float s = std::sqrt(sx * sx + sy * sy);
      if (s < k_strafe_min_speed) {
        break;
      }
      const auto slice_vel = heading_of(sx, sy);
      const float slice_theta = optimal_theta(s, accel_slice, cap);
      const float wish = normalize_angle(slice_vel + side * slice_theta);
      deltas[count++] = normalize_angle(wish - basis);
      basis = wish;
      // Step the simulated velocity the way `AirAccelerate` will:
      // `addspeed = min(wishspeed, 30) - dot(v, wishdir)`, `applied = min(A, addspeed)`.
      const float wx = std::cos(wish * k_deg_to_rad);
      const float wy = std::sin(wish * k_deg_to_rad);
      const float addspeed = cap - (sx * wx + sy * wy);
      if (addspeed > 0.0F) {
        const float applied = std::min(accel_slice, addspeed);
        sx += applied * wx;
        sy += applied * wy;
      }
    }
    // Keep the accumulated rotation inside the admission gate's limit (the bhop pair
    // shares the array) by scaling the whole sequence down if it is exceeded.
    float total = 0.0F;
    for (int k = 0; k < count; ++k) {
      total += deltas[k];
    }
    if (std::fabs(total) > k_strafe_rot_limit_deg) {
      const float scale = k_strafe_rot_limit_deg / std::fabs(total);
      for (int k = 0; k < count; ++k) {
        deltas[k] *= scale;
      }
      strafe_clamped_.store(true);
      total *= scale;
    }
    strafe_rot_.store(total);
  }

  // The camera-frame analog in the engine's **own** axes: `forwardmove` and
  // `leftmove` (the command's `leftmove` is positive to the player's LEFT —
  // `docs/re/strafe-sign.md`). The side axis goes through the engine's helper
  // so the sign lives in exactly one place
  // (`k_engine_side_axis_positive_is_left`); it is snapped to the
  // `sv_quantize_movement_input` valid set so a rounding server and the client
  // agree. Used only in the `subticks == 0` mode — the sub-tick mode rotates
  // the basis instead and keeps `(1, 0)`.
  const float rel = rot * k_deg_to_rad;
  const float forward = std::cos(rel);
  const float left = cs2::control::engine_side_axis_for_wish(rot);

  if (write_strafe_wish(bytes_base, bytes_cmd, forward, left, deltas, steps)) {
    strafe_reason_.store(4);
    strafe_writes_.fetch_add(1);
    // The wish was written: the next command's turn of the velocity heading is
    // the effect of **this** write, so this is where the sample is taken and
    // the verdict is made — once per command (see `fresh_command` above). The
    // rotation recorded with the sample is the one that was **written**
    // (`strafe_rot_`, after the gate's clamp), not the one that was asked for:
    // in the camera-frame analog encoding there is no rotation at all, and the
    // verdict is then simply unknown — the gate does not apply there.
    if (fresh_command) {
      strafe_judge_steer(vel_yaw, decision.side, strafe_rot_.load());
    }
  } else if (strafe_reason_.load() == 0) {
    strafe_reason_.store(5);  // the sub-tick array had no room
  }
}

// The steering verdict (the gate): did the engine turn the velocity toward the
// wish the payload asked for?
//
// The sample is the heading the **previous** writing tick read, together with
// the side that tick pushed the wish on: the turn between them is the effect
// of that write, and a side that flipped in between is judged against the side
// that was actually written. With no earlier sample there is nothing to
// compare (the first airborne tick), so the tick is only recorded.
void Input::strafe_judge_steer(float vel_yaw, int side, float written_rotation) noexcept {
  if (strafe_vel_yaw_valid_.load()) {
    const float turn = steer_turn(strafe_vel_yaw_last_.load(), vel_yaw);
    strafe_turn_.store(turn);
    // The verdict is made on the rotation the **previous** tick wrote (the one
    // whose effect this turn measures) and the side it pushed the wish on: the
    // current tick's request has not been written yet, and the rotation that
    // was written may have been clamped by the gate's limit.
    switch (classify_steer(strafe_vel_yaw_last_.load(), vel_yaw, strafe_side_sample_.load(),
                           strafe_rot_sample_.load())) {
      case SteerVerdict::followed:
        strafe_steered_ticks_.fetch_add(1);
        strafe_steered_.store(true);
        break;
      case SteerVerdict::ignored:
        strafe_ignored_.fetch_add(1);
        strafe_steered_.store(false);
        break;
      case SteerVerdict::unknown:
        break;
    }
  }
  strafe_vel_yaw_last_.store(vel_yaw);
  strafe_side_sample_.store(side);
  strafe_rot_sample_.store(written_rotation);
  strafe_vel_yaw_valid_.store(true);
}

// True when the command's `subtick_moves` already holds a yaw-only step (button 0,
// no analog deltas, a non-zero `yaw_delta`) — i.e. the strafe's basis rotation is
// already in the array. The engine's own steps carry buttons, so this only matches
// a strafe/`step_yaw` rotation.
auto Input::strafe_steps_present(std::uint8_t* base) const noexcept -> bool {
  if (base == nullptr) {
    return false;
  }
  auto* field = base + k_base_subtick_moves_offset;
  if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + sizeof(void*))) {
    return false;
  }
  const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
  const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
  if (count <= 0 || rep == 0 || static_cast<std::size_t>(count) > k_bhop_step_capacity) {
    return false;
  }
  const auto elements = rep + k_rep_elements_offset;
  for (int i = 0; i < count; ++i) {
    std::uintptr_t step = 0;
    if (!cs2::game::read(elements + static_cast<std::uintptr_t>(i) * 8, step) || step == 0 ||
        !writable(step, k_step_yaw_offset + sizeof(float))) {
      continue;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(step);
    if (*reinterpret_cast<const std::uint64_t*>(bytes + k_step_button_offset) != 0) {
      continue;
    }
    if (*reinterpret_cast<const float*>(bytes + k_step_forward_offset) != 0.0F ||
        *reinterpret_cast<const float*>(bytes + k_step_left_offset) != 0.0F) {
      continue;
    }
    if (*reinterpret_cast<const float*>(bytes + k_step_yaw_offset) != 0.0F) {
      return true;
    }
  }
  return false;
}

// Move the just-appended last element of `base->subtick_moves` to its `when` order,
// keeping the field monotonic (the gate discards the *whole* array on a decreasing
// `when`). The engine's own button steps and the bhop pair share the field, so an
// appended strafe step may have to move before them. Never moves a step past an
// equal `when`.
void Input::sort_last_step(std::uint8_t* base, float when) noexcept {
  if (base == nullptr) {
    return;
  }
  auto* field = base + k_base_subtick_moves_offset;
  if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + sizeof(void*))) {
    return;
  }
  const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
  const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
  if (count < 2 || rep == 0) {
    return;
  }
  const auto elements = rep + k_rep_elements_offset;
  const auto size = static_cast<std::size_t>(count);
  if (!writable(elements, size * sizeof(std::uintptr_t))) {
    return;
  }
  auto* pointers = reinterpret_cast<std::uintptr_t*>(elements);
  const std::uintptr_t moved = pointers[size - 1];
  std::size_t insert = size - 1;
  for (std::size_t i = 0; i + 1 < size; ++i) {
    if (step_when(reinterpret_cast<const void*>(pointers[i])) > when) {
      insert = i;
      break;
    }
  }
  if (insert == size - 1) {
    return;
  }
  for (std::size_t k = size - 1; k > insert; --k) {
    pointers[k] = pointers[k - 1];
  }
  pointers[insert] = moved;
}

// Write the strafe's wish. In the sub-tick mode (`steps >= 1`) the analog stays on
// the valid set (`forwardmove = 1`) and the **movement basis** is rotated onto the
// wish with `steps` `yaw_delta`s (`when = i/N`, snapped to the engine's 1/64 grid)
// — exact and immune to `sv_quantize_movement_input`, and silent (`docs/re/
// strafing.md` §"The silent movement-yaw channel"). Otherwise the direction is
// carried by the camera-frame analog and snapped to the valid set. Both write the
// command floats, both button states and the raw axes `create_move_input` derives
// the command's floats from.
auto Input::write_strafe_wish(std::uint8_t* base, std::uint8_t* cmd, float forward, float left,
                              const float* deltas, int steps) noexcept -> bool {
  if (base == nullptr) {
    return false;
  }
  if (steps >= 1 && deltas != nullptr) {
    // The analog stays on the valid set; the basis rotation carries the direction.
    forward = k_move_scale;
    left = 0.0F;
    // The hook can run more than once for the same command, and the engine rebuilds
    // the table every frame — re-derive and re-append only when the rotation is not
    // already in the array (by content, like the bhop pair).
    if (!strafe_steps_present(base)) {
      auto* field = base + k_base_subtick_moves_offset;
      if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + sizeof(void*))) {
        return false;
      }
      const auto before = *reinterpret_cast<const int*>(field + k_field_size_offset);
      if (before < 0 || static_cast<std::size_t>(before) + static_cast<std::size_t>(steps) >
                            k_bhop_step_capacity) {
        return false;  // no room under the gate's budget
      }
      for (int i = 0; i < steps; ++i) {
        const float when = quantize64(static_cast<float>(i + 1) / static_cast<float>(steps));
        if (add_yaw_step(base, deltas[i], when) == nullptr) {
          return false;
        }
        sort_last_step(base, when);
      }
    }
    strafe_steps_.store(steps);
  } else {
    strafe_rot_.store(0.0F);
  }

  // The command's analog, the raw axes and both button states (the legacy
  // `cmd + 0x60` mask and `buttons_pb`, like `apply()`'s explicit move). The
  // button bits follow the engine's convention — a positive `leftmove` is the
  // player's LEFT, so it goes with `IN_MOVELEFT` (`docs/re/strafe-sign.md`
  // §4.2). The shared helper writes all three places, with every pointer
  // read inside its `writable()` guard.
  const bool written = write_move_axes(base, cmd, forward, left);
  if (written && ccsgo_input_ != 0) {
    strafe_raw_writes_.fetch_add(1);
  }
  return true;
}

// Engine prediction (`docs/re/prediction.md`): run the game's own per-command
// movement (`CPlayer_MovementServices::RunCommand`) ahead a few ticks and read
// the landing the modern-jump system records, then restore every field the
// simulation touched. The UC recipe: `SetPredictionCommand(cmd)` →
// `RunCommand(cmd)` → read `m_ModernJump` → restore.
auto Input::simulate_landing(void* cmd, int ticks, int& landed_tick, float& landed_frac,
                             int& landed_delta) noexcept -> bool {
  landed_tick = 0;
  landed_frac = 0.0F;
  landed_delta = 0;
  // **Disabled by default.** Calling the engine's `RunCommand` from the input hook
  // without the full `CPrediction` setup faults inside client.dll
  // (`client+0xa51526 read addr=0x0`) and moves the pawn. The thread's own design
  // (UC 771389) reads the engine's recorded landing instead of calling RunCommand;
  // this manual simulation is kept only behind `predict(on)` until that lands.
  if (!predict_.load()) {
    return false;
  }
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0 || cmd == nullptr) {
    return false;
  }
  // `C_BasePlayerPawn::m_pMovementServices` is a **pointer** at pawn + 0x1330.
  std::uintptr_t svc = 0;
  if (!cs2::game::read(pawn + k_movement_services_offset, svc) || svc == 0) {
    return false;
  }
  if (!writable(svc, k_movement_services_size)) {
    return false;
  }

  // Resolve the movement-services vtable slots once (UC recipe: `RunCommand`
  // 29, `SetPredictionCommand` 43, `ResetPredictionCommand` 44).
  if (run_command_ == nullptr) {
    void** vtable = nullptr;
    if (!cs2::game::read(svc, vtable) || vtable == nullptr) {
      return false;
    }
    void* run = nullptr;
    void* set_pred = nullptr;
    void* reset_pred = nullptr;
    if (!cs2::game::read(reinterpret_cast<std::uintptr_t>(&vtable[k_run_command_slot]), run) ||
        !cs2::game::read(reinterpret_cast<std::uintptr_t>(&vtable[k_set_prediction_command_slot]),
                         set_pred) ||
        !cs2::game::read(reinterpret_cast<std::uintptr_t>(&vtable[k_reset_prediction_command_slot]),
                         reset_pred) ||
        run == nullptr || set_pred == nullptr || reset_pred == nullptr) {
      return false;
    }
    run_command_ = run;
    set_prediction_command_ = set_pred;
    reset_prediction_command_ = reset_pred;
    // Only trust the slots if every resolved pointer is inside client.dll — a
    // reordered vtable must not turn into a call into garbage.
    const cs2::game::ModuleInfo module = cs2::game::find_module("client.dll");
    const auto in_module = [&module](void* pointer) -> bool {
      return module.valid() && pointer != nullptr &&
             module.contains(reinterpret_cast<std::uintptr_t>(pointer));
    };
    if (!in_module(run) || !in_module(set_pred) || !in_module(reset_pred)) {
      run_command_ = nullptr;
      set_prediction_command_ = nullptr;
      reset_prediction_command_ = nullptr;
      cs2::game::append_log(
          "[cs2-payload] prediction: vtable slots 29/43/44 are outside client.dll — "
          "disabled");
      return false;
    }
    cs2::game::append_log(
        "[cs2-payload] prediction: movement-services vtable slots 29/43/44 resolved "
        "(RunCommand / SetPredictionCommand / ResetPredictionCommand)");
  }
  if (run_command_ == nullptr || set_prediction_command_ == nullptr ||
      reset_prediction_command_ == nullptr) {
    return false;
  }

  // Snapshot the movement object (covers the modern-jump blob and every field
  // `RunCommand` mutates inside it) and the pawn's movement state.
  thread_local std::array<std::byte, k_movement_services_size> svc_copy;
  if (!cs2::game::read_bytes(svc, svc_copy.data(), svc_copy.size())) {
    return false;
  }
  std::uint32_t flags = 0;
  float velocity[3] = {0.0F, 0.0F, 0.0F};
  (void)cs2::game::read(pawn + k_pawn_flags_offset, flags);
  (void)cs2::game::read(pawn + k_pawn_velocity_offset, velocity);
  // The simulation moves the pawn too — snapshot its origin (the scene node's
  // `m_vecAbsOrigin`) so it can be restored, and read it back afterwards as the
  // predicted position.
  std::uintptr_t scene = 0;
  float origin[3] = {0.0F, 0.0F, 0.0F};
  if (cs2::game::read(pawn + k_scene_node_offset, scene) && writable(scene, 8)) {
    (void)cs2::game::read(scene + k_abs_origin_offset, origin);
  }

  // The landing recorded before the simulation (so a stale landing is not read
  // as a fresh one), and the controller tick base `RunCommand` advances.
  int before_tick = 0;
  (void)cs2::game::read(svc + k_modern_jump_offset + k_modern_jump_landed_tick_offset, before_tick);
  std::uintptr_t controller = 0;
  int base_tick = 0;
  if (client_base_ != 0 && controllers_rva_ != 0) {
    if (cs2::game::read(client_base_ + controllers_rva_, controller) && writable(controller, 8)) {
      (void)cs2::game::read(controller + k_tick_base_offset, base_tick);
    }
  }

  const auto run = reinterpret_cast<run_command_fn>(run_command_);
  const auto set_pred = reinterpret_cast<set_prediction_command_fn>(set_prediction_command_);
  const auto reset_pred = reinterpret_cast<reset_prediction_command_fn>(reset_prediction_command_);

  set_pred(reinterpret_cast<void*>(svc), cmd);
  bool found = false;
  for (int i = 0; i < ticks; ++i) {
    run(reinterpret_cast<void*>(svc), cmd);
    int tick = 0;
    float frac = 0.0F;
    if (cs2::game::read(svc + k_modern_jump_offset + k_modern_jump_landed_tick_offset, tick) &&
        cs2::game::read(svc + k_modern_jump_offset + k_modern_jump_landed_frac_offset, frac) &&
        tick != 0 && tick != before_tick) {
      landed_tick = tick;
      landed_frac = frac;
      landed_delta = tick - base_tick;
      found = true;
      break;
    }
  }
  reset_pred(reinterpret_cast<void*>(svc));

  // The position the simulation ended at — the predicted origin (read before the
  // restore overwrites it).
  float predicted[3] = {0.0F, 0.0F, 0.0F};
  if (scene != 0) {
    (void)cs2::game::read(scene + k_abs_origin_offset, predicted);
    predicted_origin_x_.store(predicted[0]);
    predicted_origin_y_.store(predicted[1]);
    predicted_origin_z_.store(predicted[2]);
    predicted_origin_valid_.store(true);
  }

  // Restore everything the simulation touched.
  std::memcpy(reinterpret_cast<void*>(svc), svc_copy.data(), svc_copy.size());
  std::memcpy(reinterpret_cast<void*>(pawn + k_pawn_flags_offset), &flags, sizeof(flags));
  std::memcpy(reinterpret_cast<void*>(pawn + k_pawn_velocity_offset), velocity, sizeof(velocity));
  if (scene != 0 && writable(scene, 8)) {
    std::memcpy(reinterpret_cast<void*>(scene + k_abs_origin_offset), origin, sizeof(origin));
  }
  if (controller != 0 && writable(controller, 8)) {
    std::memcpy(reinterpret_cast<void*>(controller + k_tick_base_offset), &base_tick,
                sizeof(base_tick));
  }

  predicted_tick_.store(landed_tick);
  predicted_frac_.store(landed_frac);
  predicted_delta_.store(landed_delta);
  predict_runs_.fetch_add(1);
  return found;
}

auto Input::predicted_origin(float out[3]) const noexcept -> bool {
  if (!predicted_origin_valid_.load()) {
    return false;
  }
  out[0] = predicted_origin_x_.load();
  out[1] = predicted_origin_y_.load();
  out[2] = predicted_origin_z_.load();
  return true;
}

void Input::set_raw_angles(float pitch, float yaw, float roll) noexcept {
  raw_angle_pitch_.store(pitch);
  raw_angle_yaw_.store(yaw);
  raw_angle_roll_.store(roll);
  raw_angles_enabled_.store(true);
}

void Input::clear_raw_angles() noexcept {
  raw_angles_enabled_.store(false);
}

void Input::clear_command_angles() noexcept {
  command_angles_enabled_.store(false);
}

void Input::set_anti_aim_config(float yaw_offset, float pitch, float jitter, int side) noexcept {
  anti_aim_yaw_offset_.store(yaw_offset);
  anti_aim_pitch_.store(anti_aim::clamp_pitch(pitch));
  anti_aim_jitter_.store(std::abs(jitter));
  // The side: 0 = automatic, +1 = left, -1 = right.
  anti_aim_side_.store(std::clamp(side, -1, 1));
}

void Input::set_move(float forward, float side, float up, bool quantize, bool step,
                     bool raw_entry) noexcept {
  move_forward_.store(forward);
  move_side_.store(side);
  move_up_.store(up);
  move_quantize_.store(quantize);
  move_step_.store(step);
  move_raw_entry_.store(raw_entry);
  move_enabled_.store(true);
}

void Input::clear_move() noexcept {
  move_enabled_.store(false);
}

void __fastcall Input::create_move_detour(void* self, int slot, void* cmd) noexcept {
  // A game thread has no TLS block for our mapped image until we install one.
  cs2::tls::ensure_thread_tls();
  Input* input = g_active;
  if (input == nullptr) {
    return;
  }
  // Let the game build/process the command, then override what it produced.
  if (input->original_ != nullptr) {
    // The local animfix's two hooks (the graph codec and the anim tick) are
    // installed **here**, on the game's thread: the inline hook's patch is not
    // atomic, and patching a function the game may be executing right now can
    // tear (see `Input::animfix_install`).
    input->animfix_install();
    // The raw per-sub-tick angle history is the *source* the PB history is built
    // from, so a write for movement steering must land before the original.
    input->write_raw_angles();
    reinterpret_cast<create_move_fn>(input->original_)(self, slot, cmd);
  }
  input->calls_.fetch_add(1);
  input->capture(cmd);
  input->apply(cmd);
}

void __fastcall Input::create_move_input_detour(void* self, int slot, char active) noexcept {
  cs2::tls::ensure_thread_tls();
  Input* input = g_active;
  if (input == nullptr) {
    return;
  }
  // The raw move axes feed `create_move_input` (it copies `CCSGOInput + 0x270`
  // into `CBaseUserCmdPB::forwardmove`): write them *before* the original so the
  // command the engine builds already carries our movement. `write_entry_move`
  // writes the input entry `FUN_180b5f7b0` derives `+0x270` from inside the
  // original, so it must also go in first (the documented next probe).
  // The ragebot solve (and the occlusion query) first: both the raw angle writes below
  // and the command's silent history (in `apply`, after the original builds it) then use
  // this tick's freshly solved aim. Runs once per tick, on the slot-5 simulated command.
  input->run_game_thread_sink_now();
  input->write_entry_move();
  input->write_raw_move();
  input->write_raw_angles();
  // The ragebot's silent aim goes into the *raw* angle history too, before the engine
  // builds the command's `input_history` (the shot's history entry), so the shot never
  // falls back to the player's own view. This does not move the camera.
  if (input->rage_active_.load()) {
    float silent_pitch = 0.0F;
    float silent_yaw = 0.0F;
    input->rage_silent_angles(silent_pitch, silent_yaw);
    input->write_raw_silent_angles(silent_pitch, silent_yaw, 0.0F);
  }
  if (input->original5_ != nullptr) {
    reinterpret_cast<create_move_input_fn>(input->original5_)(self, slot, active);
  }
  input->calls5_.fetch_add(1);
  // The per-tick entry: write the command the game will simulate (fetched via
  // the same `GetUserCmd` chain the game uses), then re-assert the raw axes in
  // case the movement reads them after `CreateMove`.
  input->apply(input->current_cmd());
  input->write_raw_move();
  input->write_raw_angles();
}

// --- the local animfix (`docs/re/animfix.md`) -----------------------------
//
// The engine's ground truth (measured on the live `client.dll`, 2026-10-21;
// the measurement is appended to `docs/re/animfix.md` as "§9 The ordering
// measurement"):
//
//   * the per-class netvar **codec** decodes the **server's** replicated AG2
//     graph inputs into the local controller's slots. Its first argument is
//     the destination controller; the decoded value lands at the **slot
//     base** (`+0x470` air height, `+0x4b8` flashed, `+0x4d0` aim pitch,
//     `+0x4e8` aim yaw — the four **float** slots it decodes);
//   * the **client's prediction** (`ReflectPawnState` → `ReflectAim` /
//     `ReflectMove`) pushes the *client's* values into the AG2 graph through
//     the parameter **setter**, for exactly those four float slots. It never
//     writes the replicated value in the slot, so **the server's value never
//     reaches the graph** — the prediction is the only writer through the
//     setter.
//
// The animfix therefore **captures** the server's values at the codec and
// **re-applies** them into the graph's parameters in the **anim tick**
// (`CAnimGraphGameSystem::AnimTickUpdate`, `0x35aeb0`), which runs *after*
// the prediction in the same tick (§9.3) and *contains* the AG2 evaluation:
// the values written there are the ones the pose is computed from. (That the
// anim tick runs after the client's prediction in the same tick is
// **inferred**, `docs/re/animfix.md` §9.3 — and the fix does not depend
// on it: the correction is written at the start of the anim tick and the
// evaluation happens inside it.)
//
// **Why not the setter** (`0x8492a0`), which is where the prediction writes
// and would be the natural seam? Its third prologue instruction is a
// RIP-relative load (`movss xmm0, [rip+0x13c0fa0]`), and the repo's inline
// relocator rewrites such a displacement as `disp + (target - trampoline)`.
// The module is mapped high under ASLR and the trampoline comes from
// `VirtualAlloc` low, so the rewrite is ~141 TB and can never fit in an
// `int32`: the hook engine bails with `unsupported_instruction` and the
// animfix would never hook anything. The anim tick's prologue is
// RIP-free, so the correction is written there instead — with the setter's
// own write replicated by hand
// (`*(float *)(*(node + 0x10)[index] + 0x18) = value`).
//
// The enum/bool slots the codec also decodes (`m_moveType`, `m_flinchHead`,
// …) are **not** covered: the prediction pushes those into the graph with
// direct stores, which no available seam intercepts. The **anim state**
// divergence (the client's own move state and turn angle) stays open too —
// see the report.

void __fastcall Input::animfix_codec_detour(void* dst, void* src, void* schema) noexcept {
  // The original runs **first**: the codec writes the server's values into
  // the slots, and the capture copies them out afterwards.
  Input* input = g_active;
  if (input == nullptr) {
    return;
  }
  reinterpret_cast<animfix_codec_fn>(input->animfix_codec_hook_.original())(dst, src, schema);
  if (!input->animfix_enabled_.load()) {
    return;
  }
  input->animfix_capture(dst);
}

void __fastcall Input::animfix_anim_tick_detour(void* self, int client_tick_flag) noexcept {
  Input* input = g_active;
  if (input == nullptr) {
    return;
  }
  // The driver runs **two** variants - the client tick and (in a listen
  // server) the server tick - and the flag says which: the engine picks its
  // VProf scope by it ("AnimGraph Server Tick" when the flag is 0, "AnimGraph
  // Client Tick" otherwise).
  //
  // The correction is for the **local player's client-side graph**, so it
  // is written on the client tick only: the server tick evaluates the
  // server's own copies of the entities, which the animfix never writes to.
  // (And the correction is re-applied on *every* client anim tick while the
  // capture is fresh, so the server's value stays in the graph between the
  // decodes - a consume-once rule would leave the client's prediction in
  // the graph for the later anim ticks of the same tick, and the pose would
  // oscillate at the server's tick rate.)
  if (client_tick_flag != 0 && input->animfix_enabled_.load()) {
    input->animfix_apply();
  }
  input->animfix_anim_ticks_.fetch_add(1);
  if (client_tick_flag != 0) {
    input->animfix_client_ticks_.fetch_add(1);
  }
  reinterpret_cast<animfix_anim_tick_fn>(input->animfix_anim_tick_hook_.original())(
      self, client_tick_flag);
}

auto Input::animfix_controller() const noexcept -> std::uintptr_t {
  // `C_CSPlayerPawn::m_AnimationState` (the `CCS2PawnGraphController*`) is
  // at `+0x34f8` (`docs/re/animfix.md` §7 step 1: `ReflectPawnState` is
  // called with `*(pawn + 0x34f8)`).
  constexpr std::size_t k_controller_offset = 0x34f8;
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return 0;
  }
  std::uintptr_t controller = 0;
  if (!cs2::game::read(pawn + k_controller_offset, controller)) {
    return 0;
  }
  return controller;
}

void Input::animfix_capture(void* controller) noexcept {
  const std::uintptr_t local = animfix_controller();
  if (local == 0 || controller != reinterpret_cast<void*>(local)) {
    return;  // another entity's controller
  }
  std::array<float, animfix::k_tracked_slot_count> values{};
  for (std::size_t i = 0; i < animfix::k_tracked_slot_count; ++i) {
    const animfix::Slot slot = animfix::k_tracked_slots[i];
    if (!cs2::game::read(local + animfix::slot_offset(slot), values[i])) {
      return;
    }
  }
  const std::uint64_t now = animfix_now_ms();
  {
    const std::lock_guard<std::mutex> lock(animfix_mutex_);
    animfix::store(animfix_capture_, local, values, now);
    // The server's snapshot rate: the animfix's staleness bound is measured
    // against it (see `animfix::snapshot_bound_ms`).
    animfix::observe_decode(animfix_clock_, now);
  }
  animfix_captures_.fetch_add(1);
  animfix_snapshot_interval_.store(animfix::snapshot_interval_ms(animfix_clock_));
  animfix_server_yaw_.store(values[animfix::slot_position(animfix::Slot::aim_yaw)]);
}

// The re-apply: the server's values into the graph's parameters.
//
// The write is the setter's own (`*(float *)(*(node + 0x10)[index] + 0x18) =
// value`), replicated here because the setter is not hookable (see the
// comment above). The parameter's **current** value is read first: that is
// the client's own prediction, and the difference between it and the
// server's value is the divergence the animfix removes — the operator's
// evidence that the feature does something.
void Input::animfix_apply() noexcept {
  animfix_acted_.store(false);
  // The write goes to the graph parameters of the **live** controller, so the
  // capture must belong to it: a local pawn that died and respawned has a new
  // controller, and the old one (with its AG2 instance) is freed. Writing
  // through the captured `(node, index)` pairs would then be a write into
  // freed memory — a corruption, which is worse than a fault.
  const std::uintptr_t controller = animfix_controller();
  const std::uint64_t now = animfix_now_ms();
  // The write targets are read from the **live** controller, not from the
  // capture: the AG2 instance inside it is a separate object, and it can be
  // re-created (an anim-graph restart, a model change) while the controller
  // survives - the captured `(node, index)` pairs would then dangle and the
  // write would land in freed memory. Reading them here also means the write
  // can only happen into a controller that answers a read right now.
  std::array<animfix::Target, animfix::k_tracked_slot_count> targets{};
  for (std::size_t i = 0; i < animfix::k_tracked_slot_count; ++i) {
    const animfix::Slot slot = animfix::k_tracked_slots[i];
    const std::size_t offset = animfix::slot_offset(slot);
    std::uintptr_t node = 0;
    if (!cs2::game::read(controller + offset + animfix::slot_node_offset(slot), node)) {
      return;
    }
    std::int16_t index = 0;
    if (!cs2::game::read(controller + offset + animfix::slot_index_offset(slot), index)) {
      return;
    }
    targets[i] = animfix::Target{node, index};
  }
  if (!animfix::targets_are_distinct(targets)) {
    // Two slots that share a graph parameter would collide: one of them would
    // silently never be corrected. Refuse the controller instead.
    count_animfix_refusal(animfix::Refusal::controller);
    animfix_skips_.fetch_add(1);
    return;
  }
  std::uint64_t bound = 0;
  {
    const std::lock_guard<std::mutex> lock(animfix_mutex_);
    bound = animfix::snapshot_bound_ms(animfix_clock_);
  }
  bool corrected = false;
  for (std::size_t i = 0; i < animfix::k_tracked_slot_count; ++i) {
    const animfix::Slot slot = animfix::k_tracked_slots[i];
    float server = 0.0F;
    {
      const std::lock_guard<std::mutex> lock(animfix_mutex_);
      const animfix::Decision decision =
          animfix::decide(animfix_capture_, slot, now, bound, controller);
      if (!decision.reapply) {
        // One refusal per anim tick: the capture-level reasons (no capture,
        // aged out, the controller changed) hold for every slot, so counting
        // them per slot would multiply them by the number of slots.
        count_animfix_refusal(decision.refusal);
        break;
      }
      server = animfix_capture_.values[animfix::slot_position(slot)];
    }
    // The graph parameter's address: node -> the parameter array -> the
    // parameter object -> the value.
    std::uintptr_t array = 0;
    if (!cs2::game::read(targets[i].node + animfix::k_node_array_offset, array) || array == 0) {
      continue;
    }
    std::uintptr_t object = 0;
    const std::uint64_t element =
        array + static_cast<std::uint64_t>(targets[i].index) * animfix::k_parameter_stride;
    if (!cs2::game::read(element, object) || object == 0) {
      continue;
    }
    const std::uintptr_t value_address = object + animfix::k_parameter_value_offset;
    // The client's own prediction — the value that is in the graph now.
    float current = 0.0F;
    if (!cs2::game::read(value_address, current)) {
      continue;
    }
    if (slot == animfix::Slot::aim_yaw && current != server) {
      // The value in the graph is the **client's own prediction** - it
      // differs from the server's. (When it does not differ, the graph
      // holds the animfix's own correction, or the two agree; either way
      // there is no divergence to report, and the last observed one is
      // kept.)
      animfix_predicted_yaw_.store(current);
      // Both numbers are known now: the server's aim yaw (from the capture)
      // and the client's predicted one (the value the graph held). Until
      // then the status says "no data" rather than "no divergence".
      animfix_data_valid_.store(true);
    }
    // The engine's own guard: a graph parameter must never hold a NaN, an
    // infinity or a value outside the engine's range. The captured value is
    // network-controlled, so the animfix has to refuse exactly what the
    // engine's setter would refuse (and count the refusals).
    if (!animfix::value_is_writable(server)) {
      animfix_rejected_.fetch_add(1);
      continue;
    }
    if (!cs2::game::write_bytes(value_address, &server, sizeof(server))) {
      continue;
    }
    animfix_applies_.fetch_add(1);
    corrected = true;
  }
  if (corrected) {
    // The **duty cycle**: the anim ticks on which the correction won. The
    // operator divides it by the client anim ticks to see the fraction of
    // frames that carry the server's value - a duty cycle below 100 % is the
    // oscillation at the server's tick rate.
    animfix_corrected_ticks_.fetch_add(1);
  } else {
    // Nothing was corrected in this anim tick: count it, with the reason of
    // the first refusal, so the operator can tell "the codec has not run
    // yet" from "every capture aged out".
    animfix_skips_.fetch_add(1);
  }
  animfix_acted_.store(corrected);
}

void Input::count_animfix_refusal(animfix::Refusal refusal) noexcept {
  switch (refusal) {
    case animfix::Refusal::no_capture:
      animfix_skip_no_capture_.fetch_add(1);
      break;
    case animfix::Refusal::stale:
      animfix_skip_stale_.fetch_add(1);
      break;
    case animfix::Refusal::controller:
      animfix_skip_controller_.fetch_add(1);
      break;
    case animfix::Refusal::not_tracked:
    case animfix::Refusal::none:
      break;
  }
}

auto Input::animfix_aim_yaw_delta() const noexcept -> float {
  return animfix::aim_yaw_difference(animfix_server_yaw_.load(), animfix_predicted_yaw_.load());
}

auto Input::animfix_status() const noexcept -> std::string {
  const std::lock_guard<std::mutex> lock(animfix_mutex_);
  return animfix_status_;
}

void Input::set_animfix_status(std::string status) noexcept {
  const std::lock_guard<std::mutex> lock(animfix_mutex_);
  animfix_status_ = std::move(status);
}

// The animfix's hooks are installed **on the game's thread** (from the
// CreateMove detour), not from the control thread's `refresh()`.
//
// The inline hook patches the target with a 14-byte write that is not atomic
// (`InlineHook::install` copies the jump in with a `memcpy`). A patch that
// lands while another thread is *executing* the target can tear: the thread
// would fetch a mixture of the old prologue and the new jump. The anim tick
// is the client's tick - it runs on the main thread - so patching it from
// the control thread would race with the main thread's own execution of it.
//
// The CreateMove detour also runs on the main thread, and it is *not* inside
// the anim tick, so patching from there means no thread can be executing the
// patched function: the window is a single write on the thread that owns
// the function.
//
// (The residual: `InlineHook::original()` is read by the detour while the
// installing thread wrote it. The hook engine publishes the trampoline
// *before* it patches the target, precisely so the detour always finds it;
// the write is a plain pointer store, which is atomic on x86.)
void Input::animfix_install() noexcept {
  if (animfix_codec_hook_.installed() && animfix_anim_tick_hook_.installed()) {
    return;
  }
  const cs2::game::ModuleInfo module = cs2::game::find_module("client.dll");
  if (!module.valid()) {
    set_animfix_status("the client module is not resolved yet");
    return;
  }
  const OffsetInfo* codec = find_offset("animfix_graph_codec");
  const OffsetInfo* anim_tick = find_offset("animfix_anim_tick");
  if (codec == nullptr || anim_tick == nullptr) {
    // The offsets come from the generated SDK, which is rendered from the
    // **calibration dump** — a build-specific, gitignored artifact. A dump
    // that predates this feature has no entry for it, and the animfix would
    // then be silently inert. Say so, loudly, in the log and in the status.
    const std::string missing = codec == nullptr ? "animfix_graph_codec" : "animfix_anim_tick";
    set_animfix_status("the calibration dump has no '" + missing +
                       "' signature (re-run scripts/calibrate-live.py to refresh it)");
    cs2::game::append_log("[cs2-payload] animfix: " + animfix_status());
    return;
  }
  if (codec->module != "client.dll" || anim_tick->module != "client.dll") {
    set_animfix_status("the animfix's signatures are not from client.dll");
    cs2::game::append_log("[cs2-payload] animfix: " + animfix_status());
    return;
  }
  if (!animfix_anim_tick_hook_.installed()) {
    const auto error =
        animfix_anim_tick_hook_.install(reinterpret_cast<void*>(module.base + anim_tick->rva),
                                        reinterpret_cast<void*>(&Input::animfix_anim_tick_detour));
    if (error.has_value()) {
      set_animfix_status(std::string("the anim tick is not hookable: ") +
                         cs2::hooks::to_string(*error));
      cs2::game::append_log("[cs2-payload] animfix: " + animfix_status());
      return;
    }
  }
  if (!animfix_codec_hook_.installed()) {
    const auto error =
        animfix_codec_hook_.install(reinterpret_cast<void*>(module.base + codec->rva),
                                    reinterpret_cast<void*>(&Input::animfix_codec_detour));
    if (error.has_value()) {
      set_animfix_status(std::string("the graph codec is not hookable: ") +
                         cs2::hooks::to_string(*error));
      cs2::game::append_log("[cs2-payload] animfix: " + animfix_status());
      return;
    }
  }
  set_animfix_status("ok");
  cs2::game::append_log("[cs2-payload] animfix: the graph codec and the anim tick are hooked");
}

auto Input::current_cmd() noexcept -> void* {
  if (client_base_ == 0 || tick_ == nullptr || array_ == nullptr || sequence_ == nullptr) {
    return nullptr;
  }
  const auto controller = *reinterpret_cast<std::uintptr_t*>(client_base_ + controllers_rva_);
  if (!writable(controller, 8)) {
    return nullptr;
  }
  int tick = 0;
  tick_(reinterpret_cast<void*>(controller), &tick);
  int index = tick - 1;
  index = std::max(index, -1);
  const auto array_base = *reinterpret_cast<std::uintptr_t*>(client_base_ + user_cmd_array_rva_);
  if (!writable(array_base, 8)) {
    return nullptr;
  }
  auto* array = static_cast<std::uint8_t*>(array_(reinterpret_cast<void*>(array_base), index));
  if (array == nullptr ||
      !writable(reinterpret_cast<std::uintptr_t>(array), k_user_cmd_array_sequence_offset + 4)) {
    return nullptr;
  }
  const auto sequence = *reinterpret_cast<std::uint32_t*>(array + k_user_cmd_array_sequence_offset);
  auto* cmd = static_cast<std::uint8_t*>(sequence_(reinterpret_cast<void*>(controller), sequence));
  if (cmd == nullptr ||
      !writable(reinterpret_cast<std::uintptr_t>(cmd), k_cmd_buttons_offset + 8)) {
    return nullptr;
  }
  return cmd;
}

void Input::capture(void* cmd) noexcept {
  if (cmd == nullptr) {
    return;
  }
  last_cmd_.store(reinterpret_cast<std::uintptr_t>(cmd), std::memory_order_relaxed);
  const auto* words = static_cast<const std::uint64_t*>(cmd);
  for (std::size_t i = 0; i < k_probe_words; ++i) {
    probe_[i].store(words[i], std::memory_order_relaxed);
  }
  const auto base = *reinterpret_cast<std::uintptr_t*>(static_cast<std::uint8_t*>(cmd) +
                                                       k_cmd_pb_offset + k_pb_base_offset);
  last_base_.store(base, std::memory_order_relaxed);
  if (writable(base, k_probe_base_words * sizeof(std::uint64_t))) {
    const auto* base_words = reinterpret_cast<const std::uint64_t*>(base);
    for (std::size_t i = 0; i < k_probe_base_words; ++i) {
      base_probe_[i].store(base_words[i], std::memory_order_relaxed);
    }
  }
  // The engine's own sub-tick steps on this command (`base->subtick_moves`):
  // read them back so the probe can learn the convention a real `+forward` or
  // jump produces before synthesising movement (`docs/re/subtick.md`).
  step_count_.store(0, std::memory_order_relaxed);
  const auto field = base + k_base_subtick_moves_offset;
  if (base != 0 && writable(field, k_field_rep_offset + sizeof(void*))) {
    const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
    const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
    step_count_.store(count > 0 ? count : 0, std::memory_order_relaxed);
    if (count > 0 && rep != 0 && writable(rep, k_rep_elements_offset + sizeof(void*))) {
      const auto elements = rep + k_rep_elements_offset;
      for (std::size_t i = 0; i < k_probe_steps; ++i) {
        if (static_cast<int>(i) >= count) {
          break;
        }
        const auto step = *reinterpret_cast<const std::uintptr_t*>(elements + i * 8);
        if (step == 0 || !writable(step, k_step_left_offset + sizeof(float))) {
          continue;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(step);
        step_when_[i].store(*reinterpret_cast<const float*>(bytes + k_step_when_offset),
                            std::memory_order_relaxed);
        step_forward_[i].store(*reinterpret_cast<const float*>(bytes + k_step_forward_offset),
                               std::memory_order_relaxed);
        step_left_[i].store(*reinterpret_cast<const float*>(bytes + k_step_left_offset),
                            std::memory_order_relaxed);
        step_button_[i].store(*reinterpret_cast<const std::uint64_t*>(bytes + k_step_button_offset),
                              std::memory_order_relaxed);
        step_pressed_[i].store(
            *reinterpret_cast<const std::uint8_t*>(bytes + k_step_pressed_offset) != 0,
            std::memory_order_relaxed);
      }
    }
  }
}

auto Input::last_word(std::size_t index) const noexcept -> std::uint64_t {
  return index < k_probe_words ? probe_[index].load(std::memory_order_relaxed) : 0;
}

auto Input::last_base_word(std::size_t index) const noexcept -> std::uint64_t {
  return index < k_probe_base_words ? base_probe_[index].load(std::memory_order_relaxed) : 0;
}

auto Input::base_has_bits() const noexcept -> std::uint32_t {
  std::uint32_t bits = 0;
  const auto base = last_base_.load(std::memory_order_relaxed);
  return (base != 0 && cs2::game::read(base + k_base_has_bits_offset, bits)) ? bits : 0;
}

auto Input::base_move() const noexcept -> MoveSample {
  return read_move_sample(last_base_.load(std::memory_order_relaxed) + k_base_move_offset);
}

auto Input::raw_move() const noexcept -> MoveSample {
  return read_move_sample(ccsgo_input_ + k_input_move_offset);
}

auto Input::entry_move() const noexcept -> MoveSample {
  return read_move_sample(ccsgo_input_ + k_input_entry_array_offset + k_input_entry_move_offset);
}

auto Input::step_probe(std::size_t index) const noexcept -> StepProbe {
  StepProbe probe;
  if (index >= k_probe_steps) {
    return probe;
  }
  probe.when = step_when_[index].load(std::memory_order_relaxed);
  probe.forward = step_forward_[index].load(std::memory_order_relaxed);
  probe.left = step_left_[index].load(std::memory_order_relaxed);
  probe.button = step_button_[index].load(std::memory_order_relaxed);
  probe.pressed = step_pressed_[index].load(std::memory_order_relaxed);
  probe.valid = true;
  return probe;
}

auto Input::alloc_step(void* base) noexcept -> void* {
  if (base == nullptr || alloc_step_ == nullptr || add_step_ == nullptr) {
    return nullptr;
  }
  auto* field = static_cast<std::uint8_t*>(base) + k_base_subtick_moves_offset;
  if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + 8)) {
    return nullptr;
  }
  const auto current = *reinterpret_cast<int*>(field + k_field_size_offset);
  auto* rep = *reinterpret_cast<void**>(field + k_field_rep_offset);
  void* step = nullptr;
  if (rep != nullptr &&
      writable(reinterpret_cast<std::uintptr_t>(rep), k_rep_elements_offset + 8)) {
    const auto allocated =
        *reinterpret_cast<int*>(static_cast<std::uint8_t*>(rep) + k_rep_allocated_offset);
    if (current < allocated) {  // reuse a spare pre-allocated slot
      step = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(rep) + k_rep_elements_offset +
                                       current * 8);
      *reinterpret_cast<int*>(field + k_field_size_offset) = current + 1;
    }
  }
  if (step == nullptr) {
    auto* arena = *reinterpret_cast<void**>(field + k_field_arena_offset);
    step = alloc_step_(arena);
    if (step == nullptr) {
      return nullptr;
    }
    add_step_(field, step);
  }
  return step;
}

auto Input::add_yaw_step(void* base, float yaw_delta, float when) noexcept -> void* {
  void* step = alloc_step(base);
  if (step == nullptr) {
    return nullptr;
  }
  auto* bytes = static_cast<std::uint8_t*>(step);
  // A yaw-only step: timing + the angle deltas, no button, no analog (UC 769450's
  // reply is explicit that a yaw-delta step must not carry button/analog data).
  *reinterpret_cast<std::uint64_t*>(bytes + k_step_button_offset) = 0;
  *reinterpret_cast<std::uint8_t*>(bytes + k_step_pressed_offset) = 0;
  *reinterpret_cast<float*>(bytes + k_step_when_offset) = when;
  *reinterpret_cast<float*>(bytes + k_step_forward_offset) = 0.0F;
  *reinterpret_cast<float*>(bytes + k_step_left_offset) = 0.0F;
  *reinterpret_cast<float*>(bytes + k_step_pitch_offset) = 0.0F;
  *reinterpret_cast<float*>(bytes + k_step_yaw_offset) = yaw_delta;
  *reinterpret_cast<std::uint32_t*>(bytes + k_step_has_bits_offset) = k_step_angle_bits;
  return step;
}

auto Input::add_button_step(void* base, std::uint64_t button, bool pressed, float when) noexcept
    -> void* {
  auto* step = static_cast<std::uint8_t*>(alloc_step(base));
  if (step == nullptr) {
    return nullptr;
  }
  // A button-only step: the press/release, its timing, and nothing else.
  *reinterpret_cast<std::uint64_t*>(step + k_step_button_offset) = button;
  *reinterpret_cast<std::uint8_t*>(step + k_step_pressed_offset) = pressed ? 1 : 0;
  *reinterpret_cast<float*>(step + k_step_when_offset) = when;
  *reinterpret_cast<float*>(step + k_step_forward_offset) = 0.0F;
  *reinterpret_cast<float*>(step + k_step_left_offset) = 0.0F;
  *reinterpret_cast<float*>(step + k_step_pitch_offset) = 0.0F;
  *reinterpret_cast<float*>(step + k_step_yaw_offset) = 0.0F;
  *reinterpret_cast<std::uint32_t*>(step + k_step_has_bits_offset) = k_step_button_bits;
  return step;
}

auto Input::controller() const noexcept -> std::uintptr_t {
  if (client_base_ == 0 || controllers_rva_ == 0) {
    return 0;
  }
  std::uintptr_t value = 0;
  return cs2::game::read(client_base_ + controllers_rva_, value) ? value : 0;
}

// The record-based sub-tick bunnyhop (`docs/re/research-bhop.md`): a **post**
// hook on `CreateMove` that only *reads* engine state — the landing record the
// engine's own prediction wrote into `m_ModernJump`, plus the command's build
// tick — and appends a release+press pair to the command's sub-tick table.
void Input::bhop_tick(void* cmd, std::uint8_t* base, std::uint64_t buttons) noexcept {
  bhop_mode_.store(0);
  if (!bhop_.load() || cmd == nullptr || base == nullptr) {
    return;
  }
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return;
  }
  std::uintptr_t services = 0;
  if (!cs2::game::read(pawn + k_movement_services_offset, services) || services == 0) {
    return;
  }
  // The command's own number: `fresh` (a command the engine allocated this frame
  // and has not simulated yet) is the phase correction the build tick needs.
  // Sampled once per (command, landing) — the hook can run more than once per
  // frame, and re-sampling would flip the phase between the calls.
  const auto number = *reinterpret_cast<const std::uint32_t*>(
      static_cast<const std::uint8_t*>(cmd) + k_cmd_number_offset);
  const bool fresh = number != bhop_prev_number_;
  bhop_prev_number_ = number;
  bhop_cmd_number_.store(static_cast<int>(number));

  // The assist only acts while the player actually holds jump: the engine's own
  // button state (the `CInButtonState` word 0, the `IN_JUMP` bit) and the
  // already-settled command mask are both reads — the pair below is the only
  // write, so a held jump never becomes a whole-tick press.
  std::uint64_t engine_buttons = 0;
  (void)cs2::game::read(services + k_services_button_offset, engine_buttons);
  if (((engine_buttons & button::k_jump) == 0) && ((buttons & button::k_jump) == 0)) {
    bhop_active_number_ = 0;
    bhop_active_landed_ = 0;
    return;
  }
  bhop_holding_ticks_.fetch_add(1);
  if (!on_ground()) {
    bhop_air_ticks_.fetch_add(1);
  }

  const std::uintptr_t jump = services + k_modern_jump_offset;
  int landed_tick = 0;
  float landed_frac = 0.0F;
  if (!cs2::game::read(jump + k_modern_jump_landed_tick_offset, landed_tick) ||
      !cs2::game::read(jump + k_modern_jump_landed_frac_offset, landed_frac) || landed_tick == 0 ||
      landed_frac < 0.0F || landed_frac > 1.0F) {
    bhop_active_number_ = 0;
    bhop_active_landed_ = 0;
    return;
  }
  bhop_landed_tick_.store(landed_tick);
  bhop_landed_frac_.store(landed_frac);

  // The command's build tick. `RunCommand` advances the controller's tick base
  // once per simulated command ("client advancing tick base to %u for command
  // %d"), so a freshly allocated command reads one behind it; the record itself
  // is written in `{build - 1, f}` coordinates, which is why the target is
  // `build == landed_tick + 1`. The command's own `client_tick` (the same counter
  // sampled at build time) and the raw tick base are kept as diagnostics for the
  // phase — offline they should differ by exactly the `fresh` term.
  const std::uintptr_t ctrl = controller();
  int tick_base = 0;
  if (ctrl != 0) {
    (void)cs2::game::read(ctrl + k_tick_base_offset, tick_base);
  }
  int client_tick = 0;
  (void)cs2::game::read(reinterpret_cast<std::uintptr_t>(base) + k_base_client_tick_offset,
                        client_tick);
  const int build = tick_base + (fresh ? 1 : 0);
  bhop_tick_base_.store(tick_base);
  bhop_client_tick_.store(client_tick);
  bhop_build_.store(build);
  bhop_age_.store(build - landed_tick);

  float when = 0.0F;
  if (number == bhop_active_number_ && landed_tick == bhop_active_landed_) {
    // Already decided for this command/landing (an earlier call this frame): keep
    // the fraction, so the re-write below is byte-identical.
    when = bhop_active_when_;
    bhop_when_.store(when);
    bhop_mode_.store(bhop_active_mode_);
  } else {
    const float contact = quantize64(landed_frac);
    int mode = 0;
    if (build == landed_tick + 1) {
      // A: the pair goes into the very command the landing was recorded from.
      // `f == 0` is the normalised boundary landing, whose record is readable one
      // command early — its press stands at `offset/64` from the tick start.
      when = landed_frac == 0.0F
                 ? k_bhop_offset
                 : std::clamp(contact + k_bhop_offset, contact + k_subtick_grid, k_bhop_last_when);
      mode = 1;
    } else if (build > landed_tick + 1 && build <= landed_tick + 1 + k_bhop_catchup_ticks) {
      // B: the command is one or more ticks past the contact. A whole tick late
      // takes `DoJump`'s beta path (a hop, but the ground-slice clamp keeps the
      // speed loss); the catch-up window keeps the array from ever starving.
      when =
          std::clamp(contact + k_bhop_offset + k_subtick_grid, k_bhop_first_when, k_bhop_last_when);
      mode = 2;
    }
    if (when < k_bhop_first_when || when > k_bhop_last_when) {
      bhop_skipped_.fetch_add(1);
      bhop_active_number_ = 0;
      bhop_active_landed_ = 0;
      return;
    }
    if (mode == 1) {
      bhop_a_.fetch_add(1);
    } else {
      bhop_b_.fetch_add(1);
    }
    bhop_active_number_ = number;
    bhop_active_landed_ = landed_tick;
    bhop_active_when_ = when;
    bhop_active_mode_ = mode;
    bhop_when_.store(when);
    bhop_mode_.store(mode);
  }

  // The command's sub-tick table is rebuilt from scratch every frame (`CreateMove`
  // runs per frame and `CUserCmd::Reset` clears it), so the pair must be
  // re-derived and re-appended on **every** frame. The idempotency check is on
  // the live array, not on "already handled this tick" — that is what makes it
  // safe: when the engine clears the table the pair is gone and gets written
  // again, and when it is still there nothing is appended twice.
  if (bhop_pair_present(base, when)) {
    return;
  }
  if (write_bhop_pair(base, when)) {
    bhop_writes_.fetch_add(1);
    bhop_presses_.fetch_add(1);
  }
}

auto Input::bhop_pair_present(std::uint8_t* base, float when) const noexcept -> bool {
  if (base == nullptr) {
    return false;
  }
  auto* field = base + k_base_subtick_moves_offset;
  if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + sizeof(void*))) {
    return false;
  }
  const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
  const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
  if (count <= 0 || rep == 0 || static_cast<std::size_t>(count) > k_bhop_step_capacity) {
    return false;
  }
  const auto elements = rep + k_rep_elements_offset;
  bool release = false;
  bool press = false;
  for (int i = 0; i < count; ++i) {
    std::uintptr_t step = 0;
    if (!cs2::game::read(elements + static_cast<std::uintptr_t>(i) * 8, step) || step == 0 ||
        !writable(step, k_step_pressed_offset + 8)) {
      continue;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(step);
    if (*reinterpret_cast<const std::uint64_t*>(bytes + k_step_button_offset) != button::k_jump ||
        *reinterpret_cast<const float*>(bytes + k_step_when_offset) != when) {
      continue;
    }
    if (*reinterpret_cast<const std::uint8_t*>(bytes + k_step_pressed_offset) != 0) {
      press = true;
    } else {
      release = true;
    }
  }
  return release && press;
}

auto Input::write_bhop_pair(std::uint8_t* base, float when) noexcept -> bool {
  if (base == nullptr || alloc_step_ == nullptr || add_step_ == nullptr) {
    return false;
  }
  auto* field = base + k_base_subtick_moves_offset;
  if (!writable(reinterpret_cast<std::uintptr_t>(field), k_field_rep_offset + sizeof(void*))) {
    return false;
  }
  const auto before = *reinterpret_cast<const int*>(field + k_field_size_offset);
  // The admission gate discards the whole sub-tick array above 32 entries, and
  // the engine's own steps live in the same field — refuse rather than break the
  // command.
  if (before < 0 || static_cast<std::size_t>(before) + 2 > k_bhop_step_capacity) {
    return false;
  }
  if (add_button_step(base, button::k_jump, false, when) == nullptr ||
      add_button_step(base, button::k_jump, true, when) == nullptr) {
    return false;
  }
  // Keep the field monotonic: the gate also kills the array on a decreasing
  // `when`, so insert the two appended steps at their sorted position (the
  // engine's own steps are `when = 0` button transitions, ours may sit before a
  // trailing e.g. yaw-delta step at `when = 1`).
  const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
  const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
  if (count < 2 || rep == 0) {
    return true;
  }
  const auto elements = rep + k_rep_elements_offset;
  const auto size = static_cast<std::size_t>(count);
  if (!writable(elements, size * sizeof(std::uintptr_t))) {
    return true;
  }
  auto* pointers = reinterpret_cast<std::uintptr_t*>(elements);
  const std::uintptr_t release = pointers[size - 2];
  const std::uintptr_t press = pointers[size - 1];
  std::size_t insert = size - 2;
  for (std::size_t i = 0; i + 2 < size; ++i) {
    if (step_when(reinterpret_cast<const void*>(pointers[i])) > when) {
      insert = i;
      break;
    }
  }
  if (insert != size - 2) {
    for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(size) - 1;
         k >= static_cast<std::ptrdiff_t>(insert) + 2; --k) {
      pointers[k] = pointers[k - 2];
    }
    pointers[insert] = release;
    pointers[insert + 1] = press;
  }
  return true;
}

void Input::write_raw_angles() noexcept {
  if (!raw_angles_enabled_.load() || ccsgo_input_ == 0) {
    return;
  }
  std::int32_t count = 0;
  std::uintptr_t rep = 0;
  if (!cs2::game::read(ccsgo_input_ + k_input_history_count_offset, count) ||
      !cs2::game::read(ccsgo_input_ + k_input_history_rep_offset, rep)) {
    return;
  }
  if (count <= 0 || count > 64 || rep == 0) {
    return;
  }
  const float pitch = raw_angle_pitch_.load();
  const float yaw = raw_angle_yaw_.load();
  const float roll = raw_angle_roll_.load();
  for (int i = 0; i < count; ++i) {
    const auto entry = rep + static_cast<std::uintptr_t>(i) * k_input_history_stride;
    if (!writable(entry + k_input_history_angles_offset, 3 * sizeof(float))) {
      break;
    }
    auto* xyz = reinterpret_cast<float*>(entry + k_input_history_angles_offset);
    xyz[0] = pitch;
    xyz[1] = yaw;
    xyz[2] = roll;
    raw_angle_writes_.fetch_add(1);
  }
}

auto Input::add_analog_step(void* base, float forward, float side, float when) noexcept -> void* {
  auto* step = static_cast<std::uint8_t*>(alloc_step(base));
  if (step == nullptr) {
    return nullptr;
  }
  *reinterpret_cast<std::uint64_t*>(step + k_step_button_offset) = 0;
  *reinterpret_cast<std::uint8_t*>(step + k_step_pressed_offset) = 0;
  *reinterpret_cast<float*>(step + k_step_when_offset) = when;
  *reinterpret_cast<float*>(step + k_step_forward_offset) = forward;
  *reinterpret_cast<float*>(step + k_step_left_offset) = side;
  // Declare exactly the three fields we filled ({when, forward, left}). The
  // community's `CheckAndSetBits` relies on a fresh (zeroed) element; a reused
  // spare can still carry last tick's `button`/`pressed` bits, so *assign* rather
  // than OR to avoid advertising stale fields (UC 746996 / 769450).
  *reinterpret_cast<std::uint32_t*>(step + k_step_has_bits_offset) = k_step_bits;
  return step;
}

void Input::write_raw_move() noexcept {
  if (ccsgo_input_ == 0) {
    return;
  }
  auto* move = reinterpret_cast<float*>(ccsgo_input_ + k_input_move_offset);
  if (!writable(reinterpret_cast<std::uintptr_t>(move), 3 * sizeof(float))) {
    return;
  }
  // Ragebot auto-stop: zero the raw axes *before* the engine's `create_move_input`
  // copies them into the command (this is the source the engine derives the move from),
  // so the player's own WASD cannot move the shot. Runs even when the move feature is
  // off, hence before the `move_enabled_` check.
  if (rage_active_.load() && rage_stop_.load()) {
    move[0] = 0.0F;
    move[1] = 0.0F;
    move[2] = 0.0F;
    raw_writes_.fetch_add(1);
    return;
  }
  if (!move_enabled_.load()) {
    return;
  }
  float axes[3];
  resolve_axes(move_forward_.load(), move_side_.load(), move_up_.load(), move_quantize_.load(),
               axes);
  move[0] = axes[0];
  move[1] = axes[1];
  move[2] = axes[2];
  raw_writes_.fetch_add(1);
}

void Input::write_entry_move() noexcept {
  if (!move_enabled_.load() || !move_raw_entry_.load() || ccsgo_input_ == 0) {
    return;
  }
  float axes[3];
  resolve_axes(move_forward_.load(), move_side_.load(), move_up_.load(), move_quantize_.load(),
               axes);
  // Entry 0 is the local player (`FUN_180b5f7b0(input, 0)`), whose move pair
  // `FUN_180b5f7b0` copies into `CCSGOInput + 0x270` *inside* slot 5 — so this
  // write must land before the original runs to survive that copy.
  //
  // **UNSAFE — do not enable.** Writing here crashed the game with an access
  // violation inside the input path (`client.dll + 0xb64a80`, next to
  // `create_move_input`) on 2026-10-15: the `+0xb58`/stride-`0x440`/`+0x18`
  // layout taken from the older notes does not hold for this build, so the write
  // corrupts the input state. The flag exists only so the layout can be
  // re-derived and retried (`docs/re/subtick.md` §"Actionable seams").
  const auto entry = ccsgo_input_ + k_input_entry_array_offset + k_input_entry_move_offset;
  auto* move = reinterpret_cast<float*>(entry);
  if (!writable(entry, 3 * sizeof(float))) {
    return;
  }
  move[0] = axes[0];
  move[1] = axes[1];
  move[2] = axes[2];
  entry_writes_.fetch_add(1);
}

void Input::write_move_steps(void* base, float forward, float side) noexcept {
  if (base == nullptr || alloc_step_ == nullptr || add_step_ == nullptr) {
    return;
  }
  // Nothing the base move does not already carry: appending zero-delta steps
  // would only crowd the repeated field (the engine's own button steps live in
  // it — UC 769450).
  if (std::fabs(forward) < k_move_epsilon && std::fabs(side) < k_move_epsilon) {
    return;
  }
  // The sub-tick analog channel. The engine *adds* each step's delta on top of
  // the command's base move, so the delta is relative to what the base already
  // carries (UC 761983 / 769450; `docs/re/subtick.md` §"The movement step") — we
  // pass the residual the quantised base could not express, so an
  // `sv_quantize_movement_input` server still sees the fractional part.
  //
  // Existing steps are left in place: the engine's own button steps (a real jump
  // press/release) live in the same repeated field, and clearing it breaks them
  // (UC 769450). 12 sub-ticks per tick is the engine's own decode size.
  constexpr int k_subticks = 12;
  for (int i = 0; i < k_subticks; ++i) {
    const auto when = static_cast<float>(i) / static_cast<float>(k_subticks);
    if (add_analog_step(base, forward, side, when) == nullptr) {
      break;
    }
    step_writes_.fetch_add(1);
  }
}

void Input::run_game_thread_sink_now() noexcept {
  if (game_thread_sink_ != nullptr) {
    game_thread_sink_(game_thread_context_);
  }
}

void Input::apply(void* cmd) noexcept {
  if (cmd == nullptr) {
    return;
  }
  // The **control commands** (`press`/`move`/`look`) are a backend choice: in the
  // `console` backend the runner drives the engine's own `+button`/`setang`
  // commands through `exec`, so the command must be left untouched here. The
  // **payload features** — the `bhop` jump assist, the auto-`strafe` and the
  // triggerbot's fire — are owned by the overlay menu (`wire::Settings`,
  // ADR-0021) and have no console equivalent, so they must act regardless of
  // the backend. Gating *them* on `usercmd` is why enabling them in the menu did
  // nothing (the menu cannot select the backend): measured live 2026-10-15,
  // `strafe`/`bhop` flags true but
  // `writes = move_writes = strafe_writes = 0` until `input(via="usercmd")`.
  //
  // The gate itself is `cs2::control::button_reaches_command`, a pure function
  // the host test pins: the triggerbot's press used to travel through the
  // control channel's mask, so in the default `console` backend it decided to
  // fire, counted the shot and answered `{"firing": true}` while the gun
  // stayed silent.
  const bool usercmd_backend = backend_ == Backend::usercmd;
  const bool control = cs2::control::button_reaches_command(
      cs2::control::ButtonPath::control_command, usercmd_backend);
  auto* bytes = static_cast<std::uint8_t*>(cmd);

  // 1. Legacy button mask (`CInButtonState`). The bhop assist runs after the mask
  //    is settled, as a separate record-based step (below); it *reads* the held
  //    state and only writes the sub-tick table.
  auto* buttons = reinterpret_cast<std::uint64_t*>(bytes + k_cmd_buttons_offset);
  const std::uint64_t current = *buttons;
  // The features' buttons: the triggerbot's own hold, plus the ragebot's
  // assists. The ragebot's attack is a **persistent** press while a solve is
  // active — a payload feature (like the movement features) that must hold on
  // *every* command the engine simulates, not just the command `rage_tick`
  // happens to see. Verified subtlety: pressing attack only on the command whose
  // history we wrote that tick let a shot be released on a command the silent
  // write had not reached, so it followed the camera (the auto-fire timing
  // bug). A persistent press is exactly why manual `press("attack")` always hit.
  const bool rage_solved = rage_active_.load();
  const bool rage_firing = rage_solved && rage_fire_.load();
  std::uint64_t feature_bits = feature_mask_.load();
  if (rage_firing) {
    feature_bits |= button::k_attack;
  }
  // Auto-scope / auto-reload (common ragebot assists, `docs/ragebot.md`): hold zoom
  // and/or reload while a solve is active. The engine ignores zoom on a weapon that
  // cannot scope; reload is a no-op when the clip is full.
  // The scope is triggered by `IN_ATTACK2` (the right mouse); `IN_ZOOM` is the
  // engine's own zoom bit (`1 << 34`) and no client action produces it, so it
  // only states the scope the attack2 press has already asked for.
  if (rage_solved && rage_scope_.load()) {
    feature_bits |= button::k_attack2 | button::k_zoom;
  }
  if (rage_solved && rage_reload_.load()) {
    feature_bits |= button::k_reload;
  }
  const std::uint64_t value = cs2::control::compose_button_mask(
      current, set_mask_.load(), clear_mask_.load(), feature_bits, usercmd_backend);
  if (value != current) {
    *buttons = value;
    writes_.fetch_add(1);
  }
  bhop_tick(cmd,
            reinterpret_cast<std::uint8_t*>(
                *reinterpret_cast<std::uintptr_t*>(bytes + k_cmd_pb_offset + k_pb_base_offset)),
            value);

  // 2. Analog move. Two things can swallow a command-side write: the engine
  //    re-derives `base->forwardmove` from the raw input (`CCSGOInput + 0x270`,
  //    fed from the per-controller input entry) inside slot 5, and
  //    `sv_quantize_movement_input` (on by default on Valve servers) rounds the
  //    two floats to `{-1, 0, 1}`, so a fractional value never reaches the
  //    server. We therefore (a) write the axes resolved to the valid set in
  //    `quantize` mode, (b) mirror the direction into both button states
  //    (`cmd + 0x60` and `buttons_pb` state1/state2), and (c) in `step` mode push
  //    whatever the base cannot express onto the sub-tick analog channel.
  if (control && move_enabled_.load()) {
    const auto base =
        *reinterpret_cast<std::uintptr_t*>(bytes + k_cmd_pb_offset + k_pb_base_offset);
    if (writable(base, k_base_move_offset + 12)) {
      float axes[3];
      resolve_axes(move_forward_.load(), move_side_.load(), move_up_.load(), move_quantize_.load(),
                   axes);
      auto* move = reinterpret_cast<float*>(base + k_base_move_offset);
      move[0] = axes[0];
      move[1] = axes[1];
      move[2] = axes[2];
      *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_move_bits;

      constexpr std::uint64_t move_bits =
          button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
      // The button bits follow the engine's convention — a positive side axis
      // is the player's LEFT (`cs2::control::engine_move_buttons`).
      const std::uint64_t held = *buttons & ~move_bits;
      *buttons = held | cs2::control::engine_move_buttons(move[0], move[1], k_move_epsilon);

      const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
      if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
        auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
        state[0] = (state[0] & ~move_bits) | (held & move_bits);  // buttonstate1: held
        state[1] = (state[1] & ~move_bits) | (held & move_bits);  // buttonstate2: changed
        *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3U;
      }
      if (move_step_.load()) {
        write_move_steps(reinterpret_cast<void*>(base), move_forward_.load() - axes[0],
                         move_side_.load() - axes[1]);
      }
      if (step_yaw_enabled_.load()) {
        // One yaw-delta step per tick (`when = 1`, no button/analog): the silent
        // movement-yaw candidate (`docs/re/subtick.md` §"The movement yaw channel").
        add_yaw_step(reinterpret_cast<void*>(base), step_yaw_.load(), 1.0F);
      }
      move_writes_.fetch_add(1);
    }
  }

  // 3. Auto-strafe (see `set_strafe`): airborne only, silent (sub-tick `yaw_delta`
  //    steps + full-forward analog in the rotated frame). Runs after the explicit
  //    move write so it owns the airborne case.
  const auto strafe_base =
      *reinterpret_cast<std::uintptr_t*>(bytes + k_cmd_pb_offset + k_pb_base_offset);
  const bool rage_stopping = rage_active_.load() && rage_stop_.load();
  if (rage_stopping) {
    // Ragebot auto-stop. With `rage_stop_speed_ == 0` it holds still (maximum accuracy);
    // with a speed limit it **counter-strafes** only until the horizontal speed is at or
    // below the limit — the fastest movement that does not add spread — instead of
    // stopping outright. Every channel the engine moves from is written: the command's
    // base move + both button states, the raw axes source (`CCSGOInput + 0x270`, also
    // pre-original in `write_raw_move`) and the engine's own held-button word.
    constexpr std::uint64_t move_bits =
        button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;

    const float limit = rage_stop_speed_.load();
    float vx = 0.0F;
    float vy = 0.0F;
    float speed = 0.0F;
    if (limit > 0.0F && velocity(vx, vy)) {
      speed = std::sqrt(vx * vx + vy * vy);
    }
    const bool brake = limit > 0.0F && speed > limit;
    // The move axes: the counter-wish when braking, else zero (hold still).
    //
    // The counter-wish is the direction that opposes the player's velocity,
    // expressed in the aim's frame: `rel` is its bearing from the aim,
    // counter-clockwise positive. It goes through `cs2::control::counter_wish_for`,
    // so the side axis carries the engine's convention (positive to the player's
    // **left**) and the forward axis drops to zero when the wish is
    // perpendicular — a counter-strafe wants lateral only, and the sign of
    // `cos(90 degrees)` (`-4.37e-8` in float) would otherwise push the player
    // backwards.
    float forward = 0.0F;
    float side = 0.0F;
    if (brake) {
      const float vel_yaw = std::atan2(vy, vx) * 180.0F / k_pi;
      const float rel = vel_yaw + 180.0F - rage_yaw_.load();
      const cs2::control::CounterWish wish = cs2::control::counter_wish_for(rel);
      forward = wish.forward;
      side = wish.side;
    }

    if (writable(strafe_base, k_base_move_offset + 12)) {
      auto* move = reinterpret_cast<float*>(strafe_base + k_base_move_offset);
      move[0] = forward;
      move[1] = side;
      *reinterpret_cast<std::uint32_t*>(strafe_base + k_base_has_bits_offset) |= k_base_move_bits;
    }
    // The button bits follow the engine's convention — a positive side axis is
    // the player's LEFT (`cs2::control::engine_move_buttons`).
    const std::uint64_t held =
        (*buttons & ~move_bits) | cs2::control::engine_move_buttons(forward, side, k_move_epsilon);
    *buttons = held;
    const auto buttons_pb =
        *reinterpret_cast<std::uintptr_t*>(strafe_base + k_base_buttons_pb_offset);
    if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
      auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
      state[0] = (state[0] & ~move_bits) | (held & move_bits);
      state[1] = (state[1] & ~move_bits) | (held & move_bits);
      *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3U;
    }
    if (ccsgo_input_ != 0) {
      auto* raw = reinterpret_cast<float*>(ccsgo_input_ + k_input_move_offset);
      if (writable(reinterpret_cast<std::uintptr_t>(raw), 3 * sizeof(float))) {
        raw[0] = brake ? forward : 0.0F;
        raw[1] = brake ? side : 0.0F;
        raw[2] = 0.0F;
      }
    }
    write_engine_buttons(held & move_bits, move_bits & ~held);
    rage_stops_.fetch_add(1);
  }
  if (!rage_stopping) {
    strafe_tick(reinterpret_cast<void*>(strafe_base), bytes);
  }

  // 4. The command's **own** `viewangles` (`base + 0x40`) — what movement's
  //     wishdir is computed from. Measured live 2026-10-15: writing the
  //     `input_history` entries does not steer movement, so an auto-strafe yaw
  //     goes here. This is not "silent" in the same sense as the history write,
  //     but it does not move the camera either — the camera is the engine's own
  //     view global.
  if (control && command_angles_enabled_.load()) {
    const auto base =
        *reinterpret_cast<std::uintptr_t*>(bytes + k_cmd_pb_offset + k_pb_base_offset);
    if (write_command_angles(reinterpret_cast<std::uint8_t*>(base), command_angle_pitch_.load(),
                             command_angle_yaw_.load(), command_angle_roll_.load())) {
      command_angle_writes_.fetch_add(1);
    }
  }

  // 4b. The anti-aim: the fake (desync) angles into the command's own
  //     `viewangles`, and the **movement re-base** that keeps the operator's
  //     W/A/S_D camera-relative. The command's angles are the movement's wish
  //     basis (`SetupMove` copies them into the `CMoveData`'s angles and
  //     `AirMove` builds the wish from them), so the two halves belong
  //     together: the fake angles without the re-base rotate the walk basis.
  //     It runs after the strafe and after the `look` command's angles — it
  //     **re-expresses** the axes the command already carries (the strafe's
  //     wish, the ragebot's auto-stop, the player's own keys) in the anti-aim
  //     frame, so those features keep working. It refuses to act where a
  //     composition is impossible (the strafe's sub-tick basis rotation, the
  //     `look` command's own angles) and says so in `anti_aim_reason`.
  anti_aim_tick(cmd, reinterpret_cast<std::uint8_t*>(*reinterpret_cast<std::uintptr_t*>(
                         bytes + k_cmd_pb_offset + k_pb_base_offset)));

  // 5. Sub-tick view angles: every `input_history` entry's `view_angles` (the
  //    silent-aim seam). The shot reads the entry at
  //    `attack1_start_history_index` (`pb + 0x3c`), which is not necessarily the
  //    newest, so overwrite them all (the community silent-aim recipe;
  //    `docs/re/subtick.md`). NOTE: these do **not** steer movement (measured
  //    2026-10-15) — that is what the command's own `viewangles` above are for.
  if (control && angles_enabled_.load()) {
    write_silent_angles(cmd, angle_pitch_.load(), angle_yaw_.load(), angle_roll_.load());
  }

  // 6. Ragebot: the render-thread solve written as the silent sub-tick aim, plus
  //    the optional attack. Independent of the control backend (like the movement
  //    features) — nothing is written unless a solve is active.
  rage_tick(bytes, buttons);
}

auto Input::on_ground() const noexcept -> bool {
  const std::uintptr_t pawn = local_pawn_.load();
  if (pawn == 0) {
    return false;
  }
  std::uint32_t flags = 0;
  if (!cs2::game::read(pawn + flags_offset_, flags)) {
    return false;
  }
  return (flags & 1U) != 0;  // FL_ONGROUND
}

void Input::refresh() noexcept {
  if (installed_) {
    return;
  }
  // One-shot guard: `refresh()` is called from both the control thread and the
  // per-frame state refresh. Only one caller may run the patch at a time; a
  // caller that loses the race returns, and the claim is released on exit so a
  // later call retries when the target was not ready yet.
  bool expected = false;
  if (!refresh_claimed_.compare_exchange_strong(expected, true)) {
    return;
  }
  struct ClaimReset {
    std::atomic<bool>& flag;
    ~ClaimReset() {
      flag.store(false);
    }
  } claim_reset{refresh_claimed_};
  const cs2::game::ModuleInfo module = cs2::game::find_module("client.dll");
  if (!module.valid()) {
    return;
  }
  const OffsetInfo* object_offset = find_offset("ccsgo_input");
  const OffsetInfo* move_offset = find_offset("create_move");
  if (object_offset == nullptr || move_offset == nullptr || object_offset->module != "client.dll" ||
      move_offset->module != "client.dll") {
    return;
  }
  void* object = reinterpret_cast<void*>(module.base + object_offset->rva);
  ccsgo_input_ = reinterpret_cast<std::uintptr_t>(object);
  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable == nullptr || !module.contains(reinterpret_cast<std::uintptr_t>(vtable))) {
    return;
  }
  // Find the slot holding `create_move` rather than assuming an index.
  const void* target = reinterpret_cast<void*>(module.base + move_offset->rva);
  std::size_t index = k_max_vtable_slots;
  for (std::size_t i = 0; i < k_max_vtable_slots; ++i) {
    if (vtable[i] == target) {
      index = i;
      break;
    }
  }
  if (index == k_max_vtable_slots) {
    return;
  }
  void** slot = &vtable[index];
  if (*slot == nullptr || !cs2::hooks::make_writable(slot, sizeof(void*))) {
    return;
  }
  original_ = *slot;
  *slot = reinterpret_cast<void*>(&Input::create_move_detour);
  slot_ = slot;
  slot_index_ = index;
  flags_offset_ = flags_offset();
  velocity_offset_ = velocity_offset();
  if (const OffsetInfo* alloc = find_offset("subtick_step_alloc");
      alloc != nullptr && alloc->module == "client.dll") {
    alloc_step_ = reinterpret_cast<alloc_step_fn>(module.base + alloc->rva);
  }
  if (const OffsetInfo* add = find_offset("repeated_ptr_add");
      add != nullptr && add->module == "client.dll") {
    add_step_ = reinterpret_cast<add_step_fn>(module.base + add->rva);
  }
  // Slot 5 (`create_move_input`) is the per-tick entry; the command it builds is
  // the one the movement services simulate (the slot-25 command is a copy).
  if (const OffsetInfo* move_in = find_offset("create_move_input");
      move_in != nullptr && move_in->module == "client.dll") {
    const void* target5 = reinterpret_cast<void*>(module.base + move_in->rva);
    for (std::size_t i = 0; i < k_max_vtable_slots; ++i) {
      if (vtable[i] == target5) {
        void** slot5 = &vtable[i];
        if (*slot5 != nullptr && cs2::hooks::make_writable(slot5, sizeof(void*))) {
          original5_ = *slot5;
          *slot5 = reinterpret_cast<void*>(&Input::create_move_input_detour);
          slot5_ = slot5;
          slot5_index_ = i;
        }
        break;
      }
    }
  }
  // The `GetUserCmd` chain (`docs/re/client-dll.md` §"Input path").
  client_base_ = module.base;
  if (const OffsetInfo* controllers = find_offset("local_player_controllers");
      controllers != nullptr && controllers->module == "client.dll") {
    controllers_rva_ = controllers->rva;
  }
  if (const OffsetInfo* array = find_offset("user_cmd_array");
      array != nullptr && array->module == "client.dll") {
    user_cmd_array_rva_ = array->rva;
  }
  if (const OffsetInfo* tick = find_offset("get_user_cmd_tick");
      tick != nullptr && tick->module == "client.dll") {
    tick_ = reinterpret_cast<tick_fn>(module.base + tick->rva);
  }
  if (const OffsetInfo* array = find_offset("get_user_cmd_array");
      array != nullptr && array->module == "client.dll") {
    array_ = reinterpret_cast<array_fn>(module.base + array->rva);
  }
  if (const OffsetInfo* sequence = find_offset("get_user_cmd_by_sequence");
      sequence != nullptr && sequence->module == "client.dll") {
    sequence_ = reinterpret_cast<sequence_fn>(module.base + sequence->rva);
  }
  g_active = this;
  installed_ = true;
  cs2::game::append_log("[cs2-payload] input: CCSGOInput::CreateMove hooked (slot " +
                        std::to_string(index) + ")");
}

}  // namespace cs2::control
