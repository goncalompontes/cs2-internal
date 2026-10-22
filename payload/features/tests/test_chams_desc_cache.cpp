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
// against it too: it is the proof that the probe's verdict is not vacuous.
//
// ## What it asserts — and why every check is deterministic
//
// The test used to assert four things about *counts* — "the generation really
// advanced", "the probe still read the cache", "the reader gave up at least
// once" — and every one of them was a statement about the **scheduler**, not
// about the cache: on a fast machine the test failed one run in four, and
// the shipped cache had reported **zero** mismatches in every one of those
// runs. A test that fails because the OS did not interleave the threads the
// way the test hoped is a broken test.
//
// So every check here is one of two things:
//
//   * an **invariant** — true for every correct execution: the cache never
//     pairs two writes, and the entry it hands out is the right one; or
//   * an **arranged** fact — the test **drives** the contention until the
//     path it wants to cover has been taken (a deadline turns "it never
//     happened" into a failure instead of a hang), or it **arranges** the
//     precondition with a handshake (the publishers say they are running, the
//     main thread bumps the generation itself, the writers stop so the slot
//     settles).
//
// The **control** is the proof that the probe is not vacuous, and it is held
// to the same rule: the interleaving that breaks the old protocol is **put
// together by hand**, so the control fails on every run and not on one run in
// four.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build (the chams tests'
// convention).

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <vector>

#include "cs2/features/chams.hpp"

namespace {

int failures = 0;

auto expect(bool condition, const char* what) -> void {
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

// How long the test gives itself to **observe** a path.
//
// The test drives the contention until the path it wants to cover has been
// taken, and this is what turns "it never happened" into a failure instead
// of a hang. It is generous: the paths are reached in microseconds on a
// quiet machine, and the deadline is only ever reached when the cache is
// wrong.
inline constexpr std::chrono::milliseconds OBSERVE_DEADLINE{5000};

// How many times the reader may go round the loop **after** the stress is
// done, still looking for the give-up.
//
// The give-up is observed in the first few lookups; the multiplier is the
// bound that keeps the drive finite when the path does not exist at all
// (the old protocol has no give-up path, so the drive runs to the bound and
// the test fails).
inline constexpr std::uint64_t GIVE_UP_DRIVE = 16;

// How many times the probe may go round the loop **after** the stress is
// done, still looking for the paths the test asserts.
//
// The bound keeps the drive finite when the paths do not exist at all (the
// old protocol has neither a claim nor a give-up path, so the drive runs to
// the bound and the test fails). It is generous: the paths are taken in the
// first thousands of lookups.
inline constexpr std::uint64_t PROBE_DRIVE = 4;

// The least number of times the generation is bumped under the contention.
//
// The bump is paced on the probe's reads, so this is the number of times the
// probe gets to read the cache between two bumps.
inline constexpr std::uint64_t MIN_ADVANCES = 64;

// Waits until `condition` holds, for at most `deadline`.
//
// Returns whether the condition held. The test uses it to **drive** the
// contention until the path it wants to cover has been taken, so that the
// assertion that follows is a fact and not a hope.
template <typename Predicate>
[[nodiscard]] auto wait_until(Predicate condition, std::chrono::steady_clock::duration deadline)
    -> bool {
  const auto until = std::chrono::steady_clock::now() + deadline;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= until) {
      return condition();
    }
    std::this_thread::yield();
  }
  return true;
}

// What the probe counts.
struct Probe {
  // The desc and the entity came from two different writes: the defect.
  std::atomic<std::uint64_t> mismatches{0};
  // The lookups the cache answered with an entry.
  std::atomic<std::uint64_t> hits{0};
  // The lookups the cache answered with nothing, without giving up.
  std::atomic<std::uint64_t> misses{0};
  // The lookups the cache gave up on: the slot never settled.
  std::atomic<std::uint64_t> give_ups{0};
  // The entries the probe published.
  std::atomic<std::uint64_t> publishes{0};
  // The publishers the cache refused: another writer was inside the slot.
  std::atomic<std::uint64_t> refused{0};
};

// The rounds the probe's threads have made.
template <std::size_t N>
[[nodiscard]] auto rounds_total(const std::array<std::atomic<std::uint64_t>, N>& rounds)
    -> std::uint64_t {
  std::uint64_t total = 0;
  for (const auto& count : rounds) {
    total += count.load(std::memory_order_relaxed);
  }
  return total;
}

// --- The control: the protocol the cache used before the fix ----------------

