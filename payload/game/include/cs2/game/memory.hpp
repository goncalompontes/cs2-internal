#pragma once

// Safe in-process memory reads.
//
// The payload is manually mapped without registered exception tables, so an
// invalid pointer must never fault: reads go through the OS primitive
// (`ReadProcessMemory`), which fails cleanly on an unmapped address.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cs2::game {

// Read `count` bytes from `address` in the current process. Returns false when
// the address is invalid or the read is short.
[[nodiscard]] bool read_bytes(std::uintptr_t address, void* out, std::size_t count) noexcept;

// Write `count` bytes to `address` in the current process. Returns false when
// the address is not writable.
//
// The write goes through the OS primitive for the same reason the read does:
// the payload is manually mapped without registered exception tables, so an
// invalid pointer must never fault. A read-only page fails the write instead.
[[nodiscard]] bool write_bytes(std::uintptr_t address, const void* data,
                               std::size_t count) noexcept;

template <typename T>
[[nodiscard]] bool read(std::uintptr_t address, T& out) noexcept {
  return read_bytes(address, &out, sizeof(T));
}

// Read a NUL-terminated string, bounded by `max_length`. Returns the empty
// string if the address is invalid.
[[nodiscard]] std::string read_cstring(std::uintptr_t address, std::size_t max_length = 128);

// Read a fixed-size inline `char[N]` field, stopping at the first NUL.
[[nodiscard]] std::string read_inline_string(std::uintptr_t address, std::size_t capacity);

}  // namespace cs2::game
