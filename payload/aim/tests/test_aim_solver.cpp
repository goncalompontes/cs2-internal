// Unit tests for the aim solver and the hitbox construction (docs/ragebot.md).

#include <cmath>
#include <cstdio>

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

}  // namespace

auto main() -> int {
  using cs2::aim::AimObjective;
  using cs2::aim::Basis;
  using cs2::aim::Hitbox;
  using cs2::aim::Occluder;
  using cs2::aim::SolveInput;
  using cs2::aim::Vec2;
  using cs2::aim::Vec3;

  const Basis basis{
      .forward = Vec3{1.0F, 0.0F, 0.0F}, .right = Vec3{0.0F, 1.0F, 0.0F}, .up = Vec3{0.0F, 0.0F, 1.0F}};

  // --- hitbox construction -------------------------------------------------
  {
    const auto boxes = cs2::aim::split_player_bounds(Vec3{-16.0F, -16.0F, 0.0F},
                                                     Vec3{16.0F, 16.0F, 72.0F}, 8.0F, 4.0F, 1.0F);
    expect(near(boxes[0].box.center.z, 68.0F), "head box sits on top");
    expect(near(boxes[1].box.center.z, 32.0F), "body box below the head");
    expect(near(boxes[0].multiplier, 4.0F), "head multiplier");

    // The head box rides the eye (a real reference), not the AABB top.
    const auto at_head = cs2::aim::split_player_at_head(Vec3{-16.0F, -16.0F, 0.0F},
                                                        Vec3{16.0F, 16.0F, 72.0F},
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
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F},
                                                              Vec3{11.0F, 1.0F, 1.0F}),
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
    capsule.capsule = cs2::aim::Capsule{.a = Vec3{10.0F, -3.0F, 0.0F},
                                        .b = Vec3{10.0F, 3.0F, 0.0F},
                                        .radius = 1.0F};
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
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F},
                                                              Vec3{11.0F, 1.0F, 1.0F}),
                                    .multiplier = 1.0F});
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.5F, -0.5F, 4.5F},
                                                              Vec3{10.5F, 0.5F, 5.5F}),
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
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F},
                                                              Vec3{11.0F, 1.0F, 1.0F}),
                                    .multiplier = 1.0F});
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.5F, -0.5F, 4.5F},
                                                              Vec3{10.5F, 0.5F, 5.5F}),
                                    .multiplier = 4.0F});
    // Keep v <= 0.4 (an occluder below the head).
    input.occluders.push_back(Occluder{.normal = Vec2{0.0F, 1.0F}, .offset = 0.4F});

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "occluded field still solves");
    expect(result.aim.y < 0.2F, "occluded aim falls to the body");
    expect(near(result.value, 100.0F * std::pow(0.98F, 10.0F / 500.0F), 1.0F), "body damage");
  }

  // --- hitchance objective -------------------------------------------------
  {
    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F};
    input.objective = AimObjective::hitchance;
    input.hitboxes.push_back(Hitbox{.box = cs2::aim::aabb_obb(Vec3{9.0F, -1.0F, -1.0F},
                                                              Vec3{11.0F, 1.0F, 1.0F}),
                                    .multiplier = 1.0F});
    const auto result = cs2::aim::solve(input);
    expect(result.valid, "hitchance solves");
    expect(near(result.aim.x, 0.0F) && near(result.aim.y, 0.0F), "hitchance centre at the box centre");
  }

  if (failures == 0) {
    std::printf("aim-solver: ok\n");
    return 0;
  }
  return 1;
}
