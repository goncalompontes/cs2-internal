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
//    in the slot, so the **server's value never reaches the graph**: the
//    prediction is the only writer, and the graph is evaluated from it.
//
// The animfix therefore has one job: hand the setter the **server's** value
// instead of the client's, for the local player's four float slots. That is
// the seam that needs **no ordering assumption** — the correction lands
// exactly where the prediction writes, so it does not matter whether the
// snapshot decode runs before or after the prediction inside one frame.
//
// What is left open (and deliberately so): the **anim-state** divergence.
// The client's `CCSPlayerAnimationState` is never replicated, so the move
// type, the ground/air action and the turn angle the client predicts are its
// own; reproducing the server's would mean re-running the velocity → state
// derivation, which this task does not attempt.
//
// The maths here is pure and platform-independent: the host test
// (`payload/control/tests/test_control_animfix.cpp`) pins it.

#include <array>
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
// index inside that node. The prediction's write is recognised by this pair.
struct Target {
  std::uintptr_t node{0};
  std::int16_t index{0};
};

// The tracked slot a write to `(node, index)` belongs to, if any. The
// prediction writes the graph's parameters through the setter, and the
// animfix recognises **its own** four slots by this pair — a write to any
// other parameter (another entity's, another class's) is left alone.
//
// A slot whose node is null is not bound to a graph parameter yet, so it
// cannot be recognised (and a null node must not match an unbound slot).
[[nodiscard]] inline auto find_slot(const std::array<Target, k_tracked_slot_count>& targets,
                                    std::uintptr_t node, std::int16_t index) noexcept
    -> std::optional<Slot> {
  if (node == 0) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < k_tracked_slot_count; ++i) {
    if (targets[i].node == node && targets[i].index == index) {
      return k_tracked_slots[i];
    }
  }
  return std::nullopt;
}

// --- the validity rule ----------------------------------------------------

// How old a capture may be before it is refused, in milliseconds on the
// caller's monotonic clock. The server ticks at 64 Hz, so 32 ms is about
// **two of its ticks**: the capture still describes the server's current
// animation. The bound only bites when the connection stalls (or the player
// dies, or the game is paused) — then the server's value describes a pose it
// left behind, and writing it over the client's fresh prediction would move
// the local hitboxes *backwards*, which is worse than not fixing them at
// all.
inline constexpr std::uint64_t k_max_capture_age_ms = 32;

// The captured state: the server's values for the tracked slots and the time
// they arrived.
//
// A decode **replaces** the capture (the values are copied out of the
// controller's slots, which the client's prediction never writes), so the
// value that is re-applied is always the last one the server sent — never an
// accumulation and never a superseded one.
struct Capture {
  std::array<float, k_tracked_slot_count> values{};
  std::uint64_t generation{0};
  std::uint64_t captured_at_ms{0};
  bool valid{false};
};

// Record a capture: the server's values for the tracked slots, copied out of
// the controller. The copy is the point — the slots are rewritten every
// frame, so a live pointer would not be a capture at all.
inline void store(Capture& capture, const std::array<float, k_tracked_slot_count>& values,
                  std::uint64_t now_ms) noexcept {
  capture.values = values;
  capture.generation += 1;
  capture.captured_at_ms = now_ms;
  capture.valid = true;
}

// May the captured value for `slot` be re-applied now?
//
// Two conditions, both required:
//   * there is a capture at all (the codec has run at least once), and
//   * the capture is **fresh**: it is not older than
//     `k_max_capture_age_ms`.
//
// The second condition is the "captured *this* frame" rule: the server's
// value describes the server's animation of at most the last two ticks, and
// a capture that has aged out describes a pose the server has long left
// behind.
//
// The capture is deliberately **not** consumed by a re-apply. The engine
// writes a graph parameter more than once per decode (the prediction runs
// from the command path *and* from the frame path), and only the **last**
// write before the graph is evaluated matters: a rule that allowed one
// re-apply per capture would leave the client's own value in the graph, and
// the animfix would do nothing at all.
[[nodiscard]] inline auto may_reapply(const Capture& capture, Slot slot,
                                      std::uint64_t now_ms) noexcept -> bool {
  if (!capture.valid) {
    return false;
  }
  if (slot_position(slot) >= k_tracked_slot_count) {
    return false;
  }
  return (now_ms - capture.captured_at_ms) <= k_max_capture_age_ms;
}

// The captured value for `slot`, if the validity rule allows it.
[[nodiscard]] inline auto take(const Capture& capture, Slot slot, std::uint64_t now_ms) noexcept
    -> std::optional<float> {
  if (!may_reapply(capture, slot, now_ms)) {
    return std::nullopt;
  }
  return capture.values[slot_position(slot)];
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
