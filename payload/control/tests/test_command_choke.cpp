#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "cs2/control/command_choke.hpp"
namespace {
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
}  // namespace
int main() {
  using namespace cs2::control;
  RecoveryChokePolicy policy;
  ChokeSample s{true, true, false, 1, 2, 0, 100, 10, .14F, 1000, 100.0F};
  check(!policy.defer(s, 1000, true), "first observation sends normally");
  for (int cycle = 0; cycle < 2; ++cycle) {
    for (int i = 1; i <= 5; ++i) {
      ++s.command;
      s.time_ms += 15;
      s.penalty *= .9F;
      const bool hold = i <= 4;
      check(policy.defer(s, s.time_ms, true) == hold, "four unique commands then release fifth");
      check(policy.defer(s, s.time_ms + 1, true) == hold, "duplicate callbacks preserve decision");
      check(policy.held() == (hold ? i : 0), "unique command budget");
    }
  }
  auto advance = [&] {
    ++s.command;
    s.time_ms += 15;
    s.penalty *= .9F;
  };
  advance();
  check(policy.defer(s, s.time_ms, true), "new recovery cycle starts");
  s.attack = true;
  check(!policy.defer(s, s.time_ms, true) && policy.held() == 0,
        "final attack releases same command");
  s.attack = false;
  check(!policy.defer(s, s.time_ms, true), "attack reset cannot immediately rechoke");
  advance();
  check(policy.defer(s, s.time_ms, true), "resume recovering");
  check(!policy.defer(s, s.time_ms, false), "disable releases immediately");
  check(!policy.defer(s, s.time_ms, true), "reenable establishes a fresh baseline");
  advance();
  check(policy.defer(s, s.time_ms, true), "hold before timeout");
  check(!policy.defer(s, s.time_ms + 100, true), "stale state releases without a new command");
  advance();
  check(!policy.defer(s, s.time_ms, true), "new baseline after timeout");
  advance();
  check(policy.defer(s, s.time_ms, true), "hold before weapon change");
  ++s.weapon;
  check(!policy.defer(s, s.time_ms, true), "weapon change releases");
  advance();
  check(policy.defer(s, s.time_ms, true), "new weapon recovery");
  s.grounded = false;
  check(!policy.defer(s, s.time_ms, true), "airborne releases");
  s.grounded = true;
  advance();
  check(!policy.defer(s, s.time_ms, true), "landing baseline");
  advance();
  check(policy.defer(s, s.time_ms, true), "landing recovery holds");
  ++s.command;
  s.time_ms += 15;
  check(!policy.defer(s, s.time_ms, true), "moving plateau releases");
  advance();
  check(policy.defer(s, s.time_ms, true), "hold before lost sample");
  s.valid = false;
  check(!policy.defer(s, s.time_ms, true), "invalid local sample releases");
  s.valid = true;
  advance();
  policy.defer(s, s.time_ms, true);
  s.command += 3;
  s.time_ms += 15;
  s.penalty *= .9F;
  check(!policy.defer(s, s.time_ms, true), "discontinuous commands release");
  advance();
  check(policy.defer(s, s.time_ms, true), "continuous recovery");
  --s.clip;
  check(!policy.defer(s, s.time_ms, true), "ammo change releases even on duplicate command");
  // Stationary windows continue across many releases with a flat penalty.
  policy = {};
  s.penalty = .005F;
  s.horizontal_speed = 0;
  advance();
  check(!policy.defer(s, s.time_ms, true), "stationary baseline sends");
  for (int cycle = 0; cycle < 20; ++cycle) {
    for (int i = 1; i <= 5; ++i) {
      ++s.command;
      s.time_ms += 15;
      check(policy.defer(s, s.time_ms, true) == (i <= 4),
            "stationary plateau repeats four/fifth cycle");
      check(policy.defer(s, s.time_ms + 1, true) == (i <= 4),
            "stationary duplicate does not consume budget");
    }
  }
  ++s.command;
  s.time_ms += 15;
  s.horizontal_speed = 1;
  check(policy.defer(s, s.time_ms, true), "stationary threshold includes small velocity noise");
  ++s.command;
  s.time_ms += 15;
  s.horizontal_speed = 1.01F;
  check(!policy.defer(s, s.time_ms, true),
        "movement beyond stationary threshold releases at plateau");
  ++s.command;
  s.time_ms += 15;
  s.horizontal_speed = 0;
  check(policy.defer(s, s.time_ms, true),
        "stopping resumes cycles without requiring penalty decay");
  s.attack = true;
  check(!policy.defer(s, s.time_ms, true), "stationary attack releases immediately");
  s.attack = false;
  ++s.command;
  s.time_ms += 15;
  check(!policy.defer(s, s.time_ms, true), "new baseline after stationary attack");
  ++s.command;
  s.time_ms += 15;
  check(policy.defer(s, s.time_ms, true), "stationary cycle resumes after attack");
  s.horizontal_speed = std::nanf("");
  check(!policy.defer(s, s.time_ms, true), "unknown motion cannot be treated as stationary");
  s.horizontal_speed = 0;
  s.penalty = std::nanf("");
  check(!policy.defer(s, s.time_ms, true), "nonfinite penalty fails open");
  std::puts(
      "PASS: repeated stationary and recovery four-command windows, duplicate/final-attack gates, "
      "timeout, identity and "
      "state resets");
}
