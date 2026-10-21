// Unit tests for the **local animfix**'s decision logic
// (`cs2/control/animfix.hpp`).
//
// The animfix makes the **server's** animation win for the local player: it
// captures the server's replicated AG2 graph inputs at the netvar codec and
// hands them to the graph instead of the client's own prediction
// (`docs/re/animfix.md` §9).
//
// The failure this test exists for is the one that would make the animfix
// **worse** than no animfix at all: a **stale** capture — one that is older
// than the client's fresh prediction — written over that prediction. The
// validity rule is what prevents it, so the rule is pinned here, in both of
// its clauses:
//
//   * a capture is **consumed once per slot** ("captured *this* frame"): the
//     animfix corrects a slot at most once per capture, so a value that has
//     already been written over a fresh prediction is never written again;
//   * a capture that has **aged out** is refused outright.
//
// The engine's layout is pinned too: the slot offsets, the stride and the
// four slots the server replicates as floats. Those are the numbers the
// engine access uses to read the server's values and to recognise the
// prediction's write, so a wrong offset here would silently read the wrong
// field.

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "cs2/control/animfix.hpp"

namespace {

namespace animfix = cs2::control::animfix;

constexpr float k_epsilon = 0.001F;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

// The server's values for the four tracked slots.
auto server_values(float aim_yaw) -> std::array<float, animfix::k_tracked_slot_count> {
  return {12.5F, 0.25F, -3.0F, aim_yaw};
}

// --- the engine's layout ------------------------------------------------------

// The slot offsets are the engine's own (`docs/re/animfix.md` §2, the 28
// graph-input slots of `CCS2PawnGraphController`): the base, the stride and
// the four float slots the codec decodes.
void test_the_slot_layout_is_the_engines() {
  assert(animfix::k_slots_base == 0x2d8);
  assert(animfix::k_slot_stride == 0x18);
  assert(animfix::k_slot_count == 28);
  assert(animfix::slot_offset(animfix::Slot::air_height) == 0x470);
  assert(animfix::slot_offset(animfix::Slot::flashed) == 0x4b8);
  assert(animfix::slot_offset(animfix::Slot::aim_pitch) == 0x4d0);
  assert(animfix::slot_offset(animfix::Slot::aim_yaw) == 0x4e8);
  // The node pointer and the parameter index live inside the slot.
  assert(animfix::slot_node_offset(animfix::Slot::aim_yaw) == 0x08);
  assert(animfix::slot_index_offset(animfix::Slot::aim_yaw) == 0x10);
  // The four tracked slots are the four the codec decodes as floats.
  assert(animfix::k_tracked_slot_count == 4);
}

// --- the validity rule --------------------------------------------------------

// Nothing is re-applied before the first capture: the animfix has nothing of
// the server's to offer yet.
void test_nothing_is_reapplied_before_the_first_capture() {
  animfix::Capture capture{};
  assert(!animfix::may_reapply(capture, animfix::Slot::aim_yaw, 1000));
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
}

// A fresh capture is re-applied — once.
void test_a_fresh_capture_is_reapplied() {
  animfix::Capture capture{};
  animfix::store(capture, server_values(42.0F), 1000);
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1000);
  assert(value.has_value());
  assert(nearly(*value, 42.0F));
}

