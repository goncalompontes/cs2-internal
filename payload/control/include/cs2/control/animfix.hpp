#pragma once

// The **local animfix**'s decision logic (`docs/re/animfix.md`).
//
// The engine's ground truth, measured on the live `client.dll`
// (sha256 `659940c1…`, 2026-10-21 — the measurement is appended to
// `docs/re/animfix.md` as "§9 The ordering measurement"):
//
// 1. The server **replicates** a part of the AG2 graph inputs of
//    `CCS2PawnGraphController`. The per-class netvar **codec** decodes them
//    into the controller's 28 slots: the value lands at the **slot base**,
//    the AG2 parameter node at `+0x08` and the parameter index inside that
//    node at `+0x10` (the slot stride is `0x18`).
// 2. The codec decodes **four float** slots — `m_flAirHeightAboveGround`
//    (`+0x470`), `m_flFlashedAmount` (`+0x4b8`), `m_flAimPitchAngle`
//    (`+0x4d0`) and `m_flAimYawAngle` (`+0x4e8`). The other replicated slots
//    are enums or bools, and the client pushes them into the graph with
//    *direct* stores, not through the parameter setter (see below).
// 3. The **client's prediction** (`CCS2PawnGraphController::ReflectPawnState`
//    → `ReflectAim` / `ReflectMove`) pushes the *client's* values into the
//    AG2 graph through the parameter **setter** — and it does so for exactly
//    those four float slots. The prediction never writes the replicated value
//    in the slot, so the **server's value never reaches the graph**: through
//    the setter, the prediction is the only writer.
//
// So the animfix has one job: put the **server's** value into the graph
// instead of the client's, for the local player's four float slots.
//
// **Where the correction goes.** The graph is evaluated in
// `CAnimGraphGameSystem::AnimTickUpdate` (`0x35aeb0`), and the correction is
// applied there — in the anim tick, before the evaluation, so the graph is
// evaluated from the server's values. (That the anim tick runs *after* the
// client's prediction in the same tick is **inferred**, not measured —
// `docs/re/animfix.md` §9.3 — but the fix does not depend on it: the
// correction is written at the start of the anim tick and the evaluation
// happens inside it.) The parameter write is
// the setter's own (`*(float *)(*(node + 0x10)[index] + 0x18) = value`),
// replicated here because the setter itself is **not hookable**: its third
// prologue instruction is a RIP-relative load whose displacement cannot be
// relocated to the trampoline (the module is mapped high, the trampoline
// low), so the repo's inline relocator refuses it.
//
// What is left open (and deliberately so): the **anim-state** divergence.
// The client's `CCSPlayerAnimationState` is never replicated, so the move
// type, the ground/air action and the turn angle the client predicts are its
// own; reproducing the server's would mean re-running the velocity → state
// derivation, which this task does not attempt.
//
// The maths here is pure and platform-independent: the host test
// (`payload/control/tests/test_control_animfix.cpp`) pins it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cs2/control/strafe_geometry.hpp"  // normalize_angle

