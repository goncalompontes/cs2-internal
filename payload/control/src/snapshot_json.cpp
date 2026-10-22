#include "cs2/control/snapshot_json.hpp"

#include <cstddef>
#include <cstdio>
#include <string>

namespace cs2::control {
namespace {

[[nodiscard]] auto hex_pointer(std::uintptr_t value) -> std::string {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
  return std::string(buffer);
}

[[nodiscard]] auto player_to_json(const game::PlayerSnapshot& player) -> Json {
  Json out = Json::object();
  out.set("index", Json::integer(player.index));
  out.set("name", Json::string(player.name));
  out.set("weapon", Json::string(player.weapon));
  out.set("team", Json::integer(player.team));
  out.set("connected", Json::integer(player.connected));
  out.set("health", Json::integer(player.health));
  out.set("max_health", Json::integer(player.max_health));
  out.set("armor", Json::integer(player.armor));
  out.set("life_state", Json::integer(player.life_state));
  out.set("on_ground", Json::boolean(player.on_ground));
  out.set("flags", Json::integer(static_cast<std::int64_t>(player.flags)));
  out.set("clip", Json::integer(player.clip));
  out.set("reserve", Json::integer(player.reserve));
  out.set("ping", Json::integer(player.ping));
  out.set("money", Json::integer(player.money));
  out.set("flash", Json::number(player.flash));
  out.set("alive", Json::boolean(player.alive));
  out.set("is_local", Json::boolean(player.is_local));
  out.set("helmet", Json::boolean(player.helmet));
  out.set("defuser", Json::boolean(player.defuser));
  out.set("scoped", Json::boolean(player.scoped));
  out.set("defusing", Json::boolean(player.defusing));
  out.set("in_buy_zone", Json::boolean(player.in_buy_zone));
  out.set("has_bomb", Json::boolean(player.has_bomb));
  out.set("origin", to_json(player.origin));
  out.set("eye_origin", to_json(player.eye_origin));
  out.set("velocity", to_json(player.velocity));
  out.set("eye_angles", to_json(player.eye_angles));
  out.set("distance", Json::number(player.distance));
  return out;
}

}  // namespace

auto to_json(const game::Vec3& value) -> Json {
  Json out = Json::object();
  out.set("x", Json::number(value.x));
  out.set("y", Json::number(value.y));
  out.set("z", Json::number(value.z));
  return out;
}

auto to_json(const game::Angle& value) -> Json {
  Json out = Json::object();
  out.set("pitch", Json::number(value.pitch));
  out.set("yaw", Json::number(value.yaw));
  out.set("roll", Json::number(value.roll));
  return out;
}

auto to_json(const game::PlayerSnapshot& player) -> Json {
  return player_to_json(player);
}

auto to_json(const game::RulesSnapshot& rules) -> Json {
  Json out = Json::object();
  out.set("loaded", Json::boolean(rules.loaded));
  out.set("match_started", Json::boolean(rules.has_match_started));
  out.set("freeze", Json::boolean(rules.freeze_period));
  out.set("warmup", Json::boolean(rules.warmup_period));
  out.set("game_restart", Json::boolean(rules.game_restart));
  out.set("bomb_planted", Json::boolean(rules.bomb_planted));
  out.set("bomb_dropped", Json::boolean(rules.bomb_dropped));
  out.set("t_cant_buy", Json::boolean(rules.t_cant_buy));
  out.set("ct_cant_buy", Json::boolean(rules.ct_cant_buy));
  out.set("game_phase", Json::integer(rules.game_phase));
  out.set("round_time", Json::integer(rules.round_time));
  out.set("freeze_time", Json::integer(rules.freeze_time));
  out.set("round_win_status", Json::integer(rules.round_win_status));
  out.set("round_win_reason", Json::integer(rules.round_win_reason));
  out.set("total_rounds_played", Json::integer(rules.total_rounds_played));
  out.set("match_start_time", Json::number(rules.match_start_time));
  out.set("round_start_time", Json::number(rules.round_start_time));
  out.set("restart_round_time", Json::number(rules.restart_round_time));
  out.set("game_start_time", Json::number(rules.game_start_time));
  out.set("warmup_period_start", Json::number(rules.warmup_period_start));
  out.set("time_until_next_phase", Json::number(rules.time_until_next_phase));
  return out;
}

auto to_json(const game::LevelSnapshot& level) -> Json {
  Json out = Json::object();
  out.set("up", Json::boolean(level.up));
  out.set("proxy", Json::string(hex_pointer(level.proxy)));
  out.set("rules_global", Json::boolean(level.rules != 0));
  out.set("identities", Json::integer(level.identities));
  out.set("local_connected", Json::integer(level.local_spawned));
  return out;
}

auto to_json(const game::Snapshot& snapshot) -> Json {
  Json out = Json::object();
  out.set("valid", Json::boolean(snapshot.valid));
  out.set("in_map", Json::boolean(snapshot.valid && snapshot.local.has_value()));
  // NOTE: there is deliberately no `in_game` here. The payload reports *facts*;
  // "is a level up?" is a policy the harness derives (`scripts/scenario/screens.py`
  // `_level_up`), because none of the raw facts is sufficient on its own — the
  // main menu has a readable `game_rules` global, a `C_CSGameRulesProxy` entity and
  // hundreds of entities (measured live 2026-10-15).
  out.set("local_pawn", Json::number(static_cast<double>(snapshot.local_pawn)));
  out.set("level", to_json(snapshot.level));
  out.set("rules", to_json(snapshot.rules));
  out.set("local", snapshot.local.has_value() ? player_to_json(*snapshot.local) : Json());
  Json players = Json::array();
  for (const game::PlayerSnapshot& player : snapshot.players) {
    players.push_back(player_to_json(player));
  }
  out.set("players", std::move(players));
  return out;
}

}  // namespace cs2::control