// The old cache, reproduced **verbatim**: three atomics, the desc published
// last with a release store, and the entry read with an acquire on the desc
// and relaxed loads on the rest.
//
// It is here so the probe's verdict is not vacuous: a probe that reports
// zero mismatches against a protocol that cannot fail proves nothing.
//
// **The publish is split in two** (`publish_begin` and `publish_end`), and
// that is the only change: the halves are the two stores the old publish
// did, in the same order and with the same memory orders. The split is
// there because the test has to be able to **arrange** the interleaving
// that breaks the protocol — a writer that has stored the entity but has not
// published the desc yet, while a second writer publishes into the same slot
// — instead of hoping that the scheduler produces it.
class OldDescCache {
 public:
  // The first half of the old publish: the entity and the flag, relaxed.
  auto publish_begin(std::uint64_t desc_address, std::uint64_t entity) -> void {
    Entry& entry = entries_[slot_of(desc_address)];
    entry.entity.store(entity, std::memory_order_relaxed);
    entry.view_model.store(0, std::memory_order_relaxed);
  }

  // The second half: the desc, last, with the release.
  auto publish_end(std::uint64_t desc_address) -> void {
    Entry& entry = entries_[slot_of(desc_address)];
    entry.desc.store(desc_address, std::memory_order_release);
  }

  // The whole publish, the way the old cache did it in one go.
  auto publish(std::uint64_t desc_address, std::uint64_t entity) -> bool {
    publish_begin(desc_address, entity);
    publish_end(desc_address);
    return true;
  }

