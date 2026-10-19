#include "cs2/control/input.hpp"

#include <algorithm>
#include <array>
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

struct NamedBit {
  std::string_view name;
  std::uint64_t bit;
};

constexpr NamedBit k_named_bits[] = {
    {"attack", button::k_attack},     {"mouse1", button::k_attack},
    {"jump", button::k_jump},         {"space", button::k_jump},
    {"duck", button::k_duck},         {"crouch", button::k_duck},
    {"ctrl", button::k_duck},         {"forward", button::k_forward},
    {"w", button::k_forward},         {"back", button::k_back},
    {"s", button::k_back},            {"use", button::k_use},
    {"e", button::k_use},             {"left", button::k_left},
    {"right", button::k_right},       {"moveleft", button::k_moveleft},
    {"a", button::k_moveleft},        {"moveright", button::k_moveright},
    {"d", button::k_moveright},       {"attack2", button::k_attack2},
    {"mouse2", button::k_attack2},    {"run", button::k_run},
    {"reload", button::k_reload},     {"r", button::k_reload},
    {"speed", button::k_speed},       {"shift", button::k_speed},
    {"walk", button::k_walk},         {"zoom", button::k_zoom},
};

// The one Input the payload owns, for the static hook detour.
Input* g_active = nullptr;

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
// on the sub-tick channel (`Input::write_move_steps`).
[[nodiscard]] auto valid_set(float value) noexcept -> float {
  const auto steps = std::round(value / k_move_scale);
  return std::clamp(steps, -1.0F, 1.0F) * k_move_scale;
}

