// Host tests for the ragebot's settings **ownership rule**
// (`cs2/control/rage_settings.hpp`).
//
// **Why the rule lives in a header.** The overlay's settings sink
// (`payload/src/payload_main.cpp`) re-applied the **whole** rage group on every
// publication — the menu publishes its block every frame — so whatever the
// control channel had set was overwritten within a second. The control channel
// is the **only** way to reach the Monte-Carlo objective (the menu has no such
// option), so the feature T35 shipped was inert in practice.
//
// The rule — "did the operator change this field?" — is a **pure** comparison
// over two snapshots, so it is host-testable here. The repo's convention is an
// `expect(bool, const char*)` plus a `failures` counter (the project keeps
// `b_ndebug=false`, so a test that only bites in the debug build is not a
// test — `meson.build`).
//
// The last checks are **source** checks: the sink and the service need the
// game, so their wiring is checked by reading them (the repo's pattern —
// `payload/control/tests/test_control_rage_objective.cpp`).

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/control/rage_settings.hpp"
#include "cs2/overlay/wire.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// The wire's default block — the menu's starting point.
auto default_block() -> cs2::overlay::wire::Settings {
  return cs2::overlay::wire::default_settings();
}

// The snapshot of the default block.
auto default_snapshot() -> cs2::control::RageSettings {
  return cs2::control::rage_settings_from_wire(default_block());
}

// One rage field: the wire's name, the bit the rule reports for it, and how
// the block is changed so the field moves.
struct FieldCase {
  const char* wire_field;  // the wire's field name (the coverage check)
  std::uint32_t bit;
  void (*mutate)(cs2::overlay::wire::Settings&);
  const char* what;
};

// **Every** rage setting the wire owns. The coverage check below compares this
// table with the wire's own field list, so a rage field added to the block and
// forgotten here fails the test.
const FieldCase k_field_cases[] = {
    {"rage", cs2::control::RAGE_FIELD_ENABLED,
     [](cs2::overlay::wire::Settings& block) { block.rage = 1; },
     "the ragebot's switch is a field the rule covers"},
    {"rage_key", cs2::control::RAGE_FIELD_KEY,
     [](cs2::overlay::wire::Settings& block) { block.rage_key = 0x58; },
     "the trigger key is a field the rule covers"},
    {"rage_fire", cs2::control::RAGE_FIELD_FIRE,
     [](cs2::overlay::wire::Settings& block) { block.rage_fire = 1; },
     "the auto-fire switch is a field the rule covers"},
    {"rage_objective", cs2::control::RAGE_FIELD_OBJECTIVE,
     [](cs2::overlay::wire::Settings& block) { block.rage_objective = 3; },
     "the objective is a field the rule covers"},
    {"rage_hitbox", cs2::control::RAGE_FIELD_HITBOX,
     [](cs2::overlay::wire::Settings& block) { block.rage_hitbox = 2; },
     "the hitbox filter is a field the rule covers"},
    {"rage_fov", cs2::control::RAGE_FIELD_FOV,
     [](cs2::overlay::wire::Settings& block) { block.rage_fov = 90.0F; },
     "the target cone is a field the rule covers"},
    {"rage_min_damage", cs2::control::RAGE_FIELD_MIN_DAMAGE,
     [](cs2::overlay::wire::Settings& block) { block.rage_min_damage = 50.0F; },
     "the damage gate is a field the rule covers"},
    {"rage_min_hitchance", cs2::control::RAGE_FIELD_MIN_HITCHANCE,
     [](cs2::overlay::wire::Settings& block) { block.rage_min_hitchance = 0.5F; },
     "the hitchance gate is a field the rule covers"},
    {"rage_autostop", cs2::control::RAGE_FIELD_AUTOSTOP,
     [](cs2::overlay::wire::Settings& block) { block.rage_autostop = 2; },
     "the auto-stop mode is a field the rule covers"},
    {"rage_autoscope", cs2::control::RAGE_FIELD_AUTOSCOPE,
     [](cs2::overlay::wire::Settings& block) { block.rage_autoscope = 1; },
     "the auto-scope mode is a field the rule covers"},
    {"rage_autoreload", cs2::control::RAGE_FIELD_AUTORELOAD,
     [](cs2::overlay::wire::Settings& block) { block.rage_autoreload = 1; },
     "the auto-reload switch is a field the rule covers"},
    {"rage_only_visible", cs2::control::RAGE_FIELD_ONLY_VISIBLE,
     [](cs2::overlay::wire::Settings& block) { block.rage_only_visible = 1; },
     "the visible-only switch is a field the rule covers"},
    {"rage_mode", cs2::control::RAGE_FIELD_MODE,
     [](cs2::overlay::wire::Settings& block) { block.rage_mode = 2; },
     "the trigger's mode is a field the rule covers"},
    {"rage_per_weapon", cs2::control::RAGE_FIELD_PER_WEAPON,
     [](cs2::overlay::wire::Settings& block) { block.rage_per_weapon = 1; },
     "the per-weapon switch is a field the rule covers"},
    {"rage_weapon", cs2::control::RAGE_FIELD_WEAPON,
     [](cs2::overlay::wire::Settings& block) {
       const char* const name = "weapon_ak47";
       for (std::size_t index = 0; index + 1 < sizeof(block.rage_weapon); ++index) {
         block.rage_weapon[index] = name[index];
         if (name[index] == '\0') {
           break;
         }
       }
     },
     "the profile's weapon key is a field the rule covers"},
    {"rage_recoil", cs2::control::RAGE_FIELD_RECOIL,
     [](cs2::overlay::wire::Settings& block) { block.rage_recoil = 1; },
     "the recoil compensation is a field the rule covers"},
    {"rage_stop_speed", cs2::control::RAGE_FIELD_STOP_SPEED,
     [](cs2::overlay::wire::Settings& block) { block.rage_stop_speed = 120.0F; },
     "the auto-stop speed is a field the rule covers"},
    {"rage_backtrack", cs2::control::RAGE_FIELD_BACKTRACK,
     [](cs2::overlay::wire::Settings& block) { block.rage_backtrack = 1; },
     "the backtracking switch is a field the rule covers"},
};

