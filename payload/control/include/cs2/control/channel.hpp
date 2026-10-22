#pragma once

// The control channel's transport: a local stream socket carrying JSON-RPC 2.0
// messages.
//
// The payload listens on an **AF_UNIX** socket under its data directory
// (`%LOCALAPPDATA%\cs2-internal\control.sock` on native Windows) and falls back
// to a loopback TCP port if the platform refuses AF_UNIX. Frames are
// `Content-Length: N\r\n\r\n` followed by N bytes of UTF-8 JSON -- the framing
// LSP clients already know, and the one that makes a partial read harmless.
//
// The transport knows nothing about the RPC: it moves frames. The token
// handshake and the method dispatch live in `Service`.
//
// Windows-only sockets are hidden behind `std::uintptr_t` so this header stays
// free of <winsock2.h> (the platform-independent core must not include it --
// docs/style-guide.md section 3).

#include <cstdint>
#include <string>
#include <string_view>

namespace cs2::control {

class Channel {
 public:
  // The socket's file name, under the payload's data directory.
  static constexpr const char* k_default_socket = "control.sock";
  // The largest frame the server accepts (a `heatmap` answer is the biggest
  // legitimate one; 8 MiB leaves room and stops a runaway client).
  static constexpr std::size_t k_max_frame = 8U * 1024U * 1024U;
  // `INVALID_SOCKET` without including <winsock2.h>.
  static constexpr std::uintptr_t k_invalid = static_cast<std::uintptr_t>(~0ULL);

  Channel() = default;
  ~Channel();
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;

  // Bind and listen. `path` is the AF_UNIX path; an empty path asks for the
  // loopback TCP fallback. Returns false when neither can be created.
  [[nodiscard]] bool listen(std::string_view path) noexcept;
  void close() noexcept;

  // Block until a client connects. A second client waits (the payload serves
  // one at a time). Returns false when the listener was closed.
  [[nodiscard]] bool accept() noexcept;
  void disconnect() noexcept;

  // Read one whole frame into `out`. Returns false on disconnect, on a
  // malformed or oversized header, or when the listener was closed.
  [[nodiscard]] bool read_message(std::string& out) noexcept;

  // Write one whole frame. Returns false on a broken connection.
  [[nodiscard]] bool write_message(std::string_view json) noexcept;

  // Where the server listens: for a Unix socket the path, for TCP the port.
  [[nodiscard]] bool is_unix() const noexcept { return unix_; }
  [[nodiscard]] unsigned short port() const noexcept { return port_; }
  [[nodiscard]] const std::string& endpoint() const noexcept { return endpoint_; }

 private:
  [[nodiscard]] bool listen_unix(std::string_view path) noexcept;
  [[nodiscard]] bool listen_tcp() noexcept;

  std::uintptr_t listener_ = k_invalid;
  std::uintptr_t client_ = k_invalid;
  bool unix_ = false;
  bool started_winsock_ = false;
  unsigned short port_ = 0;
  std::string endpoint_;
  std::string pending_;
};

}  // namespace cs2::control
