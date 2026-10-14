#pragma once

// Minimal append-only file log for the payload.
//
// Uses Win32 file I/O rather than the CRT: a manually mapped module only has
// static-CRT per-thread state on the injector's entry thread, so `std::FILE`
// logging from the render thread is unsafe.

#include <cstddef>
#include <string_view>

namespace cs2::game {

// Resolve a path for one of the payload's data files under its writable data
// directory. On Windows a standard-user game process cannot create files in
// `C:\` root (Medium integrity masks the ACL), so the directory is
// `%LOCALAPPDATA%\cs2-internal` (override with the `CS2_PAYLOAD_DIR`
// environment variable); it is created on demand. Returns false on failure.
[[nodiscard]] bool data_file_path(const char* name, char* out, std::size_t size) noexcept;

// Append `text` (plus a newline) to the payload log file. No-op on failure.
void append_log(std::string_view text) noexcept;

}  // namespace cs2::game
