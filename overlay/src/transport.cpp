#include "transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.hpp"

namespace cs2::overlay::transport {
namespace {

std::mutex g_lock;
wire::Shared* g_shared = nullptr;
int g_fd = -1;
const char* g_path = nullptr;
char g_path_buffer[1024];

// The payload writes to `C:\cs2-overlay.shm`; under Proton that is
// `<prefix>/drive_c/cs2-overlay.shm`. Steam sets WINEPREFIX and
// STEAM_COMPAT_DATA_PATH in the game environment.
const char* resolve_path() {
  if (const char* explicit_path = std::getenv("CS2_OVERLAY_SHM")) return explicit_path;
  if (const char* prefix = std::getenv("WINEPREFIX")) {
    std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s/drive_c/cs2-overlay.shm", prefix);
    return g_path_buffer;
  }
  if (const char* data = std::getenv("STEAM_COMPAT_DATA_PATH")) {
    std::snprintf(g_path_buffer, sizeof(g_path_buffer), "%s/pfx/drive_c/cs2-overlay.shm", data);
    return g_path_buffer;
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

}  // namespace

void write_extent(uint32_t width, uint32_t height) noexcept {
  std::lock_guard<std::mutex> lock(g_lock);
  if (g_shared == nullptr && !ensure_mapped()) return;
  g_shared->screen_width.store(width, std::memory_order_relaxed);
  g_shared->screen_height.store(height, std::memory_order_relaxed);
}

bool read_model(wire::Model& out) noexcept {
  std::lock_guard<std::mutex> lock(g_lock);
  if (g_shared == nullptr && !ensure_mapped()) return false;
  const uint32_t first = g_shared->sequence.load(std::memory_order_acquire);
  if ((first & 1u) != 0u) return false;  // a write is in progress
  std::memcpy(&out, &g_shared->model, sizeof(wire::Model));
  const uint32_t second = g_shared->sequence.load(std::memory_order_acquire);
  return first == second;
}

}  // namespace cs2::overlay::transport
