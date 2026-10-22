#include "cs2/control/input.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <chrono>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include <string>

#include "cs2/game/log.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "cs2/hooks/memory.hpp"
#include "cs2/tls/bootstrap.hpp"
#if defined(CS2_NATIVE_WINDOWS_BUILD)
#include "MinHook.h"
#endif
#include "offsets.hpp"
#if __has_include("schemas/client_dll.hpp")
#include "schemas/client_dll.hpp"
#define CS2_CONTROL_HAVE_CLIENT_SCHEMA 1
#endif

namespace cs2::control {
namespace {

using namespace cs2::sdk;
auto input_now_ms() noexcept -> unsigned long long {
  return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
auto input_key_down(int key) noexcept -> bool {
#if defined(_WIN32)
  return (GetAsyncKeyState(key) & 0x8000) != 0;
#else
  (void)key; return false;
#endif
}

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
[[nodiscard]] // Protobuf default instances are readable but shared/read-only. Never turn
// those objects writable or confuse a successful read probe with write access.
auto mutable_command_storage(std::uintptr_t address, std::size_t bytes) noexcept -> bool {
  if (!writable(address, bytes)) return false;
#if defined(_WIN32)
  const auto end = address + bytes;
  if (end < address) return false;
  for (auto cursor = address; cursor < end;) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    constexpr DWORD flags = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(region.Protect & flags)) return false;
    const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (next <= cursor) return false;
    cursor = next;
  }
#endif
  return true;
}

auto valid_set(float value) noexcept -> float {
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
  const auto restore = [](void** slot, void* expected, void* original) {
    if (slot == nullptr) return;
    void* current = nullptr;
    if (!game::read(reinterpret_cast<std::uintptr_t>(slot), current) || current != expected) return;
#if defined(_WIN32)
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return;
    InterlockedCompareExchangePointer(slot, original, expected);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), previous, &ignored);
#else
    if (hooks::make_writable(slot, sizeof(void*))) *slot = original;
#endif
  };
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  if (view_inline_target_) {
    MH_DisableHook(view_inline_target_);
    MH_RemoveHook(view_inline_target_);
  }
#endif
  if (installed_) {
    restore(slot_, reinterpret_cast<void*>(&Input::create_move_detour), original_);
    restore(slot5_, reinterpret_cast<void*>(&Input::create_move_input_detour), original5_);
    restore(view_slot_, reinterpret_cast<void*>(&Input::handle_view_detour), original_view_);
  }
  if (g_active == this) g_active = nullptr;
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
  if (!rage_allowed_.load()) return;
  rage_sequence_.fetch_add(1);
  rage_pitch_.store(pitch);
  rage_yaw_.store(yaw);
  rage_target_.store(target);
  rage_hitbox_.store(hitbox);
  rage_updated_ms_.store(input_now_ms());
  rage_sequence_.fetch_add(1);
  rage_active_.store(rage_allowed_.load());
}

void Input::clear_rage_aim() noexcept {
  rage_active_.store(false);
}

