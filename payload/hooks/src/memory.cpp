#include "cs2/hooks/memory.hpp"

#if defined(_WIN32)

#include <windows.h>

namespace cs2::hooks {

auto allocate_executable(std::size_t size) noexcept -> void* {
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void free_executable(void* address, std::size_t /*size*/) noexcept {
  if (address != nullptr) {
    VirtualFree(address, 0, MEM_RELEASE);
  }
}

auto make_writable(void* address, std::size_t size) noexcept -> bool {
  DWORD previous = 0;
  return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous) != 0;
}

}  // namespace cs2::hooks

#else  // POSIX host (tests)

#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>

namespace cs2::hooks {
namespace {

std::uintptr_t page_size() {
  static const std::uintptr_t size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
  return size;
}

std::uintptr_t page_floor(std::uintptr_t value) {
  return value & ~(page_size() - 1);
}

std::uintptr_t page_ceil(std::uintptr_t value) {
  return page_floor(value + page_size() - 1);
}

}  // namespace

void* allocate_executable(std::size_t size) noexcept {
  void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return memory == MAP_FAILED ? nullptr : memory;
}

void free_executable(void* address, std::size_t size) noexcept {
  if (address != nullptr) {
    munmap(address, size);
  }
}

bool make_writable(void* address, std::size_t size) noexcept {
  const auto start = page_floor(reinterpret_cast<std::uintptr_t>(address));
  const auto end = page_ceil(reinterpret_cast<std::uintptr_t>(address) + size);
  return mprotect(reinterpret_cast<void*>(start), end - start,
                  PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

}  // namespace cs2::hooks

#endif