// The wire's rage fields that are **not** settings.
//
// The one-shot op is a *request* ("save this profile"), not a setting: the
// service's own token makes each request fire exactly once, so it is
// delivered on every write rather than compared. A field left out of the
// comparison is a field that keeps clobbering the control channel — and the
// op cannot: the control channel has no op of its own in the block.
constexpr const char* k_excluded_wire_fields[] = {
    "rage_config_op",
    "rage_config_token",
};

// --- The source helpers -----------------------------------------------------

// The source with its **comments removed**, so a source check looks at the
// code and not at the prose around it.
auto strip_comments(const std::string& source) -> std::string {
  std::string out;
  out.reserve(source.size());
  bool in_line = false;
  bool in_block = false;
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char c = source[index];
    const char next = (index + 1 < source.size()) ? source[index + 1] : '\0';
    if (in_line) {
      if (c == '\n') {
        in_line = false;
        out.push_back(c);
      }
      continue;
    }
    if (in_block) {
      if (c == '*' && next == '/') {
        in_block = false;
        ++index;
      } else if (c == '\n') {
        out.push_back(c);
      }
      continue;
    }
    if (c == '/' && next == '/') {
      in_line = true;
      ++index;
      continue;
    }
    if (c == '/' && next == '*') {
      in_block = true;
      ++index;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

// The body of a function: from its definition to the closing brace at the
// start of a line.
auto function_body(const std::string& source, const std::string& marker) -> std::string {
  const std::size_t start = source.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t end = source.find("\n}\n", start);
  if (end == std::string::npos) {
    return {};
  }
  return source.substr(start, end - start);
}

// The wire's rage field names: the identifiers in the `Settings` struct that
// start with "rage".
//
// **The comments are stripped first**: the wire's own comments name the rage
// fields (`rage_fov`, `rage_objective`, …), and a comment is not a field.
auto wire_rage_fields(const std::string& wire) -> std::vector<std::string> {
  const std::string source = strip_comments(wire);
  const std::size_t start = source.find("struct Settings {");
  const std::size_t end = source.find("\n};", start);
  std::vector<std::string> names;
  if (start == std::string::npos || end == std::string::npos) {
    return names;
  }
  const std::string body = source.substr(start, end - start);
  std::size_t index = 0;
  while (index < body.size()) {
    // Find the next "rage" that starts an identifier.
    const std::size_t at = body.find("rage", index);
    if (at == std::string::npos) {
      break;
    }
    index = at + 4;
    const bool starts = at == 0 || (std::isalnum(static_cast<unsigned char>(body[at - 1])) == 0 &&
                                    body[at - 1] != '_');
    if (!starts) {
      continue;
    }
    std::string name = "rage";
    while (index < body.size() &&
           (std::isalnum(static_cast<unsigned char>(body[index])) != 0 || body[index] == '_')) {
      name.push_back(body[index]);
      ++index;
    }
    names.push_back(name);
  }
  return names;
}

// --- The checks -------------------------------------------------------------

// The comparison covers **every** rage field, and reports only the field that
// moved.
void test_the_comparison_covers_every_field() {
  for (const FieldCase& field : k_field_cases) {
    cs2::overlay::wire::Settings block = default_block();
    const auto before = cs2::control::rage_settings_from_wire(block);
    field.mutate(block);
    const auto after = cs2::control::rage_settings_from_wire(block);
    const std::uint32_t changed = cs2::control::rage_changed_fields(after, before);
    expect((changed & field.bit) != 0, field.what);
    expect(changed == field.bit, "the rule reports only the field that moved");
  }
}

// Nothing moved: the rule reports nothing, so the control channel's value for
// the field survives the menu's next frame.
void test_an_unchanged_block_reports_nothing() {
  const auto snapshot = default_snapshot();
  expect(cs2::control::rage_changed_fields(snapshot, snapshot) == 0,
         "an unchanged block reports nothing");
  // A copy of the same values is the same value: the comparison is by value,
  // not by identity.
  cs2::control::RageSettings copy = snapshot;
  expect(cs2::control::rage_changed_fields(copy, snapshot) == 0, "an equal copy reports nothing");
  // The float fields are compared exactly: a value that differs in the last
  // bit is a value the operator moved.
  cs2::control::RageSettings moved = snapshot;
  moved.fov = std::nextafter(snapshot.fov, 1.0F);
  expect((cs2::control::rage_changed_fields(moved, snapshot) & cs2::control::RAGE_FIELD_FOV) != 0,
         "a float that differs in the last bit is a change");
}

// A **NaN** in the block is not a change.
//
// The block is written by another process (the Vulkan layer's menu) and the
// repo plans for a corrupt shared segment, so a NaN can reach it. A plain
// `!=` says a NaN differs from itself: the field would be reported as changed
// on **every** publication, the menu's clamped value would be applied on
// every frame, and the control channel's value for that field would be
// clobbered every frame — the defect the rule exists to remove.
void test_a_nan_is_not_a_change() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (const auto& field : {&cs2::control::RageSettings::fov,
                             &cs2::control::RageSettings::min_damage,
                             &cs2::control::RageSettings::min_hitchance,
                             &cs2::control::RageSettings::stop_speed}) {
    cs2::control::RageSettings before = default_snapshot();
    before.*field = nan;
    const cs2::control::RageSettings after = before;
    expect(cs2::control::rage_changed_fields(after, before) == 0,
           "a NaN is not a change: the field is treated as unchanged");
    // A NaN against a real value **is** a change: the field moved off the NaN.
    cs2::control::RageSettings real = before;
    real.*field = 90.0F;
    expect(cs2::control::rage_changed_fields(real, before) != 0,
           "a NaN against a real value is a change");
  }
  // A signed zero is not a change either: `==` says `-0.0 == 0.0`, and a
  // bitwise comparison would report it on every frame.
  cs2::control::RageSettings negative_zero = default_snapshot();
  negative_zero.fov = -0.0F;
  cs2::control::RageSettings positive_zero = default_snapshot();
  positive_zero.fov = 0.0F;
  expect(cs2::control::rage_changed_fields(negative_zero, positive_zero) == 0,
         "a signed zero is not a change");
}

// The menu does **not** republish a recomputed value.
//
// The one widget that writes the block without an operator action is the
// combo's byte wrapper, which pulls an out-of-range byte back into the
// widget's range. The recompute is **idempotent**: it converges after one
// frame, so the rule does not degenerate into "always changed" — which would
// have left the control channel's value overwritten on every frame.
void test_the_menus_recompute_converges() {
  cs2::overlay::wire::Settings block = default_block();
  // A byte the menu's combo cannot show (a corrupt block, or a control client
  // that wrote one into the block).
  block.rage_objective = 7;
  const auto before = cs2::control::rage_settings_from_wire(block);
  // The menu's combo has three options, so it pulls the byte back to 0.
  block.rage_objective = 0;
  const auto after = cs2::control::rage_settings_from_wire(block);
  expect(
      (cs2::control::rage_changed_fields(after, before) & cs2::control::RAGE_FIELD_OBJECTIVE) != 0,
      "the menu's pull-back of an out-of-range byte is a change");
  expect(cs2::control::rage_changed_fields(after, after) == 0,
         "the pull-back converges: the next publication reports nothing");
  // An in-range byte is written back unchanged, so it is never a change.
  cs2::overlay::wire::Settings in_range = default_block();
  in_range.rage_objective = 2;
  const auto kept = cs2::control::rage_settings_from_wire(in_range);
  expect(cs2::control::rage_changed_fields(kept, kept) == 0,
         "the menu's write-back of an in-range byte is not a change");
}

// The weapon's key is compared as a **name**: the menu's text field writes the
// string and its NUL but leaves the bytes after the NUL stale, and a stale byte
// is not a change of the value.
void test_the_weapon_key_is_compared_as_a_name() {
  cs2::control::RageSettings before = default_snapshot();
  cs2::control::RageSettings after = before;
  after.weapon[0] = 'a';
  after.weapon[1] = 'k';
  after.weapon[2] = '\0';
  after.weapon[3] = 'X';  // a stale byte after the NUL
  expect((cs2::control::rage_changed_fields(after, before) & cs2::control::RAGE_FIELD_WEAPON) != 0,
         "a different weapon name is a change");
  cs2::control::RageSettings stale = before;
  stale.weapon[3] = 'X';
  expect((cs2::control::rage_changed_fields(stale, before) & cs2::control::RAGE_FIELD_WEAPON) == 0,
         "a stale byte after the NUL is not a change of the name");
}

// The rule covers every rage field the **wire** owns: a field left out of the
// comparison is a field that keeps clobbering the control channel.
void test_the_rule_covers_the_wires_rage_fields(const std::string& root) {
  const std::filesystem::path path =
      std::filesystem::path(root) / "overlay" / "include" / "cs2" / "overlay" / "wire.hpp";
  std::ifstream file(path);
  expect(file.good(), "the wire's source is readable");
  if (!file.good()) {
    return;
  }
  const std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  const std::vector<std::string> fields = wire_rage_fields(source);
  expect(!fields.empty(), "the wire's rage fields were found");
  for (const std::string& field : fields) {
    bool covered = false;
    for (const FieldCase& known : k_field_cases) {
      if (field == known.wire_field) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      for (const char* excluded : k_excluded_wire_fields) {
        if (field == excluded) {
          covered = true;
          break;
        }
      }
    }
    expect(covered, "every rage field the wire owns is either compared or excluded on purpose");
  }
  // The excluded ones are the one-shot op: they are a request, not a setting.
  expect(cs2::control::RAGE_FIELD_ALL ==
             (cs2::control::RAGE_FIELD_ENABLED | cs2::control::RAGE_FIELD_KEY |
              cs2::control::RAGE_FIELD_FIRE | cs2::control::RAGE_FIELD_OBJECTIVE |
              cs2::control::RAGE_FIELD_HITBOX | cs2::control::RAGE_FIELD_FOV |
              cs2::control::RAGE_FIELD_MIN_DAMAGE | cs2::control::RAGE_FIELD_MIN_HITCHANCE |
              cs2::control::RAGE_FIELD_AUTOSTOP | cs2::control::RAGE_FIELD_AUTOSCOPE |
              cs2::control::RAGE_FIELD_AUTORELOAD | cs2::control::RAGE_FIELD_ONLY_VISIBLE |
              cs2::control::RAGE_FIELD_MODE | cs2::control::RAGE_FIELD_PER_WEAPON |
              cs2::control::RAGE_FIELD_WEAPON | cs2::control::RAGE_FIELD_RECOIL |
              cs2::control::RAGE_FIELD_STOP_SPEED | cs2::control::RAGE_FIELD_BACKTRACK),
         "the field mask covers every field and nothing else");
  // Every field has a name, and every name is unique: the status lists them.
  std::uint32_t named = 0;
  for (const FieldCase& known : k_field_cases) {
    expect(cs2::control::rage_field_name(known.bit) != "",
           "every rage field has a name for the status");
    named |= known.bit;
  }
  expect(named == cs2::control::RAGE_FIELD_ALL, "the named fields are every field");
}

// The rule's **contract**, end to end: the control channel's value survives
// the menu's next frame, and the menu's value wins when the operator changes
// the field.
//
// This is the rule used the way the sink uses it (compare the published group
// against the last view, apply the fields that moved), so it pins the
// behaviour the spec asks for — not the wiring, which the source checks below
// cover.
void test_the_rule_keeps_the_control_channels_value() {
  // The control channel set the objective (the Monte-Carlo one — the menu
  // cannot select it).
  int in_force = 3;
  // The menu's last view of the block: the hitchance.
  const auto view = default_snapshot();
  // The menu publishes its block again, unchanged.
  const auto published = default_snapshot();
  const std::uint32_t unchanged = cs2::control::rage_changed_fields(published, view);
  if ((unchanged & cs2::control::RAGE_FIELD_OBJECTIVE) != 0) {
    in_force = published.objective;
  }
  expect(in_force == 3, "the control channel's objective survives the menu's next frame");
  // The operator changes **another** field in the menu.
  auto other = published;
  other.fov = 90.0F;
  const std::uint32_t elsewhere = cs2::control::rage_changed_fields(other, published);
  if ((elsewhere & cs2::control::RAGE_FIELD_OBJECTIVE) != 0) {
    in_force = other.objective;
  }
  expect(in_force == 3, "the control channel's objective survives a change to another field");
  // The operator changes the objective itself: the menu's value wins.
  auto edited = published;
  edited.objective = 0;
  const std::uint32_t moved = cs2::control::rage_changed_fields(edited, published);
  if ((moved & cs2::control::RAGE_FIELD_OBJECTIVE) != 0) {
    in_force = edited.objective;
  }
  expect(in_force == 0, "the menu's objective wins when the operator changes it");
  // **The rule's limit** (`docs/ragebot.md` §15.1): the operator picks the
  // objective the menu **already shows**. Dear ImGui's combo writes its index
  // only when the selection differs, so the block does not change, the mask
  // is empty, and the control channel's objective stays in force. This is the
  // documented behaviour, pinned here so it cannot drift silently.
  auto reselected = published;
  reselected.objective = published.objective;  // the same value, clicked again
  const std::uint32_t again = cs2::control::rage_changed_fields(reselected, published);
  if ((again & cs2::control::RAGE_FIELD_OBJECTIVE) != 0) {
    in_force = reselected.objective;
  }
  expect(in_force == 0, "re-selecting the objective the menu already shows changes nothing");
}

// The status says **which channel owns the objective**.
void test_the_status_names_the_owner() {
  expect(cs2::control::rage_owner_name(cs2::control::RAGE_FIELD_OBJECTIVE,
                                       cs2::control::RAGE_FIELD_OBJECTIVE) == "menu",
         "a set bit means the menu owns the field");
  expect(cs2::control::rage_owner_name(0, cs2::control::RAGE_FIELD_OBJECTIVE) == "control",
         "a clear bit means the control channel owns the field");
  expect(cs2::control::rage_menu_owns(cs2::control::RAGE_FIELD_FOV, cs2::control::RAGE_FIELD_FOV),
         "the mask answers the ownership question");
  expect(!cs2::control::rage_menu_owns(cs2::control::RAGE_FIELD_FOV,
                                       cs2::control::RAGE_FIELD_OBJECTIVE),
         "the ownership is per field");
}

// The status's **effective** owner: the owner of the objective **in force**.
//
// The scope is the point: `objective` is the effective objective (the
// per-weapon profile's when one is in force), so its owner has to be the
// effective one too — otherwise the two keys describe different objectives.
void test_the_status_names_the_effective_owner() {
  // No profile in force: the effective objective is the global one, so the
  // owner is the channel that owns it.
  expect(cs2::control::rage_effective_owner_name(0, 3, 3) == "control",
         "the control channel owns the objective in force");
  expect(cs2::control::rage_effective_owner_name(cs2::control::RAGE_FIELD_OBJECTIVE, 1, 1) ==
             "menu",
         "the menu owns the objective in force");
  // A profile's objective is in force: it comes from the profile file, so no
  // channel owns it.
  expect(cs2::control::rage_effective_owner_name(cs2::control::RAGE_FIELD_OBJECTIVE, 2, 3) ==
             "profile",
         "a profile's objective has no channel owner");
  // A profile that happens to carry the global objective's value is not a
  // profile's objective in force.
  expect(cs2::control::rage_effective_owner_name(0, 3, 3) != "profile",
         "the global objective in force is not a profile's");
}

// The **sink** consults the rule before it applies, and the copy it compares
// against is the one it just applied.
void test_the_sink_consults_the_rule(const std::string& root) {
  const std::filesystem::path path =
      std::filesystem::path(root) / "payload" / "src" / "payload_main.cpp";
  std::ifstream file(path);
  expect(file.good(), "the payload's entry point is readable");
  if (!file.good()) {
    return;
  }
  const std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  const std::string body = function_body(source, "void on_movement_settings(");
  expect(!body.empty(), "the settings sink was found");
  if (body.empty()) {
    return;
  }
  const std::string code = strip_comments(body);
  // The rule is consulted.
  expect(code.find("rage_changed_fields(") != std::string::npos,
         "the sink consults the rule before it applies");
  expect(code.find("rage_settings_from_wire(") != std::string::npos,
         "the sink takes the block's rage group as the rule's snapshot");
  // The **first** publication applies the whole group: there is no earlier
  // view to compare against.
  expect(code.find("RAGE_FIELD_ALL") != std::string::npos,
         "the first publication applies the whole group");
  // The write carries the mask, and only the masked fields are applied.
  const std::size_t write = code.find("set_rage_features(");
  expect(write != std::string::npos, "the sink writes the rage settings");
  if (write == std::string::npos) {
    return;
  }
  const std::size_t end = code.find(");", write);
  const std::string arguments = code.substr(write, end - write);
  expect(arguments.find("rage_changed") != std::string::npos,
         "the write carries the fields the operator changed");
  expect(arguments.find("RageSource::menu") != std::string::npos,
         "the sink's write says the menu wrote it");
  // The copy is updated **after** the apply, so the next publication compares
  // against what was actually applied.
  const std::size_t update = code.find("rage_menu_view = rage_now");
  expect(update != std::string::npos, "the sink updates the copy it compares against");
  expect(update > write, "the copy is updated after the apply");
}

// The **service** applies only the fields the write carries, and it records
// the owner.
void test_the_service_applies_only_the_masked_fields(const std::string& root) {
  const std::filesystem::path path =
      std::filesystem::path(root) / "payload" / "control" / "src" / "service.cpp";
  std::ifstream file(path);
  expect(file.good(), "the service's source is readable");
  if (!file.good()) {
    return;
  }
  const std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  const std::string body = function_body(source, "void Service::set_rage_features(");
  expect(!body.empty(), "the service's rage write was found");
  if (body.empty()) {
    return;
  }
  const std::string code = strip_comments(body);
  // Every field is masked: a field the write stores without a mask is a field
  // that keeps clobbering the control channel.
  //
  // **The check is anchored**: the bit has to be ANDed with the write's mask,
  // not merely to appear somewhere in the function. A bare mention of the bit
  // would satisfy a substring search and prove nothing.
  const char* const k_bits[] = {
      "RAGE_FIELD_ENABLED",    "RAGE_FIELD_KEY",           "RAGE_FIELD_FIRE",
      "RAGE_FIELD_OBJECTIVE",  "RAGE_FIELD_HITBOX",        "RAGE_FIELD_FOV",
      "RAGE_FIELD_MIN_DAMAGE", "RAGE_FIELD_MIN_HITCHANCE", "RAGE_FIELD_AUTOSTOP",
      "RAGE_FIELD_AUTOSCOPE",  "RAGE_FIELD_AUTORELOAD",    "RAGE_FIELD_ONLY_VISIBLE",
      "RAGE_FIELD_MODE",       "RAGE_FIELD_PER_WEAPON",    "RAGE_FIELD_WEAPON",
      "RAGE_FIELD_RECOIL",     "RAGE_FIELD_STOP_SPEED",    "RAGE_FIELD_BACKTRACK",
  };
  for (const char* bit : k_bits) {
    const std::string guard = "fields & " + std::string(bit);
    expect(code.find(guard) != std::string::npos, "the service masks every rage field");
  }
  // The ownership is recorded.
  expect(code.find("note_rage_owner(") != std::string::npos,
         "the service records the owner of the fields it wrote");
  // The menu's **displayed** objective is recorded, so the status can say what
  // the menu's tab shows.
  expect(code.find("rage_menu_objective_") != std::string::npos,
         "the service records the objective the menu's tab is showing");
  // The status publishes the owner.
  const std::string status = function_body(source, "void Service::do_rage(");
  expect(!status.empty(), "the rage command's status was found");
  if (!status.empty()) {
    const std::string status_code = strip_comments(status);
    // **The keys are matched as the quoted literals they are.** A substring
    // search would accept a key named `objective_source_disabled` — and the
    // status would then not say what the check claims it says.
    expect(status_code.find("\"objective_source\"") != std::string::npos,
           "the status says which channel owns the objective in force");
    expect(status_code.find("\"objective_global_source\"") != std::string::npos,
           "the status says which channel owns the global objective");
    expect(status_code.find("\"menu_objective\"") != std::string::npos,
           "the status says which objective the menu's tab is showing");
    expect(status_code.find("\"menu_fields\"") != std::string::npos,
           "the status says which channel owns each field");
  }
}

// The menu's **float** widgets do not write the block back: Dear ImGui's
// slider writes the value while it is being dragged, and the menu's helper
// does not touch it. So the rule never sees a recomputed float.
void test_the_menu_does_not_recompute_the_floats(const std::string& root) {
  const std::filesystem::path path =
      std::filesystem::path(root) / "overlay" / "include" / "cs2" / "overlay" / "menu_pages.hpp";
  std::ifstream file(path);
  expect(file.good(), "the menu's source is readable");
  if (!file.good()) {
    return;
  }
  const std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  const std::string body = function_body(source, "inline void slider_float(");
  expect(!body.empty(), "the menu's float slider was found");
  if (!body.empty()) {
    const std::string code = strip_comments(body);
    expect(code.find("value =") == std::string::npos,
           "the menu's float slider does not write the value back");
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  test_the_comparison_covers_every_field();
  test_an_unchanged_block_reports_nothing();
  test_a_nan_is_not_a_change();
  test_the_menus_recompute_converges();
  test_the_weapon_key_is_compared_as_a_name();
  test_the_rule_keeps_the_control_channels_value();
  test_the_status_names_the_owner();
  test_the_status_names_the_effective_owner();

  if (argc >= 2) {
    const std::string root(argv[1]);
    test_the_rule_covers_the_wires_rage_fields(root);
    test_the_sink_consults_the_rule(root);
    test_the_service_applies_only_the_masked_fields(root);
    test_the_menu_does_not_recompute_the_floats(root);
  } else {
    expect(false, "the repository root is an argument");
  }

  if (failures == 0) {
    std::printf("control-rage-settings: ok\n");
    return 0;
  }
  return 1;
}
