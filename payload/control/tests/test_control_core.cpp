// Unit tests for the control-channel core (ADR-0020): the JSON value, the
// `wait_for` predicate, snapshot serialization, the settings name table and
// the new commands' platform-independent parts (the `settings` command, the
// `batch` runner and the log tail). These are the platform-independent parts
// of `payload/control`, so they run host-side.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cs2/control/event_ring.hpp"
#include "cs2/control/json.hpp"
#include "cs2/control/predicate.hpp"
#include "cs2/control/settings_access.hpp"
#include "cs2/control/snapshot_json.hpp"
#include "cs2/overlay/settings_table.hpp"
#include "cs2/overlay/wire.hpp"

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
  local.on_ground = true;
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
  assert(Predicate::from_json(*Json::parse(R"({"on_ground":true})"))->matches(snapshot));
  assert(!Predicate::from_json(*Json::parse(R"({"on_ground":false})"))->matches(snapshot));
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
  local.on_ground = true;
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
  assert(player->find("on_ground")->as_bool());
  assert(player->find("origin")->find("z")->as_number() == 3.0);

  // An empty/unattached snapshot is still well-formed.
  const Json empty = cs2::control::to_json(Snapshot{});
  assert(empty.find("valid") != nullptr && !empty.find("valid")->as_bool());
  assert(empty.find("players")->items().empty());
}

void test_event_ring() {
  using cs2::control::EventArg;
  using cs2::control::EventArgMatch;
  using cs2::control::EventEntry;
  using cs2::control::EventRing;

  EventRing ring;
  assert(ring.count() == 0);
  assert(ring.push("round_start", nullptr, 0) == 1);

  EventArg args[2] = {};
  std::strcpy(args[0].key, "userid");
  std::strcpy(args[0].value, "7");
  std::strcpy(args[1].key, "headshot");
  std::strcpy(args[1].value, "true");
  assert(ring.push("player_death", args, 2) == 2);
  assert(ring.count() == 2);
  assert(ring.seen_since("player_death", 1));
  assert(!ring.seen_since("player_death", 2));  // strictly after `after`
  assert(!ring.seen_since("round_end", 0));

  EventEntry recent[4];
  const std::size_t captured = ring.recent(recent, 4);
  assert(captured == 2);
  assert(std::string(recent[0].name) == "round_start" && recent[0].seq == 1);
  assert(recent[0].arg_count == 0);
  assert(std::string(recent[1].name) == "player_death" && recent[1].seq == 2);
  assert(recent[1].arg_count == 2);
  assert(std::string(recent[1].args[0].key) == "userid");
  assert(std::string(recent[1].args[0].value) == "7");

  // Argument constraints.
  const EventArgMatch want_userid[] = {{.key = "userid", .value = "7"}};
  const EventArgMatch wrong_userid[] = {{.key = "userid", .value = "8"}};
  const EventArgMatch want_both[] = {{.key = "userid", .value = "7"},
                                     {.key = "headshot", .value = "true"}};
  const EventArgMatch missing[] = {{.key = "nope", .value = "1"}};
  assert(ring.seen_since("player_death", 1, want_userid, 1));
  assert(ring.seen_since("player_death", 1, want_both, 2));
  assert(!ring.seen_since("player_death", 1, wrong_userid, 1));
  assert(!ring.seen_since("player_death", 1, missing, 1));

  // Over capacity: the newest `k_capacity` survive, oldest first.
  for (int i = 0; i < 200; ++i) {
    ring.push("tick", nullptr, 0);
  }
  assert(ring.count() == 202);
  EventEntry all[EventRing::k_capacity];
  const std::size_t kept = ring.recent(all, EventRing::k_capacity);
  assert(kept == EventRing::k_capacity);
  assert(all[kept - 1].seq == 202);
  assert(all[0].seq == 202 - EventRing::k_capacity + 1);

  // Long names are truncated, never overflowed.
  ring.push(std::string(80, 'x'), nullptr, 0);
  EventEntry one[1];
  assert(ring.recent(one, 1) == 1);
  assert(std::string(one[0].name).size() == EventEntry::k_max_name_length);
}

// The fake owner of the settings block: the accessors the `settings` command
// reads and write. It stands in for the overlay menu (or the shared segment).
struct FakeSettings {
  cs2::overlay::wire::Settings block{};
  int writes = 0;
};

void fake_read(void* context, cs2::overlay::wire::Settings& out) {
  out = static_cast<const FakeSettings*>(context)->block;
}

