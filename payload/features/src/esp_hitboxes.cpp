#include <algorithm>
#include <cstddef>
#include <vector>

#include "cs2/aim/reader.hpp"
#include "cs2/features/esp.hpp"
#include "cs2/game/math.hpp"

#include "esp_detail.hpp"

namespace cs2::features::esp_detail {
namespace {

// How long a pawn's studio hitboxes stay fresh, in milliseconds.
//
// Reading a pawn's studio hitboxes is the most expensive read the ESP does:
// the model's hitbox set, the bone remap and the bone array are several
// hundred scattered reads, each of them a `ReadProcessMemory`. The set is
// therefore cached per pawn and refreshed at a bounded rate.
constexpr std::uint64_t k_hitbox_refresh_ms = 50;

// How many pawns may have their hitboxes re-read in one frame. The frames
// that hit the cap keep the set they already had, so the skeleton lags a
// little instead of the frame time exploding.
constexpr int k_max_hitbox_reads_per_frame = 4;

// How many pawns the cache holds. A round's respawn replaces the pawns, so
// the cache is dropped when it grows past this.
constexpr std::size_t k_max_hitbox_entries = 64;

}  // namespace

auto read_capsules(const EspProviders& providers, std::uintptr_t pawn, game::HitboxCapsule* out,
                   int max) -> int {
  if (out == nullptr || max <= 0 || pawn == 0) {
    return 0;
  }
  if (providers.hitboxes != nullptr) {
    const int count = providers.hitboxes(providers.hitboxes_context, pawn, out, max);
    return count > 0 ? (std::min)(count, max) : 0;
  }
  // The fallback: the same model reader the overlay's hitbox provider calls
  // (`Service::read_hitboxes` -> `aim::read_model_hitboxes`), so there is
  // exactly one model reader in the tree. The bone names are not needed here.
  const std::vector<aim::ModelHitbox> boxes = aim::read_model_hitboxes(pawn, /*read_names=*/false);
  int count = 0;
  for (const aim::ModelHitbox& box : boxes) {
    if (count >= max) {
      break;
    }
    out[count].a = box.world_a;
    out[count].b = box.world_b;
    out[count].radius = box.radius;
    out[count].group = box.group;
    ++count;
  }
  return count;
}

auto capsules_for(Frame& frame, std::uintptr_t pawn, int& count) -> const game::HitboxCapsule* {
  count = 0;
  if (pawn == 0) {
    return nullptr;
  }
  HitboxCache& cache = frame.hitboxes;
  HitboxCache::Entry* entry = cache.find(pawn);
  if (entry == nullptr) {
    if (cache.entries.size() >= k_max_hitbox_entries) {
      // The pawns changed (a round ended): start the cache over.
      cache.entries.clear();
    }
    cache.entries.push_back(HitboxCache::Entry{});
    entry = &cache.entries.back();
    entry->pawn = pawn;
  }
  const bool stale = frame.now_ms - entry->stamp_ms >= k_hitbox_refresh_ms;
  if (entry->count == 0 || stale) {
    if (cache.reads < k_max_hitbox_reads_per_frame) {
      ++cache.reads;
      entry->count = read_capsules(frame.providers, pawn, entry->capsules.data(),
                                   static_cast<int>(entry->capsules.size()));
      entry->stamp_ms = frame.now_ms;
    }
  }
  count = entry->count;
  return entry->capsules.data();
}

}  // namespace cs2::features::esp_detail