bool Input::commit_shot_angles(void* cmd, float pitch, float yaw, float roll) noexcept {
  if (!cmd) return false;
  auto* bytes = static_cast<std::uint8_t*>(cmd);
  std::uintptr_t base = 0, angles = 0;
  if (!game::read(reinterpret_cast<std::uintptr_t>(cmd) + k_cmd_pb_offset + k_pb_base_offset, base) ||
      !mutable_command_storage(base, k_base_move_offset + 12) ||
      !game::read(base + k_base_viewangles_offset, angles) ||
      !mutable_command_storage(angles, k_angles_values_offset + 12)) return false;
  auto* view = reinterpret_cast<float*>(angles + k_angles_values_offset);
  if (!std::isfinite(view[1])) return false;
  const float wanted_yaw = normalize_angle(yaw);
  const float delta = normalize_angle(view[1] - wanted_yaw) * (k_pi / 180.0F);
  const float cosine = std::cos(delta), sine = std::sin(delta);
  const auto rotate = [cosine, sine](float forward, float left, float& out_forward, float& out_left) {
    out_forward = cosine * forward - sine * left;
    out_left = sine * forward + cosine * left;
  };
  auto* axes = reinterpret_cast<float*>(base + k_base_move_offset);
  if (!std::isfinite(axes[0]) || !std::isfinite(axes[1])) return false;
  bool yaw_encoded=false;
  if(std::fabs(delta)>1e-6F&&nl_movement_encoded_&&nl_first_yaw_) {
    if(!nl_owns_yaw(nl_first_yaw_)||!mutable_command_storage(nl_first_yaw_,0x38))return false;
    auto* owned=reinterpret_cast<float*>(nl_first_yaw_+0x34);
    // Rebase the steering prefix into the shot frame and its restoration tail
    // back out. Otherwise shot correction leaves a residual camera yaw.
    if (nl_restore_yaw_ && (!nl_owns_yaw(nl_restore_yaw_) ||
        !mutable_command_storage(nl_restore_yaw_,0x38))) return false;
    *owned=normalize_angle(*owned+view[1]-wanted_yaw);
    if (nl_restore_yaw_) {
      auto* tail = reinterpret_cast<float*>(nl_restore_yaw_+0x34);
      *tail=normalize_angle(*tail-view[1]+wanted_yaw);
    }
    for(int i=0;i<nl_owned_count_;++i)if(nl_owned_yaws_[i].pointer==nl_first_yaw_ ||
        nl_owned_yaws_[i].pointer==nl_restore_yaw_) {
      auto& entry=nl_owned_yaws_[i];
      (void)game::read(entry.pointer+0x34,entry.value);
      (void)game::read_bytes(entry.pointer+0x10,entry.encoded.data(),entry.encoded.size());
    }
    yaw_encoded=true;
  }
  if (std::fabs(delta) > 1e-6F&&!yaw_encoded) {
    float forward = 0, left = 0;
    rotate(axes[0], axes[1], forward, left);
    // Both axes share a scale, preserving direction when a rotated diagonal
    // exceeds the native +/-1 analog domain.
    const float scale = std::max({1.0F, std::fabs(forward), std::fabs(left)});
    axes[0] = forward / scale; axes[1] = left / scale;
    *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= 0xc0u;
    // Autostop's fractional analog residuals use the same movement basis.
    // Preserve timestamps, button events and yaw deltas; rotate analog deltas only.
    const auto field = base + k_base_subtick_moves_offset;
    int size = 0, capacity = 0;
    std::uintptr_t rep = 0;
    if (game::read(field + k_field_size_offset, size) && size > 0 && size <= 32 &&
        game::read(field + k_field_rep_offset, rep) && game::read(rep, capacity) && capacity >= size) {
      const bool analog_timeline=nl_movement_encoded_ && !nl_first_yaw_;
      bool rebase_previous=nl_movement_encoded_;
      game::Vec2 old_state=nl_previous_axes_,corrected_state=nl_previous_axes_;
      for (int i = 0; i < size; ++i) {
        std::uintptr_t step = 0;
        if (!game::read(rep + k_rep_elements_offset + i * 8, step) ||
            !mutable_command_storage(step, k_step_left_offset + 4)) continue;
        auto* bits = reinterpret_cast<std::uint32_t*>(step + k_step_has_bits_offset);
        if (!(*bits & 0x18u)) continue;
        const auto* values = reinterpret_cast<const float*>(step + k_step_forward_offset);
        const float old_forward = (*bits & 0x8u) ? values[0] : 0;
        const float old_left = (*bits & 0x10u) ? values[1] : 0;
        if (!std::isfinite(old_forward) || !std::isfinite(old_left)) continue;
        if(analog_timeline) {
          // Rotate absolute states, then rebuild their deltas against the raw
          // service baseline. Scaling all deltas by the base diagonal's scale
          // would weaken otherwise unit-length paired gain impulses.
          old_state.x+=old_forward;old_state.y+=old_left;
          float f=0,l=0;rotate(old_state.x,old_state.y,f,l);
          const float state_scale=std::max({1.0F,std::fabs(f),std::fabs(l)});
          const game::Vec2 desired{f/state_scale,l/state_scale};
          auto* output=reinterpret_cast<float*>(step+k_step_forward_offset);
          output[0]=desired.x-corrected_state.x;output[1]=desired.y-corrected_state.y;
          corrected_state=desired;*bits|=0x18u;
          for(int n=0;n<nl_owned_count_;++n)if(nl_owned_yaws_[n].pointer==step)
            (void)game::read_bytes(step+0x10,nl_owned_yaws_[n].encoded.data(),nl_owned_yaws_[n].encoded.size());
          for(int n=0;n<nl_augmented_count_;++n)if(nl_augmented_[n].pointer==step)
            (void)game::read_bytes(step+0x10,nl_augmented_[n].encoded.data(),nl_augmented_[n].encoded.size());
          continue;
        }
        float new_forward = 0, new_left = 0;
        rotate(old_forward, old_left, new_forward, new_left);
        auto* output = reinterpret_cast<float*>(step + k_step_forward_offset);
        output[0] = new_forward / scale; output[1] = new_left / scale;
        if(rebase_previous) {
          float initial_forward=0,initial_left=0;
          rotate(nl_previous_axes_.x,nl_previous_axes_.y,initial_forward,initial_left);
          output[0]+=initial_forward/scale-nl_previous_axes_.x;
          output[1]+=initial_left/scale-nl_previous_axes_.y;
          rebase_previous=false;
        }
        *bits |= 0x18u;
      }
    }
    // A paired analog timeline owns the wish; preserve the physical keys so
    // a same-command rebuild does not reinterpret shot-frame correction as WASD.
    if(!(nl_movement_encoded_ && !nl_first_yaw_)) {
    constexpr auto movement = button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
    std::uint64_t directions = 0;
    if (axes[0] > k_move_epsilon) directions |= button::k_forward;
    if (axes[0] < -k_move_epsilon) directions |= button::k_back;
    if (axes[1] > k_move_epsilon) directions |= button::k_moveleft;
    if (axes[1] < -k_move_epsilon) directions |= button::k_moveright;
    auto* buttons = reinterpret_cast<std::uint64_t*>(bytes + k_cmd_buttons_offset);
    const auto changed = (buttons[0] ^ directions) & movement;
    buttons[0] = (buttons[0] & ~movement) | directions;
    buttons[1] = (buttons[1] & ~movement) | changed;
    std::uintptr_t buttons_pb = 0;
    if (game::read(base + k_base_buttons_pb_offset, buttons_pb) &&
        writable(buttons_pb, k_buttonstate_values_offset + 24)) {
      auto* values = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
      values[0] = (values[0] & ~movement) | directions;
      values[1] = (values[1] & ~movement) | changed;
      *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
    }
  }
  }
  view[0] = std::clamp(pitch, -89.0F, 89.0F); view[1] = wanted_yaw; view[2] = roll;
  *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
  *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_viewangles_bit;
  return true;
}

