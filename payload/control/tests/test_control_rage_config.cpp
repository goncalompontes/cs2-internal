// Host tests for the per-weapon rage profile's line codec
// (`cs2/control/rage_config.hpp`).
//
// **Why the codec is exposed.** The profile's text format is a property of the
// file, not of the file system, so the codec is pure and a host test drives it:
// the round trip (save -> load) with the Monte-Carlo objective, and the refusal
// of an unknown objective byte, are testable without touching the payload's
// data directory (the file's own I/O is Windows-only and stays unchanged).
//
// The harness is the repo's convention: an `expect(bool, const char*)` plus a
// `failures` counter (the project keeps `b_ndebug=false`, so a test that only
// bites in the debug build is not a test — `meson.build`).

#include <cstdio>
#include <cstring>

#include "cs2/control/rage_config.hpp"
#include "cs2/control/rage_objective.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  const float difference = a > b ? a - b : b - a;
  return difference <= tolerance;
}

}  // namespace

auto main() -> int {
  using cs2::control::RageProfile;
  using cs2::control::OBJECTIVE_VALUE_DEFAULT;
  using cs2::control::OBJECTIVE_VALUE_EXPECTED;
  using cs2::control::OBJECTIVE_VALUE_HITCHANCE;
  using cs2::control::OBJECTIVE_VALUE_MONTE_CARLO;

  // --- 1. the round trip with the Monte-Carlo objective ---------------------
  {
    RageProfile profile{};
    std::snprintf(profile.weapon, sizeof(profile.weapon), "weapon_ak47");
    profile.objective = static_cast<std::uint8_t>(OBJECTIVE_VALUE_MONTE_CARLO);
    profile.hitbox = 1;
    profile.mode = 2;
    profile.key = 65;
    profile.fov = 12.5F;
    profile.min_damage = 40.0F;
    profile.min_hitchance = 0.35F;
    profile.autostop = 2;
    profile.autoscope = 1;
    profile.autoreload = 1;
    profile.only_visible = 1;
    profile.fire = 1;

    char line[256] = {};
    expect(cs2::control::rage_profile_format(profile, line, sizeof(line)),
           "the profile formats into a line");
    RageProfile loaded{};
    expect(cs2::control::rage_profile_parse(line, loaded), "the line parses back into a profile");
    // **The objective byte survives the round trip** — the value the operator
    // selected must still be there after a save and a load.
    expect(loaded.objective == OBJECTIVE_VALUE_MONTE_CARLO,
           "the Monte-Carlo objective survives the round trip");
    expect(std::strcmp(loaded.weapon, "weapon_ak47") == 0, "the weapon's name survives");
    expect(loaded.hitbox == 1 && loaded.mode == 2 && loaded.key == 65,
           "the hitbox, the mode and the key survive");
    expect(near(loaded.fov, 12.5F) && near(loaded.min_damage, 40.0F) &&
               near(loaded.min_hitchance, 0.35F),
           "the thresholds survive");
    expect(loaded.autostop == 2 && loaded.autoscope == 1 && loaded.autoreload == 1 &&
               loaded.only_visible == 1 && loaded.fire == 1,
           "the gates survive");
  }

  // --- 2. every objective value round-trips ---------------------------------
  {
    for (const int value : {0, 1, 2, 3}) {
      RageProfile profile{};
      std::snprintf(profile.weapon, sizeof(profile.weapon), "weapon_ak47");
      profile.objective = static_cast<std::uint8_t>(value);
      char line[256] = {};
      expect(cs2::control::rage_profile_format(profile, line, sizeof(line)),
             "the profile formats");
      RageProfile loaded{};
      expect(cs2::control::rage_profile_parse(line, loaded), "the line parses");
      expect(loaded.objective == value, "the objective value survives the round trip");
    }
  }

  // --- 3. a garbage objective byte loads as the documented safe default -----
  {
    // The value is the profile's **wire format**, so a file written by a newer
    // payload (or a corrupted one) must not select an objective the service does
    // not know: it falls back to the documented safe default.
    for (const char* garbage : {"weapon_ak47|200|0|0|0|0.00|0.00|0.000|0|0|0|0|0",
                                 "weapon_ak47|4|0|0|0|0.00|0.00|0.000|0|0|0|0|0",
                                 "weapon_ak47|255|0|0|0|0.00|0.00|0.000|0|0|0|0|0"}) {
      RageProfile loaded{};
      expect(cs2::control::rage_profile_parse(garbage, loaded), "the garbage line still parses");
      expect(loaded.objective == OBJECTIVE_VALUE_DEFAULT,
             "a garbage objective byte loads as the documented safe default");
      expect(loaded.objective != OBJECTIVE_VALUE_MONTE_CARLO,
             "a garbage objective byte never selects the Monte-Carlo objective");
    }
    // And the default is the hitchance — the value the control channel has
    // always used for a name it did not know.
    expect(OBJECTIVE_VALUE_DEFAULT == OBJECTIVE_VALUE_HITCHANCE,
           "the documented safe default is the hitchance");
    // The known values are **not** rewritten.
    RageProfile known{};
    expect(cs2::control::rage_profile_parse("weapon_ak47|3|0|0|0|0.00|0.00|0.000|0|0|0|0|0",
                                            known),
           "the Monte-Carlo line parses");
    expect(known.objective == OBJECTIVE_VALUE_MONTE_CARLO,
           "the Monte-Carlo objective is not rewritten");
    RageProfile expected{};
    expect(cs2::control::rage_profile_parse("weapon_ak47|2|0|0|0|0.00|0.00|0.000|0|0|0|0|0",
                                            expected),
           "the expected line parses");
    expect(expected.objective == OBJECTIVE_VALUE_EXPECTED,
           "the expected objective is not rewritten");
  }

  // --- 4. what is not a profile --------------------------------------------
  {
    RageProfile profile{};
    expect(!cs2::control::rage_profile_parse("# a comment", profile),
           "a comment is not a profile");
    expect(!cs2::control::rage_profile_parse("", profile), "an empty line is not a profile");
    expect(!cs2::control::rage_profile_parse("weapon_ak47|1", profile),
           "a truncated line is not a profile");
    expect(!cs2::control::rage_profile_parse(nullptr, profile),
           "a null line is not a profile");
    // A line with the four mandatory fields **is** a profile: the rest keeps the
    // defaults (a truncated write must not hand the caller stack garbage).
    expect(cs2::control::rage_profile_parse("weapon_ak47|1|0|0", profile),
           "the four mandatory fields are a profile");
    expect(profile.objective == OBJECTIVE_VALUE_HITCHANCE && profile.hitbox == 0 &&
               profile.mode == 0 && profile.key == 0 && near(profile.fov, 0.0F),
           "the fields after the fourth keep their defaults");
  }

  // --- 5. the weapon's name is normalised -----------------------------------
  {
    RageProfile profile{};
    expect(cs2::control::rage_profile_parse("ak47|1|0|0", profile), "a short name parses");
    expect(std::strcmp(profile.weapon, "weapon_ak47") == 0,
           "the weapon's name is normalised on load");
    expect(cs2::control::rage_profile_parse("WEAPON_AK47|1|0|0", profile),
           "an upper-case name parses");
    expect(std::strcmp(profile.weapon, "weapon_ak47") == 0,
           "the weapon's name is lower-cased on load");
  }

  if (failures == 0) {
    std::printf("control-rage-config: ok\n");
    return 0;
  }
  return 1;
}
