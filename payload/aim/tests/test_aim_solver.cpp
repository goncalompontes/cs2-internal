// Unit tests for the aim solver and the hitbox construction (docs/ragebot.md).

#include <cmath>
#include <cstdio>
#include <vector>

#include "cs2/aim/solver.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-2F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

// A world-space AABB as 8 convex-hull vertices / its inward half-spaces.
auto box_vertices(float cx, float cy, float cz, float hx, float hy, float hz)
    -> std::vector<cs2::game::Vec3> {
  std::vector<cs2::game::Vec3> v;
  for (int i = 0; i < 8; ++i) {
    v.push_back(cs2::game::Vec3{cx + ((i & 1) != 0 ? hx : -hx), cy + ((i & 2) != 0 ? hy : -hy),
                                cz + ((i & 4) != 0 ? hz : -hz)});
  }
  return v;
}

auto box_planes(float cx, float cy, float cz, float hx, float hy, float hz)
    -> std::vector<cs2::aim::OccluderPlane> {
  using cs2::aim::OccluderPlane;
  using cs2::game::Vec3;
  return {
      OccluderPlane{Vec3{1.0F, 0.0F, 0.0F}, cx + hx},
      OccluderPlane{Vec3{-1.0F, 0.0F, 0.0F}, -(cx - hx)},
      OccluderPlane{Vec3{0.0F, 1.0F, 0.0F}, cy + hy},
      OccluderPlane{Vec3{0.0F, -1.0F, 0.0F}, -(cy - hy)},
      OccluderPlane{Vec3{0.0F, 0.0F, 1.0F}, cz + hz},
      OccluderPlane{Vec3{0.0F, 0.0F, -1.0F}, -(cz - hz)},
  };
}

}  // namespace

