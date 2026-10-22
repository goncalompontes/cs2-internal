#pragma once

// Executable memory and page-protection helpers, platform-specific.
//
// Windows uses VirtualAlloc/VirtualProtect; the host (tests) uses mmap/mprotect.

#include <cstddef>

namespace cs2::hooks {

// Allocate `size` bytes of executable memory (RWX). Returns nullptr on failure.
void* allocate_executable(std::size_t size) noexcept;

// Release memory from `allocate_executable`.
void free_executable(void* address, std::size_t size) noexcept;

// Make `[address, address + size)` writable (whole pages). Returns false on
// failure. Used to patch read-only vtables / code.
bool make_writable(void* address, std::size_t size) noexcept;

}  // namespace cs2::hooks
