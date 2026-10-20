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
#include "cs2/control/rage_config.hpp"
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

  // The auto-strafe's own knobs (the wire's `movement_strafe_*` bytes): how the
  // goal heading is derived (`mode`: 0 = off, 1 = the camera's forward/back —
  // the legacy goal, 2 = the player's W/A/S/D intent), whether a held `A`/`D`
  // stands the feature down (`ignore_keys` — the legacy rule), the analog's
  // wish speed (`wishspeed`, 0 keeps the current one) and the number of
  // sub-tick `yaw_delta` steps (`subticks`, 0 = the camera-frame analog).
  void set_strafe_features(int mode, bool ignore_keys, float wishspeed, int subticks) noexcept;

  // Drive the ragebot (`docs/ragebot.md`): the overlay menu's Ragebot tab and the
  // `rage` control command set the same state. `key` is a Win32 virtual-key code
  // (0 = none), held to trigger the solve. `autostop`/`autoscope` are modes
  // (0 = off, 1 = always while solving, 2 = only while firing); `autoreload` and
  // `only_visible` are flags.
  void set_rage_features(bool on, int key, bool fire, int objective, int hitbox, float fov,
                         float min_damage, float min_hitchance, int autostop, int autoscope,
                         bool autoreload, bool only_visible, int mode, bool per_weapon,
                         const char* weapon, int config_op, std::uint32_t config_token, bool recoil,
                         float stop_speed, bool backtrack) noexcept;

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
  // The ragebot solve, run on the **game thread** from the CreateMove hook
  // (`on_game_thread`) so the aim belongs to the command being simulated — not from the
  // render thread. Uses the latest snapshot (the hitboxes are re-read fresh here) and the
  // occlusion hulls the hook just enumerated (`docs/ragebot-perf.md` §3.3).
  void rage_tick(const game::Snapshot& snapshot) noexcept;
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
  // The ragebot's usability gate (`docs/ragebot.md`): the target FOV (degrees; 0 =
  // unlimited), the hitbox filter (0 auto, 1 head, 2 body) and the aim/fire thresholds.
  std::atomic<int> rage_hitbox_{0};
  std::atomic<float> rage_fov_{0.0F};
  std::atomic<float> rage_min_damage_{0.0F};
  std::atomic<float> rage_min_hitchance_{0.0F};
  // The common ragebot assists: auto-stop mode, auto-scope mode (0 off / 1 always /
  // 2 when firing), auto-reload, and "only fire on an unattenuated (visible) shot".
  std::atomic<int> rage_autostop_{0};
  std::atomic<int> rage_autoscope_{0};
  std::atomic<bool> rage_autoreload_{false};
  std::atomic<bool> rage_only_visible_{false};
  // Recoil compensation, and the auto-stop speed threshold in units/s (0 = full stop).
  std::atomic<bool> rage_recoil_{true};
  std::atomic<float> rage_stop_speed_{0.0F};
  // Lag-record backtracking (`docs/ragebot.md` §8). Off by default: each pose is a full
  // solve and the record sampling itself costs ~0.6 ms/tick. The feature is retained and
  // re-enableable from the menu / `rage backtrack=1`.
  std::atomic<bool> rage_backtrack_{false};
  // True when the last solve's aim reached the target with no wall in front.
  std::atomic<bool> rage_visible_{false};
  // The trigger mode (0 = hold key, 1 = always on, 2 = toggle key) and its toggle state.
  std::atomic<int> rage_mode_{0};
  bool rage_key_down_ = false;
  bool rage_toggle_ = false;
  // Per-weapon profiles (`rage_config.hpp`), persisted to the payload data file. The
  // menu's "weapon key" and the pending config op arrive with the settings; the last
  // applied token is remembered so one click runs one op.
  std::atomic<bool> rage_per_weapon_{false};
  std::atomic<std::uint32_t> rage_config_token_{0};
  std::mutex rage_config_mutex_;
  char rage_weapon_[24] = {};
  RageConfigStore rage_profiles_{};
  void apply_rage_config_op(int op, const char* weapon) noexcept;
  // The tuning actually used by the solve: the per-weapon profile when `per_weapon` is
  // on and the active weapon has one, else the global settings.
  struct RageTuning {
    int objective = 1;
    int hitbox = 0;
    int mode = 0;
    int autostop = 0;
    int autoscope = 0;
    bool autoreload = false;
    bool only_visible = false;
    bool fire = false;
    bool recoil = true;
    int key = 0;
    float fov = 0.0F;
    float min_damage = 0.0F;
    float min_hitchance = 0.0F;
    float stop_speed = 0.0F;
  };
  [[nodiscard]] auto effective_rage_tuning(const char* active_weapon) noexcept -> RageTuning;
  // Auto-scope is driven through the engine's own `+zoom`/`-zoom` (Cbuf), the only
  // channel the game's zoom actually reads (measured live). The render thread sets the
  // wanted state; the game-thread sink applies the transition via `+zoom`/`-zoom`.
  std::atomic<bool> rage_scope_wanted_{false};
  bool rage_scope_applied_ = false;
  std::atomic<std::uint64_t> rage_solves_{0};
  std::atomic<int> rage_last_target_{-1};
  // Backtracking (`docs/ragebot.md` §8): how many solves chose a lag record over the
  // live pose, and the tick of the last chosen record (-1 = live).
  std::atomic<std::uint64_t> rage_backtracks_{0};
  std::atomic<int> rage_last_record_{-1};
  // The number of world solids projected into the last rage solve's chart (the
  // wallbang-aware occlusion, `docs/ragebot.md` §3). 0 = no solids enumerated.
  std::atomic<int> rage_occluders_{0};
  std::atomic<bool> rage_occlusion_candidates_complete_{true};
  std::atomic<bool> rage_occlusion_query_complete_{false};
  std::atomic<std::uint32_t> rage_occlusion_query_issues_{
      static_cast<std::uint32_t>(OcclusionQueryIssue::not_invoked)};
  // Ragebot instrumentation (`docs/ragebot-perf.md`). `on_snapshot` accumulates
  // per-stage wall time (µs) and the last frame's counts; the `rage` command reads
  // and resets them so each poll reports the interval. This is how the solve's cost
  // is attributed on the live build without a profiler.
  struct RagePerf {
    std::atomic<std::uint64_t> frames{0};           // rage-path invocations in the interval
    std::atomic<std::uint64_t> snapshot_frames{0};  // on_snapshot (render-thread) calls
    std::atomic<std::uint64_t> sample_us{0};
    std::atomic<std::uint64_t> query_us{0};      // the world occlusion enumeration
    std::atomic<std::uint64_t> build_us{0};      // build_input over the candidates
    std::atomic<std::uint64_t> project_us{0};    // occluder silhouette projection
    std::atomic<std::uint64_t> solve_us{0};      // aim::solve (multi-target field)
    std::atomic<std::uint64_t> backtrack_us{0};  // aim::solve_backtrack (all poses)
    // Breakdown of the solve(s) (`SolveStats`): hitbox projection, occlusion split and
    // the objective search. Tells us whether the cost is geometry or the erf-based mass.
    std::atomic<std::uint64_t> solve_project_us{0};
    std::atomic<std::uint64_t> solve_occlude_us{0};
    std::atomic<std::uint64_t> solve_argmax_us{0};
    std::atomic<std::uint64_t> total_us{0};       // whole rage path
    std::atomic<std::uint64_t> last_total_us{0};  // the last frame's whole-rage time
    std::atomic<std::uint64_t> max_total_us{0};   // worst single frame
    std::atomic<std::uint64_t> solve_calls{0};    // aim::solve calls (multi + backtrack)
    std::atomic<std::uint32_t> candidates{0};     // enemies in the FOV
    std::atomic<std::uint32_t> hitboxes{0};       // solver hitbox silhouettes
    std::atomic<std::uint32_t> occluders{0};      // projected world solids
    std::atomic<std::uint32_t> poses{0};          // backtrack poses (live + records)
    std::atomic<std::uint32_t> cells{0};          // field cells after occlusion
  };
  RagePerf perf_;
  std::uint64_t rage_perf_log_frames_ = 0;
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
  // Diagnostic: 0 = the per-target pyramid enumeration (the default), 1 = the older
  // single eye→all-targets AABB. Both are exact; the AABB is the conservative baseline
  // used to prove the pyramid drops no occluder (`docs/ragebot-perf.md` §2.1).
  std::atomic<int> occlusion_enum_mode_{0};
  std::atomic<bool> occlusion_request_{false};
  std::mutex occlusion_mutex_;
  game::Vec3 occlusion_eye_{};
  // The enumeration AABB: the eye plus **every** target's eye position. It is
  // deliberately independent of the camera angles — the solids between the eye and
  // the targets do not depend on where the player is looking (`docs/ragebot.md` §3).
  game::Vec3 occlusion_lo_{};
  game::Vec3 occlusion_hi_{};
  // The target hitboxes' world AABBs, so the world enumeration is the union of the
  // eye→target pyramids rather than the single eye→all-targets box
  // (`docs/ragebot-perf.md` §2.1). Bounded: the multi-target solve caps candidates.
  static constexpr int k_max_occlusion_targets = 64;
  aim::Aabb occlusion_targets_[k_max_occlusion_targets] = {};
  int occlusion_target_count_ = 0;
  // Entity-handle indices (`handle & 0x7fff`) of the players, so the occlusion query
  // skips their collision bodies (a player is a target, not an occluder).
  static constexpr int k_max_occlusion_exclude = 64;
  std::uint16_t occlusion_exclude_[k_max_occlusion_exclude] = {};
  std::uintptr_t occlusion_pawns_[k_max_occlusion_exclude] = {};
  int occlusion_exclude_count_ = 0;
  bool occlusion_inputs_complete_ = true;
  std::vector<OcclusionHull> occlusion_hulls_;
  OcclusionQueryStats occlusion_stats_;
  // The latest snapshot handed over by the render thread's per-frame refresh, for the
  // game-thread solve. Copied under the mutex so the render thread never blocks on the
  // solve. (The hitboxes are re-read on the hook, so only the player list / eye / weapon
  // come from here — all slow-changing.)
  std::mutex snapshot_mutex_;
  game::Snapshot last_snapshot_;
  std::atomic<bool> have_snapshot_{false};
  Channel channel_;
  Console console_;
  EventLog events_;
  Input input_;
  std::optional<game::StateReader> reader_;
};

}  // namespace cs2::control