int Input::write_silent_angles(void* cmd, float pitch, float yaw, float roll) noexcept {
  if (cmd == nullptr || !std::isfinite(pitch) || !std::isfinite(yaw)) return -1;
  auto* bytes = static_cast<std::uint8_t*>(cmd);
  const std::uintptr_t pb = reinterpret_cast<std::uintptr_t>(bytes) + k_cmd_pb_offset;
  const auto size = *reinterpret_cast<std::uint32_t*>(pb + k_pb_history_size_offset);
  const auto rep = *reinterpret_cast<std::uintptr_t*>(pb + k_pb_history_rep_offset);
  std::uint32_t capacity = 0;
  if (rep == 0 || size == 0 || size > 64 || !game::read(rep, capacity) || capacity < size)
    return -1;
  int last_written = -1;
  for (std::uint32_t i = 0; i < size; ++i) {
    std::uintptr_t entry = 0, angles = 0;
    if (!game::read(rep + 8 + i * 8, entry) ||
        !game::read(entry + k_history_entry_angles_offset, angles) ||
        !mutable_command_storage(entry + 0x10, sizeof(std::uint32_t)) ||
        !mutable_command_storage(angles, k_angles_values_offset + 12)) continue;
    auto* xyz = reinterpret_cast<float*>(angles + k_angles_values_offset);
    xyz[0] = std::clamp(pitch, -89.0F, 89.0F);
    xyz[1] = normalize_angle(yaw);
    xyz[2] = roll;
    *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
    // The entry itself must advertise its view_angles submessage as present.
    *reinterpret_cast<std::uint32_t*>(entry + 0x10) |= 1u;
    angle_writes_.fetch_add(1);
    last_written = static_cast<int>(i);
  }
  return last_written;
}

