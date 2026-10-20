#pragma once

// The triggerbot (`docs/ragebot.md` §"The trigger"): pull the trigger when the
// crosshair is on a target at a computed hit chance — and **never** write a view
// angle.
//
// Why a module of its own: the *decision* is a pure function of the view angles,
// the solve's answer, the configuration and the clock, so it lives in a header
// that depends on nothing but the standard library and the module's own angle
// helper. The host test (`control-trigger`) then covers every rule — the angle
// gate, the hitchance gate, the head preference, the reaction delay and the
// "already firing" latch — without a game.
//
// The *runtime* (the cheap pre-check, the solve, the press/release) is in
// `trigger.cpp` and runs on the **game thread**, in the same sink the ragebot's
// `rage_tick` runs in. It touches the command's **button** mask only
// (`Input::press`/`Input::release`): the trigger has no path to the view angles
// at all, which is what makes "it never aims" a property of the code and not a
// convention.

#include <atomic>
#include <cmath>
#include <cstdint>

#include "cs2/control/strafe_geometry.hpp"  // normalize_angle (the module's angle helper)

namespace cs2 {

namespace game {
// The game's point-in-time state (the trigger's input). Forward-declared so this
// header stays dependency-free: the host test compiles it on its own.
struct Snapshot;
}  // namespace game

namespace control {

// The command backend the trigger pulls the trigger through.
class Input;

// --- The configuration ---------------------------------------------------------

// How the trigger is engaged.
enum class TriggerMode : std::uint8_t {
  hold = 0,    // fire only while the trigger key is held (key 0 = always)
  always = 1,  // fire whenever the conditions hold
  toggle = 2,  // the trigger key's press edge toggles the trigger
};

// The trigger's configuration.
//
// It comes from the `trigger` **control command** and the payload's environment
// (`CS2_PAYLOAD_TRIGGER`), not from the wire settings block: the settings block
// is versioned and full, and the trigger is a control-channel feature.
struct TriggerConfig {
  bool on = false;               // the feature is enabled
  float hitchance = 0.75F;       // the minimum hit probability (0..1)
  float head_hitchance = 0.50F;  // the minimum on a head hitbox (0..1)
  float yaw_tolerance = 2.5F;    // degrees: the crosshair's horizontal error
  float pitch_tolerance = 2.5F;  // degrees: the crosshair's vertical error
  std::uint32_t delay_ms = 0;    // the reaction delay (a timestamp, never a sleep)
  int key = 0;                   // a Win32 virtual-key code (0 = no key)
  bool fire = true;              // the trigger pulls the trigger itself
  TriggerMode mode = TriggerMode::hold;
  // The solve's rate limit. The solve is the expensive part, so it runs at most
  // this often — and only when the cheap pre-check says the crosshair is on
  // somebody (see `trigger.cpp`).
  std::uint32_t solve_interval_ms = 16;
  // Bypass the trigger key's check (a scenario cannot synthesize a key press).
  bool force = false;
};

// --- The decision's inputs -----------------------------------------------------

// The current view (degrees). The game's convention: yaw 0 = +X, and the pitch
// is **positive down** (the engine's own `AngleVectors`, `docs/re/subtick.md`).
struct TriggerView {
  float yaw = 0.0F;
  float pitch = 0.0F;
  // The trigger key's state. It is read from the operating system's own key
  // state, never from a state the trigger writes (the auto-strafe's latch
  // landmine: a feature that reads the button mask it also writes latches on
  // its own output).
  bool key_held = false;
};

// The solve's answer, as the trigger consumes it.
//
// The trigger reuses `aim::solve` (the ragebot's solver) and adds only the
// *decision* on top, so this is the solve's own result reduced to what the
// decision needs: where the solve points, how likely that point is to be hit and
// whether it landed on a head hitbox (the solve does not carry the hitgroup).
struct TriggerSolve {
  bool valid = false;
  float hitchance = 0.0F;  // the probability the shot lands on the target
  float yaw = 0.0F;        // the solved aim point (degrees)
  float pitch = 0.0F;      // the solved aim point (degrees)
  bool head = false;       // the aim point landed on a head hitbox
};

// The trigger's state. The game thread owns it; the decision takes it as an
// input and returns the next one, so the decision stays a pure function.
struct TriggerLatch {
  bool armed = false;             // the conditions currently hold (the delay is running)
  std::uint64_t armed_at_ms = 0;  // when they first held
  bool firing = false;            // the shot is currently held down
  bool key_down = false;          // the trigger key's previous state (the toggle's edge)
  bool toggled = false;           // the toggle mode's state
};

// Why the trigger did (or did not) fire. The operator's window: the reason says
// *why* the trigger stayed quiet.
enum class TriggerReason : std::uint8_t {
  disabled = 0,    // the feature is off
  not_engaged,     // the trigger key is not held
  no_target,       // the crosshair is not on anybody
  no_solution,     // the solve found nothing to shoot at
  yaw_error,       // the crosshair is off the aim point (horizontally)
  pitch_error,     // the crosshair is off the aim point (vertically)
  hitchance,       // the hit chance is below the threshold
  delay,           // the reaction delay has not elapsed yet
  on_target,       // firing
  already_firing,  // the shot is already down (a continuation, not a new shot)
};

// The reason's operator-facing text.
[[nodiscard]] inline auto to_string(TriggerReason reason) noexcept -> const char* {
  switch (reason) {
    case TriggerReason::disabled:
      return "disabled";
    case TriggerReason::not_engaged:
      return "key not held";
    case TriggerReason::no_target:
      return "no target";
    case TriggerReason::no_solution:
      return "no solution";
    case TriggerReason::yaw_error:
      return "yaw error";
    case TriggerReason::pitch_error:
      return "pitch error";
    case TriggerReason::hitchance:
      return "hitchance too low";
    case TriggerReason::delay:
      return "reaction delay";
    case TriggerReason::on_target:
      return "on target";
    case TriggerReason::already_firing:
      return "already firing";
  }
  return "unknown";
}

// The decision for one evaluation.
//
// `fire` is an **edge** ("pull the trigger now") and `hold` a **level** ("the
// shot must stay down"): the runtime presses on the edge and releases when the
// level falls, so one continuous aim is one press and one release.
struct TriggerDecision {
  bool fire = false;  // start the shot now (an edge)
  bool hold = false;  // the shot must stay down (a level)
  TriggerReason reason = TriggerReason::disabled;
  std::uint32_t delay_remaining_ms = 0;  // the reaction delay still to wait
  TriggerLatch latch{};                  // the state for the next evaluation
};

// The whole decision.
//
// The rules, in order:
//   1. the feature is on and the trigger is engaged (the mode + the key);
//   2. the solve found a target;
//   3. the crosshair is on the solved aim point (the yaw and the pitch error,
//      each against its own tolerance);
//   4. the solve's hit chance is good enough — a head hitbox uses the lower
//      `head_hitchance` threshold (the classic "head = trigger" behaviour);
//   5. the reaction delay has elapsed since the conditions first held.
//
// Any rule that fails disarms the delay and releases the shot, so the next
// continuous aim starts the delay again.
[[nodiscard]] inline auto decide(const TriggerView& view, const TriggerSolve& solve,
                                 const TriggerConfig& config, std::uint64_t now_ms,
                                 const TriggerLatch& latch) -> TriggerDecision {
  TriggerDecision decision{};
  decision.latch = latch;

  // Any rule that fails: disarm the delay (the next continuous aim starts it
  // again) and release the shot.
  const auto stop = [&decision](TriggerReason reason) -> TriggerDecision {
    decision.reason = reason;
    decision.latch.armed = false;
    decision.latch.armed_at_ms = 0;
    decision.latch.firing = false;
    return decision;
  };

  if (!config.on) {
    return stop(TriggerReason::disabled);
  }

  // The engagement: the mode and the trigger key. The key's state is the
  // operating system's own (never a state the trigger writes), so the toggle's
  // edge is the player's press and not the trigger's own output.
  const bool key_held = view.key_held;
  decision.latch.key_down = key_held;
  bool engaged = false;
  switch (config.mode) {
    case TriggerMode::always:
      engaged = true;
      break;
    case TriggerMode::toggle:
      if (key_held && !latch.key_down) {
        decision.latch.toggled = !latch.toggled;
      }
      engaged = decision.latch.toggled;
      break;
    default:  // hold (key 0 = always, for back-compat)
      engaged = config.key <= 0 || key_held;
      break;
  }
  if (!engaged) {
    return stop(TriggerReason::not_engaged);
  }

  if (!solve.valid) {
    return stop(TriggerReason::no_solution);
  }

  // The crosshair must be on the solved aim point. The yaw and the pitch have
  // their own tolerance: a player is wider than they are tall.
  const float yaw_error = std::fabs(normalize_angle(view.yaw - solve.yaw));
  if (yaw_error > config.yaw_tolerance) {
    return stop(TriggerReason::yaw_error);
  }
  const float pitch_error = std::fabs(view.pitch - solve.pitch);
  if (pitch_error > config.pitch_tolerance) {
    return stop(TriggerReason::pitch_error);
  }

  // The hit chance. A head hitbox uses the lower threshold — the classic "head
  // = trigger" behaviour — and the crosshair being on the head is what the
  // angle gate above already established.
  const float threshold = solve.head ? config.head_hitchance : config.hitchance;
  if (solve.hitchance < threshold) {
    return stop(TriggerReason::hitchance);
  }

  // The conditions hold: the reaction delay starts now (a timestamp on the game
  // thread, never a sleep).
  if (!latch.armed) {
    decision.latch.armed = true;
    decision.latch.armed_at_ms = now_ms;
  }
  const std::uint64_t elapsed = now_ms - decision.latch.armed_at_ms;
  if (elapsed < config.delay_ms) {
    decision.reason = TriggerReason::delay;
    decision.delay_remaining_ms = static_cast<std::uint32_t>(config.delay_ms - elapsed);
    decision.latch.firing = false;
    return decision;
  }

  // Fire. `fire` is the edge (the shot starts), `hold` the level (the shot
  // stays down) — so one continuous aim is one press and one release.
  decision.hold = true;
  decision.fire = !latch.firing;
  decision.reason = latch.firing ? TriggerReason::already_firing : TriggerReason::on_target;
  decision.latch.firing = true;
  return decision;
}

// --- The runtime ---------------------------------------------------------------

// The trigger's runtime: the game-thread evaluation, the cost gate and the
// press/release.
//
// The trigger is the **conservative** consumer of the ragebot's mathematics: it
// reuses `aim::solve` and adds only the decision. It never writes a view angle.
class Trigger {
 public:
  Trigger() = default;
  Trigger(const Trigger&) = delete;
  Trigger& operator=(const Trigger&) = delete;