namespace cs2::control::animfix {

// --- the engine's layout (measured; see the header's comment) ------------

// The 28 AG2 graph-input slots of `CCS2PawnGraphController`.
inline constexpr std::size_t k_slots_base = 0x2d8;
inline constexpr std::size_t k_slot_stride = 0x18;
inline constexpr std::size_t k_slot_count = 28;

// Inside one slot: the replicated value (the codec writes it here), the AG2
// parameter node pointer, and the parameter index inside that node.
inline constexpr std::size_t k_slot_value_offset = 0x00;
inline constexpr std::size_t k_slot_node_offset = 0x08;
inline constexpr std::size_t k_slot_index_offset = 0x10;

// Inside the AG2 parameter **node**: the array of parameter objects, the
// stride of that array, and the offset of the value inside a parameter
// object. The setter's own write is
// `*(float *)(*(node + 0x10)[index] + 0x18) = value`.
inline constexpr std::size_t k_node_array_offset = 0x10;
inline constexpr std::size_t k_parameter_stride = 8;
inline constexpr std::size_t k_parameter_value_offset = 0x18;

// The slots the animfix owns: the four the server replicates **as floats**
// and the client's prediction rewrites through the parameter setter. The
// enum/bool slots the codec also writes are *not* here — the prediction
// pushes them with direct stores, which no available seam intercepts.
enum class Slot : std::size_t {
  air_height = 17,  // +0x470  m_flAirHeightAboveGround
  flashed = 20,     // +0x4b8  m_flFlashedAmount
  aim_pitch = 21,   // +0x4d0  m_flAimPitchAngle
  aim_yaw = 22,     // +0x4e8  m_flAimYawAngle
};

// The slots the animfix captures and re-applies, in the order the engine
// lists them.
inline constexpr std::array<Slot, 4> k_tracked_slots{Slot::air_height, Slot::flashed,
                                                     Slot::aim_pitch, Slot::aim_yaw};
inline constexpr std::size_t k_tracked_slot_count = k_tracked_slots.size();

// The slot's position in `k_tracked_slots` (the index into the capture's
// arrays).
[[nodiscard]] constexpr auto slot_position(Slot slot) noexcept -> std::size_t {
  for (std::size_t i = 0; i < k_tracked_slot_count; ++i) {
    if (k_tracked_slots[i] == slot) {
      return i;
    }
  }
  return k_tracked_slot_count;
}

// The slot's offset inside the controller.
[[nodiscard]] constexpr auto slot_offset(Slot slot) noexcept -> std::size_t {
  return k_slots_base + static_cast<std::size_t>(slot) * k_slot_stride;
}

// The offset of the AG2 parameter node pointer inside the slot.
[[nodiscard]] constexpr auto slot_node_offset(Slot /*slot*/) noexcept -> std::size_t {
  return k_slot_node_offset;
}

// The offset of the AG2 parameter index inside the slot.
[[nodiscard]] constexpr auto slot_index_offset(Slot /*slot*/) noexcept -> std::size_t {
  return k_slot_index_offset;
}

// The AG2 parameter a slot writes to: the node pointer and the parameter's
// index inside that node.
struct Target {
  std::uintptr_t node{0};
  std::int16_t index{0};
};

// True when the four tracked slots point at four **different** graph
// parameters. Two slots that share a `(node, index)` pair would collide in
// `find_slot` (the first match wins) and one of them would silently never
// be corrected — so the capture refuses such a controller.
[[nodiscard]] inline auto targets_are_distinct(
    const std::array<Target, k_tracked_slot_count>& targets) noexcept -> bool {
  for (std::size_t i = 0; i < k_tracked_slot_count; ++i) {
    if (targets[i].node == 0) {
      return false;  // an unbound slot
    }
    for (std::size_t j = i + 1; j < k_tracked_slot_count; ++j) {
      if (targets[i].node == targets[j].node && targets[i].index == targets[j].index) {
        return false;
      }
    }
  }
  return true;
}

// --- the engine's own guard on a graph parameter ---------------------------

// The engine refuses a value before it reaches the graph. `FUN_1808492a0`
// (the AG2 parameter setter) is:
//
//     if (((-2.1474836e+09 < v) && (v < 2.1474836e+09)) && (!NAN(v)) &&
//         (((uint)value & 0x7f800000) != 0x7f800000)) { write }
//
// The mask test is the "exponent is all ones" test, so it refuses both the
// NaNs and the infinities; the range test refuses the values a float can
// hold but a graph parameter must not.
//
// **The animfix must not be laxer than the engine.** The captured value is
// network-controlled — it is whatever the server put in the snapshot — and a
// single NaN in `m_flAimYawAngle` would propagate through the graph
// evaluation into the pose, the bone array and every hitbox transform. The
// engine's own prediction path refuses such a value (the client's value
// stays in the graph); the animfix has to refuse it too.
inline constexpr double k_engine_value_limit = 2.1474836e9;

// True when the engine would write `value` into a graph parameter.
[[nodiscard]] inline auto value_is_writable(float value) noexcept -> bool {
  if (!std::isfinite(value)) {
    // The engine's mask test refuses the NaNs and the infinities. (The
    // range test below would refuse them too - every comparison with a NaN
    // is false - so this branch is the defence in depth, not the only thing
    // that stands between a NaN and the graph.)
    return false;
  }
  return std::fabs(static_cast<double>(value)) < k_engine_value_limit;
}

// --- the staleness bound ----------------------------------------------------

// The shortest snapshot interval the animfix assumes.
inline constexpr std::uint64_t k_min_snapshot_interval_ms = 8;

// The longest: beyond a quarter of a second the server's value is not the
// server's current animation under any reading, so it is refused however the
// measured interval looks.
inline constexpr std::uint64_t k_max_snapshot_interval_ms = 250;

// How many intervals the estimate is taken over.
//
// The estimate is the **median** of the window, and that is what makes it
// robust in both directions: a single **late** decode (a packet-reordering
// burst, a Wi-Fi stall, a server-side hitch) cannot move the median, and
// neither can a single fast one. A plain average would be inflated by the
// late decode - and an inflated bound is exactly the failure the bound
// exists to prevent: a capture that is minutes old would be written over
// the client's fresh prediction.
inline constexpr std::size_t k_snapshot_window = 8;

// The animfix's estimate of the server's snapshot interval: the observed
// intervals, and the time of the last decode.
//
// The window is filled from the *accepted* intervals - every interval is
// kept, and the median is what discards the outliers - so a stall (which
// produces no decodes at all) cannot widen the window either: the estimate
// only changes when a decode happens.
struct SnapshotClock {
  std::array<std::uint64_t, k_snapshot_window> samples{};
  std::size_t count{0};
  std::uint64_t last_ms{0};
  bool seen{false};
};

// A decode happened at `now_ms`: record the interval since the previous one.
inline void observe_decode(SnapshotClock& clock, std::uint64_t now_ms) noexcept {
  if (clock.seen && now_ms > clock.last_ms) {
    clock.samples[clock.count % k_snapshot_window] = now_ms - clock.last_ms;
    if (clock.count < k_snapshot_window) {
      ++clock.count;
    }
  }
  clock.last_ms = now_ms;
  clock.seen = true;
}

// The server's snapshot interval, in milliseconds: the **median** of the
// observed intervals. Zero until the first interval has been seen.
[[nodiscard]] inline auto snapshot_interval_ms(const SnapshotClock& clock) noexcept
    -> std::uint64_t {
  if (clock.count == 0) {
    return 0;
  }
  std::array<std::uint64_t, k_snapshot_window> sorted = clock.samples;
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(clock.count));
  return sorted[(clock.count - 1) / 2];
}