// The axes actually handed to the engine: clamped to ±`k_move_scale` and, in
// `quantize` mode, snapped to the valid set above. Applied identically to the
// raw axes, the input entry and the command's move floats so every consumer
// agrees.
auto resolve_axes(float forward, float side, float up, bool quantize, float out[3]) noexcept
    -> void {
  const auto axis = [quantize](float value) {
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
constexpr std::size_t k_aim_punch_angle_offset = 0x50;               // m_predictableBaseAngle
constexpr std::size_t k_aim_punch_angle_offset_unpredictable = 0xa4;  // m_unpredictableBaseAngle
constexpr std::size_t k_cmd_number_offset = 0x8;        // CUserCmd::nSequenceNumber
constexpr std::size_t k_base_client_tick_offset = 0x54;  // CBaseUserCmdPB::client_tick
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
[[nodiscard]] auto read_move_sample(std::uintptr_t address) noexcept -> MoveSample {  MoveSample sample;
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

auto button_bit(std::string_view name) noexcept -> std::uint64_t {
  for (const NamedBit& entry : k_named_bits) {
    if (entry.name == name) {
      return entry.bit;
    }
  }
  return 0;
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

void Input::rage_tick(std::uint8_t* cmd, std::uint64_t* buttons) noexcept {
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
  const auto base =
      *reinterpret_cast<std::uintptr_t*>(cmd + k_cmd_pb_offset + k_pb_base_offset);
  const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
  std::uint64_t extra = *buttons & button::k_attack;
  if (rage_scope_.load()) {
    extra |= button::k_attack2 | button::k_zoom;
  }
  if (rage_reload_.load()) {
    extra |= button::k_reload;
  }
  if (extra != 0 && writable(buttons_pb, k_buttonstate_values_offset + 16)) {
    auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
    state[0] = (state[0] & ~extra) | extra;  // held
    state[1] = (state[1] & ~extra) | extra;  // changed (a press edge)
    *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
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
// The goal heading is the held keys read in the *view* frame — `W`/nothing is the
// camera's forward, `S` is behind — and a held `A`/`D` stands the feature down so
// the player's own air control is never hijacked. The wish is then pushed
// perpendicular to the **current** horizontal velocity on the goal's side, at the
// `AirAccelerate` optimum `theta*` (`docs/re/strafing.md`), recomputed every tick
// so it re-perpendicularises instead of decaying (a *held* side with a fixed basis
// stops accelerating after ~3 ticks). Nothing writes a view angle, so neither the
// camera nor the pawn's eye angles move (`write_strafe_wish` documents the two
// silent encodings).
void Input::strafe_tick(void* base, void* cmd) noexcept {
  strafe_airborne_.store(false);
  strafe_delta_.store(0.0F);
  strafe_goal_.store(0.0F);
  strafe_wish_.store(0.0F);
  strafe_rot_.store(0.0F);
  strafe_side_.store(0);
  strafe_steps_.store(0);
  strafe_clamped_.store(false);
  strafe_reason_.store(0);
  if (base == nullptr || !strafe_enabled_.load()) {
    return;
  }
  if (on_ground()) {
    strafe_reason_.store(1);  // grounded: leave the player's own input alone
    return;
  }
  float vx = 0.0F;
  float vy = 0.0F;
  if (!velocity(vx, vy)) {
    strafe_reason_.store(2);
    return;
  }
  const float speed = std::sqrt(vx * vx + vy * vy);
  strafe_speed_.store(speed);
  if (speed < k_strafe_min_speed) {
    strafe_reason_.store(2);
    return;
  }
  strafe_airborne_.store(true);

  auto* bytes_base = static_cast<std::uint8_t*>(base);
  auto* bytes_cmd = static_cast<std::uint8_t*>(cmd);
  const auto angles_ptr = *reinterpret_cast<std::uintptr_t*>(bytes_base + k_base_viewangles_offset);
  float view_yaw = 0.0F;
  if (angles_ptr == 0 ||
      !cs2::game::read(angles_ptr + k_angles_values_offset + sizeof(float), view_yaw)) {
    strafe_reason_.store(3);
    strafe_airborne_.store(false);
    return;
  }

  // The player's travel intent, read from the command's own mask *before* it is
  // overwritten. Holding a strafe key (A/D) stands the assist down — the player is
  // already air-controlling — and `S` flips the goal behind the camera.
  std::uint64_t buttons = 0;
  const auto cmd_ptr = reinterpret_cast<std::uintptr_t>(cmd);
  if (cmd != nullptr && writable(cmd_ptr + k_cmd_buttons_offset, sizeof(std::uint64_t))) {
    buttons = *reinterpret_cast<std::uint64_t*>(cmd_ptr + k_cmd_buttons_offset);
  }
  constexpr std::uint64_t side_bits = button::k_moveleft | button::k_moveright;
  if ((buttons & side_bits) != 0) {
    strafe_reason_.store(7);
    strafe_airborne_.store(false);
    return;
  }
  const float goal = normalize_angle(view_yaw + ((buttons & button::k_back) != 0 ? 180.0F : 0.0F));
  strafe_goal_.store(goal);

  // The `AirAccelerate` optimum: with the per-call budget
  // `A = wishspeed * sv_airaccelerate * frametime` and the air cap
  // `W = sv_air_max_wishspeed` (30), the wish sits at `theta* = acos((W - A)/s)`
  // off the velocity; `A >= W` collapses it to exactly 90 degrees (the usual case
  // for a full analog, `A = 250 * 12 / 64 = 46.875`). `theta` is clamped so the
  // degenerate `A >= W` branch and a slow player both stay sane.
  const int steps = std::clamp(strafe_subticks_.load(), 0, k_strafe_max_steps);
  const float cap = k_strafe_air_wishspeed;
  const float wishspeed = strafe_wishspeed_.load();
  // The per-call budget `A` and the optimal angle it implies. The engine re-runs
  // `AirAccelerate` once per slice with `frametime = (1/64)/slices`
  // (`docs/re/strafing.md` §2.5), so a slice's `A` is the full-tick `A` divided by
  // the slice count.
  const auto theta_for = [cap](float speed, float accel) -> float {
    if (cap - accel <= 0.0F || speed <= 0.0F) {
      return 90.0F;  // A >= W: exactly perpendicular
    }
    return static_cast<float>(std::acos(std::clamp((cap - accel) / speed, -1.0F, 1.0F)) * 180.0 /
                              k_pi);
  };
  const float accel_tick = wishspeed * k_strafe_airaccelerate * k_tick_interval;

  const float vel_yaw = static_cast<float>(std::atan2(vy, vx) * 180.0 / k_pi);
  const float delta = normalize_angle(goal - vel_yaw);
  const float side = delta >= 0.0F ? 1.0F : -1.0F;
  const float theta = std::clamp(theta_for(speed, accel_tick), 1.0F, 90.0F);
  const float wish_yaw = normalize_angle(vel_yaw + side * theta);
  const float rot = normalize_angle(wish_yaw - view_yaw);
  strafe_side_.store(side > 0.0F ? 1 : -1);
  strafe_wish_.store(wish_yaw);
  strafe_delta_.store(rot);

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
      const float slice_vel = static_cast<float>(std::atan2(sy, sx) * 180.0 / k_pi);
      const float slice_theta = std::clamp(theta_for(s, accel_slice), 1.0F, 90.0F);
      const float wish = normalize_angle(slice_vel + side * slice_theta);
      deltas[count++] = normalize_angle(wish - basis);
      basis = wish;
      // Step the simulated velocity the way `AirAccelerate` will:
      // `addspeed = min(wishspeed, 30) - dot(v, wishdir)`, `applied = min(A, addspeed)`.
      const float wx = std::cos(wish * k_pi / 180.0F);
      const float wy = std::sin(wish * k_pi / 180.0F);
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

  // The camera-frame analog (`leftmove` positive = right, the engine's `right(yaw)
  // = (sin, -cos)`), snapped to the `sv_quantize_movement_input` valid set so a
  // rounding server and the client agree. Used only in the `subticks == 0` mode —
  // the sub-tick mode rotates the basis instead and keeps `(1, 0)`.
  const float rel = rot * k_pi / 180.0F;
  const float forward = std::cos(rel);
  const float left = -std::sin(rel);

  if (write_strafe_wish(bytes_base, bytes_cmd, forward, left, deltas, steps)) {
    strafe_reason_.store(4);
    strafe_writes_.fetch_add(1);
  } else if (strafe_reason_.load() == 0) {
    strafe_reason_.store(5);  // the sub-tick array had no room
  }
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

  // The command's analog + both button states (the legacy `cmd + 0x60` mask and
  // `buttons_pb`, like `apply()`'s explicit move). Positive `leftmove` moves right
  // (the engine's `right = (sin, -cos)`).
  constexpr std::uint64_t move_bits =
      button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
  std::uint64_t held = 0;
  if (forward > k_move_epsilon) {
    held |= button::k_forward;
  } else if (forward < -k_move_epsilon) {
    held |= button::k_back;
  }
  if (left > k_move_epsilon) {
    held |= button::k_moveright;
  } else if (left < -k_move_epsilon) {
    held |= button::k_moveleft;
  }
  if ((held & move_bits) == 0) {
    return false;  // nothing to write
  }
  auto* move = reinterpret_cast<float*>(base + k_base_move_offset);
  move[0] = forward;
  move[1] = left;
  move[2] = 0.0F;
  *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_move_bits;
  if (cmd != nullptr && writable(reinterpret_cast<std::uintptr_t>(cmd) + k_cmd_buttons_offset,
                                 sizeof(std::uint64_t))) {
    auto* legacy = reinterpret_cast<std::uint64_t*>(cmd + k_cmd_buttons_offset);
    *legacy = (*legacy & ~move_bits) | held;
  }
  const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
  if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
    auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
    state[0] = (state[0] & ~move_bits) | held;
    state[1] = (state[1] & ~move_bits) | held;
    *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
  }

  // The raw axes are the source `create_move_input` derives the command floats
  // from; write them here too so the value survives (and is the one the movement
  // reads).
  if (ccsgo_input_ != 0) {
    auto* raw = reinterpret_cast<float*>(ccsgo_input_ + k_input_move_offset);
    if (writable(reinterpret_cast<std::uintptr_t>(raw), 3 * sizeof(float))) {
      raw[0] = forward;
      raw[1] = left;
      raw[2] = 0.0F;
      strafe_raw_writes_.fetch_add(1);
    }
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
        !cs2::game::read(
            reinterpret_cast<std::uintptr_t>(&vtable[k_reset_prediction_command_slot]),
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
    const auto in_module = [&module](void* pointer) {
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
  (void)cs2::game::read(svc + k_modern_jump_offset + k_modern_jump_landed_tick_offset,
                        before_tick);
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
  if (index < -1) {
    index = -1;
  }
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
  if (cmd == nullptr || !writable(reinterpret_cast<std::uintptr_t>(cmd), k_cmd_buttons_offset + 8)) {
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
  const auto base = *reinterpret_cast<std::uintptr_t*>(
      static_cast<std::uint8_t*>(cmd) + k_cmd_pb_offset + k_pb_base_offset);
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

auto Input::add_button_step(void* base, std::uint64_t button, bool pressed,
                            float when) noexcept -> void* {
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
      !cs2::game::read(jump + k_modern_jump_landed_frac_offset, landed_frac) ||
      landed_tick == 0 || !(landed_frac >= 0.0F && landed_frac <= 1.0F)) {
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
                 : std::clamp(contact + k_bhop_offset, contact + k_subtick_grid,
                              k_bhop_last_when);
      mode = 1;
    } else if (build > landed_tick + 1 && build <= landed_tick + 1 + k_bhop_catchup_ticks) {
      // B: the command is one or more ticks past the contact. A whole tick late
      // takes `DoJump`'s beta path (a hop, but the ground-slice clamp keeps the
      // speed loss); the catch-up window keeps the array from ever starving.
      when = std::clamp(contact + k_bhop_offset + k_subtick_grid, k_bhop_first_when,
                        k_bhop_last_when);
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

void Input::apply(void* cmd) noexcept {
  if (cmd == nullptr) {
    return;
  }
  // Run the game-thread sink first: engine calls that need the game's TLS (the
  // physics query allocates through tier0) must run on this thread, not the
  // payload's publish thread.
  if (game_thread_sink_ != nullptr) {
    game_thread_sink_(game_thread_context_);
  }
  // The **control commands** (`press`/`move`/`look`) are a backend choice: in the
  // `console` backend the runner drives the engine's own `+button`/`setang`
  // commands through `exec`, so the command must be left untouched here. The
  // **payload features** — the `bhop` jump assist and the auto-`strafe` — are
  // owned by the overlay menu (`wire::Settings`, ADR-0021) and have no console
  // equivalent, so they must act regardless of the backend. Gating *them* on
  // `usercmd` is why enabling them in the menu did nothing (the menu cannot select
  // the backend): measured live 2026-10-15, `strafe`/`bhop` flags true but
  // `writes = move_writes = strafe_writes = 0` until `input(via="usercmd")`.
  const bool control = backend_ == Backend::usercmd;
  auto* bytes = static_cast<std::uint8_t*>(cmd);

  // 1. Legacy button mask (`CInButtonState`). The bhop assist runs after the mask
  //    is settled, as a separate record-based step (below); it *reads* the held
  //    state and only writes the sub-tick table.
  auto* buttons = reinterpret_cast<std::uint64_t*>(bytes + k_cmd_buttons_offset);
  const std::uint64_t current = *buttons;
  std::uint64_t value =
      control ? (current | set_mask_.load()) & ~clear_mask_.load() : current;
  // The ragebot's attack is a **persistent** press while a solve is active — a
  // payload feature (like the movement features) that must hold on *every*
  // command the engine simulates, not just the command `rage_tick` happens to
  // see. Verified subtlety: pressing attack only on the command whose history we
  // wrote that tick let a shot be released on a command the silent write had not
  // reached, so it followed the camera (the auto-fire timing bug). A persistent
  // press is exactly why manual `press("attack")` always hit.
  const bool rage_solved = rage_active_.load();
  const bool rage_firing = rage_solved && rage_fire_.load();
  if (rage_firing) {
    value |= button::k_attack;
  }
  // Auto-scope / auto-reload (common ragebot assists, `docs/ragebot.md`): hold zoom
  // and/or reload while a solve is active. The engine ignores zoom on a weapon that
  // cannot scope; reload is a no-op when the clip is full.
  if (rage_solved && rage_scope_.load()) {
    value |= button::k_attack2 | button::k_zoom;
  }
  if (rage_solved && rage_reload_.load()) {
    value |= button::k_reload;
  }
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

      constexpr std::uint64_t move_bits = button::k_forward | button::k_back |
                                          button::k_moveleft | button::k_moveright;
      std::uint64_t held = *buttons & ~move_bits;
      if (move[0] > k_move_epsilon) {
        held |= button::k_forward;
      } else if (move[0] < -k_move_epsilon) {
        held |= button::k_back;
      }
      if (move[1] > k_move_epsilon) {
        held |= button::k_moveleft;
      } else if (move[1] < -k_move_epsilon) {
        held |= button::k_moveright;
      }
      *buttons = held;

      const auto buttons_pb =
          *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
      if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
        auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb +
                                                       k_buttonstate_values_offset);
        state[0] = (state[0] & ~move_bits) | (held & move_bits);  // buttonstate1: held
        state[1] = (state[1] & ~move_bits) | (held & move_bits);  // buttonstate2: changed
        *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
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
    constexpr std::uint64_t move_bits = button::k_forward | button::k_back |
                                        button::k_moveleft | button::k_moveright;
    constexpr float k_pi = 3.14159265358979323846F;
    constexpr float k_deg_to_rad = 0.01745329251994329577F;

    const float limit = rage_stop_speed_.load();
    float vx = 0.0F;
    float vy = 0.0F;
    float speed = 0.0F;
    if (limit > 0.0F && velocity(vx, vy)) {
      speed = std::sqrt(vx * vx + vy * vy);
    }
    const bool brake = limit > 0.0F && speed > limit;
    // The move axes: the counter-wish when braking, else zero (hold still).
    float forward = 0.0F;
    float side = 0.0F;
    if (brake) {
      const float aim_yaw = rage_yaw_.load() * k_deg_to_rad;
      const float rel = std::atan2(vy, vx) + k_pi - aim_yaw;
      forward = std::cos(rel) >= 0.0F ? 1.0F : -1.0F;
      side = -std::sin(rel) >= 0.0F ? 1.0F : -1.0F;
    }

    if (writable(strafe_base, k_base_move_offset + 12)) {
      auto* move = reinterpret_cast<float*>(strafe_base + k_base_move_offset);
      move[0] = forward;
      move[1] = side;
      *reinterpret_cast<std::uint32_t*>(strafe_base + k_base_has_bits_offset) |= k_base_move_bits;
    }
    std::uint64_t held = *buttons & ~move_bits;
    if (forward > 0.0F) {
      held |= button::k_forward;
    } else if (forward < 0.0F) {
      held |= button::k_back;
    }
    if (side > 0.0F) {
      held |= button::k_moveleft;
    } else if (side < 0.0F) {
      held |= button::k_moveright;
    }
    *buttons = held;
    const auto buttons_pb =
        *reinterpret_cast<std::uintptr_t*>(strafe_base + k_base_buttons_pb_offset);
    if (writable(buttons_pb, k_buttonstate_values_offset + 16)) {
      auto* state = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
      state[0] = (state[0] & ~move_bits) | (held & move_bits);
      state[1] = (state[1] & ~move_bits) | (held & move_bits);
      *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
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
    if (writable(base, k_base_viewangles_offset + sizeof(void*))) {
      const auto angles = *reinterpret_cast<std::uintptr_t*>(base + k_base_viewangles_offset);
      if (writable(angles, k_angles_values_offset + 3 * sizeof(float))) {
        auto* xyz = reinterpret_cast<float*>(angles + k_angles_values_offset);
        xyz[0] = command_angle_pitch_.load();
        xyz[1] = command_angle_yaw_.load();
        xyz[2] = command_angle_roll_.load();
        *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
        *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_viewangles_bit;
        command_angle_writes_.fetch_add(1);
      }
    }
  }

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
  return (flags & 1u) != 0;  // FL_ONGROUND
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
    ~ClaimReset() { flag.store(false); }
  } claim_reset{refresh_claimed_};
  const cs2::game::ModuleInfo module = cs2::game::find_module("client.dll");
  if (!module.valid()) {
    return;
  }
  const OffsetInfo* object_offset = find_offset("ccsgo_input");
  const OffsetInfo* move_offset = find_offset("create_move");
  if (object_offset == nullptr || move_offset == nullptr ||
      object_offset->module != "client.dll" || move_offset->module != "client.dll") {
    return;
  }
  void* object = reinterpret_cast<void*>(module.base + object_offset->rva);
  ccsgo_input_ = reinterpret_cast<std::uintptr_t>(object);
  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable == nullptr || !module.contains(reinterpret_cast<std::uintptr_t>(vtable))) {
    return;
  }
  // Find the slot holding `create_move` rather than assuming an index.
  void* target = reinterpret_cast<void*>(module.base + move_offset->rva);
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
    void* target5 = reinterpret_cast<void*>(module.base + move_in->rva);
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
