#include "cs2/control/event_ring.hpp"

#include <algorithm>
#include <cstring>

namespace cs2::control {
namespace {

// Copy a bounded, NUL-terminated string into `out`.
void copy_cstr(char* out, std::size_t capacity, std::string_view text) noexcept {
  const std::size_t length = text.size() < capacity - 1 ? text.size() : capacity - 1;
  std::memcpy(out, text.data(), length);
  out[length] = '\0';
}

}  // namespace

void EventRing::lock() const noexcept {
  while (lock_.test_and_set(std::memory_order_acquire)) {
    // Spin: the critical section is a bounded copy of one entry.
  }
}

void EventRing::unlock() const noexcept {
  lock_.clear(std::memory_order_release);
}

auto EventRing::oldest_locked(std::uint64_t total) const noexcept -> std::uint64_t {
  const auto retained = total < k_capacity ? total : static_cast<std::uint64_t>(k_capacity);
  return total - retained + 1;  // sequence of the oldest retained event
}

auto EventRing::push(std::string_view name, const EventArg* args, std::size_t arg_count) noexcept
    -> std::uint64_t {
  return push(name, args, arg_count, 0);
}

auto EventRing::push(std::string_view name, const EventArg* args, std::size_t arg_count,
                     std::uint64_t time_ms) noexcept -> std::uint64_t {
  lock();
  const std::uint64_t seq = count_.load(std::memory_order_relaxed) + 1;
  count_.store(seq, std::memory_order_relaxed);
  EventEntry& slot = entries_[(seq - 1) % k_capacity];
  slot.seq = seq;
  slot.time_ms = time_ms;
  copy_cstr(slot.name, sizeof(slot.name), name);
  std::size_t stored = arg_count;
  stored = std::min(stored, EventEntry::k_max_args);
  slot.arg_count = static_cast<std::uint8_t>(stored);
  for (std::size_t i = 0; i < stored; ++i) {
    copy_cstr(slot.args[i].key, sizeof(slot.args[i].key), args[i].key);
    copy_cstr(slot.args[i].value, sizeof(slot.args[i].value), args[i].value);
  }
  unlock();
  return seq;
}

auto EventRing::count() const noexcept -> std::uint64_t {
  return count_.load(std::memory_order_relaxed);
}

auto EventRing::recent(EventEntry* out, std::size_t limit) const noexcept -> std::size_t {
  if (out == nullptr || limit == 0) {
    return 0;
  }
  lock();
  const std::uint64_t total = count_.load(std::memory_order_relaxed);
  std::size_t available = total < k_capacity ? static_cast<std::size_t>(total) : k_capacity;
  available = std::min(available, limit);
  const std::uint64_t begin = total - available;  // skip older entries
  for (std::size_t i = 0; i < available; ++i) {
    out[i] = entries_[(begin + i) % k_capacity];
  }
  unlock();
  return available;
}

auto EventRing::seen_since(std::string_view name, std::uint64_t after, const EventArgMatch* args,
                           std::size_t arg_count) const noexcept -> bool {
  lock();
  const std::uint64_t total = count_.load(std::memory_order_relaxed);
  const std::uint64_t first = oldest_locked(total);
  const std::uint64_t retained = total - first + 1;
  bool found = false;
  for (std::uint64_t i = 0; i < retained && !found; ++i) {
    const EventEntry& entry = entries_[(first - 1 + i) % k_capacity];
    if (entry.seq <= after || name != std::string_view(entry.name)) {
      continue;
    }
    bool matched = true;
    for (std::size_t a = 0; a < arg_count && matched; ++a) {
      bool present = false;
      for (std::size_t e = 0; e < entry.arg_count; ++e) {
        if (args[a].key == std::string_view(entry.args[e].key) &&
            args[a].value == std::string_view(entry.args[e].value)) {
          present = true;
          break;
        }
      }
      matched = present;
    }
    found = matched;
  }
  unlock();
  return found;
}

}  // namespace cs2::control