void Input::rage_tick(std::uint8_t* cmd, std::uint64_t* buttons) noexcept {
  if (!cmd || !buttons) return;
  const int number = *reinterpret_cast<int*>(cmd + k_cmd_number_offset);
  const bool same = number == rage_attack_command_;
  const bool automatic = rage_allowed_.load() && rage_fire_.load();
  auto sync_buttons = [&] {
    std::uintptr_t base = 0, state = 0;
    if (game::read(reinterpret_cast<std::uintptr_t>(cmd) + 0x40, base) &&
        game::read(base + k_base_buttons_pb_offset, state) &&
        mutable_command_storage(state, k_buttonstate_values_offset + 24)) {
      std::memcpy(reinterpret_cast<void*>(state + k_buttonstate_values_offset), buttons, 24);
      *reinterpret_cast<std::uint32_t*>(state + k_buttonstate_has_bits_offset) |= 7u;
    }
  };
  auto release = [&](ShotBlock why, bool cancel_partial_shot = false) {
    // Auto fire owns its trigger. A blocked candidate must not leak a held shot.
    if (automatic || cancel_partial_shot || (same && rage_injected_attack_)) {
      const bool was_held = (*buttons & button::k_attack) || rage_injected_attack_;
      buttons[0] &= ~button::k_attack;
      buttons[1] = (buttons[1] & ~button::k_attack) | (was_held ? button::k_attack : 0);
      buttons[2] &= ~button::k_attack;
      *reinterpret_cast<int*>(cmd + k_cmd_pb_offset + 0x3c) = -1;
      *reinterpret_cast<unsigned*>(cmd + k_cmd_pb_offset + 0x10) |= 0x20u;
      sync_buttons();
    }
    rage_injected_attack_ = false;
    rage_attack_command_ = number;
    rage_attack_edge_ = false;
    shot_block_.store(why);
  };
  if (!rage_allowed_.load() || !rage_active_.load()) { release(ShotBlock::no_target); return; }
  if (!rage_shot_ready_.load()) { release(ShotBlock::accuracy); return; }
  const bool physical = (*buttons & button::k_attack) && !(same && rage_injected_attack_);
  if (!automatic && !physical) { release(ShotBlock::ready); return; }
  if (require_live_shot_state_ && !(same && rage_attack_edge_)) {
    const auto block = shot_gate_.check(rage_shot_state_);
    if (block != ShotBlock::ready) { release(block); return; }
  }
  float pitch = 0, yaw = 0;
  bool coherent = false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const auto sequence = rage_sequence_.load();
    if (sequence & 1u) continue;
    pitch = rage_pitch_.load(); yaw = rage_yaw_.load();
    if (sequence == rage_sequence_.load()) { coherent = true; break; }
  }
  if (!coherent) { release(ShotBlock::stale); return; }
  if (const auto query = recoil_query_.load()) {
    float punch[3]{};
    if (!query(recoil_context_.load(), local_pawn_.load(), punch)) { release(ShotBlock::recoil); return; }
    pitch -= punch[0]; yaw -= punch[1];
  }
  const int history = write_silent_angles(cmd, pitch, yaw, 0);
  if (history < 0 || !stamp_shot_timing(cmd, rage_timing_) || !commit_shot_angles(cmd, pitch, yaw, 0)) {
    release(ShotBlock::history, true); return;
  }
  // A fresh committed shot carries all three attack words, like Velocity.
  // Cooldown commands release it; duplicate processing reuses the same decision.
  if (automatic) for (int i = 0; i < 3; ++i) buttons[i] |= button::k_attack;
  *reinterpret_cast<int*>(cmd + k_cmd_pb_offset + 0x3c) = history;
  *reinterpret_cast<unsigned*>(cmd + k_cmd_pb_offset + 0x10) |= 0x20u;
  sync_buttons();
  if (!same || !rage_attack_edge_) {
    rage_shots_.fetch_add(1); // requests, not proof of a server weapon_fire event
    if (require_live_shot_state_) shot_gate_.commit(rage_shot_state_);
  }
  last_shot_command_.store(number);
  rage_writes_.fetch_add(1);
  rage_injected_attack_ = automatic;
  rage_attack_command_ = number;
  rage_attack_edge_ = true;
  shot_block_.store(ShotBlock::ready);
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
  if (input->original5_ != nullptr) {
    reinterpret_cast<create_move_input_fn>(input->original5_)(self, slot, active);
  }
  input->calls5_.fetch_add(1);
  // The per-tick entry: write the command the game will simulate (fetched via
  // the same `GetUserCmd` chain the game uses), then re-assert the raw axes in
  // case the movement reads them after `CreateMove`.
  void* command = input->current_cmd();
  input->process_built_command(command);
  if (!input->stop_applied_.load()) input->write_raw_move();
  input->write_raw_angles();
}

