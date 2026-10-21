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
// validity rule is what prevents it, so the rule is pinned here, in both of
// its clauses:
//
//   * a capture is **consumed once per slot** ("captured *this* frame"): the
//     animfix corrects a slot at most once per capture, so a value that has
//     already been written over the fresh prediction is never written again;
//   * a capture that has **aged out** is refused outright — and the bound is
//     pinned against a **literal**, not against the constant, so raising the
//     constant cannot make the suite pass.
//
// The engine's layout is pinned too: the slot offsets, the stride, the four
// slots the server replicates as floats, and the shape of the graph
// parameter the correction writes. Those are the numbers the engine access
// uses to read the server's values and to find the parameter, so a wrong
// offset here would silently read the wrong field.

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "cs2/control/animfix.hpp"
#include "cs2/hooks/inline.hpp"
#include "cs2/hooks/memory.hpp"

namespace {

namespace animfix = cs2::control::animfix;

// A detour that is never called: the seam test only installs and removes the
// hook.
auto probe_detour(void*) -> void {}

constexpr float k_epsilon = 0.001F;

// The client frame the capture must not be older than (one 60 Hz frame, and
// just as much one of the server's 64 Hz ticks). The literal is the point:
// the test must not derive the ages it checks from the constant.
constexpr std::uint64_t k_one_frame_ms = 16;
// An age that is plainly past the frame: the capture must be refused.
constexpr std::uint64_t k_two_frames_ms = 31;

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

// The graph parameter the correction writes: the setter's own
// `*(float *)(*(node + 0x10)[index] + 0x18) = value`.
void test_the_parameter_layout_is_the_engines() {
  assert(animfix::k_node_array_offset == 0x10);
  assert(animfix::k_parameter_stride == 8);
  assert(animfix::k_parameter_value_offset == 0x18);
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
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1000);
  assert(value.has_value());
  assert(nearly(*value, 42.0F));
}

// **The specification's rule**: a capture is consumed once per slot. The
// second re-apply of the same capture is refused, so a value that has already
// been written over the client's fresh prediction is never written again —
// the animfix cannot make the pose worse than the prediction by repeating
// itself.
void test_a_capture_is_consumed_once_per_slot() {
  animfix::Capture capture{};
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  assert(animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  // The same capture, again: refused.
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  // The consumption is **per slot**: the other slots of the same capture are
  // still pending.
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
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  assert(animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  animfix::store(capture, 0x1000, server_values(43.0F), 1016);
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1016);
  assert(value.has_value());
  assert(nearly(*value, 43.0F));
}

// A capture that has **aged out** is refused, even though it was never
// re-applied. This is the stalled-connection case: the server's value is
// older than the client's own prediction, so writing it would make the pose
// worse, not better.
//
// The ages are **literals** (a frame and two frames), not the constant: a
// test that derives its ages from the bound cannot fail when the bound is
// raised.
void test_an_aged_out_capture_is_refused() {
  animfix::Capture capture{};
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  // One frame later: still fresh.
  assert(animfix::may_reapply(capture, animfix::Slot::aim_yaw, 1000 + k_one_frame_ms));
  // Two frames later: aged out.
  assert(!animfix::may_reapply(capture, animfix::Slot::aim_yaw, 1000 + k_two_frames_ms));
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, 1000 + k_two_frames_ms).has_value());
}

// The bound is **one client frame** — and just as much one of the server's
// 64 Hz ticks. A looser bound would let a capture that is older than the
// prediction be written over it.
void test_the_bound_is_one_frame() {
  assert(animfix::k_max_capture_age_ms <= k_one_frame_ms);
  assert(animfix::k_max_capture_age_ms > 0);
}

// A capture that aged out and is then **refreshed** by a new one works
// again: the animfix recovers as soon as the connection does.
void test_a_refreshed_capture_works_again() {
  animfix::Capture capture{};
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  const std::uint64_t too_late = 1000 + k_two_frames_ms;
  assert(!animfix::take(capture, animfix::Slot::aim_yaw, too_late).has_value());
  animfix::store(capture, 0x1000, server_values(44.0F), too_late);
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
  animfix::store(capture, 0x1000, values, 1000);
  values[animfix::slot_position(animfix::Slot::aim_yaw)] = 999.0F;
  const auto value = animfix::take(capture, animfix::Slot::aim_yaw, 1000);
  assert(value.has_value());
  assert(nearly(*value, 42.0F));
}

// The freshness of the capture is a question of its own: it is fresh while
// it has not aged out, whether or not it was already re-applied.
void test_the_freshness_of_a_capture() {
  animfix::Capture capture{};
  assert(!animfix::capture_is_fresh(capture, 1000));  // no capture at all
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  assert(animfix::capture_is_fresh(capture, 1000));
  assert(animfix::capture_is_fresh(capture, 1000 + k_one_frame_ms));
  assert(!animfix::capture_is_fresh(capture, 1000 + k_two_frames_ms));
  // A consumed capture is still fresh — it is simply no longer usable for
  // that slot.
  assert(animfix::take(capture, animfix::Slot::aim_yaw, 1000).has_value());
  assert(animfix::capture_is_fresh(capture, 1000));
  assert(!animfix::may_reapply(capture, animfix::Slot::aim_yaw, 1000));
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
  assert(!animfix::capture_matches_controller(capture, 0x1000));
  animfix::store(capture, 0x1000, server_values(42.0F), 1000);
  // The controller the capture came from: it matches.
  assert(animfix::capture_matches_controller(capture, 0x1000));
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
  // `CAnimGraphGameSystem::AnimTickUpdate` (0x35aeb0) and the netvar codec
  // (0x8d9740) on the live build (sha256 659940c1…).
  constexpr unsigned char k_anim_tick_prologue[] = {0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24,
                                                    0x08, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55};
  constexpr unsigned char k_codec_prologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56,
                                                0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
  // The setter's prologue — the one with the RIP-relative load.
  constexpr unsigned char k_setter_prologue[] = {0xf3, 0x41, 0x0f, 0x10, 0x08, 0x4c, 0x8b, 0xc9,
                                                 0xf3, 0x0f, 0x10, 0x05, 0xa0, 0x0f, 0x3c, 0x01};

  // Installs the hook over the prologue and reports whether the relocator
  // had to **rewrite** anything (a RIP-relative displacement) and whether the
  // hook was accepted at all.
  struct Outcome {
    bool installed = false;
    bool rewritten = false;
  };
  // A detour that never runs: the hook is only installed and removed.
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
  test_the_parameter_layout_is_the_engines();
  test_nothing_is_reapplied_before_the_first_capture();
  test_a_fresh_capture_is_reapplied();
  test_a_capture_is_consumed_once_per_slot();
  test_a_new_capture_reopens_the_slots();
  test_an_aged_out_capture_is_refused();
  test_the_bound_is_one_frame();
  test_the_freshness_of_a_capture();
  test_a_refreshed_capture_works_again();
  test_the_capture_keeps_its_own_copy();
  test_the_engines_own_guard_is_reproduced();
  test_the_capture_must_belong_to_the_live_controller();
  test_only_the_animfixs_own_slots_are_recognised();
  test_the_four_slots_must_be_distinct();
  test_the_seams_prologues_are_hookable();
  test_the_aim_yaw_difference_is_signed_and_wrapped();
  std::puts("control-animfix: ok");
  return 0;
}
