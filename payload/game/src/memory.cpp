#include "cs2/game/memory.hpp"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace cs2::game {

auto read_bytes(std::uintptr_t address, void* out, std::size_t count) noexcept -> bool {
  if (address == 0 || out == nullptr || count == 0) {
    return false;
  }
#if defined(_WIN32)
  SIZE_T transferred = 0;
  const BOOL ok = ReadProcessMemory(GetCurrentProcess(),
                                    reinterpret_cast<const void*>(address), out, count,
                                    &transferred);
  return ok != 0 && transferred == count;
#else
  std::memcpy(out, reinterpret_cast<const void*>(address), count);
  return true;
#endif
}

auto read_cstring(std::uintptr_t address, std::size_t max_length) -> std::string {
  std::string result;
  if (address == 0 || max_length == 0) {
    return result;
  }
  result.reserve(std::min<std::size_t>(max_length, 64));
  char buffer[64];
  for (std::size_t offset = 0; offset < max_length; offset += sizeof(buffer)) {
    const std::size_t chunk = std::min(sizeof(buffer), max_length - offset);
    if (!read_bytes(address + offset, buffer, chunk)) {
      break;
    }
    for (std::size_t i = 0; i < chunk; ++i) {
      if (buffer[i] == '\0') {
        return result;
      }
      result.push_back(buffer[i]);
    }
  }
  return result;
}

auto read_inline_string(std::uintptr_t address, std::size_t capacity) -> std::string {
  if (address == 0 || capacity == 0) {
    return {};
  }
  std::string result;
  result.resize(capacity);
  if (!read_bytes(address, result.data(), capacity)) {
    return {};
  }
  result.resize(result.find('\0'));
  return result;
}

}  // namespace cs2::game