auto main() -> int {
  using cs2::aim::AimObjective;
  using cs2::aim::Basis;
  using cs2::aim::Hitbox;
  using cs2::aim::Occluder;
  using cs2::aim::SolveInput;
  using cs2::aim::Vec2;
  using cs2::aim::Vec3;

  const Basis basis{.forward = Vec3{1.0F, 0.0F, 0.0F},
                    .right = Vec3{0.0F, 1.0F, 0.0F},
                    .up = Vec3{0.0F, 0.0F, 1.0F}};

  // --- hitbox construction -------------------------------------------------
  {
    const auto boxes = cs2::aim::split_player_bounds(Vec3{-16.0F, -16.0F, 0.0F},
                                                     Vec3{16.0F, 16.0F, 72.0F}, 8.0F, 4.0F, 1.0F);
    expect(near(boxes[0].box.center.z, 68.0F), "head box sits on top");
    expect(near(boxes[1].box.center.z, 32.0F), "body box below the head");
    expect(near(boxes[0].multiplier, 4.0F), "head multiplier");

    // The head box rides the eye (a real reference), not the AABB top.
    const auto at_head =
        cs2::aim::split_player_at_head(Vec3{-16.0F, -16.0F, 0.0F}, Vec3{16.0F, 16.0F, 72.0F},
                                       Vec3{0.0F, 0.0F, 60.0F}, 4.0F, 4.0F, 1.0F);
    expect(near(at_head[0].box.center.z, 60.0F), "head box rides the eye");
    expect(near(at_head[0].box.half.x, 4.0F), "head box is small");
    expect(near(at_head[1].box.center.z, 28.0F), "body box below the head");
  }

  // --- single hitbox -------------------------------------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F}, Vec3{11.0F, 1.0F, 1.0F}),
               .multiplier = 1.0F});

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "single hitbox solves");
    expect(near(result.direction.x, 1.0F) && near(result.direction.y, 0.0F) &&
               near(result.direction.z, 0.0F),
           "aim points straight at the box");
    expect(near(result.value, 100.0F * std::pow(0.98F, 10.0F / 500.0F), 0.5F), "damage value");
  }

  // --- capsule hitbox (the real studio shape) ------------------------------
  {
    const auto polygon = cs2::aim::project_capsule(Vec3{}, Vec3{10.0F, -3.0F, 0.0F},
                                                   Vec3{10.0F, 3.0F, 0.0F}, 1.0F, basis);
    expect(!polygon.empty(), "capsule projects to a polygon");
    expect(cs2::aim::polygon_area(polygon) > 0.0F, "capsule silhouette has area");

    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};
    Hitbox capsule;
    capsule.is_capsule = true;
    capsule.capsule = cs2::aim::Capsule{
        .a = Vec3{10.0F, -3.0F, 0.0F}, .b = Vec3{10.0F, 3.0F, 0.0F}, .radius = 1.0F};
    capsule.multiplier = 4.0F;
    input.hitboxes.push_back(capsule);
    const auto result = cs2::aim::solve(input);
    expect(result.valid, "capsule hitbox solves");
    expect(near(result.direction.x, 1.0F) && near(result.direction.y, 0.0F) &&
               near(result.direction.z, 0.0F),
           "aim points at the capsule axis");
  }

  // --- head beats body -----------------------------------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F}, Vec3{11.0F, 1.0F, 1.0F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.5F, -0.5F, 4.5F}, Vec3{10.5F, 0.5F, 5.5F}),
               .multiplier = 4.0F});

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "two hitboxes solve");
    expect(result.aim.y > 0.4F, "aim rises to the head");
    expect(near(result.value, 400.0F * std::pow(0.98F, 10.0F / 500.0F), 2.0F), "head damage");
  }

  // --- an occluder removes the head ---------------------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F}, Vec3{11.0F, 1.0F, 1.0F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.5F, -0.5F, 4.5F}, Vec3{10.5F, 0.5F, 5.5F}),
               .multiplier = 4.0F});
    // Keep v <= 0.4 (an occluder below the head).
    input.occluders.push_back(Occluder{.normal = Vec2{0.0F, 1.0F}, .offset = 0.4F});

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "occluded field still solves");
    expect(result.aim.y < 0.2F, "occluded aim falls to the body");
    expect(near(result.value, 100.0F * std::pow(0.98F, 10.0F / 500.0F), 1.0F), "body damage");
  }

  // --- a real world solid (`OccluderCell`) attenuates / blocks ---------------
  {
    auto wall = [&](float surface) {
      return cs2::aim::project_occluder(Vec3{}, basis, box_vertices(10.0F, 0.0F, 0.0F, 0.25F, 2.0F, 2.0F),
                                        box_planes(10.0F, 0.0F, 0.0F, 0.25F, 2.0F, 2.0F), surface);
    };

    SolveInput head;
    head.eye = Vec3{};
    head.basis = basis;
    head.weapon = cs2::aim::WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .penetration = 1.0F, .headshot_multiplier = 4.0F};
    head.penetration_params.weapon_penetration = 1.0F;
    Hitbox cap;
    cap.is_capsule = true;
    cap.capsule = cs2::aim::Capsule{
        .a = Vec3{20.0F, -1.0F, -1.0F}, .b = Vec3{20.0F, 1.0F, 1.0F}, .radius = 1.0F};
    cap.multiplier = 4.0F;
    head.hitboxes.push_back(cap);

    const auto open = cs2::aim::solve(head);
    expect(open.valid, "open shot solves");
    expect(!open.penetrated, "an open shot is not attenuated");

    SolveInput thin = head;
    if (const auto w = wall(1.0F); w.has_value()) {
      thin.occlusion.push_back(*w);
    }
    const auto through = cs2::aim::solve(thin);
    expect(through.valid, "a penetrable wall still leaves a hittable head");
    expect(through.value < open.value, "the wall attenuates the head");
    expect(through.value > 0.0F, "a thin wall does not block the shot");
    expect(through.penetrated, "a shot through a wall is flagged as attenuated");

    SolveInput blocked = head;
    if (const auto w = wall(0.05F); w.has_value()) {  // the < 0.1 distance-modifier gate
      blocked.occlusion.push_back(*w);
    }
    expect(!cs2::aim::solve(blocked).valid, "an impenetrable surface leaves nothing to aim at");

    SolveInput capped = head;
    capped.penetration_params.max_occluders = 1;
    if (const auto w = wall(1.0F); w.has_value()) {
      capped.occlusion.push_back(*w);
      capped.occlusion.push_back(*w);
    }
    const auto incomplete = cs2::aim::solve(capped);
    expect(!incomplete.valid && !incomplete.occlusion_candidates_complete,
           "an occluder cap overflow fails closed instead of scoring a partial field");

    SolveInput tagged = capped;
    tagged.occlusion[0].target = 2;
    tagged.occlusion[1].target = 2;
    Hitbox valid_pose_hitbox = cap;
    valid_pose_hitbox.target = 1;  // neither tagged wall belongs to this pose's target
    Hitbox capped_pose_hitbox = cap;
    capped_pose_hitbox.target = 2;
    const std::vector<cs2::aim::PoseCandidate> capped_poses{
        cs2::aim::PoseCandidate{.record = -1, .hitboxes = {valid_pose_hitbox}},
        cs2::aim::PoseCandidate{.record = 1, .hitboxes = {capped_pose_hitbox}}};
    const auto capped_backtrack = cs2::aim::solve_backtrack(tagged, capped_poses);
    expect(!capped_backtrack.valid && !capped_backtrack.occlusion_candidates_complete,
           "backtracking fails closed if any candidate pose has incomplete geometry");
  }

  // --- hitchance objective -------------------------------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F};
    input.objective = AimObjective::hitchance;
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F}, Vec3{11.0F, 1.0F, 1.0F}),
               .multiplier = 1.0F});
    const auto result = cs2::aim::solve(input);
    expect(result.valid, "hitchance solves");
    expect(near(result.aim.x, 0.0F) && near(result.aim.y, 0.0F),
           "hitchance centre at the box centre");
  }

  // --- backtracking: the best record wins (docs/ragebot.md §8) --------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};

    std::vector<cs2::aim::PoseCandidate> poses;
    // Live pose: a body box only (multiplier 1).
    poses.push_back(cs2::aim::PoseCandidate{
        .record = -1,
        .age = 0.0F,
        .hitboxes = {
            Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F}, Vec3{11.0F, 1.0F, 1.0F}),
                   .multiplier = 1.0F}}});
    // A lag record carrying a head (multiplier 4) — the server can rewind to it.
    poses.push_back(cs2::aim::PoseCandidate{
        .record = 53,
        .age = 4.0F,
        .hitboxes = {
            Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.5F, -0.5F, 4.5F}, Vec3{10.5F, 0.5F, 5.5F}),
                   .multiplier = 4.0F}}});

    const auto best = cs2::aim::solve_backtrack(input, poses);
    expect(best.valid, "a pose is selected");
    expect(best.record == 53 && near(best.age, 4.0F), "the higher-damage record wins over live");
    expect(near(best.solve.value, 400.0F * std::pow(0.98F, 10.0F / 500.0F), 2.0F),
           "the winning record's value is used");

    // A tie keeps the earlier (live) pose.
    std::vector<cs2::aim::PoseCandidate> tie = poses;
    tie[1].hitboxes = poses[0].hitboxes;
    const auto tied = cs2::aim::solve_backtrack(input, tie);
    expect(tied.valid && tied.record == -1, "a tie keeps the live pose");

    // No usable pose -> invalid.
    const auto empty = cs2::aim::solve_backtrack(input, {});
    expect(!empty.valid, "no poses -> no backtrack aim");
  }

  // --- the pellet objective (docs/ragebot.md §3) ---------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 1.0F};
    input.objective = cs2::aim::AimObjective::pellets;
    // Two boxes a pellet-offset apart along the view-up axis (z ~ 1.1 at x ~ 10).
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -0.5F, -0.1F}, Vec3{11.0F, 0.5F, 0.1F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -0.5F, 1.0F}, Vec3{11.0F, 0.5F, 1.2F}),
               .multiplier = 1.0F});
    input.pellet_offsets = {Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.11F}};
    const auto result = cs2::aim::solve(input);
    expect(result.valid, "the pellet objective solves");
    expect(result.value > 150.0F, "the pellet aim sums two hitboxes");
  }

  if (failures == 0) {
    std::printf("aim-solver: ok\n");
    return 0;
  }
  return 1;
}
