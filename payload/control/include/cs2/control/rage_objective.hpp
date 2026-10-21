#pragma once

// The live rage objective's **pure rules**: the objective's names, its values,
// the seed policy's names, and the weapon's spread pattern.
//
// **Why a header of its own.** The control channel's `rage` command maps an
// objective *name* to the per-weapon profile's *byte*, the `aim` command maps a
// name to the solver's `AimObjective`, and the `rage` command's status maps the
// byte back to a name. Those mappings used to live inline in `service.cpp`,
// where no host test can reach them: the Python scenario suite drives a fake
// pipe and never runs the service, so a test written there would be testing the
// fake pipe. So the rules live here — **pure**, host-testable — and the service
// *uses* them (`payload/control/src/service.cpp`) instead of re-implementing the
// mapping at each call site.
//
// The precedent is the chams' target rules (`cs2/features/hook_target.hpp`),
// which are tested by `payload/features/tests/test_chams_target.cpp`.
//
// **Two namespaces, and they are not the same.** The *profile byte* is the
// persisted value (`cs2-rage.cfg`), so its numbers may not be renumbered; the
// *enum* is the solver's `AimObjective`. The `pellets` objective exists only in
// the enum: the profile has never carried it, and the `rage` command has always
// mapped a name it did not know to the hitchance.

#include <cstdint>
#include <string_view>

#include "cs2/aim/solver.hpp"

