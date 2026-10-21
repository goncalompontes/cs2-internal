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
    v.push_back(cs2::game::Vec3{.x = cx + ((i & 1) != 0 ? hx : -hx),
                                .y = cy + ((i & 2) != 0 ? hy : -hy),
                                .z = cz + ((i & 4) != 0 ? hz : -hz)});
  }
  return v;
}

auto box_planes(float cx, float cy, float cz, float hx, float hy, float hz)
    -> std::vector<cs2::aim::OccluderPlane> {
  using cs2::aim::OccluderPlane;
  using cs2::game::Vec3;
  return {
      OccluderPlane{.normal = Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, .dist = cx + hx},
      OccluderPlane{.normal = Vec3{.x = -1.0F, .y = 0.0F, .z = 0.0F}, .dist = -(cx - hx)},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, .dist = cy + hy},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = -1.0F, .z = 0.0F}, .dist = -(cy - hy)},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F}, .dist = cz + hz},
      OccluderPlane{.normal = Vec3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, .dist = -(cz - hz)},
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

  const Basis basis{.forward = Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F},
                    .right = Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                    .up = Vec3{.x = 0.0F, .y = 0.0F, .z = 1.0F}};

  // --- hitbox construction -------------------------------------------------
  {
    const auto boxes =
        cs2::aim::split_player_bounds(Vec3{.x = -16.0F, .y = -16.0F, .z = 0.0F},
                                      Vec3{.x = 16.0F, .y = 16.0F, .z = 72.0F}, 8.0F, 4.0F, 1.0F);
    expect(near(boxes[0].box.center.z, 68.0F), "head box sits on top");
    expect(near(boxes[1].box.center.z, 32.0F), "body box below the head");
    expect(near(boxes[0].multiplier, 4.0F), "head multiplier");

    // The head box rides the eye (a real reference), not the AABB top.
    const auto at_head = cs2::aim::split_player_at_head(
        Vec3{.x = -16.0F, .y = -16.0F, .z = 0.0F}, Vec3{.x = 16.0F, .y = 16.0F, .z = 72.0F},
        Vec3{.x = 0.0F, .y = 0.0F, .z = 60.0F}, 4.0F, 4.0F, 1.0F);
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
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                         Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F}),
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
    const auto polygon =
        cs2::aim::project_capsule(Vec3{}, Vec3{.x = 10.0F, .y = -3.0F, .z = 0.0F},
                                  Vec3{.x = 10.0F, .y = 3.0F, .z = 0.0F}, 1.0F, basis);
    expect(!polygon.empty(), "capsule projects to a polygon");
    expect(cs2::aim::polygon_area(polygon) > 0.0F, "capsule silhouette has area");

    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{.damage = 100.0F, .range_modifier = 0.98F};
    Hitbox capsule;
    capsule.is_capsule = true;
    capsule.capsule = cs2::aim::Capsule{.a = Vec3{.x = 10.0F, .y = -3.0F, .z = 0.0F},
                                        .b = Vec3{.x = 10.0F, .y = 3.0F, .z = 0.0F},
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
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                         Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.5F, .y = -0.5F, .z = 4.5F},
                                         Vec3{.x = 10.5F, .y = 0.5F, .z = 5.5F}),
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
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                         Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.5F, .y = -0.5F, .z = 4.5F},
                                         Vec3{.x = 10.5F, .y = 0.5F, .z = 5.5F}),
               .multiplier = 4.0F});
    // Keep v <= 0.4 (an occluder below the head).
    input.occluders.push_back(Occluder{.normal = Vec2{.x = 0.0F, .y = 1.0F}, .offset = 0.4F});

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "occluded field still solves");
    expect(result.aim.y < 0.2F, "occluded aim falls to the body");
    expect(near(result.value, 100.0F * std::pow(0.98F, 10.0F / 500.0F), 1.0F), "body damage");
  }

  // --- a real world solid (`OccluderCell`) attenuates / blocks ---------------
  {
    auto wall = [&](float surface) -> std::optional<cs2::aim::OccluderCell> {
      return cs2::aim::project_occluder(Vec3{}, basis,
                                        box_vertices(10.0F, 0.0F, 0.0F, 0.25F, 2.0F, 2.0F),
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
    cap.capsule = cs2::aim::Capsule{.a = Vec3{.x = 20.0F, .y = -1.0F, .z = -1.0F},
                                    .b = Vec3{.x = 20.0F, .y = 1.0F, .z = 1.0F},
                                    .radius = 1.0F};
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
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                         Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F}),
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
        .hitboxes = {Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -1.0F, .z = -1.0F},
                                                      Vec3{.x = 11.0F, .y = 1.0F, .z = 1.0F}),
                            .multiplier = 1.0F}}});
    // A lag record carrying a head (multiplier 4) — the server can rewind to it.
    poses.push_back(cs2::aim::PoseCandidate{
        .record = 53,
        .age = 4.0F,
        .hitboxes = {Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.5F, .y = -0.5F, .z = 4.5F},
                                                      Vec3{.x = 10.5F, .y = 0.5F, .z = 5.5F}),
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
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -0.5F, .z = -0.1F},
                                         Vec3{.x = 11.0F, .y = 0.5F, .z = 0.1F}),
               .multiplier = 1.0F});
    input.hitboxes.push_back(
        Hitbox{.box = cs2::aim::aabb_obb(Vec3{.x = 9.0F, .y = -0.5F, .z = 1.0F},
                                         Vec3{.x = 11.0F, .y = 0.5F, .z = 1.2F}),
               .multiplier = 1.0F});
    input.pellet_offsets = {Vec2{.x = 0.0F, .y = 0.0F}, Vec2{.x = 0.0F, .y = 0.11F}};
    const auto result = cs2::aim::solve(input);
    expect(result.valid, "the pellet objective solves");
    expect(result.value > 150.0F, "the pellet aim sums two hitboxes");
  }

  // --- the Monte-Carlo objective (docs/ragebot.md §13) ----------------------
  {
    // The scene: one sphere at 100 units, so its silhouette is symmetric about the
    // view axis and the field's cell is centred on it. The cone (the weapon's
    // spread) is twice the sphere's angular radius, so the Monte-Carlo solver
    // measures a probability of about one half — where the field's **Gaussian**
    // (whose sigma is too wide, `docs/research/aim-objective-math.md`) reports far
    // less. The two numbers are not interchangeable, and the objective must report
    // the Monte-Carlo's.
    const Vec3 centre{.x = 100.0F, .y = 0.0F, .z = 0.0F};
    constexpr float k_radius = 1.0F;
    constexpr float k_spread = 0.02F;

    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{
        .damage = 100.0F, .range_modifier = 1.0F, .spread = k_spread, .weapon_type = 3};
    input.spread_sigma = k_spread;
    input.objective = AimObjective::monte_carlo;
    Hitbox sphere;
    sphere.is_capsule = true;
    sphere.capsule = cs2::aim::Capsule{.a = centre, .b = centre, .radius = k_radius};
    sphere.multiplier = 1.0F;
    sphere.group = cs2::aim::HITGROUP_CHEST;
    sphere.target = 0;
    input.hitboxes.push_back(sphere);
    input.monte_carlo_targets.push_back(cs2::aim::TargetHealth{.index = 0, .health = 1000.0F});
    input.monte_carlo_seed = cs2::aim::SeedContext{.sequence = 4242U, .tick = 42U};

    const auto result = cs2::aim::solve(input);
    expect(result.valid, "the Monte-Carlo objective solves");
    expect(result.monte_carlo.has_value(), "the Monte-Carlo solver produced a verdict");
    expect(!result.monte_carlo_refusal.has_value(), "the Monte-Carlo solver did not refuse");

    // The field's own Gaussian at the same aim — the number the objective must
    // **not** report.
    cs2::aim::DamageField field;
    {
      const auto polygon = cs2::aim::project_capsule(input.eye, centre, centre, k_radius, basis);
      field.add_hitbox(polygon, sphere.multiplier, 100.0F, input.weapon);
    }
    const float gaussian = field.hit_probability(result.aim, input.spread_sigma);
    expect(!near(result.hitchance, gaussian, 0.05F),
           "the reported hitchance is not the field's Gaussian");
    expect(result.hitchance > 0.3F && result.hitchance < 0.7F,
           "the reported hitchance is the Monte-Carlo's measured probability");
    // The value is the **expected** damage: a miss contributes 0, so it is the
    // probability times the damage of a hit — and it is not the field's value (the
    // damage of a shot that hits).
    expect(near(result.value, result.hitchance * 100.0F, 0.01F),
           "the reported value is the expected damage");
    expect(result.value < 100.0F, "the expected damage is not the damage of a shot that hits");
    expect(near(field.value_at(result.aim), 100.0F, 0.01F),
           "the field's own value is the damage of a shot that hits");
    // The verdict carries the policy that produced it, and the sample count.
    expect(result.monte_carlo.has_value() && result.monte_carlo->samples == 64,
           "the verdict reports the sample count");
    expect(result.monte_carlo.has_value() &&
               result.monte_carlo->seed_source == cs2::aim::SeedSource::sequence,
           "the verdict reports the seed policy");

    // The can-kill key: the damage **attributed to the target** against that
    // target's own health.
    expect(result.monte_carlo.has_value() && !result.monte_carlo->can_kill,
           "a target with 1000 hp left does not die from the expected damage");
    SolveInput lethal = input;
    lethal.monte_carlo_targets.clear();
    lethal.monte_carlo_targets.push_back(cs2::aim::TargetHealth{.index = 0, .health = 10.0F});
    const auto lethal_result = cs2::aim::solve(lethal);
    expect(lethal_result.monte_carlo.has_value() && lethal_result.monte_carlo->can_kill,
           "a target with 10 hp left dies from the expected damage");

    // A **malformed** hitbox must not refuse the whole scene. The reader does
    // not validate the shape it reads — a capsule with a radius of zero reaches
    // the solver today (`docs/ragebot.md` §13.3) — so the objective filters the
    // set for the core instead of handing it a scene the core refuses.
    SolveInput malformed = input;
    Hitbox degenerate;
    degenerate.is_capsule = true;
    degenerate.capsule = cs2::aim::Capsule{.a = centre, .b = centre, .radius = 0.0F};
    degenerate.multiplier = 1.0F;
    degenerate.target = 1;
    malformed.hitboxes.push_back(degenerate);
    const auto filtered = cs2::aim::solve(malformed);
    expect(filtered.valid, "the scene with a degenerate hitbox still solves");
    expect(filtered.monte_carlo.has_value(),
           "a degenerate hitbox is filtered for the core, not a refusal of the whole scene");
    expect(!filtered.monte_carlo_refusal.has_value(), "the scene is not refused");
    expect(filtered.monte_carlo.has_value() && filtered.monte_carlo->probability > 0.0F,
           "the good target is still found");

    // --- the refusal: the weapon's pattern is not modelled ------------------
    // The shotgun's pellet pattern table is not modelled, so the core refuses —
    // and the refusal is **observable**: the aim falls back to the `damage`
    // objective, and the reason is in the result.
    SolveInput shotgun = input;
    shotgun.weapon.weapon_type = cs2::aim::WEAPON_TYPE_SHOTGUN;
    const auto refused = cs2::aim::solve(shotgun);
    expect(refused.valid, "the refused solve still produces an aim");
    expect(!refused.monte_carlo.has_value(), "the refused solve has no Monte-Carlo verdict");
    expect(refused.monte_carlo_refusal.has_value() &&
               *refused.monte_carlo_refusal == cs2::aim::MonteCarloError::unsupported_pattern,
           "the refusal names the unmodelled pattern");
    // The fallback is the `damage` objective — the aim is exactly the one the
    // `damage` objective produces on the same field. **The scene here is
    // symmetric, so this pair of assertions cannot tell the `damage` objective
    // from the `hitchance` one** (every objective's optimum is the same point):
    // the fallback's *identity* is pinned by the asymmetric scene in the
    // regression block below.
    SolveInput damage = input;
    damage.objective = AimObjective::damage;
    const auto damage_result = cs2::aim::solve(damage);
    expect(near(refused.aim.x, damage_result.aim.x, 1e-4F) &&
               near(refused.aim.y, damage_result.aim.y, 1e-4F),
           "the refusal falls back to the damage objective's aim");
    expect(near(refused.value, damage_result.value, 1e-4F),
           "the refusal reports the damage objective's value");

    // The **search's** own refusal (not only the kernel's): a sample count of zero
    // is refused by the core, and the refusal is observable too.
    SolveInput no_samples = input;
    no_samples.monte_carlo_samples = 0;
    const auto refused_samples = cs2::aim::solve(no_samples);
    expect(refused_samples.valid, "the refused search still produces an aim");
    expect(!refused_samples.monte_carlo.has_value(), "the refused search has no verdict");
    expect(refused_samples.monte_carlo_refusal.has_value() &&
               *refused_samples.monte_carlo_refusal == cs2::aim::MonteCarloError::invalid_samples,
           "the search's refusal names the invalid sample count");
  }

  // --- the four other objectives are unchanged -------------------------------
  {
    // The regression: the Monte-Carlo objective is a **value**, not a behaviour
    // change. The four objectives above must produce exactly what the field's
    // own optima produce — the same aim, the same value, the same hitchance and
    // the same penetration flag — and a `SolveInput` that does not set the
    // Monte-Carlo parameters must be solved exactly as it was before.
    //
    // **The scene is asymmetric on purpose.** With one symmetric sphere every
    // objective's optimum is the *same* point, so the comparison below is
    // satisfied by *any* objective: pointing `damage` at the hitchance search
    // (or the reverse) leaves the test green. Two cells — a big body and a
    // small, higher-multiplier head off-centre — give the objectives
    // **different** optima, and the block asserts that too, so it is a guard
    // and not a tautology.
    //
    // **The multipliers are chosen so both cells clear the hitchance
    // objective's value floor** (`argmax_hitchance` only considers the cells
    // worth at least half the best one). With a 4x head the body would be
    // excluded and the margin optimum would land on the head again — the scene
    // would be asymmetric in name only.
    const Vec3 body_centre{.x = 100.0F, .y = 0.0F, .z = 0.0F};
    const Vec3 head_centre{.x = 100.0F, .y = 0.30F, .z = 0.0F};
    constexpr float k_body_radius = 2.0F;
    constexpr float k_head_radius = 0.5F;
    constexpr float k_body_multiplier = 1.0F;
    constexpr float k_head_multiplier = 1.5F;

    SolveInput input;
    input.eye = Vec3{};
    input.basis = basis;
    input.weapon = cs2::aim::WeaponProfile{
        .damage = 100.0F, .range_modifier = 0.98F, .spread = 0.02F, .weapon_type = 3};
    input.spread_sigma = 0.02F;
    input.pose_sigma = 0.01F;
    input.pellet_offsets = {Vec2{.x = 0.0F, .y = 0.0F}, Vec2{.x = 0.0F, .y = 0.002F}};
    Hitbox body;
    body.is_capsule = true;
    body.capsule = cs2::aim::Capsule{.a = body_centre, .b = body_centre, .radius = k_body_radius};
    body.multiplier = k_body_multiplier;
    input.hitboxes.push_back(body);
    Hitbox head;
    head.is_capsule = true;
    head.capsule = cs2::aim::Capsule{.a = head_centre, .b = head_centre, .radius = k_head_radius};
    head.multiplier = k_head_multiplier;
    input.hitboxes.push_back(head);

    // The field the solver builds, rebuilt here: two hitboxes, no occluders.
    cs2::aim::DamageField field;
    {
      const auto body_polygon =
          cs2::aim::project_capsule(input.eye, body_centre, body_centre, k_body_radius, basis);
      field.add_hitbox(body_polygon, body.multiplier, cs2::game::distance(input.eye, body_centre),
                       input.weapon);
      const auto head_polygon =
          cs2::aim::project_capsule(input.eye, head_centre, head_centre, k_head_radius, basis);
      field.add_hitbox(head_polygon, head.multiplier, cs2::game::distance(input.eye, head_centre),
                       input.weapon);
    }

    // The four optima. The `hitchance` objective maximizes the **hit
    // probability** when the input carries a cone (`solver.cpp`), and falls back
    // to the Chebyshev-centre margin heuristic when it does not; the scene
    // carries a cone, so the probability optimum is the one the objective
    // produces.
    const Vec2 damage_aim = field.argmax_damage();
    const Vec2 margin_aim = field.argmax_hitchance();
    const Vec2 hitchance_aim = field.argmax_hit_probability(input.spread_sigma);
    const Vec2 expected_aim = field.argmax_expected(input.pose_sigma);
    const Vec2 pellets_aim = field.argmax_pellets(input.pellet_offsets);

    // **The optima must differ.** A scene where they coincide cannot tell the
    // objectives apart, so the comparison below would be satisfied by any of
    // them — that is what made this block vacuous in the first place.
    expect(!near(damage_aim.x, margin_aim.x, 1e-4F) || !near(damage_aim.y, margin_aim.y, 1e-4F),
           "the damage and the margin optima differ in an asymmetric scene");
    expect(
        !near(damage_aim.x, hitchance_aim.x, 1e-4F) || !near(damage_aim.y, hitchance_aim.y, 1e-4F),
        "the damage and the hitchance optima differ in an asymmetric scene");
    expect(!near(damage_aim.x, expected_aim.x, 1e-4F) || !near(damage_aim.y, expected_aim.y, 1e-4F),
           "the damage and the expected optima differ in an asymmetric scene");
    expect(!near(hitchance_aim.x, expected_aim.x, 1e-4F) ||
               !near(hitchance_aim.y, expected_aim.y, 1e-4F),
           "the hitchance and the expected optima differ in an asymmetric scene");

    const auto check = [&](AimObjective objective, const Vec2& expected_aim_point,
                           float expected_value) {
      SolveInput solved = input;
      solved.objective = objective;
      const auto result = cs2::aim::solve(solved);
      expect(result.valid, "the objective solves");
      expect(near(result.aim.x, expected_aim_point.x, 1e-4F) &&
                 near(result.aim.y, expected_aim_point.y, 1e-4F),
             "the objective's aim is the field's own optimum");
      expect(near(result.value, expected_value, 1e-3F), "the objective's value is the field's");
      expect(near(result.hitchance, field.hit_probability(result.aim, input.spread_sigma), 1e-4F),
             "the objective's hitchance is the field's Gaussian");
      expect(!result.monte_carlo.has_value(),
             "the other objectives produce no Monte-Carlo verdict");
      expect(!result.monte_carlo_refusal.has_value(), "the other objectives do not refuse");
    };

    check(AimObjective::damage, damage_aim, field.value_at(damage_aim));
    check(AimObjective::hitchance, hitchance_aim, field.value_at(hitchance_aim));
    check(AimObjective::expected, expected_aim, field.value_at(expected_aim));
    check(AimObjective::pellets, pellets_aim,
          field.value_at_pellets(pellets_aim, input.pellet_offsets));

    // --- the fallback's identity (the Monte-Carlo's refusal) ----------------
    // The Monte-Carlo objective falls back to the **`damage`** objective when
    // the core refuses (`solver.cpp`), and the spec asks for a *predictable*
    // fallback. In the symmetric scene above every objective's optimum was the
    // same point, so a fallback to *any* of them looked the same; here the
    // damage optimum and the margin optimum are different points, so the
    // fallback's identity is pinned.
    SolveInput refused = input;
    refused.objective = AimObjective::monte_carlo;
    refused.weapon.weapon_type = cs2::aim::WEAPON_TYPE_SHOTGUN;
    const auto refused_result = cs2::aim::solve(refused);
    expect(refused_result.valid, "the refused solve still produces an aim");
    expect(!refused_result.monte_carlo.has_value(), "the refused solve has no verdict");
    expect(
        refused_result.monte_carlo_refusal.has_value() &&
            *refused_result.monte_carlo_refusal == cs2::aim::MonteCarloError::unsupported_pattern,
        "the refusal names the unmodelled pattern");
    expect(near(refused_result.aim.x, damage_aim.x, 1e-4F) &&
               near(refused_result.aim.y, damage_aim.y, 1e-4F),
           "the refusal falls back to the damage objective's aim");
    expect(!near(refused_result.aim.x, margin_aim.x, 1e-4F) ||
               !near(refused_result.aim.y, margin_aim.y, 1e-4F),
           "the refusal does not fall back to the hitchance objective's aim");

    // The **search's** own refusal (an invalid sample count) falls back the
    // same way: the aim is the `damage` objective's, not the hitchance one's.
    SolveInput refused_search = input;
    refused_search.objective = AimObjective::monte_carlo;
    refused_search.monte_carlo_samples = 0;
    const auto refused_search_result = cs2::aim::solve(refused_search);
    expect(refused_search_result.monte_carlo_refusal.has_value() &&
               *refused_search_result.monte_carlo_refusal ==
                   cs2::aim::MonteCarloError::invalid_samples,
           "the search's refusal names the invalid sample count");
    expect(near(refused_search_result.aim.x, damage_aim.x, 1e-4F) &&
               near(refused_search_result.aim.y, damage_aim.y, 1e-4F),
           "the search's refusal falls back to the damage objective's aim");
  }

  if (failures == 0) {
    std::printf("aim-solver: ok\n");
    return 0;
  }
  return 1;
}
