#include "cs2/control/trigger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

#include "cs2/aim/reader.hpp"
#include "cs2/aim/solver.hpp"
#include "cs2/control/input.hpp"
#include "cs2/game/snapshot.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {
namespace {

// The microsecond clock (the solve's cost instrumentation).
auto now_us() noexcept -> std::uint64_t {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

// The player's body spans the feet (`origin`) to the eyes (`eye_origin`). The
// trigger's cheap pre-check uses that band: the crosshair is on the player when
// the view direction falls inside the cone the body subtends.
constexpr float k_body_half_height = 0.9F;  // the band around the body's centre

// The slack the pre-check adds to the body's angular radius: the solved aim
// point is a hitbox, not the body's centre, and the hitbox may sit outside the
// feet-to-eyes band (a crouched player, a leaning one).
constexpr float k_gate_slack_deg = 3.0F;

// Radians <-> degrees.
constexpr float k_rad_to_deg = 57.295779513082320876F;

// The view's forward direction, in the engine's convention (the pitch is
// positive down, `docs/re/subtick.md`).
[[nodiscard]] auto forward_of(float yaw_deg, float pitch_deg) noexcept -> game::Vec3 {
  const float yaw = yaw_deg * k_deg_to_rad;
  const float pitch = pitch_deg * k_deg_to_rad;
  return game::Vec3{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                    -std::sin(pitch)};
}

// The angle between two directions, in degrees.
[[nodiscard]] auto angle_between(const game::Vec3& a, const game::Vec3& b) noexcept -> float {
  const float dot = game::dot(a, b);
  return std::acos(std::clamp(dot, -1.0F, 1.0F)) * k_rad_to_deg;
}

// The trigger's cheap pre-check: the entity index of the enemy the crosshair is
// plausibly on, or -1.
//
// This is the trigger's **cost gate**. The solve is the expensive part (the
// ragebot's own solve is measured at ~5 ms of the tick at the best spot), so it
// only runs when the crosshair is on somebody. The test needs no engine read at
// all: the players' positions are already in the snapshot.
[[nodiscard]] auto crosshair_target(const game::Snapshot& snapshot, const game::Vec3& eye,
                                    const game::Vec3& forward, float slack_deg) noexcept -> int {
  int best = -1;
  float best_angle = 0.0F;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || player.pawn == 0) {
      continue;
    }
    if (player.team != 2 && player.team != 3) {
      continue;  // not an enemy
    }
    const game::Vec3 centre{(player.origin.x + player.eye_origin.x) * 0.5F,
                            (player.origin.y + player.eye_origin.y) * 0.5F,
                            (player.origin.z + player.eye_origin.z) * 0.5F};
    const game::Vec3 to_centre = centre - eye;
    const float distance = game::length(to_centre);
    if (distance <= 1.0F) {
      continue;  // too close to tell
    }
    // The body's angular radius: the half height over the distance, plus the
    // slack. The feet-to-eyes band is ~1.8 m tall, so the half height is ~0.9 m.
    const float radius = std::atan2(k_body_half_height, distance) * k_rad_to_deg + slack_deg;
    const float angle = angle_between(forward, game::normalized(to_centre));
    if (angle > radius) {
      continue;  // the crosshair is not on this player
    }
    if (best < 0 || angle < best_angle) {
      best = player.index;
      best_angle = angle;
    }
  }
  return best;
}

// Is the weapon one the trigger may fire with?
//
// The ragebot's rule (`is_rage_weapon` in `service.cpp`), repeated here because
// that helper is file-local and the trigger must not accept a grenade: the
// attack button throws it.
[[nodiscard]] auto is_firearm(int weapon_type, const std::string& name) noexcept -> bool {
  if (weapon_type >= 1 && weapon_type <= 6) {
    return true;  // pistol / smg / rifle / shotgun / sniper / machinegun
  }
  if (weapon_type > 6 || weapon_type == 0) {
    return false;  // a known non-firearm, or the knife
  }
  // Unknown type: any `weapon_*` that is not on the deny-list.
  static constexpr const char* k_deny[] = {
      "weapon_knife",      "weapon_c4",           "weapon_taser",        "weapon_flashbang",
      "weapon_hegrenade",  "weapon_smokegrenade", "weapon_molotov",      "weapon_incgrenade",
      "weapon_decoy",      "weapon_tagrenade",    "weapon_breachcharge", "weapon_bumpmine",
      "weapon_healthshot", "weapon_shield"};
  for (const char* deny : k_deny) {
    if (name == deny) {
      return false;
    }
  }
  return name.starts_with("weapon_");
}

