// Unit tests for the backtrack record ring (docs/re/lagcomp.md, docs/ragebot.md).

#include <cstdio>

#include "cs2/aim/records.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto make_record(std::int32_t tick) -> cs2::aim::Record {
  cs2::aim::Record record;
  record.tick = tick;
  record.simulation_time = static_cast<float>(tick) / 64.0F;
  return record;
}

}  // namespace

auto main() -> int {
  using cs2::aim::RecordRing;

  // Capacity: the oldest records fall off.
  {
    RecordRing ring;
    ring.capacity = 2;
    ring.push(make_record(10));
    ring.push(make_record(11));
    ring.push(make_record(12));
    expect(ring.size() == 2, "capacity bounds the ring");
    expect(ring.oldest() != nullptr && ring.oldest()->tick == 11, "oldest after overflow");
    expect(ring.newest() != nullptr && ring.newest()->tick == 12, "newest after overflow");
  }

  // Trim: drop everything older than the window floor.
  {
    RecordRing ring;
    for (std::int32_t tick = 10; tick <= 20; ++tick) {
      ring.push(make_record(tick));
    }
    ring.trim(15);
    expect(ring.size() == 6, "trim keeps the window");
    expect(ring.oldest()->tick == 15, "trim floor");
    expect(ring.newest()->tick == 20, "trim keeps the newest");
  }

  // Nearest: the record closest to a target tick.
  {
    RecordRing ring;
    ring.push(make_record(100));
    ring.push(make_record(105));
    ring.push(make_record(110));
    const auto* record = ring.nearest(104);
    expect(record != nullptr && record->tick == 105, "nearest picks the closest tick");
    expect(ring.nearest(300) != nullptr && ring.nearest(300)->tick == 110, "nearest above range");
    expect(ring.nearest(0) != nullptr && ring.nearest(0)->tick == 100, "nearest below range");
  }

  // The store trims every ring to the lag-comp window (`FUN_180c90b90`).
  {
    cs2::aim::RecordStore store(32);
    for (std::int32_t tick = 100; tick <= 110; ++tick) {
      store.push(7, make_record(tick));
    }
    expect(store.records(7).size() == 11, "the store holds every record before the trim");
    store.trim(105);
    expect(store.records(7).size() == 6, "the store trims to the window floor");
    expect(store.records(7).front().tick == 105, "the trim floor is kept");
    expect(store.records(7).back().tick == 110, "the newest record survives the trim");
  }

  // The rewind-tick formula (docs/re/lagcomp.md).
  {
    // unlag = min(0.07, 0.2*0.95) = 0.07; + interp 0.03 + 1/64 + 0.1 = 0.215625 s
    // -> 13.8 ticks -> 13; 1000 - 13 = 987.
    expect(cs2::aim::rewind_tick(1000, 0.2F, 0.03F) == 987, "rewind tick matches the client formula");
  }

  if (failures == 0) {
    std::printf("aim-records: ok\n");
    return 0;
  }
  return 1;
}
