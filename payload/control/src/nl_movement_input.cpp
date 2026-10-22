#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include "cs2/control/input.hpp"
#include "cs2/game/entity_system.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/schema.hpp"
#include "offsets.hpp"
namespace cs2::control {
namespace {
bool mutable_memory(std::uintptr_t p, std::size_t n) {
  MEMORY_BASIC_INFORMATION m{};
  return p && VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m)) == sizeof(m) &&
         m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
         (m.Protect &
          (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
         p + n >= p && p + n <= reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize;
}
bool same_step_payload(std::array<std::byte, 0x28> a, std::array<std::byte, 0x28> b) {
  // Protobuf cached size and struct padding may change during serialization;
  // they do not transfer ownership of the semantic movement event.
  std::fill(a.begin() + 4, a.begin() + 8, std::byte{});
  std::fill(b.begin() + 4, b.begin() + 8, std::byte{});
  std::fill(a.begin() + 0x11, a.begin() + 0x14, std::byte{});
  std::fill(b.begin() + 0x11, b.begin() + 0x14, std::byte{});
  return a == b;
}
float time_of(void* p) {
  float t = 0;
  std::memcpy(&t, static_cast<std::byte*>(p) + k_step_when_offset, 4);
  return t;
}
}  // namespace
bool Input::nl_owns_yaw(std::uintptr_t pointer) const noexcept {
  if (nl_owned_command_ != nl_current_command_) return false;
  for (int i = 0; i < nl_owned_count_; ++i) {
    const auto& owned = nl_owned_yaws_[i];
    if (owned.pointer != pointer) continue;
    std::array<std::byte, 0x28> encoded{};
    return game::read_bytes(pointer + 0x10, encoded.data(), encoded.size()) &&
           same_step_payload(encoded, owned.encoded);
  }
  return false;
}
void Input::nl_restore_augmented_yaw() noexcept {
  if (nl_owned_command_ == nl_current_command_) {
    for (int i = 0; i < nl_augmented_count_; ++i) {
      const auto& entry = nl_augmented_[i];
      std::array<std::byte, 0x28> encoded{};
      if (game::read_bytes(entry.pointer + 0x10, encoded.data(), encoded.size()) &&
          same_step_payload(encoded, entry.encoded) && mutable_memory(entry.pointer, 0x38))
        std::memcpy(reinterpret_cast<void*>(entry.pointer + 0x10), entry.original.data(),
                    entry.original.size());
    }
  }
  nl_augmented_count_ = 0;
}
void Input::nl_clear_owned_yaw() noexcept {
  nl_restore_augmented_yaw();
  if (nl_owned_command_ == nl_current_command_)
    for (int i = 0; i < nl_owned_count_; ++i) {
      const auto p = nl_owned_yaws_[i].pointer;
      if (nl_owns_yaw(p) && mutable_memory(p, 0x38)) {
        *reinterpret_cast<unsigned*>(p + 0x10) &= ~0x40u;
        *reinterpret_cast<float*>(p + 0x34) = 0;
      }
    }
  nl_owned_count_ = 0;
  nl_first_yaw_ = nl_restore_yaw_ = 0;
}
bool Input::nl_read_state() noexcept {
  // Rebuilds of one command must use its first pre-prediction state. Reading
  // the movement services again after simulation feeds the assisted result
  // back into its own next wish and impulse-reconciliation baseline.
  if (nl_state_cached_ && nl_state_command_ == nl_current_command_ &&
      nl_state_.pawn == local_pawn_.load()) {
    nl_state_reuses_.fetch_add(1);
    return true;
  }
  nl_state_cached_ = false;
  auto& f = nl_fields_;
  if (!f.ready) {
    if (GetTickCount64() < f.retry_ms) {
      nl_move_reason_.store(3);
      return false;
    }
    f.retry_ms = GetTickCount64() + 1000;
    auto get = [](const char* type, const char* field, std::uint32_t& out) {
      auto offset = game::schema_field(type, field);
      if (!offset) return false;
      out = *offset;
      return true;
    };
    f.ready = get("C_BasePlayerPawn", "m_pMovementServices", f.movement) &&
              get("C_BaseEntity", "m_fFlags", f.flags) &&
              get("C_BaseEntity", "m_vecVelocity", f.velocity) &&
              get("C_BaseEntity", "m_iHealth", f.health) &&
              get("C_BaseEntity", "m_lifeState", f.life) &&
              get("C_BaseEntity", "m_nActualMoveType", f.move_type) &&
              get("CPlayer_MovementServices", "m_nButtons", f.buttons) &&
              get("CPlayer_MovementServices", "m_flMaxspeed", f.max_speed) &&
              get("CPlayer_MovementServices", "m_flCmdForwardMove", f.forward) &&
              get("CPlayer_MovementServices", "m_flCmdLeftMove", f.left) &&
              get("CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction", f.friction) &&
              get("CCSPlayer_MovementServices", "m_ModernJump", f.modern) &&
              get("CCSPlayerModernJump", "m_nLastLandedTick", f.land_tick) &&
              get("CCSPlayerModernJump", "m_flLastLandedFrac", f.land_frac) &&
              get("CCSPlayerModernJump", "m_nLastActualJumpPressTick", f.actual_tick) &&
              get("CCSPlayerModernJump", "m_flLastActualJumpPressFrac", f.actual_frac) &&
              get("CCSPlayer_MovementServices", "m_flStamina", f.stamina) &&
              get("CCSPlayer_MovementServices", "m_flDuckAmount", f.duck) &&
              get("CPlayer_MovementServices", "m_nLastCommandNumberProcessed", f.last_processed);
    f.trace_ready = get("C_BaseEntity", "m_pGameSceneNode", f.scene) &&
                    get("CGameSceneNode", "m_vecAbsOrigin", f.origin) &&
                    get("C_BaseModelEntity", "m_Collision", f.collision) &&
                    get("CCollisionProperty", "m_vecMins", f.mins) &&
                    get("CCollisionProperty", "m_vecMaxs", f.maxs) &&
                    get("CCollisionProperty", "m_collisionAttribute", f.attribute) &&
                    get("VPhysicsCollisionAttribute_t", "m_nInteractsAs", f.mask) &&
                    get("C_BaseEntity", "m_flGravityScale", f.gravity);
    if (!f.ready) {
      nl_move_reason_.store(3);
      return false;
    }
  }
  auto pawn = local_pawn_.load();
  if (client_base_) {
    const auto* list = sdk::find_offset("entity_list");
    const auto handle_offset = game::schema_field("CCSPlayerController", "m_hPlayerPawn");
    std::uintptr_t blocks = 0;
    std::uint32_t handle = 0;
    const auto ctrl = controller();
    if (!list || !handle_offset || !ctrl || !game::read(ctrl + *handle_offset, handle) ||
        !game::read(client_base_ + list->rva, blocks)) {
      nl_move_reason_.store(4);
      return false;
    }
    pawn = game::EntitySystem(blocks).resolve(handle);
    local_pawn_.store(pawn);
  }
  NlState s;
  s.pawn = pawn;
  int health = 0;
  std::uint8_t life = 1, type = 0;
  std::uint32_t flags = 0;
  if (!pawn || !game::read(pawn + f.health, health) || health <= 0 ||
      !game::read(pawn + f.life, life) || life || !game::read(pawn + f.move_type, type) ||
      !game::read(pawn + f.flags, flags) || !game::read(pawn + f.movement, s.services) ||
      !s.services || !game::read(pawn + f.velocity, s.velocity)) {
    nl_move_reason_.store(4);
    return false;
  }
  if (type != 2) {
    nl_move_reason_.store(6);
    return false;
  }
  s.ground = (flags & 1) != 0;
  if (!game::read(s.services + f.forward, s.previous.x) ||
      !game::read(s.services + f.left, s.previous.y) ||
      !game::read(s.services + f.max_speed, s.max_speed) ||
      !game::read(s.services + f.friction, s.friction) || !std::isfinite(s.previous.x) ||
      !std::isfinite(s.previous.y) || !std::isfinite(s.max_speed) || s.max_speed <= 0 ||
      s.max_speed > 1000 || !std::isfinite(s.friction) || s.friction <= 0 || s.friction > 10 ||
      !std::isfinite(s.velocity.x) || !std::isfinite(s.velocity.y) ||
      !std::isfinite(s.velocity.z)) {
    nl_move_reason_.store(5);
    return false;
  }
  (void)game::read(s.services + f.buttons + 8, s.held);
  (void)game::read(s.services + f.modern + f.land_tick, s.landed_tick);
  (void)game::read(s.services + f.modern + f.land_frac, s.landed_fraction);
  if (!game::read(s.services + f.modern + f.actual_tick, s.actual_tick) ||
      !game::read(s.services + f.modern + f.actual_frac, s.actual_fraction) ||
      !game::read(s.services + f.stamina, s.stamina) || !game::read(s.services + f.duck, s.duck) ||
      !std::isfinite(s.stamina) || !std::isfinite(s.duck) || s.stamina < 0 || s.stamina > 100 ||
      s.duck < 0 || s.duck > 1) {
    nl_move_reason_.store(5);
    return false;
  }
  int processed = -1;
  if (!game::read(s.services + f.last_processed, processed)) {
    nl_move_reason_.store(4);
    return false;
  }
  nl_source_command_.store(nl_current_command_);
  nl_last_processed_.store(processed);
  nl_velocity_[0].store(s.velocity.x);
  nl_velocity_[1].store(s.velocity.y);
  nl_velocity_[2].store(s.velocity.z);
  nl_trace_available_.store(f.trace_ready && nl_trace_.ready());
  if (processed >= nl_current_command_ && nl_current_command_ > 0) {
    if (nl_missed_command_ != nl_current_command_) {
      nl_missed_command_ = nl_current_command_;
      nl_state_misses_.fetch_add(1);
    }
    nl_move_reason_.store(17);
    return false;
  }
  if (nl_physics_state_.valid || GetTickCount64() >= nl_physics_retry_) {
    nl_physics_retry_ = GetTickCount64() + 2000;
    nl_physics_state_ = nl_physics_.read();
  }
  nl_physics_valid_.store(nl_physics_state_.valid);
  nl_quantized_.store(nl_physics_state_.quantized);
  nl_air_accel_.store(nl_physics_state_.air_acceleration);
  nl_air_cap_.store(nl_physics_state_.air_cap);
  nl_stamina_recovery_.store(nl_physics_state_.stamina_recovery);
  nl_state_command_ = nl_current_command_;
  nl_state_ = s;
  nl_state_cached_ = true;
  nl_move_reason_.store(1);
  return true;
}
bool Input::nl_replace_steps(void* base, const std::vector<NlStep>& replacements, bool analog,
                             bool jump, std::vector<void*>* emitted, bool steering_only) noexcept {
  const auto field = reinterpret_cast<std::uintptr_t>(base) + k_base_subtick_moves_offset;
  int count = 0, allocated = 0;
  std::uintptr_t rep = 0;
  if (!mutable_memory(field, 0x18) || !game::read(field + 8, count) || count < 0 || count > 32 ||
      !game::read(field + 16, rep))
    return false;
  if (rep && (!game::read(rep, allocated) || allocated < count || allocated > 256)) return false;
  if (count && !rep) return false;
  std::vector<void*> original, retained, retired;
  std::array<std::array<std::byte, 0x38>, 32> saved{};
  for (int i = 0; i < allocated; ++i) {
    void* p = nullptr;
    if (!game::read(rep + 8 + i * 8, p)) return false;
    original.push_back(p);
  }
  for (int i = 0; i < count; ++i) {
    auto* p = original[i];
    if (!mutable_memory(reinterpret_cast<std::uintptr_t>(p), 0x38) ||
        !game::read_bytes(reinterpret_cast<std::uintptr_t>(p), saved[i].data(), 0x38))
      return false;
    const float t = time_of(p);
    if (!std::isfinite(t) || t < 0 || t > 1) return false;
  }
  int keep_count = 0;
  for (int i = 0; i < count; ++i) {
    unsigned bits = 0;
    std::uint64_t event_button = 0;
    std::memcpy(&bits, saved[i].data() + 0x10, 4);
    std::memcpy(&event_button, saved[i].data() + 0x18, 8);
    if (analog) {
      if (!steering_only || nl_owns_yaw(reinterpret_cast<std::uintptr_t>(original[i])))
        bits &= ~0x18u;
      if (nl_owns_yaw(reinterpret_cast<std::uintptr_t>(original[i]))) bits &= ~0x40u;
    }
    if (jump && (event_button & button::k_jump) && !(event_button & ~button::k_jump)) bits &= ~3u;
    if (bits & ~4u) ++keep_count;
  }
  if (keep_count + replacements.size() > 32) {
    nl_move_reason_.store(9);
    return false;
  }
  for (int i = 0; i < count; ++i) {
    auto* p = static_cast<std::byte*>(original[i]);
    auto& bits = *reinterpret_cast<unsigned*>(p + 0x10);
    if (analog) {
      if (nl_owns_yaw(reinterpret_cast<std::uintptr_t>(p))) {
        bits &= ~0x58u;
        std::memset(p + 0x28, 0, 8);
        *reinterpret_cast<float*>(p + 0x34) = 0;
      }
      if (!steering_only) {
        bits &= ~0x18u;
        std::memset(p + 0x28, 0, 8);
      }
    }
    if (jump) {
      auto& event_button = *reinterpret_cast<std::uint64_t*>(p + 0x18);
      if (event_button & button::k_jump) {
        event_button &= ~button::k_jump;
        if (!event_button) {
          bits &= ~3u;
          *reinterpret_cast<std::uint8_t*>(p + 0x20) = 0;
        }
      }
    }
    ((bits & ~4u) ? retained : retired).push_back(p);
  }
  auto** pointers = rep ? reinterpret_cast<void**>(rep + 8) : nullptr;
  for (std::size_t i = 0; i < retained.size(); ++i) pointers[i] = retained[i];
  for (std::size_t i = 0; i < retired.size(); ++i) pointers[retained.size() + i] = retired[i];
  *reinterpret_cast<int*>(field + 8) = static_cast<int>(retained.size());
  for (const auto& item : replacements) {
    void* step = alloc_step(base);
    if (!step) {
      // Restore the old active prefix and every original allocated pointer.
      std::uintptr_t current_rep = 0;
      int current_allocated = 0;
      (void)game::read(field + 16, current_rep);
      (void)game::read(current_rep, current_allocated);
      std::vector<void*> extra;
      for (int i = 0; i < current_allocated && i < 256; ++i) {
        void* p = nullptr;
        if (game::read(current_rep + 8 + i * 8, p) &&
            std::find(original.begin(), original.end(), p) == original.end())
          extra.push_back(p);
      }
      if (current_rep) {
        auto** slots = reinterpret_cast<void**>(current_rep + 8);
        for (std::size_t i = 0; i < original.size(); ++i) slots[i] = original[i];
        for (std::size_t i = 0; i < extra.size(); ++i) slots[original.size() + i] = extra[i];
      }
      for (int i = 0; i < count; ++i) std::memcpy(original[i], saved[i].data(), 0x38);
      *reinterpret_cast<int*>(field + 8) = count;
      nl_move_reason_.store(10);
      return false;
    }
    auto* p = static_cast<std::byte*>(step);
    std::memset(p + 0x10, 0, 0x28);
    *reinterpret_cast<unsigned*>(p + 0x10) =
        4u | (item.analog ? 0x18u : 0) | (item.button ? 3u : 0) | (item.angle ? 0x40u : 0);
    *reinterpret_cast<float*>(p + 0x24) = item.when;
    *reinterpret_cast<float*>(p + 0x34) = item.yaw;
    if (emitted) emitted->push_back(step);
    *reinterpret_cast<float*>(p + 0x28) = item.forward;
    *reinterpret_cast<float*>(p + 0x2c) = item.left;
    *reinterpret_cast<std::uint64_t*>(p + 0x18) = item.button;
    *reinterpret_cast<std::uint8_t*>(p + 0x20) = item.pressed;
  }
  std::uintptr_t current_rep = 0;
  int size = 0;
  (void)game::read(field + 16, current_rep);
  (void)game::read(field + 8, size);
  if (size > 0) {
    auto** slots = reinterpret_cast<void**>(current_rep + 8);
    std::stable_sort(slots, slots + size, [](void* a, void* b) { return time_of(a) < time_of(b); });
  }
  if (analog) {
    nl_owned_count_ = 0;
    nl_first_yaw_ = nl_restore_yaw_ = 0;
  }
  return true;
}
std::optional<float> Input::nl_landing_fraction() noexcept {
  const auto& f = nl_fields_;
  if (!f.trace_ready || !nl_trace_.ready() || nl_state_.ground || nl_state_.velocity.z > 0)
    return {};
  LandingQuery q;
  q.pawn = nl_state_.pawn;
  q.velocity = nl_state_.velocity;
  q.gravity = stop_gravity_.load();
  std::uintptr_t scene = 0;
  float gravity_scale = 1;
  const auto collision = q.pawn + f.collision;
  if (!game::read(q.pawn + f.scene, scene) || !scene || !game::read(scene + f.origin, q.origin) ||
      !game::read(collision + f.mins, q.mins) || !game::read(collision + f.maxs, q.maxs) ||
      !game::read(collision + f.attribute + f.mask, q.mask) ||
      !game::read(q.pawn + f.gravity, gravity_scale) || !std::isfinite(gravity_scale) ||
      gravity_scale <= 0)
    return {};
  q.gravity *= gravity_scale;
  nl_trace_calls_.fetch_add(1);
  const auto contact = nl_trace_.landing(q);
  if (contact) {
    nl_trace_hits_.fetch_add(1);
    nl_trace_fraction_.store(*contact);
  }
  return contact;
}
void Input::nl_bhop_tick(void* cmd, std::uint8_t* base, std::uint64_t* buttons) noexcept {
  bhop_mode_.store(0);
  if (!bhop_.load() || !cmd || !base || !buttons || !nl_read_state()) return;
  const bool held = ((buttons[0] | buttons[2]) & button::k_jump) != 0;
  int tick = 0;
  std::uint32_t number = 0;
  (void)game::read(reinterpret_cast<std::uintptr_t>(base) + 0x54, tick);
  (void)game::read(reinterpret_cast<std::uintptr_t>(cmd) + 8, number);
  int build_tick = tick;
  int tick_base = 0;
  const auto ctrl = controller();
  const auto tick_field =
      client_base_ ? game::schema_field("CBasePlayerController", "m_nTickBase") : std::nullopt;
  if (ctrl && tick_field && game::read(ctrl + *tick_field, tick_base) && tick_base > 0) {
    // Keep the same phase on repeated builds of this command.
    if (nl_hop_clock_command_ != static_cast<int>(number) || nl_hop_clock_pawn_ != nl_state_.pawn) {
      nl_hop_clock_command_ = static_cast<int>(number);
      nl_hop_clock_pawn_ = nl_state_.pawn;
      nl_hop_build_tick_ = tick_base + 1;
    }
    build_tick = nl_hop_build_tick_;
  }
  aim::NlHopState state{held,       nl_state_.ground,      nl_state_.landed_tick > 0,
                        build_tick, nl_state_.landed_tick, nl_state_.landed_fraction};
  // A fresh landing record must not request a second jump during ascent.
  if (!state.grounded && nl_state_.velocity.z > 1) state.record_valid = false;
  if (held && !state.grounded && nl_state_.velocity.z <= 0) {
    if (const auto contact = nl_landing_fraction()) {
      state.predicted_contact = true;
      state.contact_fraction = *contact;
    }
  }
  state.actual_valid = nl_state_.actual_tick > 0;
  state.actual_tick = nl_state_.actual_tick;
  state.actual_fraction = nl_state_.actual_fraction;
  auto plan = aim::nl_hop_plan(state);
  // Prediction may already show ascent on a later render frame of the same
  // command. Preserve its accepted edge until physical release/new command.
  if (held && nl_hop_edge_command_ == static_cast<int>(number) &&
      nl_hop_edge_pawn_ == nl_state_.pawn)
    plan = {true, true, nl_hop_edge_when_, nl_hop_edge_mode_};
  if (!held) nl_hop_edge_command_ = -1;
  if (!plan.release_held) {
    (void)nl_replace_steps(base, {}, false, true);
    nl_move_reason_.store(7);
    return;
  }
  bhop_holding_ticks_.fetch_add(1);
  if (!nl_state_.ground) bhop_air_ticks_.fetch_add(1);
  std::vector<NlStep> steps;
  if (plan.pulse) {
    steps.push_back({plan.when, 0, 0, button::k_jump, true, false});
  }
  if (!nl_replace_steps(base, steps, false, true)) return;
  buttons[0] &= ~button::k_jump;
  // A native subtick press carries the edge. A scroll bit would introduce a
  // second, untimed press at command start and defeat the landing fraction.
  buttons[1] &= ~button::k_jump;
  buttons[2] &= ~button::k_jump;
  nl_bhop_encoded_ = plan.pulse;
  bhop_mode_.store(plan.mode);
  bhop_when_.store(plan.when);
  bhop_landed_tick_.store(nl_state_.landed_tick);
  bhop_landed_frac_.store(nl_state_.landed_fraction);
  bhop_client_tick_.store(tick);
  bhop_tick_base_.store(tick_base);
  bhop_build_.store(build_tick);
  bhop_age_.store(build_tick - nl_state_.landed_tick);
  bhop_cmd_number_.store(static_cast<int>(number));
  if (plan.pulse) {
    nl_hop_edge_command_ = static_cast<int>(number);
    nl_hop_edge_pawn_ = nl_state_.pawn;
    nl_hop_edge_when_ = plan.when;
    nl_hop_edge_mode_ = plan.mode;
    bhop_writes_.fetch_add(1);
    bhop_presses_.fetch_add(1);
  } else
    nl_move_reason_.store(11);
}
void Input::nl_strafe_tick(void* base, void* cmd) noexcept {
  strafe_steps_.store(0);
  nl_counter_.store(0);
  nl_plan_gain_.store(0);
  nl_plan_begin_.store(0);
  nl_balanced_active_.store(false);
  nl_terminal_lateral_.store(0);
  strafe_airborne_.store(false);
  if (!base || !cmd) return;
  if (!strafe_enabled_.load() || !nl_read_state()) {
    if (nl_owned_command_ == nl_current_command_ && nl_owned_count_)
      (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    return;
  }
  const auto held =
      *reinterpret_cast<std::uint64_t*>(static_cast<std::byte*>(cmd) + k_cmd_buttons_offset);
  if (nl_direction_pawn_ != nl_state_.pawn) {
    nl_direction_pawn_ = nl_state_.pawn;
    nl_direction_buttons_ = nl_pressed_buttons_ = 0;
    nl_phase_command_ = -1;
    nl_phase_ = 0;
    nl_phase_was_near_ = false;
  }
  nl_pressed_buttons_ &= held;
  // Match the reference's last-pressed handling: A then D should steer right
  // even while A remains held, rather than canceling both axes to zero.
  const auto select = [&](std::uint64_t key, std::uint64_t opposite) {
    if (!(held & key))
      nl_pressed_buttons_ &= ~key;
    else if (!(nl_direction_buttons_ & key) || !(nl_pressed_buttons_ & opposite)) {
      nl_pressed_buttons_ = (nl_pressed_buttons_ & ~opposite) | key;
    }
  };
  select(button::k_moveleft, button::k_moveright);
  select(button::k_moveright, button::k_moveleft);
  select(button::k_forward, button::k_back);
  select(button::k_back, button::k_forward);
  nl_direction_buttons_ = held;
  if (nl_blend_.load() <= 0) {
    (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    nl_move_reason_.store(16);
    return;
  }
  if (nl_state_.ground ||
      (nl_bhop_encoded_ && (bhop_mode_.load() == 6 || bhop_mode_.load() == 4))) {
    (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    strafe_reason_.store(1);
    nl_move_reason_.store(2);
    return;
  }
  std::uintptr_t angles = 0;
  float yaw = 0, axes[3]{};
  const auto b = reinterpret_cast<std::uintptr_t>(base);
  if (!game::read(b + 0x40, angles) || !game::read(angles + 0x1c, yaw) ||
      !game::read_bytes(b + 0x58, axes, 12) || !std::isfinite(yaw)) {
    nl_move_reason_.store(5);
    return;
  }
  auto* buttons =
      reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(cmd) + k_cmd_buttons_offset);
  float forward = axes[0], side = axes[1];
  game::Vec3 camera;
  float intent_yaw = yaw;
  if (ccsgo_input_ && native_command_.view(reinterpret_cast<void*>(ccsgo_input_), camera) &&
      std::isfinite(camera.y))
    intent_yaw = camera.y;
  constexpr auto movement =
      button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
  if (buttons[0] & movement) {
    forward = ((nl_pressed_buttons_ & button::k_forward) ? 1.0F : 0) -
              ((nl_pressed_buttons_ & button::k_back) ? 1.0F : 0);
    side = ((nl_pressed_buttons_ & button::k_moveleft) ? 1.0F : 0) -
           ((nl_pressed_buttons_ & button::k_moveright) ? 1.0F : 0);
  }
  // Player intent is read again on each rebuilt command. Never feed an
  // assisted output direction back as the next frame's physical input.
  if (std::fabs(forward) + std::fabs(side) < .001F ||
      std::hypot(nl_state_.velocity.x, nl_state_.velocity.y) < 5) {
    (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    strafe_reason_.store(2);
    nl_move_reason_.store(13);
    return;
  }
  const float goal = intent_yaw + std::atan2(side, forward) * 180 / k_pi;
  // Direction keys select real-camera intent; the emitted impulses retain the
  // current command frame, which silent aim may already have rotated.
  if (std::fabs(axes[0]) + std::fabs(axes[1]) > .001F) {
    forward = axes[0];
    side = axes[1];
  }
  strafe_goal_.store(goal);
  const auto field = b + 0x18;
  int count = 0;
  std::uintptr_t rep = 0;
  (void)game::read(field + 8, count);
  (void)game::read(field + 16, rep);
  int keep = 0;
  float physical_end = -1;
  std::vector<std::pair<float, float>> yaw_events;
  std::vector<std::pair<float, std::uintptr_t>> yaw_slots;
  std::vector<NlStep> physical_moves;
  for (int i = 0; i < count && i < 32; ++i) {
    std::uintptr_t p = 0;
    unsigned bits = 0;
    if (!game::read(rep + 8 + i * 8, p) || !game::read(p + 0x10, bits)) return;
    const bool owned = nl_owns_yaw(p);
    if (bits & ~(4u | (owned ? 0x58u : 0u))) {
      ++keep;
      float when = 0;
      if (!game::read(p + 0x24, when) || !std::isfinite(when) || when < 0 || when > 1) return;
      std::uint64_t event_button = 0;
      if (!game::read(p + 0x18, event_button)) return;
      constexpr auto direction_buttons = button::k_forward | button::k_back | button::k_moveleft |
                                         button::k_moveright | button::k_jump;
      if ((bits & 0x18u) || ((bits & 3u) && (event_button & direction_buttons)))
        physical_end = std::max(physical_end, when);
    }
    if ((bits & 0x18u) && !owned) {
      float t = 0, f = 0, l = 0;
      if (!game::read(p + 0x24, t) || ((bits & 8) && !game::read(p + 0x28, f)) ||
          ((bits & 16) && !game::read(p + 0x2c, l)) || !std::isfinite(t) || !std::isfinite(f) ||
          !std::isfinite(l))
        return;
      physical_moves.push_back({t, f, l});
    }
    if ((bits & 0x40) && !owned) {
      float t = 0, delta = 0;
      if (!game::read(p + 0x24, t) || !game::read(p + 0x34, delta) || !std::isfinite(t) ||
          !std::isfinite(delta))
        return;
      yaw_events.emplace_back(t, delta);
      yaw_slots.emplace_back(t, p);
    }
  }
  const float begin = physical_end < 0 ? 0 : (std::floor(physical_end * 64) + 1) / 64;
  // Preserve the player's complete event prefix. A late event leaves no room
  // for assisted steering; release our old steps and keep native control.
  if (begin >= 1) {
    (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    nl_move_reason_.store(15);
    return;
  }
  const bool reconcile_prefix = begin > 0 && physical_moves.empty();
  const int available = 32 - keep - (reconcile_prefix ? 1 : 0) - 1;
  if (available < 1) {
    nl_move_reason_.store(9);
    return;
  }
  aim::NlAirState state;
  state.velocity = {nl_state_.velocity.x, nl_state_.velocity.y};
  state.begin = begin;
  state.goal_yaw = goal;
  state.wish_speed = nl_state_.max_speed * (1 - .66F * nl_state_.duck);
  state.stamina = nl_state_.stamina;
  state.balanced_lateral = nl_physics_state_.valid && !nl_physics_state_.quantized;
  state.smooth_yaw = !state.balanced_lateral;
  game::Vec2 starting{forward, side};
  if (!physical_moves.empty()) {
    starting = nl_state_.previous;
    for (const auto& m : physical_moves)
      if (m.when <= begin) {
        starting.x += m.forward;
        starting.y += m.left;
      }
  }
  float prefix_yaw = 0;
  for (const auto& [when, delta] : yaw_events)
    if (when <= begin) prefix_yaw += delta;
  state.seed_yaw = yaw + prefix_yaw + std::atan2(starting.y, starting.x) * 180 / k_pi;
  state.friction = nl_state_.friction;
  state.air_acceleration =
      nl_physics_state_.valid ? nl_physics_state_.air_acceleration : stop_air_acceleration_.load();
  state.air_cap = nl_physics_state_.air_cap;
  state.stamina_recovery = nl_physics_state_.stamina_recovery;
  std::vector<float> synchronized;
  for (const auto& [when, delta] : yaw_events)
    if (when >= begin) synchronized.push_back(when);
  std::sort(synchronized.begin(), synchronized.end());
  synchronized.erase(std::unique(synchronized.begin(), synchronized.end()), synchronized.end());
  // Mouse events do not consume the entire gain window. Reconcile at their
  // exact times so their retained physical yaw cannot invalidate an interval.
  const int regular_budget = available;
  if (regular_budget < 1) {
    (void)nl_replace_steps(base, {}, true, false, nullptr, true);
    nl_move_reason_.store(9);
    return;
  }
  state.steps = std::min(std::clamp(strafe_subticks_.load(), 1, 32), regular_budget);
  state.blend = nl_blend_.load();
  state.allow_counter = false;
  // Advance through the unmodified physical prefix before planning its suffix.
  // Otherwise a late mouse/key event would still receive a whole tick's gain.
  std::vector<float> boundaries{0, begin};
  for (const auto& move : physical_moves)
    if (move.when > 0 && move.when < begin) boundaries.push_back(move.when);
  for (const auto& [when, delta] : yaw_events)
    if (when > 0 && when < begin) boundaries.push_back(when);
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  for (std::size_t i = 1; i < boundaries.size(); ++i) {
    const float when = boundaries[i - 1];
    game::Vec2 axes = physical_moves.empty() ? game::Vec2{forward, side} : nl_state_.previous;
    for (const auto& move : physical_moves)
      if (move.when <= when) {
        axes.x += move.forward;
        axes.y += move.left;
      }
    float angle = yaw;
    for (const auto& [time, delta] : yaw_events)
      if (time <= when) angle += delta;
    const float magnitude = game::length(axes);
    if (magnitude > .001F) {
      const float rad = angle * k_pi / 180;
      const game::Vec2 wish{(std::cos(rad) * axes.x - std::sin(rad) * axes.y) / magnitude,
                            (std::sin(rad) * axes.x + std::cos(rad) * axes.y) / magnitude};
      const float speed = state.wish_speed * std::min(1.0F, magnitude);
      const float dt = (boundaries[i] - when) * state.tick_interval;
      const float gain =
          std::min(std::max(0.0F, std::min(state.air_cap, speed) - game::dot(state.velocity, wish)),
                   state.air_acceleration * speed * state.friction * dt);
      state.velocity = state.velocity + wish * gain;
      state.stamina = std::max(0.0F, state.stamina - state.stamina_recovery * dt);
    }
  }
  if (nl_phase_command_ != nl_current_command_) {
    // Advance the alternation only after a near-heading gain choice. A forced
    // correction on the next command must not consume the opposite gain side.
    if (nl_phase_command_ != -1 && nl_phase_was_near_) ++nl_phase_;
    const float heading = std::atan2(state.velocity.y, state.velocity.x) * 180 / k_pi;
    nl_phase_was_near_ = std::fabs(normalize_angle(goal - heading)) <= 2;
    nl_phase_command_ = nl_current_command_;
  }
  state.directional_feedback = true;
  state.phase = nl_phase_;
  const auto plan = aim::nl_air_plan(state, synchronized);
  if (!plan.valid) {
    nl_move_reason_.store(5);
    return;
  }
  std::vector<NlStep> steps;
  struct Augment {
    std::uintptr_t pointer{};
    float yaw{}, forward{}, left{};
    bool analog{};
  };
  std::vector<Augment> augment;
  float own_yaw = 0;
  if (reconcile_prefix)
    steps.push_back({0, forward - nl_state_.previous.x, side - nl_state_.previous.y, 0, false, true,
                     0, !state.balanced_lateral});
  game::Vec2 simulated = state.velocity;
  game::Vec2 previous_analog = reconcile_prefix
                                   ? game::Vec2{forward, side}
                                   : (!physical_moves.empty() ? starting : nl_state_.previous);
  float maximum_counter = 0;
  for (int i = 0; i < plan.count; ++i) {
    const auto& e = plan.events[i];
    const float speed = game::length(simulated), magnitude = game::length(e.world_wish);
    const auto unit = magnitude > .000001F ? e.world_wish * (1 / magnitude) : game::Vec2{};
    if (speed > 0) maximum_counter = std::max(maximum_counter, -game::dot(simulated, unit) / speed);
    const float end = i + 1 < plan.count ? plan.events[i + 1].when : 1;
    const float wish_speed = state.wish_speed * std::min(1.0F, magnitude);
    const float acceleration =
        std::min(std::max(0.0F, std::min(state.air_cap, wish_speed) - game::dot(simulated, unit)),
                 state.air_acceleration * wish_speed * state.friction * (end - e.when) *
                     state.tick_interval);
    simulated = simulated + unit * acceleration;
    float physical_yaw = 0;
    for (const auto& [t, delta] : yaw_events)
      if (t <= e.when) physical_yaw += delta;
    if (state.balanced_lateral) {
      const float frame = (yaw + physical_yaw) * k_pi / 180;
      const game::Vec2 axes{std::cos(frame) * e.world_wish.x + std::sin(frame) * e.world_wish.y,
                            -std::sin(frame) * e.world_wish.x + std::cos(frame) * e.world_wish.y};
      const auto delta = axes - previous_analog;
      std::uintptr_t shared = 0;
      if (i != 0)
        for (const auto& [when, pointer] : yaw_slots)
          if (when == e.when) shared = pointer;
      if (shared)
        augment.push_back({shared, 0, delta.x, delta.y, true});
      else
        steps.push_back({e.when, delta.x, delta.y, 0, false, true, 0, false});
      previous_analog = axes;
      continue;
    }
    const float wanted = std::atan2(e.world_wish.y, e.world_wish.x) * 180 / k_pi;
    // Keep the engine's movement impulses and button events. Rotate their
    // actual basis instead of rewriting every direction to synthetic keys.
    game::Vec2 impulses{forward, side};
    if (!physical_moves.empty()) {
      impulses = nl_state_.previous;
      for (const auto& move : physical_moves)
        if (move.when <= e.when) {
          impulses.x += move.forward;
          impulses.y += move.left;
        }
    }
    const bool moving = std::fabs(impulses.x) + std::fabs(impulses.y) > .001F;
    const float input_yaw = moving ? std::atan2(impulses.y, impulses.x) * 180 / k_pi : 0;
    const float delta =
        moving ? normalize_angle(wanted - input_yaw - yaw - physical_yaw - own_yaw) : 0;
    const bool reconcile = i == 0 && physical_moves.empty() && !reconcile_prefix;
    std::uintptr_t shared_slot = 0;
    if (i != 0 && !reconcile)
      for (const auto& [when, pointer] : yaw_slots)
        if (when == e.when) shared_slot = pointer;
    if (shared_slot)
      augment.push_back({shared_slot, delta, 0, 0, false});
    else
      steps.push_back({e.when, reconcile ? forward - nl_state_.previous.x : 0,
                       reconcile ? side - nl_state_.previous.y : 0, 0, false, reconcile, delta,
                       true});
    own_yaw += delta;
  }
  // Restore only our synthetic yaw at the command boundary, preserving the
  // player's physical mouse delta. The near-1 timestamp matches the reference
  // and gives the accelerating final slice practically its entire duration.
  const std::size_t restore_index = steps.size();
  if (state.balanced_lateral)
    steps.push_back({std::nextafter(1.0F, 0.0F), forward - previous_analog.x,
                     side - previous_analog.y, 0, false, true, 0, false});
  else
    steps.push_back(
        {std::nextafter(1.0F, 0.0F), 0, 0, 0, false, false, normalize_angle(-own_yaw), true});
  std::vector<void*> emitted;
  if (!nl_replace_steps(base, steps, true, false, &emitted, true)) return;
  nl_restore_yaw_ =
      state.balanced_lateral ? 0 : reinterpret_cast<std::uintptr_t>(emitted[restore_index]);
  // Share existing mouse timestamps rather than spending an extra event slot
  // for every mouse sample. Retain both original and modified payloads so a
  // rebuild/off transition restores only our own additive contribution.
  nl_augmented_count_ = 0;
  for (const auto& change : augment) {
    const auto pointer = change.pointer;
    auto& entry = nl_augmented_[nl_augmented_count_++];
    entry.pointer = pointer;
    (void)game::read_bytes(pointer + 0x10, entry.original.data(), entry.original.size());
    *reinterpret_cast<float*>(pointer + 0x34) += change.yaw;
    if (change.analog) {
      auto& bits = *reinterpret_cast<unsigned*>(pointer + 0x10);
      auto* axes = reinterpret_cast<float*>(pointer + 0x28);
      axes[0] = ((bits & 8u) ? axes[0] : 0) + change.forward;
      axes[1] = ((bits & 16u) ? axes[1] : 0) + change.left;
      bits |= 0x18u;
    }
    (void)game::read_bytes(pointer + 0x10, entry.encoded.data(), entry.encoded.size());
  }
  nl_owned_command_ = nl_current_command_;
  for (std::size_t i = 0; i < steps.size(); ++i)
    if (steps[i].angle || steps[i].analog) {
      const auto pointer = reinterpret_cast<std::uintptr_t>(emitted[i]);
      auto& owned = nl_owned_yaws_[nl_owned_count_++];
      owned.pointer = pointer;
      owned.value = steps[i].yaw;
      (void)game::read_bytes(pointer + 0x10, owned.encoded.data(), owned.encoded.size());
      if (steps[i].angle && !nl_first_yaw_) nl_first_yaw_ = pointer;
    }
  nl_movement_encoded_ = true;
  nl_previous_axes_ = nl_state_.previous;
  strafe_airborne_.store(true);
  strafe_steps_.store(plan.count);
  nl_counter_.store(maximum_counter);
  nl_plan_gain_.store(game::length(plan.final_velocity) - game::length(state.velocity));
  nl_plan_begin_.store(begin);
  nl_balanced_active_.store(state.balanced_lateral);
  const float target_angle = goal * k_pi / 180;
  nl_terminal_lateral_.store(
      game::dot(plan.final_velocity, game::Vec2{-std::sin(target_angle), std::cos(target_angle)}));
  strafe_speed_.store(game::length(state.velocity));
  strafe_goal_.store(goal);
  strafe_reason_.store(0);
  strafe_writes_.fetch_add(1);
  nl_move_reason_.store(1);
}
void Input::nl_reconcile_jump_move(void* base, void* cmd) noexcept {
  if (!nl_bhop_encoded_ || nl_movement_encoded_ || !nl_state_cached_) return;
  const auto b = reinterpret_cast<std::uintptr_t>(base);
  int size = 0;
  std::uintptr_t rep = 0;
  (void)game::read(b + 0x20, size);
  (void)game::read(b + 0x28, rep);
  for (int i = 0; i < size && i < 32; ++i) {
    std::uintptr_t p = 0;
    unsigned bits = 0;
    if (!game::read(rep + 8 + i * 8, p) || !game::read(p + 0x10, bits)) return;
    if (bits & 0x18) return;
  }
  float axes[2]{};
  if (!game::read_bytes(b + 0x58, axes, 8)) return;
  const auto buttons =
      *reinterpret_cast<std::uint64_t*>(static_cast<std::byte*>(cmd) + k_cmd_buttons_offset);
  if (std::fabs(axes[0]) + std::fabs(axes[1]) < .001F) {
    axes[0] = ((buttons & button::k_forward) ? 1.0F : 0) - ((buttons & button::k_back) ? 1.0F : 0);
    axes[1] =
        ((buttons & button::k_moveleft) ? 1.0F : 0) - ((buttons & button::k_moveright) ? 1.0F : 0);
  }
  const std::vector<NlStep> steps{
      {0, axes[0] - nl_state_.previous.x, axes[1] - nl_state_.previous.y, 0, false, true}};
  if (nl_replace_steps(base, steps, true, false)) {
    nl_movement_encoded_ = true;
    nl_previous_axes_ = nl_state_.previous;
  }
}
}  // namespace cs2::control