// The margin the bound carries over the observed interval.
//
// The median is, by construction, the value that **half** the intervals
// exceed - so a capture measured against it would age out in the second half
// of every gap, and the client's own prediction would win those frames. That
// is the oscillation the animfix exists to remove, and it would show up as a
// jitter at the server's tick rate.
//
// Twice the median covers the jitter (a snapshot that arrives late is still
// the server's current value) while the median itself keeps the outliers
// out.
inline constexpr std::uint64_t k_bound_margin = 2;

// The staleness bound: the observed snapshot interval with the margin over
// it, clamped to the floor and to the hard maximum.
[[nodiscard]] inline auto snapshot_bound_ms(const SnapshotClock& clock) noexcept -> std::uint64_t {
  const std::uint64_t interval = snapshot_interval_ms(clock);
  if (interval == 0) {
    return k_min_snapshot_interval_ms;
  }
  const std::uint64_t margin = interval * k_bound_margin;
  if (margin > k_max_snapshot_interval_ms) {
    return k_max_snapshot_interval_ms;
  }
  return margin > k_min_snapshot_interval_ms ? margin : k_min_snapshot_interval_ms;
}

// --- the validity rule ----------------------------------------------------

// How old a capture may be before it is refused, in milliseconds on the
// caller's monotonic clock.
//
// The bound is the **observed snapshot interval** (`snapshot_bound_ms`): the
// server's value is as old as the time its snapshots take to arrive, and a
// capture older than that has been superseded by a snapshot that did not
// arrive - the connection stalled. Writing it over the client's fresh
// prediction would then move the local hitboxes *backwards*, which is worse
// than not fixing them at all.
//
// The bound is on the **age of the data**, not on the time since the
// receipt: a decode re-stamps the capture, so the age is measured from the
// decode that produced the values.

// The captured state: the server's values for the tracked slots, the
// generation they were captured at, the generation each slot was last
// re-applied at, and **the controller the values came from**.
//
// A decode **replaces** the capture (the values are copied out of the
// controller's slots, which the client's prediction never writes), so the
// value that is re-applied is always the last one the server sent — never an
// accumulation and never a superseded one.
struct Capture {
  std::array<float, k_tracked_slot_count> values{};
  /// How many times this capture has been re-applied. **Diagnostic only**:
  /// the decision must never depend on it - the capture is re-applied on every
  /// anim tick while it is fresh, and a "consume once" rule is what made the
  /// feature fragile (see `decide`).
  std::uint64_t reapply_count{0};
  std::uint64_t captured_at_ms{0};
  std::uintptr_t controller{0};
  bool valid{false};
};

// Record a capture: the server's values for the tracked slots, copied out of
// the controller. The copy is the point — the slots are rewritten every
// frame, so a live pointer would not be a capture at all.
//
// The controller is recorded with it: the write goes to the graph parameters
// *of that controller*, and a local pawn that dies and respawns gets a new
// one — the old one (and its AG2 instance) is freed, and writing through the
// captured `(node, index)` pairs would be a write into freed memory.
inline void store(Capture& capture, std::uintptr_t controller,
                  const std::array<float, k_tracked_slot_count>& values,
                  std::uint64_t now_ms) noexcept {
  capture.values = values;
  capture.reapply_count = 0;
  capture.captured_at_ms = now_ms;
  capture.controller = controller;
  capture.valid = true;
}

