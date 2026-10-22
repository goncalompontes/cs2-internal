#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cs2/control/move_clock_probe.hpp"
using namespace cs2::control;
namespace {
void check(bool value, const char* why) {
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
CommandTickFn fixture_entry = nullptr;
struct Fixture {
  int command{100}, tick_base{1000}, processed{100}, acknowledged{90};
  int stage_calls{}, seen_argument{}, out_of_range{};
  float penalty{.125F};
  bool simulate{}, reenter{};
  bool sample_valid{true};
  bool grounded{true};
  int landed_tick{10};
};
#if defined(__clang__)
__attribute__((noinline,optnone))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
void command_tick(void* pointer,int count) {
  auto* fixture = static_cast<Fixture*>(pointer);
  fixture->seen_argument = count;
  if (count < 0) return;
  if (count > 128) {
    ++fixture->out_of_range;
    return;
  }
  for (int i = 0; i <= count; ++i) {
    ++fixture->stage_calls;
    ++fixture->command;
    // Synthetic, separately controlled simulation. Command generation alone
    // does not advance it. This is not an engine/server prediction claim.
    if (fixture->simulate) {
      ++fixture->tick_base;
      ++fixture->processed;
      fixture->penalty = .02F + (fixture->penalty - .02F) * std::exp(-std::log(10.0F) / 64 / .4F);
    }
  }
}
MoveClockSnapshot sample(void* pointer, void*) noexcept {
  auto& f = *static_cast<Fixture*>(pointer);
  if (f.reenter) {
    f.reenter = false;
    fixture_entry(&f, -1);
  }
  MoveClockSnapshot out{f.sample_valid, f.command,      f.tick_base,
                        f.processed,    f.acknowledged, f.penalty};
  out.pawn = reinterpret_cast<std::uintptr_t>(&f);
  out.flags = f.grounded ? 1u : 0u;
  out.landed_tick = f.landed_tick;
  return out;
}
void print_case(const char* name, const MoveClockReport& r) {
  std::printf(
      "case=%s synthetic=true arg=%d->%d extra=%d generated=%d tickbase_delta=%d "
      "processed_delta=%d ack_delta=%d penalty=%.6f->%.6f\n",
      name, r.original_count, r.forwarded_count, r.applied_extra,
      r.after.command - r.before.command, r.after.tick_base - r.before.tick_base,
      r.after.last_processed - r.before.last_processed,
      r.after.acknowledged_command - r.before.acknowledged_command, r.before.accuracy_penalty,
      r.after.accuracy_penalty);
}

}  // namespace
int main() {
  Fixture fixture;
  // Stable executable entry, independent of compiler-selected spill length.
  // A separate PE probe checks the inspected engine's actual prologue.
  auto* entry = static_cast<unsigned char*>(
      VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  check(entry != nullptr, "fixture entry allocation");
  std::memset(entry, 0x90, 14);
  const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0};
  std::memcpy(entry + 14, jump, sizeof(jump));
  const auto destination = reinterpret_cast<std::uintptr_t>(&command_tick);
  std::memcpy(entry + 20, &destination, sizeof(destination));
  DWORD previous = 0;
  check(VirtualProtect(entry, 64, PAGE_EXECUTE_READ, &previous) != 0, "fixture entry executable");
  FlushInstructionCache(GetCurrentProcess(), entry, 64);
  fixture_entry = reinterpret_cast<CommandTickFn>(entry);
  MoveClockProbe probe;
  check(probe.install(fixture_entry, &sample, &fixture),
        "real function detour installs in the test process");
  fixture_entry(&fixture, 0);
  auto r = probe.report();
  check(r.calls == 1 && r.original_count == 0 && r.forwarded_count == 0 &&
            r.after.command - r.before.command == 1,
        "baseline preserves zero=>one native count semantics");
  check(probe.request_extra(3), "one bounded experiment can be armed");
  check(!probe.request_extra(2), "pending experiment cannot be silently overwritten");
  fixture_entry(&fixture, 0);
  r = probe.report();
  check(r.applied_extra == 3 && r.forwarded_count == 3 && r.after.command - r.before.command == 4,
        "detour adjusts the integer count once");
  check(r.after.tick_base == r.before.tick_base &&
            r.after.last_processed == r.before.last_processed &&
            r.after.accuracy_penalty == r.before.accuracy_penalty,
        "extra generated commands are not advertised as simulated clock/accuracy recovery");
  print_case("generation_without_simulation", r);
  check(r.after.acknowledged_command == r.before.acknowledged_command,
        "server acknowledgement is independent");
  fixture_entry(&fixture, 0);
  check(probe.report().applied_extra == 0, "experiment is one-shot");
  check(probe.request_extra(2), "second experiment arms");
  fixture_entry(&fixture, -1);
  check(probe.report().forwarded_count == -1, "negative no-command phase is preserved");
  fixture.simulate = true;
  fixture_entry(&fixture, 1);
  r = probe.report();
  check(r.forwarded_count == 3 && r.after.tick_base - r.before.tick_base == 4 &&
            r.after.last_processed - r.before.last_processed == 4 &&
            r.after.accuracy_penalty < r.before.accuracy_penalty,
        "reader observes simulation only when the synthetic fixture explicitly performs it");
  print_case("synthetic_explicit_simulation", r);
  check(!probe.request_extra(-1) && !probe.request_extra(9), "invalid request sizes are rejected");
  check(probe.request_extra(1), "overflow case arms");
  fixture_entry(&fixture, 0x7fffffff);
  check(fixture.seen_argument == 0x7fffffff, "native argument cannot wrap on adjustment");
  fixture.reenter = true;
  fixture_entry(&fixture, 0);
  r = probe.report();
  check(r.reentrant == 1 && r.applied_extra == 1,
        "recursive call does not consume or duplicate the experiment");
  check(probe.request_extra(2), "unavailable-clock experiment arms");
  fixture.sample_valid = false;
  fixture_entry(&fixture, 0);
  check(probe.report().applied_extra == 0,
        "unavailable pre-state does not run an unmeasurable experiment");
  fixture.sample_valid = true;
  fixture_entry(&fixture, 0);
  check(probe.report().applied_extra == 2, "pending request waits for a valid measurement");
  probe.uninstall();
  const auto calls = probe.report().calls;
  fixture_entry(&fixture, 0);
  check(probe.report().calls == calls, "uninstall restores the real function");
  MoveClockProbe live;
  check(live.install_live(fixture_entry, &sample, &fixture),
        "live backend publishes trampoline before enabling hook");
  fixture_entry(&fixture, 0);
  check(live.request_extra(1, true), "landing-specific experiment arms from a valid snapshot");
  fixture_entry(&fixture, 0);
  check(live.report().applied_extra == 0, "old landing cannot trigger live request");
  fixture.grounded = false;
  ++fixture.landed_tick;
  fixture_entry(&fixture, 0);
  check(live.report().applied_extra == 0, "ungrounded record does not trigger landing request");
  fixture.grounded = true;
  fixture_entry(&fixture, 0);
  check(live.report().applied_extra == 1, "first new grounded landing triggers once");
  fixture_entry(&fixture, 0);
  check(live.report().applied_extra == 0 && live.last_adjustment().applied_extra == 1,
        "adjustment evidence is latched independently of later normal calls");
  const auto live_calls = live.report().calls;
  check(live.disable_live(), "live hook disables without freeing in-flight storage");
  fixture_entry(&fixture, 0);
  check(live.report().calls == live_calls, "disabled live backend restores normal entry");
  live.uninstall();
  VirtualFree(entry, 0, MEM_RELEASE);
  std::puts(
      "PASS: real inline detour; inspected ABI/count model; one-shot extra commands; "
      "clock/processed/ack separation; recursion; overflow; restore");
}
