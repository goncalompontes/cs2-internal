// Host tests for the live rage objective's pure rules
// (`cs2/control/rage_objective.hpp`).
//
// **Why the rules live in a header.** The `rage` command maps an objective name
// to the per-weapon profile's byte, the `aim` command maps a name to the
// solver's `AimObjective`, and the status maps the byte back to a name. Those
// call sites live in `service.cpp`, which no host test can run (the Python
// scenario suite drives a fake pipe, not the service), so the rules are here and
// the service uses them.
//
// The harness is the repo's convention: an `expect(bool, const char*)` plus a
// `failures` counter (the project keeps `b_ndebug=false`, so a test that only
// bites in the debug build is not a test — `meson.build`).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/control/rage_objective.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// Whether `haystack` contains `needle`, ignoring the case.
//
// The disclosure's notes are prose, so the test asks for the **word** the
// operator has to read ("fallback", "indicative") rather than for the whole
// sentence: a reworded note is still a disclosure, and a note that lost the
// word is not.
auto contains(std::string_view haystack, std::string_view needle) -> bool {
  if (needle.empty() || needle.size() > haystack.size()) {
    return false;
  }
  const auto lower = [](char c) -> char {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  };
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (lower(haystack[i + j]) != lower(needle[j])) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

// The source with its **comments removed**, so a source check looks at the code
// and not at the prose around it.
//
// A comment that quotes the old inline mapping used to fail the check
// spuriously; the check is about the code.
auto strip_comments(const std::string& source) -> std::string {
  std::string out;
  out.reserve(source.size());
  bool in_line = false;
  bool in_block = false;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
    const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
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
        ++i;
      } else if (c == '\n') {
        out.push_back(c);
      }
      continue;
    }
    if (c == '/' && next == '/') {
      in_line = true;
      ++i;
      continue;
    }
    if (c == '/' && next == '*') {
      in_block = true;
      ++i;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

// The service's call sites must **use** the rules, not re-implement them: the
// mapping that is tested here has to be the mapping that runs, and a second
// copy inline in the service would be a second truth.
//
// This is a **source check** -- the repo's pattern for the call sites no host
// test can run (`payload/control/tests/test_control_anti_aim.cpp`): the service
// needs the game, so the wiring is checked by reading it.
void test_the_service_uses_the_rules(const std::string& root) {
  const std::filesystem::path path =
      std::filesystem::path(root) / "payload" / "control" / "src" / "service.cpp";
  std::ifstream file(path);
  expect(file.good(), "the service's source is readable");
  if (!file.good()) {
    return;
  }
  const std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  expect(source.find("objective_value_for(") != std::string::npos,
         "the rage command uses the name -> value rule");
  expect(source.find("objective_for_name(") != std::string::npos,
         "the aim command uses the name -> objective rule");
  expect(source.find("objective_name_for(") != std::string::npos,
         "the status uses the value -> name rule");
  expect(source.find("seed_source_is_fallback(") != std::string::npos,
         "the status uses the seed's fallback rule");
  expect(source.find("seed_source_note(") != std::string::npos,
         "the status uses the seed's note rule");
  expect(source.find("hitbox_target(") != std::string::npos,
         "the aim command takes the health list's index from the hitboxes");
  expect(source.find("health_entry_for(") != std::string::npos,
         "the aim command builds the health entry with the rule");
  expect(source.find("AimObjective::monte_carlo") != std::string::npos,
         "the service dispatches the Monte-Carlo objective");
  // The **inline** mapping must be gone: a second copy of the truth in the
  // service is a second truth.
  //
  // **The comments are stripped first** (`strip_comments`): a comment that
  // quotes the old mapping would otherwise fail the check spuriously, and the
  // check is about the *code*.
  const std::string code = strip_comments(source);
  expect(code.find("name == \"damage\" ? 0") == std::string::npos,
         "the inline name -> value mapping is gone");
  expect(code.find("name == \"hitchance\"") == std::string::npos,
         "the inline name -> objective mapping is gone");
  // The `aim` command must always read the weapon **from the game**: the
  // Monte-Carlo kernel is built from the spread, the inaccuracy and the type,
  // and the caller's profile carries none of them.
  expect(code.find("build_input(snapshot, weapon, target, !explicit_weapon)") == std::string::npos,
         "the aim command always reads the weapon from the game");
  expect(code.find("apply_stated_weapon(") != std::string::npos,
         "the aim command applies the caller's weapon with the rule");

  // The **reader** stamps the hitboxes with the target's index: the health
  // list and the hitboxes must speak about the same target, or the core's
  // can-kill key can never match.
  const std::filesystem::path reader_path =
      std::filesystem::path(root) / "payload" / "aim" / "src" / "reader.cpp";
  std::ifstream reader(reader_path);
  expect(reader.good(), "the reader's source is readable");
  if (reader.good()) {
    const std::string reader_source((std::istreambuf_iterator<char>(reader)),
                                    std::istreambuf_iterator<char>());
    expect(strip_comments(reader_source).find("hitbox.target = target->index") != std::string::npos,
           "the reader stamps the hitboxes with the target's index");
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  using cs2::aim::AimObjective;
  using cs2::aim::SpreadPattern;
  using cs2::control::OBJECTIVE_VALUE_DAMAGE;
  using cs2::control::OBJECTIVE_VALUE_DEFAULT;
  using cs2::control::OBJECTIVE_VALUE_EXPECTED;
  using cs2::control::OBJECTIVE_VALUE_HITCHANCE;
  using cs2::control::OBJECTIVE_VALUE_MONTE_CARLO;
  using cs2::control::SEED_SOURCE_ANGLE_GRID;
  using cs2::control::SEED_SOURCE_SEQUENCE;

  // --- 1. the name -> value mapping (the `rage` command's) ------------------
  {
    expect(cs2::control::objective_value_for("damage") == OBJECTIVE_VALUE_DAMAGE,
           "damage is the objective 0");
    expect(cs2::control::objective_value_for("hitchance") == OBJECTIVE_VALUE_HITCHANCE,
           "hitchance is the objective 1");
    expect(cs2::control::objective_value_for("expected") == OBJECTIVE_VALUE_EXPECTED,
           "expected is the objective 2");
    expect(cs2::control::objective_value_for("monte_carlo") == OBJECTIVE_VALUE_MONTE_CARLO,
           "monte_carlo is the objective 3");

    // An **unknown** name selects the documented safe default — and it is **not**
    // the new objective: a typo must never switch the solver silently.
    expect(OBJECTIVE_VALUE_DEFAULT == OBJECTIVE_VALUE_HITCHANCE,
           "the documented safe default is the hitchance");
    expect(cs2::control::objective_value_for("") == OBJECTIVE_VALUE_DEFAULT,
           "an empty name selects the default");
    expect(cs2::control::objective_value_for("nonsense") == OBJECTIVE_VALUE_DEFAULT,
           "an unknown name selects the default");
    expect(cs2::control::objective_value_for("Monte_Carlo") == OBJECTIVE_VALUE_DEFAULT,
           "a wrongly cased name selects the default");
    expect(cs2::control::objective_value_for("monte carlo") == OBJECTIVE_VALUE_DEFAULT,
           "a misspelled name selects the default");
    expect(cs2::control::objective_value_for("monte_carlo") != OBJECTIVE_VALUE_DEFAULT,
           "the new objective is not the default");
    // The name the profile has never carried stays out of the profile's
    // namespace: it maps to the default, exactly as it did before.
    expect(cs2::control::objective_value_for("pellets") == OBJECTIVE_VALUE_DEFAULT,
           "pellets is not a profile objective and maps to the default");
  }

  // --- 2. the value -> name mapping (the status) ----------------------------
  {
    expect(cs2::control::objective_name_for(OBJECTIVE_VALUE_DAMAGE) == "damage",
           "the status names the damage objective");
    expect(cs2::control::objective_name_for(OBJECTIVE_VALUE_HITCHANCE) == "hitchance",
           "the status names the hitchance objective");
    expect(cs2::control::objective_name_for(OBJECTIVE_VALUE_EXPECTED) == "expected",
           "the status names the expected objective");
    expect(cs2::control::objective_name_for(OBJECTIVE_VALUE_MONTE_CARLO) == "monte_carlo",
           "the status names the Monte-Carlo objective, not a number");
    // An unknown value reports the default's name: the operator never sees a
    // number where a name belongs.
    expect(cs2::control::objective_name_for(200) == "hitchance",
           "an unknown value reports the default's name");
    expect(cs2::control::objective_name_for(-1) == "hitchance",
           "a negative value reports the default's name");
  }

  // --- 3. the round trip ----------------------------------------------------
  {
    for (const int value : {OBJECTIVE_VALUE_DAMAGE, OBJECTIVE_VALUE_HITCHANCE,
                            OBJECTIVE_VALUE_EXPECTED, OBJECTIVE_VALUE_MONTE_CARLO}) {
      const std::string_view name = cs2::control::objective_name_for(value);
      expect(cs2::control::objective_value_for(name) == value,
             "the objective's name and value round-trip");
    }
  }

  // --- 4. the value's validity (the profile's loader) ----------------------
  {
    expect(cs2::control::objective_value_valid(OBJECTIVE_VALUE_DAMAGE),
           "the damage value is valid");
    expect(cs2::control::objective_value_valid(OBJECTIVE_VALUE_HITCHANCE),
           "the hitchance value is valid");
    expect(cs2::control::objective_value_valid(OBJECTIVE_VALUE_EXPECTED),
           "the expected value is valid");
    expect(cs2::control::objective_value_valid(OBJECTIVE_VALUE_MONTE_CARLO),
           "the Monte-Carlo value is valid");
    expect(!cs2::control::objective_value_valid(4), "an unknown value is not valid");
    expect(!cs2::control::objective_value_valid(200), "a garbage byte is not valid");
    expect(!cs2::control::objective_value_valid(-1), "a negative value is not valid");
  }

  // --- 5. the name -> enum mapping (the `aim` command's) --------------------
  {
    expect(cs2::control::objective_for_name("damage") == AimObjective::damage,
           "the aim command's damage objective");
    expect(cs2::control::objective_for_name("hitchance") == AimObjective::hitchance,
           "the aim command's hitchance objective");
    expect(cs2::control::objective_for_name("expected") == AimObjective::expected,
           "the aim command's expected objective");
    expect(cs2::control::objective_for_name("pellets") == AimObjective::pellets,
           "the aim command's pellets objective");
    expect(cs2::control::objective_for_name("monte_carlo") == AimObjective::monte_carlo,
           "the aim command's Monte-Carlo objective");
    // An unknown name selects the **damage** objective — the `aim` command's
    // documented default, unchanged.
    expect(cs2::control::objective_for_name("nonsense") == AimObjective::damage,
           "an unknown name selects the damage objective");
    expect(cs2::control::objective_for_name("") == AimObjective::damage,
           "an empty name selects the damage objective");
    // And the enum's own names.
    expect(cs2::control::objective_name_of(AimObjective::damage) == "damage",
           "the enum's damage name");
    expect(cs2::control::objective_name_of(AimObjective::hitchance) == "hitchance",
           "the enum's hitchance name");
    expect(cs2::control::objective_name_of(AimObjective::expected) == "expected",
           "the enum's expected name");
    expect(cs2::control::objective_name_of(AimObjective::pellets) == "pellets",
           "the enum's pellets name");
    expect(cs2::control::objective_name_of(AimObjective::monte_carlo) == "monte_carlo",
           "the enum's Monte-Carlo name");
  }

  // --- 6. the seed policy ---------------------------------------------------
  {
    expect(cs2::control::seed_source_for("sequence") == SEED_SOURCE_SEQUENCE,
           "the sequence policy is selected by its name");
    expect(cs2::control::seed_source_for("angle_grid") == SEED_SOURCE_ANGLE_GRID,
           "the angle_grid policy is selected by its name");
    expect(cs2::control::seed_source_for("nonsense") == SEED_SOURCE_SEQUENCE,
           "an unknown policy name selects the sequence policy");
    expect(cs2::control::seed_source_name(SEED_SOURCE_SEQUENCE) == "sequence",
           "the sequence policy is named");
    expect(cs2::control::seed_source_name(SEED_SOURCE_ANGLE_GRID) == "angle_grid",
           "the angle_grid policy is named");
    expect(cs2::control::seed_source_name(99) == "sequence",
           "an unknown policy value is named as the default");
    // The stored value is the core's own enum value, so the two cannot drift.
    expect(SEED_SOURCE_SEQUENCE == static_cast<int>(cs2::aim::SeedSource::sequence),
           "the stored sequence value is the core's");
    expect(SEED_SOURCE_ANGLE_GRID == static_cast<int>(cs2::aim::SeedSource::angle_grid),
           "the stored angle_grid value is the core's");

    // --- the disclosure -----------------------------------------------------
    //
    // **These two functions are the whole mechanism** by which the spec's
    // §2.3 ("the status must say the seed is a fallback") and §2.4 ("the
    // `angle_grid` number is indicative, not exact") are met. They had **no
    // test at all**: inverting `seed_source_is_fallback` and replacing
    // `seed_source_note`'s body with "the numbers are exact" left the whole
    // suite green, so the operator could be told the opposite of the truth —
    // that a stand-in number is the engine's own seed.
    expect(cs2::control::seed_source_is_fallback(SEED_SOURCE_SEQUENCE),
           "the sequence policy's seed is a fallback (the tick stands in)");
    expect(!cs2::control::seed_source_is_fallback(SEED_SOURCE_ANGLE_GRID),
           "the angle_grid policy uses no sequence at all, so it is no fallback");
    // An unknown policy value behaves like the default: the sequence policy,
    // whose seed **is** a fallback.
    expect(cs2::control::seed_source_is_fallback(99),
           "an unknown policy value is treated as the sequence policy");
    expect(contains(cs2::control::seed_source_note(SEED_SOURCE_SEQUENCE), "fallback"),
           "the sequence policy's note says the seed is a fallback");
    expect(contains(cs2::control::seed_source_note(SEED_SOURCE_SEQUENCE), "sequence"),
           "the sequence policy's note names what stands in");
    expect(contains(cs2::control::seed_source_note(SEED_SOURCE_ANGLE_GRID), "indicative"),
           "the angle_grid policy's note says the number is indicative");
    expect(contains(cs2::control::seed_source_note(SEED_SOURCE_ANGLE_GRID), "not exact"),
           "the angle_grid policy's note says the number is not exact");
    // The two notes must not be the same sentence: one discloses the fallback,
    // the other the stand-in hash.
    expect(cs2::control::seed_source_note(SEED_SOURCE_SEQUENCE) !=
               cs2::control::seed_source_note(SEED_SOURCE_ANGLE_GRID),
           "the two policies carry different notes");
  }

  // --- 7. the weapon's spread pattern ---------------------------------------
  {
    // The revolver's `1 - r^2` is keyed on the designer name, because the weapon
    // *type* cannot tell a revolver from a rifle (both are type 3).
    expect(cs2::control::spread_pattern_for("weapon_revolver") == SpreadPattern::revolver,
           "the R8's designer name selects the revolver's pattern");
    expect(cs2::control::spread_pattern_for("revolver") == SpreadPattern::revolver,
           "the short name selects the revolver's pattern");
    expect(cs2::control::spread_pattern_for("Weapon_Revolver") == SpreadPattern::revolver,
           "the designer name's case does not matter");
    expect(cs2::control::spread_pattern_for("weapon_ak47") == SpreadPattern::standard,
           "a rifle gets the standard pattern");
    expect(cs2::control::spread_pattern_for("weapon_glock") == SpreadPattern::standard,
           "a pistol gets the standard pattern");
    expect(cs2::control::spread_pattern_for("weapon_deagle") == SpreadPattern::standard,
           "the desert eagle is not the engine's revolver pattern");
    expect(cs2::control::spread_pattern_for("weapon_negev") == SpreadPattern::standard,
           "the negev is not the engine's revolver pattern");
    // A weapon the payload could not name: the standard pattern (the
    // conservative choice).
    expect(cs2::control::spread_pattern_for("") == SpreadPattern::standard,
           "an unnamed weapon gets the standard pattern");
    expect(cs2::control::spread_pattern_for("weapon_revolver_2") == SpreadPattern::standard,
           "only the exact name is the revolver");
    expect(cs2::control::spread_pattern_name(SpreadPattern::revolver) == "revolver",
           "the revolver pattern is named");
    expect(cs2::control::spread_pattern_name(SpreadPattern::standard) == "standard",
           "the standard pattern is named");
  }

  // --- 8. the aim command's weapon rule ---------------------------------------
  {
    // The caller's weapon wins on the **damage model**; the game wins on the
    // **kernel** (the spread, the inaccuracy and the type).
    //
    // **The bug this pins.** The `aim` command used to build the weapon from
    // the caller's profile alone when the caller stated any weapon argument,
    // so the spread and the inaccuracy stayed 0 and the type stayed unknown:
    // the Monte-Carlo kernel was degenerate (every sample landed exactly on
    // the aim, so the "measured probability" was 1.0) and a shotgun would have
    // been scored with the standard pattern.
    const cs2::aim::WeaponProfile from_game{.damage = 36.0F,
                                            .range_modifier = 0.99F,
                                            .penetration = 2.0F,
                                            .headshot_multiplier = 4.0F,
                                            .spread = 0.002F,
                                            .inaccuracy = 0.001F,
                                            .weapon_type = 3};
    const cs2::aim::WeaponProfile stated{.damage = 100.0F,
                                         .range_modifier = 0.98F,
                                         .penetration = 1.0F,
                                         .headshot_multiplier = 4.0F};
    const cs2::aim::WeaponProfile merged = cs2::control::apply_stated_weapon(from_game, stated);
    expect(merged.damage == stated.damage, "the stated damage wins");
    expect(merged.range_modifier == stated.range_modifier, "the stated range modifier wins");
    expect(merged.penetration == stated.penetration, "the stated penetration wins");
    expect(merged.headshot_multiplier == stated.headshot_multiplier,
           "the stated headshot multiplier wins");
    // **The kernel's terms are the game's** — the ones the Monte-Carlo kernel
    // is built from.
    expect(merged.spread == from_game.spread, "the spread is the game's");
    expect(merged.inaccuracy == from_game.inaccuracy, "the inaccuracy is the game's");
    expect(merged.weapon_type == from_game.weapon_type, "the weapon type is the game's");
  }

  // --- 9. the aim command's target rule ---------------------------------------
  {
    // The hitboxes carry the target's entity index, and the health entry
    // carries **the same** one: the core looks a target's health up under the
    // hitbox's own target index (`TargetHealth::index` against
    // `Hitbox::target`), so the two must agree.
    //
    // **The bug this pins.** The `aim` command used to hand the health list
    // the entity index while the hitboxes kept their default -1, so the
    // lookup never matched: `mc_target` was always -1 and `mc_can_kill`
    // always false — the rule's **first** key was dead.
    std::vector<cs2::aim::Hitbox> hitboxes;
    cs2::aim::Hitbox box;
    box.target = 7;
    hitboxes.push_back(box);
    expect(cs2::control::hitbox_target(hitboxes) == 7,
           "the hitboxes' target index is the target's entity index");
    const auto entry = cs2::control::health_entry_for(hitboxes, 42.0F);
    expect(entry.index == 7, "the health entry carries the hitboxes' index");
    expect(entry.health == 42.0F, "the health entry carries the health");
    // The health entry's index and the hitboxes' index are **the same
    // number** — that is the whole rule.
    expect(entry.index == cs2::control::hitbox_target(hitboxes),
           "the health entry and the hitboxes agree on the target");

    // No hitboxes: no target, so the health entry names no target and the
    // core's can-kill key stays false.
    const std::vector<cs2::aim::Hitbox> none;
    expect(cs2::control::hitbox_target(none) == -1, "no hitboxes means no target");
    const auto empty = cs2::control::health_entry_for(none, 42.0F);
    expect(empty.index < 0, "an unknown target's health entry names no target");
  }

  // --- 10. the sample count ----------------------------------------------------
  {
    // **64 is the default**: the only count that fits the frame budget at a
    // live-sized hitbox set (`docs/ragebot.md` §13.6).
    expect(cs2::control::MC_SAMPLES_DEFAULT == 64, "the sample count's default is 64");
    expect(cs2::control::mc_samples_in_range(1), "one sample is in range");
    expect(cs2::control::mc_samples_in_range(64), "64 samples are in range");
    expect(cs2::control::mc_samples_in_range(256), "256 samples are in range");
    expect(!cs2::control::mc_samples_in_range(0), "no samples is out of range");
    expect(!cs2::control::mc_samples_in_range(257), "257 samples are out of range");
    expect(!cs2::control::mc_samples_in_range(-1), "a negative count is out of range");
    // The note the status carries: a count above 64 does not fit the tick, and
    // a count outside the range is **ignored** (both entry points ignore it,
    // so the two behave the same way).
    expect(cs2::control::mc_samples_note(64, 64).empty(), "the default count needs no note");
    expect(contains(cs2::control::mc_samples_note(1000, 64), "ignored"),
           "an out-of-range request is said to be ignored");
    expect(contains(cs2::control::mc_samples_note(256, 256), "tick"),
           "a count of 256 is said not to fit the tick");
    expect(contains(cs2::control::mc_samples_note(128, 128), "tick"),
           "a count of 128 is said not to fit the tick");
  }

  // --- 11. the service uses the rules -----------------------------------------
  {
    expect(argc >= 2, "the repository root is an argument");
    if (argc >= 2) {
      test_the_service_uses_the_rules(argv[1]);
    }
  }

  if (failures == 0) {
    std::printf("control-rage-objective: ok\n");
    return 0;
  }
  return 1;
}
