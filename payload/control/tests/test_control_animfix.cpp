// Unit tests for the **local animfix**'s decision logic
// (`cs2/control/animfix.hpp`).
//
// The animfix makes the **server's** animation win for the local player: it
// captures the server's replicated AG2 graph inputs at the netvar codec and
// puts them into the graph's parameters where the graph is evaluated. The
// engine facts it rests on are in `docs/re/animfix.md` §9 — this file only
// pins the *decision* logic.
//
// The failure this test exists for is the one that would make the animfix
// **worse** than no animfix at all: a **stale** capture — one that is older
// than the client's fresh prediction — written over that prediction. The
// validity rule is what prevents it, so the rule is pinned here, in all of
// its clauses:
//
//   * a fresh capture is **not consumed** by a re-apply: it is re-applied on
//     every client anim tick while it is fresh, so the server's value stays
//     in the graph between the decodes (the engine writes the parameters on
//     the command path and evaluates the graph on the anim path);
//   * a capture that has **aged out** is refused outright — and the bound is
//     the **observed snapshot interval** (the server's rate), pinned against
//     literals, so it cannot be loosened without the test noticing.
//
// The refusal **reasons** are pinned too: the operator has to be able to
// tell "the codec has not run yet" from "every capture aged out" and from
// "the controller is not the one the capture came from".
//
// The **bound's estimator** is pinned as well: the estimate must survive a
// **burst** of fast decodes (a catch-up after a hitch), or the bound
// collapses to the floor and the correction stops for the length of the
// window — the oscillation at the server's tick rate that the animfix exists
// to remove.
//
// The **log** is pinned too: the hooks are installed from a per-tick detour,
// so the status must be latched and the animfix's log must have exactly one
// call site — a line per tick would be ~64 file round trips a second on the
// game's main thread.
//
// The engine's layout is pinned as well: the slot offsets, the stride, the
// four slots the server replicates as floats, and the shape of the graph
// parameter the correction writes. Those are the numbers the engine access
// uses to read the server's values and to find the parameter, so a wrong
// offset here would silently read the wrong field.

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "cs2/control/animfix.hpp"
#include "cs2/hooks/inline.hpp"
#include "cs2/hooks/memory.hpp"

