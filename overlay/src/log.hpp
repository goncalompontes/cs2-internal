#pragma once

// Small append-only logger for the overlay layer (runs inside the game
// process; under Proton the log path comes from CS2_OVERLAY_LOG or $HOME).

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace cs2::overlay {

inline void overlay_log(const char* format, ...) noexcept {
  static std::mutex log_mutex;
  std::lock_guard<std::mutex> lock(log_mutex);
  const char* path = std::getenv("CS2_OVERLAY_LOG");
  static char fallback[512];
  if (path == nullptr) {
    const char* home = std::getenv("HOME");
    std::snprintf(fallback, sizeof(fallback), "%s/cs2-overlay-layer.log",
                  home != nullptr ? home : "/tmp");
    path = fallback;
  }
  std::FILE* file = std::fopen(path, "a");
  if (file == nullptr) return;
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fclose(file);
}

}  // namespace cs2::overlay
