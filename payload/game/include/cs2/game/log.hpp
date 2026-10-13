#pragma once

// Minimal append-only file log for the payload.
//
// Uses Win32 file I/O rather than the CRT: a manually mapped module only has
// static-CRT per-thread state on the injector's entry thread, so `std::FILE`
// logging from the render thread is unsafe.

#include <string_view>

namespace cs2::game {

// Append `text` (plus a newline) to `C:\cs2-payload.log`. No-op on failure.
void append_log(std::string_view text) noexcept;

}  // namespace cs2::game
