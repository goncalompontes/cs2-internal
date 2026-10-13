#pragma once

// Target module lookup.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs2::game {

struct ModuleInfo {
  std::uintptr_t base = 0;
  std::size_t size = 0;  // PE `SizeOfImage` (0 if unknown)

  [[nodiscard]] bool valid() const noexcept { return base != 0; }

  // True when `address` lies inside the module image.
  [[nodiscard]] bool contains(std::uintptr_t address) const noexcept {
    return size != 0 && address >= base && address < base + size;
  }
};

// Find a loaded module by file name (e.g. "client.dll"). Case-insensitive, as
// the Windows loader treats module names.
[[nodiscard]] ModuleInfo find_module(std::string_view name) noexcept;

}  // namespace cs2::game
