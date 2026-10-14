#include "cs2/control/service.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "cs2/control/predicate.hpp"
#include "cs2/control/snapshot_json.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/tls/bootstrap.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {

Service::~Service() { stop(); }

#if defined(_WIN32)
namespace {

constexpr DWORD k_poll_ms = 100;
constexpr std::int64_t k_default_timeout_ms = 30000;

void set_error(Json& out, std::string_view message) {
  out.set("ok", Json::boolean(false));
  out.set("error", Json::string(std::string(message)));
}

void set_ok(Json& out, Json value) {
  out.set("ok", Json::boolean(true));
  out.set("result", std::move(value));
}

}  // namespace

unsigned long __stdcall Service::thread_entry(void* self) noexcept {
  // This thread has no TLS block for the mapped image until we install one.
  cs2::tls::ensure_thread_tls();
  static_cast<Service*>(self)->run();
  return 0;
}

void Service::run() noexcept {
  while (running_.load(std::memory_order_relaxed)) {
    cs2::diag::heartbeat();
    if (!channel_.accept()) {
      break;  // pipe closed
    }
    std::string line;
    while (running_.load(std::memory_order_relaxed)) {
      if (!channel_.read_line(line)) {
        break;  // client disconnected
      }
      if (line.empty()) {
        continue;
      }
      std::string response;
      const bool keep_going = handle(line, response);
      if (!response.empty() && !channel_.write_line(response)) {
        break;  // client went away
      }
      if (!keep_going) {
        running_.store(false, std::memory_order_relaxed);
        break;
      }
    }
    channel_.disconnect();
  }
}

bool Service::handle(const std::string& line, std::string& response) noexcept {
  Json out = Json::object();
  const std::optional<Json> request = Json::parse(line);
  if (!request.has_value() || !request->is_object()) {
    set_error(out, "malformed request: expected a JSON object");
    response = out.dump();
    return true;
  }
  if (const Json* id = request->find("id"); id != nullptr) {
    out.set("id", *id);
  }
  const Json* cmd = request->find("cmd");
  if (cmd == nullptr || !cmd->is_string()) {
    set_error(out, "missing \"cmd\" string");
    response = out.dump();
    return true;
  }

  const std::string_view command = cmd->as_string();
  const Json* args = request->find("args");
  const Json empty = Json::object();
  const Json& arguments = args != nullptr ? *args : empty;

  if (command == "quit") {
    set_ok(out, Json::object());
    response = out.dump();
    return false;
  }
  if (command == "ping") {
    set_ok(out, ping_result());
  } else if (command == "state") {
    do_state(out);
  } else if (command == "exec") {
    do_exec(arguments, out);
  } else if (command == "wait_for") {
    do_wait_for(arguments, out);
  } else {
    set_error(out, "unknown cmd");
  }
  response = out.dump();
  return true;
}

Json Service::ping_result() noexcept {
  console_.refresh();
  Json result = Json::object();
  result.set("pong", Json::boolean(true));
  result.set("pid", Json::number(static_cast<double>(GetCurrentProcessId())));
  result.set("exec_ready", Json::boolean(console_.ready()));
  result.set("state_ready", Json::boolean(reader_.has_value()));
  return result;
}

void Service::refresh_reader(bool last_snapshot_valid) noexcept {
  if (reader_.has_value() && (!reader_->ready() || !last_snapshot_valid)) {
    reader_.reset();  // client module moved, or the level/entity list changed
  }
  if (reader_.has_value()) {
    return;
  }
  std::optional<game::StateReader> attached = game::StateReader::attach();
  if (attached.has_value()) {
    reader_ = std::move(*attached);
  }
}

void Service::do_state(Json& out) noexcept {
  refresh_reader(true);
  if (!reader_.has_value()) {
    set_error(out, "state unavailable: client.dll / entity system not ready");
    return;
  }
  const game::Snapshot snapshot = reader_->snapshot();
  refresh_reader(snapshot.valid);
  set_ok(out, to_json(snapshot));
}

void Service::do_exec(const Json& args, Json& out) noexcept {
  const Json* line = args.find("line");
  if (line == nullptr || !line->is_string()) {
    set_error(out, "exec requires a string \"line\"");
    return;
  }
  console_.refresh();
  if (!console_.ready()) {
    set_error(out, "exec unavailable: engine2 Cbuf offsets not resolved");
    return;
  }
  if (!console_.exec(line->as_string())) {
    set_error(out, "exec failed");
    return;
  }
  Json result = Json::object();
  result.set("queued", Json::boolean(true));
  set_ok(out, std::move(result));
}

void Service::do_wait_for(const Json& args, Json& out) noexcept {
  Predicate predicate = Predicate::always_true();
  if (const Json* expr = args.find("expr"); expr != nullptr) {
    std::optional<Predicate> parsed = Predicate::from_json(*expr);
    if (!parsed.has_value()) {
      set_error(out, "invalid \"expr\" predicate");
      return;
    }
    predicate = std::move(*parsed);
  }

  std::int64_t timeout_ms = k_default_timeout_ms;
  if (const Json* timeout = args.find("timeout_ms"); timeout != nullptr && timeout->is_number()) {
    timeout_ms = timeout->as_int();
  }
  if (timeout_ms < 0) {
    timeout_ms = 0;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  bool matched = false;
  Json snapshot = Json();
  for (;;) {
    cs2::diag::heartbeat();
    refresh_reader(true);
    if (reader_.has_value()) {
      const game::Snapshot current = reader_->snapshot();
      refresh_reader(current.valid);
      if (current.valid) {
        snapshot = to_json(current);
      }
      if (predicate.matches(current)) {
        matched = true;
        break;
      }
    }
    if (!running_.load(std::memory_order_relaxed) ||
        std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    Sleep(k_poll_ms);
  }

  Json result = Json::object();
  result.set("matched", Json::boolean(matched));
  result.set("snapshot", std::move(snapshot));
  set_ok(out, std::move(result));
}

bool Service::start() noexcept {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }
  const char* name = std::getenv("CS2_CONTROL_PIPE");
  if (name == nullptr || *name == '\0') {
    name = Channel::k_default_pipe;
  }
  if (!channel_.open(name)) {
    cs2::game::append_log("[cs2-payload] control: failed to create the named pipe");
    return false;
  }
  running_.store(true, std::memory_order_relaxed);
  // Skip LdrpInitializeThread (see cs2::tls::create_thread_no_attach).
  void* thread = cs2::tls::create_thread_no_attach(
      reinterpret_cast<void* (*)(void*)>(&Service::thread_entry), this);
  if (thread == nullptr) {
    running_.store(false, std::memory_order_relaxed);
    channel_.close();
    return false;
  }
  thread_ = thread;
  cs2::game::append_log("[cs2-payload] control: listening on " +
                        std::string(name));
  return true;
}

void Service::stop() noexcept {
  running_.store(false, std::memory_order_relaxed);
  // Closing the pipe unblocks a thread parked in ConnectNamedPipe/ReadFile.
  channel_.close();
  if (thread_ != nullptr) {
    WaitForSingleObject(static_cast<HANDLE>(thread_), 2000);
    CloseHandle(static_cast<HANDLE>(thread_));
    thread_ = nullptr;
  }
}

#else

bool Service::start() noexcept { return false; }
void Service::stop() noexcept {}

#endif

}  // namespace cs2::control
