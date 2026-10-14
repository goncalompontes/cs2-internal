#include "transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "log.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace cs2::overlay::transport {
namespace {

std::mutex g_lock;
wire::Shared* g_shared = nullptr;
const char* g_path = nullptr;
char g_path_buffer[1024];

#if defined(_WIN32)
HANDLE g_mapping = nullptr;
#else
int g_fd = -1;
#endif

#if defined(_WIN32)

// Native Windows: the payload writes its shared segment under the user's local
// app data (`%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm`) — a Medium-integrity
// game process cannot create files in `C:\` root. `CS2_PAYLOAD_DIR` overrides
// the directory; `CS2_OVERLAY_SHM` overrides the whole path. See payload
// ModelPublisher::map.
auto resolve_path() -> const char* {
  if (const char* explicit_path = std::getenv("CS2_OVERLAY_SHM")) {
    return explicit_path;
  }
  if (const char* dir = std::getenv("CS2_PAYLOAD_DIR")) {
    std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s\\cs2-overlay.shm", dir);
    return g_path_buffer;
  }
  char base[MAX_PATH];
  if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base)) == 0 &&
      GetEnvironmentVariableA("TEMP", base, sizeof(base)) == 0) {
    return nullptr;
  }
  std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s\\cs2-internal\\cs2-overlay.shm", base);
  return g_path_buffer;
}

auto ensure_mapped() -> bool {
  if (g_shared != nullptr) {
    return true;
  }
  if (g_path == nullptr) {
    g_path = resolve_path();
  }
  if (g_path == nullptr) {
    return false;
  }

  HANDLE file = CreateFileA(g_path, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(sizeof(wire::Shared)), nullptr);
  CloseHandle(file);
  if (mapping == nullptr) {
    return false;
  }
  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(wire::Shared));
  if (view == nullptr) {
    CloseHandle(mapping);
    return false;
  }
  auto* shared = static_cast<wire::Shared*>(view);
  if (shared->magic != wire::k_magic || shared->version != wire::k_version) {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return false;
  }
  g_mapping = mapping;
  g_shared = shared;
  overlay_log("cs2-overlay: shared segment mapped at %s", g_path);
  return true;
}

#else  // POSIX / Wine/Proton

// Candidate segment paths under a Wine `drive_c`, in priority order: the
// payload's per-user data dir (`%LOCALAPPDATA%\cs2-internal`, i.e.
// `users/<user>/AppData/Local/cs2-internal` — the native-Windows default),
// a shared `drive_c\cs2-internal`, then the legacy `drive_c\cs2-overlay.shm`.
const char* pick_existing(const char* drive_c_root) {
  char users_dir[1024];
  std::snprintf(users_dir, sizeof(users_dir), "%s/users", drive_c_root);
  if (DIR* dir = ::opendir(users_dir)) {
    while (dirent* entry = ::readdir(dir)) {
      if (entry->d_name[0] == '.') continue;
      std::snprintf(g_path_buffer, sizeof(g_path_buffer),
                    "%s/users/%s/AppData/Local/cs2-internal/cs2-overlay.shm", drive_c_root,
                    entry->d_name);
      if (::access(g_path_buffer, F_OK) == 0) {
        ::closedir(dir);
        return g_path_buffer;
      }
    }
    ::closedir(dir);
  }
  std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s/cs2-internal/cs2-overlay.shm",
                drive_c_root);
  if (::access(g_path_buffer, F_OK) == 0) return g_path_buffer;
  std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s/cs2-overlay.shm", drive_c_root);
  return g_path_buffer;  // may not exist yet; ensure_mapped retries
}

// The payload writes under the user's local app data; under Proton that is
// `<prefix>/drive_c/users/<user>/AppData/Local/cs2-internal/cs2-overlay.shm`.
// Steam sets WINEPREFIX and STEAM_COMPAT_DATA_PATH in the game environment.
const char* resolve_path() {
  if (const char* explicit_path = std::getenv("CS2_OVERLAY_SHM")) return explicit_path;
  if (const char* dir = std::getenv("CS2_PAYLOAD_DIR")) {
    std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s/cs2-overlay.shm", dir);
    return g_path_buffer;
  }
  char drive_c[1024];
  if (const char* prefix = std::getenv("WINEPREFIX")) {
    std::snprintf(drive_c, sizeof(drive_c), "%s/drive_c", prefix);
    return pick_existing(drive_c);
  }
  if (const char* data = std::getenv("STEAM_COMPAT_DATA_PATH")) {
    std::snprintf(drive_c, sizeof(drive_c), "%s/pfx/drive_c", data);
    return pick_existing(drive_c);
  }
  return nullptr;
}

bool ensure_mapped() {
  if (g_shared != nullptr) return true;
  if (g_path == nullptr) g_path = resolve_path();
  if (g_path == nullptr) return false;

  int fd = ::open(g_path, O_RDWR);
  if (fd < 0) fd = ::open(g_path, O_RDONLY);
  if (fd < 0) return false;

  void* view = ::mmap(nullptr, sizeof(wire::Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (view == MAP_FAILED) {
    ::close(fd);
    return false;
  }
  auto* shared = static_cast<wire::Shared*>(view);
  if (shared->magic != wire::k_magic || shared->version != wire::k_version) {
    ::munmap(view, sizeof(wire::Shared));
    ::close(fd);
    return false;
  }
  g_fd = fd;
  g_shared = shared;
  overlay_log("cs2-overlay: shared segment mapped at %s", g_path);
  return true;
}

#endif

}  // namespace

void write_extent(uint32_t width, uint32_t height) noexcept {
  const std::scoped_lock lock(g_lock);
  if (g_shared == nullptr && !ensure_mapped()) {
    return;
  }
  g_shared->screen_width.store(width, std::memory_order_relaxed);
  g_shared->screen_height.store(height, std::memory_order_relaxed);
}

auto read_model(wire::Model& out) noexcept -> bool {
  const std::scoped_lock lock(g_lock);
  if (g_shared == nullptr && !ensure_mapped()) {
    return false;
  }
  const uint32_t first = g_shared->sequence.load(std::memory_order_acquire);
  if ((first & 1U) != 0U) {
    return false;  // a write is in progress
  }
  std::memcpy(&out, &g_shared->model, sizeof(wire::Model));
  const uint32_t second = g_shared->sequence.load(std::memory_order_acquire);
  return first == second;
}

}  // namespace cs2::overlay::transport
