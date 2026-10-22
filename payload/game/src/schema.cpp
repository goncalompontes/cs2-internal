#include "cs2/game/schema.hpp"
#include "cs2/game/memory.hpp"
#include <mutex>
#include <string>
#include <unordered_map>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace cs2::game {
namespace {
#if defined(_WIN32)
std::uintptr_t find_class(const char* name) noexcept {
  __try {
    const auto module = GetModuleHandleW(L"schemasystem.dll");
    if (!module) return 0;
    using Create = void* (*)(const char*, int*);
    const auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateInterface"));
    if (!create) return 0;
    void* system = create("SchemaSystem_001", nullptr);
    if (!system) return 0;
    using Scope = void* (*)(void*, const char*, void*);
    void* scope = reinterpret_cast<Scope>((*reinterpret_cast<void***>(system))[13])(
        system, "client.dll", nullptr);
    if (!scope) return 0;
    using Class = void (*)(void*, std::uintptr_t*, const char*);
    std::uintptr_t info = 0;
    reinterpret_cast<Class>((*reinterpret_cast<void***>(scope))[2])(scope, &info, name);
    return info;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
#endif
}
std::optional<std::uint32_t> schema_field(std::string_view type, std::string_view field) {
#if defined(_WIN32)
  static std::mutex mutex;
  static std::unordered_map<std::string, std::uint32_t> cache;
  const std::scoped_lock lock(mutex);
  const std::string key = std::string(type) + "::" + std::string(field);
  if (const auto it = cache.find(key); it != cache.end()) return it->second;
  const auto info = find_class(std::string(type).c_str());
  std::uint16_t count = 0;
  std::uintptr_t fields = 0;
  if (!info || !read(info + 0x24, count) || count > 8192 || !read(info + 0x30, fields))
    return std::nullopt;
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto entry = fields + static_cast<std::uintptr_t>(i) * 0x20;
    std::uintptr_t name = 0;
    std::uint32_t offset = 0;
    if (read(entry, name) && read_cstring(name) == field && read(entry + 0x10, offset) && offset < 0x100000) {
      cache.emplace(key, offset);
      return offset;
    }
  }
#else
  (void)type; (void)field;
#endif
  return std::nullopt;
}
}
