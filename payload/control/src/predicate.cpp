#include "cs2/control/predicate.hpp"

#include <algorithm>
#include <string_view>

namespace cs2::control {
namespace {

[[nodiscard]] bool local_alive(const game::Snapshot& snapshot) noexcept {
  return snapshot.local.has_value() && snapshot.local->alive;
}

[[nodiscard]] std::int64_t alive_count(const game::Snapshot& snapshot) noexcept {
  return static_cast<std::int64_t>(
      std::count_if(snapshot.players.begin(), snapshot.players.end(),
                    [](const game::PlayerSnapshot& player) { return player.alive; }));
}

[[nodiscard]] int team_from_json(const Json& value) noexcept {
  if (value.is_number()) {
    return static_cast<int>(value.as_int());
  }
  const std::string_view text = value.as_string();
  if (text == "T" || text == "t" || text == "2") {
    return 2;
  }
  if (text == "CT" || text == "ct" || text == "3") {
    return 3;
  }
  return 0;
}

}  // namespace

Predicate Predicate::always_true() noexcept { return Predicate{}; }

auto Predicate::from_json(const Json& value) -> std::optional<Predicate> {
  return parse_object(value);
}

// Parse a predicate object. Several keys mean AND; `all`/`any`/`not` compose.
auto Predicate::parse_object(const Json& value) -> std::optional<Predicate> {
  if (!value.is_object() || value.members().empty()) {
    return std::nullopt;
  }
  std::vector<Predicate> children;
  for (const auto& [key, member] : value.members()) {
    Predicate node;
    if (key == "in_map" || key == "local_alive") {
      if (!member.is_bool()) {
        return std::nullopt;
      }
      node.kind_ = (key == "in_map") ? Kind::kInMap : Kind::kLocalAlive;
      node.expected_ = member.as_bool();
    } else if (key == "players_gte" || key == "alive_gte" || key == "local_hp_gte") {
      if (!member.is_number()) {
        return std::nullopt;
      }
      node.kind_ = (key == "players_gte")
                       ? Kind::kPlayersGte
                       : (key == "alive_gte" ? Kind::kAliveGte : Kind::kLocalHpGte);
      node.threshold_ = member.as_int();
    } else if (key == "team") {
      const int team = team_from_json(member);
      if (team == 0) {
        return std::nullopt;
      }
      node.kind_ = Kind::kTeam;
      node.team_ = team;
    } else if (key == "all" || key == "any") {
      if (!member.is_array()) {
        return std::nullopt;
      }
      node.kind_ = (key == "all") ? Kind::kAll : Kind::kAny;
      for (const Json& element : member.items()) {
        std::optional<Predicate> nested = parse_object(element);
        if (!nested.has_value()) {
          return std::nullopt;
        }
        node.children_.push_back(std::move(*nested));
      }
    } else if (key == "not") {
      std::optional<Predicate> nested = parse_object(member);
      if (!nested.has_value()) {
        return std::nullopt;
      }
      node.kind_ = Kind::kNot;
      node.children_.push_back(std::move(*nested));
    } else {
      return std::nullopt;  // unknown key
    }
    children.push_back(std::move(node));
  }
  if (children.size() == 1) {
    return std::move(children.front());
  }
  Predicate conjunction;
  conjunction.kind_ = Kind::kAll;
  conjunction.children_ = std::move(children);
  return conjunction;
}

bool Predicate::matches(const game::Snapshot& snapshot) const noexcept {
  switch (kind_) {
    case Kind::kTrue:
      return true;
    case Kind::kInMap:
      return (snapshot.valid && snapshot.local.has_value()) == expected_;
    case Kind::kLocalAlive:
      return local_alive(snapshot) == expected_;
    case Kind::kPlayersGte:
      return static_cast<std::int64_t>(snapshot.players.size()) >= threshold_;
    case Kind::kAliveGte:
      return alive_count(snapshot) >= threshold_;
    case Kind::kLocalHpGte:
      return snapshot.local.has_value() && snapshot.local->health >= threshold_;
    case Kind::kTeam:
      return snapshot.local.has_value() && snapshot.local->team == team_;
    case Kind::kAll:
      return std::all_of(children_.begin(), children_.end(),
                         [&](const Predicate& child) { return child.matches(snapshot); });
    case Kind::kAny:
      return std::any_of(children_.begin(), children_.end(),
                         [&](const Predicate& child) { return child.matches(snapshot); });
    case Kind::kNot:
      return children_.empty() || !children_.front().matches(snapshot);
  }
  return false;
}

}  // namespace cs2::control