void fake_write(void* context, const cs2::overlay::wire::Settings& in) {
  auto* fake = static_cast<FakeSettings*>(context);
  fake->block = in;
  fake->writes += 1;
}

// The fake dispatcher of the batch: it answers the commands it knows and fails
// the rest, so the batch's failure isolation can be observed.
struct FakeDispatcher {
  std::vector<std::string> ran;
};

auto fake_dispatch(void* context, const cs2::control::Json& step, cs2::control::Json& response)
    -> bool {
  auto* fake = static_cast<FakeDispatcher*>(context);
  const cs2::control::Json* cmd = step.find("cmd");
  const std::string name = cmd != nullptr ? std::string(cmd->as_string()) : std::string();
  fake->ran.push_back(name);
  if (name == "boom") {
    response.set("ok", cs2::control::Json::boolean(false));
    response.set("error", cs2::control::Json::string("boom failed"));
    return true;
  }
  response.set("ok", cs2::control::Json::boolean(true));
  cs2::control::Json result = cs2::control::Json::object();
  result.set("cmd", cs2::control::Json::string(name));
  response.set("result", std::move(result));
  return true;
}

// The settings name table must stay in step with the struct it describes: a
// gapless walk of the packed layout, one entry per field, the right offsets.
void test_settings_table_matches_the_struct() {
  using cs2::overlay::wire::find_setting;
  using cs2::overlay::wire::SETTING_TABLE;
  using cs2::overlay::wire::Settings;
  using cs2::overlay::wire::SettingType;

  // The struct is packed, so the table is a gapless walk of it: the first
  // field starts at 0, every field follows the previous one and the last one
  // ends exactly at the struct's size. Together with the unique names below
  // that proves the table covers every field exactly once.
  std::size_t offset = 0;
  for (const auto& info : SETTING_TABLE) {
    assert(info.offset == offset);
    assert(info.size > 0);
    assert(info.count > 0);
    assert(!info.name.empty());
    offset += info.size;
  }
  assert(offset == sizeof(Settings));

  // Every field is addressable by its flat name, and the names are unique.
  for (std::size_t i = 0; i < SETTING_TABLE.size(); ++i) {
    assert(find_setting(SETTING_TABLE[i].name) == &SETTING_TABLE[i]);
    for (std::size_t j = i + 1; j < SETTING_TABLE.size(); ++j) {
      assert(SETTING_TABLE[i].name != SETTING_TABLE[j].name);
    }
  }
  assert(find_setting("esp_bxo") == nullptr);
  assert(find_setting("") == nullptr);

  // The kinds: a toggle, a number, a counter, a colour and a text.
  assert(SETTING_TABLE[0].name == "box");
  assert(SETTING_TABLE[0].type == SettingType::flag);
  assert(SETTING_TABLE[0].offset == offsetof(Settings, box));
  const auto* colour = find_setting("esp_color_enemy");
  assert(colour != nullptr && colour->type == SettingType::colour);
  assert(colour->offset == offsetof(Settings, esp_color_enemy));
  assert(colour->size == 4 && colour->count == 4);
  const auto* distance = find_setting("movement_thirdperson_distance");
  assert(distance != nullptr && distance->type == SettingType::number);
  assert(distance->offset == offsetof(Settings, movement_thirdperson_distance));
  const auto* token = find_setting("rage_config_token");
  assert(token != nullptr && token->type == SettingType::unsigned_number);
  assert(token->offset == offsetof(Settings, rage_config_token));
  const auto* path = find_setting("menu_config_path");
  assert(path != nullptr && path->type == SettingType::text);
  assert(path->offset == offsetof(Settings, menu_config_path));
  assert(path->size == sizeof(Settings::menu_config_path));
  // The last field ends the struct.
  assert(path->offset + path->size == sizeof(Settings));

  // The bytes of a few known fields, read back out of a block: the offsets
  // really are the fields' offsets.
  Settings block{};
  block.box = 1;
  block.esp_color_enemy[0] = 255;
  block.movement_thirdperson_distance = 64.0F;
  block.rage_config_token = 7;
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&block);
  assert(bytes[SETTING_TABLE[0].offset] == 1);
  assert(bytes[colour->offset] == 255);
  float read_distance = 0.0F;
  std::memcpy(&read_distance, bytes + distance->offset, sizeof(read_distance));
  assert(read_distance == 64.0F);
  std::uint32_t read_token = 0;
  std::memcpy(&read_token, bytes + token->offset, sizeof(read_token));
  assert(read_token == 7);
}

