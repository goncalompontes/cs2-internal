#include "cs2/control/rage_config.hpp"

#include <cstdio>
#include <cstring>

#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {
namespace {

constexpr int k_max_file_bytes = 16 * 1024;

// Lower-case an ASCII weapon name and strip a leading `weapon_`, writing the normalised
// key with the `weapon_` prefix back.
void normalise_weapon(const char* in, char* out, int size) noexcept {
  if (out == nullptr || size <= 0) {
    return;
  }
  char lower[64] = {};
  int n = 0;
  for (const char* p = in != nullptr ? in : ""; *p != '\0' && n < static_cast<int>(sizeof(lower)) - 1;
       ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    lower[n++] = c;
  }
  lower[n] = '\0';
  const char* body = lower;
  if (std::strncmp(body, "weapon_", 7) == 0) {
    body += 7;
  }
  std::snprintf(out, static_cast<std::size_t>(size), "weapon_%s", body);
}

auto equal_weapon(const char* a, const char* b) noexcept -> bool {
  char na[64];
  char nb[64];
  normalise_weapon(a, na, sizeof(na));
  normalise_weapon(b, nb, sizeof(nb));
  return std::strcmp(na, nb) == 0;
}

#if defined(_WIN32)
using CreateFileAFn = HANDLE(__stdcall*)(const char*, unsigned long, unsigned long, void*,
                                         unsigned long, unsigned long, HANDLE);
using ReadFileFn = int(__stdcall*)(HANDLE, void*, unsigned long, unsigned long*, void*);
using WriteFileFn = int(__stdcall*)(HANDLE, const void*, unsigned long, unsigned long*, void*);
using CloseHandleFn = int(__stdcall*)(HANDLE);

auto kernel_fn(const char* name) noexcept -> void* {
  const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  return kernel32 == nullptr ? nullptr : reinterpret_cast<void*>(GetProcAddress(kernel32, name));
}
#endif

}  // namespace

void rage_weapon_key(const char* weapon, char* out, int size) noexcept {
  normalise_weapon(weapon, out, size);
}

auto rage_profile_find(RageConfigStore& store, const char* weapon, bool create) -> RageProfile* {
  const char* key = weapon != nullptr ? weapon : "";
  for (int i = 0; i < store.count && i < k_max_rage_profiles; ++i) {
    if (equal_weapon(store.profiles[i].weapon, key)) {
      return &store.profiles[i];
    }
  }
  if (!create || store.count >= k_max_rage_profiles) {
    return nullptr;
  }
  RageProfile& profile = store.profiles[store.count++];
  profile = RageProfile{};
  normalise_weapon(key, profile.weapon, sizeof(profile.weapon));
  return &profile;
}

