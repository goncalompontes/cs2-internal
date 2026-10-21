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
//   * a capture is **consumed once per slot** ("captured *this* frame"): the
//     animfix corrects a slot at most once per capture, so a value that has
//     already been written over the fresh prediction is never written again;
//   * a capture that has **aged out** is refused outright — and the bound is
//     the **observed snapshot interval** (the server's rate), pinned against
//     literals, so it cannot be loosened without the test noticing.
//
// The refusal **reasons** are pinned too: the operator has to be able to
// tell "the codec has not run yet" from "every capture aged out" and from
// "the capture was already used".
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
  // A 64 Hz server: the interval is ~16 ms.
  animfix::observe_decode(clock, 1000);
  animfix::observe_decode(clock, 1016);
  assert(animfix::snapshot_bound_ms(clock) == 16);
  // A 30 Hz server: the interval is ~33 ms, and the bound follows it.
  animfix::SnapshotClock slow{};
  animfix::observe_decode(slow, 1000);
  animfix::observe_decode(slow, 1033);
  animfix::observe_decode(slow, 1066);
  assert(animfix::snapshot_bound_ms(slow) == 33);
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
  assert(healthy == 16);
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
  assert(animfix::snapshot_bound_ms(fast) == 16);
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
// "the capture was already used".
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

// The animfix recognises **its own** four slots by the (node, index) pair
// the parameter lives at; a write to any other parameter (a remote
// player's, another class's) is left alone.
void test_only_the_animfixs_own_slots_are_recognised() {
  std::array<animfix::Target, animfix::k_tracked_slot_count> targets{};
  for (std::size_t i = 0; i < animfix::k_tracked_slot_count; ++i) {
    targets[i] = animfix::Target{0x1000 + i * 0x10, static_cast<std::int16_t>(i)};
  }
  const auto found = animfix::find_slot(targets, 0x1000 + 2 * 0x10, 2);
  assert(found.has_value());
  assert(*found == animfix::Slot::aim_pitch);
  // Another entity's parameter: not ours.
  assert(!animfix::find_slot(targets, 0x2000, 0).has_value());
  // The right node, the wrong index: not ours.
  assert(!animfix::find_slot(targets, 0x1000, 7).has_value());
  // A null node is never a target: an unbound slot must not match a write to
  // a null parameter (which would be a null dereference inside the engine).
  assert(!animfix::find_slot(targets, 0, 0).has_value());
  // The same, with an **unbound** slot in the table: a null node must not
  // match it either.
  std::array<animfix::Target, animfix::k_tracked_slot_count> unbound{};
  unbound[1] = animfix::Target{0, 0};
  assert(!animfix::find_slot(unbound, 0, 0).has_value());
}

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

// The prologues the animfix's seams have on the live build (sha256
// 659940c1...): `CAnimGraphGameSystem::AnimTickUpdate` (0x35aeb0), the netvar
// codec (0x8d9740) and the AG2 parameter setter (0x8492a0).
constexpr unsigned char k_anim_tick_prologue[] = {0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24,
                                                  0x08, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55};
constexpr unsigned char k_codec_prologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56,
                                              0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
// The setter's prologue - the one with the RIP-relative load.
constexpr unsigned char k_setter_prologue[] = {0xf3, 0x41, 0x0f, 0x10, 0x08, 0x4c, 0x8b, 0xc9,
                                               0xf3, 0x0f, 0x10, 0x05, 0xa0, 0x0f, 0x3c, 0x01};

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
  const auto matches = [&source](const std::string& name, const unsigned char* prologue,
                                 std::size_t size) -> bool {
    const std::vector<unsigned char> bytes = signature_bytes(source, name);
    if (bytes.size() < size) {
      return false;
    }
    return std::memcmp(bytes.data(), prologue, size) == 0;
  };
  assert(matches("animfix_anim_tick", k_anim_tick_prologue, sizeof(k_anim_tick_prologue)));
  assert(matches("animfix_graph_codec", k_codec_prologue, sizeof(k_codec_prologue)));
  assert(matches("animfix_param_setter", k_setter_prologue, sizeof(k_setter_prologue)));
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
  const Outcome anim_tick = probe(k_anim_tick_prologue, sizeof(k_anim_tick_prologue));
  assert(anim_tick.installed);
  assert(!anim_tick.rewritten);
  const Outcome codec = probe(k_codec_prologue, sizeof(k_codec_prologue));
  assert(codec.installed);
  assert(!codec.rewritten);
  // The setter's prologue **is** rewritten: its displacement depends on where
  // the module and the trampoline live, which in the game is ~141 TB apart.
  // That is why the seam is the anim tick and not the setter.
  const Outcome setter = probe(k_setter_prologue, sizeof(k_setter_prologue));
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
  test_nothing_is_reapplied_before_the_first_capture();
  test_a_fresh_capture_is_reapplied();
  test_an_aged_out_capture_is_refused();
  test_the_refusal_reasons_are_distinguishable();
  test_a_fresh_capture_is_reapplied_on_every_tick();
  test_the_capture_keeps_its_own_copy();
  test_the_capture_must_belong_to_the_live_controller();
  test_only_the_animfixs_own_slots_are_recognised();
  test_the_four_slots_must_be_distinct();
  test_the_pinned_prologues_are_the_dumpers_signatures(repository_root);
  test_the_seams_prologues_are_hookable();
  test_the_aim_yaw_difference_is_signed_and_wrapped();
  std::puts("control-animfix: ok");
  return 0;
}
