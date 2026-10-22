#include <cstdio>
#include <string>
#include "cs2/aim/reader.hpp"
namespace cs2::game {
bool read_bytes(std::uintptr_t, void*, std::size_t) noexcept {
  return false;
}
std::string read_cstring(std::uintptr_t, std::size_t) {
  return {};
}
}  // namespace cs2::game
auto main() -> int {
  cs2::game::Snapshot snapshot;
  snapshot.valid = true;
  cs2::game::PlayerSnapshot local;
  local.team = 2;
  local.alive = true;
  local.is_local = true;
  local.eye_origin = {0, 0, 0};
  snapshot.local = local;
  cs2::game::PlayerSnapshot target;
  target.index = 7;
  target.pawn = 123;
  target.team = 3;
  target.alive = true;
  target.eye_origin = {-100, 0, 0};
  target.origin = {-100, 0, -64};
  target.distance = 100;
  target.armor = 50;
  target.helmet = true;
  snapshot.players.push_back(target);
  cs2::aim::WeaponProfile weapon;
  weapon.damage = 100;
  auto input = cs2::aim::build_input(snapshot, weapon, 7, false);
  if (!input || input->basis.forward.x > -.99F || input->defense.pawn != 123 ||
      input->defense.armor != 50) {
    std::puts("FAIL: rear target chart or defense binding");
    return 1;
  }
  const auto result = cs2::aim::solve(*input);
  if (!result.valid || result.direction.x > -.9F) {
    std::puts("FAIL: rear target not solved in its local chart");
    return 1;
  }
  snapshot.players[0].team = 2;
  if (cs2::aim::build_input(snapshot, weapon, 7, false)) {
    std::puts("FAIL: teammate selected as enemy");
    return 1;
  }
  if (!cs2::aim::build_input(snapshot, weapon, 7, false, true)) {
    std::puts("FAIL: explicit teammate geometry unavailable for occlusion");
    return 1;
  }
  std::puts("aim target charts: ok (rear target, defense identity, teammate geometry)");
  return 0;
}
