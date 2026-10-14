// Unit tests for the control-channel core (ADR-0020): the JSON value, the
// `wait_for` predicate, and snapshot serialization. These are the parts of
// `payload/control` that are platform-independent, so they run host-side.

#include <cstdio>
#include <string>

#include "cs2/control/json.hpp"
#include "cs2/control/predicate.hpp"
#include "cs2/control/snapshot_json.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

void test_json_round_trip() {
  using cs2::control::Json;

  const auto parsed = Json::parse(R"({"id":1,"cmd":"exec","args":{"line":"sv_cheats 1"}})");
  expect(parsed.has_value(), "object parses");
  expect(parsed->is_object(), "is object");
  const Json* cmd = parsed->find("cmd");
  expect(cmd != nullptr && cmd->as_string() == "exec", "cmd is exec");
  const Json* args = parsed->find("args");
  expect(args != nullptr && args->find("line") != nullptr, "nested args.line");
  expect(parsed->dump() == R"({"id":1,"cmd":"exec","args":{"line":"sv_cheats 1"}})",
         "compact round-trip is stable");
}

void test_json_arrays_and_types() {
  using cs2::control::Json;

  const auto parsed = Json::parse(R"([1,2.5,true,false,null,"a\"b\n\u0041"])");
  expect(parsed.has_value() && parsed->is_array(), "array parses");
  expect(parsed->items().size() == 6, "array length");
  expect(parsed->items()[0].as_int(-1) == 1, "integer");
  expect(parsed->items()[1].as_number() == 2.5, "float");
  expect(parsed->items()[2].as_bool(false), "true");
  expect(!parsed->items()[3].as_bool(true), "false");
  expect(parsed->items()[4].is_null(), "null");
  expect(parsed->items()[5].as_string() == "a\"b\nA", "escapes decoded");
}

void test_json_rejects_malformed() {
  using cs2::control::Json;

  expect(!Json::parse("").has_value(), "empty rejected");
  expect(!Json::parse("{").has_value(), "unterminated object rejected");
  expect(!Json::parse("{\"a\":1,}").has_value(), "trailing comma rejected");
  expect(!Json::parse("01").has_value(), "leading zero number rejected");
  expect(!Json::parse("{} extra").has_value(), "trailing bytes rejected");
  expect(!Json::parse("\"\\uD800\"").has_value(), "lone surrogate rejected");
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

  const auto local_alive = Predicate::from_json(*Json::parse(R"({"local_alive":true})"));
  expect(local_alive.has_value() && local_alive->matches(snapshot), "local_alive true");

  const auto players = Predicate::from_json(*Json::parse(R"({"players_gte":2})"));
  expect(players.has_value() && players->matches(snapshot), "players_gte 2");
  expect(!Predicate::from_json(*Json::parse(R"({"players_gte":3})"))->matches(snapshot),
         "players_gte 3 fails");

  const auto alive = Predicate::from_json(*Json::parse(R"({"alive_gte":1})"));
  expect(alive.has_value() && alive->matches(snapshot), "alive_gte 1");

  const auto team = Predicate::from_json(*Json::parse(R"({"team":"T"})"));
  expect(team.has_value() && team->matches(snapshot), "team T");

  const auto compound = Predicate::from_json(
      *Json::parse(R"({"all":[{"in_map":true},{"team":"T"},{"local_hp_gte":50}]})"));
  expect(compound.has_value() && compound->matches(snapshot), "all[] matches");

  const auto negated =
      Predicate::from_json(*Json::parse(R"({"not":{"local_alive":false}})"));
  expect(negated.has_value() && negated->matches(snapshot), "not{} matches");

  expect(!Predicate::from_json(*Json::parse(R"({"unknown_key":1})")).has_value(),
         "unknown key rejected");
  expect(!Predicate::from_json(*Json::parse(R"({})")).has_value(), "empty object rejected");
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
  expect(json.find("valid") != nullptr && json.find("valid")->as_bool(), "snapshot valid");
  expect(json.find("in_map") != nullptr && json.find("in_map")->as_bool(), "in_map true");
  const Json* players = json.find("players");
  expect(players != nullptr && players->is_array() && players->items().size() == 1,
         "one player serialized");
  const Json* player = &players->items()[0];
  expect(player->find("health")->as_int() == 42, "player health");
  expect(player->find("team")->as_int() == 3, "player team");
  expect(player->find("name")->as_string() == "me", "player name");
  expect(player->find("origin")->find("z")->as_number() == 3.0, "player origin z");

  // An empty/unattached snapshot is still well-formed.
  const Json empty = cs2::control::to_json(Snapshot{});
  expect(empty.find("valid") != nullptr && !empty.find("valid")->as_bool(),
         "default snapshot invalid");
  expect(empty.find("players")->items().empty(), "default snapshot has no players");
}

}  // namespace

auto main() -> int {
  test_json_round_trip();
  test_json_arrays_and_types();
  test_json_rejects_malformed();
  test_predicate_matching();
  test_snapshot_serialization();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("control-core: all checks passed\n");
  return 0;
}
