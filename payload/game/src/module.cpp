#include "cs2/game/module.hpp"

#include <string>

#include "cs2/game/memory.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::game {

ModuleInfo find_module(std::string_view name) noexcept {
#if defined(_WIN32)
  if (name.empty()) {
    return {};
  }
  const std::string terminated(name);
  const HMODULE handle = GetModuleHandleA(terminated.c_str());
  if (handle == nullptr) {
    return {};
  }
  ModuleInfo info;
  info.base = reinterpret_cast<std::uintptr_t>(handle);

  // `SizeOfImage` lives in the optional header: e_lfanew at +0x3c, then
  // +0x50 within the PE headers.
  std::uint32_t lfanew = 0;
  if (read(info.base + 0x3c, lfanew) && lfanew != 0) {
    std::uint32_t size_of_image = 0;
    if (read(info.base + lfanew + 0x50, size_of_image)) {
      info.size = size_of_image;
    }
  }
  return info;
#else
  (void)name;
  return {};
#endif
}

}  // namespace cs2::game
