#pragma once

// `wait_for` predicate: a structured condition over a game snapshot
// (ADR-0020 / `docs/control-harness.md`).
//
// Grammar (a JSON object; several keys mean AND):
//   { "in_map": true }
//   { "local_alive": true }
//   { "on_ground": true }     // local pawn FL_ONGROUND
//   { "players_gte": 3 }      // total players
//   { "alive_gte": 2 }        // players with life_state == 0
//   { "local_hp_gte": 1 }
//   { "team": "T" }           // "T"/"CT" (or 2/3)
//   { "playable": true }      // alive AND on a team (T/CT) — false on the
//                             //   team-select screen / as a spectator / dead
//   { "all": [ ... ] }        // every child matches
//   { "any": [ ... ] }        // at least one child matches
//   { "not": { ... } }        // negation
//
// Unknown keys are rejected so a typo is a visible error rather than a
// silently-always-true predicate.

#include <cstdint>
#include <optional>
#include <vector>

#include "cs2/control/json.hpp"
#include "cs2/game/snapshot.hpp"

namespace cs2::control {

class Predicate {
 public:
  // A predicate that always matches (e.g. `{}` is not accepted; use this for
  // `wait_for` with no expression).
  [[nodiscard]] static Predicate always_true() noexcept;

  // Parse from a JSON object. Returns nullopt on a malformed/unknown shape.
  [[nodiscard]] static std::optional<Predicate> from_json(const Json& value);

  [[nodiscard]] bool matches(const game::Snapshot& snapshot) const noexcept;

 private:
  enum class Kind {
    always,
    in_map,
    local_alive,
    on_ground,
    players_gte,
    alive_gte,
    local_hp_gte,
    team,
    playable,
    all,
    any,
    negated,
  };

  [[nodiscard]] static std::optional<Predicate> parse_object(const Json& value);

  Kind kind_ = Kind::always;
  bool expected_ = false;       // in_map / local_alive
  std::int64_t threshold_ = 0;  // *_gte
  int team_ = 0;                // team (2 = T, 3 = CT)
  std::vector<Predicate> children_;
};

}  // namespace cs2::control
