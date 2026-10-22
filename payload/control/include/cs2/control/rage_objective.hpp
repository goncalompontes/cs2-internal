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
#include <span>
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
  if (weapon.size() > k_prefix.size() && same_name(weapon.substr(0, k_prefix.size()), k_prefix)) {
    return weapon.substr(k_prefix.size());
  }
  return weapon;
}

// The weapon's **spread pattern**, decided from its designer name.
//
// **This names the weapon's *class*, not the engine's rule.** The engine's rule
// (docs/re/aim.md §"The attack type") is: the revolver's `1 - r^2` needs the
// weapon **id** — the VData's `m_bIsRevolver` — **and** the attack type == 1.
// The designer name is **not** equivalent: it cannot see the attack type, and it
// is a name, not an id. The rule therefore lives in the **core**
// (`cs2::aim::radius_transform`), which reads the VData's flag and the attack
// type from the weapon profile, so this class can only ever **narrow** what the
// core does — it can never put the revolver's transform on a weapon the engine
// would not transform.
//
// The rule is an **exact match** on the weapon's short name:
//
//   * `revolver` -> the revolver's class;
//   * every other name -> the standard class.
//
// **A weapon the payload could not name** (an empty designer name) gets the
// standard class as well: it is the class every weapon except the revolver uses,
// so it is the conservative choice. The name is what the class is keyed on and
// the caller reports it, so an operator can always see which weapon was
// classified.
[[nodiscard]] constexpr auto spread_pattern_for(std::string_view weapon) noexcept
    -> cs2::aim::SpreadPattern {
  return same_name(weapon_short_name(weapon), "revolver") ? cs2::aim::SpreadPattern::revolver
                                                          : cs2::aim::SpreadPattern::standard;
}

// The weapon's **spread pattern**, decided from the weapon's profile.
//
// **This is the engine's discriminator**: the VData's own `m_bIsRevolver` flag
// (+0x71E) — the payload's read of the weapon **id** the engine compares. The
// deagle's flag is false and the R8's is true, so the two are told apart by the
// same thing the engine tells them apart by.
//
// **The class alone is not the transform.** The core applies the revolver's
// `1 - r^2` only when the attack type is the secondary one
// (`cs2::aim::radius_transform`), because the engine's rule needs both.
[[nodiscard]] constexpr auto spread_pattern_for(const cs2::aim::WeaponProfile& weapon) noexcept
    -> cs2::aim::SpreadPattern {
  return weapon.is_revolver ? cs2::aim::SpreadPattern::revolver
                            : cs2::aim::SpreadPattern::standard;
}

// The pattern's name (the status).
[[nodiscard]] constexpr auto spread_pattern_name(cs2::aim::SpreadPattern pattern) noexcept
    -> std::string_view {
  return pattern == cs2::aim::SpreadPattern::revolver ? "revolver" : "standard";
}

// --- The aim command's weapon rule ---------------------------------------------

// The caller's explicit weapon values, applied to the profile the **game**
// supplied.
//
// **The kernel's terms are never touched.** The Monte-Carlo kernel is built
// from the weapon's **spread**, its **inaccuracy** and its **type**: a
// profile that carried none of them (the caller's own, which sets only the
// damage model) would build a **degenerate kernel** — every sample lands
// exactly on the aim, so the "measured probability" is 1.0 for a weapon that
// really spreads, and an unknown type counts as "modelled", so a shotgun
// would be scored with the standard pattern.
//
// So the two are **merged**: the caller wins on the damage model, the game
// wins on the kernel.
[[nodiscard]] constexpr auto apply_stated_weapon(cs2::aim::WeaponProfile profile,
                                                 const cs2::aim::WeaponProfile& stated)
    -> cs2::aim::WeaponProfile {
  profile.damage = stated.damage;
  profile.range_modifier = stated.range_modifier;
  profile.penetration = stated.penetration;
  profile.headshot_multiplier = stated.headshot_multiplier;
  return profile;
}

// --- The aim command's target rule --------------------------------------------

// The **target index** the hitboxes carry.
//
// The Monte-Carlo rule's first key is judged against a target's own health,
// and the core looks that health up under the **hitbox's** target index
// (`Hitbox::target` and `TargetHealth::index` are the same key). The `aim`
// command used to hand the health list the entity index while the hitboxes
// kept their default -1, so the lookup never matched: `mc_target` was always
// -1 and `mc_can_kill` always false — the rule's **first** key was dead.
//
// **One rule, one index.** The rule is stated here, so the health list and the
// hitboxes cannot drift apart.
[[nodiscard]] constexpr auto hitbox_target(std::span<const cs2::aim::Hitbox> hitboxes) noexcept
    -> int {
  return hitboxes.empty() ? -1 : hitboxes.front().target;
}

// The health entry that goes with a set of hitboxes: **the same index**.
//
// A negative index means "no target": the core finds no health for it, so the
// can-kill key stays false — "an unknown target's health is not a proof, so it
// does not kill" (`cs2/aim/montecarlo.hpp`).
[[nodiscard]] constexpr auto health_entry_for(std::span<const cs2::aim::Hitbox> hitboxes,
                                              float health) noexcept -> cs2::aim::TargetHealth {
  return cs2::aim::TargetHealth{.index = hitbox_target(hitboxes), .health = health};
}

// --- The Monte-Carlo's sample count --------------------------------------------

// The sample count the live path uses.
//
// **64 is the default**: it is the only count that fits the frame budget at a
// live-sized hitbox set (`docs/ragebot.md` §13.6 — 4.3 ms at the p50 for 57
// hitboxes, against a 15.6 ms tick). A larger count is the operator's
// explicit choice, but 128 and 256 **do not fit the tick**, so the count in
// force is always said out loud
// (`docs/orchestration/spec-t35-montecarlo-live.md` §2.1a).
inline constexpr int MC_SAMPLES_DEFAULT = 64;
inline constexpr int MC_SAMPLES_MIN = 1;
inline constexpr int MC_SAMPLES_MAX = 256;
// The largest count that still fits a 15.6 ms tick.
inline constexpr int MC_SAMPLES_TICK_BUDGET = 64;

// Whether the count is one the live path accepts.
[[nodiscard]] constexpr auto mc_samples_in_range(int samples) noexcept -> bool {
  return samples >= MC_SAMPLES_MIN && samples <= MC_SAMPLES_MAX;
}

// The note the status carries with the sample count.
//
// **Two things the operator has to know**: a count above 64 does not fit the
// tick, and a count outside 1..256 is **ignored** — by both entry points, so
// the two behave the same way (`requested` is what the caller asked for,
// `in_force` is what the solve will use).
[[nodiscard]] constexpr auto mc_samples_note(int requested, int in_force) noexcept
    -> std::string_view {
  if (requested != in_force) {
    return "out of range (1..256): the request was ignored";
  }
  if (in_force > MC_SAMPLES_TICK_BUDGET) {
    return "over the tick budget: 64 is the count that fits a tick";
  }
  return "";
}

}  // namespace cs2::control
