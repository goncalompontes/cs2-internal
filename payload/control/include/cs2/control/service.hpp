#pragma once

// The payload's control-channel server (ADR-0020). A Win32 named pipe carrying
// newline-delimited JSON; commands are `ping`, `state`, `exec`, `events`,
// `wait_for` and `quit` (`docs/control-harness.md`).
//
// The service runs on its own thread and never touches the render/present path.
// It is self-healing: `state`/`wait_for` re-attach the snapshot reader when the
// client module moves (game restart / level change).

#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "cs2/aim/heatmap.hpp"
#include "cs2/aim/records.hpp"
#include "cs2/control/channel.hpp"
#include "cs2/control/console.hpp"
#include "cs2/control/events.hpp"
#include "cs2/control/input.hpp"
#include "cs2/control/json.hpp"
#include "cs2/control/occlusion_query.hpp"
#include "cs2/control/rage_config.hpp"
#include "cs2/control/settings_access.hpp"
#include "cs2/control/trigger.hpp"
#include "cs2/game/state_reader.hpp"

namespace cs2::control {

// The command-surface version: the `ping` result's `protocol`.
//
// The channel was not versioned, so a stale client and a new payload could
// disagree silently. It starts at 2: the surface the harness documents as
// "v1" plus the `settings`, `batch` and `log` commands.
inline constexpr std::int64_t k_protocol_version = 2;

// A player's visibility state.
//
// The features module has its own spelling of the same three states
// (`cs2::features::Visibility`); the control module cannot include that
// header (its build declares no dependency on the features module), so the
// payload's registration maps the two.
enum class Visibility : std::uint8_t {
  unknown = 0,  // not known: the ESP treats the player as visible
  visible = 1,
  hidden = 2,
};

// One captured sound event, positioned in the world.
//
// `player_index` is the snapshot's player index (the key the ESP looks the
// player up with), `origin` the sounding player's position when the event was
// seen and `radius` the sound's own radius (0 for a footstep, which carries
// none). `sequence` is the event's sequence number, which the ESP uses to
// spot the events it has not seen yet.
struct SoundEvent {
  int player_index = -1;
  game::Vec3 origin{};
  float radius = 0.0F;
  std::uint64_t age_ms = 0;
  std::uint64_t sequence = 0;
};

// The sound fields of one captured event.
//
// `player_footstep` carries only `userid` (the game's **player slot**);
// `player_sound` carries the `radius` as well. The ring stores the values as
// strings, so they are parsed here — no allocation.
struct SoundFields {
  int slot = -1;  // the event's `userid`
  float radius = 0.0F;
  bool valid = false;  // the entry is a sound event with a usable `userid`
};

// The sound fields of a captured event, or an invalid result.
[[nodiscard]] inline auto read_sound_fields(const EventEntry& entry) noexcept -> SoundFields {
  SoundFields fields;
  const std::string_view name(entry.name);
  if (name != "player_footstep" && name != "player_sound") {
    return fields;
  }
  for (std::size_t i = 0; i < entry.arg_count; ++i) {
    const std::string_view key(entry.args[i].key);
    const std::string_view value(entry.args[i].value);
    if (key == "userid") {
      int slot = -1;
      const auto result = std::from_chars(value.data(), value.data() + value.size(), slot);
      if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return fields;  // an unusable userid: the event cannot be placed
      }
      fields.slot = slot;
    } else if (key == "radius") {
      float radius = 0.0F;
      const auto result = std::from_chars(value.data(), value.data() + value.size(), radius);
      if (result.ec == std::errc{}) {
        fields.radius = radius;
      }
    }
  }
  fields.valid = fields.slot >= 0;
  return fields;
}

// A player the sound provider can place a marker on.
struct SoundPlayer {
  int index = -1;       // the snapshot's entity index (the ESP's lookup key)
  game::Vec3 origin{};  // the player's world origin (the feet)
};

// The player a sound event came from, or null.
//
// The event's `userid` is the game's **player slot** (0..63 — the engine's
// `GetPlayerIndex` returns the slot), while the snapshot keys its players by
// the controller's **entity index** (the identity list's index, which runs
// into the hundreds). The payload has no slot -> entity table (the engine's
// own slot table is not exposed to it), so the slot is resolved by its
// **rank** in the snapshot's player list: the client creates the player
// controllers in the server's slot order, so the rank over the players is
// the slot.
//
// An exact match on the entity index is the fallback (it is right when the
// game's slot and the controller's entity index agree).
//
// The position is an approximation either way: it is the player's position
// when the event was *seen*, not when the sound was made.
[[nodiscard]] inline auto resolve_sound_player(const SoundPlayer* players, int count,
                                               int slot) noexcept -> const SoundPlayer* {
  if (players == nullptr || slot < 0) {
    return nullptr;
  }
  if (slot < count) {
    return &players[slot];  // the rank (the client creates the controllers in slot order)
  }
  for (int i = 0; i < count; ++i) {
    if (players[i].index == slot) {
      return &players[i];  // the exact match
    }
  }
  return nullptr;
}

