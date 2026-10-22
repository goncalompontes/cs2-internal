#pragma once

// Small append-only logger for the overlay layer (runs inside the game
// process; under Proton the log path comes from CS2_OVERLAY_LOG or $HOME).

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <filesystem>
#include <system_error>

namespace cs2::overlay {

inline void overlay_log(const char* format, ...) noexcept {
  static std::mutex log_mutex;
  const std::scoped_lock lock(log_mutex);
  const char* path = std::getenv("CS2_OVERLAY_LOG");
  static char fallback[512];
  if (path == nullptr) {
    // Native Windows: default next to the payload's other output (the env var
    // may be absent when the game inherits Steam's cached environment).
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != nullptr) {
      std::error_code error;
      std::filesystem::create_directories(std::filesystem::path(local_app_data) / "cs2-internal", error);
      std::snprintf(fallback, sizeof(fallback), "%s\\cs2-internal\\cs2-overlay-layer.log",
                    local_app_data);
    } else {
      const char* home = std::getenv("HOME");
      std::snprintf(fallback, sizeof(fallback), "%s/cs2-overlay-layer.log",
                    home != nullptr ? home : "/tmp");
    }
    path = fallback;
  }
  std::FILE* file = std::fopen(path, "a");
  if (file == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fclose(file);
}

}  // namespace cs2::overlay