auto rage_config_load(RageConfigStore& store) noexcept -> bool {
  store.count = 0;
#if defined(_WIN32)
  using CreateFileA = CreateFileAFn;
  using ReadFileT = ReadFileFn;
  using CloseHandleT = CloseHandleFn;
  auto create_file = reinterpret_cast<CreateFileA>(kernel_fn("CreateFileA"));
  auto read_file = reinterpret_cast<ReadFileT>(kernel_fn("ReadFile"));
  auto close_handle = reinterpret_cast<CloseHandleT>(kernel_fn("CloseHandle"));
  if (create_file == nullptr || read_file == nullptr || close_handle == nullptr) {
    return false;
  }
  char path[MAX_PATH];
  if (!cs2::game::data_file_path("cs2-rage.cfg", path, sizeof(path))) {
    return false;
  }
  constexpr unsigned long k_generic_read = 0x80000000UL;
  constexpr unsigned long k_share_read = 0x1UL;
  constexpr unsigned long k_open_existing = 0x3UL;
  constexpr unsigned long k_file_attribute_normal = 0x80UL;
  HANDLE file = create_file(path, k_generic_read, k_share_read, nullptr, k_open_existing,
                            k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) {
    return false;
  }
  char buffer[k_max_file_bytes] = {};
  unsigned long read = 0;
  const int ok = read_file(file, buffer, sizeof(buffer) - 1, &read, nullptr);
  close_handle(file);
  if (ok == 0) {
    return false;
  }
  buffer[read < sizeof(buffer) ? read : sizeof(buffer) - 1] = '\0';

  char* line = buffer;
  while (line != nullptr && *line != '\0' && store.count < k_max_rage_profiles) {
    char* next = std::strchr(line, '\n');
    if (next != nullptr) {
      *next = '\0';
      ++next;
    }
    // Skip comments / blanks.
    char* p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r') {
      ++p;
    }
    if (*p != '\0' && *p != '#') {
      RageProfile profile;
      int key = 0;
      const int fields = std::sscanf(
          p, "%23[^|]|%hhu|%hhu|%hhu|%d|%f|%f|%f|%hhu|%hhu|%hhu|%hhu|%hhu", profile.weapon,
          &profile.objective, &profile.hitbox, &profile.mode, &key, &profile.fov,
          &profile.min_damage, &profile.min_hitchance, &profile.autostop, &profile.autoscope,
          &profile.autoreload, &profile.only_visible, &profile.fire);
      if (fields >= 4) {
        profile.key = key;
        char normalised[24];
        normalise_weapon(profile.weapon, normalised, sizeof(normalised));
        std::snprintf(profile.weapon, sizeof(profile.weapon), "%s", normalised);
        store.profiles[store.count++] = profile;
      }
    }
    line = next;
  }
  return store.count > 0;
#else
  (void)store;
  return false;
#endif
}

auto rage_config_save(const RageConfigStore& store) noexcept -> bool {
#if defined(_WIN32)
  char path[MAX_PATH];
  if (!cs2::game::data_file_path("cs2-rage.cfg", path, sizeof(path))) {
    return false;
  }
  char buffer[k_max_file_bytes] = {};
  int used = 0;
  used += std::snprintf(buffer + used, sizeof(buffer) - static_cast<std::size_t>(used),
                        "# cs2-internal ragebot profiles (docs/ragebot.md)\n");
  for (int i = 0; i < store.count && i < k_max_rage_profiles && used > 0 &&
                  used < static_cast<int>(sizeof(buffer)) - 1;
       ++i) {
    const RageProfile& p = store.profiles[i];
    const int written = std::snprintf(
        buffer + used, sizeof(buffer) - static_cast<std::size_t>(used),
        "%s|%u|%u|%u|%d|%.4f|%.2f|%.3f|%u|%u|%u|%u|%u\n", p.weapon, p.objective, p.hitbox,
        p.mode, p.key, static_cast<double>(p.fov), static_cast<double>(p.min_damage),
        static_cast<double>(p.min_hitchance), p.autostop, p.autoscope, p.autoreload,
        p.only_visible, p.fire);
    if (written <= 0) {
      break;
    }
    used += written;
  }
  using CreateFileA = CreateFileAFn;
  using WriteFileT = WriteFileFn;
  using CloseHandleT = CloseHandleFn;
  auto create_file = reinterpret_cast<CreateFileA>(kernel_fn("CreateFileA"));
  auto write_file = reinterpret_cast<WriteFileT>(kernel_fn("WriteFile"));
  auto close_handle = reinterpret_cast<CloseHandleT>(kernel_fn("CloseHandle"));
  if (create_file == nullptr || write_file == nullptr || close_handle == nullptr) {
    return false;
  }
  constexpr unsigned long k_generic_write = 0x40000000UL;
  constexpr unsigned long k_share_read = 0x1UL;
  constexpr unsigned long k_create_always = 0x2UL;
  constexpr unsigned long k_file_attribute_normal = 0x80UL;
  HANDLE file = create_file(path, k_generic_write, k_share_read, nullptr, k_create_always,
                            k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) {
    return false;
  }
  unsigned long written = 0;
  const int ok = write_file(file, buffer, static_cast<unsigned long>(used), &written, nullptr);
  close_handle(file);
  return ok != 0;
#else
  (void)store;
  return false;
#endif
}

}  // namespace cs2::control
