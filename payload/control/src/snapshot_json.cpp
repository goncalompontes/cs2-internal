#include "cs2/control/snapshot_json.hpp"

#include <cstddef>

namespace cs2::control {
namespace {

[[nodiscard]] auto player_to_json(const game::PlayerSnapshot& player) -> Json {
  Json out = Json::object();
  out.set("index", Json::integer(player.index));
  out.set("name", Json::string(player.name));
  out.set("weapon", Json::string(player.weapon));
  out.set("team", Json::integer(player.team));
  out.set("health", Json::integer(player.health));
  out.set("max_health", Json::integer(player.max_health));
  out.set("armor", Json::integer(player.armor));
  out.set("life_state", Json::integer(player.life_state));
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

auto to_json(const game::Snapshot& snapshot) -> Json {
  Json out = Json::object();
  out.set("valid", Json::boolean(snapshot.valid));
  out.set("in_map", Json::boolean(snapshot.valid && snapshot.local.has_value()));
  out.set("local_pawn", Json::number(static_cast<double>(snapshot.local_pawn)));
  out.set("local", snapshot.local.has_value() ? player_to_json(*snapshot.local) : Json());
  Json players = Json::array();
  for (const game::PlayerSnapshot& player : snapshot.players) {
    players.push_back(player_to_json(player));
  }
  out.set("players", std::move(players));
  return out;
}

}  // namespace cs2::control
