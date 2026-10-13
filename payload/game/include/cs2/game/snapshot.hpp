#pragma once

// A point-in-time view of the target's player state.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cs2/game/math.hpp"

namespace cs2::game {

// One player (local or enemy), captured from its controller + pawn.
struct PlayerSnapshot {
  int index = -1;          // entity index
  std::uintptr_t pawn = 0;
  std::uintptr_t controller = 0;
  std::string name;
  std::string weapon;      // active weapon designer name (e.g. "weapon_ak47")
  int team = 0;            // 2 = T, 3 = CT
  int health = 0;
  int max_health = 0;
  int armor = 0;
  int life_state = 0;      // 0 = alive
  int clip = 0;            // rounds in the active weapon
  int reserve = 0;         // reserve rounds
  int ping = 0;            // ms
  int money = 0;           // account balance
  float flash = 0.0F;      // remaining flash duration (seconds)
  bool alive = false;
  bool is_local = false;
  bool helmet = false;
  bool defuser = false;
  bool scoped = false;
  bool defusing = false;
  bool in_buy_zone = false;
  bool has_bomb = false;
  Vec3 origin{};           // world origin (feet)
  Vec3 eye_origin{};       // origin + view offset
  Vec3 mins{-16.0F, -16.0F, 0.0F};  // local collision bounds
  Vec3 maxs{16.0F, 16.0F, 72.0F};
  Vec3 velocity{};
  Angle eye_angles{};
  float distance = 0.0F;   // distance from the local player (0 for local)
};

struct Snapshot {
  bool valid = false;                 // target state was readable
  std::uintptr_t local_pawn = 0;
  std::optional<PlayerSnapshot> local;
  std::vector<PlayerSnapshot> players;  // every player, local included
};

}  // namespace cs2::game
