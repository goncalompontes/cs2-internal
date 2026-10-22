#include "cs2/game/pattern.hpp"
#include "cs2/game/memory.hpp"
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace cs2::game {
#if defined(_WIN32)
auto unique_code_match(ModuleInfo module, const char* pattern) -> std::uintptr_t {
  std::vector<int> bytes;
  const auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
  for (const char* p = pattern; *p; p += 2)
    bytes.push_back(*p == '?' ? -1 : hex(p[0]) * 16 + hex(p[1]));
  IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
  if (!read(module.base, dos) || !read(module.base + dos.e_lfanew, nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections > 96) return 0;
  const auto sections = module.base + dos.e_lfanew + 24 + nt.FileHeader.SizeOfOptionalHeader;
  std::uintptr_t found = 0;
  for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
    IMAGE_SECTION_HEADER section{};
    if (!read(sections + i * sizeof(section), section) ||
        !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const auto size = static_cast<std::size_t>(section.Misc.VirtualSize);
    if (size < bytes.size() || size > 128 * 1024 * 1024 ||
        !module.contains(module.base + section.VirtualAddress + size - 1)) continue;
    std::vector<unsigned char> text(size);
    if (!read_bytes(module.base + section.VirtualAddress, text.data(), size)) continue;
    for (std::size_t offset = 0; offset + bytes.size() <= size; ++offset) {
      bool match = true;
      for (std::size_t n = 0; n < bytes.size(); ++n)
        if (bytes[n] >= 0 && text[offset + n] != bytes[n]) { match = false; break; }
      if (match) {
        if (found) return 0; // ambiguity must never select a function by match order
        found = module.base + section.VirtualAddress + offset;
      }
    }
  }
  return found;
}
#endif
#if !defined(_WIN32)
std::uintptr_t unique_code_match(ModuleInfo, const char*) { return 0; }
#endif
}