// **The rule that matters**: a capture is consumed once per slot. The second
// re-apply of the same capture is refused, so a value that has already been
// written over the client's fresh prediction is never written again — the
// animfix cannot make the pose worse than the prediction by repeating
// itself.
void test_a_capture_is_consumed_once_per_slot() {
  animfix::Capture capture{};
  animfix::store(capture, server_values(42.0F), 1000);
  assert(animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  // The same capture, again: refused.
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  // The consumption is **per slot**: the other slots of the same capture are
  // still pending, because the prediction writes each of them once.
  assert(animfix::take(capture, animfix::Slot::aim_pitch, 1000).has_value());
  assert(animfix::take(capture, animfix::Slot::flashed, 1000).has_value());
  assert(animfix::take(capture, animfix::Slot::air_height, 1000).has_value());
  // And now every slot of that capture is consumed.
  assert(!animfix::take(capture, animfix::Slot::aim_pitch, 1000).has_value());
}

// A **new** capture re-opens the slots: the server sent a new value, so the
// animfix may correct the slot again.
void test_a_new_capture_reopens_the_slots() {
  animfix::Capture capture{};
  animfix::store(capture, server_values(42.0F), 1000);
  assert(animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  animfix::store(capture, server_values(43.0F), 1016);
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1016);
  assert(value.has_value());
  assert(nearly(*value, 43.0F));
}

// A capture that has **aged out** is refused, even though it was never
// re-applied. This is the stalled-connection case: the server's value is
// seconds old and the client's own prediction is newer, so writing it would
// make the pose worse, not better.
void test_an_aged_out_capture_is_refused() {
  animfix::Capture capture{};
  animfix::store(capture, server_values(42.0F), 1000);
  const std::uint64_t too_late = 1000 + animfix::k_max_capture_age_ms + 1;
  assert(!animfix::may_reapply(capture, animfix::Slot::aim_yaw, too_late));
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, too_late).has_value());
  // The boundary itself is still allowed (the age is inclusive).
  assert(animfix::may_reapply(capture, animfix::Slot::aim_yaw,
                              1000 + animfix::k_max_capture_age_ms));
}

// A capture that aged out and is then **refreshed** by a new one works
// again: the animfix recovers as soon as the connection does.
void test_a_refreshed_capture_works_again() {
  animfix::Capture capture{};
  animfix::store(capture, server_values(42.0F), 1000);
  const std::uint64_t too_late = 1000 + animfix::k_max_capture_age_ms + 1;
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, too_late).has_value());
  animfix::store(capture, server_values(44.0F), too_late);
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, too_late);
  assert(value.has_value());
  assert(nearly(*value, 44.0F));
}

// The capture is a **copy**: the values the animfix hands back are the ones
// the server sent, not the ones the slots hold afterwards. (The engine side
// copies them out of the controller; here the point is that the capture
// keeps its own copy.)
void test_the_capture_keeps_its_own_copy() {
  animfix::Capture capture{};
  std::array<float, animfix::k_tracked_slot_count> values = server_values(42.0F);
  animfix::store(capture, values, 1000);
  values[animfix::slot_position(animfix::Slot::aim_yaw)] = 999.0F;
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1000);
  assert(value.has_value());
  assert(nearly(*value, 42.0F));
}

// --- the evidence -------------------------------------------------------------

// The operator's evidence: the signed difference between the server's aim
// yaw and the client's predicted one, in degrees.
void test_the_aim_yaw_difference_is_signed_and_wrapped() {
  assert(nearly(animfix::aim_yaw_difference(10.0F, 4.0F), 6.0F));
  assert(nearly(animfix::aim_yaw_difference(4.0F, 10.0F), -6.0F));
  // Across the wrap: 179 vs -179 is 358 degrees apart, not 358.
  assert(nearly(animfix::aim_yaw_difference(179.0F, -179.0F), -2.0F));
  assert(nearly(animfix::aim_yaw_difference(-179.0F, 179.0F), 2.0F));
}

}  // namespace

auto main() -> int {
  test_the_slot_layout_is_the_engines();
  test_nothing_is_reapplied_before_the_first_capture();
  test_a_fresh_capture_is_reapplied();
  test_a_capture_is_consumed_once_per_slot();
  test_a_new_capture_reopens_the_slots();
  test_an_aged_out_capture_is_refused();
  test_a_refreshed_capture_works_again();
  test_the_capture_keeps_its_own_copy();
  test_the_aim_yaw_difference_is_signed_and_wrapped();
  std::puts("control-animfix: ok");
  return 0;
}
