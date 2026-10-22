#include "cs2/control/input.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "cs2/aim/autostop.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/schema.hpp"
#if __has_include("schemas/client_dll.hpp")
#include "schemas/client_dll.hpp"
#define CS2_STOP_HAVE_SCHEMA 1
#endif

namespace cs2::control {
namespace {
auto mapped(std::uintptr_t p, std::size_t n) noexcept -> bool {
  unsigned char b = 0;
  return p != 0 && n != 0 && game::read_bytes(p, &b, 1) && game::read_bytes(p + n - 1, &b, 1);
}
auto schema(std::string_view type, std::string_view field) noexcept -> std::uint32_t {
  if (const auto live = game::schema_field(type, field)) return *live;
#if defined(CS2_STOP_HAVE_SCHEMA)
  return sdk::schemas::client_dll::field_offset_by_target(type, field);
#else
  (void)type;
  (void)field;
  return 0;
#endif
}
void read_float(std::uintptr_t object, std::string_view type, std::string_view name,
                float& value) noexcept {
  const auto offset = schema(type, name);
  float sample = 0;
  if (object != 0 && offset != 0 && game::read(object + offset, sample) && std::isfinite(sample))
    value = sample;
}
}  // namespace

void Input::set_stop_physics(float max_speed, float friction, float acceleration,
                             float air_acceleration, float stop_speed, float gravity) noexcept {
  const float values[] = {max_speed, friction, acceleration, air_acceleration, stop_speed, gravity};
  for (float v : values)
    if (!std::isfinite(v) || v < 0) return;
  if (max_speed <= 0 || gravity <= 0) return;
  stop_max_speed_.store(max_speed);
  stop_friction_.store(friction);
  stop_acceleration_.store(acceleration);
  stop_air_acceleration_.store(air_acceleration);
  stop_speed_.store(stop_speed);
  stop_gravity_.store(gravity);
  stop_revision_.fetch_add(1);
}

void Input::set_stop_weapon_scaling(bool on, int category, float speed0, float speed1) noexcept {
  if (!std::isfinite(speed0) || !std::isfinite(speed1) || speed0 <= 0 || speed1 <= 0) return;
  stop_weapon_scaling_.store(on);
  stop_weapon_category_.store(category);
  stop_weapon_speed0_.store(speed0);
  stop_weapon_speed1_.store(speed1);
  stop_revision_.fetch_add(1);
}

void Input::set_rage_autostop(bool on, bool air, bool fractional, bool cancel_jump) noexcept {
  stop_enabled_.store(on);
  stop_air_.store(air);
  stop_fractional_.store(fractional);
  stop_cancel_jump_.store(cancel_jump);
}

auto Input::autostop_tick(std::uint8_t* cmd, std::uint8_t* base) noexcept -> bool {
  stop_path_.store(static_cast<int>(aim::StopPath::inactive));
  if (!mapped(reinterpret_cast<std::uintptr_t>(base), k_base_move_offset + 12)) {
    stop_cache_valid_ = false;
    return false;
  }
  const auto pawn = local_pawn_.load();
  aim::StopState state;
  std::uint32_t flags = 0;
  float velocity[3]{};
  if (pawn == 0 || !game::read(pawn + flags_offset_, flags) ||
      !game::read(pawn + velocity_offset_, velocity))
    return false;
  state.ground = (flags & 1u) != 0;
  const int tick = *reinterpret_cast<const int*>(base + 0x54);
  if (!state.ground) stop_last_air_tick_ = tick;
  if (!rage_active_.load() || !rage_stop_requested_.load() || !stop_enabled_.load()) {
    stop_cache_valid_ = false;
    return false;
  }
  const auto age = static_cast<std::int32_t>(static_cast<std::uint32_t>(tick) -
                                             static_cast<std::uint32_t>(stop_last_air_tick_));
  const auto* command_buttons = reinterpret_cast<const std::uint64_t*>(cmd + k_cmd_buttons_offset);
  const bool jump_requested =
      ((command_buttons[0] | command_buttons[1] | command_buttons[2]) & button::k_jump) != 0;
  const bool cancel = stop_cancel_jump_.load() && jump_requested && !(state.ground && age >= 17);
  if (!state.ground && !stop_air_.load()) return false;
  state.vx = velocity[0];
  state.vy = velocity[1];
  state.vz = velocity[2];
  state.fractional = stop_fractional_.load();
  state.speed_limit = stop_max_speed_.load();
  state.ground_friction = stop_friction_.load();
  state.ground_acceleration = stop_acceleration_.load();
  state.air_acceleration = stop_air_acceleration_.load();
  state.stop_speed = stop_speed_.load();
  state.vertical_coefficient = stop_gravity_.load();
  // These live field names are independent adapter inputs, not claimed bindings
  // for the unresolved source-dump fields. Missing fields retain explicit defaults.
  std::uintptr_t services = 0;
  auto service_offset = schema("C_BasePlayerPawn", "m_pMovementServices");
  if (service_offset != 0 && game::read(pawn + service_offset, services)) {
    read_float(services, "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction",
               state.movement_factor);
    read_float(services, "CCSPlayer_MovementServices", "m_flStamina", state.stamina);
    read_float(services, "CCSPlayer_MovementServices", "m_flDuckAmount", state.duck_amount);
    read_float(services, "CPlayer_MovementServices", "m_flMaxspeed", state.speed_limit);
  }
  state.duck_amount = std::clamp(state.duck_amount, 0.0F, 1.0F);
  state.duck_active = (flags & 2u) != 0 || state.duck_amount > 0.0F;
  // Coefficient roles in the dump are explicit; their live bindings are partial.
  state.ground_friction *= state.movement_factor;
  state.ground_acceleration *= state.movement_factor;
  float weapon_speed[2]{};
  const auto speed_offset = schema("CCSWeaponBaseVData", "m_flMaxSpeed");
  const auto vdata = stop_weapon_vdata_.load();
  if (vdata != 0 && speed_offset != 0 && game::read(vdata + speed_offset, weapon_speed) &&
      std::isfinite(weapon_speed[0]) && std::isfinite(weapon_speed[1]) && weapon_speed[0] > 0 &&
      weapon_speed[1] > 0) {
    state.weapon_speed = {weapon_speed[0], weapon_speed[1]};
    state.speed_limit = weapon_speed[stop_scoped_.load() ? 1 : 0];
  }
  // The dump's scaling-enable/category inputs are not proven to be scoped/zoom.
  // Expose explicit inputs instead of silently assigning those meanings.
  state.weapon_scaling = stop_weapon_scaling_.load();
  state.weapon_category = stop_weapon_category_.load();
  state.weapon_selector = stop_scoped_.load() ? 1 : 0;
  if (state.weapon_scaling)
    state.weapon_speed = {stop_weapon_speed0_.load(), stop_weapon_speed1_.load()};

  const auto angles = *reinterpret_cast<std::uintptr_t*>(base + k_base_viewangles_offset);
  if (!mapped(angles, k_angles_values_offset + 12)) return false;
  aim::StopCommand original;
  std::memcpy(&original.axes, base + k_base_move_offset, 12);
  const auto* angle_values = reinterpret_cast<const float*>(angles + k_angles_values_offset);
  original.pitch = angle_values[0];
  original.yaw = angle_values[1];
  original.roll = angle_values[2];
  const auto sequence = *reinterpret_cast<const std::uint32_t*>(cmd + 0x8);
  const unsigned options = (stop_fractional_.load() ? 1u : 0u) | (state.ground ? 2u : 0u) |
                           (stop_cancel_jump_.load() ? 4u : 0u) | (stop_scoped_.load() ? 8u : 0u);
  const auto revision = stop_revision_.load();
  if (!stop_cache_valid_ || stop_cache_sequence_ != sequence || stop_cache_pawn_ != pawn ||
      stop_cache_weapon_ != vdata || stop_cache_revision_ != revision ||
      stop_cache_options_ != options) {
    stop_cached_ = aim::solve_autostop(state, original);
    // The current PB has no third-axis delta. Preserve up input via ordinary
    // mode rather than dropping a recovered ground reversal's nonzero up axis.
    if (stop_cached_.event_count == 2 &&
        stop_cached_.events[0].command.axes.up != stop_cached_.events[1].command.axes.up) {
      state.fractional = false;
      stop_cached_ = aim::solve_autostop(state, original);
    }
    stop_cache_valid_ = true;
    stop_cache_sequence_ = sequence;
    stop_cache_pawn_ = pawn;
    stop_cache_options_ = options;
    stop_cache_weapon_ = vdata;
    stop_cache_revision_ = revision;
  }
  const auto& plan = stop_cached_;
  stop_path_.store(static_cast<int>(plan.path));
  if (plan.path == aim::StopPath::invalid || plan.path == aim::StopPath::unchanged) return false;

  auto* field = base + k_base_subtick_moves_offset;
  const auto count = *reinterpret_cast<const int*>(field + k_field_size_offset);
  const auto rep = *reinterpret_cast<const std::uintptr_t*>(field + k_field_rep_offset);
  if (count < 0 || count > 32 || (count > 0 && !mapped(rep, k_rep_elements_offset + count * 8)))
    return false;
  // Validate before touching existing steps. Preserve button/angle payloads in
  // mixed steps while replacing only their analog components.
  for (int i = 0; i < count; ++i) {
    const auto step = *reinterpret_cast<const std::uintptr_t*>(rep + k_rep_elements_offset + i * 8);
    if (!mapped(step, k_step_yaw_offset + 4)) return false;
  }
  const aim::StopCommand first = plan.event_count != 0 ? plan.events[0].command : plan.command;
  const float delta_forward =
      plan.event_count == 2 ? plan.events[1].command.axes.forward - first.axes.forward : 0;
  const float delta_side =
      plan.event_count == 2 ? plan.events[1].command.axes.side - first.axes.side : 0;
  const float delta_yaw = plan.event_count == 2 && plan.events[1].angles
                              ? normalize_angle(plan.events[1].command.yaw - first.yaw)
                              : 0;
  const bool analog = plan.event_count == 2;
  const bool yaw = delta_yaw != 0;
  const int needed = (analog ? 1 : 0) + (yaw ? 1 : 0);
  // Allocate before command writes; rollback current_size on any allocation failure.
  int retained = 0;
  for (int i = 0; i < count; ++i) {
    const auto* step =
        *reinterpret_cast<const std::uint8_t* const*>(rep + k_rep_elements_offset + i * 8);
    auto has = *reinterpret_cast<const std::uint32_t*>(step + k_step_has_bits_offset);
    if (stop_owned_yaw_ == reinterpret_cast<std::uintptr_t>(step) &&
        stop_owned_sequence_ == sequence && (has & 0x40u) != 0 &&
        *reinterpret_cast<const float*>(step + k_step_yaw_offset) == stop_owned_delta_)
      continue;
    has &= ~(0x8u | 0x10u);
    if (cancel && (*reinterpret_cast<const std::uint64_t*>(step + k_step_button_offset) &
                   button::k_jump) != 0)
      has &= ~(0x1u | 0x2u);
    if ((has & ~0x4u) != 0) ++retained;
  }
  if (retained + needed > 32) return false;
  void* analog_step = nullptr;
  void* yaw_step = nullptr;
  if (analog) analog_step = add_analog_step(base, delta_forward, delta_side, plan.events[1].when);
  if (analog && analog_step == nullptr) return false;
  if (yaw) yaw_step = add_yaw_step(base, delta_yaw, plan.events[1].when);
  if (yaw && yaw_step == nullptr) {
    *reinterpret_cast<int*>(field + k_field_size_offset) = count;
    return false;
  }
  // alloc_step may relocate rep: fetch it again, then compact only old entries.
  auto* pointers = reinterpret_cast<void**>(
      *reinterpret_cast<std::uintptr_t*>(field + k_field_rep_offset) + k_rep_elements_offset);
  int keep = 0;
  std::array<void*, 32> retired{};
  int retired_count = 0;
  for (int i = 0; i < count; ++i) {
    auto* step = static_cast<std::uint8_t*>(pointers[i]);
    auto* has = reinterpret_cast<std::uint32_t*>(step + k_step_has_bits_offset);
    const auto step_ptr = reinterpret_cast<std::uintptr_t>(step);
    if (stop_owned_yaw_ == step_ptr && stop_owned_sequence_ == sequence && (*has & 0x40u) != 0 &&
        *reinterpret_cast<float*>(step + k_step_yaw_offset) == stop_owned_delta_) {
      retired[retired_count++] = step;
      continue;
    }
    *has &= ~(0x8u | 0x10u);
    *reinterpret_cast<float*>(step + k_step_forward_offset) = 0;
    *reinterpret_cast<float*>(step + k_step_left_offset) = 0;
    if (cancel && (*reinterpret_cast<const std::uint64_t*>(step + k_step_button_offset) &
                   button::k_jump) != 0)
      *has &= ~(0x1u | 0x2u);
    if ((*has & ~0x4u) != 0)
      pointers[keep++] = step;
    else
      retired[retired_count++] = step;
  }
  if (analog_step != nullptr) pointers[keep++] = analog_step;
  if (yaw_step != nullptr) pointers[keep++] = yaw_step;
  // Retain every allocated pointer exactly once, including reusable spare slots.
  // Overwriting discarded pointers would let alloc_step reuse an active object.
  for (int i = 0; i < retired_count; ++i) pointers[keep + i] = retired[i];
  *reinterpret_cast<int*>(field + k_field_size_offset) = keep;
  // Stable time sort of the active prefix; a pair inserted after an existing
  // later button step must not leave analog at the end out of order.
  for (int i = 1; i < keep; ++i) {
    void* value = pointers[i];
    float when = 0;
    std::memcpy(&when, static_cast<std::uint8_t*>(value) + k_step_when_offset, 4);
    int j = i;
    while (j > 0) {
      float previous = 0;
      std::memcpy(&previous, static_cast<std::uint8_t*>(pointers[j - 1]) + k_step_when_offset, 4);
      if (!(previous > when)) break;
      pointers[j] = pointers[j - 1];
      --j;
    }
    pointers[j] = value;
  }
  stop_owned_yaw_ = reinterpret_cast<std::uintptr_t>(yaw_step);
  stop_owned_sequence_ = sequence;
  stop_owned_delta_ = delta_yaw;

  std::memcpy(base + k_base_move_offset, &first.axes, 12);
  *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_move_bits;
  if (plan.path != aim::StopPath::ground_scaled && plan.path != aim::StopPath::ground_fractional) {
    auto* xyz = reinterpret_cast<float*>(angles + k_angles_values_offset);
    xyz[0] = first.pitch;
    xyz[1] = first.yaw;
    xyz[2] = first.roll;
    *reinterpret_cast<std::uint32_t*>(angles + k_angles_has_bits_offset) |= k_angles_bits;
    *reinterpret_cast<std::uint32_t*>(base + k_base_has_bits_offset) |= k_base_viewangles_bit;
  }
  constexpr auto movement =
      button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
  auto* buttons = reinterpret_cast<std::uint64_t*>(cmd + k_cmd_buttons_offset);
  std::uint64_t held = *buttons & ~movement;
  if (first.axes.forward > k_move_epsilon) held |= button::k_forward;
  if (first.axes.forward < -k_move_epsilon) held |= button::k_back;
  if (first.axes.side > k_move_epsilon) held |= button::k_moveright;
  if (first.axes.side < -k_move_epsilon) held |= button::k_moveleft;
  if (cancel) held &= ~button::k_jump;
  *buttons = held;
  if (cancel) {
    buttons[1] &= ~button::k_jump;
    buttons[2] &= ~button::k_jump;
  }
  const auto buttons_pb = *reinterpret_cast<std::uintptr_t*>(base + k_base_buttons_pb_offset);
  if (mapped(buttons_pb, k_buttonstate_values_offset + 24)) {
    auto* pb = reinterpret_cast<std::uint64_t*>(buttons_pb + k_buttonstate_values_offset);
    for (int i = 0; i < 2; ++i) {
      pb[i] = (pb[i] & ~movement) | (held & movement);
      if (cancel) pb[i] &= ~button::k_jump;
    }
    *reinterpret_cast<std::uint32_t*>(buttons_pb + k_buttonstate_has_bits_offset) |= 0x3u;
  }
  if (mapped(ccsgo_input_ + k_input_move_offset, 12))
    std::memcpy(reinterpret_cast<void*>(ccsgo_input_ + k_input_move_offset), &first.axes, 12);
  stop_writes_.fetch_add(1);
  return true;
}
}  // namespace cs2::control
