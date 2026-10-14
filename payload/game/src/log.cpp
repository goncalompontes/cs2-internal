#include "cs2/game/log.hpp"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::game {
namespace {

#if defined(_WIN32)

// Resolve the payload's writable data directory. A native Windows game runs at
// Medium integrity, so it cannot create files in `C:\` root; use the user's
// local app data instead. `CS2_PAYLOAD_DIR` overrides the directory. The
// directory is created on demand (idempotent).
bool resolve_data_dir(char* out, std::size_t size) noexcept {
  if (const char* override_dir = std::getenv("CS2_PAYLOAD_DIR")) {
    if (override_dir[0] != '\0') {
      const int written = std::snprintf(out, size, "%s", override_dir);
      if (written <= 0 || static_cast<std::size_t>(written) >= size) return false;
      CreateDirectoryA(out, nullptr);
      return true;
    }
  }
  char base[MAX_PATH];
  if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) == 0 &&
      GetEnvironmentVariableA("TEMP", base, sizeof(base)) == 0) {
    return false;
  }
  const int written = std::snprintf(out, size, "%s\\cs2-internal", base);
  if (written <= 0 || static_cast<std::size_t>(written) >= size) return false;
  CreateDirectoryA(out, nullptr);  // idempotent (ERROR_ALREADY_EXISTS is fine)
  return true;
}

#endif

}  // namespace

bool data_file_path(const char* name, char* out, std::size_t size) noexcept {
#if defined(_WIN32)
  char dir[MAX_PATH];
  if (!resolve_data_dir(dir, sizeof(dir))) return false;
  const int written = std::snprintf(out, size, "%s\\%s", dir, name);
  return written > 0 && static_cast<std::size_t>(written) < size;
#else
  (void)name;
  (void)out;
  (void)size;
  return false;
#endif
}

void append_log(std::string_view text) noexcept {
#if defined(_WIN32)
  using CreateFileAFn = HANDLE(__stdcall*)(const char*, unsigned long, unsigned long, void*,
                                           unsigned long, unsigned long, HANDLE);
  static CreateFileAFn create_file = nullptr;
  if (create_file == nullptr) {
    const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) return;
    create_file = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel32, "CreateFileA"));
    if (create_file == nullptr) return;
  }
  char path[MAX_PATH];
  if (!data_file_path("cs2-payload.log", path, sizeof(path))) return;

  constexpr unsigned long k_file_append_data = 0x4;
  constexpr unsigned long k_file_share_read = 0x1;
  constexpr unsigned long k_open_always = 0x4;
  constexpr unsigned long k_file_attribute_normal = 0x80;
  HANDLE file = create_file(path, k_file_append_data, k_file_share_read, nullptr, k_open_always,
                            k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) return;
  unsigned long written = 0;
  WriteFile(file, text.data(), static_cast<unsigned long>(text.size()), &written, nullptr);
  WriteFile(file, "\n", 1, &written, nullptr);
  CloseHandle(file);
#else
  (void)text;
#endif
}

}  // namespace cs2::game
