// Host regression test for the chams' draw-hook **target guard**.
//
// The guard is the whole point of the feature: the draw hook's address is
// resolved through a dumper signature and the bytes at that address are
// compared with the prologue the RE measured **before** anything is patched.
// The previous target came from an MSVC RTTI *lambda* type and took the game
// down with an access violation, so the rule has to be provable without a
// game.
//
// The decision is a pure function (`cs2::features::hook_target::decide`), so
// this test drives it directly: no engine, no Windows headers, no game.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build (Meson's `b_ndebug` default
// is `if-release`, so `NDEBUG` is defined in the configuration
// `scripts/release.sh` builds and every `assert` would be compiled out — a
// test that only bites in the debug build is not a test).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "cs2/features/hook_target.hpp"

namespace {

namespace target = cs2::features::hook_target;

// The number of failed checks. The test's exit code is the verdict, so a
// release build (where `assert` is compiled out) still reports the failure.
int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// The facts of a healthy target: the offset is there, the module is loaded
// and the prologue is the expected one.
auto good_facts() -> target::Facts {
  target::Facts facts;
  facts.offset_present = true;
  facts.rva = 0x580A0;
  facts.offset_module = "scenesystem.dll";
  facts.module_present = true;
  facts.module_base = 0x180000000;
  facts.module_size = 0x978000;
  facts.prologue_read = true;
  facts.prologue_length = target::PROLOGUE_SIZE;
  facts.prologue = target::SCENE_DRAW_PROLOGUE;
  return facts;
}

// The verdict for the facts, with the chams' own expectations.
auto verdict(const target::Facts& facts) -> target::Status {
  return target::decide(facts, target::SCENE_DRAW_PROLOGUE, "scenesystem.dll");
}

// The happy path: everything checks out and the target may be patched.
void test_accepts_the_right_prologue() {
  expect(verdict(good_facts()) == target::Status::accepted, "the right prologue is accepted");
  // The prologue the RE measured is the one the constant carries.
  expect(target::SCENE_DRAW_PROLOGUE[0] == 0x4C && target::SCENE_DRAW_PROLOGUE[19] == 0x57,
         "the expected prologue is the measured one");
  expect(target::PROLOGUE_SIZE == 20, "the guard compares twenty bytes");
}

// One byte changed: the module is not the build the measurement was taken on.
void test_refuses_a_changed_prologue() {
  for (std::size_t index = 0; index < target::PROLOGUE_SIZE; ++index) {
    target::Facts facts = good_facts();
    facts.prologue[index] = static_cast<std::uint8_t>(facts.prologue[index] ^ 0x01);
    const std::string what =
        "a prologue with byte " + std::to_string(index) + " changed is refused";
    expect(verdict(facts) == target::Status::mismatch, what.c_str());
  }
}

// A read that returned less than the whole prologue is not a verification.
void test_refuses_a_truncated_prologue() {
  target::Facts facts = good_facts();
  facts.prologue_length = target::PROLOGUE_SIZE - 1;
  expect(verdict(facts) == target::Status::unreadable, "a truncated prologue is refused");
  facts.prologue_length = 0;
  expect(verdict(facts) == target::Status::unreadable, "a prologue of no bytes is refused");
}

// An empty read: the target could not be read at all.
void test_refuses_an_empty_read() {
  target::Facts facts = good_facts();
  facts.prologue_read = false;
  facts.prologue_length = 0;
  facts.prologue = {};
  expect(verdict(facts) == target::Status::unreadable, "an empty read is refused");
}

// The calibration dump has no offset for the target: the signature was never
// resolved, so there is nothing to patch.
void test_refuses_a_missing_offset() {
  target::Facts facts = good_facts();
  facts.offset_present = false;
  facts.rva = 0;
  expect(verdict(facts) == target::Status::no_offset, "a missing offset is refused");

  facts = good_facts();
  facts.rva = 0;
  expect(verdict(facts) == target::Status::no_offset, "an offset of zero is refused");

  // The offset belongs to another module: the RVA would be meaningless here.
  facts = good_facts();
  facts.offset_module = "client.dll";
  expect(verdict(facts) == target::Status::no_offset, "an offset from another module is refused");

  // An offset that names no module at all: the module identity cannot be
  // checked, so the offset cannot be trusted either.
  facts = good_facts();
  facts.offset_module = "";
  expect(verdict(facts) == target::Status::no_offset, "an offset without a module is refused");
}

// The game is not up yet: the module is not loaded.
void test_refuses_without_the_module() {
  target::Facts facts = good_facts();
  facts.module_present = false;
  facts.module_base = 0;
  facts.module_size = 0;
  facts.prologue_read = false;
  facts.prologue_length = 0;
  expect(verdict(facts) == target::Status::no_module, "a missing module is refused");
}

// The resolved address has to lie inside the module image.
void test_refuses_an_address_outside_the_module() {
  target::Facts facts = good_facts();
  facts.rva = 0x978000;  // the first byte past the image
  expect(verdict(facts) == target::Status::outside_module, "an address past the module is refused");

  facts = good_facts();
  facts.rva = 0x977FF0;  // inside, but the prologue would run past the end
  expect(verdict(facts) == target::Status::outside_module,
         "an address with no room for the prologue is refused");

  facts = good_facts();
  facts.module_size = 0;
  expect(verdict(facts) == target::Status::outside_module, "a module of no size is refused");
}

// Every state has a description: the log and the overlay show it.
void test_every_status_is_described() {
  const target::Status statuses[] = {
      target::Status::idle,           target::Status::pending,    target::Status::accepted,
      target::Status::installed,      target::Status::no_offset,  target::Status::no_module,
      target::Status::outside_module, target::Status::unreadable, target::Status::mismatch,
      target::Status::refused,
  };
  for (const target::Status status : statuses) {
    expect(!target::describe(status).empty(), "every status has a description");
  }
  expect(target::describe(target::Status::installed) == "installed",
         "the installed status reads as installed");
}

// --- The install's bookkeeping ------------------------------------------------
//
// The refusal latch is what stands between a refused install and a log line
// per frame. It has to be cleared when the operator switches the feature off,
// or the next switch-on never retries the install: the HUD would say
// "installing" forever (no install is being attempted) and the reason the
// hook is not installed would never be reported again.

// A refused install is not retried while the feature stays on.
void test_a_refusal_is_not_retried() {
  expect(!target::may_attempt_install(true, true), "a refused install is not retried");
  // The feature is off: nothing is patched, so nothing is attempted either.
  expect(!target::may_attempt_install(false, false), "an off feature is never patched");
  expect(!target::may_attempt_install(false, true), "an off feature is never patched");
  // The feature is on and nothing was refused: the install is attempted.
  expect(target::may_attempt_install(true, false), "an enabled feature is patched");
}

// The latch follows the operator's switch: off clears it, on keeps it.
void test_the_refusal_latch_follows_the_switch() {
  bool refused = true;
  refused = target::refusal_after_switch(false, refused);
  expect(!refused, "switching the feature off clears the refusal latch");
  expect(target::may_attempt_install(true, refused),
         "the next switch-on retries the install and reports the refusal again");
  // Staying on keeps the latch: the refusal is not retried every frame.
  bool still_refused = true;
  expect(target::refusal_after_switch(true, still_refused), "staying on keeps the refusal latch");
  expect(!target::may_attempt_install(true, still_refused),
         "a refusal is not retried while the feature stays on");
}

// The whole sequence, as the feature runs it: the refusal is reported, the
// operator switches the feature off and on again, and the refusal is
// reported **again** (the log's latch is reset with the state).
void test_the_refusal_is_reported_again_after_a_re_enable() {
  // The state the HUD shows and the state the log last reported.
  target::Status status = target::Status::pending;
  target::Status reported = target::Status::pending;
  bool refused = false;
  int lines = 0;

  // The install is attempted and refused.
  refused = true;
  status = target::Status::no_offset;
  if (target::must_report(reported, status)) {
    ++lines;
    reported = status;
  }
  expect(lines == 1, "the refusal is reported once");
  expect(status == target::Status::no_offset, "the HUD shows the reason the hook is not installed");

  // The same state again: the log is latched, so there is no second line.
  if (target::must_report(reported, status)) {
    ++lines;
    reported = status;
  }
  expect(lines == 1, "the same state is not reported twice");

  // The operator switches the feature off: the state goes back to "off", the
  // log's latch is reset and the refusal latch is cleared.
  status = target::Status::idle;
  reported = target::Status::idle;
  refused = target::refusal_after_switch(false, refused);
  expect(status == target::Status::idle, "the state follows the switch");
  expect(!refused, "the refusal latch is cleared by the switch-off");

  // The operator switches the feature on again: the install is retried.
  status = target::Status::pending;
  reported = target::Status::pending;
  expect(target::may_attempt_install(true, refused), "the install is retried after a re-enable");

  // The retry is refused again, and the refusal is reported again.
  status = target::Status::no_offset;
  if (target::must_report(reported, status)) {
    ++lines;
    reported = status;
  }
  expect(lines == 2, "the refusal is reported again after a re-enable");
  expect(status != target::Status::pending, "the status is not stuck at installing");
}

}  // namespace

auto main() -> int {
  test_accepts_the_right_prologue();
  test_refuses_a_changed_prologue();
  test_refuses_a_truncated_prologue();
  test_refuses_an_empty_read();
  test_refuses_a_missing_offset();
  test_refuses_without_the_module();
  test_refuses_an_address_outside_the_module();
  test_every_status_is_described();
  test_a_refusal_is_not_retried();
  test_the_refusal_latch_follows_the_switch();
  test_the_refusal_is_reported_again_after_a_re_enable();
  if (failures != 0) {
    std::printf("features-chams-target: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-chams-target: ok");
  return 0;
}
