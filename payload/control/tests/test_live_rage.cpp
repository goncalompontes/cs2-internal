#include <cstdio>
#include <cstdlib>
#include "cs2/control/live_rage.hpp"
using namespace cs2;
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
int main() {
  control::AimFrame frame{11, 22, 100, 7, {}};
  control::CapturedTarget target;
  target.index = 3;
  target.health = 100;
  target.simulation_time = 10;
  target.geometry.pawn = 33;
  target.geometry.eye = {0, 0, 64};
  target.geometry.weapon.damage = 50;
  target.geometry.basis = aim::basis_from_angles({0, 0, 0});
  target.geometry.spread_known = true;
  target.geometry.kernel = {{{0, 0}, 1}};
  aim::Hitbox head;
  head.is_capsule = true;
  head.capsule = {{100, 0, 64}, {100, 0, 68}, 5};
  head.group = 1;
  head.bone = 6;
  head.multiplier = 4;
  aim::Hitbox body = head;
  body.group = 2;
  body.bone = 4;
  body.multiplier = 1;
  body.capsule = {{100, 0, 40}, {100, 0, 55}, 8};
  target.geometry.hitboxes = {head, body};
  int callbacks = 0;
  target.geometry.world_query = [&](auto, auto, float, const auto&) {
    ++callbacks;
    return aim::WorldDamage{};
  };
  frame.targets.push_back(target);
  const auto captured = frame;
  frame.targets[0].geometry.hitboxes.clear();
  auto proposals = control::propose_shots(captured);
  check(callbacks == 0, "worker never invokes live engine callbacks");
  check(proposals.entries.size() == 2 && proposals.local == 11 && proposals.weapon == 22 &&
            proposals.generation == 7,
        "immutable frame retains head and body alternatives with identities");
  const auto p = proposals.entries[0];
  const auto old = control::proposal_point(target, p);
  check(old.has_value(), "proposal matched to target and bone");
  for (auto& h : target.geometry.hitboxes) {
    h.capsule.a.y += 20;
    h.capsule.b.y += 20;
  }
  const auto moved = control::proposal_point(target, p);
  check(moved && std::abs(moved->y - old->y - 20) < .001F,
        "command reanchors proposal on freshly captured pose");

  // Final acceptance uses current spread and real penetration results on the caller thread.
  control::AimFrame current{11, 22, 120, 7, {target}};
  const auto world = [](aim::Vec3, aim::Vec3 direction, float, const aim::WeaponProfile&) {
    return aim::WorldDamage{direction.z < -.02F ? 50.0F : 0.0F, true};
  };
  bool body_accepted = false, head_blocked = false;
  for (const auto& proposal : proposals.entries) {
    const auto shot =
        control::check_proposal(current, current.targets[0], proposal, world, 20, .6F);
    if (proposal.group == 1) head_blocked = !shot.accepted;
    if (proposal.group == 2) body_accepted = shot.accepted;
  }
  check(head_blocked && body_accepted,
        "blocked high-damage head does not eliminate a valid body alternative");
  auto blocked = control::check_proposal(
      current, current.targets[0], p,
      [](auto, auto, float, const auto&) {
        return aim::WorldDamage{0, true};
      },
      20, .6F);
  check(!blocked.accepted, "world-blocked shot rejected at command boundary");
  current.targets[0].geometry.spread_known = false;
  check(!control::check_proposal(current, current.targets[0], p, world, 20, .6F).accepted,
        "unknown spread cannot authorize a shot");
  target.geometry.pawn = 44;
  check(!control::proposal_point(target, p),
        "reused entity index cannot retarget a different pawn");
  std::puts(
      "PASS: immutable worker geometry, no engine callbacks, regional alternatives and "
      "current-pose/identity revalidation");
}
