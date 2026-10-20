#include "cs2/control/channel.hpp"

#include <cstddef>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {
namespace {

constexpr unsigned long k_pipe_buffer = 64UL * 1024UL;

}  // namespace

Channel::~Channel() {
  close();
}

#if defined(_WIN32)

auto Channel::open(std::string_view name) noexcept -> bool {
  if (handle_ != nullptr) {
    return true;
  }
  const std::string path(name);
  const HANDLE pipe = CreateNamedPipeA(
      path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES, k_pipe_buffer, k_pipe_buffer, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return false;
  }
  handle_ = pipe;
  pending_.clear();
  return true;
}

void Channel::close() noexcept {
  if (handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
  pending_.clear();
}

auto Channel::accept() noexcept -> bool {
  if (handle_ == nullptr) {
    return false;
  }
  if (ConnectNamedPipe(static_cast<HANDLE>(handle_), nullptr) != FALSE) {
    return true;
  }
  // A client that connected between CreateNamedPipe and ConnectNamedPipe makes
  // ConnectNamedPipe fail with ERROR_PIPE_CONNECTED, which is success.
  return GetLastError() == ERROR_PIPE_CONNECTED;
}

void Channel::disconnect() noexcept {
  if (handle_ != nullptr) {
    DisconnectNamedPipe(static_cast<HANDLE>(handle_));
  }
  pending_.clear();
}

auto Channel::read_line(std::string& out) noexcept -> bool {
  if (handle_ == nullptr) {
    return false;
  }
  for (;;) {
    const std::size_t newline = pending_.find('\n');
    if (newline != std::string::npos) {
      out.assign(pending_, 0, newline);
      pending_.erase(0, newline + 1);
      if (!out.empty() && out.back() == '\r') {
        out.pop_back();
      }
      return true;
    }
    char buffer[2048];
    DWORD read = 0;
    if (ReadFile(static_cast<HANDLE>(handle_), buffer, sizeof(buffer), &read, nullptr) == FALSE ||
        read == 0) {
      return false;  // client disconnected or the pipe broke
    }
    pending_.append(buffer, read);
  }
}

auto Channel::write_line(std::string_view bytes) noexcept -> bool {
  if (handle_ == nullptr) {
    return false;
  }
  std::string payload(bytes);
  payload.push_back('\n');
  const char* data = payload.data();
  std::size_t remaining = payload.size();
  while (remaining > 0) {
    const auto chunk = static_cast<DWORD>(remaining > k_pipe_buffer ? k_pipe_buffer : remaining);
    DWORD written = 0;
    if (WriteFile(static_cast<HANDLE>(handle_), data, chunk, &written, nullptr) == FALSE) {
      return false;
    }
    if (written == 0) {
      return false;
    }
    data += written;
    remaining -= written;
  }
  return true;
}

#else

auto Channel::open(std::string_view /*name*/) noexcept -> bool {
  return false;
}
void Channel::close() noexcept {}
auto Channel::accept() noexcept -> bool {
  return false;
}
void Channel::disconnect() noexcept {}
auto Channel::read_line(std::string& /*out*/) noexcept -> bool {
  return false;
}
auto Channel::write_line(std::string_view /*bytes*/) noexcept -> bool {
  return false;
}

#endif

}  // namespace cs2::control