// The per-player visibility cache.
//
// The ESP's provider reads it from the render thread; the game thread
// publishes the states it derived from the world-occlusion query. The cache
// is what makes the feature cheap: the query behind it runs at a low rate
// (2-4 Hz) on the game thread, never per frame.
class VisibilityCache {
 public:
  // The players the cache can hold (a match has at most 64).
  static constexpr int k_capacity = 64;
  // A state older than this is stale: the provider answers `unknown` rather
  // than a state the world may have changed since.
  static constexpr std::uint64_t k_ttl_ms = 1000;

  // The state of `player_index` at `now_ms`: `unknown` when it was never
  // published, when it is `unknown`, or when the published state is stale.
  [[nodiscard]] auto get(int player_index, std::uint64_t now_ms) const noexcept -> Visibility {
    for (const Entry& entry : entries_) {
      if (entry.player_index != player_index) {
        continue;
      }
      if (entry.state == Visibility::unknown) {
        return Visibility::unknown;
      }
      if (now_ms < entry.stamp_ms || now_ms - entry.stamp_ms > k_ttl_ms) {
        return Visibility::unknown;  // stale: fail closed
      }
      return entry.state;
    }
    return Visibility::unknown;
  }

  // Publish `player_index`'s state, stamped at `now_ms`.
  void publish(int player_index, Visibility state, std::uint64_t now_ms) noexcept {
    for (Entry& entry : entries_) {
      if (entry.player_index == player_index) {
        entry.state = state;
        entry.stamp_ms = now_ms;
        return;
      }
    }
    // A free slot first, then the oldest state (the cache is bounded and the
    // players come and go).
    Entry* target = nullptr;
    for (Entry& entry : entries_) {
      if (entry.state == Visibility::unknown) {
        target = &entry;
        break;
      }
      if (target == nullptr || entry.stamp_ms < target->stamp_ms) {
        target = &entry;
      }
    }
    if (target == nullptr) {
      return;
    }
    target->player_index = player_index;
    target->state = state;
    target->stamp_ms = now_ms;
  }

  // Forget every state (the world changed: a new round, a new map).
  void clear() noexcept {
    for (Entry& entry : entries_) {
      entry = Entry{};
    }
  }

 private:
  struct Entry {
    int player_index = -1;
    Visibility state = Visibility::unknown;
    std::uint64_t stamp_ms = 0;
  };