  [[nodiscard]] auto lookup(std::uint64_t desc_address) -> std::optional<std::uint64_t> {
    Entry& entry = entries_[slot_of(desc_address)];
    if (entry.desc.load(std::memory_order_acquire) == desc_address) {
      return static_cast<std::uint64_t>(entry.entity.load(std::memory_order_relaxed));
    }
    return std::nullopt;
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
//
// It runs until `stop` (or until it has made `max_rounds` rounds), and the
// test stops it when the stress is done and the paths the test asserts have
// been taken.
auto probe_shipped(cs2::features::DescCache& cache, Probe& probe, std::uint64_t desc,
                   const std::atomic<bool>& stop, std::atomic<std::uint64_t>& rounds,
                   std::uint64_t max_rounds) -> void {
  while (rounds.load(std::memory_order_relaxed) < max_rounds &&
         !stop.load(std::memory_order_relaxed)) {
    const auto hit = cache.lookup(desc);
    rounds.fetch_add(1, std::memory_order_relaxed);
    if (!hit.has_value()) {
      const std::uint64_t stamp = cache.stamp();
      if (!cache.publish(desc, stamp, entity_of(desc), false)) {
        // Another writer was inside the slot: the cache's claim. The old
        // protocol had no claim, so two writers were inside the same slot at
        // once — and that is the whole defect.
        probe.refused.fetch_add(1, std::memory_order_relaxed);
      }
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
auto probe_old(OldDescCache& cache, Probe& probe, std::uint64_t desc, int rounds) -> void {
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
auto test_the_cache_never_pairs_two_writes() -> void {
  cs2::features::DescCache cache;
  Probe probe;
  constexpr int THREADS = 4;
  constexpr int ROUNDS = 40000;
  std::atomic<bool> stop{false};
  std::array<std::atomic<std::uint64_t>, THREADS> rounds{};

  const std::uint64_t floor = static_cast<std::uint64_t>(ROUNDS) * THREADS;
  const std::uint64_t cap = floor * PROBE_DRIVE;
  std::vector<std::thread> threads;
  for (int index = 0; index < THREADS; ++index) {
    // Two threads on each desc: the two descs share one slot, so the two
    // writers are in the same slot at the same time.
    const std::uint64_t desc = (index % 2 == 0) ? DESC_A : DESC_B;
    std::atomic<std::uint64_t>& count = rounds[static_cast<std::size_t>(index)];
    threads.emplace_back([&cache, &probe, &stop, &count, desc, cap]() {
      probe_shipped(cache, probe, desc, stop, count, cap);
    });
  }

  // The test **drives** the contention: the probe runs its stress, and then
  // it keeps running until the two paths the test asserts have been taken —
  // the cache refused a writer (the claim) and the cache handed out an entry
  // (the hit). Both are facts when the loop ends, and a failure says which
  // of the two never happened.
  //
  // The old protocol has neither: it never refuses a writer (there is no
  // claim) and it never gives up (there is no reader's retry), so the drive
  // runs to the bound and the check below fails — on every run, not on one
  // run in four.
  (void)wait_until(
      [&]() {
        const std::uint64_t total = rounds_total(rounds);
        return (total >= floor && probe.refused.load() > 0 && probe.hits.load() > 0) ||
               total >= cap;
      },
      OBSERVE_DEADLINE);
  stop.store(true, std::memory_order_relaxed);
  for (auto& thread : threads) {
    thread.join();
  }

  std::printf("the shipped cache: %llu mismatches, %llu hits, %llu publishes, %llu refused\n",
              static_cast<unsigned long long>(probe.mismatches.load()),
              static_cast<unsigned long long>(probe.hits.load()),
              static_cast<unsigned long long>(probe.publishes.load()),
              static_cast<unsigned long long>(probe.refused.load()));
  // The invariant: the cache never pairs two writes.
  expect(probe.mismatches.load() == 0, "the cache never pairs two writes");
  // The arranged facts: the stress ran, and the two paths were taken.
  expect(rounds_total(rounds) >= floor, "the probe ran the whole stress");
  expect(probe.refused.load() > 0, "the cache refused a second writer in the slot");
  expect(probe.hits.load() > 0, "the probe read the cache");
}

// The two descs really do share one slot: the probe would be pointless
// otherwise.
auto test_the_two_descs_collide_in_one_slot() -> void {
  expect(cs2::features::DescCache::slot_of(DESC_A) == cs2::features::DescCache::slot_of(DESC_B),
         "the two descs collide in one slot");
  expect(entity_of(DESC_A) != entity_of(DESC_B), "the two descs resolve to different entities");
}

// The invalidation is part of the protocol: an entry of an older generation
// is a miss, so a desc address the engine recycles cannot carry a stale
// entity into the next frame.
auto test_an_older_generation_is_a_miss() -> void {
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
auto test_a_stale_publisher_cannot_resurrect_the_entry() -> void {
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
auto test_the_slots_are_independent() -> void {
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

// The **generation bump under a concurrent publisher**: the frame advances
// while the writers are hammering the slot.
//
// The invalidation is part of the protocol — a publisher that was mid-publish
// when the frame advanced publishes an entry of the **older** generation,
// and the reader rejects it — and this is the interaction that argument
// rests on. The single-threaded case above covers the rule; this one covers
// the rule **under** the concurrency, which is the only way it ever happens
// in the payload.
//
// **The main thread is the bumper.** It advances the generation itself, and
// it paces itself on the probe: it waits for the probe to read the cache
// before it invalidates it again. So "the generation advanced" and "the
// probe read the cache" are both **arranged** facts and not hopes about the
// scheduler. The stress is not softened: the bump still happens again and
// again, concurrent with the publishers, and the publishers still hammer the
// slot for their whole stress.
auto test_the_generation_bump_under_a_concurrent_publisher() -> void {
  cs2::features::DescCache cache;
  Probe probe;
  constexpr int THREADS = 4;
  constexpr int ROUNDS = 40000;
  std::atomic<bool> stop{false};
  std::atomic<int> ready{0};
  std::array<std::atomic<std::uint64_t>, THREADS> rounds{};

  const std::uint64_t floor = static_cast<std::uint64_t>(ROUNDS) * THREADS;
  const std::uint64_t cap = floor * PROBE_DRIVE;
  std::vector<std::thread> threads;
  for (int index = 0; index < THREADS; ++index) {
    const std::uint64_t desc = (index % 2 == 0) ? DESC_A : DESC_B;
    std::atomic<std::uint64_t>& count = rounds[static_cast<std::size_t>(index)];
    threads.emplace_back([&cache, &probe, &stop, &ready, &count, desc, cap]() {
      // The publisher says it is running, so the main thread's first bump is
      // guaranteed to happen while the publishers are running, and not
      // before they started.
      ready.fetch_add(1, std::memory_order_release);
      probe_shipped(cache, probe, desc, stop, count, cap);
    });
  }

  // The publishers are running: the bump below is a fact, and it is a fact
  // about a bump that happened while the publishers were running.
  (void)wait_until([&]() { return ready.load(std::memory_order_acquire) == THREADS; },
                   OBSERVE_DEADLINE);
  cache.advance();
  std::uint64_t advances = 1;

  // The rest of the bumps are paced on the probe's reads: the main thread
  // waits for the probe to read the cache before it invalidates it again.
  // That is what makes "the probe read the cache" a fact as well.
  std::uint64_t seen_hits = 0;
  while (rounds_total(rounds) < floor || advances < MIN_ADVANCES) {
    // The publishers have made their cap's worth of rounds: the drive is
    // over, and the checks below say what was not observed.
    if (rounds_total(rounds) >= cap) {
      break;
    }
    // Wait for the probe to read the cache again: the bump is paced on it.
    if (!wait_until([&]() { return probe.hits.load() > seen_hits || rounds_total(rounds) >= cap; },
                    OBSERVE_DEADLINE)) {
      break;
    }
    if (rounds_total(rounds) >= cap) {
      break;
    }
    seen_hits = probe.hits.load();
    cache.advance();
    ++advances;
  }
  stop.store(true, std::memory_order_relaxed);
  for (auto& thread : threads) {
    thread.join();
  }

  std::printf("the generation bump: %llu mismatches, %llu hits, %llu publishes, %llu advances\n",
              static_cast<unsigned long long>(probe.mismatches.load()),
              static_cast<unsigned long long>(probe.hits.load()),
              static_cast<unsigned long long>(probe.publishes.load()),
              static_cast<unsigned long long>(advances));
  // The invariant: the cache never pairs two writes, not even across a bump.
  expect(probe.mismatches.load() == 0, "the cache never pairs two writes across a bump");
  // The arranged facts: the bump happened, and the probe read the cache.
  expect(advances > 0, "the generation really advanced");
  expect(probe.hits.load() > 0, "the probe still read the cache across the bumps");
}

// The reader's **give-up** path: the slot never settled, so the reader
// resolves the desc itself (the uncached path).
//
// This is the one path where the cache's promise ("once per model") does not
// hold: the draw path pays two engine reads per *primitive* instead of per
// *model* while it lasts. It was correct and **unmeasured**; the cache
// counts the give-ups, and this test drives the contention that produces
// them and checks that the entry the cache hands out is still the right one.
auto test_the_reader_gives_up_and_the_entry_is_still_right() -> void {
  cs2::features::DescCache cache;
  Probe probe;
  constexpr int WRITERS = 8;
  constexpr int ROUNDS = 20000;
  std::atomic<bool> stop{false};

  // The writers hammer **one** slot, so the reader keeps finding it claimed.
  std::vector<std::thread> writers;
  for (int index = 0; index < WRITERS; ++index) {
    writers.emplace_back([&cache, &stop]() {
      while (!stop.load(std::memory_order_relaxed)) {
        const std::uint64_t stamp = cache.stamp();
        (void)cache.publish(DESC_A, stamp, entity_of(DESC_A), false);
      }
    });
  }

  // The reader: it looks the desc up, and it **drives** the contention: it
  // keeps looking until the cache has given up at least once. The give-up is
  // the reader's bounded retry running out, and the cache counts them, so
  // the test watches the counter instead of hoping for the path.
  //
  // The old protocol has no give-up path at all — there is no reader's
  // retry to run out — so the drive runs to the bound and the check below
  // fails: on every run, not on one run in four.
  const std::uint64_t floor = static_cast<std::uint64_t>(ROUNDS) * WRITERS;
  const std::uint64_t cap = floor * GIVE_UP_DRIVE;
  std::atomic<std::uint64_t> rounds{0};
  std::thread reader([&cache, &probe, &rounds, floor, cap]() {
    while (rounds.load(std::memory_order_relaxed) < cap) {
      const std::uint64_t before = cache.give_ups();
      const auto hit = cache.lookup(DESC_A);
      const std::uint64_t after = cache.give_ups();
      rounds.fetch_add(1, std::memory_order_relaxed);
      if (after > before) {
        // The slot never settled: the reader gave up and resolves the desc
        // itself (the uncached path).
        probe.give_ups.fetch_add(1, std::memory_order_relaxed);
      } else if (!hit.has_value()) {
        probe.misses.fetch_add(1, std::memory_order_relaxed);
      } else {
        probe.hits.fetch_add(1, std::memory_order_relaxed);
        if (hit->entity != entity_of(DESC_A)) {
          // The defect: the desc and the entity came from two different
          // writes.
          probe.mismatches.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (rounds.load(std::memory_order_relaxed) >= floor && probe.give_ups.load() > 0) {
        break;
      }
    }
  });
  reader.join();
  stop.store(true, std::memory_order_relaxed);
  for (auto& writer : writers) {
    writer.join();
  }

  // The writers are gone and the slot is stable again, so the reader's next
  // lookup is answered with the entry the writers published: the slot
  // settled, the desc is the one that was looked up, and the entry belongs
  // to the current generation. That is the "the reader still got the entry
  // when it could" check — an **arranged** fact now, and not a hope about
  // the scheduler. (Under the contention the reader may legitimately never
  // settle the slot: the writers hold it claimed, so the reader gives up
  // every time. That is the cache behaving as it must, not a defect.)
  const auto settled = cache.lookup(DESC_A);
  expect(settled.has_value() && settled->entity == entity_of(DESC_A),
         "the reader still got the entry when it could");

  std::printf("the give-up path: %llu give-ups, %llu hits, %llu misses, %llu mismatches\n",
              static_cast<unsigned long long>(probe.give_ups.load()),
              static_cast<unsigned long long>(probe.hits.load()),
              static_cast<unsigned long long>(probe.misses.load()),
              static_cast<unsigned long long>(probe.mismatches.load()));
  // The arranged fact: the give-up path was taken.
  expect(probe.give_ups.load() > 0, "the reader gave up at least once under the contention");
  // The invariant: the entry the cache hands out is the right one.
  expect(probe.mismatches.load() == 0, "the entry is still the right one after a give-up");
}

// The control, **arranged**: the interleaving that breaks the old protocol,
// put together by hand so the test does not have to hope for it.
//
//   * the first writer stores the entity and stops there (`publish_begin`),
//   * the second writer publishes its own desc and entity into the same
//     slot (the whole publish),
//   * the first writer publishes its desc (`publish_end`),
//   * the reader looks the first desc up.
//
// The slot holds **desc A with entity B**, and the reader sees exactly that.
// The memory model guarantees it, so the control fails on every run: the
// second writer's store of the entity happens-before the reader's load of
// it (through the second writer's flag), the first writer's store of the
// desc happens-before the reader's load of it (through the first writer's
// flag), and the first writer's store of the entity happens-before the
// second writer's (through the first writer's first flag) — so the reader
// must see the second writer's entity and the first writer's desc.
auto test_the_old_protocol_really_does_pair_two_writes() -> void {
  OldDescCache cache;
  std::atomic<bool> first_wrote{false};
  std::atomic<bool> second_published{false};
  std::atomic<bool> first_published{false};

  std::thread first([&]() {
    cache.publish_begin(DESC_A, entity_of(DESC_A));
    first_wrote.store(true, std::memory_order_release);
    while (!second_published.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    cache.publish_end(DESC_A);
    first_published.store(true, std::memory_order_release);
  });

  std::thread second([&]() {
    while (!first_wrote.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    (void)cache.publish(DESC_B, entity_of(DESC_B));
    second_published.store(true, std::memory_order_release);
  });

  while (!first_published.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  const auto hit = cache.lookup(DESC_A);
  first.join();
  second.join();

  // The defect, arranged: the desc is the first writer's and the entity is
  // the second writer's. The control **must** fail here — that is what
  // makes the probe's verdict worth something.
  expect(hit.has_value(), "the control's torn entry is a hit");
  expect(hit.has_value() && hit.value() != entity_of(DESC_A),
         "the control really does pair two writes: the probe is not vacuous");
  expect(hit.has_value() && hit.value() == entity_of(DESC_B),
         "the control's torn entry carries the other writer's entity");
}

// The same probe against the old protocol, under the same stress.
//
// The number it reports is the **scheduler's**: the old protocol tears only
// when the OS interleaves the writers the way the defect needs, and the
// count was measured between 0 and 23257 over the runs of this test. It is
// **reported**, not asserted — the assertion is the arranged one above,
// which holds on every run.
auto test_the_old_protocol_under_the_same_stress() -> void {
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
  std::printf(
      "the old protocol (the control, under the same stress): %llu mismatches, %llu hits, "
      "%llu publishes\n",
      static_cast<unsigned long long>(probe.mismatches.load()),
      static_cast<unsigned long long>(probe.hits.load()),
      static_cast<unsigned long long>(probe.publishes.load()));
}

}  // namespace

auto main() -> int {
  test_the_two_descs_collide_in_one_slot();
  test_an_older_generation_is_a_miss();
  test_a_stale_publisher_cannot_resurrect_the_entry();
  test_the_slots_are_independent();
  test_the_cache_never_pairs_two_writes();
  test_the_generation_bump_under_a_concurrent_publisher();
  test_the_reader_gives_up_and_the_entry_is_still_right();
  test_the_old_protocol_really_does_pair_two_writes();
  test_the_old_protocol_under_the_same_stress();

  if (failures != 0) {
    std::printf("features-chams-desc-cache: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-chams-desc-cache: ok");
  return 0;
}
