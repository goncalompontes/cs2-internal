// Unit tests for the engine penetration / damage model (docs/re/penetration.md).
//
// The pure model is pinned to hand-computed numbers from the CS2
// `HandleBulletPenetration` (`client.dll` `FUN_1808a1820`): the combined modifier is
// the **min** of the crossed surfaces' distance modifiers, with the material special
// cases and the thickness<6 condition; a surface < 0.1 blocks the shot.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "cs2/aim/heatmap.hpp"
#include "cs2/aim/occlusion.hpp"
#include "cs2/aim/penetration.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

// A wall: `dist` is the surface `bulletPenetrationDistanceModifier`, `material` the
// `CHAR_TEX_*` gamematerial (both from the live surface table).
auto layer(float thickness, float dist = 1.0F, int material = -1)
    -> cs2::aim::PenetrationLayer {
  cs2::aim::PenetrationLayer l;
  l.thickness = thickness;
  l.enter.penetration_modifier = dist;
  l.enter.material = material;
  l.exit = l.enter;
  return l;
}

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

auto head_capsule(float x) -> cs2::aim::Hitbox {
  cs2::aim::Hitbox hitbox;
  hitbox.is_capsule = true;
  hitbox.capsule =
      cs2::aim::Capsule{cs2::game::Vec3{x, 0.0F, 0.0F}, cs2::game::Vec3{x, 0.0F, 4.0F}, 4.0F};
  hitbox.group = 1;
  hitbox.multiplier = 4.0F;
  return hitbox;
}

}  // namespace