// The `settings` command: the unknown key, the wrong type, the changed list.
void test_settings_command() {
  using cs2::control::Json;
  using cs2::control::run_settings;
  using cs2::control::SettingsAccess;
  using cs2::overlay::wire::SETTING_TABLE;

  FakeSettings fake;
  fake.block.box = 1;
  fake.block.esp_color_enemy[0] = 235;
  fake.block.movement_thirdperson_distance = 64.0F;
  SettingsAccess access;
  access.set(&fake_read, &fake_write, &fake);
  assert(access.available());

  // An unknown key is an error, not a silent no-op, and nothing is written.
  const auto unknown = run_settings(*Json::parse(R"({"set": {"esp_bxo": 1}})"), access);
  assert(!unknown.has_value());
  assert(unknown.error().message == "settings: unknown key \"esp_bxo\"");
  assert(fake.writes == 0);
  const auto unknown_get = run_settings(*Json::parse(R"({"get": ["esp_bxo"]})"), access);
  assert(!unknown_get.has_value());
  assert(unknown_get.error().message == "settings: unknown key \"esp_bxo\"");

  // A wrong value type is an error naming the key and the expected type.
  const auto wrong = run_settings(*Json::parse(R"({"set": {"box": "yes"}})"), access);
  assert(!wrong.has_value());
  assert(wrong.error().message.find("box") != std::string::npos);
  assert(wrong.error().message.find("flag") != std::string::npos);
  assert(fake.writes == 0);
  const auto wrong_colour =
      run_settings(*Json::parse(R"({"set": {"esp_color_enemy": [1, 2, 3]}})"), access);
  assert(!wrong_colour.has_value());
  assert(wrong_colour.error().message.find("esp_color_enemy") != std::string::npos);
  assert(wrong_colour.error().message.find("colour") != std::string::npos);
  // A typo in one key leaves the whole request unwritten.
  const auto mixed = run_settings(*Json::parse(R"({"set": {"box": 0, "bxo": 1}})"), access);
  assert(!mixed.has_value());
  assert(fake.writes == 0);
  assert(fake.block.box == 1);

  // A set to the value the field already has is not a change.
  const auto same = run_settings(*Json::parse(R"({"set": {"box": 1}})"), access);
  assert(same.has_value());
  assert(same->wrote);
  assert(same->changed.empty());
  assert(fake.writes == 1);

  // A real change is listed, and the owner holds the new value.
  const auto changed = run_settings(
      *Json::parse(R"({"set": {"box": 0, "esp_color_enemy": [255, 0, 0, 255]}})"), access);
  assert(changed.has_value());
  assert(changed->changed.size() == 2);
  assert(changed->changed[0] == "box");
  assert(changed->changed[1] == "esp_color_enemy");
  assert(fake.writes == 2);
  assert(fake.block.box == 0);
  assert(fake.block.esp_color_enemy[0] == 255);
  assert(fake.block.esp_color_enemy[1] == 0);
  // The reported values are the new ones.
  const Json* box = changed->values.find("box");
  assert(box != nullptr && box->as_int() == 0);
  const Json* colour = changed->values.find("esp_color_enemy");
  assert(colour != nullptr && colour->is_array());
  assert(colour->items().size() == 4);
  assert(colour->items()[0].as_int() == 255);

  // A get reports the live values.
  const auto got =
      run_settings(*Json::parse(R"({"get": ["box", "movement_thirdperson_distance"]})"), access);
  assert(got.has_value());
  assert(!got->wrote);
  assert(got->changed.empty());
  assert(got->values.find("box")->as_int() == 0);
  assert(got->values.find("movement_thirdperson_distance")->as_number() == 64.0);
  assert(got->values.find("esp_color_enemy") == nullptr);

  // A text field is truncated to the field's size and NUL-terminated.
  const auto text =
      run_settings(*Json::parse(R"({"set": {"menu_config_path": "C:/tmp/cs2.cfg"}})"), access);
  assert(text.has_value());
  assert(std::string(fake.block.menu_config_path) == "C:/tmp/cs2.cfg");

  // With neither a get nor a set, every field is reported.
  const auto all = run_settings(*Json::parse("{}"), access);
  assert(all.has_value());
  assert(all->values.members().size() == SETTING_TABLE.size());
  assert(all->values.find("esp_color_enemy") != nullptr);
  assert(all->values.find("menu_config_path") != nullptr);
  assert(all->values.find("menu_config_path")->as_string() == "C:/tmp/cs2.cfg");

  // A request that names nothing returns nothing: only a request with
  // neither a get nor a set returns every field.
  const auto empty_get = run_settings(*Json::parse(R"({"get": []})"), access);
  assert(empty_get.has_value());
  assert(empty_get->values.members().empty());
}

