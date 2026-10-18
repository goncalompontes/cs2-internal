#pragma once

// The payload's control-channel server (ADR-0020). A Win32 named pipe carrying
// newline-delimited JSON; commands are `ping`, `state`, `exec`, `events`,
// `wait_for` and `quit` (`docs/control-harness.md`).
//
// The service runs on its own thread and never touches the render/present path.
// It is self-healing: `state`/`wait_for` re-attach the snapshot reader when the
// client module moves (game restart / level change).

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "cs2/aim/heatmap.hpp"
#include "cs2/aim/records.hpp"
#include "cs2/control/channel.hpp"
#include "cs2/control/console.hpp"
#include "cs2/control/events.hpp"
#include "cs2/control/input.hpp"
#include "cs2/control/json.hpp"
#include "cs2/control/occlusion_query.hpp"
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

  // Drive the ragebot (`docs/ragebot.md`): the overlay menu's Ragebot tab and the
  // `rage` control command set the same state. `key` is a Win32 virtual-key code
  // (0 = none), held to trigger the solve.
  void set_rage_features(bool on, int key, bool fire) noexcept;

  // Run the ragebot for one state refresh. The overlay's per-frame sink calls this
  // with the current snapshot; the solve runs on the render thread and stashes the
  // aim, which `Input::apply()` writes per tick as the silent sub-tick view angles.
  void on_snapshot(const game::Snapshot& snapshot) noexcept;

  // The ragebot's one-line status for the HUD (false when there is nothing to
  // show, e.g. the feature is off).
  auto rage_status(char* out, std::size_t size) const noexcept -> bool;

  // The ragebot's current target entity index (-1 = none), so the overlay can
  // highlight it.
  [[nodiscard]] int rage_target_index() const noexcept {
    return input_.rage_target();
  }

  // Fill `out` (up to `max`) with `pawn`'s real studio hitbox capsules in world
  // space, for the overlay's hitbox visualiser (`docs/re/aim.md`). Returns the
  // count written.
  auto read_hitboxes(std::uintptr_t pawn, game::HitboxCapsule* out, int max) const noexcept -> int;

  // Note the local pawn for the movement features (the `usercmd` on-ground test
  // and the bhop prediction). Called from the per-frame overlay refresh, so the
  // pawn stays fresh even when no scenario is polling `state`.
  void note_local_pawn(std::uintptr_t pawn) noexcept {
    input_.note_local_pawn(pawn);
  }

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
  // Ragebot aim (docs/ragebot.md): solve the damage field for the nearest enemy
  // and report the aim direction. Diagnostic — does not write the view yet.
  void do_aim(const Json& args, Json& out) noexcept;
  // Ragebot keybind: enable / configure the hold-key ragebot (`on`, `key`,
  // `fire`, `damage`, `headshot`, `objective`).
  void do_rage(const Json& args, Json& out) noexcept;
  // The multi-target damage heatmap (`docs/ragebot.md`, ADR-0022): project every
  // enemy's hitboxes — and their sampled lag records — into one shared angular
  // chart, valued by damage, and return the cells for the Python renderer.
  void do_heatmap(const Json& args, Json& out) noexcept;
  // Append one record per live enemy to the store (called from the render thread
  // each frame, before the ragebot's own early-out).
  void sample_records(const game::Snapshot& snapshot) noexcept;
  // Cache the eye + nearest enemy for the game-thread query (runs on the payload's
  // publish thread — no engine call here).
  void cache_occlusion_targets(const game::Snapshot& snapshot) noexcept;
  // Perform the enumeration on the game's main thread (the CreateMove hook), which
  // — unlike the publish thread — has the game's TLS, so tier0's allocator works.
  void on_game_thread() noexcept;
  static void game_thread_entry(void* context) noexcept;
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
  // Ragebot configuration (menu / control command). The solve runs in
  // `on_snapshot`; the aim is stashed in `input_`.
  std::atomic<bool> rage_enabled_{false};
  std::atomic<int> rage_key_{0};
  std::atomic<bool> rage_fire_{false};
  // Bypass the physical key check (a scenario cannot synthesize one).
  std::atomic<bool> rage_force_{false};
  std::atomic<float> rage_damage_{100.0F};
  std::atomic<float> rage_headshot_{4.0F};
  std::atomic<float> rage_range_modifier_{0.98F};
  std::atomic<int> rage_objective_{1};  // 0 damage, 1 hitchance, 2 expected
  std::atomic<float> rage_sigma_{0.0F};
  std::atomic<std::uint64_t> rage_solves_{0};
  std::atomic<int> rage_last_target_{-1};
  // Backtracking (`docs/ragebot.md` §8): how many solves chose a lag record over the
  // live pose, and the tick of the last chosen record (-1 = live).
  std::atomic<std::uint64_t> rage_backtracks_{0};
  std::atomic<int> rage_last_record_{-1};
  // The number of world solids projected into the last rage solve's chart (the
  // wallbang-aware occlusion, `docs/ragebot.md` §3). 0 = no solids enumerated.
  std::atomic<int> rage_occluders_{0};
  // The multi-target heatmap (`docs/ragebot.md`, ADR-0022). The render thread
  // samples each live enemy's hitboxes per frame into `records_`; the control
  // thread reads a copy to build the heatmap. Sampling is on by default (so the
  // records are always warm); the `heatmap` command's `sample` flag can toggle it.
  static constexpr std::uintptr_t k_heatmap_capacity = 32;
  std::atomic<bool> heatmap_sampling_{true};
  std::atomic<std::int32_t> record_tick_{0};
  std::mutex records_mutex_;
  aim::RecordStore records_{k_heatmap_capacity};
  // The lag-comp window parameters (`docs/re/lagcomp.md`): the rewind is measured
  // from the local `m_nTickBase` against `sv_maxunlag` and the player interp. The
  // heatmap command can override them; the defaults are the common 64-tick values.
  std::atomic<float> lag_max_unlag_{0.2F};
  std::atomic<float> lag_interp_{0.03125F};
  // Occlusion solids, enumerated on the render thread (`sample_occlusion`) and read
  // by the `heatmap` command under this lock.
  std::atomic<bool> occlusion_live_{false};
  std::atomic<bool> occlusion_invoke_{false};
  std::atomic<bool> occlusion_request_{false};
  std::mutex occlusion_mutex_;
  game::Vec3 occlusion_eye_{};
  // The enumeration AABB: the eye plus **every** target's eye position. It is
  // deliberately independent of the camera angles — the solids between the eye and
  // the targets do not depend on where the player is looking (`docs/ragebot.md` §3).
  game::Vec3 occlusion_lo_{};
  game::Vec3 occlusion_hi_{};
  // Entity-handle indices (`handle & 0x7fff`) of the players, so the occlusion query
  // skips their collision bodies (a player is a target, not an occluder).
  static constexpr int k_max_occlusion_exclude = 64;
  std::uint16_t occlusion_exclude_[k_max_occlusion_exclude] = {};
  std::uintptr_t occlusion_pawns_[k_max_occlusion_exclude] = {};
  int occlusion_exclude_count_ = 0;
  std::vector<OcclusionHull> occlusion_hulls_;
  OcclusionQueryStats occlusion_stats_;
  Channel channel_;
  Console console_;
  EventLog events_;
  Input input_;
  std::optional<game::StateReader> reader_;
};

}  // namespace cs2::control