void Input::process_built_command(void* command) noexcept {
  int debug_command=-1;
  if(command)(void)game::read(reinterpret_cast<std::uintptr_t>(command)+8,debug_command);
  // The installed builder sets CUserCmd+0x94 to 2 on ordinary completed commands
  // (client RVA 0xb661f6). Velocity's overwrite test belongs before its builder;
  // applying it here would bypass every coordinator, aim and CRC update.
  if (command) if (const auto observe = this->command_observer_.load())
    observe(this->command_context_.load(), command);
  this->capture(command);
  this->apply(command);
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  if (command && this->native_command_.finalize(command)) {
    last_crc_command_.store(debug_command);last_crc_ok_.store(true);
    const auto count = this->command_crc_writes_.fetch_add(1) + 1;
    if (count == 1) game::append_log("[native-input] post-CreateMove command/buttons/move_crc finalized");
  } else if (command) {
    last_crc_command_.store(debug_command);last_crc_ok_.store(false);
    const auto count = this->command_crc_failures_.fetch_add(1) + 1;
    this->clear_rage_aim();
    if(this->rage_attack_edge_) {
      auto* bytes=static_cast<std::uint8_t*>(command);
      auto* words=reinterpret_cast<std::uint64_t*>(bytes+k_cmd_buttons_offset);
      words[0]&=~button::k_attack;words[1]|=button::k_attack;words[2]&=~button::k_attack;
      *reinterpret_cast<int*>(bytes+k_cmd_pb_offset+0x3c)=-1;
      *reinterpret_cast<unsigned*>(bytes+k_cmd_pb_offset+0x10)|=0x20u;
      this->rage_injected_attack_=false;this->rage_attack_edge_=false;
      (void)this->native_command_.finalize(command);
    }
    if (count == 1 || count % 256 == 0)
      game::append_log("[native-input] command finalization FAILED; aim result cleared");
  }
#endif
}

void __fastcall Input::handle_view_detour(void* self, int slot) noexcept {
  tls::ensure_thread_tls();
  auto* input = g_active;
  if (!input || !input->original_view_) return;
  input->preserve_command_view(self, slot);
}
void Input::preserve_command_view(void* self, int slot) noexcept {
  if (!original_view_) return;
  game::Vec3 before{};
  const bool preserve = native_command_.view(self, before);
  if (preserve) camera_before_yaw_.store(before.y);
  reinterpret_cast<void (*)(void*, int)>(original_view_)(self, slot);
  game::Vec3 candidate{};
  if (native_command_.view(self,candidate)) camera_candidate_yaw_.store(candidate.y);
  camera_restore_verified_.store(false);
  if (preserve) {
    native_command_.restore_view(self, before);
    game::Vec3 restored{};
    if (native_command_.view(self,restored)) {
      camera_restored_yaw_.store(restored.y);
      camera_restore_verified_.store(std::fabs(normalize_angle(restored.y-before.y))<.001F &&
                                    std::fabs(restored.x-before.x)<.001F);
    }
    camera_restores_.fetch_add(1);
  }
}

