#pragma once

// A small fixed-capacity ring of recently observed game events (ADR-0020).
//
// The payload's dispatch hooks run on the game thread and append here while the
// control thread reads it, so every operation is guarded by a short spinlock.
// An append never allocates and cannot fail; names and arguments are truncated
// to fixed bounds. This is platform-independent and built host-side for its
// regression test.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs2::control {

// One captured event argument: a field name and its formatted value.
struct EventArg {
  static constexpr std::size_t k_max_key_length = 31;
  static constexpr std::size_t k_max_value_length = 47;

  char key[k_max_key_length + 1] = {};
  char value[k_max_value_length + 1] = {};
};

// One captured event. Holds no pointer, so a reader may copy it freely.
struct EventEntry {
  static constexpr std::size_t k_max_name_length = 31;
  static constexpr std::size_t k_max_args = 6;

  std::uint64_t seq = 0;  // 1-based; strictly increasing
  // When the event was captured (the payload's millisecond clock). The sound
  // provider turns it into the event's age, so a stale event is not offered
  // as a fresh sound. 0 = the caller did not stamp it.
  std::uint64_t time_ms = 0;
  char name[k_max_name_length + 1] = {};
  std::uint8_t arg_count = 0;
  EventArg args[k_max_args] = {};
};

// A `(key, value)` an event must carry to match.
struct EventArgMatch {
  std::string_view key;
  std::string_view value;
};

class EventRing {
 public:
  // Retained events; older ones are overwritten.
  static constexpr std::size_t k_capacity = 64;

  // Append `name` (with up to `EventEntry::k_max_args` arguments) and return its
  // sequence number (the first event is 1).
  auto push(std::string_view name, const EventArg* args, std::size_t arg_count) noexcept
      -> std::uint64_t;

  // Append `name` and stamp it with the capture time (`time_ms`, the payload's
  // millisecond clock — the same clock the event log dedupes with). The stamp
  // is what lets a reader tell a fresh event from a stale one.
  auto push(std::string_view name, const EventArg* args, std::size_t arg_count,
            std::uint64_t time_ms) noexcept -> std::uint64_t;

  // Total number of events ever appended.
  [[nodiscard]] auto count() const noexcept -> std::uint64_t;

  // Copy up to `limit` of the most recent events, oldest first, into `out`.
  // Returns the number written (bounded by `k_capacity` and `limit`).
  auto recent(EventEntry* out, std::size_t limit) const noexcept -> std::size_t;

  // Visit the retained events, oldest first, while the ring is locked. The
  // visitor returns false to stop the walk; the number of visited events is
  // returned. The lock is held for the whole walk, so the visitor must not
  // call back into the ring (and must not throw: the walk is `noexcept`).
  //
  // This is how a reader consumes the events without copying them: the sound
  // provider reduces the two events it cares about into a small buffer while
  // the ring is locked, instead of copying 64 whole entries.
  template <typename Visitor>
  auto visit(Visitor&& visitor) const noexcept -> std::size_t {
    lock();
    const std::uint64_t total = count_.load(std::memory_order_relaxed);
    const std::uint64_t first = oldest_locked(total);
    const std::uint64_t retained = total - first + 1;
    std::size_t visited = 0;
    for (std::uint64_t i = 0; i < retained; ++i) {
      const EventEntry& entry = entries_[(first - 1 + i) % k_capacity];
      ++visited;
      if (!visitor(entry)) {
        break;
      }
    }
    unlock();
    return visited;
  }

  // True when an event named `name` was appended at or after sequence `after`
  // and carries every `(key, value)` in `args`.
  [[nodiscard]] auto seen_since(std::string_view name, std::uint64_t after,
                                const EventArgMatch* args, std::size_t arg_count) const noexcept
      -> bool;

  [[nodiscard]] auto seen_since(std::string_view name, std::uint64_t after) const noexcept -> bool {
    return seen_since(name, after, nullptr, 0);
  }

 private:
  void lock() const noexcept;
  void unlock() const noexcept;

  // The oldest retained sequence number (1 while fewer than `k_capacity`
  // events have been appended).
  auto oldest_locked(std::uint64_t total) const noexcept -> std::uint64_t;

  mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  std::atomic<std::uint64_t> count_{0};
  EventEntry entries_[k_capacity] = {};
};

}  // namespace cs2::control
