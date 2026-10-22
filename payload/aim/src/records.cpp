#include "cs2/aim/records.hpp"

#include <algorithm>
#include <cmath>

namespace cs2::aim {

void RecordRing::push(Record record) {
  records_.push_back(std::move(record));
  if (records_.size() > capacity) {
    records_.erase(records_.begin(),
                   records_.begin() + static_cast<std::ptrdiff_t>(records_.size() - capacity));
  }
}

void RecordRing::trim(std::int32_t oldest_tick) {
  // Records are appended in tick order, so the stale ones are a prefix.
  const auto stale = std::find_if(records_.begin(), records_.end(),
                                  [oldest_tick](const Record& record) noexcept {
                                    return record.tick >= oldest_tick;
                                  });
  records_.erase(records_.begin(), stale);
}

auto RecordRing::newest() const noexcept -> const Record* {
  return records_.empty() ? nullptr : &records_.back();
}

auto RecordRing::oldest() const noexcept -> const Record* {
  return records_.empty() ? nullptr : &records_.front();
}

auto RecordRing::nearest(std::int32_t tick) const noexcept -> const Record* {
  const Record* best = nullptr;
  std::int32_t best_distance = 0;
  for (const Record& record : records_) {
    const std::int32_t distance = std::abs(record.tick - tick);
    if (best == nullptr || distance < best_distance) {
      best = &record;
      best_distance = distance;
    }
  }
  return best;
}

auto rewind_tick(std::int32_t local_tick, float max_unlag, float interp) noexcept -> std::int32_t {
  const float unlag = std::min(0.07F, max_unlag * 0.95F);
  const float seconds = unlag + interp + (1.0F / 64.0F) + 0.1F;
  return local_tick - static_cast<std::int32_t>(seconds * 64.0F);
}

}  // namespace cs2::aim