auto main() -> int {
  using cs2::aim::PenetrationLayer;
  using cs2::aim::PenetrationParams;

  // --- the per-wall loss (CS2 FUN_1808a1820) --------------------------------
  // Neutral surface (dist 1.0), penetration 1.0, zero thickness:
  //   inverse = 1/1 = 1; power = (3/1)*1.25 = 3.75
  //   lost    = 0 + 3.75*1*3 + 0.16*100 = 27.25  ->  72.75
  {
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F));
    expect(s.has_value() && near(*s, 72.75F), "neutral wall: 100 -> 72.75");
    const auto thick = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(10.0F));
    expect(thick.has_value() && near(*thick, 68.5833F), "thickness loss = t^2/24");
  }

  // Concrete ('C' 67, dist 0.5): inverse = 2; lost = 3.75*2*3 + 16 = 38.5 -> 61.5.
  {
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F, 0.5F, 67));
    expect(s.has_value() && near(*s, 61.5F), "concrete distance modifier 0.5");
  }

  // Grate ('G' 71, dist 0.95) with a sub-6-unit span: entry 0.05, combined 3.0.
  //   lost = 0 + 3.75*(1/3)*3 + 0.05*100 = 8.75 -> 91.25
  {
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F, 0.95F, 71));
    expect(s.has_value() && near(*s, 91.25F), "grate: entry 0.05, combined 3.0");
  }

  // Wood ('W' 87) overrides the combined modifier to 3.0 (same material).
  //   lost = 3.75*(1/3)*3 + 16 = 19.75 -> 80.25
  {
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F, 0.9F, 87));
    expect(s.has_value() && near(*s, 80.25F), "wood: combined forced to 3.0");
  }

  // Plastic ('L' 76) overrides the combined modifier to 2.0.
  //   lost = 3.75*0.5*3 + 16 = 21.625 -> 78.375
  {
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F, 0.7F, 76));
    expect(s.has_value() && near(*s, 78.375F), "plastic: combined forced to 2.0");
  }

  // The combined modifier is the MIN over the crossed surfaces (not the average).
  //   enter dist 0.5, exit dist 0.9, same material -> combined 0.5 -> inverse 2.
  {
    PenetrationLayer l = layer(0.0F, 0.5F, 67);
    l.exit.penetration_modifier = 0.9F;
    const auto s = cs2::aim::penetration_layer_damage(100.0F, 1.0F, l);
    expect(s.has_value() && near(*s, 61.5F), "combined modifier is the min");
  }

  // A surface whose distance modifier is < 0.1 cannot be penetrated at all.
  {
    expect(!cs2::aim::penetration_layer_damage(100.0F, 1.0F, layer(0.0F, 0.05F, 67)).has_value(),
           "a < 0.1 distance modifier blocks the shot");
  }

  // A wall that absorbs the whole bullet is blocked, and a weak shot too.
  {
    expect(!cs2::aim::penetration_layer_damage(20.0F, 1.0F, layer(50.0F)).has_value(),
           "a thick wall drops the shot");
    expect(!cs2::aim::penetration_layer_damage(5.0F, 1.0F, layer(0.0F)).has_value(),
           "the entry cost alone can drop a weak shot");
  }

  // --- the shot-level walk --------------------------------------------------
  {
    const PenetrationParams params{.weapon_penetration = 1.0F,
                                   .max_penetrations = 4,
                                   .max_total_thickness = 90.0F,
                                   .max_trace_length = 3000.0F};
    const PenetrationLayer one[1] = {layer(10.0F)};
    expect(near(cs2::aim::penetration_after_walls(100.0F, params, one), 68.5833F, 1e-2F),
           "one wall survives at the per-wall value");

    const PenetrationLayer two[2] = {layer(10.0F), layer(10.0F)};
    expect(near(cs2::aim::penetration_after_walls(100.0F, params, two), 42.1933F, 1e-2F),
           "two walls compound");

    PenetrationParams one_pen = params;
    one_pen.max_penetrations = 1;
    expect(near(cs2::aim::penetration_after_walls(100.0F, one_pen, two), 0.0F),
           "the penetration budget caps the wall count");

    const PenetrationLayer ninety_one[1] = {layer(91.0F)};
    expect(near(cs2::aim::penetration_after_walls(100000.0F, params, ninety_one), 0.0F),
           "the ~90-unit crossed-thickness cap blocks the shot");

    PenetrationLayer far = layer(10.0F);
    far.entry = 3100.0F;
    const PenetrationLayer far_one[1] = {far};
    expect(near(cs2::aim::penetration_after_walls(100.0F, params, far_one), 0.0F),
           "a wall beyond 3000 units stops the shot");
    expect(near(cs2::aim::penetration_after_walls(100.0F, params, {}), 100.0F),
           "the trace-length cap does not zero an unobstructed shot");
  }

  // --- heatmap integration: engine vs simple, base_value bookkeeping ---------
  {
    using cs2::aim::Basis;
    using cs2::aim::HeatmapOptions;
    using cs2::aim::PenetrationModel;
    using cs2::aim::TargetPose;
    using cs2::aim::WeaponProfile;
    using cs2::game::Vec3;

    const Basis basis = Basis::world();
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon =
        WeaponProfile{.damage = 100.0F, .range_modifier = 1.0F, .penetration = 1.0F,
                      .headshot_multiplier = 4.0F};
    options.penetration_params.weapon_penetration = 1.0F;
    const auto wall = cs2::aim::project_occluder(
        Vec3{}, basis, box_vertices(50, 0, 0, 0.25F, 1.0F, 1.0F),
        box_planes(50, 0, 0, 0.25F, 1.0F, 1.0F), 1.0F);
    expect(wall.has_value(), "the wall projects");
    if (wall.has_value()) {
      options.occlusion.push_back(*wall);
    }

    options.penetration = PenetrationModel::engine;
    const auto engine = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    float engine_lo = 1e9F;
    float engine_hi = -1e9F;
    float base_hi = -1e9F;
    for (const auto& cell : engine.cells) {
      engine_lo = std::min(engine_lo, cell.value);
      engine_hi = std::max(engine_hi, cell.value);
      base_hi = std::max(base_hi, cell.base_value);
    }
    expect(near(base_hi, 400.0F, 1.0F), "base_value is the unoccluded damage");
    expect(near(engine_hi, 400.0F, 1.0F), "the uncovered part keeps full damage");
    expect(engine_lo < engine_hi, "the wall attenuates the covered part");
    expect(engine_lo > 0.0F, "a thin wall does not block the shot");
    for (const auto& cell : engine.cells) {
      expect(cell.value <= cell.base_value + 1e-3F, "no cell exceeds its base damage");
    }

    options.penetration = PenetrationModel::simple;
    const auto simple = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    float simple_lo = 1e9F;
    for (const auto& cell : simple.cells) {
      simple_lo = std::min(simple_lo, cell.value);
    }
    expect(!near(simple_lo, engine_lo, 1.0F), "the engine model differs from the simple one");
  }

  if (failures == 0) {
    std::printf("aim-penetration: ok\n");
    return 0;
  }
  std::printf("aim-penetration: %d failure(s)\n", failures);
  return 1;
}