auto Input::current_cmd() noexcept -> void* {
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  std::uintptr_t controller = 0;
  if (!game::read(client_base_ + controllers_rva_, controller)) return nullptr;
  return native_command_.current(reinterpret_cast<void*>(controller));
#else
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
#endif
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
  if (!move_enabled_.load() || ccsgo_input_ == 0) {
    return;
  }
  float axes[3];
  resolve_axes(move_forward_.load(), move_side_.load(), move_up_.load(), move_quantize_.load(),
               axes);
  auto* move = reinterpret_cast<float*>(ccsgo_input_ + k_input_move_offset);
  if (!writable(reinterpret_cast<std::uintptr_t>(move), 3 * sizeof(float))) {
    return;
  }
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
  stop_applied_.store(false);
  nl_movement_encoded_=false;nl_bhop_encoded_=false;
  if (!rage_allowed_.load()) clear_rage_aim();
  if (rage_active_.load()) {
    const int hold_key = rage_hold_key_.load();
    if ((hold_key > 0 && !input_key_down(hold_key)) ||
        input_now_ms() - rage_updated_ms_.load() > 150) clear_rage_aim();
  }
  if (cmd == nullptr) {
    return;
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
  nl_current_command_=*reinterpret_cast<int*>(bytes+8);
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  nl_restore_augmented_yaw();
#endif

  // 1. Legacy button mask (`CInButtonState`). The bhop assist runs after the mask
  //    is settled, as a separate record-based step (below); it *reads* the held
  //    state and only writes the sub-tick table.
  auto* buttons = reinterpret_cast<std::uint64_t*>(bytes + k_cmd_buttons_offset);
  const std::uint64_t current = *buttons;
  std::uint64_t value =
      control ? (current | set_mask_.load()) & ~clear_mask_.load() : current;
  if (value != current) {
    *buttons = value;
    writes_.fetch_add(1);
  }
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  nl_bhop_tick(cmd, *reinterpret_cast<std::uint8_t**>(bytes+k_cmd_pb_offset+k_pb_base_offset), buttons);
#else
  bhop_tick(cmd,
            reinterpret_cast<std::uint8_t*>(
                *reinterpret_cast<std::uintptr_t*>(bytes + k_cmd_pb_offset + k_pb_base_offset)),
            value);
#endif

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
  if (!(rage_active_.load() && rage_stop_requested_.load() && stop_enabled_.load() && (on_ground() || stop_air_.load()))) {
#if defined(CS2_NATIVE_WINDOWS_BUILD)
    nl_strafe_tick(reinterpret_cast<void*>(strafe_base),bytes);
#else
    strafe_tick(reinterpret_cast<void*>(strafe_base), bytes);
#endif
  } else {
#if defined(CS2_NATIVE_WINDOWS_BUILD)
    nl_move_reason_.store(12);strafe_steps_.store(0);strafe_airborne_.store(false);
#endif
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

#if defined(CS2_NATIVE_WINDOWS_BUILD)
  nl_reconcile_jump_move(reinterpret_cast<void*>(strafe_base),bytes);
#endif
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  if(rage_active_.load()&&rage_stop_requested_.load()&&stop_enabled_.load())nl_clear_owned_yaw();
#endif
  // Rage autostop owns movement after other overrides; silent aim stays separate.
  stop_applied_.store(autostop_tick(bytes, reinterpret_cast<std::uint8_t*>(strafe_base)));
  if(stop_applied_.load())nl_movement_encoded_=false; // autostop replaced the analog contract

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
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  const auto now=GetTickCount64();
  if(client_base_ && strafe_enabled_.load() && now-nl_state_log_ms_>=1000) {
    nl_state_log_ms_=now;
    char status[768];
    std::snprintf(status,sizeof(status),
      "[movement-state] pid=%lu cmd=%d processed=%d state=%d speed=%.3f model_gain=%.3f begin=%.4f reuses=%llu misses=%llu physics=%d quantized=%d air_accel=%.3f cap=%.3f stamina_recovery=%.3f camera_ok=%d paired=%d terminal_lateral=%.5f",
      GetCurrentProcessId(),nl_source_command_.load(),nl_last_processed_.load(),nl_move_reason_.load(),
      std::hypot(nl_velocity_[0].load(),nl_velocity_[1].load()),nl_plan_gain_.load(),nl_plan_begin_.load(),
      static_cast<unsigned long long>(nl_state_reuses_.load()),static_cast<unsigned long long>(nl_state_misses_.load()),
      nl_physics_valid_.load(),nl_quantized_.load(),nl_air_accel_.load(),nl_air_cap_.load(),nl_stamina_recovery_.load(),
      camera_restore_verified_.load(),nl_balanced_active_.load(),nl_terminal_lateral_.load());
    game::append_log(status);
  }
#endif
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

bool Input::install_native(game::ModuleInfo module) noexcept {
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  (void)nl_trace_.prepare(module);
  const auto* object_offset = find_offset("ccsgo_input");
  const auto* move_offset = find_offset("create_move_input");
  const auto* controller_offset = find_offset("local_player_controllers");
  if (!object_offset || !move_offset || !controller_offset || !native_command_.initialize(module)) return false;
  auto* object = reinterpret_cast<void*>(module.base + object_offset->rva);
  void** table = nullptr;
  if (!game::read(reinterpret_cast<std::uintptr_t>(object), table) || !table ||
      !module.contains(reinterpret_cast<std::uintptr_t>(table))) return false;
  std::size_t move_index = k_max_vtable_slots, view_index = k_max_vtable_slots;
  void* move = reinterpret_cast<void*>(module.base + move_offset->rva);
  void* view = native_command_.view_handler();
  for (std::size_t i = 0; i < k_max_vtable_slots; ++i) {
    void* entry = nullptr;
    if (!game::read(reinterpret_cast<std::uintptr_t>(table + i), entry)) return false;
    if (entry == move) move_index = i;
    if (entry == view) view_index = i;
  }
  if (move_index == k_max_vtable_slots || view_index == k_max_vtable_slots || move_index == view_index)
    return false;
  require_live_shot_state_ = true;
  ccsgo_input_ = reinterpret_cast<std::uintptr_t>(object);
  client_base_ = module.base;
  controllers_rva_ = controller_offset->rva;
  flags_offset_ = flags_offset(); velocity_offset_ = velocity_offset();
  if (const auto* step = find_offset("subtick_step_alloc"))
    alloc_step_ = reinterpret_cast<alloc_step_fn>(module.base + step->rva);
  if (const auto* add = find_offset("repeated_ptr_add"))
    add_step_ = reinterpret_cast<add_step_fn>(module.base + add->rva);
  auto* move_slot = table + move_index;
  auto* view_slot = table + view_index;
  (void)view_slot;
  if (!hooks::make_writable(move_slot, sizeof(void*))) return false;
  const auto initialized = MH_Initialize();
  if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
  void* view_trampoline = nullptr;
  if (MH_CreateHook(view, reinterpret_cast<void*>(&Input::handle_view_detour), &view_trampoline) != MH_OK)
    return false;
  original5_ = move; original_view_ = view_trampoline;
  slot5_ = move_slot; view_inline_target_ = view;
  slot5_index_ = move_index; slot_index_ = move_index;
  g_active = this;
  if (MH_EnableHook(view) != MH_OK) {
    MH_RemoveHook(view); view_inline_target_ = nullptr; original_view_ = nullptr;
    g_active = nullptr; return false;
  }
  *move_slot = reinterpret_cast<void*>(&Input::create_move_input_detour);
  installed_ = true;
  game::append_log("[native-input] post-build CreateMove + inline camera hook installed (slots " +
      std::to_string(move_index) + "," + std::to_string(view_index) + "); slot 25 left unchanged");
  return true;
#else
  (void)module; return false;
#endif
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
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  (void)install_native(module);
  return;
#endif
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
