#pragma once

// The payload's control-channel server (ADR-0020). A Win32 named pipe carrying
// newline-delimited JSON; commands are `ping`, `state`, `exec`, `events`,
// `wait_for` and `quit` (`docs/control-harness.md`).
//
// The service runs on its own thread and never touches the render/present path.
// It is self-healing: `state`/`wait_for` re-attach the snapshot reader when the
// client module moves (game restart / level change).

#include <atomic>
#include <optional>
#include <string>

#include "cs2/control/channel.hpp"
#include "cs2/control/console.hpp"
#include "cs2/control/events.hpp"
#include "cs2/control/input.hpp"
#include "cs2/control/json.hpp"
#include "cs2/game/state_reader.hpp"

namespace cs2::control {

class Service {
 public:
  Service();
  ~Service();
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  // Create the pipe and start the control thread. `CS2_CONTROL_PIPE` overrides
  // the pipe name. Returns false when the pipe cannot be created.
  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept {
    return running_.load();
  }

  // Drive the payload's movement features (`docs/re/subtick.md`): the overlay
  // menu's settings and the payload's env call this, and the `strafe`/`bhop`
  // control commands set the same flags (the last writer wins).
  void set_movement_features(bool auto_strafe, bool bhop) noexcept;

  // Note the local pawn for the movement features (the `usercmd` on-ground test
  // and the bhop prediction). Called from the per-frame overlay refresh, so the
  // pawn stays fresh even when no scenario is polling `state`.
  void note_local_pawn(std::uintptr_t pawn) noexcept { input_.note_local_pawn(pawn); }

  // The bhop prediction's current position, for the overlay's demonstration box.
  auto predicted_origin(float out[3]) const noexcept -> bool {
    return input_.predicted_origin(out);
  }

 private:
#if defined(_WIN32)
  static unsigned long __stdcall thread_entry(void* self) noexcept;
  void run() noexcept;

  // Handle one NDJSON request, setting `response`. Returns false when the
  // client asked to quit.
  [[nodiscard]] bool handle(const std::string& line, std::string& response) noexcept;

  [[nodiscard]] Json ping_result() noexcept;
  // Milliseconds since the payload was installed (the control service is built
  // right after injection). The harness waits for this before the first console
  // line: `exec` faults inside the engine's Cbuf path when issued too early.
  [[nodiscard]] std::uint64_t uptime_ms() const noexcept;
  void do_state(Json& out) noexcept;
  void do_exec(const Json& args, Json& out) noexcept;
  void do_events(const Json& args, Json& out) noexcept;
  void do_wait_for(const Json& args, Json& out) noexcept;
  void do_input(const Json& args, Json& out) noexcept;
  void do_press(const Json& args, Json& out, bool release) noexcept;
  void do_move(const Json& args, Json& out) noexcept;
  void do_look(const Json& args, Json& out) noexcept;
  void do_bhop(const Json& args, Json& out) noexcept;
  // Toggle the manual engine-simulation (`RunCommand`) bhop path (off by default).
  void do_predict(const Json& args, Json& out) noexcept;
  // Auto-strafe (`set_strafe`): airborne-only silent air-strafing.
  void do_strafe(const Json& args, Json& out) noexcept;
  // The common `input`/`press`/`bhop` status payload.
  [[nodiscard]] Json input_status() noexcept;

  // Note the local pawn for the `usercmd` auto-jump on-ground test.
  void note_local_pawn(const game::Snapshot& snapshot) noexcept {
    input_.note_local_pawn(snapshot.local_pawn);
  }

  // Attach (or drop) the snapshot reader depending on readiness.
  void refresh_reader(bool last_snapshot_valid) noexcept;

  void* thread_ = nullptr;  // HANDLE
#endif
  std::atomic<bool> running_{false};
  std::uint64_t start_tick_ = 0;  // ms, from `GetTickCount64` at construction
  Channel channel_;
  Console console_;
  EventLog events_;
  Input input_;
  std::optional<game::StateReader> reader_;
};

}  // namespace cs2::control