  std::array<Entry, k_capacity> entries_{};
};

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

  // Register the access to the live feature settings block (ADR-0021).
  //
  // The block is **owned** elsewhere: the in-process overlay menu owns it, and
  // in the Vulkan-layer mode the shared segment does (the layer's menu writes
  // it, the payload publishes it). The owner registers a reader and a writer,
  // so the `settings` command reads and writes the same block the menu owns —
  // a control-side change and a menu-side change are one block, and the
  // owner's next frame does not overwrite the change.
  void set_settings_access(SettingsReader reader, SettingsWriter writer, void* context) noexcept;

  // The same, with a readiness probe: whether the owner has published the
  // block yet. A control-side write must not touch a block the control channel
  // has never seen.
  void set_settings_access(SettingsReader reader, SettingsWriter writer, SettingsReady ready,
                           void* context) noexcept;

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

  // Configure the triggerbot (`docs/ragebot.md` §"The trigger"): the `trigger`
  // control command and the payload's environment (`CS2_PAYLOAD_TRIGGER`) set the
  // same state. The trigger's settings are **not** in the wire settings block (the
  // block is versioned and full), so they travel through the control channel and
  // the environment, like the ragebot's own tuning.
  //
  // The trigger never writes a view angle: it only pulls the trigger through
  // the **feature** button path (`Input::press_feature`), which reaches the
  // command in every backend — the control channel's own `press` is applied
  // only in the `usercmd` backend, and the trigger used to go through it.
  void set_trigger_features(const TriggerConfig& config) noexcept;

  // Run the ragebot for one state refresh. The overlay's per-frame sink calls this
  // with the current snapshot; the solve runs on the render thread and stashes the
  // aim, which `Input::apply()` writes per tick as the silent sub-tick view angles.
  void on_snapshot(const game::Snapshot& snapshot) noexcept;

  // The ragebot's one-line status for the HUD (false when there is nothing to
  // show, e.g. the feature is off).
  auto rage_status(char* out, std::size_t size) const noexcept -> bool;

  // The movement features' one-line status for the HUD (false when neither the
  // auto-strafe nor the bhop is on).
  //
  // It carries the two numbers that answer "does the movement accelerate?":
  // the **gain** of the last airtime (u/s) and whether the engine applied the
  // strafe's basis rotation. The second is the gate: a server that ignores the
  // sub-tick view angles (`sv_subtick_movement_view_angles 0`) makes the
  // silent encoding inert, and the operator has to see that rather than guess
  // (`docs/re/strafing.md` §"The gate").
  auto movement_status(char* out, std::size_t size) const noexcept -> bool;

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

  // --- The ESP's out-of-module data providers ------------------------------
  //
  // The features module cannot reach the control module's event ring or the
  // engine's world solids, so the ESP asks for them through the provider
  // callbacks (`cs2::features::EspProviders`). The control module cannot name
  // the features module's provider types either (its Meson build declares no
  // dependency on the features module), so the types above are the control
  // module's own spelling of the answers and the payload's registration maps
  // the two.

  // The recent sound events (the footstep markers), oldest first. Returns the
  // number written (<= `max`).
  //
  // The cost is one call per ESP refresh (~30 Hz), not per frame, and it
  // allocates nothing: the ring's events are reduced into a small stack
  // buffer while the ring's lock is held, and the players are read from a
  // fixed-size table the snapshot refresh fills.
  auto recent_sounds(SoundEvent* out, int max) noexcept -> int;

  // A player's visibility state: `unknown` when it is not known. The ESP
  // leaves an unknown player alone — it never hides a player it cannot judge
  // and never fires the glow on a guess.
  //
  // The answer comes from a cache the game thread fills at a low rate (2-4
  // Hz) from the world-occlusion enumeration; this call is a cache read, so
  // it is safe to make it per drawn player per frame.
  [[nodiscard]] auto visibility(int player_index) const noexcept -> Visibility;

  // The ESP's visibility toggles (the glow and the visible-only filter). The
  // world-occlusion query behind the visibility state is the expensive part
  // (the ragebot's, measured at ~1 ms and worse at dense spots), so it only
  // runs while one of the two is on.
  void set_esp_features(bool glow, bool visible_only) noexcept;

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
  // The feature settings block (`settings`): read and write the live block by
  // its flat wire field names.
  void do_settings(const Json& args, Json& out) noexcept;
  // Run a list of commands in order (`batch`). Returns false when a step
  // closed the channel (a `quit` step).
  [[nodiscard]] bool do_batch(const Json& args, Json& out) noexcept;
  // The payload's own log tail (`log`).
  void do_log(const Json& args, Json& out) noexcept;
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
  // The triggerbot (`docs/ragebot.md` §"The trigger"): configure / query the
  // trigger (`on`, `hitchance`, `head_hitchance`, `yaw_tolerance`,
  // `pitch_tolerance`, `delay_ms`, `key`, `fire`, `mode`). With no arguments it
  // returns the current state.
  void do_trigger(const Json& args, Json& out) noexcept;
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
  // The ESP's visibility: the per-player states, derived from the solids the
  // occlusion enumeration produced. Runs on the game thread (the render thread
  // only reads the cache), at a low rate.
  void refresh_visibility(const game::Snapshot& snapshot) noexcept;
  // Subscribe the two sound events the ESP's markers need (lazily: the
  // footsteps are the game's highest-rate event, so they are only captured
  // once the sound ESP is actually used).
  void ensure_sound_subscription() noexcept;
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

  // Run one batch step through the service's own dispatcher, so a step
  // behaves exactly like a direct request. Returns false when the channel
  // must close.
  static auto dispatch_step(void* context, const Json& step, Json& response) noexcept -> bool;

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
  // The ESP's visibility cache. The provider reads it from the render thread;
  // the game thread publishes the states it derived from the world solids.
  mutable std::mutex visibility_mutex_;
  VisibilityCache visibility_{};
  // The ESP asked for a visibility state (the world query is armed) and the
  // visibility toggles are on (the query may run at all). `mutable`: the
  // provider is a const read, but the ask arms the query.
  mutable std::atomic<bool> visibility_wanted_{false};
  std::atomic<bool> visibility_enabled_{false};
  // The low-rate gate: when the world query was last requested for the
  // visibility, and when the states were last published.
  std::atomic<std::uint64_t> visibility_query_ms_{0};
  std::atomic<std::uint64_t> visibility_publish_ms_{0};
  // The sound provider's player table: the entity index and the world origin
  // of every player in the latest snapshot. A fixed array, so the provider's
  // hot path never touches the snapshot's vectors (and never allocates).
  static constexpr int k_max_sound_players = 64;
  // The players the visibility refresh traces a ray to (the same bound).
  static constexpr int k_max_visibility_players = 64;
  std::mutex sound_mutex_;
  std::array<SoundPlayer, k_max_sound_players> sound_players_{};
  int sound_player_count_ = 0;
  // The sound events are subscribed on the first provider call.
  std::atomic<bool> sound_subscribed_{false};
  Channel channel_;
  Console console_;
  EventLog events_;
  Input input_;
  // The triggerbot (`docs/ragebot.md` §"The trigger"). It runs on the **game
  // thread** (the same sink the ragebot's `rage_tick` runs in) and never writes
  // a view angle.
  Trigger trigger_{};
  std::optional<game::StateReader> reader_;
  // The access to the live feature settings block (ADR-0021), registered by
  // the block's owner (`payload_main.cpp`).
  SettingsAccess settings_access_;
  // Set when a batch step closed the channel (a `quit` step).
  bool batch_closed_ = false;
};

}  // namespace cs2::control