// The hitgroup of the hitbox the solve's aim point landed on (1 = head).
//
// The solve does not carry the hitgroup, so it is recovered the same way the
// ragebot's diagnostic does it: the hitbox whose centre is angularly closest
// to the aim direction.
[[nodiscard]] auto hitgroup_for(const aim::SolveInput& input, const game::Vec3& direction) noexcept
    -> int {
  int best_group = 0;
  float best_dot = -2.0F;
  for (const aim::Hitbox& hitbox : input.hitboxes) {
    const game::Vec3 centre =
        hitbox.is_capsule ? (hitbox.capsule.a + hitbox.capsule.b) * 0.5F : hitbox.box.center;
    const game::Vec3 to_centre = game::normalized(centre - input.eye);
    const float d = game::dot(direction, to_centre);
    if (d > best_dot) {
      best_dot = d;
      best_group = hitbox.group;
    }
  }
  return best_group;
}

}  // namespace

// --- The runtime ---------------------------------------------------------------

void Trigger::configure(const TriggerConfig& config) noexcept {
  on_.store(config.on, std::memory_order_relaxed);
  hitchance_.store(config.hitchance, std::memory_order_relaxed);
  head_hitchance_.store(config.head_hitchance, std::memory_order_relaxed);
  yaw_tolerance_.store(config.yaw_tolerance, std::memory_order_relaxed);
  pitch_tolerance_.store(config.pitch_tolerance, std::memory_order_relaxed);
  delay_ms_.store(config.delay_ms, std::memory_order_relaxed);
  key_.store(config.key, std::memory_order_relaxed);
  fire_.store(config.fire, std::memory_order_relaxed);
  mode_.store(static_cast<int>(config.mode), std::memory_order_relaxed);
  solve_interval_ms_.store(config.solve_interval_ms, std::memory_order_relaxed);
  force_.store(config.force, std::memory_order_relaxed);
}

auto Trigger::config() const noexcept -> TriggerConfig {
  TriggerConfig config{};
  config.on = on_.load(std::memory_order_relaxed);
  config.hitchance = hitchance_.load(std::memory_order_relaxed);
  config.head_hitchance = head_hitchance_.load(std::memory_order_relaxed);
  config.yaw_tolerance = yaw_tolerance_.load(std::memory_order_relaxed);
  config.pitch_tolerance = pitch_tolerance_.load(std::memory_order_relaxed);
  config.delay_ms = delay_ms_.load(std::memory_order_relaxed);
  config.key = key_.load(std::memory_order_relaxed);
  config.fire = fire_.load(std::memory_order_relaxed);
  config.mode = static_cast<TriggerMode>(mode_.load(std::memory_order_relaxed));
  config.solve_interval_ms = solve_interval_ms_.load(std::memory_order_relaxed);
  config.force = force_.load(std::memory_order_relaxed);
  return config;
}

void Trigger::publish(const TriggerDecision& decision) noexcept {
  last_hold_.store(decision.hold, std::memory_order_relaxed);
  last_reason_.store(static_cast<std::uint32_t>(decision.reason), std::memory_order_relaxed);
  last_delay_ms_.store(decision.delay_remaining_ms, std::memory_order_relaxed);
}

void Trigger::release(Input& input) noexcept {
  if (pressed_) {
    input.release_feature(button::k_attack);
    pressed_ = false;
  }
  latch_.armed = false;
  latch_.armed_at_ms = 0;
  latch_.firing = false;
}

void Trigger::reset(Input& input) noexcept {
  release(input);
  latch_ = TriggerLatch{};
  solve_ = TriggerSolve{};
  last_solve_ms_ = 0;
  local_pawn_ = 0;
  solved_target_ = -1;
  target_.store(-1, std::memory_order_relaxed);
  publish(TriggerDecision{});
}

