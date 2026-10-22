#include "cs2/control/channel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>

#include <cstring>
#endif

namespace cs2::control {
namespace {

#if defined(_WIN32)


auto to_socket(std::uintptr_t value) noexcept -> SOCKET {
  return static_cast<SOCKET>(value);
}

auto socket_value(SOCKET socket) noexcept -> std::uintptr_t {
  return static_cast<std::uintptr_t>(socket);
}

// Lower-case an ASCII string in place (the header names are case-insensitive).
void lowercase(std::string& text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
}

#endif

}  // namespace

Channel::~Channel() {
  close();
}

#if defined(_WIN32)

void Channel::close() noexcept {
  if (client_ != k_invalid) {
    ::shutdown(to_socket(client_), SD_BOTH);
    ::closesocket(to_socket(client_));
    client_ = k_invalid;
  }
  if (listener_ != k_invalid) {
    ::closesocket(to_socket(listener_));
    listener_ = k_invalid;
  }
  if (unix_ && !endpoint_.empty()) {
    std::remove(endpoint_.c_str());
  }
  if (started_winsock_) {
    ::WSACleanup();
    started_winsock_ = false;
  }
  pending_.clear();
}

auto Channel::listen(std::string_view path) noexcept -> bool {
  close();
  WSADATA data{};
  if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    return false;
  }
  started_winsock_ = true;
  if (!path.empty() && listen_unix(path)) {
    return true;
  }
  return listen_tcp();
}

auto Channel::listen_unix(std::string_view path) noexcept -> bool {
  const std::string socket_path(path);
  sockaddr_un probe{};
  if (socket_path.size() >= sizeof(probe.sun_path)) {
    return false;  // the kernel's own limit: fail loudly, do not truncate
  }
  // A socket file left behind by a killed payload would make bind fail with
  // EADDRINUSE forever. It is ours (same private directory), so remove it.
  std::remove(socket_path.c_str());

  const SOCKET listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener == INVALID_SOCKET) {
    return false;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 4) != 0) {
    ::closesocket(listener);
    return false;
  }
  listener_ = socket_value(listener);
  unix_ = true;
  endpoint_ = socket_path;
  port_ = 0;
  return true;
}

auto Channel::listen_tcp() noexcept -> bool {
  const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) {
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;  // the kernel picks the port
  if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 4) != 0) {
    ::closesocket(listener);
    return false;
  }
  int length = sizeof(address);
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    ::closesocket(listener);
    return false;
  }
  listener_ = socket_value(listener);
  unix_ = false;
  port_ = ntohs(address.sin_port);
  endpoint_ = "127.0.0.1:" + std::to_string(port_);
  return true;
}

auto Channel::accept() noexcept -> bool {
  if (listener_ == k_invalid) {
    return false;
  }
  const SOCKET client = ::accept(to_socket(listener_), nullptr, nullptr);
  if (client == INVALID_SOCKET) {
    return false;
  }
  // Input commands are small; Nagle would add latency to every one.
  BOOL nodelay = TRUE;
  ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
               sizeof(nodelay));
  client_ = socket_value(client);
  pending_.clear();
  return true;
}

void Channel::disconnect() noexcept {
  if (client_ != k_invalid) {
    ::shutdown(to_socket(client_), SD_BOTH);
    ::closesocket(to_socket(client_));
    client_ = k_invalid;
  }
  pending_.clear();
}

auto Channel::read_message(std::string& out) noexcept -> bool {
  if (client_ == k_invalid) {
    return false;
  }
  // Read until the header terminator is in the buffer.
  std::size_t header_end = pending_.find("\r\n\r\n");
  while (header_end == std::string::npos) {
    if (pending_.size() > 64U * 1024U) {
      return false;  // a header this large is not one of ours
    }
    char buffer[4096];
    const int read = ::recv(to_socket(client_), buffer, sizeof(buffer), 0);
    if (read <= 0) {
      return false;  // disconnected or the socket broke
    }
    pending_.append(buffer, static_cast<std::size_t>(read));
    header_end = pending_.find("\r\n\r\n");
  }

  // Parse `Content-Length` case-insensitively.
  std::size_t length = 0;
  bool have_length = false;
  std::size_t line_start = 0;
  while (line_start < header_end) {
    std::size_t line_end = pending_.find("\r\n", line_start);
    if (line_end == std::string::npos || line_end > header_end) {
      line_end = header_end;
    }
    std::string line = pending_.substr(line_start, line_end - line_start);
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = line.substr(0, colon);
      lowercase(name);
      if (name == "content-length") {
        const std::string value = line.substr(colon + 1);
        const char* begin = value.c_str();
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(begin, &end, 10);
        if (end == begin) {
          return false;
        }
        length = static_cast<std::size_t>(parsed);
        have_length = true;
      }
    }
    line_start = line_end + 2;
  }
  if (!have_length || length > k_max_frame) {
    return false;
  }

  const std::size_t body_start = header_end + 4;
  while (pending_.size() < body_start + length) {
    char buffer[8192];
    const int read = ::recv(to_socket(client_), buffer, sizeof(buffer), 0);
    if (read <= 0) {
      return false;
    }
    pending_.append(buffer, static_cast<std::size_t>(read));
  }
  out.assign(pending_, body_start, length);
  pending_.erase(0, body_start + length);
  return true;
}

auto Channel::write_message(std::string_view json) noexcept -> bool {
  if (client_ == k_invalid) {
    return false;
  }
  std::string frame = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n";
  frame.append(json);
  const char* data = frame.data();
  std::size_t remaining = frame.size();
  while (remaining > 0) {
    const int written = ::send(to_socket(client_), data, static_cast<int>(remaining), 0);
    if (written <= 0) {
      return false;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
  return true;
}

#else

void Channel::close() noexcept {}
auto Channel::listen(std::string_view) noexcept -> bool {
  return false;
}
auto Channel::listen_unix(std::string_view) noexcept -> bool {
  return false;
}
auto Channel::listen_tcp() noexcept -> bool {
  return false;
}
auto Channel::accept() noexcept -> bool {
  return false;
}
void Channel::disconnect() noexcept {}
auto Channel::read_message(std::string&) noexcept -> bool {
  return false;
}
auto Channel::write_message(std::string_view) noexcept -> bool {
  return false;
}

#endif

}  // namespace cs2::control