// The `batch` command: the failure isolation, the recursion guard and the
// bounds.
void test_batch_runner() {
  using cs2::control::BatchRunner;
  using cs2::control::Json;

  FakeDispatcher fake;
  Json results = Json::array();

  // The steps run in order; a failing step is recorded and the batch
  // continues.
  const auto ran = BatchRunner::run(
      *Json::parse(R"([{"cmd":"exec","args":{"line":"sv_cheats 1"}},{"cmd":"boom"},{"cmd":"ping"}])"), 1,
      &fake_dispatch, &fake, results);
  assert(ran.has_value());
  assert(fake.ran.size() == 3);
  assert(fake.ran[0] == "exec");
  assert(fake.ran[1] == "boom");
  assert(fake.ran[2] == "ping");
  assert(results.items().size() == 3);
  const Json& first = results.items()[0];
  assert(first.find("cmd")->as_string() == "exec");
  assert(first.find("ok")->as_bool());
  assert(first.find("result") != nullptr);
  const Json& second = results.items()[1];
  assert(second.find("cmd")->as_string() == "boom");
  assert(!second.find("ok")->as_bool());
  assert(second.find("error")->as_string() == "boom failed");
  const Json& third = results.items()[2];
  assert(third.find("cmd")->as_string() == "ping");
  assert(third.find("ok")->as_bool());

  // The recursion guard: a step must not be a batch.
  FakeDispatcher nested_fake;
  Json nested_results = Json::array();
  const auto nested = BatchRunner::run(*Json::parse(R"([{"cmd":"batch","args":{"steps":[]}}])"), 1,
                                       &fake_dispatch, &nested_fake, nested_results);
  assert(!nested.has_value());
  assert(nested.error().message.find("must not be a batch") != std::string::npos);
  assert(nested_fake.ran.empty());

  // The depth is bounded too.
  const auto deep = BatchRunner::run(*Json::parse("[]"), BatchRunner::k_max_depth + 1,
                                     &fake_dispatch, &fake, results);
  assert(!deep.has_value());

  // More steps than the limit is an error, not a truncation.
  Json many = Json::array();
  for (std::size_t i = 0; i <= BatchRunner::k_max_steps; ++i) {
    many.push_back(Json::object());
  }
  const auto too_many = BatchRunner::run(many, 1, &fake_dispatch, &fake, results);
  assert(!too_many.has_value());
  assert(too_many.error().message.find("too many steps") != std::string::npos);

  // A malformed batch fails as a whole, and no step runs.
  FakeDispatcher malformed_fake;
  Json malformed_results = Json::array();
  const auto not_array =
      BatchRunner::run(*Json::parse("{}"), 1, &fake_dispatch, &malformed_fake, malformed_results);
  assert(!not_array.has_value());
  const auto no_cmd = BatchRunner::run(*Json::parse(R"([{"args":{}}])"), 1, &fake_dispatch,
                                       &malformed_fake, malformed_results);
  assert(!no_cmd.has_value());
  assert(malformed_fake.ran.empty());
}

// The `log` command's tail: the last N lines of the payload's log file.
void test_log_tail() {
  using cs2::control::log_tail;

  const std::vector<std::string> lines = log_tail("one\ntwo\nthree\n", 2);
  assert(lines.size() == 2);
  assert(lines[0] == "two");
  assert(lines[1] == "three");

  const std::vector<std::string> crlf = log_tail("one\r\ntwo\r\n", 5);
  assert(crlf.size() == 2);
  assert(crlf[0] == "one");
  assert(crlf[1] == "two");

  assert(log_tail("", 5).empty());
  assert(log_tail("only", 5).size() == 1);
  assert(log_tail("only", 5)[0] == "only");
  assert(log_tail("a\nb\nc", 10).size() == 3);
  assert(log_tail("a\nb\nc", 10)[2] == "c");
  assert(log_tail("a\nb\nc", 0).empty());
}

}  // namespace

auto main() -> int {
  test_json_round_trip();
  test_json_arrays_and_types();
  test_json_rejects_malformed();
  test_predicate_matching();
  test_snapshot_serialization();
  test_event_ring();
  test_settings_table_matches_the_struct();
  test_settings_command();
  test_batch_runner();
  test_log_tail();
  std::puts("control-core: ok");
  return 0;
}
