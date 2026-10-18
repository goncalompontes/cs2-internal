#pragma once

// Backtracking: the per-enemy record ring (docs/re/lagcomp.md, docs/ragebot.md).
//
// The server rewinds a target to a past tick for hit registration, clamped to
// `sv_maxunlag` / `sv_maxunlag_player`. The ragebot samples each enemy's
// hitboxes every tick and can fire at any record within the window. This is the
// pure logic; the game wiring samples it per frame.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cs2/aim/hitbox.hpp"

namespace cs2::aim {

// One sampled pose: the **game tick** it was taken at (the local player's
// `m_nTickBase`, `docs/re/lagcomp.md`), the enemy's `m_flSimulationTime`, and the
// hitboxes at that instant. The store trims to the lag-comp window, so the ring
// holds exactly the records the server could rewind to.
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

// A store of per-player record rings, keyed by the player's pawn pointer. This is
// the pure sampling buffer for the heatmap's lag records (`docs/ragebot.md` §8):
// the game wiring appends a pose per player per tick; the heatmap reads them back.
//
// Pure logic with no locking — the caller owns concurrency. The control service
// guards its instance with a mutex because the render thread samples while the
// control thread reads.
class RecordStore {
 public:
  explicit RecordStore(std::size_t per_pawn = 32) : capacity_(per_pawn == 0 ? 1 : per_pawn) {}

  void set_capacity(std::size_t per_pawn) noexcept { capacity_ = per_pawn == 0 ? 1 : per_pawn; }
  [[nodiscard]] auto capacity() const noexcept -> std::size_t { return capacity_; }

  // Append a pose for `pawn` (drops the oldest past `capacity`).
  void push(std::uintptr_t pawn, Record record);

  // Forget pawns not in `pawns` (they left / died), so stale rings do not linger.
  void retain(const std::vector<std::uintptr_t>& pawns);

  // Drop every record at or before `floor_tick` — the lag-comp window floor the
  // client itself trims to (`docs/re/lagcomp.md`, `FUN_180c90b90`). After this the
  // store holds exactly the *valid* backtrack records.
  void trim(std::int32_t floor_tick);

  void clear() noexcept;

  // The records for one pawn, oldest first. The reference is into the store's
  // storage; valid until the next mutation (copy it if it must outlive one).
  [[nodiscard]] auto records(std::uintptr_t pawn) const -> const std::vector<Record>&;

  [[nodiscard]] auto pawns() const -> std::vector<std::uintptr_t>;

  // The newest tick seen across every ring (0 when empty). Ages are measured from it.
  [[nodiscard]] auto newest_tick() const noexcept -> std::int32_t { return newest_tick_; }

 private:
  std::unordered_map<std::uintptr_t, RecordRing> rings_;
  std::size_t capacity_ = 32;
  std::int32_t newest_tick_ = 0;
};

}  // namespace cs2::aim
