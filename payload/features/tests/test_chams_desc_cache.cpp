// The desc cache's **concurrency probe** — the acceptance test of T33.
//
// ## The defect it pins
//
// The scene system renders the draw list in **jobs on the tier0 thread pool**
// (`scenesystem.dll` imports `g_pThreadPool` and carries
// `CSceneSystem::Thread_RenderSceneDrawList` and the `sc_disableThreading`
// convar), so **two** threads can be inside the draw detour at once. The
// per-owner re-entrancy guard lets them both in — that is the guard's whole
// job: it refuses a *recursion*, never another job's draw.
//
// The desc cache published its entry with **three separate atomics**, the
// desc last:
//
//   entry.entity.store(entity, relaxed);
//   entry.view_model.store(view_model, relaxed);
//   entry.desc.store(desc_address, release);
//
// That is a protocol that is safe for **one** writer and not for two. Writer
// A stores `entity=A`; writer B stores `entity=B`; writer A stores `desc=A`.
// A reader loads `desc` (acquire) and sees `A` — the acquire synchronises
// with A's release, so A's `entity` store is visible — but the reader's
// `entity` load is relaxed and `entity`'s own modification order now has B
// **after** A, so the reader may legally observe `entity=B`.
//
// **The result is desc A with entity B**: the chams are decided on the wrong
// player (the CT's model chamsed, the T not). The reviewer's probe of the
// exact protocol measured 965–3048 such mismatches per run.
//
// ## What this test does
//
// The probe is the reviewer's: **four threads, two descs that collide in one
// slot**. It drives the **shipped** cache (`cs2::features::DescCache`), not a
// copy of it, so a regression in the real protocol fails this test.
//
// The **old** protocol is reproduced here as well, and the probe is driven
// against it too: the number it reports is the evidence that the probe can
// fail at all. (The number is printed, not asserted: a race that does not
// manifest in one run must not fail the suite. The proof that the probe
// bites is the run against the old protocol in a scratch copy of the tree,
// whose number is recorded in the task's report.)
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build (the chams tests'
// convention).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <vector>

#include "cs2/features/chams.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// The two descs that hash to the **same** slot.
//
// The slot is `(desc >> 4) & 63`: 0x1000 gives 0x100 & 0x3F == 0, and 0x1400
// gives 0x140 & 0x3F == 0 as well. Two descs in one slot are the routine
// case, not an exotic one: the scene system has hundreds of models.
inline constexpr std::uint64_t DESC_A = 0x1000;
inline constexpr std::uint64_t DESC_B = 0x1400;

// The entity a desc resolves to.
//
// The real draw path reads it from the engine; the probe derives it from the
// desc, so a reader that pairs desc A with entity B is caught. The two descs
// resolve to **different** entities, which is the whole point.
[[nodiscard]] auto entity_of(std::uint64_t desc) noexcept -> std::uintptr_t {
  return static_cast<std::uintptr_t>(desc ^ 0x5eed5eed5eed5000ULL);
}

// What the probe counts.
struct Probe {
  std::atomic<std::uint64_t> mismatches{0};
  std::atomic<std::uint64_t> hits{0};
  std::atomic<std::uint64_t> publishes{0};
};

// --- The control: the protocol the cache used before the fix ----------------

// The old cache, reproduced **verbatim**: three atomics, the desc published
// last with a release store, and the entry read with an acquire on the desc
// and relaxed loads on the rest.
//
// It is here so the probe's verdict is not vacuous: a probe that reports
// zero mismatches against a protocol that cannot fail proves nothing.
class OldDescCache {
 public:
  [[nodiscard]] auto lookup(std::uint64_t desc_address) -> std::optional<std::uint64_t> {
    Entry& entry = entries_[slot_of(desc_address)];
    if (entry.desc.load(std::memory_order_acquire) == desc_address) {
      return static_cast<std::uint64_t>(entry.entity.load(std::memory_order_relaxed));
    }
    return std::nullopt;
  }

  auto publish(std::uint64_t desc_address, std::uint64_t entity) -> bool {
    Entry& entry = entries_[slot_of(desc_address)];
    entry.entity.store(entity, std::memory_order_relaxed);
    entry.view_model.store(0, std::memory_order_relaxed);
    entry.desc.store(desc_address, std::memory_order_release);
    return true;
  }

 private:
  struct Entry {
    std::atomic<std::uint64_t> desc{0};
    std::atomic<std::uint64_t> entity{0};
    std::atomic<std::uint8_t> view_model{0};
  };

  static constexpr std::size_t SLOT_COUNT = 64;

  [[nodiscard]] static auto slot_of(std::uint64_t desc_address) noexcept -> std::size_t {
    return static_cast<std::size_t>((desc_address >> 4) & (SLOT_COUNT - 1));
  }

  std::array<Entry, SLOT_COUNT> entries_{};
};

// --- The probe ---------------------------------------------------------------

// One thread of the probe: it looks the desc up, and when the cache does not
// know it, it resolves it and publishes it.
void probe_shipped(cs2::features::DescCache& cache, Probe& probe, std::uint64_t desc, int rounds) {
  for (int round = 0; round < rounds; ++round) {
    const auto hit = cache.lookup(desc);
    if (!hit.has_value()) {
      const std::uint64_t stamp = cache.stamp();
      (void)cache.publish(desc, stamp, entity_of(desc), false);
      probe.publishes.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    probe.hits.fetch_add(1, std::memory_order_relaxed);
    if (hit->entity != entity_of(desc)) {
      // **The defect**: the desc and the entity came from two different
      // writes, so the chams would be decided on the wrong player.
      probe.mismatches.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

// The same probe, against the old protocol.
void probe_old(OldDescCache& cache, Probe& probe, std::uint64_t desc, int rounds) {
  for (int round = 0; round < rounds; ++round) {
    const auto hit = cache.lookup(desc);
    if (!hit.has_value()) {
      (void)cache.publish(desc, entity_of(desc));
      probe.publishes.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    probe.hits.fetch_add(1, std::memory_order_relaxed);
    if (*hit != entity_of(desc)) {
      probe.mismatches.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

// The acceptance test: four threads, two descs colliding in one slot, and
// **zero** mismatches.
void test_the_cache_never_pairs_two_writes() {
  cs2::features::DescCache cache;
  Probe probe;
  constexpr int THREADS = 4;
  constexpr int ROUNDS = 40000;
  std::vector<std::thread> threads;
  for (int index = 0; index < THREADS; ++index) {
    // Two threads on each desc: the two descs share one slot, so the two
    // writers are in the same slot at the same time.
    const std::uint64_t desc = (index % 2 == 0) ? DESC_A : DESC_B;
    threads.emplace_back([&cache, &probe, desc]() { probe_shipped(cache, probe, desc, ROUNDS); });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  std::printf("the shipped cache: %llu mismatches, %llu hits, %llu publishes\n",
              static_cast<unsigned long long>(probe.mismatches.load()),
              static_cast<unsigned long long>(probe.hits.load()),
              static_cast<unsigned long long>(probe.publishes.load()));
  expect(probe.mismatches.load() == 0, "the cache never pairs two writes");
  expect(probe.hits.load() > 0, "the probe actually read the cache");
}

// The control: the same probe against the old protocol. The number is
// printed, not asserted (a race that does not manifest in one run must not
// fail the suite), but it is the evidence that the probe can fail.
void test_the_old_protocol_is_the_control() {
  OldDescCache cache;
  Probe probe;
  constexpr int THREADS = 4;
  constexpr int ROUNDS = 40000;
  std::vector<std::thread> threads;
  for (int index = 0; index < THREADS; ++index) {
    const std::uint64_t desc = (index % 2 == 0) ? DESC_A : DESC_B;
    threads.emplace_back([&cache, &probe, desc]() { probe_old(cache, probe, desc, ROUNDS); });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  std::printf("the old protocol (the control): %llu mismatches, %llu hits, %llu publishes\n",
              static_cast<unsigned long long>(probe.mismatches.load()),
              static_cast<unsigned long long>(probe.hits.load()),
              static_cast<unsigned long long>(probe.publishes.load()));
  expect(probe.hits.load() > 0, "the control actually read the cache");
}

// The two descs really do share one slot: the probe would be pointless
// otherwise.
void test_the_two_descs_collide_in_one_slot() {
  expect(cs2::features::DescCache::slot_of(DESC_A) == cs2::features::DescCache::slot_of(DESC_B),
         "the two descs collide in one slot");
  expect(entity_of(DESC_A) != entity_of(DESC_B), "the two descs resolve to different entities");
}

// The invalidation is part of the protocol: an entry of an older generation
// is a miss, so a desc address the engine recycles cannot carry a stale
// entity into the next frame.
void test_an_older_generation_is_a_miss() {
  cs2::features::DescCache cache;
  const std::uint64_t stamp = cache.stamp();
  (void)cache.publish(DESC_A, stamp, entity_of(DESC_A), false);
  const auto hit = cache.lookup(DESC_A);
  expect(hit.has_value(), "the entry is a hit in its own generation");
  expect(hit.has_value() && hit->entity == entity_of(DESC_A),
         "the entry is the one that was published");

  // A new frame: the entry belongs to the older generation, so it is a miss.
  cache.advance();
  expect(!cache.lookup(DESC_A).has_value(), "an entry of an older generation is a miss");
}

// A publisher that was mid-publish when the frame advanced cannot resurrect
// the stale entry: it publishes the **older** generation, and the reader
// rejects it.
void test_a_stale_publisher_cannot_resurrect_the_entry() {
  cs2::features::DescCache cache;
  const std::uint64_t old_stamp = cache.stamp();
  // The frame moves on while the publisher is thinking.
  cache.advance();
  (void)cache.publish(DESC_A, old_stamp, entity_of(DESC_A), false);
  expect(!cache.lookup(DESC_A).has_value(), "a stale publisher does not resurrect the entry");
  // The current generation's publisher does.
  (void)cache.publish(DESC_A, cache.stamp(), entity_of(DESC_A), false);
  const auto hit = cache.lookup(DESC_A);
  expect(hit.has_value() && hit->entity == entity_of(DESC_A),
         "the current generation's publisher is accepted");
}

// A desc in another slot is not disturbed by the collision.
void test_the_slots_are_independent() {
  cs2::features::DescCache cache;
  const std::uint64_t stamp = cache.stamp();
  (void)cache.publish(DESC_A, stamp, entity_of(DESC_A), false);
  const std::uint64_t other = DESC_A + (65ULL << 4);  // the next slot
  expect(cs2::features::DescCache::slot_of(other) != cs2::features::DescCache::slot_of(DESC_A),
         "the other desc is in another slot");
  expect(!cache.lookup(other).has_value(), "the other slot is empty");
  const auto hit = cache.lookup(DESC_A);
  expect(hit.has_value() && hit->entity == entity_of(DESC_A), "the first slot is untouched");
}

}  // namespace

auto main() -> int {
  test_the_two_descs_collide_in_one_slot();
  test_an_older_generation_is_a_miss();
  test_a_stale_publisher_cannot_resurrect_the_entry();
  test_the_slots_are_independent();
  test_the_cache_never_pairs_two_writes();
  test_the_old_protocol_is_the_control();

  if (failures != 0) {
    std::printf("features-chams-desc-cache: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-chams-desc-cache: ok");
  return 0;
}