  // The configuration (the `trigger` control command / the environment). The
  // control thread writes it, the game thread reads it.
  void configure(const TriggerConfig& config) noexcept;
  [[nodiscard]] auto config() const noexcept -> TriggerConfig;
  // Is the feature enabled? (The cheap check the game thread does before it
  // copies the snapshot.)
  [[nodiscard]] auto enabled() const noexcept -> bool {
    return on_.load(std::memory_order_relaxed);
  }

  // One game-thread evaluation: the cheap pre-check, the (rate-limited) solve
  // and the decision. `input` is the command backend the trigger pulls the
  // trigger through.
  void tick(const game::Snapshot& snapshot, Input& input, std::uint64_t now_ms) noexcept;

  // Release the shot and forget the delay's arming: the game state changed
  // (a new round, a dead local player, the target is gone). The toggle's state
  // survives — it is the operator's, not the game's.
  void release(Input& input) noexcept;

  // The full reset: the feature is off, or the control channel disconnected.
  // Releases the shot and forgets the solve's cache.
  void reset(Input& input) noexcept;

  // The last decision (the operator's window). The control thread reads it, so
  // it is published through atomics.
  [[nodiscard]] auto last_decision() const noexcept -> TriggerDecision;
  [[nodiscard]] auto last_reason() const noexcept -> TriggerReason;

