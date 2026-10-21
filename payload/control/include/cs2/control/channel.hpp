#pragma once

// Win32 named-pipe server carrying newline-delimited JSON (ADR-0020). The
// payload owns the server end; the scenario runner connects as a client. The
// payload runs in the game's (session 1) process while the runner may be in
// session 0, which is why the transport is a same-user named pipe rather than a
// socket or shared memory.

#include <string>
#include <string_view>

#include "cs2/core/handle.hpp"

namespace cs2::control {

// The closer for the pipe handle. The handle is stored opaquely and the
// closer is supplied by the implementation, so this header stays free of
// <windows.h> (the platform-independent core must not include it -- see
// docs/style-guide.md §3).
struct PipeHandlePolicy {
  // A pipe that was never opened is a null handle; `CreateNamedPipeA` reports
  // failure with INVALID_HANDLE_VALUE, which `Channel::open` rejects before
  // the handle is stored.
  bool is_valid(void* handle) const noexcept {
    return handle != nullptr;
  }
  void close(void* handle) const noexcept;
};

class Channel {
 public:
  static constexpr const char* k_default_pipe = "\\\\.\\pipe\\cs2-internal-control";

  Channel() = default;
  ~Channel();
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;

  // Create the pipe server end. Returns false when it cannot be created (e.g.
  // the name is already taken by a live instance).
  [[nodiscard]] bool open(std::string_view name) noexcept;
  void close() noexcept;
  [[nodiscard]] bool valid() const noexcept {
    return handle_.valid();
  }

  // Block until a client connects. Returns false when the pipe is closed or the
  // wait failed.
  [[nodiscard]] bool accept() noexcept;
  // Disconnect the current client so the next one can connect.
  void disconnect() noexcept;

  // Read one newline-terminated line into `out` (the terminator is stripped).
  // Returns false when the client disconnected or the read failed.
  [[nodiscard]] bool read_line(std::string& out) noexcept;

  // Write `bytes` followed by a newline. Returns false on failure.
  [[nodiscard]] bool write_line(std::string_view bytes) noexcept;

 private:
  // The pipe's server end. It is owned by the wrapper, so it is released on
  // every path -- including the ones that return early.
  core::ScopedHandle<void*, PipeHandlePolicy> handle_{};
  std::string pending_;  // bytes read but not yet terminated by a newline
};

}  // namespace cs2::control
