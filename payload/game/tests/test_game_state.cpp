#include <cstdio>

#include "cs2/game/game_state.hpp"

namespace {

using cs2::game::Capability;
using cs2::game::GameScreen;
using cs2::game::GameSignals;
using cs2::game::GameState;
using cs2::game::Snapshot;

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("game-state: FAILED: %s\n", what);
    ++failures;
  }
}

// A spawned local player in a level, with a level's rules.
auto base_snapshot() -> Snapshot {
  Snapshot snapshot;
  snapshot.valid = true;
  snapshot.local = cs2::game::PlayerSnapshot{};
  snapshot.local->team = 2;
  snapshot.local->alive = true;
  snapshot.local->origin = cs2::game::Vec3{128.0F, 64.0F, 0.0F};
  snapshot.local->health = 100;
  snapshot.level.up = true;
  snapshot.rules.loaded = true;
  snapshot.rules.has_match_started = true;
  snapshot.rules.game_start_time = 30.0F;
  snapshot.rules.t_cant_buy = true;  // a live round, the window is closed
  snapshot.rules.ct_cant_buy = true;
  return snapshot;
}

auto screen_of(const Snapshot& snapshot, GameSignals signals = {}) -> GameScreen {
  return cs2::game::deduce(snapshot, signals).screen;
}

void test_screens() {
  {
    Snapshot snapshot;  // invalid: the engine is not readable
    expect(screen_of(snapshot) == GameScreen::not_ready, "an unreadable target is not-ready");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local.reset();  // the main screen: no local player
    snapshot.rules.has_match_started = false;
    snapshot.rules.game_start_time = 0.0F;
    snapshot.level.up = true;
    expect(screen_of(snapshot) == GameScreen::not_in_game, "no spawned local is not-in-game");
    GameSignals loading;
    loading.loading = true;
    expect(screen_of(snapshot, loading) == GameScreen::loading, "a load in flight is loading");
  }
  {
    Snapshot snapshot = base_snapshot();
    GameSignals signals;
    signals.disconnected = true;
    expect(screen_of(snapshot, signals) == GameScreen::disconnected,
           "a lost server is disconnected");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local->team = 0;
    expect(screen_of(snapshot) == GameScreen::team_select, "no team is team-select");
    snapshot.local->team = 1;
    expect(screen_of(snapshot) == GameScreen::spectator, "team 1 is spectator");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.warmup_period = true;
    expect(screen_of(snapshot) == GameScreen::warmup, "warmup wins over alive");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local->alive = false;
    expect(screen_of(snapshot) == GameScreen::dead, "a dead local is dead");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.freeze_period = true;
    expect(screen_of(snapshot) == GameScreen::freeze, "freeze time is freeze");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.game_phase = 4;
    expect(screen_of(snapshot) == GameScreen::half_time, "game phase 4 is half-time");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.overtime_playing = 1;
    expect(screen_of(snapshot) == GameScreen::overtime, "overtime is overtime");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.game_paused = true;
    expect(screen_of(snapshot) == GameScreen::paused, "a paused match is paused");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.round_win_status = 1;
    expect(screen_of(snapshot) == GameScreen::round_end, "a winner is round-end");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local->defusing = true;
    snapshot.rules.bomb_planted = true;
    snapshot.local->defuser = true;
    expect(screen_of(snapshot) == GameScreen::defusing, "defusing is defusing");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.bomb_planted = true;
    expect(screen_of(snapshot) == GameScreen::planted, "a planted bomb is planted");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.t_cant_buy = false;  // the window is open
    expect(screen_of(snapshot) == GameScreen::buy_time, "an open window is buy-time");
  }
  {
    Snapshot snapshot = base_snapshot();
    expect(screen_of(snapshot) == GameScreen::live, "otherwise the round is live");
  }
}

void test_capabilities() {
  {
    Snapshot snapshot;  // the main screen
    const GameState state = cs2::game::deduce(snapshot);
    expect(!state.can(Capability::overlay), "the menu does not draw the ESP");
    expect(!state.can(Capability::aim), "the menu does not aim");
    expect(!state.can(Capability::movement), "the menu does not move");
    expect(state.can(Capability::state) && state.can(Capability::score),
           "reading is always allowed");
  }
  {
    Snapshot snapshot = base_snapshot();
    const GameState state = cs2::game::deduce(snapshot);
    expect(state.can(Capability::overlay), "a live round draws");
    expect(state.can(Capability::aim), "a live round aims");
    expect(state.can(Capability::movement), "a live round moves");
    expect(!state.can(Capability::buy), "a live round has no buy window");
    expect(!state.can(Capability::defuse), "a live round has no bomb to defuse");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.freeze_period = true;
    const GameState state = cs2::game::deduce(snapshot);
    expect(state.can(Capability::buy), "freeze time buys");
    expect(!state.can(Capability::aim), "freeze time does not aim");
    expect(!state.can(Capability::movement), "freeze time does not move");
    expect(state.can(Capability::overlay), "freeze time still draws");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local->has_bomb = true;
    snapshot.rules.t_cant_buy = false;
    const GameState state = cs2::game::deduce(snapshot);
    expect(state.can(Capability::plant), "the C4 carrier can plant");
    snapshot.rules.bomb_planted = true;
    expect(!cs2::game::deduce(snapshot).can(Capability::plant),
           "a planted bomb cannot be planted again");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.rules.bomb_planted = true;
    snapshot.local->defuser = true;
    expect(cs2::game::deduce(snapshot).can(Capability::defuse), "a defuser can defuse");
    snapshot.local->defuser = false;
    expect(!cs2::game::deduce(snapshot).can(Capability::defuse),
           "a player without a defuser cannot defuse");
  }
  {
    Snapshot snapshot = base_snapshot();
    snapshot.local->team = 1;
    const GameState state = cs2::game::deduce(snapshot);
    expect(state.can(Capability::overlay), "a spectator draws");
    expect(!state.can(Capability::aim), "a spectator does not aim");
  }
}

}  // namespace

auto main() -> int {
  test_screens();
  test_capabilities();
  if (failures != 0) {
    std::printf("game-state: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("game-state: ok");
  return 0;
}