  // The instrumentation (`docs/ragebot.md` §"The trigger"): how often the
  // trigger was evaluated, how often the cheap pre-check skipped the solve, how
  // many solves ran and what the last one cost.
  [[nodiscard]] auto evaluations() const noexcept -> std::uint64_t;
  [[nodiscard]] auto gate_skips() const noexcept -> std::uint64_t;
  [[nodiscard]] auto solves() const noexcept -> std::uint64_t;
  [[nodiscard]] auto shots() const noexcept -> std::uint64_t;
  [[nodiscard]] auto last_solve_us() const noexcept -> std::uint64_t;
  // The entity index the last solve was for (-1 = none).
  [[nodiscard]] auto target_index() const noexcept -> int;

 private:
  // The configuration, one atomic per field: the control thread writes it, the
  // game thread reads it (the module owns no lock on the game thread's path).
  std::atomic<bool> on_{false};
  std::atomic<float> hitchance_{0.75F};
  std::atomic<float> head_hitchance_{0.50F};
  std::atomic<float> yaw_tolerance_{2.5F};
  std::atomic<float> pitch_tolerance_{2.5F};
  std::atomic<std::uint32_t> delay_ms_{0};
  std::atomic<int> key_{0};
  std::atomic<bool> fire_{true};
  std::atomic<int> mode_{0};
  std::atomic<std::uint32_t> solve_interval_ms_{16};
  std::atomic<bool> force_{false};

  // The game thread's own state (no atomics: only the game thread writes it).
  TriggerLatch latch_{};
  TriggerSolve solve_{};
  std::uint64_t last_solve_ms_ = 0;
  std::uintptr_t local_pawn_ = 0;
  int solved_target_ = -1;  // the entity index the cached solve belongs to
  bool pressed_ = false;    // the trigger currently holds the attack button

  // The counters and the last decision (the control thread reads them).
  std::atomic<std::uint64_t> evaluations_{0};
  std::atomic<std::uint64_t> gate_skips_{0};
  std::atomic<std::uint64_t> solves_{0};
  std::atomic<std::uint64_t> shots_{0};
  std::atomic<std::uint64_t> last_solve_us_{0};
  std::atomic<int> target_{-1};
  std::atomic<bool> last_hold_{false};
  std::atomic<std::uint32_t> last_reason_{0};
  std::atomic<std::uint32_t> last_delay_ms_{0};

  // Publish the decision for the control thread.
  void publish(const TriggerDecision& decision) noexcept;
};

}  // namespace control

}  // namespace cs2
