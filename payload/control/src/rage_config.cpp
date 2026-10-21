#include "cs2/control/rage_config.hpp"

#include <cstdio>
#include <cstring>

#include "cs2/core/handle.hpp"
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

// The kernel32 entry points the config's file I/O needs. They are resolved by
// name rather than imported: the payload is manually mapped, so it does not
// rely on the loader filling an import table.
//
// The load and the save need different subsets of them, and each checks the
// subset it uses, so one resolution serves both.
struct KernelFileApi {
  CreateFileAFn create_file = nullptr;
  ReadFileFn read_file = nullptr;
  WriteFileFn write_file = nullptr;
  CloseHandleFn close_handle = nullptr;
};

auto kernel_file_api() noexcept -> KernelFileApi {
  const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if (kernel32 == nullptr) {
    return {};
  }
  KernelFileApi api{};
  api.create_file = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel32, "CreateFileA"));
  api.read_file = reinterpret_cast<ReadFileFn>(GetProcAddress(kernel32, "ReadFile"));
  api.write_file = reinterpret_cast<WriteFileFn>(GetProcAddress(kernel32, "WriteFile"));
  api.close_handle = reinterpret_cast<CloseHandleFn>(GetProcAddress(kernel32, "CloseHandle"));
  return api;
}

// The closer for the config file's handle: the `CloseHandle` that was resolved
// from kernel32, so the file is released by exactly the entry point the rest
// of this file uses.
struct FilePolicy {
  CloseHandleFn close_handle = nullptr;

  // The closer is resolved from kernel32 at run time, so a policy that does
  // not carry one cannot work: it cannot release the file. The wrapper asks
  // this before it asks the policy anything else, so a policy without a
  // closer is never asked to close anything.
  [[nodiscard]] bool usable() const noexcept {
    return close_handle != nullptr;
  }

  [[nodiscard]] bool is_valid(void* handle) const noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
  }
  void close(void* handle) const noexcept {
    if (close_handle != nullptr) {
      close_handle(static_cast<HANDLE>(handle));
      return;
    }
    // A null closer is a programming error, not a state to recover from: the
    // callers resolve the entry points and check them before they open a
    // file, so this cannot happen on a path that exists. Say so in the
    // payload's log instead of leaking the handle in silence.
    cs2::game::append_log(
        "rage config: the resolved CloseHandle is null; the file handle leaks");
  }
};
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
  const KernelFileApi api = kernel_file_api();
  if (api.create_file == nullptr || api.read_file == nullptr || api.close_handle == nullptr) {
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
  unsigned long read = 0;
  int ok = 0;
  char buffer[k_max_file_bytes] = {};
  {
    // The handle is owned by the wrapper, so every path out of this block
    // releases it -- including the early return above (which never got one).
    const core::ScopedHandle<void*, FilePolicy> file(
        api.create_file(path, k_generic_read, k_share_read, nullptr, k_open_existing,
                        k_file_attribute_normal, nullptr),
        FilePolicy{api.close_handle});
    if (!file.valid()) {
      return false;
    }
    ok = api.read_file(static_cast<HANDLE>(file.get()), buffer, sizeof(buffer) - 1, &read, nullptr);
  }  // the file is released here, before the buffer is parsed
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
    const char* p = line;
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
  const KernelFileApi api = kernel_file_api();
  if (api.create_file == nullptr || api.write_file == nullptr || api.close_handle == nullptr) {
    return false;
  }
  constexpr unsigned long k_generic_write = 0x40000000UL;
  constexpr unsigned long k_share_read = 0x1UL;
  constexpr unsigned long k_create_always = 0x2UL;
  constexpr unsigned long k_file_attribute_normal = 0x80UL;
  // The handle is owned by the wrapper, so every path out of this block
  // releases it.
  int ok = 0;
  {
    const core::ScopedHandle<void*, FilePolicy> file(
        api.create_file(path, k_generic_write, k_share_read, nullptr, k_create_always,
                        k_file_attribute_normal, nullptr),
        FilePolicy{api.close_handle});
    if (!file.valid()) {
      return false;
    }
    unsigned long written = 0;
    ok = api.write_file(static_cast<HANDLE>(file.get()), buffer, static_cast<unsigned long>(used),
                        &written, nullptr);
  }  // the file is released here, before the result is returned
  return ok != 0;
#else
  (void)store;
  return false;
#endif
}

}  // namespace cs2::control
