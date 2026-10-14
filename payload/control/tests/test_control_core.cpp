// Unit tests for the control-channel core (ADR-0020): the JSON value, the
// `wait_for` predicate, and snapshot serialization. These are the
// platform-independent parts of `payload/control`, so they run host-side.

#include <cassert>
#include <cstdio>
#include <string>

#include "cs2/control/json.hpp"
#include "cs2/control/predicate.hpp"
#include "cs2/control/snapshot_json.hpp"

namespace {

void test_json_round_trip() {
  using cs2::control::Json;

  const auto parsed = Json::parse(R"({"id":1,"cmd":"exec","args":{"line":"sv_cheats 1"}})");
  assert(parsed.has_value());
  assert(parsed->is_object());
  const Json* cmd = parsed->find("cmd");
  assert(cmd != nullptr && cmd->as_string() == "exec");
  const Json* args = parsed->find("args");
  assert(args != nullptr && args->find("line") != nullptr);
  assert(parsed->dump() == R"({"id":1,"cmd":"exec","args":{"line":"sv_cheats 1"}})");
}

void test_json_arrays_and_types() {
  using cs2::control::Json;

  const auto parsed = Json::parse(R"([1,2.5,true,false,null,"a\"b\n\u0041"])");
  assert(parsed.has_value() && parsed->is_array());
  assert(parsed->items().size() == 6);
  assert(parsed->items()[0].as_int(-1) == 1);
  assert(parsed->items()[1].as_number() == 2.5);
  assert(parsed->items()[2].as_bool(false));
  assert(!parsed->items()[3].as_bool(true));
  assert(parsed->items()[4].is_null());
  assert(parsed->items()[5].as_string() == "a\"b\nA");
}

void test_json_rejects_malformed() {
  using cs2::control::Json;

  assert(!Json::parse("").has_value());
  assert(!Json::parse("{").has_value());
  assert(!Json::parse("{\"a\":1,}").has_value());
  assert(!Json::parse("01").has_value());  // leading zero
  assert(!Json::parse("{} extra").has_value());
  assert(!Json::parse("\"\\uD800\"").has_value());  // lone surrogate
}

void test_predicate_matching() {
  using cs2::control::Json;
  using cs2::control::Predicate;
  using cs2::game::PlayerSnapshot;
  using cs2::game::Snapshot;

  Snapshot snapshot;
  snapshot.valid = true;
  PlayerSnapshot local;
  local.is_local = true;
  local.alive = true;
  local.health = 100;
  local.team = 2;
  snapshot.local = local;
  snapshot.local_pawn = 0x1000;
  snapshot.players = {local};
  PlayerSnapshot bot;
  bot.alive = false;
  bot.team = 3;
  snapshot.players.push_back(bot);

  assert(Predicate::from_json(*Json::parse(R"({"local_alive":true})"))->matches(snapshot));
  assert(Predicate::from_json(*Json::parse(R"({"players_gte":2})"))->matches(snapshot));
  assert(!Predicate::from_json(*Json::parse(R"({"players_gte":3})"))->matches(snapshot));
  assert(Predicate::from_json(*Json::parse(R"({"alive_gte":1})"))->matches(snapshot));
  assert(Predicate::from_json(*Json::parse(R"({"team":"T"})"))->matches(snapshot));
  assert(Predicate::from_json(
             *Json::parse(R"({"all":[{"in_map":true},{"team":"T"},{"local_hp_gte":50}]})"))
             ->matches(snapshot));
  assert(Predicate::from_json(*Json::parse(R"({"not":{"local_alive":false}})"))->matches(snapshot));

  assert(!Predicate::from_json(*Json::parse(R"({"unknown_key":1})")).has_value());
  assert(!Predicate::from_json(*Json::parse(R"({})")).has_value());
}

void test_snapshot_serialization() {
  using cs2::control::Json;
  using cs2::game::PlayerSnapshot;
  using cs2::game::Snapshot;

  Snapshot snapshot;
  snapshot.valid = true;
  PlayerSnapshot local;
  local.index = 3;
  local.is_local = true;
  local.alive = true;
  local.health = 42;
  local.team = 3;
  local.name = "me";
  local.origin = {1.0F, 2.0F, 3.0F};
  snapshot.local = local;
  snapshot.players = {local};

  const Json json = cs2::control::to_json(snapshot);
  assert(json.find("valid") != nullptr && json.find("valid")->as_bool());
  assert(json.find("in_map") != nullptr && json.find("in_map")->as_bool());
  const Json* players = json.find("players");
  assert(players != nullptr && players->items().size() == 1);
  const Json* player = &players->items()[0];
  assert(player->find("health")->as_int() == 42);
  assert(player->find("team")->as_int() == 3);
  assert(player->find("name")->as_string() == "me");
  assert(player->find("origin")->find("z")->as_number() == 3.0);

  // An empty/unattached snapshot is still well-formed.
  const Json empty = cs2::control::to_json(Snapshot{});
  assert(empty.find("valid") != nullptr && !empty.find("valid")->as_bool());
  assert(empty.find("players")->items().empty());
}

}  // namespace

auto main() -> int {
  test_json_round_trip();
  test_json_arrays_and_types();
  test_json_rejects_malformed();
  test_predicate_matching();
  test_snapshot_serialization();
  std::puts("control-core: ok");
  return 0;
}
