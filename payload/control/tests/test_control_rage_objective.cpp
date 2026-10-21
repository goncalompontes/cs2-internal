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

#include "cs2/control/rage_objective.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
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
  expect(source.find("AimObjective::monte_carlo") != std::string::npos,
         "the service dispatches the Monte-Carlo objective");
  // The **inline** mapping must be gone: a second copy of the truth in the
  // service is a second truth.
  expect(source.find("name == \"damage\" ? 0") == std::string::npos,
         "the inline name -> value mapping is gone");
  expect(source.find("name == \"hitchance\"") == std::string::npos,
         "the inline name -> objective mapping is gone");
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

  // --- 8. the service uses the rules -----------------------------------------
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
