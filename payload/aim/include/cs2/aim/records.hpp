#pragma once

// Backtracking: the per-enemy record ring (docs/re/lagcomp.md, docs/ragebot.md).
//
// The server rewinds a target to a past tick for hit registration, clamped to
// `sv_maxunlag` / `sv_maxunlag_player`. The ragebot samples each enemy's
// hitboxes every tick and can fire at any record within the window. This is the
// pure logic; the game wiring samples it per frame.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cs2/aim/hitbox.hpp"

namespace cs2::aim {

// One sampled pose: the tick it was taken, the enemy's simulation time, and the
// hitboxes at that instant.
struct Record {
  std::int32_t tick = 0;
  float simulation_time = 0.0F;
  Vec3 origin{};
  std::vector<Hitbox> hitboxes;
};

// A per-enemy ring of records, oldest first, bounded to the lag-comp window.
struct RecordRing {
  // ~`maxunlag` + latency at 64 tick, plus margin. 32 ticks ~ 0.5 s.
  std::size_t capacity = 32;

  // Append a record (drops the oldest past `capacity`).
  void push(Record record);

  // Drop every record older than `oldest_tick` (the rewind-window floor).
  void trim(std::int32_t oldest_tick);

  [[nodiscard]] auto empty() const noexcept -> bool { return records_.empty(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return records_.size(); }
  [[nodiscard]] auto newest() const noexcept -> const Record*;
  [[nodiscard]] auto oldest() const noexcept -> const Record*;
  [[nodiscard]] auto records() const noexcept -> const std::vector<Record>& { return records_; }

  // The record whose tick is nearest `tick` — the pose the server will rewind to.
  [[nodiscard]] auto nearest(std::int32_t tick) const noexcept -> const Record*;

 private:
  std::vector<Record> records_;
};

// The lag-comp rewind tick for a local tick base — the client's own prediction
// (`docs/re/lagcomp.md`, `FUN_1808a0390`):
//   `tick - (int)((min(0.07, max_unlag*0.95) + interp + 1/64 + 0.1) * 64)`
[[nodiscard]] auto rewind_tick(std::int32_t local_tick, float max_unlag, float interp) noexcept
    -> std::int32_t;

}  // namespace cs2::aim