namespace {

namespace animfix = cs2::control::animfix;

constexpr float k_epsilon = 0.001F;

// The staleness bound the tests use: a 64 Hz server's snapshot interval.
constexpr std::uint64_t k_interval_ms = 16;
// An age that is plainly past the interval: the capture must be refused.
constexpr std::uint64_t k_two_intervals_ms = 2 * k_interval_ms;

// The controller the captures below belong to.
constexpr std::uintptr_t k_controller = 0x1000;

auto nearly(float value, float expected) -> bool {
  return std::fabs(value - expected) <= k_epsilon;
}

// The server's values for the four tracked slots.
auto server_values(float aim_yaw) -> std::array<float, animfix::k_tracked_slot_count> {
  return {12.5F, 0.25F, -3.0F, aim_yaw};
}

// The **duty cycle**: the fraction of the anim ticks on which the correction
// wins.
//
// The server's snapshots arrive at the given intervals (cycling); the client
// animates every `anim_period_ms`. At each anim tick the newest capture is
// applied when it is younger than the bound. A duty cycle below 100 % means
// the client's own prediction wins some frames - the oscillation at the
// server's tick rate that the animfix exists to remove.
auto duty_cycle(const std::vector<std::uint64_t>& server_intervals, std::uint64_t anim_period_ms)
    -> double {
  animfix::SnapshotClock clock{};
  std::uint64_t now = 1000;
  std::uint64_t last_decode = now;
  std::uint64_t next_decode = now + server_intervals.front();
  std::size_t next_interval = 1;
  std::uint64_t ticks = 0;
  std::uint64_t applied = 0;
  const std::uint64_t horizon = 8000;  // eight seconds of ticks
  while (now < horizon) {
    // The next event: the anim tick, or a snapshot at its own time (the
    // decode runs on the network path, not on the anim tick).
    const std::uint64_t tick_at = now + anim_period_ms;
    if (tick_at < next_decode) {
      now = tick_at;
      ++ticks;
      const std::uint64_t bound = animfix::snapshot_bound_ms(clock);
      if (now - last_decode <= bound) {
        ++applied;
      }
      continue;
    }
    now = next_decode;
    last_decode = now;
    animfix::observe_decode(clock, now);
    next_decode = now + server_intervals[next_interval % server_intervals.size()];
    ++next_interval;
  }
  return static_cast<double>(applied) / static_cast<double>(ticks);
}

// A capture of the server's values at `now_ms`, for the controller above.
auto captured_at(std::uint64_t now_ms, float aim_yaw = 42.0F) -> animfix::Capture {
  animfix::Capture capture{};
  animfix::store(capture, k_controller, server_values(aim_yaw), now_ms);
  return capture;
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

// The graph parameter the correction writes: the setter's own
// `*(float *)(*(node + 0x10)[index] + 0x18) = value`.
void test_the_parameter_layout_is_the_engines() {
  assert(animfix::k_node_array_offset == 0x10);
  assert(animfix::k_parameter_stride == 8);
  assert(animfix::k_parameter_value_offset == 0x18);
}

// --- the engine's own guard --------------------------------------------------

// The animfix writes into the graph where the engine's setter writes, so it
// must refuse **exactly** what the engine refuses: the setter's test is
// `(-2.1474836e+09 < v) && (v < 2.1474836e+09) && !NAN(v) &&
// ((bits & 0x7f800000) != 0x7f800000)` — the mask test refuses the NaNs and
// the infinities, the range test refuses the rest.
//
// The captured values are network-controlled, so a NaN in the server's
// `m_flAimYawAngle` would otherwise propagate into the pose, the bone array
// and every hitbox transform.
void test_the_engines_own_guard_is_reproduced() {
  // A normal value: the engine writes it.
  assert(animfix::value_is_writable(0.0F));
  assert(animfix::value_is_writable(42.0F));
  assert(animfix::value_is_writable(-42.0F));
  assert(animfix::value_is_writable(180.0F));
  // A NaN: refused (the engine's `!NAN(v)` and its mask test).
  const float nan = std::numeric_limits<float>::quiet_NaN();
  assert(!animfix::value_is_writable(nan));
  assert(!animfix::value_is_writable(-nan));
  // The infinities: refused (the engine's mask test).
  assert(!animfix::value_is_writable(std::numeric_limits<float>::infinity()));
  assert(!animfix::value_is_writable(-std::numeric_limits<float>::infinity()));
  // The limit: the engine's range test is strict, so the value at the limit
  // is refused.
  const float limit = static_cast<float>(animfix::k_engine_value_limit);
  assert(!animfix::value_is_writable(limit));
  assert(!animfix::value_is_writable(-limit));
  // The largest float the engine accepts: one float below the limit.
  const float largest = 2147483520.0F;  // 2^31 - 128
  assert(animfix::value_is_writable(largest));
  assert(animfix::value_is_writable(-largest));
}

// --- the staleness bound ----------------------------------------------------

// The bound is the **observed snapshot interval** — the server's rate — not
// the client's frame rate: the server's value is as old as the time its
// snapshots take to arrive. A server that sends at 30 Hz delivers values
// that are up to 33 ms old, and refusing them because the client draws at
// 60 Hz would make the animfix inert everywhere but on the default rate.
void test_the_bound_is_the_observed_snapshot_interval() {
  animfix::SnapshotClock clock{};
  // Before the first decode there is no interval: the floor applies.
  assert(animfix::snapshot_bound_ms(clock) == animfix::k_min_snapshot_interval_ms);
  // A 64 Hz server: the interval is ~16 ms, and the bound is the interval
  // with the margin over it.
  animfix::observe_decode(clock, 1000);
  animfix::observe_decode(clock, 1016);
  assert(animfix::snapshot_bound_ms(clock) == 32);
  // A 30 Hz server: the interval is ~33 ms, and the bound follows it.
  animfix::SnapshotClock slow{};
  animfix::observe_decode(slow, 1000);
  animfix::observe_decode(slow, 1033);
  animfix::observe_decode(slow, 1066);
  assert(animfix::snapshot_bound_ms(slow) == 66);
  // A **stall** must not loosen the bound: a stalled connection produces no
  // new decodes at all, so the estimate stays at the server's normal rate
  // and the capture ages out against it.
  const std::uint64_t bound_before = animfix::snapshot_bound_ms(clock);
  // ... time passes, no decode ...
  assert(animfix::snapshot_bound_ms(clock) == bound_before);
  // And the capture ages out against it.
  const animfix::Capture stalled = captured_at(1000);
  assert(!animfix::decide(stalled, animfix::Slot::aim_yaw, 1000 + bound_before + 1, bound_before,
                          k_controller)
              .reapply);
}

// **A late decode must not widen the bound.** One snapshot that arrives
// 500 ms late (a packet-reordering burst, a Wi-Fi stall, a server-side
// hitch) must not turn the window into a half-second one: with such a
// window a capture that is a hundred milliseconds old would be written over
// the client's fresh prediction, which is the one thing the bound exists to
// prevent.
//
// The estimate is the **median** of the observed intervals, so an outlier
// cannot move it - in either direction.
void test_a_late_decode_does_not_widen_the_bound() {
  animfix::SnapshotClock clock{};
  // A healthy 64 Hz stream.
  for (int i = 0; i < 8; ++i) {
    animfix::observe_decode(clock, 1000 + static_cast<std::uint64_t>(i) * 16);
  }
  const std::uint64_t healthy = animfix::snapshot_bound_ms(clock);
  assert(healthy == 32);
  // One late decode.
  animfix::observe_decode(clock, 1000 + 7 * 16 + 500);
  assert(animfix::snapshot_bound_ms(clock) == healthy);
  // And a capture that is older than the healthy bound is still refused.
  const animfix::Capture capture = captured_at(1000);
  assert(
      !animfix::decide(capture, animfix::Slot::aim_yaw, 1000 + healthy + 1, healthy, k_controller)
           .reapply);
  // A single **fast** decode does not move it either.
  animfix::SnapshotClock fast{};
  for (int i = 0; i < 8; ++i) {
    animfix::observe_decode(fast, 1000 + static_cast<std::uint64_t>(i) * 16);
  }
  animfix::observe_decode(fast, 1000 + 7 * 16 + 2);
  assert(animfix::snapshot_bound_ms(fast) == 32);
}

// The bound has a **hard maximum** as well as the margin: no measurement may
// make the window unbounded. (Removing the clamp would let a server whose
// snapshots arrive every few hundred milliseconds keep a stale value alive
// for the same time.)
void test_the_bound_has_a_hard_maximum() {
  assert(animfix::k_max_snapshot_interval_ms == 250);
  assert(animfix::k_max_snapshot_interval_ms > animfix::k_min_snapshot_interval_ms);
  // A server whose snapshots arrive every 200 ms: the bound is the margin
  // over the interval, clamped to the maximum.
  animfix::SnapshotClock slow{};
  for (int i = 0; i < 8; ++i) {
    animfix::observe_decode(slow, 1000 + static_cast<std::uint64_t>(i) * 200);
  }
  assert(animfix::snapshot_interval_ms(slow) == 200);
  assert(animfix::snapshot_bound_ms(slow) == animfix::k_max_snapshot_interval_ms);
}

// **A burst of fast decodes must not collapse the bound.** The floor is the
// shortest snapshot interval the animfix assumes, so a decode that arrives
// sooner than that is not a snapshot interval - it is a **burst** (a
// catch-up after a hitch, a re-ordered packet) - and the estimate must not
// learn from it.
//
// The failure this pins: four fast decodes among four normal ones used to
// collapse the estimate to the burst's interval, the bound fell to the 8 ms
// floor, and the capture aged out in the second half of every gap - the
// client's own prediction won those frames, which is the oscillation at the
// server's tick rate that the animfix exists to remove.
void test_a_burst_of_fast_decodes_does_not_collapse_the_bound() {
  animfix::SnapshotClock clock{};
  // A healthy 64 Hz stream.
  for (int i = 0; i < 8; ++i) {
    animfix::observe_decode(clock, 1000 + static_cast<std::uint64_t>(i) * 16);
  }
  assert(animfix::snapshot_bound_ms(clock) == 32);
  // A burst: four decodes two milliseconds apart.
  std::uint64_t now = 1000 + 7 * 16;
  for (int i = 0; i < 4; ++i) {
    now += 2;
    animfix::observe_decode(clock, now);
  }
  // The burst is not sampled: the estimate is still the server's rate.
  assert(animfix::snapshot_interval_ms(clock) == 16);
  assert(animfix::snapshot_bound_ms(clock) == 32);
  // And the capture is still fresh one interval later.
  const animfix::Capture capture = captured_at(now);
  assert(animfix::decide(capture, animfix::Slot::aim_yaw, now + k_interval_ms, 32, k_controller)
             .reapply);
}

// The burst must not cost the **duty cycle** either: the correction wins on
// every anim tick while the capture is fresh, and a collapsed bound would
// take it away from half of them.
void test_a_burst_of_fast_decodes_keeps_the_duty_cycle() {
  // Four normal snapshots and a burst of four fast ones, repeating.
  const double burst = duty_cycle({16, 16, 16, 16, 2, 2, 2, 2}, 7);
  assert(burst >= 0.99);
}

// A server that really does snapshot **faster** than the floor: the estimate
// has nothing to sample, and the bound is the floor - which still covers an
// interval shorter than the floor, so the capture never ages out.
void test_a_server_faster_than_the_floor_is_still_covered() {
  animfix::SnapshotClock clock{};
  for (int i = 0; i < 8; ++i) {
    animfix::observe_decode(clock, 1000 + static_cast<std::uint64_t>(i) * 4);
  }
  // Nothing above the floor: no estimate.
  assert(animfix::snapshot_interval_ms(clock) == 0);
  // The bound is the floor, and a capture younger than the floor is fresh.
  assert(animfix::snapshot_bound_ms(clock) == animfix::k_min_snapshot_interval_ms);
  const animfix::Capture capture = captured_at(1032);
  assert(animfix::decide(capture, animfix::Slot::aim_yaw, 1032 + 7,
                         animfix::k_min_snapshot_interval_ms, k_controller)
             .reapply);
}

// The **duty cycle** of the correction: the fraction of the anim ticks on
// which the server's value wins. The bound carries a margin over the median
// precisely so that this is 100 % on an ordinary connection - the median is
// by construction the value that half the intervals exceed, so a capture
// measured against it would age out in the second half of every gap and the
// client's own prediction would win those frames (a jitter at the server's
// tick rate, which is the failure the animfix exists to remove).
void test_the_correction_holds_a_full_duty_cycle() {
  // A 64 Hz server and a 144 Hz client: every anim tick is corrected.
  assert(duty_cycle({16}, 7) >= 0.99);
  // The same server with jitter (16-19 ms).
  assert(duty_cycle({16, 17, 18, 19}, 7) >= 0.99);
  // A quarter of the snapshots 16 ms late (16/32 ms).
  assert(duty_cycle({16, 16, 16, 32}, 7) >= 0.99);
  // Half of them late.
  assert(duty_cycle({16, 32}, 7) >= 0.99);
  // A 48 ms hitch every eighth snapshot: the hitch is longer than the
  // bound, so the last frames of it are refused - the rest is not.
  const double hitched = duty_cycle({16, 16, 16, 16, 16, 16, 16, 48}, 7);
  assert(hitched >= 0.85);
  assert(hitched < 1.0);
  // A **total** stall (no snapshot for half a second) is refused: the bound
  // does not move without a decode, so the capture ages out.
  const double stalled = duty_cycle({16, 16, 16, 16, 16, 16, 16, 500}, 7);
  assert(stalled < 0.6);
}

// --- the validity rule --------------------------------------------------------

// Nothing is re-applied before the first capture: the animfix has nothing of
// the server's to offer yet.
void test_nothing_is_reapplied_before_the_first_capture() {
  const animfix::Capture capture{};
  const animfix::Decision decision =
      animfix::decide(capture, animfix::Slot::aim_yaw, 1000, k_interval_ms, k_controller);
  assert(!decision.reapply);
  assert(decision.refusal == animfix::Refusal::no_capture);
}

// A fresh capture is re-applied.
void test_a_fresh_capture_is_reapplied() {
  animfix::Capture capture = captured_at(1000);
  const animfix::Decision decision =
      animfix::decide(capture, animfix::Slot::aim_yaw, 1000, k_interval_ms, k_controller);
  assert(decision.reapply);
  assert(decision.refusal == animfix::Refusal::none);
}

// A capture that has **aged out** is refused, even though it was never
// re-applied. This is the stalled-connection case: the server's value is
// older than the client's own prediction, so writing it would make the pose
// worse, not better.
//
// The ages are **literals** (an interval and two), not the constant: a test
// that derives its ages from the bound cannot fail when the bound is
// changed.
void test_an_aged_out_capture_is_refused() {
  const animfix::Capture capture = captured_at(1000);
  // One interval later: still fresh.
  assert(animfix::decide(capture, animfix::Slot::aim_yaw, 1000 + k_interval_ms, k_interval_ms,
                         k_controller)
             .reapply);
  // Two intervals later: aged out.
  const animfix::Decision late = animfix::decide(
      capture, animfix::Slot::aim_yaw, 1000 + k_two_intervals_ms, k_interval_ms, k_controller);
  assert(!late.reapply);
  assert(late.refusal == animfix::Refusal::stale);
}

// The refusal reasons are distinguishable: the operator has to be able to
// tell "the codec has not run yet" from "every capture aged out" and from
// "the controller is not the one the capture came from".
void test_the_refusal_reasons_are_distinguishable() {
  animfix::Capture capture = captured_at(1000);
  // A capture for another controller (the pawn respawned).
  const animfix::Decision other =
      animfix::decide(capture, animfix::Slot::aim_yaw, 1000, k_interval_ms, 0x2000);
  assert(!other.reapply);
  assert(other.refusal == animfix::Refusal::controller);
  // A capture that aged out.
  const animfix::Decision aged = animfix::decide(
      capture, animfix::Slot::aim_yaw, 1000 + k_two_intervals_ms, k_interval_ms, k_controller);
  assert(!aged.reapply);
  assert(aged.refusal == animfix::Refusal::stale);
}

// The capture is **not** consumed by a re-apply: it is re-applied on every
// anim tick while it is fresh, so the server's value stays in the graph
// between the decodes.
//
// The engine notes every write (`mark_applied`), and the **next** anim tick
// must still be allowed to re-apply the same capture. This is the pin for
// the property: a "consume once" rule - the one that made the feature
// fragile - would refuse the second apply, and the engine would then leave
// the client's prediction in the graph for the rest of the tick.
//
// A consume-once rule would leave the graph with the client's prediction for
// every later anim tick of the same tick - the pose would oscillate between
// the server's and the client's aim yaw at the server's tick rate - and in a
// listen server whichever of the two driver variants ran first would consume
// the capture, leaving the variant that evaluates the pose the player sees
// with nothing to write.
void test_a_fresh_capture_is_reapplied_on_every_tick() {
  animfix::Capture capture = captured_at(1000);
  for (int tick = 0; tick < 4; ++tick) {
    const animfix::Decision decision =
        animfix::decide(capture, animfix::Slot::aim_yaw, 1000 + static_cast<std::uint64_t>(tick),
                        k_interval_ms, k_controller);
    assert(decision.reapply);
  }
  // The engine notes the write; the capture must **not** be consumed by it.
  animfix::mark_applied(capture);
  assert(
      animfix::decide(capture, animfix::Slot::aim_yaw, 1001, k_interval_ms, k_controller).reapply);
  assert(
      animfix::decide(capture, animfix::Slot::aim_yaw, 1002, k_interval_ms, k_controller).reapply);
  animfix::mark_applied(capture);
  assert(
      animfix::decide(capture, animfix::Slot::aim_yaw, 1002, k_interval_ms, k_controller).reapply);
  assert(capture.reapply_count == 2);
  // And it stops when the capture ages out - that is the protection the
  // specification's rule is for.
  const animfix::Decision late = animfix::decide(
      capture, animfix::Slot::aim_yaw, 1000 + 2 * k_interval_ms, k_interval_ms, k_controller);
  assert(!late.reapply);
  assert(late.refusal == animfix::Refusal::stale);
}

// The capture is a **copy**: the values the animfix hands back are the ones
// the server sent, not the ones the slots hold afterwards. (The engine side
// copies them out of the controller; here the point is that the capture
// keeps its own copy.)
void test_the_capture_keeps_its_own_copy() {
  animfix::Capture capture{};
  std::array<float, animfix::k_tracked_slot_count> values = server_values(42.0F);
  animfix::store(capture, k_controller, values, 1000);
  values[animfix::slot_position(animfix::Slot::aim_yaw)] = 999.0F;
  assert(nearly(capture.values[animfix::slot_position(animfix::Slot::aim_yaw)], 42.0F));
}

// The **engine's own guard** refuses a bad value, and the refusal is counted
// **once per capture**.
//
// The capture is re-applied on every anim tick while it is fresh, so a
// per-apply count would climb at the tick rate (64 a second) and the
// operator could not tell "the server sent one bad snapshot" from "the
// server sends bad snapshots all the time". The count is per capture, so it
// says how many *snapshots* the server sent something the graph must not
// hold.
void test_the_guard_refusal_is_counted_once_per_capture() {
  animfix::Capture capture{};
  animfix::store(capture, k_controller, server_values(42.0F), 1000);
  const std::uint64_t generation = capture.generation;
  // The first refusal of this capture is counted.
  assert(animfix::mark_guard_refused(capture, generation));
  // The capture is re-applied on every anim tick: the refusal must not be
  // counted again.
  for (int tick = 0; tick < 64; ++tick) {
    assert(!animfix::mark_guard_refused(capture, generation));
  }
  // A new capture (the next decode) counts again - and the generations tell
  // the two apart even when they are taken in the same millisecond.
  animfix::store(capture, k_controller, server_values(42.0F), 1000);
  assert(capture.generation != generation);
  // A refusal that belongs to the **previous** capture is not counted
  // against this one: the new capture counts its own refusal.
  assert(!animfix::mark_guard_refused(capture, generation));
  assert(animfix::mark_guard_refused(capture, capture.generation));
  // And the flag does not leak into the decision: a capture the guard
  // refused is still re-applied on the slots it did not refuse.
  assert(
      animfix::decide(capture, animfix::Slot::aim_yaw, 1001, k_interval_ms, k_controller).reapply);
}

// The **log** must not get a line per tick.
//
// The hooks are installed from the CreateMove detour, which runs on every
// tick, and the payload's log helper opens, writes and closes the log file
// on **every call**: a line per tick would be ~64 file round trips a second
// on the game's main thread and ~500 MB of log a day. So the status is
// latched: the log gets a line only when the status **changes**.
void test_the_status_is_logged_once_per_distinct_status() {
  animfix::StatusLatch latch{};
  const std::string missing = "the calibration dump has no 'animfix_anim_tick' signature";
  // The first occurrence is logged.
  assert(latch.should_log(missing));
  // Every tick after it says the same thing: nothing is logged.
  for (int tick = 0; tick < 1000; ++tick) {
    assert(!latch.should_log(missing));
  }
  // A **different** status is logged again (the install recovered).
  assert(latch.should_log("ok"));
  assert(!latch.should_log("ok"));
  // And the latch holds the last one it was given.
  assert(latch.held() == "ok");
}

// The animfix's log must have **one** call site: the one that latches.
//
// This is the wiring the latch above cannot see for itself: a second
// `append_log` in the install path would put a line per tick back into the
// log, and the host test would still be green.
void test_the_animfix_logs_through_one_latched_call_site(const std::string& root) {
  std::ifstream source(root + "/payload/control/src/input.cpp");
  assert(source.good());
  const std::string text((std::istreambuf_iterator<char>(source)),
                         std::istreambuf_iterator<char>());
  const std::string prefix = "[cs2-payload] animfix: ";
  std::size_t sites = 0;
  std::size_t at = text.find(prefix);
  while (at != std::string::npos) {
    ++sites;
    at = text.find(prefix, at + prefix.size());
  }
  assert(sites == 1);
  // And that one call site is **latched**: the status is consulted before
  // the line is written.
  assert(text.find("animfix_status_latch_.should_log") != std::string::npos);
}

// The **duty cycle**'s denominator: the client anim ticks that ran while the
// animfix was on.
//
// The correction counter only moves while the animfix is on, and the
// driver's own client-tick counter climbs since the payload started (the
// animfix is off by default), so the two absolute counters have **different
// windows**: dividing them would report a duty cycle of 0 % for a feature
// that wins every tick, diluted by the payload's uptime. The denominator is
// therefore counted on the same branch as the correction itself.
void test_the_duty_cycles_denominator_is_counted_with_the_correction(const std::string& root) {
  std::ifstream source(root + "/payload/control/src/input.cpp");
  assert(source.good());
  const std::string text((std::istreambuf_iterator<char>(source)),
                         std::istreambuf_iterator<char>());
  const std::size_t branch = text.find("animfix_apply();");
  assert(branch != std::string::npos);
  // The counter is incremented in the same branch as the apply.
  const std::string window = text.substr(branch, 512);
  assert(window.find("animfix_enabled_client_ticks_.fetch_add") != std::string::npos);
}

// --- the controller the capture belongs to -----------------------------------

// The write goes to the graph parameters of the **live** controller. A local
// pawn that dies and respawns gets a new `CCS2PawnGraphController`, and the
// old one — with its AG2 instance — is freed: the `(node, index)` pairs
// captured from it then point into freed memory, and a write through them
// would be a write into freed memory (corruption, which is worse than a
// fault).
void test_the_capture_must_belong_to_the_live_controller() {
  animfix::Capture capture{};
  // No capture: it belongs to nothing.
  assert(!animfix::capture_matches_controller(capture, k_controller));
  animfix::store(capture, k_controller, server_values(42.0F), 1000);
  // The controller the capture came from: it matches.
  assert(animfix::capture_matches_controller(capture, k_controller));
  // A different controller (the pawn respawned): it does not.
  assert(!animfix::capture_matches_controller(capture, 0x2000));
  // The live controller is not known (0): it does not.
  assert(!animfix::capture_matches_controller(capture, 0));
}

// --- the target lookup ----------------------------------------------------

// The four tracked slots must point at four **different** graph parameters:
// two slots that shared one would collide in the lookup (the first match
// wins) and one of them would silently never be corrected.
void test_the_four_slots_must_be_distinct() {
  std::array<animfix::Target, animfix::k_tracked_slot_count> targets{};
  for (std::size_t i = 0; i < animfix::k_tracked_slot_count; ++i) {
    targets[i] = animfix::Target{0x1000 + i * 0x10, static_cast<std::int16_t>(i)};
  }
  assert(animfix::targets_are_distinct(targets));
  // A duplicate: refused.
  targets[2] = targets[1];
  assert(!animfix::targets_are_distinct(targets));
  // An unbound slot: refused.
  targets[2] = animfix::Target{0x1000 + 2 * 0x10, 2};
  targets[1] = animfix::Target{0, 0};
  assert(!animfix::targets_are_distinct(targets));
}

// --- the seam's prologue ----------------------------------------------------

// A detour that is never called: the seam test only installs and removes the
// hook.
auto probe_detour(void*) -> void {}

// The **whole** dumper signatures of the animfix's seams, byte for byte (the
// live build, sha256 659940c1...): `CAnimGraphGameSystem::AnimTickUpdate`
// (0x35aeb0), the netvar codec (0x8d9740) and the AG2 parameter setter
// (0x8492a0).
//
// The **whole** pattern is pinned, not just the prologue: the payload
// resolves the seam through the whole pattern, so a signature whose tail is
// wrong resolves nothing - and the animfix would be silently inert. The
// prologue (the first bytes, the window the inline hook copies) is the
// prefix of the pattern.
constexpr unsigned char k_anim_tick_pattern[] = {0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c,
                                                 0x24, 0x08, 0x55, 0x57, 0x41, 0x54, 0x41,
                                                 0x55, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24};
constexpr unsigned char k_codec_pattern[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8d, 0xac, 0x24, 0xf0, 0xf9, 0xff, 0xff, 0x48, 0x81, 0xec, 0x10, 0x07, 0x00, 0x00};
// The setter's pattern - the one with the RIP-relative load.
constexpr unsigned char k_setter_pattern[] = {0xf3, 0x41, 0x0f, 0x10, 0x08, 0x4c, 0x8b, 0xc9,
                                              0xf3, 0x0f, 0x10, 0x05, 0xa0, 0x0f, 0x3c, 0x01};

// The window the inline hook copies: the first bytes of the pattern.
constexpr std::size_t k_anim_tick_prologue_size = 15;
constexpr std::size_t k_codec_prologue_size = 14;
constexpr std::size_t k_setter_prologue_size = 16;

// The repository's root, so the test can read the dumper's own source.
auto test_repository_root(int argc, char** argv) -> std::string {
  // The repository's root is passed by the Meson test (the same shape as the
  // settings-sink test's source check).
  return argc > 1 ? std::string(argv[1]) : std::string();
}

// The dumper's signature for a name, as the hex pattern it pins.
auto signature_pattern(const std::string& source, const std::string& name) -> std::string {
  const auto at = source.find('"' + name + '"');
  if (at == std::string::npos) {
    return {};
  }
  const auto open = source.find('"', at + name.size() + 2);
  if (open == std::string::npos) {
    return {};
  }
  const auto close = source.find('"', open + 1);
  if (close == std::string::npos) {
    return {};
  }
  return source.substr(open + 1, close - open - 1);
}

// The bytes of a dumper signature.
auto signature_bytes(const std::string& source, const std::string& name)
    -> std::vector<unsigned char> {
  std::vector<unsigned char> bytes{};
  const std::string pattern = signature_pattern(source, name);
  std::size_t i = 0;
  while (i < pattern.size()) {
    const std::size_t end = pattern.find(' ', i);
    const std::string byte = pattern.substr(i, end == std::string::npos ? end : end - i);
    if (!byte.empty()) {
      bytes.push_back(static_cast<unsigned char>(std::stoul(byte, nullptr, 16)));
    }
    if (end == std::string::npos) {
      break;
    }
    i = end + 1;
  }
  return bytes;
}

// The test's pinned prologues and the dumper's signatures must not drift
// apart: the payload resolves the seam through the signature, so a
// signature that no longer matches the prologue the test pins would leave
// the test green and the seam unhookable.
void test_the_pinned_prologues_are_the_dumpers_signatures(const std::string& root) {
  assert(!root.empty());
  std::ifstream dumper(root + "/external/dumper/src/analysis.rs");
  assert(dumper.good());
  const std::string source((std::istreambuf_iterator<char>(dumper)),
                           std::istreambuf_iterator<char>());
  const auto matches = [&source](const std::string& name, const unsigned char* pattern,
                                 std::size_t size) -> bool {
    const std::vector<unsigned char> bytes = signature_bytes(source, name);
    // The **whole** pattern: the payload resolves the seam through all of
    // it, so a signature whose tail is wrong would resolve nothing.
    if (bytes.size() != size) {
      return false;
    }
    return std::memcmp(bytes.data(), pattern, size) == 0;
  };
  assert(matches("animfix_anim_tick", k_anim_tick_pattern, sizeof(k_anim_tick_pattern)));
  assert(matches("animfix_graph_codec", k_codec_pattern, sizeof(k_codec_pattern)));
  assert(matches("animfix_param_setter", k_setter_pattern, sizeof(k_setter_pattern)));
}

// **The seam must be hookable by the repository's own relocator, at any
// address distance.** The animfix's first cut hooked the AG2 parameter
// setter, whose prologue ends in a RIP-relative load: the relocator rewrites
// such a displacement as `disp + (target - trampoline)`, and with the module
// mapped high under ASLR and the trampoline allocated low the rewrite is
// ~141 TB and cannot fit in an `int32` — the hook engine refuses it, and the
// feature was silently inert.
//
// The property that decides this is **not** the instruction but the
// *distance*: a prologue with no RIP-relative operand needs no relocation at
// all, so the hook works wherever the two live. The relocator makes that
// observable — the trampoline's copied prologue is byte-identical to the
// original iff nothing had to be rewritten.
//
// The bytes of the two seams the animfix uses are pinned here: the anim
// tick's 15-byte prologue and the codec's 14-byte prologue must both be
// **relocation-free**. The setter's prologue is pinned too, as the
// counter-example: it *is* rewritten, which is why the seam is the anim
// tick and not the setter.
void test_the_seams_prologues_are_hookable() {
  // Installs the hook over the prologue and reports whether the relocator
  // had to **rewrite** anything (a RIP-relative displacement) and whether the
  // hook was accepted at all.
  struct Outcome {
    bool installed = false;
    bool rewritten = false;
  };
  const auto detour = reinterpret_cast<void*>(&probe_detour);
  const auto probe = [detour](const unsigned char* prologue, std::size_t size) -> Outcome {
    constexpr std::size_t k_buffer = 64;
    void* buffer = cs2::hooks::allocate_executable(k_buffer);
    if (buffer == nullptr) {
      return Outcome{};
    }
    auto* code = static_cast<unsigned char*>(buffer);
    std::memset(code, 0x90, k_buffer);
    std::memcpy(code, prologue, size);
    code[size] = 0xc3;  // ret
    cs2::hooks::InlineHook hook;
    const auto error = hook.install(buffer, detour);
    Outcome outcome{};
    outcome.installed = !error.has_value();
    if (outcome.installed) {
      const auto* trampoline = static_cast<const unsigned char*>(hook.original());
      outcome.rewritten = trampoline != nullptr && std::memcmp(trampoline, prologue, size) != 0;
    }
    hook.uninstall();
    cs2::hooks::free_executable(buffer, k_buffer);
    return outcome;
  };

  // The two seams the animfix uses: accepted, and **nothing to rewrite** -
  // so the hook works at any address distance.
  const Outcome anim_tick = probe(k_anim_tick_pattern, k_anim_tick_prologue_size);
  assert(anim_tick.installed);
  assert(!anim_tick.rewritten);
  const Outcome codec = probe(k_codec_pattern, k_codec_prologue_size);
  assert(codec.installed);
  assert(!codec.rewritten);
  // The setter's prologue **is** rewritten: its displacement depends on where
  // the module and the trampoline live, which in the game is ~141 TB apart.
  // That is why the seam is the anim tick and not the setter.
  const Outcome setter = probe(k_setter_pattern, k_setter_prologue_size);
  assert(setter.installed);  // it fits here, where both live close together
  assert(setter.rewritten);
}

// --- the evidence ---------------------------------------------------------

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

auto main(int argc, char** argv) -> int {
  const std::string repository_root = test_repository_root(argc, argv);
  test_the_slot_layout_is_the_engines();
  test_the_parameter_layout_is_the_engines();
  test_the_engines_own_guard_is_reproduced();
  test_the_bound_is_the_observed_snapshot_interval();
  test_a_late_decode_does_not_widen_the_bound();
  test_the_bound_has_a_hard_maximum();
  test_a_burst_of_fast_decodes_does_not_collapse_the_bound();
  test_a_burst_of_fast_decodes_keeps_the_duty_cycle();
  test_a_server_faster_than_the_floor_is_still_covered();
  test_the_correction_holds_a_full_duty_cycle();
  test_nothing_is_reapplied_before_the_first_capture();
  test_a_fresh_capture_is_reapplied();
  test_an_aged_out_capture_is_refused();
  test_the_refusal_reasons_are_distinguishable();
  test_a_fresh_capture_is_reapplied_on_every_tick();
  test_the_capture_keeps_its_own_copy();
  test_the_guard_refusal_is_counted_once_per_capture();
  test_the_status_is_logged_once_per_distinct_status();
  test_the_animfix_logs_through_one_latched_call_site(repository_root);
  test_the_duty_cycles_denominator_is_counted_with_the_correction(repository_root);
  test_the_capture_must_belong_to_the_live_controller();
  test_the_four_slots_must_be_distinct();
  test_the_pinned_prologues_are_the_dumpers_signatures(repository_root);
  test_the_seams_prologues_are_hookable();
  test_the_aim_yaw_difference_is_signed_and_wrapped();
  std::puts("control-animfix: ok");
  return 0;
}
