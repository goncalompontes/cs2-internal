#pragma once

// The game-state machine: one screen, and what that screen permits.
//
// The payload used to gate its features on ad-hoc tests (`local->alive`,
// `level.up`, a menu flag). That is how a feature ends up solving in the main
// menu, firing in freeze time, or drawing when there is no level. This header
// is the **single** answer: reduce the snapshot (and the two facts only the
// caller knows -- a load in flight, a lost server) to one `GameScreen`, and ask
// `GameState::can` before doing anything.
//
// The reducer is pure and header-only: the host test `game-state` drives every
// screen and every capability without a game. The rules mirror
// `scripts/scenario/screens.py`, which is the harness's copy of the same
// decision; a divergence is a bug in one of them.

#include <cstdint>
#include <string_view>

#include "cs2/game/snapshot.hpp"

namespace cs2::game {

// The one screen the game is in. The order is the decision order (see
// `deduce`): the enum is the documentation.
enum class GameScreen : std::uint8_t {
  loading = 0,     // a map load is in flight (the caller saw `map`)
  not_ready,       // the target state is not readable yet
  not_in_game,     // the main screen
  disconnected,    // the client lost the server
  team_select,     // a level is up, no team yet
  spectator,       // team 1
  dead,            // the local player is not alive
  warmup,          // warmup period
  freeze,          // freeze time before a round
  half_time,       // between the halves
  overtime,        // overtime is being played
  paused,          // a timeout / pause
  round_end,       // a winner is set
  defusing,        // the local player is defusing
  planted,         // the bomb is down
  buy_time,        // the buy window is open during a live round
  live,            // a normal live round
};

[[nodiscard]] constexpr auto screen_name(GameScreen screen) noexcept -> std::string_view {
  switch (screen) {
    case GameScreen::loading:
      return "loading";
    case GameScreen::not_ready:
      return "not-ready";
    case GameScreen::not_in_game:
      return "not-in-game";
    case GameScreen::disconnected:
      return "disconnected";
    case GameScreen::team_select:
      return "team-select";
    case GameScreen::spectator:
      return "spectator";
    case GameScreen::dead:
      return "dead";
    case GameScreen::warmup:
      return "warmup";
    case GameScreen::freeze:
      return "freeze";
    case GameScreen::half_time:
      return "half-time";
    case GameScreen::overtime:
      return "overtime";
    case GameScreen::paused:
      return "paused";
    case GameScreen::round_end:
      return "round-end";
    case GameScreen::defusing:
      return "defusing";
    case GameScreen::planted:
      return "planted";
    case GameScreen::buy_time:
      return "buy-time";
    case GameScreen::live:
      return "live";
  }
  return "not-ready";
}

// Everything the payload may do, as a bit. The action bits match the harness's
// `Can`; the last group is the hack's own features, so a feature asks the same
// question as a console command instead of re-deriving it.
enum class Capability : std::uint8_t {
  state = 0,
  score,
  look,
  move,
  jump,
  attack,
  reload,
  buy,
  plant,
  defuse,
  drop,
  inspect,
  switch_weapon,
  grenade,
  teleport,
  reconnect,
  // The hack's features. Each is a capability so a feature cannot invent its
  // own gate: the table below is the one policy.
  overlay,      // draw the ESP/HUD
  world_overlay,// draw the world ESP (radar, sounds, items)
  chams,        // the 3D player chams
  thirdperson,  // the camera
  movement,     // auto-strafe / bhop
  aim,          // the ragebot's solve and its view write
  trigger,      // the triggerbot
  anti_aim,     // the fake angles
  animfix,      // the local animation fix
  backtrack,    // lag-record rewind
};

inline constexpr int k_capability_count = 26;

[[nodiscard]] constexpr auto capability_bit(Capability capability) noexcept -> std::uint32_t {
  return 1U << static_cast<std::uint32_t>(capability);
}

[[nodiscard]] constexpr auto capability_name(Capability capability) noexcept -> std::string_view {
  switch (capability) {
    case Capability::state:
      return "state";
    case Capability::score:
      return "score";
    case Capability::look:
      return "look";
    case Capability::move:
      return "move";
    case Capability::jump:
      return "jump";
    case Capability::attack:
      return "attack";
    case Capability::reload:
      return "reload";
    case Capability::buy:
      return "buy";
    case Capability::plant:
      return "plant";
    case Capability::defuse:
      return "defuse";
    case Capability::drop:
      return "drop";
    case Capability::inspect:
      return "inspect";
    case Capability::switch_weapon:
      return "switch-weapon";
    case Capability::grenade:
      return "grenade";
    case Capability::teleport:
      return "teleport";
    case Capability::reconnect:
      return "reconnect";
    case Capability::overlay:
      return "overlay";
    case Capability::world_overlay:
      return "world-overlay";
    case Capability::chams:
      return "chams";
    case Capability::thirdperson:
      return "thirdperson";
    case Capability::movement:
      return "movement";
    case Capability::aim:
      return "aim";
    case Capability::trigger:
      return "trigger";
    case Capability::anti_aim:
      return "anti-aim";
    case Capability::animfix:
      return "animfix";
    case Capability::backtrack:
      return "backtrack";
  }
  return "unknown";
}

// The capabilities every screen has: reading is always safe.
inline constexpr std::uint32_t k_read_only =
    capability_bit(Capability::state) | capability_bit(Capability::score);

// The capabilities a screen has when it is in a level and the local player can
// interact. The feature bits are derived from the action bits below.
inline constexpr std::uint32_t k_in_level =
    k_read_only | capability_bit(Capability::look) | capability_bit(Capability::overlay) |
    capability_bit(Capability::world_overlay) | capability_bit(Capability::chams) |
    capability_bit(Capability::thirdperson) | capability_bit(Capability::animfix) |
    capability_bit(Capability::anti_aim) | capability_bit(Capability::teleport);

inline constexpr std::uint32_t k_play =
    k_in_level | capability_bit(Capability::move) | capability_bit(Capability::jump) |
    capability_bit(Capability::attack) | capability_bit(Capability::reload) |
    capability_bit(Capability::drop) | capability_bit(Capability::inspect) |
    capability_bit(Capability::switch_weapon) | capability_bit(Capability::grenade) |
    capability_bit(Capability::movement) | capability_bit(Capability::aim) |
    capability_bit(Capability::trigger) | capability_bit(Capability::backtrack);

[[nodiscard]] constexpr auto screen_capabilities(GameScreen screen) noexcept -> std::uint32_t {
  switch (screen) {
    case GameScreen::not_ready:
    case GameScreen::loading:
    case GameScreen::team_select:
    case GameScreen::round_end:
      return k_read_only;
    case GameScreen::not_in_game:
    case GameScreen::disconnected:
      return k_read_only | capability_bit(Capability::reconnect);
    case GameScreen::spectator:
      return k_read_only | capability_bit(Capability::look) | capability_bit(Capability::overlay) |
             capability_bit(Capability::world_overlay) | capability_bit(Capability::chams) |
             capability_bit(Capability::thirdperson) | capability_bit(Capability::anti_aim);
    case GameScreen::dead:
      return k_in_level;
    case GameScreen::warmup:
      return k_play;
    case GameScreen::freeze:
      return k_in_level | capability_bit(Capability::buy);
    case GameScreen::half_time:
    case GameScreen::paused:
      return k_in_level;
    case GameScreen::overtime:
      return k_play;
    case GameScreen::defusing:
      return k_in_level;
    case GameScreen::planted:
      return k_play | capability_bit(Capability::defuse);
    case GameScreen::buy_time:
      return k_play | capability_bit(Capability::buy) | capability_bit(Capability::plant);
    case GameScreen::live:
      return k_play | capability_bit(Capability::plant);
  }
  return k_read_only;
}

// The facts only the caller knows: a load it asked for, and a server it saw
// the client leave. Everything else is in the snapshot.
struct GameSignals {
  bool loading = false;
  bool disconnected = false;
};

// Whether a level is up. The `game_rules` global and `level.up` are true at the
// main screen too, so this is the local-player rule the harness measured: the
// local is spawned (a real origin or a team), or the match has started.
[[nodiscard]] inline auto level_up(const Snapshot& snapshot) noexcept -> bool {
  if (!snapshot.valid) {
    return false;
  }
  if (snapshot.local.has_value()) {
    const PlayerSnapshot& local = *snapshot.local;
    if (local.origin.x != 0.0F || local.origin.y != 0.0F || local.origin.z != 0.0F) {
      return true;
    }
    if (local.team == 2 || local.team == 3) {
      return true;
    }
  }
  const RulesSnapshot& rules = snapshot.rules;
  return rules.has_match_started || rules.game_start_time > 0.0F ||
         rules.total_rounds_played > 0;
}

// Whether the buy window is open for the local player's team.
[[nodiscard]] inline auto buy_window_open(const Snapshot& snapshot) noexcept -> bool {
  if (!snapshot.local.has_value()) {
    return false;
  }
  // Freeze time is the pre-round buy window, whatever the per-team "buy time
  // is over" flags say.
  if (snapshot.rules.freeze_period) {
    return true;
  }
  const int team = snapshot.local->team;
  if (team == 2) {
    return !snapshot.rules.t_cant_buy;
  }
  if (team == 3) {
    return !snapshot.rules.ct_cant_buy;
  }
  return false;
}

// The reduced state: one screen and the capabilities in force.
struct GameState {
  GameScreen screen = GameScreen::not_ready;
  std::uint32_t capabilities = k_read_only;
  bool buy_window = false;
  bool bomb_planted = false;
  bool has_bomb = false;
  bool defuser = false;
  bool defusing = false;

