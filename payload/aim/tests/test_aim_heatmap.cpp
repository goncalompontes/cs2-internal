// Unit tests for the multi-target damage heatmap and the record store
// (docs/ragebot.md §0, ADR-0022).

#include <cmath>
#include <cstdio>
#include <cstdint>

#include "cs2/aim/heatmap.hpp"
#include "cs2/aim/records.hpp"

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

// A head capsule at `x` along the view axis (the default basis is +X forward).
auto head_capsule(float x, float z) -> cs2::aim::Hitbox {
  cs2::aim::Hitbox hitbox;
  hitbox.is_capsule = true;
  hitbox.capsule = cs2::aim::Capsule{cs2::game::Vec3{x, 0.0F, z}, cs2::game::Vec3{x, 0.0F, z + 4.0F},
                                     4.0F};
  hitbox.group = 1;
  hitbox.multiplier = 4.0F;
  return hitbox;
}

}  // namespace

auto main() -> int {
  using cs2::aim::Basis;
  using cs2::aim::HeatmapOptions;
  using cs2::aim::Record;
  using cs2::aim::RecordStore;
  using cs2::aim::TargetPose;
  using cs2::aim::Vec3;
  using cs2::aim::WeaponProfile;

  const Basis basis{.forward = Vec3{1.0F, 0.0F, 0.0F},
                    .right = Vec3{0.0F, 1.0F, 0.0F},
                    .up = Vec3{0.0F, 0.0F, 1.0F}};
  const WeaponProfile weapon{.damage = 100.0F, .range_modifier = 0.98F};

  // --- one target, one pose ------------------------------------------------
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 7;
    pose.hitboxes.push_back(head_capsule(100.0F, 0.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = weapon;
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);

    expect(map.cells.size() == 1, "one silhouette -> one cell");
    if (!map.cells.empty()) {
      const auto& cell = map.cells.front();
      expect(cell.target == 7, "cell carries the target index");
      expect(cell.hitgroup == 1, "cell carries the hitgroup");
      expect(cell.record == -1, "live pose is record -1");
      expect(near(cell.age, 0.0F), "live pose age is 0");
      const float expected = 100.0F * 4.0F * cs2::aim::range_falloff(0.98F, 100.0F);
      expect(near(cell.value, expected, 0.5F), "head value = damage x 4 x falloff");
      expect(!cell.polygon.empty(), "silhouette has points");
    }
  }

  // --- two targets, records carried ----------------------------------------
  {
    std::vector<TargetPose> poses;
    TargetPose live;
    live.target = 1;
    live.record = -1;
    live.hitboxes.push_back(head_capsule(200.0F, 0.0F));
    poses.push_back(std::move(live));

    TargetPose past;
    past.target = 1;
    past.record = 53;
    past.age = 4.0F;
    past.hitboxes.push_back(head_capsule(200.0F, 0.0F));
    poses.push_back(std::move(past));

    TargetPose other;
    other.target = 2;
    other.record = 51;
    other.age = 6.0F;
    other.hitboxes.push_back(head_capsule(200.0F, 0.0F));
    poses.push_back(std::move(other));

    HeatmapOptions options;
    options.weapon = weapon;
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);

    expect(map.cells.size() == 3, "each pose contributes its own cell");
    bool saw_record = false;
    bool saw_other = false;
    for (const auto& cell : map.cells) {
      if (cell.target == 1 && cell.record == 53 && near(cell.age, 4.0F)) {
        saw_record = true;
      }
      if (cell.target == 2 && cell.record == 51 && near(cell.age, 6.0F)) {
        saw_other = true;
      }
    }
    expect(saw_record, "the lag record is tagged with its id and age");
    expect(saw_other, "a second target's record is tagged independently");
  }

  // --- the chart square clips the silhouette -------------------------------
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    // Straddles the chart edge: the projected circle leaves the square.
    pose.hitboxes.push_back(head_capsule(20.0F, 3.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = weapon;
    const float extent = 0.25F;
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, extent, poses, options);
    expect(!map.cells.empty(), "an off-axis silhouette still yields a cell");
    for (const auto& cell : map.cells) {
      for (const auto& p : cell.polygon.points) {
        expect(std::fabs(p.x) <= extent + 1e-3F, "clipped to u <= extent");
        // The pitch axis is always clipped to +-pi/2 (the chart's pitch bound).
        expect(std::fabs(p.y) <= 1.5707964F + 1e-3F, "clipped to v <= pi/2");
      }
    }
  }

  // --- occluder clips the silhouette ---------------------------------------
  {
    std::vector<TargetPose> poses;
    TargetPose pose;
    pose.target = 1;
    pose.hitboxes.push_back(head_capsule(100.0F, 0.0F));
    poses.push_back(std::move(pose));

    HeatmapOptions options;
    options.weapon = weapon;
    // Keep only the half-plane u <= 0 — the wall hides the right half.
    options.occluders.push_back(cs2::aim::Occluder{cs2::game::Vec2{1.0F, 0.0F}, 0.0F});
    const auto map = cs2::aim::build_heatmap(Vec3{}, basis, 1.0F, poses, options);
    for (const auto& cell : map.cells) {
      for (const auto& p : cell.polygon.points) {
        expect(p.x <= 1e-3F, "occluder trims the silhouette");
      }
    }
  }

  // --- the record store ----------------------------------------------------
  {
    RecordStore store(2);
    store.push(1, Record{.tick = 10, .hitboxes = {}});
    store.push(1, Record{.tick = 11, .hitboxes = {}});
    store.push(1, Record{.tick = 12, .hitboxes = {}});  // capacity 2 -> tick 10 drops
    expect(store.records(1).size() == 2, "ring honours its capacity");
    expect(store.records(1).front().tick == 11, "oldest beyond capacity is dropped");
    expect(store.newest_tick() == 12, "newest tick tracks every push");

    store.push(2, Record{.tick = 12, .hitboxes = {}});
    expect(store.pawns().size() == 2, "two pawns recorded");
    store.retain(std::vector<std::uintptr_t>{2});
    expect(store.records(1).empty(), "retain forgets absent pawns");
    expect(store.records(2).size() == 1, "retain keeps present pawns");

    store.clear();
    expect(store.records(2).empty(), "clear empties the store");
    expect(store.newest_tick() == 0, "clear resets the newest tick");
  }

  if (failures == 0) {
    std::printf("aim-heatmap: ok\n");
    return 0;
  }
  std::printf("aim-heatmap: %d failure(s)\n", failures);
  return 1;
}