namespace cs2::control {

// --- The objective's values (the per-weapon profile's byte) -------------------

// The profile's objective byte. **These numbers are the profile's wire format**
// — they are written to `cs2-rage.cfg` and read back — so they may not be
// renumbered.
inline constexpr int OBJECTIVE_VALUE_DAMAGE = 0;
inline constexpr int OBJECTIVE_VALUE_HITCHANCE = 1;
inline constexpr int OBJECTIVE_VALUE_EXPECTED = 2;
inline constexpr int OBJECTIVE_VALUE_MONTE_CARLO = 3;

// The objective an **unknown** name selects: the hitchance.
//
// **It is the value the control channel has always used** for a name it did not
// know (`name == "damage" ? 0 : (name == "expected" ? 2 : 1)`), so an unknown
// name keeps behaving exactly as it did. It is deliberately **not** the
// Monte-Carlo objective: a typo must never silently switch the solver, and it
// must never silently switch it to the new one either.
inline constexpr int OBJECTIVE_VALUE_DEFAULT = OBJECTIVE_VALUE_HITCHANCE;

// The name → value mapping (the `rage` command's).
//
// An unknown name selects `OBJECTIVE_VALUE_DEFAULT`.
[[nodiscard]] constexpr auto objective_value_for(std::string_view name) noexcept -> int {
  if (name == "damage") {
    return OBJECTIVE_VALUE_DAMAGE;
  }
  if (name == "expected") {
    return OBJECTIVE_VALUE_EXPECTED;
  }
  if (name == "monte_carlo") {
    return OBJECTIVE_VALUE_MONTE_CARLO;
  }
  // "hitchance" and every unknown name: the default.
  return OBJECTIVE_VALUE_DEFAULT;
}

// The value → name mapping (the `rage` command's status).
//
// An unknown value reports the **default's** name, so the operator never sees a
// number where a name belongs. The profile's loader normalises an unknown byte
// to the default anyway (`rage_config.hpp`), so the two agree.
[[nodiscard]] constexpr auto objective_name_for(int value) noexcept -> std::string_view {
  switch (value) {
    case OBJECTIVE_VALUE_DAMAGE:
      return "damage";
    case OBJECTIVE_VALUE_EXPECTED:
      return "expected";
    case OBJECTIVE_VALUE_MONTE_CARLO:
      return "monte_carlo";
    case OBJECTIVE_VALUE_HITCHANCE:
    default:
      return "hitchance";
  }
}

// Whether the value is one of the four objectives.
//
// The profile's loader refuses the others: an **unknown** objective byte in a
// config file falls back to the documented safe default, never to an arbitrary
// objective.
[[nodiscard]] constexpr auto objective_value_valid(int value) noexcept -> bool {
  return value == OBJECTIVE_VALUE_DAMAGE || value == OBJECTIVE_VALUE_HITCHANCE ||
         value == OBJECTIVE_VALUE_EXPECTED || value == OBJECTIVE_VALUE_MONTE_CARLO;
}

// --- The objective's enum (the `aim` command's) ------------------------------

// The name → `AimObjective` mapping (the `aim` command's).
//
// An unknown name selects the **damage** objective — the `aim` command's
// documented default, unchanged.
[[nodiscard]] constexpr auto objective_for_name(std::string_view name) noexcept
    -> cs2::aim::AimObjective {
  if (name == "hitchance") {
    return cs2::aim::AimObjective::hitchance;
  }
  if (name == "expected") {
    return cs2::aim::AimObjective::expected;
  }
  if (name == "pellets") {
    return cs2::aim::AimObjective::pellets;
  }
  if (name == "monte_carlo") {
    return cs2::aim::AimObjective::monte_carlo;
  }
  return cs2::aim::AimObjective::damage;
}

// The `AimObjective` → name mapping (the `aim` command's report).
[[nodiscard]] constexpr auto objective_name_of(cs2::aim::AimObjective objective) noexcept
    -> std::string_view {
  switch (objective) {
    case cs2::aim::AimObjective::hitchance:
      return "hitchance";
    case cs2::aim::AimObjective::expected:
      return "expected";
    case cs2::aim::AimObjective::pellets:
      return "pellets";
    case cs2::aim::AimObjective::monte_carlo:
      return "monte_carlo";
    case cs2::aim::AimObjective::damage:
    default:
      return "damage";
  }
}

// --- The seed policy ----------------------------------------------------------

// The seed policies (`cs2/aim/montecarlo.hpp`). They are stored as the enum's
// own values, so the control channel's byte and the core's enum cannot drift.
inline constexpr int SEED_SOURCE_SEQUENCE = static_cast<int>(cs2::aim::SeedSource::sequence);
inline constexpr int SEED_SOURCE_ANGLE_GRID = static_cast<int>(cs2::aim::SeedSource::angle_grid);

// The seed policy an **unknown** name selects: the `sequence` policy — the
// repository's own RE (`MD5_PseudoRandom(sequence)`), and the core's default.
inline constexpr int SEED_SOURCE_DEFAULT = SEED_SOURCE_SEQUENCE;

// The name → policy mapping (`"sequence"` / `"angle_grid"`).
[[nodiscard]] constexpr auto seed_source_for(std::string_view name) noexcept -> int {
  if (name == "angle_grid") {
    return SEED_SOURCE_ANGLE_GRID;
  }
  return SEED_SOURCE_DEFAULT;
}

// The policy → name mapping (the status).
[[nodiscard]] constexpr auto seed_source_name(int value) noexcept -> std::string_view {
  return value == SEED_SOURCE_ANGLE_GRID ? "angle_grid" : "sequence";
}

// Whether the seed's **sequence** is a fallback.
//
// Under the `sequence` policy the samples' seeds are derived from the command's
// sequence number -- the thing `MD5_PseudoRandom(sequence)` is derived from
// (`docs/re/aim.md` §"The seed"). **The live path cannot reach it**: the
// `Input` module owns the `CUserCmd` pointer and exposes no accessor for the
// command's number, and the one that exists is refreshed by the bunnyhop assist
// alone (so it is zero when the assist is off and stale when it is on). The
// prediction tick stands in for it, and the status says so.
//
// Under the `angle_grid` policy the sequence is not used at all -- the seed is
// a function of the aim and the tick -- so the question does not arise.
[[nodiscard]] constexpr auto seed_source_is_fallback(int value) noexcept -> bool {
  return value != SEED_SOURCE_ANGLE_GRID;
}

// The note the status carries with the seed policy: what the number means and
// whether it is exact.
[[nodiscard]] constexpr auto seed_source_note(int value) noexcept -> std::string_view {
  if (value == SEED_SOURCE_ANGLE_GRID) {
    return "indicative, not exact: the hash is a stand-in for the engine's get_tick_view_angles";
  }
  return "fallback: the prediction tick stands in for the command's sequence number "
         "(it is not reachable from the service)";
}

// --- The weapon's spread pattern ----------------------------------------------

// Whether two names are the same, ignoring the case.
[[nodiscard]] constexpr auto same_name(std::string_view a, std::string_view b) noexcept -> bool {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    char left = a[i];
    char right = b[i];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

// The weapon's short name: the designer name without its `weapon_` prefix.
//
// The prefix is matched **ignoring the case**: the game's designer names are
// lower-case, but the rule must not depend on that.
[[nodiscard]] constexpr auto weapon_short_name(std::string_view weapon) noexcept
    -> std::string_view {
  constexpr std::string_view k_prefix = "weapon_";
  if (weapon.size() > k_prefix.size() &&
      same_name(weapon.substr(0, k_prefix.size()), k_prefix)) {
    return weapon.substr(k_prefix.size());
  }
  return weapon;
}

// The weapon's **spread pattern**, decided from its designer name.
//
// **The engine's rule is per weapon, not per type.** The engine's spread kernel
// transforms the radius `r -> 1 - r^2` for the weapons whose VData carries
// `attackType == 1` (`docs/re/aim.md` §"The spread cone"); in CS2 that is the R8
// Revolver, whose designer name is `weapon_revolver`. `WeaponProfile` carries
// the weapon *type* only, and a revolver is type 3 like every rifle, so the
// pattern cannot be inferred from it — and the core has **no default** for it
// (`spread_kernel`'s pattern argument is required), because a default would drop
// the revolver's transform without a word.
//
// The rule is an **exact match** on the weapon's short name:
//
//   * `revolver` -> the revolver's `1 - r^2`;
//   * every other name -> the standard pattern.
//
// **A weapon the payload could not name** (an empty designer name) gets the
// standard pattern as well: it is the pattern every weapon except the revolver
// uses, so it is the conservative choice. The name is what the rule is keyed on
// and the caller reports it, so an operator can always see which weapon was
// classified.
[[nodiscard]] constexpr auto spread_pattern_for(std::string_view weapon) noexcept
    -> cs2::aim::SpreadPattern {
  return same_name(weapon_short_name(weapon), "revolver") ? cs2::aim::SpreadPattern::revolver
                                                          : cs2::aim::SpreadPattern::standard;
}

// The pattern's name (the status).
[[nodiscard]] constexpr auto spread_pattern_name(cs2::aim::SpreadPattern pattern) noexcept
    -> std::string_view {
  return pattern == cs2::aim::SpreadPattern::revolver ? "revolver" : "standard";
}

}  // namespace cs2::control