void Trigger::tick(const game::Snapshot& snapshot, Input& input, std::uint64_t now) noexcept {
  evaluations_.fetch_add(1, std::memory_order_relaxed);
  const TriggerConfig config = this->config();

  if (!config.on) {
    reset(input);
    return;
  }
  if (!snapshot.valid || !snapshot.local.has_value() || !snapshot.local->alive) {
    reset(input);
    return;
  }
  const game::PlayerSnapshot& local = *snapshot.local;
  // A new pawn (a new round, a respawn) is a new game state: the trigger's
  // state does not survive it.
  if (local.pawn != local_pawn_) {
    release(input);
    local_pawn_ = local.pawn;
  }
  // Only real firearms (the ragebot's rule): the knife, the C4, the taser, the
  // grenades and the equipment are not targets — the attack button throws a
  // grenade.
  if (!is_firearm(local.weapon_type, local.weapon)) {
    release(input);
    return;
  }

  // The trigger key's state: the operating system's own key state. It is never
  // read from the command's button mask, which the trigger itself writes (the
  // auto-strafe's latch landmine).
  bool key_held = false;
#if defined(_WIN32)
  const int key = config.key;
  key_held = key > 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
#endif

  const TriggerView view{local.eye_angles.yaw, local.eye_angles.pitch,
                         config.force ? true : key_held};
  const game::Vec3 forward = forward_of(view.yaw, view.pitch);

  // --- The cheap pre-check: is the crosshair plausibly on somebody? ---------
  //
  // This is the cost gate. Below it, the trigger costs a handful of floating
  // point operations per player; above it, the solve runs.
  const int target = crosshair_target(snapshot, local.eye_origin, forward, k_gate_slack_deg);
  if (target < 0) {
    gate_skips_.fetch_add(1, std::memory_order_relaxed);
    release(input);
    latch_.key_down = view.key_held;
    TriggerDecision quiet{};
    quiet.reason = TriggerReason::no_target;
    quiet.latch = latch_;
    publish(quiet);
    return;
  }
  target_.store(target, std::memory_order_relaxed);

  // --- The solve (the rate-limited, expensive part) --------------------------
  //
  // The trigger reuses the ragebot's solver: the same `aim::solve`, the same
  // weapon model, the same hit probability. The difference is the scope: the
  // trigger solves for **one** target (the one under the crosshair), so the cost
  // does not grow with the number of enemies in the scene.
  //
  // A **new target** bypasses the rate limit: the cached solve belongs to the
  // previous target, and the trigger must not fire on it.
  const bool target_changed = target != solved_target_;
  if (target_changed || now - last_solve_ms_ >= config.solve_interval_ms) {
    const std::uint64_t start = now_us();
    const aim::WeaponProfile weapon;
    std::optional<aim::SolveInput> input_data = aim::build_input(snapshot, weapon, target, true);
    if (input_data.has_value() && !input_data->hitboxes.empty()) {
      input_data->objective = aim::AimObjective::hitchance;
      // The hitchance cone: the weapon's inaccuracy + spread, the same terms
      // the ragebot uses (`docs/re/aim.md` §"Inaccuracy").
      const float inaccuracy = aim::inaccuracy(input_data->weapon, 0.0F, true, false);
      input_data->spread_sigma = std::sqrt(inaccuracy * inaccuracy +
                                           input_data->weapon.spread * input_data->weapon.spread);
      const aim::SolveResult result = aim::solve(*input_data);
      if (result.valid) {
        TriggerSolve solved{};
        solved.valid = true;
        solved.hitchance = result.hitchance;
        solved.yaw = std::atan2(result.direction.y, result.direction.x) * k_rad_to_deg;
        solved.pitch = -std::asin(std::clamp(result.direction.z, -1.0F, 1.0F)) * k_rad_to_deg;
        solved.head = hitgroup_for(*input_data, result.direction) == 1;
        solve_ = solved;
      } else {
        solve_ = TriggerSolve{};
      }
    } else {
      solve_ = TriggerSolve{};
    }
    solved_target_ = target;
    last_solve_us_.store(now_us() - start, std::memory_order_relaxed);
    last_solve_ms_ = now;
    solves_.fetch_add(1, std::memory_order_relaxed);
  }

  // --- The decision ----------------------------------------------------------
  const TriggerDecision decision = decide(view, solve_, config, now, latch_);
  latch_ = decision.latch;
  publish(decision);

  // --- The press / release ---------------------------------------------------
  //
  // Balanced: the shot is pressed when the decision starts it and released when
  // it stops. The trigger never writes a view angle — only the command's button
  // mask.
  //
  // The press goes through the **feature** path (`press_feature`), not the
  // control channel's: a feature's button reaches the command in every
  // backend, while the control channel's `press` is applied only in the
  // `usercmd` backend. The trigger used to press through the control channel's
  // mask, so in the default `console` backend it decided to fire, counted the
  // shot and answered `{"firing": true}` — and the gun never fired.
  const bool want = config.fire && decision.hold;
  if (want && !pressed_) {
    input.press_feature(button::k_attack);
    pressed_ = true;
    shots_.fetch_add(1, std::memory_order_relaxed);
  } else if (!want && pressed_) {
    input.release_feature(button::k_attack);
    pressed_ = false;
  }
}

auto Trigger::last_decision() const noexcept -> TriggerDecision {
  TriggerDecision decision{};
  decision.hold = last_hold_.load(std::memory_order_relaxed);
  decision.reason = static_cast<TriggerReason>(last_reason_.load(std::memory_order_relaxed));
  decision.delay_remaining_ms = last_delay_ms_.load(std::memory_order_relaxed);
  return decision;
}

auto Trigger::last_reason() const noexcept -> TriggerReason {
  return static_cast<TriggerReason>(last_reason_.load(std::memory_order_relaxed));
}

auto Trigger::evaluations() const noexcept -> std::uint64_t {
  return evaluations_.load(std::memory_order_relaxed);
}

auto Trigger::gate_skips() const noexcept -> std::uint64_t {
  return gate_skips_.load(std::memory_order_relaxed);
}

auto Trigger::solves() const noexcept -> std::uint64_t {
  return solves_.load(std::memory_order_relaxed);
}

auto Trigger::shots() const noexcept -> std::uint64_t {
  return shots_.load(std::memory_order_relaxed);
}

auto Trigger::last_solve_us() const noexcept -> std::uint64_t {
  return last_solve_us_.load(std::memory_order_relaxed);
}

auto Trigger::target_index() const noexcept -> int {
  return target_.load(std::memory_order_relaxed);
}

}  // namespace cs2::control