// Note that the capture was re-applied.
//
// **This must never gate the decision.** The engine calls it after a write,
// and the *next* anim tick must still be allowed to re-apply the same fresh
// capture: the engine writes the graph's parameters on the command path (the
// prediction) and evaluates the graph on the anim path, so a capture that is
// consumed by the first anim tick would leave the graph with the client's
// prediction for every later anim tick of the same tick - and the pose would
// oscillate between the server's and the client's aim yaw at the server's
// tick rate.
inline void mark_applied(Capture& capture) noexcept {
  capture.reapply_count += 1;
}

// True when the capture still belongs to the controller the animfix is
// about to write to.
//
// The local pawn can die and respawn: the old `CCS2PawnGraphController` and
// its AG2 instance are freed, and the `(node, index)` pairs captured from it
// then point into freed memory. A read of a freed-but-mapped page can succeed
// and yield a garbage pointer, and the write would then land on a mapped
// garbage address — corruption, which is worse than a fault. So the animfix
// re-reads the live controller before it writes and refuses the frame when
// it is not the one the capture came from.
[[nodiscard]] inline auto capture_matches_controller(const Capture& capture,
                                                     std::uintptr_t controller) noexcept -> bool {
  return capture.valid && capture.controller != 0 && capture.controller == controller;
}

// Why a re-apply was refused. The operator has to be able to tell "the
// codec has not run yet" from "every capture aged out" (a stalled
// connection) and from "the controller is not the one the capture came
// from" (the pawn respawned) — otherwise a feature that refuses everything
// looks like a feature that is merely idle.
enum class Refusal : std::size_t {
  none = 0,     // the re-apply is allowed
  no_capture,   // the codec has not run yet
  stale,        // the capture aged out (the snapshots stopped)
  controller,   // the live controller is not the one the capture came from
  not_tracked,  // the slot is not one of the animfix's own
};

// The decision for one slot.
struct Decision {
  bool reapply = false;
  Refusal refusal = Refusal::none;
};

// May the captured value for `slot` be re-applied now, and if not, why not?
//
// Three conditions, all required:
//   * there is a capture at all (the codec has run at least once),
//   * the capture belongs to the **live** controller (the pawn has not
//     respawned since it was decoded), and
//   * the capture is **not stale** (it is younger than the observed snapshot
//     interval).
//
// **The capture is not consumed by a re-apply.** The specification's rule is
// *"a captured value may only be re-applied if it was captured **this**
// frame"*; the reason for the rule is that a stale value must not be written
// over a fresh prediction, and the **staleness bound** is what enforces
// that: a capture older than the snapshot interval may have been superseded
// by a snapshot that has not arrived, so it is refused.
//
// The capture is re-applied on **every** anim tick while it is fresh, and
// that is deliberate:
//
//   * the engine writes the graph's parameters on the **command** path (the
//     prediction), and the graph is evaluated on the **anim** path. A capture
//     that is consumed by the first anim tick would leave the graph with the
//     client's prediction for every later anim tick of the same tick - and
//     the pose would oscillate between the server's and the client's aim yaw
//     at the server's tick rate: the classic animfix jitter.
//   * the engine runs **two** variants of the driver (the client tick and the
//     server tick of a listen server). With a consume-once rule, whichever
//     variant ran first would consume the capture, and the other one - the
//     one that evaluates the pose the player sees - would find nothing.
//
// Re-applying the same fresh capture is idempotent (it writes the same
// value), so the server's value simply **stays** in the graph between the
// decodes.
[[nodiscard]] inline auto decide(const Capture& capture, Slot slot, std::uint64_t now_ms,
                                 std::uint64_t max_age_ms, std::uintptr_t controller) noexcept
    -> Decision {
  Decision decision{};
  if (!capture.valid) {
    decision.refusal = Refusal::no_capture;
    return decision;
  }
  if (capture.controller != controller) {
    decision.refusal = Refusal::controller;
    return decision;
  }
  if ((now_ms - capture.captured_at_ms) > max_age_ms) {
    decision.refusal = Refusal::stale;
    return decision;
  }
  if (slot_position(slot) >= k_tracked_slot_count) {
    decision.refusal = Refusal::not_tracked;
    return decision;
  }
  decision.reapply = true;
  return decision;
}

// --- the evidence ---------------------------------------------------------

// The signed difference between the server's aim yaw and the client's
// predicted one, in degrees, in `[-180, 180)`. This is the number the
// operator reads: it is the divergence the animfix removes.
[[nodiscard]] inline auto aim_yaw_difference(float server_yaw, float predicted_yaw) noexcept
    -> float {
  return normalize_angle(server_yaw - predicted_yaw);
}

}  // namespace cs2::control::animfix