  // Whether `capability` may be used now. The dynamic gates (the buy window,
  // the C4, the defuser) are applied here, not in the screen table.
  [[nodiscard]] auto can(Capability capability) const noexcept -> bool {
    const std::uint32_t bit = capability_bit(capability);
    if ((capabilities & bit) == 0) {
      return false;
    }
    if (capability == Capability::buy) {
      return buy_window;
    }
    if (capability == Capability::plant) {
      return has_bomb && !bomb_planted;
    }
    if (capability == Capability::defuse) {
      return bomb_planted && defuser && !defusing;
    }
    return true;
  }

  // Whether the screen is "a level is up" (drawing is safe, players exist).
  [[nodiscard]] auto in_level() const noexcept -> bool {
    switch (screen) {
      case GameScreen::team_select:
      case GameScreen::spectator:
      case GameScreen::dead:
      case GameScreen::warmup:
      case GameScreen::freeze:
      case GameScreen::half_time:
      case GameScreen::overtime:
      case GameScreen::paused:
      case GameScreen::round_end:
      case GameScreen::defusing:
      case GameScreen::planted:
      case GameScreen::buy_time:
      case GameScreen::live:
        return true;
      case GameScreen::loading:
      case GameScreen::not_ready:
      case GameScreen::not_in_game:
      case GameScreen::disconnected:
        return false;
    }
    return false;
  }
};

// Reduce a snapshot to the screen. The order is the policy; each step is why
// the next narrower screen wins.
[[nodiscard]] inline auto deduce(const Snapshot& snapshot, GameSignals signals = {}) noexcept
    -> GameState {
  GameState state;
  state.buy_window = buy_window_open(snapshot);
  if (snapshot.local.has_value()) {
    const PlayerSnapshot& local = *snapshot.local;
    state.bomb_planted = snapshot.rules.bomb_planted;
    state.has_bomb = local.has_bomb;
    state.defuser = local.defuser;
    state.defusing = local.defusing;
  }

  const bool up = level_up(snapshot);
  if (signals.loading && !up) {
    state.screen = GameScreen::loading;
  } else if (!snapshot.valid) {
    state.screen = GameScreen::not_ready;
  } else if (!up) {
    state.screen = GameScreen::not_in_game;
  } else if (signals.disconnected) {
    state.screen = GameScreen::disconnected;
  } else if (!snapshot.local.has_value() || (snapshot.local->team != 1 &&
                                             snapshot.local->team != 2 && snapshot.local->team != 3)) {
    state.screen = GameScreen::team_select;
  } else if (snapshot.local->team == 1) {
    state.screen = GameScreen::spectator;
  } else if (snapshot.rules.warmup_period) {
    state.screen = GameScreen::warmup;
  } else if (!snapshot.local->alive) {
    state.screen = GameScreen::dead;
  } else if (snapshot.rules.freeze_period) {
    state.screen = GameScreen::freeze;
  } else if (snapshot.rules.game_phase == 4) {
    state.screen = GameScreen::half_time;
  } else if (snapshot.rules.overtime_playing > 0) {
    state.screen = GameScreen::overtime;
  } else if (snapshot.rules.game_paused) {
    state.screen = GameScreen::paused;
  } else if (snapshot.rules.round_win_status != 0) {
    state.screen = GameScreen::round_end;
  } else if (snapshot.local->defusing) {
    state.screen = GameScreen::defusing;
  } else if (snapshot.rules.bomb_planted) {
    state.screen = GameScreen::planted;
  } else if (state.buy_window) {
    state.screen = GameScreen::buy_time;
  } else {
    state.screen = GameScreen::live;
  }

  state.capabilities = screen_capabilities(state.screen);
  return state;
}

}  // namespace cs2::game
