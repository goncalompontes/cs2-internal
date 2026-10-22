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

  // Total number of events ever appended.
  [[nodiscard]] auto count() const noexcept -> std::uint64_t;

  // Copy up to `limit` of the most recent events, oldest first, into `out`.
  // Returns the number written (bounded by `k_capacity` and `limit`).
  auto recent(EventEntry* out, std::size_t limit) const noexcept -> std::size_t;

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
