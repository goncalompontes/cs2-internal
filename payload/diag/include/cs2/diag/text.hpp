#pragma once

// Allocation-free text building for diagnostics.
//
// Exception handlers and the freeze watchdog run while the process is in an
// unknown state, so they must not touch the heap, the CRT or anything that
// takes a lock. `TextBuffer` writes into a caller-owned stack buffer and
// truncates rather than reallocating.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs2::diag {

class TextBuffer {
 public:
  TextBuffer(char* data, std::size_t capacity) noexcept : data_(data), capacity_(capacity) {}

  TextBuffer& operator+=(std::string_view text) noexcept;
  TextBuffer& operator+=(char character) noexcept;

  // Lowercase hex digits, no prefix.
  TextBuffer& hex(std::uintptr_t value) noexcept;
  // Decimal digits.
  TextBuffer& dec(unsigned long long value) noexcept;
  // `module+0xoffset` when `value` lands in a known module, else `0xhex`.
  TextBuffer& address(std::uintptr_t value) noexcept;

  [[nodiscard]] std::string_view view() const noexcept { return {data_, size_}; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

 private:
  char* data_;
  std::size_t capacity_;
  std::size_t size_ = 0;
};

// Record the module ranges `address()` resolves against. Call once, early, from
// a safe context (it enumerates modules); `address()` afterwards only reads the
// cached table.
void capture_modules() noexcept;

}  // namespace cs2::diag
