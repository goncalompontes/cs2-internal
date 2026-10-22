#include "cs2/control/service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <string>
#include <string_view>

#include "cs2/aim/reader.hpp"
#include "cs2/aim/solver.hpp"
#include "cs2/aim/spread.hpp"
#include "cs2/control/occlusion_query.hpp"
#include "cs2/control/predicate.hpp"
#include "cs2/control/rage_objective.hpp"
#include "cs2/control/snapshot_json.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/tls/bootstrap.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {

Service::~Service() {
  stop();
}

namespace {

// The payload's millisecond clock — the same clock the event log stamps the
// captured events with, so the sound provider's ages are real.
auto now_ms() noexcept -> std::uint64_t {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetTickCount64());
#else
  return 0;
#endif
}

// The prediction tick the seed context uses: the local controller's
// `m_nTickBase`, the tick the client is simulating (`docs/re/lagcomp.md`).
//
// **It is the fallback for the command's sequence number**, which is not
// reachable from where the service sits: the `Input` module owns the
// `CUserCmd` pointer and exposes no accessor for the command's number, and the
// one that exists (`bhop_cmd_number`) is refreshed by the **bunnyhop assist**
// alone — so it is zero when the assist is off and stale when it is on. A
// number that is the game's own must not be presented as such, so the tick
// stands in for it and the status says the seed is a fallback.
[[nodiscard]] auto prediction_tick(const game::Snapshot& snapshot) noexcept -> std::uint32_t {
  if (!snapshot.local.has_value() || snapshot.local->tick_base <= 0) {
    return 0U;
  }
  return static_cast<std::uint32_t>(snapshot.local->tick_base);
}

// The sound markers the ESP keeps (its sound cache's capacity) and the age
// beyond which a captured sound is no longer a marker.
constexpr int k_max_sound_markers = 32;
constexpr std::uint64_t k_sound_max_age_ms = 1000;

// The visibility refresh's rate: the world-occlusion enumeration behind it is
// the ragebot's, measured at ~1 ms (and worse at dense spots), so it runs a
// few times a second — never per frame.
constexpr std::uint64_t k_visibility_interval_ms = 250;

// Microsecond wall clock for the ragebot instrumentation (`docs/ragebot-perf.md`).
auto now_us() noexcept -> std::uint64_t {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

// Adds the scope's elapsed microseconds to `total` and keeps the running maximum in
// `max_value` (a lock-free CAS). Used to time one rage-path frame across all its
// early returns.
struct FrameScope {
  std::atomic<std::uint64_t>* total = nullptr;
  std::atomic<std::uint64_t>* last = nullptr;
  std::atomic<std::uint64_t>* max_value = nullptr;
  std::uint64_t start = 0;

  FrameScope(std::atomic<std::uint64_t>& total_ref, std::atomic<std::uint64_t>& last_ref,
             std::atomic<std::uint64_t>& max_ref) noexcept
      : total(&total_ref), last(&last_ref), max_value(&max_ref), start(now_us()) {}
  ~FrameScope() {
    const std::uint64_t elapsed = now_us() - start;
    total->fetch_add(elapsed, std::memory_order_relaxed);
    last->store(elapsed, std::memory_order_relaxed);
    std::uint64_t previous = max_value->load(std::memory_order_relaxed);
    while (elapsed > previous &&
           !max_value->compare_exchange_weak(previous, elapsed, std::memory_order_relaxed)) {
    }
  }
};

}  // namespace

#if defined(_WIN32)
namespace {

constexpr DWORD k_poll_ms = 100;
constexpr std::int64_t k_default_timeout_ms = 30000;

// The `log` command: how many lines the payload's own log tail returns (the
// default and the cap).
constexpr std::size_t k_default_log_lines = 50;
constexpr std::size_t k_max_log_lines = 500;
// How much of the log file's end is read (the tail is taken from it).
constexpr std::uint64_t k_log_window_bytes = 64U * 1024U;

// Extend an AABB (`lo`/`hi`) to contain `point`.
void extend_point(game::Vec3& lo, game::Vec3& hi, const game::Vec3& p) noexcept {
  lo.x = p.x < lo.x ? p.x : lo.x;
  lo.y = p.y < lo.y ? p.y : lo.y;
  lo.z = p.z < lo.z ? p.z : lo.z;
  hi.x = p.x > hi.x ? p.x : hi.x;
  hi.y = p.y > hi.y ? p.y : hi.y;
  hi.z = p.z > hi.z ? p.z : hi.z;
}

// Extend an AABB to contain a hitbox's world geometry: a capsule's endpoints ± its
// radius, or an oriented box's corners.
void extend_box(game::Vec3& lo, game::Vec3& hi, const aim::Hitbox& hitbox) noexcept {
  if (hitbox.is_capsule) {
    const float r = hitbox.capsule.radius;
    const game::Vec3 a = hitbox.capsule.a;
    const game::Vec3 b = hitbox.capsule.b;
    extend_point(lo, hi, game::Vec3{.x = a.x - r, .y = a.y - r, .z = a.z - r});
    extend_point(lo, hi, game::Vec3{.x = a.x + r, .y = a.y + r, .z = a.z + r});
    extend_point(lo, hi, game::Vec3{.x = b.x - r, .y = b.y - r, .z = b.z - r});
    extend_point(lo, hi, game::Vec3{.x = b.x + r, .y = b.y + r, .z = b.z + r});
    return;
  }
  for (const game::Vec3& corner : hitbox.box.corners()) {
    extend_point(lo, hi, corner);
  }
}

// The world AABB of one hitbox (the same geometry `extend_box` unions into the box).
void hitbox_bounds(const aim::Hitbox& hitbox, game::Vec3& lo, game::Vec3& hi) noexcept {
  if (hitbox.is_capsule) {
    const float r = hitbox.capsule.radius;
    const game::Vec3 a = hitbox.capsule.a;
    const game::Vec3 b = hitbox.capsule.b;
    // (`std::min`/`std::max` are macros under <windows.h> in this translation unit.)
    lo = game::Vec3{
        .x = a.x < b.x ? a.x : b.x, .y = a.y < b.y ? a.y : b.y, .z = a.z < b.z ? a.z : b.z};
    hi = game::Vec3{
        .x = a.x > b.x ? a.x : b.x, .y = a.y > b.y ? a.y : b.y, .z = a.z > b.z ? a.z : b.z};
    lo = game::Vec3{.x = lo.x - r, .y = lo.y - r, .z = lo.z - r};
    hi = game::Vec3{.x = hi.x + r, .y = hi.y + r, .z = hi.z + r};
    return;
  }
  lo = hitbox.box.corners().front();
  hi = lo;
  for (const game::Vec3& corner : hitbox.box.corners()) {
    extend_point(lo, hi, corner);
  }
}

void set_error(Json& out, std::string_view message) {
  out.set("ok", Json::boolean(false));
  out.set("error", Json::string(std::string(message)));
}

void set_ok(Json& out, Json value) {
  out.set("ok", Json::boolean(true));
  out.set("result", std::move(value));
}

// Parse a button spec: a friendly name, an array of names, or a raw bit mask.
// Returns false (with `bad` set) on an unknown name.
auto parse_buttons(const Json& value, std::uint64_t& mask, std::string& bad) -> bool {
  mask = 0;
  if (value.is_number()) {
    mask = static_cast<std::uint64_t>(value.as_int());
    return true;
  }
  const auto add = [&](const Json& item) -> bool {
    if (!item.is_string()) {
      bad = item.dump();
      return false;
    }
    const std::uint64_t bit = button_bit(item.as_string());
    if (bit == 0) {
      bad = std::string(item.as_string());
      return false;
    }
    mask |= bit;
    return true;
  };
  if (value.is_string()) {
    return add(value);
  }
  if (value.is_array()) {
    for (const Json& item : value.items()) {
      if (!add(item)) {
        return false;
      }
    }
    return true;
  }
  bad = value.dump();
  return false;
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
    // The control channel disconnected: the trigger's state is reset, so a
    // trigger that was holding the shot releases it (a trigger that pressed and
    // never released would leave the gun firing).
    trigger_.reset(input_);
  }
}

auto Service::handle(const std::string& line, std::string& response) noexcept -> bool {
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
  } else if (command == "events") {
    do_events(arguments, out);
  } else if (command == "wait_for") {
    do_wait_for(arguments, out);
  } else if (command == "input") {
    do_input(arguments, out);
  } else if (command == "press") {
    do_press(arguments, out, false);
  } else if (command == "release") {
    do_press(arguments, out, true);
  } else if (command == "move") {
    do_move(arguments, out);
  } else if (command == "look") {
    do_look(arguments, out);
  } else if (command == "bhop") {
    do_bhop(arguments, out);
  } else if (command == "strafe") {
    do_strafe(arguments, out);
  } else if (command == "animfix") {
    do_animfix(arguments, out);
  } else if (command == "antiaim") {
    do_antiaim(arguments, out);
  } else if (command == "aim") {
    do_aim(arguments, out);
  } else if (command == "rage") {
    do_rage(arguments, out);
  } else if (command == "trigger") {
    do_trigger(arguments, out);
  } else if (command == "heatmap") {
    do_heatmap(arguments, out);
  } else if (command == "predict") {
    do_predict(arguments, out);
  } else if (command == "settings") {
    do_settings(arguments, out);
  } else if (command == "batch") {
    if (!do_batch(arguments, out)) {
      // A step closed the channel (a `quit` step).
      response = out.dump();
      return false;
    }
  } else if (command == "log") {
    do_log(arguments, out);
  } else {
    set_error(out, "unknown cmd");
  }
  response = out.dump();
  return true;
}

Service::Service() {
#if defined(_WIN32)
  start_tick_ = static_cast<std::uint64_t>(GetTickCount64());
#endif
  // The occlusion query must run on a game thread; `Input::apply` (the CreateMove
  // hook) is one.
  input_.set_game_thread_sink(&game_thread_entry, this);
  // Restore the per-weapon ragebot profiles from the last session (`rage_config.hpp`).
  (void)rage_config_load(rage_profiles_);
}

auto Service::uptime_ms() const noexcept -> std::uint64_t {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetTickCount64()) - start_tick_;
#else
  return 0;
#endif
}

auto Service::ping_result() noexcept -> Json {
  console_.refresh();
  events_.refresh();
  input_.refresh();
  Json result = Json::object();
  result.set("pong", Json::boolean(true));
  result.set("pid", Json::number(static_cast<double>(GetCurrentProcessId())));
  // The command-surface version: a stale client and a new payload can then
  // disagree loudly instead of silently (the envelope itself is unchanged, so
  // an old client still parses it).
  result.set("protocol", Json::integer(k_protocol_version));
  // How long ago the payload was injected (its own clock): the harness waits for
  // this before the first console line, because `exec` faults in the engine's
  // Cbuf path when issued too soon after injection.
  result.set("uptime_ms", Json::number(static_cast<double>(uptime_ms())));
  result.set("exec_ready", Json::boolean(console_.ready()));
  result.set("events_ready", Json::boolean(events_.ready()));
  result.set("events_subscribed", Json::number(static_cast<double>(events_.subscriptions())));
  result.set("events_dispatch_calls", Json::number(static_cast<double>(events_.fire_calls())));
  result.set("input_ready", Json::boolean(input_.ready()));
  result.set("input_hooked", Json::boolean(input_.hooked()));
  result.set("input_backend", Json::string(Input::backend_name(input_.backend())));
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
  note_local_pawn(snapshot);
  set_ok(out, to_json(snapshot));
}

// Keep the local pawn current for the animfix's capture (see the header).
void Service::refresh_local_pawn() noexcept {
  if (!reader_.has_value()) {
    refresh_reader(true);
  }
  if (!reader_.has_value()) {
    return;
  }
  const game::Snapshot snapshot = reader_->snapshot();
  refresh_reader(snapshot.valid);
  if (snapshot.valid) {
    note_local_pawn(snapshot);
  }
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

void Service::do_events(const Json& args, Json& out) noexcept {
  events_.refresh();
  if (!events_.ready()) {
    set_error(out, "events unavailable: event dispatch hooks not installed");
    return;
  }
  // Optional on-demand subscription: `subscribe` is a name or an array of names.
  // Each name is reported as accepted or rejected (a name the build does not
  // define is rejected), so a typo is visible rather than a silent no-op.
  Json accepted = Json::array();
  Json rejected = Json::array();
  const auto handle_subscribe = [&](std::string_view name) -> void {
    const std::int32_t code = events_.subscribe(name);
    if (code == EventLog::k_subscribe_already || code >= 0) {
      accepted.push_back(Json::string(std::string(name)));
    } else {
      rejected.push_back(Json::string(std::string(name)));
    }
  };
  if (const Json* subscribe = args.find("subscribe"); subscribe != nullptr) {
    if (subscribe->is_string()) {
      handle_subscribe(subscribe->as_string());
    } else if (subscribe->is_array()) {
      for (const Json& name : subscribe->items()) {
        if (name.is_string()) {
          handle_subscribe(name.as_string());
        }
      }
    }
  }
  auto limit = static_cast<std::int64_t>(EventRing::k_capacity);
  if (const Json* arg = args.find("limit"); arg != nullptr && arg->is_number()) {
    limit = arg->as_int();
  }
  limit = std::max<std::int64_t>(limit, 0);
  if (limit > static_cast<std::int64_t>(EventRing::k_capacity)) {
    // The ring holds at most `k_capacity` events, so a larger limit asks for
    // nothing (the same explicit cap the `log` command has).
    limit = static_cast<std::int64_t>(EventRing::k_capacity);
  }
  // The optional `since` cursor: only the events with `seq > since` are
  // returned, so a client does not have to diff the sequence numbers by hand.
  // `last_seq` is the highest sequence the ring holds — the client's next
  // cursor.
  std::uint64_t since = 0;
  bool has_since = false;
  if (const Json* arg = args.find("since"); arg != nullptr && arg->is_number()) {
    const std::int64_t requested = arg->as_int();
    since = static_cast<std::uint64_t>(requested > 0 ? requested : 0);
    has_since = true;
  }
  Json subscriptions = Json::object();
  subscriptions.set("accepted", std::move(accepted));
  subscriptions.set("rejected", std::move(rejected));
  Json recent = events_.recent(static_cast<std::size_t>(limit));
  if (has_since) {
    Json filtered = Json::array();
    for (const Json& item : recent.items()) {
      const Json* seq = item.find("seq");
      if (seq != nullptr && seq->as_int() > 0 &&
          static_cast<std::uint64_t>(seq->as_int()) > since) {
        filtered.push_back(item);
      }
    }
    recent = std::move(filtered);
  }
  Json result = Json::object();
  // `count` is the number of events in this answer; `last_seq` is the highest
  // sequence the ring holds (the client's next cursor). They are different
  // numbers: the ring keeps the last `k_capacity` events, while the sequence
  // counts every event the payload ever saw.
  result.set("count", Json::integer(static_cast<std::int64_t>(recent.items().size())));
  result.set("last_seq", Json::integer(static_cast<std::int64_t>(events_.count())));
  result.set("subscribe", std::move(subscriptions));
  result.set("events", std::move(recent));
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
  timeout_ms = std::max<std::int64_t>(timeout_ms, 0);

  // Optional event gate: match once `event` fires *after* this request, carrying
  // every `event_args` value. We subscribe before snapshotting the count so the
  // wait cannot miss the event.
  std::string event_name;
  if (const Json* event = args.find("event"); event != nullptr && event->is_string()) {
    event_name = std::string(event->as_string());
  }
  // `event_args` is an object of field -> expected value; values are compared as
  // strings against the captured (formatted) argument.
  std::vector<std::string> arg_storage;
  if (const Json* event_args = args.find("event_args");
      event_args != nullptr && event_args->is_object()) {
    arg_storage.reserve(event_args->members().size() * 2);  // keep the strings stable
    for (const auto& [key, value] : event_args->members()) {
      arg_storage.push_back(key);
      arg_storage.push_back(value.is_string() ? std::string(value.as_string()) : value.dump());
    }
  }
  std::vector<EventArgMatch> arg_matches;
  arg_matches.reserve(arg_storage.size() / 2);
  for (std::size_t i = 0; i + 1 < arg_storage.size(); i += 2) {
    arg_matches.push_back(EventArgMatch{.key = arg_storage[i], .value = arg_storage[i + 1]});
  }
  std::uint64_t event_after = 0;
  if (!event_name.empty()) {
    events_.refresh();
    if (!events_.ready()) {
      set_error(out, "wait_for: game events unavailable (event dispatch hooks not installed)");
      return;
    }
    if (events_.subscribe(event_name) == EventLog::k_subscribe_rejected) {
      set_error(out, "wait_for: unknown game event \"" + event_name + "\"");
      return;
    }
    event_after = events_.count();
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  bool matched = false;
  Json snapshot = Json();
  for (;;) {
    cs2::diag::heartbeat();
    bool state_ok = false;
    refresh_reader(true);
    if (reader_.has_value()) {
      const game::Snapshot current = reader_->snapshot();
      refresh_reader(current.valid);
      if (current.valid) {
        snapshot = to_json(current);
        note_local_pawn(current);
      }
      state_ok = predicate.matches(current);
    } else {
      state_ok = predicate.matches(game::Snapshot{});
    }
    const bool event_ok =
        event_name.empty() ||
        (events_.ready() &&
         events_.seen_since(event_name, event_after, arg_matches.data(), arg_matches.size()));
    if (state_ok && event_ok) {
      matched = true;
      break;
    }
    if (!running_.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    Sleep(k_poll_ms);
  }

  Json result = Json::object();
  result.set("matched", Json::boolean(matched));
  result.set("snapshot", std::move(snapshot));
  set_ok(out, std::move(result));
}

void Service::do_settings(const Json& args, Json& out) noexcept {
  if (!settings_access_.available()) {
    set_error(out, "settings unavailable: the live settings block is not registered");
    return;
  }
  if (!settings_access_.ready()) {
    set_error(out, "settings unavailable: the live settings block is not published yet");
    return;
  }
  const auto outcome = run_settings(args, settings_access_);
  if (!outcome.has_value()) {
    set_error(out, outcome.error().message);
    return;
  }
  Json result = Json::object();
  if (outcome->wrote) {
    // Only the keys whose value actually changed (a set to the value it
    // already had is not a change).
    Json changed = Json::array();
    for (const std::string& name : outcome->changed) {
      changed.push_back(Json::string(name));
    }
    result.set("changed", std::move(changed));
  }
  result.set("values", std::move(outcome->values));
  set_ok(out, std::move(result));
}

auto Service::do_batch(const Json& args, Json& out) noexcept -> bool {
  const Json* steps = args.find("steps");
  if (steps == nullptr) {
    set_error(out, "batch requires a \"steps\" array");
    return true;
  }
  batch_closed_ = false;
  Json results = Json::array();
  const auto outcome = BatchRunner::run(*steps, &Service::dispatch_step, this, results);
  if (!outcome.has_value()) {
    // A malformed batch: the whole command fails, and no step ran.
    set_error(out, outcome.error().message);
    return true;
  }
  Json result = Json::object();
  result.set("results", std::move(results));
  set_ok(out, std::move(result));
  return !batch_closed_;
}

auto Service::dispatch_step(void* context, const Json& step, Json& response) noexcept -> bool {
  auto* self = static_cast<Service*>(context);
  // The step is a request: it goes through the service's own dispatcher, so a
  // step behaves exactly like a direct request.
  Json request = Json::object();
  const Json* cmd = step.find("cmd");
  if (cmd == nullptr) {
    response.set("ok", Json::boolean(false));
    response.set("error", Json::string("the step has no \"cmd\""));
    return true;
  }
  request.set("cmd", *cmd);
  if (const Json* step_args = step.find("args"); step_args != nullptr) {
    request.set("args", *step_args);
  }
  const std::string line = request.dump();
  std::string text;
  const bool keep_going = self->handle(line, text);
  if (!keep_going) {
    self->batch_closed_ = true;
  }
  const std::optional<Json> parsed = Json::parse(text);
  if (parsed.has_value() && parsed->is_object()) {
    for (const auto& [key, value] : parsed->members()) {
      if (key == "ok" || key == "result" || key == "error") {
        response.set(key, value);
      }
    }
  }
  if (response.find("ok") == nullptr) {
    response.set("ok", Json::boolean(false));
    response.set("error", Json::string("the step produced no response"));
  }
  return keep_going;
}

void Service::do_log(const Json& args, Json& out) noexcept {
  std::size_t count = k_default_log_lines;
  if (const Json* arg = args.find("count"); arg != nullptr && arg->is_number()) {
    const auto requested = arg->as_int();
    count = requested > 0 ? static_cast<std::size_t>(requested) : 0;
  }
  count = (std::min)(count, k_max_log_lines);

  // The payload's own log file (`cs2::game::append_log` writes it). The file
  // is read — there is no second in-memory buffer.
  char path[MAX_PATH];
  if (!cs2::game::data_file_path("cs2-payload.log", path, sizeof(path))) {
    set_error(out, "log unavailable: the payload's data directory is not known");
    return;
  }
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    set_error(out, "log unavailable: the payload has not written a log file yet");
    return;
  }
  LARGE_INTEGER size{};
  if (GetFileSizeEx(file, &size) == 0) {
    CloseHandle(file);
    set_error(out, "log unavailable: the log file's size could not be read");
    return;
  }
  const auto length = static_cast<std::uint64_t>(size.QuadPart);
  const std::uint64_t window = length < k_log_window_bytes ? length : k_log_window_bytes;
  LARGE_INTEGER offset{};
  offset.QuadPart = static_cast<LONGLONG>(length - window);
  SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
  std::string text(static_cast<std::size_t>(window), '\0');
  DWORD read = 0;
  if (window > 0) {
    ReadFile(file, text.data(), static_cast<DWORD>(window), &read, nullptr);
  }
  CloseHandle(file);
  text.resize(read);

  std::vector<std::string> lines = log_tail(text, count);
  if (length > window && !lines.empty()) {
    // The window starts in the middle of a line: that first line is partial.
    lines.erase(lines.begin());
  }
  Json array = Json::array();
  for (const std::string& line : lines) {
    array.push_back(Json::string(line));
  }
  Json result = Json::object();
  result.set("path", Json::string(std::string(path)));
  result.set("count", Json::integer(static_cast<std::int64_t>(lines.size())));
  result.set("lines", std::move(array));
  set_ok(out, std::move(result));
}

auto Service::input_status() noexcept -> Json {
  const auto hex = [](std::uint64_t value) -> std::string {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
  };
  Json result = Json::object();
  result.set("backend", Json::string(Input::backend_name(input_.backend())));
  result.set("ready", Json::boolean(input_.ready()));
  result.set("hooked", Json::boolean(input_.hooked()));
  result.set("slot", Json::number(static_cast<double>(input_.slot_index())));
  result.set("slot5", Json::number(static_cast<double>(input_.slot5_index())));
  result.set("calls5", Json::number(static_cast<double>(input_.calls5())));
  result.set("buttons_set", Json::number(static_cast<double>(input_.buttons_set())));
  result.set("buttons_clear", Json::number(static_cast<double>(input_.buttons_clear())));
  result.set("bhop", Json::boolean(input_.bhop()));
  result.set("predict", Json::boolean(input_.predict()));
  result.set("move", Json::boolean(input_.move_enabled()));
  result.set("move_quantize", Json::boolean(input_.move_quantize()));
  result.set("move_step", Json::boolean(input_.move_step()));
  result.set("move_raw_entry", Json::boolean(input_.move_raw_entry()));
  result.set("move_forward", Json::number(input_.move_forward()));
  result.set("move_side", Json::number(input_.move_side()));
  result.set("move_up", Json::number(input_.move_up()));
  result.set("angles", Json::boolean(input_.angles_enabled()));
  result.set("angle_pitch", Json::number(input_.angle_pitch()));
  result.set("angle_yaw", Json::number(input_.angle_yaw()));
  result.set("angle_roll", Json::number(input_.angle_roll()));
  result.set("command_angles", Json::boolean(input_.command_angles_enabled()));
  result.set("command_angle_yaw", Json::number(input_.command_angle_yaw()));
  result.set("command_angle_writes",
             Json::number(static_cast<double>(input_.command_angle_writes())));
  result.set("raw_angles", Json::boolean(input_.raw_angles_enabled()));
  result.set("raw_angle_writes", Json::number(static_cast<double>(input_.raw_angle_writes())));
  result.set("step_yaw", Json::number(input_.step_yaw()));
  result.set("step_yaw_enabled", Json::boolean(input_.step_yaw_enabled()));
  result.set("strafe", Json::boolean(input_.strafe()));
  result.set("strafe_airborne", Json::boolean(input_.strafe_airborne()));
  result.set("strafe_speed", Json::number(input_.strafe_speed()));
  result.set("strafe_delta", Json::number(input_.strafe_delta()));
  result.set("strafe_goal", Json::number(input_.strafe_goal()));
  result.set("strafe_wish", Json::number(input_.strafe_wish()));
  result.set("strafe_rot", Json::number(input_.strafe_rot()));
  result.set("strafe_side", Json::number(static_cast<double>(input_.strafe_side())));
  result.set("strafe_steps", Json::number(static_cast<double>(input_.strafe_steps())));
  result.set("strafe_reason", Json::number(static_cast<double>(input_.strafe_reason())));
  result.set("strafe_clamped", Json::boolean(input_.strafe_clamped()));
  result.set("strafe_wishspeed", Json::number(input_.strafe_wishspeed()));
  result.set("strafe_subticks", Json::number(static_cast<double>(input_.strafe_subticks())));
  // The strafe's own knobs and what it read: the goal mode, the legacy
  // "A/D stands down" rule, the decoded view-frame intent (forward / right),
  // the dead zone's verdict and the dead zone itself.
  result.set("strafe_mode", Json::number(static_cast<double>(input_.strafe_mode())));
  result.set("strafe_ignore_keys", Json::boolean(input_.strafe_ignore_keys()));
  result.set("strafe_intent_f", Json::number(input_.strafe_intent_f()));
  result.set("strafe_intent_l", Json::number(input_.strafe_intent_l()));
  result.set("strafe_side_locked", Json::boolean(input_.strafe_side_locked()));
  result.set("strafe_side_last", Json::number(static_cast<double>(input_.strafe_side_last())));
  result.set("strafe_deadzone_deg", Json::number(input_.strafe_deadzone_deg()));
  result.set("strafe_writes", Json::number(static_cast<double>(input_.strafe_writes())));
  result.set("strafe_raw_writes", Json::number(static_cast<double>(input_.strafe_raw_writes())));
  // The anti-aim: what it wrote (or would write), the camera's own angles (the
  // proof that the camera did **not** follow the fake ones), the re-based axes
  // and the `move_crc` gate.
  result.set("anti_aim", Json::boolean(input_.anti_aim()));
  result.set("anti_aim_move_rebase", Json::boolean(input_.anti_aim_move_rebase()));
  result.set("anti_aim_acted", Json::boolean(input_.anti_aim_acted()));
  result.set("anti_aim_reason", Json::number(static_cast<double>(input_.anti_aim_reason())));
  result.set("anti_aim_fake_yaw", Json::number(input_.anti_aim_fake_yaw()));
  result.set("anti_aim_fake_pitch", Json::number(input_.anti_aim_fake_pitch()));
  result.set("anti_aim_camera_yaw", Json::number(input_.anti_aim_camera_yaw()));
  result.set("anti_aim_camera_pitch", Json::number(input_.anti_aim_camera_pitch()));
  // The **delivered** axes (a member of the engine's discrete set — what the
  // server acts on) and what they cost: the direction error against the
  // camera's own wish and the walk-speed factor.
  result.set("anti_aim_forward", Json::number(input_.anti_aim_forward()));
  result.set("anti_aim_left", Json::number(input_.anti_aim_left()));
  result.set("anti_aim_direction_error", Json::number(input_.anti_aim_direction_error()));
  result.set("anti_aim_wish_factor", Json::number(input_.anti_aim_wish_factor()));
  result.set("anti_aim_saturated", Json::boolean(input_.anti_aim_saturated()));
  result.set("anti_aim_basis_cos", Json::number(input_.anti_aim_basis_cos()));
  // The `move_crc` caveat: the payload does not recompute the command's CRC
  // (`docs/re/angle-split.md` §5.4). It is reported, not gated: the server
  // cannot reject a command whose CRC the client never writes, and this repo's
  // own measurement shows the server honours a modified analog.
  result.set("anti_aim_crc_caveat", Json::string("the command's move_crc is not "
                                                 "recomputed (base + 0x30); the "
                                                 "server is expected to ignore it "
                                                 "(docs/re/angle-split.md §5.4)"));
  result.set("anti_aim_yaw_offset", Json::number(input_.anti_aim_yaw_offset()));
  result.set("anti_aim_pitch", Json::number(input_.anti_aim_pitch()));
  result.set("anti_aim_jitter", Json::number(input_.anti_aim_jitter()));
  result.set("anti_aim_side", Json::number(static_cast<double>(input_.anti_aim_side())));
  result.set("anti_aim_writes", Json::number(static_cast<double>(input_.anti_aim_writes())));
  result.set("anti_aim_rebase_applied",
             Json::number(static_cast<double>(input_.anti_aim_rebase_applied())));
  result.set("anti_aim_rebase_skipped",
             Json::number(static_cast<double>(input_.anti_aim_rebase_skipped())));
  // The **outcome** and the **gate verdict**: the speed gained over the last
  // airtime (the number the operator asked for: "does it accelerate?") and
  // whether the engine applied the basis rotation (it does not when the server
  // ignores the sub-tick view angles — `docs/re/strafing.md` §"The gate").
  result.set("strafe_gain", Json::number(input_.strafe_gain()));
  result.set("strafe_steered", Json::boolean(input_.strafe_steered()));
  result.set("strafe_steered_ticks",
             Json::number(static_cast<double>(input_.strafe_steered_ticks())));
  result.set("strafe_ignored", Json::number(static_cast<double>(input_.strafe_ignored())));
  result.set("strafe_turn", Json::number(input_.strafe_turn()));
  result.set("bhop_presses", Json::number(static_cast<double>(input_.bhop_presses())));
  result.set("bhop_writes", Json::number(static_cast<double>(input_.bhop_writes())));
  result.set("bhop_landed_tick", Json::number(static_cast<double>(input_.bhop_landed_tick())));
  result.set("bhop_landed_frac", Json::number(static_cast<double>(input_.bhop_landed_frac())));
  result.set("bhop_build", Json::number(static_cast<double>(input_.bhop_build())));
  result.set("bhop_age", Json::number(static_cast<double>(input_.bhop_age())));
  result.set("bhop_tick_base", Json::number(static_cast<double>(input_.bhop_tick_base())));
  result.set("bhop_client_tick", Json::number(static_cast<double>(input_.bhop_client_tick())));
  result.set("bhop_cmd_number", Json::number(static_cast<double>(input_.bhop_cmd_number())));
  result.set("bhop_mode", Json::number(static_cast<double>(input_.bhop_mode())));
  result.set("bhop_when", Json::number(static_cast<double>(input_.bhop_when())));
  result.set("bhop_a", Json::number(static_cast<double>(input_.bhop_a())));
  result.set("bhop_b", Json::number(static_cast<double>(input_.bhop_b())));
  result.set("bhop_skipped", Json::number(static_cast<double>(input_.bhop_skipped())));
  result.set("bhop_holding_ticks", Json::number(static_cast<double>(input_.bhop_holding_ticks())));
  result.set("bhop_air_ticks", Json::number(static_cast<double>(input_.bhop_air_ticks())));
  result.set("input_local_pawn", Json::number(static_cast<double>(input_.local_pawn())));
  // The local animfix (`docs/re/animfix.md`): what it did, and the evidence
  // that it does something — the **server's** aim yaw against the **client's
  // predicted** one. The difference is the divergence the animfix removes.
  // `animfix_status` says why the animfix is not ready (a stale calibration
  // dump, an unhookable seam), so a feature that cannot work says so
  // instead of looking idle.
  result.set("animfix", Json::boolean(input_.animfix()));
  result.set("animfix_ready", Json::boolean(input_.animfix_ready()));
  result.set("animfix_status", Json::string(input_.animfix_status()));
  result.set("animfix_acted", Json::boolean(input_.animfix_acted()));
  result.set("animfix_captures", Json::number(static_cast<double>(input_.animfix_captures())));
  result.set("animfix_applies", Json::number(static_cast<double>(input_.animfix_applies())));
  // The units: `animfix_captures` counts the decodes of the local controller,
  // `animfix_applies` counts the **graph parameters written**, and
  // `animfix_skips` and the refusal counters count **anim ticks** (one reason
  // per tick: the capture-level reasons - no capture, aged out, the
  // controller changed - hold for every slot, so counting them per slot
  // would multiply them by the number of slots). The write path's own
  // failures are counted per **slot**, because they are per slot: the
  // parameter that could not be written is a parameter, not a tick.
  result.set("animfix_skips", Json::number(static_cast<double>(input_.animfix_skips())));
  // The refusals, by reason: "the codec has not run yet" must be
  // distinguishable from "every capture aged out" (a stalled connection or a
  // server whose snapshot rate the bound does not cover).
  result.set("animfix_skip_no_capture",
             Json::number(static_cast<double>(input_.animfix_skip_no_capture())));
  result.set("animfix_skip_stale", Json::number(static_cast<double>(input_.animfix_skip_stale())));
  // The anim ticks the driver ran, and the client ones (the correction is
  // written on the client tick: the server tick of a listen server evaluates
  // the server's own copies of the entities).
  result.set("animfix_anim_ticks", Json::number(static_cast<double>(input_.animfix_anim_ticks())));
  result.set("animfix_client_ticks",
             Json::number(static_cast<double>(input_.animfix_client_ticks())));
  // The client anim ticks that ran **while the animfix was on**: the
  // denominator of the duty cycle. The driver's own counters climb whether
  // or not the feature is on, so dividing the correction counter by them
  // would divide two counters with different windows.
  result.set("animfix_enabled_client_ticks",
             Json::number(static_cast<double>(input_.animfix_enabled_client_ticks())));
  // The correction is written on the client tick only; a payload that never
  // saw one cannot work, and this says so.
  result.set("animfix_client_tick_seen", Json::boolean(input_.animfix_client_tick_seen()));
  // The **duty cycle**: the client anim ticks on which the correction won.
  // Divided by the client anim ticks **while the animfix was on**, it is the
  // fraction of the frames that carry the server's value - a duty cycle below
  // 100 % is the oscillation at the server's tick rate that the animfix exists
  // to remove.
  result.set("animfix_corrected_ticks",
             Json::number(static_cast<double>(input_.animfix_corrected_ticks())));
  result.set("animfix_skip_controller",
             Json::number(static_cast<double>(input_.animfix_skip_controller())));
  // The graph parameters the animfix could not write: a failure of its own,
  // so the operator can tell it from "the controller does not answer".
  result.set("animfix_skip_write_failed",
             Json::number(static_cast<double>(input_.animfix_skip_write_failed())));
  // The server's snapshot rate: the staleness bound is measured against it.
  result.set("animfix_snapshot_interval_ms",
             Json::number(static_cast<double>(input_.animfix_snapshot_interval_ms())));
  // The values the engine's own guard refused (a NaN, an infinity, a value
  // outside the engine's range). It should be zero.
  result.set("animfix_rejected", Json::number(static_cast<double>(input_.animfix_rejected())));
  result.set("animfix_data_valid", Json::boolean(input_.animfix_data_valid()));
  // The **codec counters**: what the hooked codecs did. They are the
  // diagnosis of a capture that never happened — "the codec never ran" (H1)
  // against "the codec ran, but not for the local player" (H2) — and the
  // per-codec split says which of the class's two decode paths the engine
  // used.
  result.set("animfix_codec_calls",
             Json::number(static_cast<double>(input_.animfix_codec_calls())));
  result.set("animfix_codec_other_controller",
             Json::number(static_cast<double>(input_.animfix_codec_other_controller())));
  result.set("animfix_codec_not_local",
             Json::number(static_cast<double>(input_.animfix_codec_not_local())));
  result.set("animfix_codec_local_calls",
             Json::number(static_cast<double>(input_.animfix_codec_local_calls())));
  result.set("animfix_codec_diagnosis",
             Json::string(animfix::codec_diagnosis_text(input_.animfix_codec_diagnosis())));
  if (input_.animfix_data_valid()) {
    result.set("animfix_server_aim_yaw", Json::number(input_.animfix_server_aim_yaw()));
    result.set("animfix_predicted_aim_yaw", Json::number(input_.animfix_predicted_aim_yaw()));
    result.set("animfix_aim_yaw_delta", Json::number(input_.animfix_aim_yaw_delta()));
  } else {
    // No data yet: `null`, so "nothing observed" is not "no divergence".
    result.set("animfix_server_aim_yaw", Json());
    result.set("animfix_predicted_aim_yaw", Json());
    result.set("animfix_aim_yaw_delta", Json());
  }
  result.set("predict_ready", Json::boolean(input_.predict_ready()));
  result.set("predict_runs", Json::number(static_cast<double>(input_.predict_runs())));
  result.set("predicted_landing_tick",
             Json::number(static_cast<double>(input_.predicted_landing_tick())));
  result.set("predicted_landing_frac",
             Json::number(static_cast<double>(input_.predicted_landing_frac())));
  result.set("predicted_landing_delta",
             Json::number(static_cast<double>(input_.predicted_landing_delta())));
  result.set("calls", Json::number(static_cast<double>(input_.calls())));
  result.set("writes", Json::number(static_cast<double>(input_.writes())));
  result.set("angle_writes", Json::number(static_cast<double>(input_.angle_writes())));
  result.set("move_writes", Json::number(static_cast<double>(input_.move_writes())));
  result.set("raw_writes", Json::number(static_cast<double>(input_.raw_writes())));
  result.set("entry_writes", Json::number(static_cast<double>(input_.entry_writes())));
  result.set("step_writes", Json::number(static_cast<double>(input_.step_writes())));
  result.set("last_cmd", Json::string(hex(input_.last_cmd())));
  Json words = Json::array();
  for (std::size_t i = 0; i < Input::k_probe_words; ++i) {
    words.push_back(Json::string(hex(input_.last_word(i))));
  }
  result.set("cmd_words", std::move(words));
  result.set("base", Json::string(hex(input_.last_base())));
  Json base_words = Json::array();
  for (std::size_t i = 0; i < Input::k_probe_base_words; ++i) {
    base_words.push_back(Json::string(hex(input_.last_base_word(i))));
  }
  result.set("base_words", std::move(base_words));

  // Decoded movement probes (`docs/re/subtick.md` §"Actionable seams"): what the
  // engine's own input put on the command, the raw axes and the input entry the
  // raw axes are derived from, plus the command's own sub-tick steps.
  const auto move_json = [](const MoveSample& sample) -> Json {
    Json out = Json::object();
    out.set("valid", Json::boolean(sample.valid));
    out.set("forward", Json::number(sample.forward));
    out.set("side", Json::number(sample.side));
    out.set("up", Json::number(sample.up));
    return out;
  };
  result.set("base_has_bits",
             Json::string(hex(static_cast<std::uint64_t>(input_.base_has_bits()))));
  result.set("base_move", move_json(input_.base_move()));
  result.set("raw_move", move_json(input_.raw_move()));
  result.set("entry_move", move_json(input_.entry_move()));
  result.set("steps", Json::number(static_cast<double>(input_.step_count())));
  Json steps = Json::array();
  for (std::size_t i = 0; i < k_probe_steps; ++i) {
    const StepProbe probe = input_.step_probe(i);
    if (!probe.valid) {
      continue;
    }
    Json step = Json::object();
    step.set("when", Json::number(probe.when));
    step.set("forward", Json::number(probe.forward));
    step.set("left", Json::number(probe.left));
    step.set("button", Json::string(hex(probe.button)));
    step.set("pressed", Json::boolean(probe.pressed));
    steps.push_back(std::move(step));
  }
  result.set("step_dump", std::move(steps));
  return result;
}

void Service::do_input(const Json& args, Json& out) noexcept {
  input_.refresh();
  // The animfix's capture needs the local pawn to be current (see
  // `refresh_local_pawn`): it is the way to the local controller the capture
  // compares against.
  if (input_.animfix()) {
    refresh_local_pawn();
  }
  if (const Json* via = args.find("via"); via != nullptr && via->is_string()) {
    const std::string_view name = via->as_string();
    if (name == "usercmd") {
      input_.set_backend(Input::Backend::usercmd);
    } else if (name == "console") {
      input_.set_backend(Input::Backend::console);
    } else {
      set_error(out, "input: unknown via (expected \"console\" or \"usercmd\")");
      return;
    }
  }
  set_ok(out, input_status());
}

void Service::do_press(const Json& args, Json& out, bool release) noexcept {
  input_.refresh();
  if (const Json* reset = args.find("reset");
      reset != nullptr && reset->is_bool() && reset->as_bool()) {
    input_.reset_buttons();
    set_ok(out, input_status());
    return;
  }
  const Json* buttons = args.find("buttons");
  if (buttons == nullptr) {
    set_error(out, "press/release requires \"buttons\" (a name, array of names, or mask)");
    return;
  }
  std::uint64_t mask = 0;
  std::string bad;
  if (!parse_buttons(*buttons, mask, bad)) {
    set_error(out, "unknown button \"" + bad + "\"");
    return;
  }
  if (release) {
    input_.release(mask);
  } else {
    input_.press(mask);
  }
  set_ok(out, input_status());
}

void Service::do_move(const Json& args, Json& out) noexcept {
  input_.refresh();
  if (const Json* clear = args.find("clear");
      clear != nullptr && clear->is_bool() && clear->as_bool()) {
    input_.clear_move();
    set_ok(out, input_status());
    return;
  }
  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  const auto flag = [&](const char* key) -> bool {
    const Json* value = args.find(key);
    return value != nullptr && value->is_bool() && value->as_bool();
  };
  // `quantize` = snap to the `sv_quantize_movement_input` valid set, `step` = also
  // write the residual on the sub-tick analog channel, `raw_entry` = also write
  // the per-controller input entry `+0x270` is derived from (the documented next
  // probe). All default off, so the historic write is unchanged.
  input_.set_move(number("forward", 0.0F), number("side", 0.0F), number("up", 0.0F),
                  flag("quantize"), flag("step"), flag("raw_entry"));
  set_ok(out, input_status());
}

void Service::do_look(const Json& args, Json& out) noexcept {
  input_.refresh();
  if (const Json* clear = args.find("clear");
      clear != nullptr && clear->is_bool() && clear->as_bool()) {
    input_.clear_view_angles();
    input_.clear_command_angles();
    input_.clear_raw_angles();
    input_.clear_step_yaw();
    set_ok(out, input_status());
    return;
  }
  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  const auto flag = [&](const char* key) -> bool {
    const Json* value = args.find(key);
    return value != nullptr && value->is_bool() && value->as_bool();
  };
  const float pitch = number("pitch", 0.0F);
  const float yaw = number("yaw", 0.0F);
  const float roll = number("roll", 0.0F);
  // Default: the silent sub-tick `input_history.view_angles`. `command: true`
  // writes the command's **own** `viewangles` (`base + 0x40`) instead/in addition —
  // that is the channel movement's wishdir uses (measured live 2026-10-15: the
  // history entries do not steer movement), which is what an auto-strafe needs.
  input_.set_view_angles(pitch, yaw, roll);
  if (flag("command")) {
    input_.set_command_angles(pitch, yaw, roll);
  } else {
    input_.clear_command_angles();
  }
  if (flag("raw")) {
    // The raw per-sub-tick input angle history (`CCSGOInput + 0xbd0`).
    input_.set_raw_angles(pitch, yaw, roll);
  } else {
    input_.clear_raw_angles();
  }
  // `step_yaw: <degrees>` appends a `CSubtickMoveStep` carrying only a yaw delta
  // (`when = 1`) each tick — the other silent movement-yaw candidate.
  if (const Json* step = args.find("step_yaw"); step != nullptr && step->is_number()) {
    input_.set_step_yaw(static_cast<float>(step->as_number()));
  } else {
    input_.clear_step_yaw();
  }
  set_ok(out, input_status());
}

void Service::do_bhop(const Json& args, Json& out) noexcept {
  input_.refresh();
  bool on = true;
  if (const Json* arg = args.find("on"); arg != nullptr && arg->is_bool()) {
    on = arg->as_bool();
  }
  input_.set_bhop(on);
  set_ok(out, input_status());
}

void Service::do_predict(const Json& args, Json& out) noexcept {
  input_.refresh();
  bool on = true;
  if (const Json* arg = args.find("on"); arg != nullptr && arg->is_bool()) {
    on = arg->as_bool();
  }
  input_.set_predict(on);
  set_ok(out, input_status());
}

void Service::do_strafe(const Json& args, Json& out) noexcept {
  input_.refresh();
  bool on = true;
  if (const Json* arg = args.find("on"); arg != nullptr && arg->is_bool()) {
    on = arg->as_bool();
  }
  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  // `mode` (0 = off, 1 = the camera's forward/back — the legacy goal, 2 = the
  // player's W/A/S/D intent) and `ignore` (1 = a held `A`/`D` stands the
  // feature down — the legacy rule). They are the strafe's own knobs, so they
  // are applied whether the feature is on or not.
  input_.set_strafe_mode(
      static_cast<int>(number("mode", static_cast<float>(input_.strafe_mode()))));
  bool ignore = input_.strafe_ignore_keys();
  if (const Json* arg = args.find("ignore"); arg != nullptr) {
    ignore = arg->is_bool() ? arg->as_bool() : arg->as_number() != 0.0;
  }
  input_.set_strafe_ignore_keys(ignore);
  if (on) {
    // `wishspeed` (the engine's clamped speed for a full analog, ~250) and
    // `subtick` (0 = the camera-frame analog, N = split the basis rotation over N
    // `yaw_delta` steps) follow `docs/re/strafing.md` §3.4.
    input_.set_strafe_config(
        number("wishspeed", input_.strafe_wishspeed()),
        static_cast<int>(number("subtick", static_cast<float>(input_.strafe_subticks()))));
  }
  input_.set_strafe(on);
  set_ok(out, input_status());
}

// The local animfix (`docs/re/animfix.md`): `animfix {"on": true}` makes the
// **server's** animation win for the local player. It is off by default; the
// status block reports what it did (the captures, the re-applies, the skips
// and the aim-yaw divergence).
void Service::do_animfix(const Json& args, Json& out) noexcept {
  input_.refresh();
  bool on = true;
  if (const Json* arg = args.find("on"); arg != nullptr && arg->is_bool()) {
    on = arg->as_bool();
  }
  input_.set_animfix(on);
  set_ok(out, input_status());
}

void Service::do_antiaim(const Json& args, Json& out) noexcept {
  input_.refresh();
  bool on = true;
  if (const Json* arg = args.find("on"); arg != nullptr && arg->is_bool()) {
    on = arg->as_bool();
  }
  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  // The desync offset (degrees, applied to the camera's yaw), the fake pitch
  // (degrees, clamped to the engine's range), the per-command jitter (degrees)
  // and the side (`0` automatic, `+1` left, `-1` right).
  input_.set_anti_aim_config(
      number("yaw_offset", input_.anti_aim_yaw_offset()), number("pitch", input_.anti_aim_pitch()),
      number("jitter", input_.anti_aim_jitter()),
      static_cast<int>(number("side", static_cast<float>(input_.anti_aim_side()))));
  // The movement re-base: **on by default**. Turning it off leaves the fake
  // angles in place and the movement's wish basis follows them — a
  // diagnostic, not a mode to play in. The `move_crc` caveat is reported in
  // the status, not gated (`docs/re/angle-split.md` §5.4).
  //
  // The flag is symmetric: the anti-aim off means the re-base is off, and
  // turning the anti-aim on restores the default.
  bool rebase = true;
  if (const Json* arg = args.find("move_rebase"); arg != nullptr) {
    rebase = arg->is_bool() ? arg->as_bool() : arg->as_number() != 0.0;
  }
  if (!on) {
    rebase = false;
  }
  input_.set_anti_aim_move_rebase(rebase);
  input_.set_anti_aim(on);
  set_ok(out, input_status());
}

void Service::set_movement_features(bool auto_strafe, bool bhop) noexcept {
  input_.set_strafe(auto_strafe);
  input_.set_bhop(bhop);
}

void Service::set_settings_access(SettingsReader reader, SettingsWriter writer,
                                  void* context) noexcept {
  settings_access_.set(reader, writer, context);
}

void Service::set_settings_access(SettingsReader reader, SettingsWriter writer, SettingsReady ready,
                                  void* context) noexcept {
  settings_access_.set(reader, writer, ready, context);
}

void Service::set_strafe_features(int mode, bool ignore_keys, float wishspeed,
                                  int subticks) noexcept {
  input_.set_strafe_mode(mode);
  input_.set_strafe_ignore_keys(ignore_keys);
  input_.set_strafe_config(wishspeed, subticks);
}

void Service::set_rage_features(bool on, int key, bool fire, int objective, int hitbox, float fov,
                                float min_damage, float min_hitchance, int autostop, int autoscope,
                                bool autoreload, bool only_visible, int mode, bool per_weapon,
                                const char* weapon, int config_op, std::uint32_t config_token,
                                bool recoil, float stop_speed, bool backtrack) noexcept {
  rage_enabled_.store(on);
  rage_key_.store(key);
  rage_fire_.store(fire);
  rage_objective_.store(objective);
  rage_hitbox_.store(hitbox);
  rage_fov_.store(fov);
  rage_min_damage_.store(min_damage);
  rage_min_hitchance_.store(min_hitchance);
  rage_autostop_.store(autostop);
  rage_autoscope_.store(autoscope);
  rage_autoreload_.store(autoreload);
  rage_only_visible_.store(only_visible);
  rage_mode_.store(mode);
  rage_per_weapon_.store(per_weapon);
  rage_recoil_.store(recoil);
  rage_stop_speed_.store(stop_speed);
  rage_backtrack_.store(backtrack);
  {
    const std::scoped_lock lock(rage_config_mutex_);
    if (weapon != nullptr) {
      rage_weapon_key(weapon, rage_weapon_, sizeof(rage_weapon_));
    }
  }
  // One click = one op: the menu increments the token.
  if (config_op != 0 && config_token != rage_config_token_.load()) {
    rage_config_token_.store(config_token);
    apply_rage_config_op(config_op, weapon);
  }
}

void Service::set_trigger_features(const TriggerConfig& config) noexcept {
  // The triggerbot's configuration (`docs/ragebot.md` §"The trigger"). It is set
  // by the `trigger` control command and by the payload's environment
  // (`CS2_PAYLOAD_TRIGGER`) — never by the wire settings block, which is versioned.
  trigger_.configure(config);
}

void Service::apply_rage_config_op(int op, const char* weapon) noexcept {
  char key[24] = {};
  bool persist = false;
  {
    const std::scoped_lock lock(rage_config_mutex_);
    rage_weapon_key(weapon != nullptr ? weapon : rage_weapon_, key, sizeof(key));
    switch (op) {
      case 1: {  // save the live globals into the weapon's profile
        RageProfile* profile = rage_profile_find(rage_profiles_, key, true);
        if (profile == nullptr) {
          return;
        }
        profile->objective = static_cast<std::uint8_t>(rage_objective_.load());
        profile->hitbox = static_cast<std::uint8_t>(rage_hitbox_.load());
        profile->mode = static_cast<std::uint8_t>(rage_mode_.load());
        profile->key = rage_key_.load();
        profile->fov = rage_fov_.load();
        profile->min_damage = rage_min_damage_.load();
        profile->min_hitchance = rage_min_hitchance_.load();
        profile->autostop = static_cast<std::uint8_t>(rage_autostop_.load());
        profile->autoscope = static_cast<std::uint8_t>(rage_autoscope_.load());
        profile->autoreload = rage_autoreload_.load() ? 1 : 0;
        profile->only_visible = rage_only_visible_.load() ? 1 : 0;
        profile->fire = rage_fire_.load() ? 1 : 0;
        persist = true;
        break;
      }
      case 2: {  // load the weapon's profile into the live globals
        const RageProfile* profile = rage_profile_find(rage_profiles_, key, false);
        if (profile == nullptr) {
          return;
        }
        rage_objective_.store(profile->objective);
        rage_hitbox_.store(profile->hitbox);
        rage_mode_.store(profile->mode);
        rage_key_.store(profile->key);
        rage_fov_.store(profile->fov);
        rage_min_damage_.store(profile->min_damage);
        rage_min_hitchance_.store(profile->min_hitchance);
        rage_autostop_.store(profile->autostop);
        rage_autoscope_.store(profile->autoscope);
        rage_autoreload_.store(profile->autoreload != 0);
        rage_only_visible_.store(profile->only_visible != 0);
        rage_fire_.store(profile->fire != 0);
        break;
      }
      case 4:  // load file
        (void)rage_config_load(rage_profiles_);
        return;
      case 5: {  // delete the weapon's profile
        for (int i = 0; i < rage_profiles_.count; ++i) {
          char existing[24];
          rage_weapon_key(rage_profiles_.profiles[i].weapon, existing, sizeof(existing));
          if (std::strcmp(existing, key) == 0) {
            rage_profiles_.profiles[i] = rage_profiles_.profiles[--rage_profiles_.count];
            break;
          }
        }
        persist = true;
        break;
      }
      default:
        return;
    }
  }
  // Persist after an edit (and for an explicit save). Re-take the lock so a concurrent
  // profile edit on the control thread cannot race the read; the write is tiny and rare.
  if (op == 3 || persist) {
    const std::scoped_lock lock(rage_config_mutex_);
    (void)rage_config_save(rage_profiles_);
  }
}

auto Service::effective_rage_tuning(const char* active_weapon) noexcept -> RageTuning {
  RageTuning tuning;
  tuning.objective = rage_objective_.load();
  tuning.hitbox = rage_hitbox_.load();
  tuning.mode = rage_mode_.load();
  tuning.autostop = rage_autostop_.load();
  tuning.autoscope = rage_autoscope_.load();
  tuning.autoreload = rage_autoreload_.load();
  tuning.only_visible = rage_only_visible_.load();
  tuning.fire = rage_fire_.load();
  tuning.recoil = rage_recoil_.load();
  tuning.stop_speed = rage_stop_speed_.load();
  tuning.key = rage_key_.load();
  tuning.fov = rage_fov_.load();
  tuning.min_damage = rage_min_damage_.load();
  tuning.min_hitchance = rage_min_hitchance_.load();
  if (!rage_per_weapon_.load() || active_weapon == nullptr || active_weapon[0] == '\0') {
    return tuning;
  }
  const std::scoped_lock lock(rage_config_mutex_);
  const RageProfile* profile = rage_profile_find(rage_profiles_, active_weapon, false);
  if (profile == nullptr) {
    return tuning;
  }
  tuning.objective = profile->objective;
  tuning.hitbox = profile->hitbox;
  tuning.mode = profile->mode;
  tuning.autostop = profile->autostop;
  tuning.autoscope = profile->autoscope;
  tuning.autoreload = profile->autoreload != 0;
  tuning.only_visible = profile->only_visible != 0;
  tuning.fire = profile->fire != 0;
  tuning.key = profile->key;
  tuning.fov = profile->fov;
  tuning.min_damage = profile->min_damage;
  tuning.min_hitchance = profile->min_hitchance;
  return tuning;
}

// The ragebot only solves real firearms. `SWeaponType` (`m_WeaponType`): 1 pistol,
// 2 smg, 3 rifle, 4 shotgun, 5 sniper, 6 machinegun; 0 knife, 7 c4, 8 taser, 9 grenade,
// 10 equipment, … are excluded (knife-bot is a later feature). An unknown type (-1)
// falls back to a designer-name deny-list.
[[nodiscard]] auto is_rage_weapon(int weapon_type, const std::string& name) noexcept -> bool {
  if (weapon_type >= 1 && weapon_type <= 6) {
    return true;  // pistol / smg / rifle / shotgun / sniper / machinegun
  }
  if (weapon_type > 6) {
    return false;  // a known non-firearm (c4, taser, grenade, equipment, …)
  }
  if (weapon_type == 0) {
    return false;  // knife
  }
  // Unknown type: any `weapon_*` that is not on the deny-list.
  static constexpr const char* k_deny[] = {
      "weapon_knife",      "weapon_c4",           "weapon_taser",        "weapon_flashbang",
      "weapon_hegrenade",  "weapon_smokegrenade", "weapon_molotov",      "weapon_incgrenade",
      "weapon_decoy",      "weapon_tagrenade",    "weapon_breachcharge", "weapon_bumpmine",
      "weapon_healthshot", "weapon_shield"};
  for (const char* deny : k_deny) {
    if (name == deny) {
      return false;
    }
  }
  return name.starts_with("weapon_");
}

// The entity index whose hitbox is angularly closest to `direction` — the target the
// global aimpoint landed on.
auto best_target_for(const aim::SolveInput& input, const game::Vec3& direction) -> int {
  int best = -1;
  float best_dot = -2.0F;
  for (const aim::Hitbox& hitbox : input.hitboxes) {
    const game::Vec3 centre =
        hitbox.is_capsule ? (hitbox.capsule.a + hitbox.capsule.b) * 0.5F : hitbox.box.center;
    const game::Vec3 to_centre = game::normalized(centre - input.eye);
    const float d = game::dot(direction, to_centre);
    if (d > best_dot) {
      best_dot = d;
      best = hitbox.target;
    }
  }
  return best;
}

// The pawn of the player with entity `index` (0 when absent).
auto pawn_for_index(const game::Snapshot& snapshot, int index) -> std::uintptr_t {
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.index == index) {
      return player.pawn;
    }
  }
  return 0;
}

// The hitgroup / hitbox id of the box whose centre is angularly closest to
// `direction` — a diagnostic (which hitbox the solve effectively picked).
auto rage_hitbox_for(const aim::SolveInput& solve_input, const game::Vec3& direction) -> int {
  int best = -1;
  float best_dot = -2.0F;
  for (const aim::Hitbox& hitbox : solve_input.hitboxes) {
    const game::Vec3 center =
        hitbox.is_capsule ? (hitbox.capsule.a + hitbox.capsule.b) * 0.5F : hitbox.box.center;
    const game::Vec3 to_center = game::normalized(center - solve_input.eye);
    const float d = game::dot(direction, to_center);
    if (d > best_dot) {
      best_dot = d;
      best = hitbox.group;
    }
  }
  return best;
}

void Service::sample_records(const game::Snapshot& snapshot) noexcept {
  if (!snapshot.valid || !snapshot.local.has_value()) {
    return;
  }
  // Key the record by the tick the client is simulating (the local controller's
  // `m_nTickBase`), so records line up with the server's lag-comp rewind
  // (`docs/re/lagcomp.md`). Fall back to a sample counter before a level is up.
  const std::int32_t tick_base = snapshot.local->tick_base;
  const std::int32_t tick = tick_base > 0 ? tick_base : (record_tick_.fetch_add(1) + 1);
  const aim::WeaponProfile weapon;  // the heatmap re-values heads with its own weapon

  // Read every enemy's hitboxes **outside** the lock (the memory walk is slow and
  // must not stall a concurrent `heatmap` read), then publish them together.
  struct Sampled {
    std::uintptr_t pawn = 0;
    game::Vec3 origin{};
    float simulation_time = 0.0F;
    std::vector<aim::Hitbox> hitboxes;
  };
  std::vector<Sampled> samples;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || player.pawn == 0) {
      continue;
    }
    if (player.team != 2 && player.team != 3) {
      continue;  // real combatants only
    }
    std::vector<aim::Hitbox> hitboxes = aim::read_solver_hitboxes(player.pawn, weapon);
    if (hitboxes.empty()) {
      continue;
    }
    samples.push_back(Sampled{.pawn = player.pawn,
                              .origin = player.origin,
                              .simulation_time = player.simulation_time,
                              .hitboxes = std::move(hitboxes)});
  }

  std::vector<std::uintptr_t> alive;
  alive.reserve(samples.size());
  const std::scoped_lock lock(records_mutex_);
  for (Sampled& sample : samples) {
    aim::Record record;
    record.tick = tick;
    record.simulation_time = sample.simulation_time;
    record.origin = sample.origin;
    record.hitboxes = std::move(sample.hitboxes);
    records_.push(sample.pawn, std::move(record));
    alive.push_back(sample.pawn);
  }
  records_.retain(alive);
  // Trim to the lag-comp window — exactly what the client does (`FUN_180c90b90`),
  // so the store holds only the records the server could still rewind to.
  if (tick_base > 0) {
    records_.trim(aim::rewind_tick(tick_base, lag_max_unlag_.load(), lag_interp_.load()));
  }
}

void Service::cache_occlusion_targets(const game::Snapshot& snapshot) noexcept {
  if (!snapshot.valid || !snapshot.local.has_value() || !snapshot.local->alive) {
    return;
  }
  const game::PlayerSnapshot& local = *snapshot.local;
  // The enumeration box must contain **all the world geometry between the eye and the
  // enemies' hitboxes**: the eye, plus every enemy's hitbox geometry — the live pose
  // *and* every lag record (a backtrack pose can sit behind the live one). It is built
  // from world positions only, never the camera angles (`docs/ragebot.md` §3). The
  // hitboxes are the ones `sample_records` already read this frame (live + records),
  // so no extra memory walk is needed.
  game::Vec3 lo = local.eye_origin;
  game::Vec3 hi = local.eye_origin;
  bool have_geometry = false;
  // One pyramid base **per enemy**, covering both its current snapshot bounds and every
  // sampled lag pose. The enumeration is the union of the eye→enemy pyramids, so a far
  // enemy cannot widen the query to the whole map (`docs/ragebot-perf.md` §2.1) while
  // every enemy still contributes its own volume.
  //
  // The snapshot bounds matter: a pawn that just (re)spawned or teleported can have a
  // record ring that is still empty or still holds its old position, and a pyramid built
  // from stale poses would prune the walls in the real line of sight. The snapshot bounds
  // are always current, and the record ring adds the backtrack poses on top.
  aim::Aabb targets[k_max_occlusion_targets] = {};
  std::uintptr_t target_pawns[k_max_occlusion_targets] = {};
  int target_count = 0;
  bool inputs_complete = true;
  const auto target_for = [&](std::uintptr_t pawn) -> aim::Aabb* {
    for (int i = 0; i < target_count; ++i) {
      if (target_pawns[i] == pawn) {
        return &targets[i];
      }
    }
    if (target_count >= k_max_occlusion_targets) {
      inputs_complete = false;
      return nullptr;
    }
    target_pawns[target_count] = pawn;
    aim::Aabb* target = &targets[target_count++];
    // Seed the base at the eye: the pyramid is `conv(eye, base)`, and the eye is its
    // apex, so adding the eye to the base changes nothing. It keeps "extend" uniform.
    target->min = local.eye_origin;
    target->max = local.eye_origin;
    return target;
  };
  // The current pose of every enemy (authoritative, never stale).
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || player.pawn == 0) {
      continue;
    }
    if (player.team != 2 && player.team != 3) {
      continue;
    }
    aim::Aabb* target = target_for(player.pawn);
    if (target == nullptr) {
      continue;
    }
    const game::Vec3 tlo = player.origin + player.mins;
    const game::Vec3 thi = player.origin + player.maxs;
    target->min.x = target->min.x < tlo.x ? target->min.x : tlo.x;
    target->min.y = target->min.y < tlo.y ? target->min.y : tlo.y;
    target->min.z = target->min.z < tlo.z ? target->min.z : tlo.z;
    target->max.x = target->max.x > thi.x ? target->max.x : thi.x;
    target->max.y = target->max.y > thi.y ? target->max.y : thi.y;
    target->max.z = target->max.z > thi.z ? target->max.z : thi.z;
    extend_point(lo, hi, tlo);
    extend_point(lo, hi, thi);
    have_geometry = true;
  }
  // Every sampled lag pose, so a backtrack aim has its own walls enumerated too. Only
  // when backtracking (or the live heatmap) actually consumes them.
  if (rage_backtrack_.load() || occlusion_live_.load()) {
    const std::scoped_lock lock(records_mutex_);
    for (const std::uintptr_t pawn : records_.pawns()) {
      aim::Aabb* target = target_for(pawn);
      if (target == nullptr) {
        continue;
      }
      for (const aim::Record& record : records_.records(pawn)) {
        for (const aim::Hitbox& hitbox : record.hitboxes) {
          game::Vec3 tlo{};
          game::Vec3 thi{};
          hitbox_bounds(hitbox, tlo, thi);
          target->min.x = target->min.x < tlo.x ? target->min.x : tlo.x;
          target->min.y = target->min.y < tlo.y ? target->min.y : tlo.y;
          target->min.z = target->min.z < tlo.z ? target->min.z : tlo.z;
          target->max.x = target->max.x > thi.x ? target->max.x : thi.x;
          target->max.y = target->max.y > thi.y ? target->max.y : thi.y;
          target->max.z = target->max.z > thi.z ? target->max.z : thi.z;
          extend_point(lo, hi, tlo);
          extend_point(lo, hi, thi);
          have_geometry = true;
        }
      }
    }
  }
  if (!have_geometry) {
    // No records yet (sampling just started / a fresh spawn): fall back to the
    // enemies' eye positions so a box is still enumerated.
    for (const game::PlayerSnapshot& player : snapshot.players) {
      if (player.is_local || !player.alive || player.pawn == 0) {
        continue;
      }
      if (player.team != 2 && player.team != 3) {
        continue;
      }
      extend_point(lo, hi, player.eye_origin);
      have_geometry = true;
      if (target_count < k_max_occlusion_targets) {
        const game::Vec3 pad{.x = 24.0F, .y = 24.0F, .z = 72.0F};  // a player-sized proxy box
        targets[target_count++] =
            aim::Aabb{.min = player.eye_origin - pad, .max = player.eye_origin + pad};
      } else {
        inputs_complete = false;
      }
    }
  }
  if (!have_geometry) {
    return;
  }
  const std::scoped_lock lock(occlusion_mutex_);
  // Enumerate every frame: the eye→hitboxes volume is the exact query volume, and caching
  // it would make the solid set stale (docs/ragebot.md — correctness over cost). Only the
  // *parse* of the static world body is cached.
  occlusion_eye_ = local.eye_origin;
  occlusion_lo_ = lo;
  occlusion_hi_ = hi;
  occlusion_target_count_ = target_count;
  occlusion_inputs_complete_ = inputs_complete;
  for (int i = 0; i < target_count; ++i) {
    occlusion_targets_[i] = targets[i];
  }
  // Exclude the players' own collision bodies (targets, not occluders). `index` is the
  // entity identity index, exactly the record handle's `& 0x7fff`.
  int n = 0;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (n >= k_max_occlusion_exclude) {
      occlusion_inputs_complete_ = false;
      continue;
    }
    if (player.index < 0) {
      occlusion_inputs_complete_ = false;
      continue;
    }
    occlusion_exclude_[n] = static_cast<std::uint16_t>(player.index & 0x7fff);
    occlusion_pawns_[n] = player.pawn;
    ++n;
  }
  occlusion_exclude_count_ = n;
  occlusion_request_.store(true);
}

void Service::game_thread_entry(void* context) noexcept {
  static_cast<Service*>(context)->on_game_thread();
}

void Service::on_game_thread() noexcept {
  // Auto-scope through the engine's own `+zoom`/`-zoom`: the command button mask does
  // not reach the weapon's zoom, but the console held-buttons do (measured live). The
  // transition is applied here so it runs on a game thread (Cbuf).
  {
    const bool want_scope = rage_scope_wanted_.load();
    if (want_scope != rage_scope_applied_) {
      console_.refresh();
      if (console_.ready() && console_.exec(want_scope ? "+zoom" : "-zoom")) {
        rage_scope_applied_ = want_scope;
      }
    }
  }
  if (occlusion_request_.exchange(false)) {
    game::Vec3 eye{};
    game::Vec3 box_min{};
    game::Vec3 box_max{};
    std::uint16_t exclude[k_max_occlusion_exclude] = {};
    std::uintptr_t pawns[k_max_occlusion_exclude] = {};
    aim::Aabb targets[k_max_occlusion_targets] = {};
    int exclude_count = 0;
    int target_count = 0;
    bool inputs_complete = true;
    {
      const std::scoped_lock lock(occlusion_mutex_);
      eye = occlusion_eye_;
      box_min = occlusion_lo_;
      box_max = occlusion_hi_;
      exclude_count = occlusion_exclude_count_;
      target_count = occlusion_target_count_;
      inputs_complete = occlusion_inputs_complete_;
      for (int i = 0; i < exclude_count; ++i) {
        exclude[i] = occlusion_exclude_[i];
        pawns[i] = occlusion_pawns_[i];
      }
      for (int i = 0; i < target_count; ++i) {
        targets[i] = occlusion_targets_[i];
      }
    }
    // The payload trace filter rejects the players (a target, not a solid) — the engine
    // calls it for every non-worldspawn overlap (`docs/re/occlusion.md`).
    set_occlusion_exclude_pawns(pawns, exclude_count);
    // The enumeration volume: the per-target pyramid union by default, or the older
    // single AABB when the diagnostic asks for it (`occlusion_enum_mode_`).
    // `enum=box` forces the single-AABB volume; `enum=both` uses the pyramid for the
    // result but also runs the AABB on the same tick to compare them.
    const int enum_mode = occlusion_enum_mode_.load();
    const bool use_pyramid = enum_mode != 1;
    const bool verify_enum = enum_mode == 2;
    OcclusionQueryResult query = query_world_occluders(
        eye, box_min, box_max, occlusion_invoke_.load(), exclude, exclude_count,
        use_pyramid ? targets : nullptr, use_pyramid ? target_count : 0, verify_enum);
    if (!inputs_complete) {
      query.stats.complete = false;
      query.stats.incomplete_reasons |=
          static_cast<std::uint32_t>(OcclusionQueryIssue::target_capacity);
    }
    rage_occlusion_query_complete_.store(query.complete(), std::memory_order_relaxed);
    rage_occlusion_query_issues_.store(query.stats.incomplete_reasons, std::memory_order_relaxed);
    const std::scoped_lock lock(occlusion_mutex_);
    occlusion_hulls_ = std::move(query.hulls);
    occlusion_stats_ = query.stats;
  }

  // The ESP's visibility: the per-player states, derived from the solids the
  // enumeration just produced. The render thread only ever reads the cache.
  if (visibility_enabled_.load(std::memory_order_relaxed) &&
      visibility_wanted_.load(std::memory_order_relaxed)) {
    game::Snapshot visibility_snapshot;
    {
      const std::scoped_lock lock(snapshot_mutex_);
      if (have_snapshot_.load(std::memory_order_relaxed)) {
        visibility_snapshot = last_snapshot_;
      }
    }
    if (visibility_snapshot.valid) {
      refresh_visibility(visibility_snapshot);
    }
  }

  // The ragebot solve, on the **game thread** (this hook runs per simulated command), so
  // the aim belongs to the command being built — not to the render thread's frame. The
  // hitboxes are re-read inside the solve, so only the player list / eye / weapon come
  // from the (at most one frame old) snapshot (`docs/ragebot-perf.md` §3.3).
  //
  // The triggerbot runs in the same sink (`docs/ragebot.md` §"The trigger"): it is
  // per-tick work on the game thread, and it never writes a view angle.
  const bool rage_wanted = rage_enabled_.load() || rage_force_.load();
  const bool trigger_wanted = trigger_.enabled();
  if (!rage_wanted && !trigger_wanted) {
    input_.clear_rage_aim();
    trigger_.reset(input_);
    return;
  }
  game::Snapshot snapshot;
  bool have_snapshot = false;
  {
    const std::scoped_lock lock(snapshot_mutex_);
    have_snapshot = have_snapshot_.load(std::memory_order_relaxed);
    if (have_snapshot) {
      snapshot = last_snapshot_;
    }
  }
  if (trigger_wanted) {
    if (have_snapshot) {
      trigger_.tick(snapshot, input_, now_ms());
    } else {
      // No snapshot: the game state is unknown, so the trigger stands down
      // (a trigger that pressed and never released would leave the gun firing).
      trigger_.reset(input_);
    }
  }
  if (!rage_wanted) {
    input_.clear_rage_aim();
    return;
  }
  if (!have_snapshot) {
    return;
  }
  rage_tick(snapshot);
}

void Service::on_snapshot(const game::Snapshot& snapshot) noexcept {
  // Hand the snapshot to the game-thread solve and sample the per-enemy lag records (the
  // heatmap's history) + build the occlusion-query box. The **solve itself runs on the
  // game thread** (`rage_tick` from the CreateMove hook), not here.
  {
    const std::scoped_lock lock(snapshot_mutex_);
    last_snapshot_ = snapshot;
    have_snapshot_.store(true, std::memory_order_relaxed);
  }
  // The sound provider's player table (every player's entity index and
  // origin). The snapshot's players are in entity-index order — the order the
  // provider's slot -> player resolution relies on.
  {
    const std::scoped_lock lock(sound_mutex_);
    sound_player_count_ = 0;
    for (const game::PlayerSnapshot& player : snapshot.players) {
      if (sound_player_count_ >= k_max_sound_players) {
        break;
      }
      sound_players_[static_cast<std::size_t>(sound_player_count_)] =
          SoundPlayer{.index = player.index, .origin = player.origin};
      ++sound_player_count_;
    }
  }
  perf_.snapshot_frames.fetch_add(1, std::memory_order_relaxed);
  const bool rage_requested = rage_enabled_.load() || rage_force_.load();
  // Lag-record sampling is the heatmap's history and the backtrack poses. With backtrack
  // disabled (the default) and no heatmap asking for it, skip the walk entirely: it was
  // ~0.6 ms/tick on the render thread for records nothing consumed.
  const bool want_records = heatmap_sampling_.load() || occlusion_live_.load() ||
                            (rage_requested && rage_backtrack_.load());
  if (want_records) {
    const std::uint64_t t = now_us();
    sample_records(snapshot);
    perf_.sample_us.fetch_add(now_us() - t, std::memory_order_relaxed);
  }
  // The occlusion enumeration must run on a game thread (tier0 TLS) — this only builds
  // the query box and requests it; `on_game_thread` runs the engine query.
  if (occlusion_live_.load() || rage_requested) {
    occlusion_invoke_.store(true);
    const std::uint64_t t = now_us();
    cache_occlusion_targets(snapshot);
    perf_.query_us.fetch_add(now_us() - t, std::memory_order_relaxed);
  }
  // The ESP's visibility: the world-occlusion enumeration behind it is the
  // ragebot's (measured at ~1 ms, and worse at dense spots), so it is
  // requested at a low rate — never per frame — and only while the ESP's
  // visibility toggles are on. The request reuses the ragebot's own query
  // box and its engine call; the states are derived from its solids.
  if (visibility_enabled_.load(std::memory_order_relaxed)) {
    const std::uint64_t now = now_ms();
    const std::uint64_t last = visibility_query_ms_.load(std::memory_order_relaxed);
    if (now >= last && now - last >= k_visibility_interval_ms) {
      visibility_query_ms_.store(now, std::memory_order_relaxed);
      occlusion_invoke_.store(true);
      cache_occlusion_targets(snapshot);
    }
  }
}

void Service::publish_monte_carlo(const MonteCarloState& state) noexcept {
  const std::scoped_lock lock(rage_mc_mutex_);
  rage_mc_state_ = state;
}

auto Service::read_monte_carlo() const noexcept -> MonteCarloState {
  const std::scoped_lock lock(rage_mc_mutex_);
  return rage_mc_state_;
}

void Service::rage_tick(const game::Snapshot& snapshot) noexcept {
  // Ragebot instrumentation (`docs/ragebot-perf.md`): time the whole solve and each stage.
  perf_.frames.fetch_add(1, std::memory_order_relaxed);
  const FrameScope frame_scope(perf_.total_us, perf_.last_total_us, perf_.max_total_us);
  // The Monte-Carlo verdict is **cleared first**: every early return below
  // (no target, not engaged, no hitbox, ...) must not leave the previous
  // tick's verdict on the status, or the operator reads a stale number under
  // another objective's name (`docs/ragebot.md` §13.7).
  publish_monte_carlo(MonteCarloState{});
  // Auto-scope is re-decided every tick; the same sink applies the transition (`+zoom`).
  rage_scope_wanted_.store(false);
  const bool forced = rage_force_.load();
  if (!rage_enabled_.load() && !forced) {
    input_.clear_rage_aim();
    return;
  }
  rage_occlusion_candidates_complete_.store(true, std::memory_order_relaxed);
  // Install the game-thread hook before gating on query completeness: the next hook
  // invocation is what drains the pending occlusion request and can make this gate pass.
  input_.refresh();
  if (!rage_occlusion_query_complete_.load(std::memory_order_relaxed)) {
    input_.clear_rage_aim_keep_aux();
    return;
  }
  if (!snapshot.valid || !snapshot.local.has_value() || !snapshot.local->alive) {
    input_.clear_rage_aim();
    return;
  }
  const game::PlayerSnapshot& local = *snapshot.local;

  // Only real firearms are ragebot targets (`m_WeaponType`): the knife, C4, taser,
  // grenades and equipment are excluded (knife-bot is a later feature).
  if (!is_rage_weapon(local.weapon_type, local.weapon)) {
    input_.clear_rage_aim();
    return;
  }

  // The effective tuning: the per-weapon profile when enabled and the active weapon has
  // one, else the global settings (`effective_rage_tuning`).
  const RageTuning tuning = effective_rage_tuning(local.weapon.c_str());
  // The objective the solve **runs** is published, so the status names it and
  // not the global setting: with a per-weapon profile the two differ, and the
  // numbers mean different things under each (`docs/ragebot.md` §13.7).
  rage_effective_objective_.store(tuning.objective, std::memory_order_relaxed);

  // Target selection (`docs/ragebot.md` §3): the enemies within the FOV cone (degrees;
  // 0 = unlimited), computed **before** the trigger so the assists can be predictive.
  // **Every** enemy is a candidate — the solve scans them all at once and picks the best
  // aimpoint, not the closest player.
  constexpr float k_deg_to_rad = 0.01745329251994329577F;
  constexpr std::size_t k_max_candidates = 6;
  std::vector<int> candidates;
  {
    const float fov = tuning.fov;
    const float cos_limit = fov > 0.0F ? std::cos(fov * 0.5F * k_deg_to_rad) : -1.0F;
    const game::Vec3& eye = local.eye_origin;
    const float yaw = local.eye_angles.yaw * k_deg_to_rad;
    const float pitch = local.eye_angles.pitch * k_deg_to_rad;
    const game::Vec3 forward{.x = std::cos(pitch) * std::cos(yaw),
                             .y = std::cos(pitch) * std::sin(yaw),
                             .z = -std::sin(pitch)};
    // Nearest first, capped, so the field stays bounded when 10 players are in view.
    std::vector<std::pair<float, int>> in_view;
    for (const game::PlayerSnapshot& player : snapshot.players) {
      if (player.is_local || !player.alive || player.pawn == 0) {
        continue;
      }
      if (player.team != 2 && player.team != 3) {
        continue;
      }
      const game::Vec3& to = player.eye_origin;
      const float dx = to.x - eye.x;
      const float dy = to.y - eye.y;
      const float dz = to.z - eye.z;
      const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (len <= 1.0F) {
        continue;
      }
      const float dot = (dx * forward.x + dy * forward.y + dz * forward.z) / len;
      if (dot < cos_limit) {
        continue;  // outside the FOV cone
      }
      in_view.emplace_back(player.distance, player.index);
    }
    std::sort(in_view.begin(), in_view.end(),
              [](const auto& a, const auto& b) -> auto { return a.first < b.first; });
    for (const auto& entry : in_view) {
      if (candidates.size() >= k_max_candidates) {
        break;
      }
      candidates.push_back(entry.second);
    }
  }
  // "Can we deal damage to the player": any enemy in FOV. The partner assists engage on
  // this, *before* firing.
  const bool damageable = !candidates.empty();

  // The predictive assists (`docs/ragebot.md`): stop/scope as soon as a damageable
  // target exists (mode 2) or always (mode 1). They run even when the trigger is not
  // engaged, so the player is already still/scoped before the shot.
  {
    const int autostop = tuning.autostop;
    const int autoscope = tuning.autoscope;
    const bool stop_want = autostop == 1 || (autostop == 2 && damageable);
    const bool scope_want = autoscope == 1 || (autoscope == 2 && damageable);
    input_.set_rage_aux(stop_want, scope_want, tuning.autoreload && local.clip <= 0);
    input_.set_rage_recoil(tuning.recoil);
    // The auto-stop speed threshold: an explicit units/s, else 34% of the weapon's max
    // speed — the fastest movement that does not add spread (a tunable default; the
    // menu exposes the value).
    const float stop_speed = tuning.stop_speed > 0.0F ? tuning.stop_speed : 0.34F * local.max_speed;
    input_.set_rage_stop_speed(stop_speed);
    rage_scope_wanted_.store(scope_want);
  }

  // The trigger (`docs/ragebot.md`): hold the key, always on, or toggle on the key.
  bool engaged = forced;
  if (!forced) {
#if defined(_WIN32)
    const bool key_down = tuning.key > 0 && (GetAsyncKeyState(tuning.key) & 0x8000) != 0;
#else
    const bool key_down = false;
#endif
    switch (tuning.mode) {
      case 1:  // always on
        engaged = true;
        break;
      case 2:  // toggle on the key's press edge
        if (key_down && !rage_key_down_) {
          rage_toggle_ = !rage_toggle_;
        }
        rage_key_down_ = key_down;
        engaged = rage_toggle_;
        break;
      default:  // hold (key 0 = always, for back-compat)
        engaged = tuning.key <= 0 || key_down;
        break;
    }
  }
  if (!engaged) {
    // Drop the aim but keep the predictive assists applied.
    input_.clear_rage_aim_keep_aux();
    return;
  }
  if (candidates.empty()) {
    input_.clear_rage_aim_keep_aux();
    return;
  }

  aim::WeaponProfile weapon;
  weapon.damage = rage_damage_.load();
  weapon.range_modifier = rage_range_modifier_.load();
  weapon.headshot_multiplier = rage_headshot_.load();
  // The penetration power is not a menu setting: take it from the shooter's real
  // weapon (`docs/re/penetration.md`) so the wallbang math uses the right budget.
  {
    aim::WeaponProfile from_game;
    if (aim::read_weapon_profile(local.weapon_vdata, from_game)) {
      if (from_game.penetration > 0.0F) {
        weapon.penetration = from_game.penetration;
      }
      weapon.weapon_type = from_game.weapon_type;
    }
    // The weapon **type** decides whether the Monte-Carlo kernel may model the
    // weapon's pellet pattern at all (`pattern_supported`): an unknown type
    // (-1) counts as "modelled", so a shotgun whose VData could not be read
    // would be **silently scored with the standard pattern** — the one thing
    // the core's refusal design exists to prevent. The snapshot carries the
    // same field, read independently (`state_reader.cpp`), so it is the
    // fallback when the profile read fails.
    if (weapon.weapon_type < 0) {
      weapon.weapon_type = local.weapon_type;
    }
  }

  // --- multi-target solve: every candidate in one field ----------------------------
  std::optional<aim::SolveInput> multi;
  {
    const std::uint64_t t = now_us();
    for (const int index : candidates) {
      auto in = aim::build_input(snapshot, weapon, index);
      if (!in.has_value()) {
        continue;
      }
      for (aim::Hitbox& hitbox : in->hitboxes) {
        hitbox.target = index;
      }
      if (!multi.has_value()) {
        multi = std::move(in);
      } else {
        multi->hitboxes.insert(multi->hitboxes.end(), in->hitboxes.begin(), in->hitboxes.end());
      }
    }
    perf_.build_us.fetch_add(now_us() - t, std::memory_order_relaxed);
    perf_.candidates.store(static_cast<std::uint32_t>(candidates.size()),
                           std::memory_order_relaxed);
  }
  if (!multi.has_value() || multi->hitboxes.empty()) {
    input_.clear_rage_aim_keep_aux();
    return;
  }
  std::optional<aim::SolveInput>& solve_input = multi;

  // The hitbox filter (`rage_hitbox`): 1 = head only, 2 = body only.
  if (const int mode = tuning.hitbox; mode != 0) {
    std::erase_if(solve_input->hitboxes, [mode](const aim::Hitbox& hitbox) -> bool {
      const bool head = hitbox.group == 1;
      return mode == 1 ? !head : head;
    });
    if (solve_input->hitboxes.empty()) {
      input_.clear_rage_aim_keep_aux();
      return;
    }
  }
  // Occlusion (`docs/ragebot.md` §3): project the world solids the game thread
  // enumerated into the solve's chart, once — shared by the multi-target pick and the
  // backtracking poses. Only the solids whose AABB can lie between the eye and the
  // candidates are projected (the per-hull AABB is cached, `OcclusionHull::has_bounds`).
  {
    game::Vec3 lo = solve_input->eye;
    game::Vec3 hi = solve_input->eye;
    for (const aim::Hitbox& hitbox : solve_input->hitboxes) {
      extend_box(lo, hi, hitbox);
    }
    constexpr float k_margin = 8.0F;
    lo = game::Vec3{.x = lo.x - k_margin, .y = lo.y - k_margin, .z = lo.z - k_margin};
    hi = game::Vec3{.x = hi.x + k_margin, .y = hi.y + k_margin, .z = hi.z + k_margin};
    const std::uint64_t t_project = now_us();
    const std::scoped_lock lock(occlusion_mutex_);
    for (const OcclusionHull& hull : occlusion_hulls_) {
      if (hull.has_bounds &&
          (hull.bounds_max.x < lo.x || hull.bounds_min.x > hi.x || hull.bounds_max.y < lo.y ||
           hull.bounds_min.y > hi.y || hull.bounds_max.z < lo.z || hull.bounds_min.z > hi.z)) {
        continue;  // cannot lie between the eye and the candidates
      }
      std::optional<aim::OccluderCell> cell;
      if (hull.kind == aim::SolidKind::sphere) {
        cell = aim::project_sphere_occluder(solve_input->eye, solve_input->basis, hull.a,
                                            hull.radius, hull.surface_modifier);
      } else if (hull.kind == aim::SolidKind::capsule) {
        cell = aim::project_capsule_occluder(solve_input->eye, solve_input->basis, hull.a, hull.b,
                                             hull.radius, hull.surface_modifier);
      } else if (hull.kind == aim::SolidKind::mesh) {
        cell = aim::project_mesh_occluder(solve_input->eye, solve_input->basis, hull.vertices,
                                          hull.triangles, hull.surface_modifier, hull.mesh_bvh);
      } else {
        cell = aim::project_occluder(solve_input->eye, solve_input->basis, hull.vertices,
                                     hull.planes, hull.surface_modifier);
      }
      if (cell) {
        cell->damage_modifier = hull.damage_modifier;
        cell->material = hull.material;
        cell->grate = hull.grate;
        cell->target = hull.target;
        solve_input->occlusion.push_back(std::move(*cell));
      }
    }
    perf_.project_us.fetch_add(now_us() - t_project, std::memory_order_relaxed);
  }
  // Temporary occlusion work bound (`PenetrationParams::max_occluders`). If this
  // truncates a hitbox's candidates, `solve` fails closed; it never commits the partial
  // damage score. Remove this cap only after the complete ray-query path is validated.
  static constexpr int k_max_solve_occluders = 24;
  solve_input->penetration = aim::PenetrationModel::engine;
  solve_input->penetration_params.weapon_penetration = solve_input->weapon.penetration;
  // Bound arrangement work temporarily. Overflow is surfaced as an incomplete solve, not
  // accepted as an approximate aim. See `PenetrationParams::max_occluders`.
  solve_input->penetration_params.max_occluders = k_max_solve_occluders;
  rage_occluders_.store(static_cast<int>(solve_input->occlusion.size()));
  solve_input->pose_sigma = rage_sigma_.load();
  // The hitchance cone: the weapon's inaccuracy + spread (`docs/re/aim.md`).
  {
    const float inacc = aim::inaccuracy(solve_input->weapon, 0.0F, true, false);
    solve_input->spread_sigma =
        std::sqrt(inacc * inacc + solve_input->weapon.spread * solve_input->weapon.spread);
  }
  switch (tuning.objective) {
    case OBJECTIVE_VALUE_DAMAGE:
      solve_input->objective = aim::AimObjective::damage;
      break;
    case OBJECTIVE_VALUE_EXPECTED:
      solve_input->objective = aim::AimObjective::expected;
      break;
    case OBJECTIVE_VALUE_MONTE_CARLO:
      solve_input->objective = aim::AimObjective::monte_carlo;
      break;
    default:
      solve_input->objective = aim::AimObjective::hitchance;
      break;
  }
  if (solve_input->objective == aim::AimObjective::monte_carlo) {
    // The Monte-Carlo objective's parameters (`docs/ragebot.md` §13.7).
    //
    // The **seed context**: the tick is the prediction tick (the local
    // controller's `m_nTickBase`), and the **sequence** is the documented
    // fallback — the prediction tick stands in for the command's sequence
    // number, which is not reachable from here (`prediction_tick`). The status
    // says the seed is a fallback; a number presented as the game's own would
    // not be honest.
    const std::uint32_t tick = prediction_tick(snapshot);
    solve_input->monte_carlo_seed =
        aim::SeedContext{.sequence = tick,
                         .tick = tick,
                         .source = static_cast<aim::SeedSource>(rage_mc_seed_.load())};
    // The sample count: **64** by default (`k_min_mc_samples` / `k_max_mc_samples`
    // bound what the operator may choose).
    solve_input->monte_carlo_samples = rage_mc_samples_.load();
    // The weapon's **pellet pattern**: the core has no default for it, and a
    // default would silently drop the revolver's `1 - r^2`. The designer name is
    // the only thing that tells a revolver from a rifle (both are weapon type
    // 3), so the rule is keyed on it (`cs2/control/rage_objective.hpp`).
    solve_input->monte_carlo_pattern = spread_pattern_for(local.weapon);
    // The weapon fires **one** pellet: the multi-pellet weapons are exactly the
    // ones the core refuses (`pattern_supported`), and the core's per-pellet
    // attribution is only reachable with more than one.
    solve_input->monte_carlo_pellets = 1;
    // The targets' **health**: the live enemies' own remaining health, for
    // **every** candidate the solve considered — not only the one that is
    // finally chosen. The Monte-Carlo rule's first key is "can this candidate
    // kill the target", and it is judged against that target's own damage and
    // that target's own health (`docs/ragebot.md` §13.5).
    solve_input->monte_carlo_targets.clear();
    for (const int index : candidates) {
      for (const game::PlayerSnapshot& player : snapshot.players) {
        if (player.index == index) {
          solve_input->monte_carlo_targets.push_back(
              aim::TargetHealth{.index = index, .health = static_cast<float>(player.health)});
          break;
        }
      }
    }
  }

  // The global pick across **all** candidates: which target's hitbox the best aimpoint
  // lands on. `best_target_for` maps the aim direction back to the hitbox (and so the
  // enemy) it points at.
  aim::SolveStats solve_stats;
  const std::uint64_t t_solve = now_us();
  const aim::SolveResult multi_result = aim::solve(*solve_input, &solve_stats);
  perf_.solve_us.fetch_add(now_us() - t_solve, std::memory_order_relaxed);
  // Aggregate the solve breakdown here (not after the backtrack) so a gated/early-return
  // frame still attributes its cost.
  perf_.solve_calls.fetch_add(solve_stats.solves, std::memory_order_relaxed);
  perf_.solve_project_us.fetch_add(solve_stats.project_us, std::memory_order_relaxed);
  perf_.solve_occlude_us.fetch_add(solve_stats.occlude_us, std::memory_order_relaxed);
  perf_.solve_argmax_us.fetch_add(solve_stats.argmax_us, std::memory_order_relaxed);
  perf_.hitboxes.store(solve_stats.hitboxes, std::memory_order_relaxed);
  perf_.cells.store(solve_stats.cells, std::memory_order_relaxed);
  rage_occlusion_candidates_complete_.store(multi_result.occlusion_candidates_complete,
                                            std::memory_order_relaxed);
  if (!multi_result.valid) {
    input_.clear_rage_aim_keep_aux();
    return;
  }
  const int best_target = best_target_for(*solve_input, multi_result.direction);

  // Narrow the field to the chosen enemy for the backtracking pass (records are
  // per-enemy). The occlusion set is shared.
  const std::uintptr_t chosen_pawn = pawn_for_index(snapshot, best_target);
  if (chosen_pawn == 0) {
    input_.clear_rage_aim_keep_aux();
    return;
  }
  std::erase_if(solve_input->hitboxes, [best_target](const aim::Hitbox& hitbox) -> bool {
    return hitbox.target != best_target;
  });
  if (solve_input->hitboxes.empty()) {
    input_.clear_rage_aim_keep_aux();
    return;
  }
  solve_input->pawn = chosen_pawn;

  // Backtracking (`docs/ragebot.md` §8): score the live pose and every sampled lag
  // record of the chosen target and take the best. The record set is the discrete
  // replacement for the continuous pose distribution — the server rewinds the target
  // to a past pose inside the lag-comp window, so a record can beat the live pose.
  // Records are tried newest-first and the live pose first, so the live aim wins ties.
  std::vector<aim::PoseCandidate> poses;
  poses.push_back(aim::PoseCandidate{.record = -1, .age = 0.0F, .hitboxes = solve_input->hitboxes});
  const std::int32_t backtrack_tick_base = snapshot.local->tick_base;
  // Frame-budget guard: every pose is a full solve (projection + occlusion split + the
  // objective search), so a slow position was multiplied by the pose count. When the last
  // tick already blew the budget, this tick scores the live pose only — the aim is still
  // correct, just without the lag-comp candidate that tick (`docs/ragebot-perf.md` §3.9).
  constexpr std::uint64_t k_backtrack_budget_us = 12000;
  const bool allow_backtrack =
      rage_backtrack_.load() &&
      perf_.last_total_us.load(std::memory_order_relaxed) <= k_backtrack_budget_us;
  // The operator sees the backtrack counter stop incrementing and needs to
  // know **why**: the frame had already blown the guard.
  rage_backtrack_over_budget_.store(
      rage_backtrack_.load() &&
          perf_.last_total_us.load(std::memory_order_relaxed) > k_backtrack_budget_us,
      std::memory_order_relaxed);
  if (allow_backtrack) {
    const std::scoped_lock lock(records_mutex_);
    const std::int32_t newest =
        backtrack_tick_base > 0 ? backtrack_tick_base : records_.newest_tick();
    const std::int32_t floor_tick =
        backtrack_tick_base > 0
            ? aim::rewind_tick(backtrack_tick_base, lag_max_unlag_.load(), lag_interp_.load())
            : 0;
    const std::vector<aim::Record>& ring = records_.records(solve_input->pawn);
    // The pose the server validates against is the one at the **rewind tick**
    // (`floor_tick`, `aim::rewind_tick`), so keep the live pose and the records
    // *nearest* that tick. Not every in-window record (wasteful — each is a full
    // solve), and not "the newest few" (wrong: the rewind tick is the **oldest**
    // in-window tick, so the newest records are the wrong pose). A small radius
    // absorbs the latency/interp estimate. `docs/ragebot-perf.md` §3.1.
    constexpr std::int32_t k_pose_radius = 3;
    constexpr std::size_t k_fallback_poses = 8;  // when the tick base is unknown
    std::size_t record_poses = 0;
    for (const auto& it : std::views::reverse(ring)) {
      if (backtrack_tick_base > 0) {
        if (it.tick < floor_tick - 1 || it.tick > floor_tick + k_pose_radius) {
          continue;  // not near the pose the server will rewind to
        }
      } else if (record_poses >= k_fallback_poses) {
        break;
      }
      std::vector<aim::Hitbox> record_hitboxes = it.hitboxes;
      for (aim::Hitbox& hitbox : record_hitboxes) {
        hitbox.target = best_target;
      }
      poses.push_back(aim::PoseCandidate{.record = it.tick,
                                         .age = static_cast<float>(newest - it.tick),
                                         .hitboxes = std::move(record_hitboxes)});
      ++record_poses;
    }
  }
  aim::SolveResult result;
  int winning_record = -1;
  float winning_age = 0.0F;
  if (allow_backtrack) {
    aim::SolveStats backtrack_stats;
    const std::uint64_t t_backtrack = now_us();
    const aim::BacktrackResult backtrack =
        aim::solve_backtrack(*solve_input, poses, &backtrack_stats);
    perf_.backtrack_us.fetch_add(now_us() - t_backtrack, std::memory_order_relaxed);
    perf_.solve_calls.fetch_add(backtrack_stats.solves, std::memory_order_relaxed);
    perf_.solve_project_us.fetch_add(backtrack_stats.project_us, std::memory_order_relaxed);
    perf_.solve_occlude_us.fetch_add(backtrack_stats.occlude_us, std::memory_order_relaxed);
    perf_.solve_argmax_us.fetch_add(backtrack_stats.argmax_us, std::memory_order_relaxed);
    rage_occlusion_candidates_complete_.store(backtrack.occlusion_candidates_complete,
                                              std::memory_order_relaxed);
    if (!backtrack.valid) {
      input_.clear_rage_aim_keep_aux();
      return;
    }
    result = backtrack.solve;
    winning_record = backtrack.record;
    winning_age = backtrack.age;
  } else {
    // Backtracking off: the multi-target solve **is** the live-pose answer. Re-solving the
    // narrowed field (what `solve_backtrack` does with a single pose) duplicated the whole
    // projection + occlusion + objective pass for nothing — ~4 ms/tick
    // (`docs/ragebot-perf.md` §3.9).
    result = multi_result;
  }
  perf_.occluders.store(static_cast<std::uint32_t>(solve_input->occlusion.size()),
                        std::memory_order_relaxed);
  perf_.poses.store(static_cast<std::uint32_t>(poses.size()), std::memory_order_relaxed);

  // The Monte-Carlo objective's verdict, published for the status. The solve
  // runs on the game thread and the status is read on the control **and** the
  // render thread, so the verdict crosses the boundary as **one value**.
  //
  // **A refusal is published, never swallowed**: the reason is what tells the
  // operator that the aim came from the `damage` objective instead of the
  // Monte-Carlo one.
  if (solve_input->objective == aim::AimObjective::monte_carlo) {
    MonteCarloState state;
    if (result.monte_carlo.has_value()) {
      state.valid = true;
      state.probability = result.monte_carlo->probability;
      state.expected_damage = result.monte_carlo->expected_damage;
      state.target = result.monte_carlo->target;
      state.hitgroup = result.monte_carlo->hitgroup;
      state.hits = result.monte_carlo->hits;
      state.can_kill = result.monte_carlo->can_kill;
      state.filtered_hitboxes = result.monte_carlo->filtered_hitboxes;
    } else {
      state.refusal = result.monte_carlo_refusal.has_value()
                          ? static_cast<int>(*result.monte_carlo_refusal)
                          : -1;
    }
    publish_monte_carlo(state);
  }

  // The aim/fire gate (`docs/ragebot.md` §3): only aim and fire when the solve meets
  // the user's minimum damage and hitchance, and (optionally) when the shot is not
  // attenuated by a wall ("only visible").
  if (result.value < tuning.min_damage || result.hitchance < tuning.min_hitchance) {
    input_.clear_rage_aim();
    return;
  }
  const bool visible_ok = !tuning.only_visible || !result.penetrated;
  rage_visible_.store(!result.penetrated);

  constexpr float k_rad_to_deg = 57.295779513082320876F;
  const float yaw = std::atan2(result.direction.y, result.direction.x) * k_rad_to_deg;
  const float pitch = -std::asin(std::clamp(result.direction.z, -1.0F, 1.0F)) * k_rad_to_deg;

  int target_index = -1;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.pawn == solve_input->pawn) {
      target_index = player.index;
      break;
    }
  }
  const int hitbox = rage_hitbox_for(*solve_input, result.direction);

  // The assists were already decided predictively above; here we only commit the aim
  // and (optionally) the fire.
  const bool firing = (tuning.fire || forced) && visible_ok;
  input_.set_rage_aim(pitch, yaw, target_index, hitbox);
  input_.set_rage_fire(firing);
  rage_last_target_.store(target_index);
  rage_last_record_.store(winning_record);
  if (winning_record != -1) {
    rage_backtracks_.fetch_add(1);
  }
  rage_solves_.fetch_add(1);

  // Periodic instrumentation line (`docs/ragebot-perf.md`): the average and worst
  // whole-frame cost and the stage split, so a live session leaves a performance
  // trace in the payload log (the `rage` command reports the same numbers).
  const std::uint64_t perf_frames = perf_.frames.load(std::memory_order_relaxed);
  if (perf_frames != 0 && perf_frames % 256 == 0) {
    const auto avg = [perf_frames](const std::atomic<std::uint64_t>& acc) -> unsigned long long {
      return static_cast<unsigned long long>(acc.load(std::memory_order_relaxed) / perf_frames);
    };
    const std::uint64_t perf_snapshots = perf_.snapshot_frames.load(std::memory_order_relaxed);
    const auto avg_snap =
        [perf_snapshots](const std::atomic<std::uint64_t>& acc) -> unsigned long long {
      return (perf_snapshots != 0 ? acc.load(std::memory_order_relaxed) / perf_snapshots : 0);
    };
    char line[400];
    std::snprintf(
        line, sizeof(line),
        "[rage-perf] frames=%llu total avg=%llu max=%lluus | solve=%llu (proj=%llu occ=%llu "
        "argmax=%llu) back=%llu build=%llu proj=%llu query=%llu sample=%llu us | calls=%llu "
        "cand=%u "
        "hb=%u occ=%u poses=%u cells=%u",
        static_cast<unsigned long long>(perf_frames), avg(perf_.total_us),
        static_cast<unsigned long long>(perf_.max_total_us.load(std::memory_order_relaxed)),
        avg(perf_.solve_us), avg(perf_.solve_project_us), avg(perf_.solve_occlude_us),
        avg(perf_.solve_argmax_us), avg(perf_.backtrack_us), avg(perf_.build_us),
        avg(perf_.project_us), avg_snap(perf_.query_us), avg_snap(perf_.sample_us),
        static_cast<unsigned long long>(perf_.solve_calls.load(std::memory_order_relaxed)),
        perf_.candidates.load(std::memory_order_relaxed),
        perf_.hitboxes.load(std::memory_order_relaxed),
        perf_.occluders.load(std::memory_order_relaxed),
        perf_.poses.load(std::memory_order_relaxed), perf_.cells.load(std::memory_order_relaxed));
    cs2::game::append_log(line);
  }
}

auto Service::read_hitboxes(std::uintptr_t pawn, game::HitboxCapsule* out, int max) const noexcept
    -> int {
  if (out == nullptr || max <= 0 || pawn == 0) {
    return 0;
  }
  int count = 0;
  // The bone names are not used here: the reader skips them, which is one
  // string read per hitbox saved.
  for (const aim::ModelHitbox& box : aim::read_model_hitboxes(pawn, /*read_names=*/false)) {
    if (count >= max) {
      break;
    }
    out[count].a = box.world_a;
    out[count].b = box.world_b;
    out[count].radius = box.radius;
    out[count].group = box.group;
    ++count;
  }
  return count;
}

auto Service::rage_status(char* out, std::size_t size) const noexcept -> bool {
  if ((!rage_enabled_.load() && !rage_force_.load()) || out == nullptr || size == 0) {
    return false;
  }
  const int target = input_.rage_target();
  // The objective **by name**: the operator has to be able to tell the
  // Monte-Carlo objective from the others at a glance, and the numbers mean
  // different things under each (`docs/ragebot.md` section 13.7).
  //
  // **The name is copied into a stack buffer**: this line is written on the
  // render thread (the overlay's per-frame sink), and a heap allocation there
  // is the defect the chams' status line was written to avoid.
  char objective[16] = {};
  {
    const std::string_view name = objective_name_for(rage_objective_.load());
    // **Not `std::min`**: `<windows.h>` defines `min` and `max` as macros, so
    // `std::min` does not survive it.
    const std::size_t limit = sizeof(objective) - 1;
    const std::size_t length = name.size() < limit ? name.size() : limit;
    std::memcpy(objective, name.data(), length);
    objective[length] = '\0';
  }
  // **The Monte-Carlo's marker** (`docs/ragebot.md` §13.7): the measured
  // probability, or the **refusal**.
  //
  // §2.4: a fallback to another objective must **never** be silent, and the
  // HUD is where the operator actually looks — the control channel's JSON is
  // where the operator *reads*. So the refusal reaches the line too.
  //
  // **Stack buffer, no heap**: this line is built on the render thread.
  char mc[16] = {};
  {
    const MonteCarloState state = read_monte_carlo();
    if (state.valid) {
      const int percent = static_cast<int>(state.probability * 100.0F + 0.5F);
      std::snprintf(mc, sizeof(mc), " mc%d%%", percent);
    } else if (state.refusal >= 0) {
      std::snprintf(mc, sizeof(mc), " mc-refused");
    }
  }
  // The line's tail carries the two occlusion markers, and they are the first
  // things the 128-byte buffer truncates — so the record and the occluder
  // count (both in the `rage` command's JSON status) are left out to make
  // room for the Monte-Carlo's marker.
  std::snprintf(
      out, size, "rage: %s target %d box %d obj %s%s w%llu s%llu bt%llu fire %d%s%s%s",
      input_.rage_active() ? "AIM" : "hold", target, input_.rage_hitbox(), objective, mc,
      static_cast<unsigned long long>(input_.rage_writes()),
      static_cast<unsigned long long>(input_.rage_shots()),
      static_cast<unsigned long long>(rage_backtracks_.load()), input_.rage_fire() ? 1 : 0,
      input_.rage_stop() ? " stop" : "",
      rage_occlusion_query_complete_.load(std::memory_order_relaxed) ? "" : " occlusion-incomplete",
      rage_occlusion_candidates_complete_.load(std::memory_order_relaxed) ? "" : " occlusion-cap");
  return true;
}

// --- The ESP's out-of-module data providers --------------------------------

auto Service::movement_status(char* out, std::size_t size) const noexcept -> bool {
  if (out == nullptr || size == 0) {
    return false;
  }
  const bool strafe = input_.strafe();
  const bool bhop = input_.bhop();
  if (!strafe && !bhop) {
    return false;
  }
  // The two numbers that answer "does the movement accelerate?": the **gain**
  // of the last airtime (u/s; the working encoding gains ~+85) and whether the
  // engine applied the basis rotation. The second is the **gate**: a server
  // that ignores the sub-tick view angles makes the silent encoding inert, and
  // the operator has to see that instead of guessing
  // (`docs/re/strafing.md` §"The gate").
  const int subticks = input_.strafe_subticks();
  // The steering verdict, with the three states it really has: the engine
  // applied the rotation ("yes"), it ignored it ("no (ignored)"), or nothing
  // has been judged yet ("?" — the player has not been airborne). The verdict
  // is the **ratio** of the counters, not the last judged tick: one noisy tick
  // (a wall, a landing) flips the last verdict, and the counters do not. The
  // counters cover the **current airtime** — a later phase (a fall down a
  // wall) must not poison the verdict of the airtime that worked.
  const std::uint64_t followed = input_.strafe_steered_ticks();
  const std::uint64_t ignored = input_.strafe_ignored();
  const std::uint64_t judged = followed + ignored;
  const char* steering = judged == 0 ? "?" : (ignored * 2 > judged ? "no" : "yes");
  const char* ignored_note = (judged != 0 && ignored * 2 > judged) ? " (ignored)" : "";
  std::snprintf(out, size,
                "strafe %s (%d slice%s, gain %+.0f u/s, steering %s%s, turn "
                "%+.1f deg) bhop %s (presses %llu)",
                strafe ? "on" : "off", subticks, subticks == 1 ? "" : "s", input_.strafe_gain(),
                steering, ignored_note, input_.strafe_turn(), bhop ? "on" : "off",
                static_cast<unsigned long long>(input_.bhop_presses()));
  return true;
}

void Service::ensure_sound_subscription() noexcept {
  if (sound_subscribed_.load(std::memory_order_relaxed)) {
    return;
  }
  // The two events the sound markers need. They are subscribed **lazily**:
  // the footsteps are the game's highest-rate event, so subscribing them by
  // default would flood the 64-entry ring for every user (and shrink the
  // harness's event history) even when the sound ESP is never switched on.
  // The provider is only called while the sound ESP is on, so the first call
  // is the switch-on.
  const std::int32_t footstep = events_.subscribe("player_footstep");
  const std::int32_t sound = events_.subscribe("player_sound");
  const bool footstep_ok = footstep >= 0 || footstep == EventLog::k_subscribe_already;
  const bool sound_ok = sound >= 0 || sound == EventLog::k_subscribe_already;
  // The hooks may not be installable yet (client.dll still loading): retry on
  // the next call instead of giving up on the feature.
  sound_subscribed_.store(footstep_ok && sound_ok, std::memory_order_relaxed);
}

auto Service::recent_sounds(SoundEvent* out, int max) noexcept -> int {
  if (out == nullptr || max <= 0) {
    return 0;
  }
  ensure_sound_subscription();

  // 1. The captured sound events. The ring's lock is held for the walk and no
  //    other lock is taken inside it, so the ring is never entered twice and
  //    the lock order with the sound mutex below is fixed.
  struct Captured {
    int slot = -1;
    float radius = 0.0F;
    std::uint64_t sequence = 0;
    std::uint64_t time_ms = 0;
  };
  Captured captured[k_max_sound_markers];
  int captured_count = 0;
  (void)events_.visit([&captured, &captured_count](const EventEntry& entry) noexcept -> bool {
    const SoundFields fields = read_sound_fields(entry);
    if (!fields.valid) {
      return true;  // not a sound event
    }
    // Keep the **newest** markers. The ring is walked oldest first and it is
    // shared with every game event, so a fixed-size buffer filled from the
    // front would keep the **oldest** footsteps and drop the freshest ones —
    // and the 1 s age filter below then discarded the survivors. A full
    // buffer shifts and appends.
    if (captured_count >= static_cast<int>(std::size(captured))) {
      for (int i = 1; i < captured_count; ++i) {
        captured[i - 1] = captured[i];
      }
      --captured_count;
    }
    captured[captured_count] = Captured{.slot = fields.slot,
                                        .radius = fields.radius,
                                        .sequence = entry.seq,
                                        .time_ms = entry.time_ms};
    ++captured_count;
    return true;
  });
  if (captured_count == 0) {
    return 0;
  }

  // 2. The positions: the players of the latest snapshot (a fixed-size table,
  //    so no allocation and no copy of the snapshot's vectors).
  const std::uint64_t now = now_ms();
  int written = 0;
  const std::scoped_lock lock(sound_mutex_);
  for (int i = 0; i < captured_count && written < max; ++i) {
    const SoundPlayer* player =
        resolve_sound_player(sound_players_.data(), sound_player_count_, captured[i].slot);
    if (player == nullptr) {
      continue;  // the slot names no player we know
    }
    const std::uint64_t age =
        captured[i].time_ms > 0 && now >= captured[i].time_ms ? now - captured[i].time_ms : 0;
    if (age > k_sound_max_age_ms) {
      continue;  // too old to still be a marker
    }
    out[written] = SoundEvent{.player_index = player->index,
                              .origin = player->origin,
                              .radius = captured[i].radius,
                              .age_ms = age,
                              .sequence = captured[i].sequence};
    ++written;
  }
  return written;
}

void Service::set_esp_features(bool glow, bool visible_only) noexcept {
  const bool enabled = glow || visible_only;
  visibility_enabled_.store(enabled, std::memory_order_relaxed);
  if (!enabled) {
    // The toggles are off: the states would be stale anyway, so drop them.
    const std::scoped_lock lock(visibility_mutex_);
    visibility_.clear();
  }
}

auto Service::visibility(int player_index) const noexcept -> Visibility {
  if (!visibility_enabled_.load(std::memory_order_relaxed)) {
    return Visibility::unknown;  // the toggles are off: there is nothing to answer
  }
  // The ask arms the world query behind the answer.
  visibility_wanted_.store(true, std::memory_order_relaxed);
  const std::scoped_lock lock(visibility_mutex_);
  return visibility_.get(player_index, now_ms());
}

void Service::refresh_visibility(const game::Snapshot& snapshot) noexcept {
  if (!snapshot.valid || !snapshot.local.has_value()) {
    return;
  }
  const std::uint64_t now = now_ms();
  const std::uint64_t last = visibility_publish_ms_.load(std::memory_order_relaxed);
  if (now < last || now - last < k_visibility_interval_ms) {
    return;  // the low-rate gate
  }
  visibility_publish_ms_.store(now, std::memory_order_relaxed);

  // The eye the enumeration used (the same snapshot the solids belong to).
  game::Vec3 eye{};
  {
    const std::scoped_lock lock(occlusion_mutex_);
    eye = occlusion_eye_;
  }

  // The rays: the eye to every living player's head.
  struct Ray {
    game::Vec3 direction{};
    float distance = 0.0F;
    game::Vec3 lo{};
    game::Vec3 hi{};
    int index = -1;
  };
  Ray rays[k_max_visibility_players];
  int ray_count = 0;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || ray_count >= k_max_visibility_players) {
      continue;
    }
    const game::Vec3 to = player.eye_origin - eye;
    const float distance = game::length(to);
    if (distance <= 1.0F) {
      continue;
    }
    Ray& ray = rays[ray_count++];
    ray.direction = game::normalized(to);
    ray.distance = distance;
    ray.lo = eye;
    ray.hi = player.eye_origin;
    ray.index = player.index;
  }
  if (ray_count == 0) {
    return;
  }

  // The solids: the enumeration's hulls — the very set the ragebot projects.
  // A hull tagged for another player can still cross this line of sight (the
  // eye→target pyramids share the eye), so every hull is tested.
  std::array<bool, k_max_visibility_players> hidden{};
  aim::OccluderCell cell;  // reused: the ray test reads the planes / the BVH
  {
    const std::scoped_lock lock(occlusion_mutex_);
    for (const OcclusionHull& hull : occlusion_hulls_) {
      // The cell carries what the ray test reads: a polytope's inward
      // half-spaces, the analytic sphere / capsule, and the mesh's BVH
      // (shared, so the triangle soup is not copied). It is built once per
      // solid and reused for every ray.
      cell.kind = hull.kind;
      cell.planes = hull.planes;
      cell.mesh_bvh = hull.mesh_bvh;
      cell.a = hull.a;
      cell.b = hull.b;
      cell.radius = hull.radius;
      if (hull.kind == aim::SolidKind::mesh && hull.mesh_bvh == nullptr) {
        cell.triangles = hull.triangles;  // no BVH: the ray test scans the soup
      }
      for (int i = 0; i < ray_count; ++i) {
        if (hidden[static_cast<std::size_t>(i)]) {
          continue;
        }
        const Ray& ray = rays[static_cast<std::size_t>(i)];
        // The broad phase: the solid's world AABB against the segment's.
        if (hull.has_bounds && (hull.bounds_max.x < ray.lo.x || hull.bounds_min.x > ray.hi.x ||
                                hull.bounds_max.y < ray.lo.y || hull.bounds_min.y > ray.hi.y ||
                                hull.bounds_max.z < ray.lo.z || hull.bounds_min.z > ray.hi.z)) {
          continue;  // cannot lie between the eye and the player
        }
        const auto span = cell.ray_span(eye, ray.direction);
        if (!span.has_value()) {
          continue;
        }
        if (span->exit > 0.0F && span->entry < ray.distance) {
          hidden[static_cast<std::size_t>(i)] = true;
        }
      }
    }
  }

  // Publish the states (the render thread only ever reads them).
  const std::scoped_lock lock(visibility_mutex_);
  for (int i = 0; i < ray_count; ++i) {
    visibility_.publish(
        rays[static_cast<std::size_t>(i)].index,
        hidden[static_cast<std::size_t>(i)] ? Visibility::hidden : Visibility::visible, now);
  }
}

void Service::do_aim(const Json& args, Json& out) noexcept {
  refresh_reader(true);
  if (!reader_.has_value()) {
    set_error(out, "aim unavailable: client.dll / entity system not ready");
    return;
  }
  const game::Snapshot snapshot = reader_->snapshot();
  refresh_reader(snapshot.valid);

  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  const auto integer = [&](const char* key, int fallback) -> int {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<int>(value->as_number()) : fallback;
  };

  int target = -1;
  if (const Json* value = args.find("target"); value != nullptr && value->is_number()) {
    target = static_cast<int>(value->as_number());
  }

  aim::WeaponProfile weapon;
  weapon.damage = number("damage", 100.0F);
  weapon.range_modifier = number("range_modifier", 0.98F);
  weapon.penetration = number("penetration", 1.0F);
  weapon.headshot_multiplier = number("headshot", 4.0F);
  // When the caller passes explicit values, honour them; otherwise take the
  // shooter's real weapon VData (`docs/re/aim.md`).
  const bool explicit_weapon =
      args.find("damage") != nullptr || args.find("range_modifier") != nullptr ||
      args.find("penetration") != nullptr || args.find("headshot") != nullptr;

  // **The weapon is always read from the game.** The Monte-Carlo kernel is
  // built from the weapon's **spread**, **inaccuracy** and **type**: with the
  // caller's profile alone they are 0 / 0 / -1, so every sample lands exactly
  // on the aim and the "measured probability" is 1.0 for a weapon that really
  // spreads — and an unknown type counts as "modelled", so a shotgun would be
  // scored with the standard pattern. The caller's own damage / range /
  // penetration / headshot are overlaid afterwards, so the caller still wins
  // on those.
  auto input = aim::build_input(snapshot, weapon, target, /*weapon_from_game=*/true);
  if (!input.has_value()) {
    set_error(out, "aim unavailable: no local player / live enemy");
    return;
  }
  if (explicit_weapon) {
    input->weapon = apply_stated_weapon(input->weapon, weapon);
  }
  // The weapon **type**, when the VData could not be read: the snapshot
  // carries the same field, read independently (`state_reader.cpp`). An
  // unknown type is "modelled" by the kernel, so it must not be left unknown
  // when the payload knows it (`pattern_supported`).
  if (input->weapon.weapon_type < 0 && snapshot.local.has_value()) {
    input->weapon.weapon_type = snapshot.local->weapon_type;
  }
  input->pose_sigma = number("sigma", 0.0F);
  // The sample count the caller asked for and the one in force (the Monte-Carlo
  // branch sets them; the payload reports the difference).
  int mc_samples_requested = MC_SAMPLES_DEFAULT;
  int mc_samples_in_force = MC_SAMPLES_DEFAULT;
  if (const Json* objective = args.find("objective");
      objective != nullptr && objective->is_string()) {
    const std::string_view name = objective->as_string();
    // The name -> objective mapping is a **pure rule**
    // (`cs2/control/rage_objective.hpp`), so the service and the host test
    // cannot disagree about it. An unknown name selects the `damage` objective,
    // exactly as before.
    input->objective = objective_for_name(name);
    if (input->objective == aim::AimObjective::pellets) {
      // The deterministic pellet-sum objective (docs/ragebot.md §2/§3). The seed is
      // `MD5_PseudoRandom(sequence)` unless an explicit `seed` is passed; the
      // spread/inaccuracy magnitudes come from the caller until the weapon's
      // inaccuracy (`FUN_18080b880`) is wired live (`docs/re/aim.md` §9).
      input->objective = aim::AimObjective::pellets;
      // **The `seed` key means an integer seed here** (the pellet offsets'
      // seed); under the Monte-Carlo objective the same key means the seed
      // *policy's* name. The two are told apart by their type.
      int seed = integer("seed", 0);
      if (seed == 0) {
        seed = aim::md5_pseudo_random(static_cast<std::uint32_t>(integer("sequence", 1)));
      }
      const int pellets = integer("pellets", 1);
      // Spread defaults to the shooter's real `m_flSpread`; inaccuracy to the engine's
      // effective value (`m_flInterpolatedInaccuracy`) — both read into the weapon
      // profile by `build_input`. A stated value wins and is **written back**
      // into the profile, so the report and the cone below use the same numbers
      // as the pellet offsets.
      if (args.find("spread") != nullptr) {
        input->weapon.spread = number("spread", input->weapon.spread);
      }
      if (args.find("inaccuracy") != nullptr) {
        input->weapon.inaccuracy = number("inaccuracy", input->weapon.inaccuracy);
      }
      input->pellet_offsets = aim::pellet_offsets(
          seed, input->weapon.spread, input->weapon.inaccuracy, pellets > 0 ? pellets : 1);
    } else if (input->objective == aim::AimObjective::monte_carlo) {
      // The Monte-Carlo objective (`docs/ragebot.md` section 13.7). The seed
      // context's tick is the prediction tick; the sequence is the **documented
      // fallback** (the tick) unless the caller states one -- the command's
      // sequence number is not reachable from here (`prediction_tick`).
      const std::uint32_t tick = prediction_tick(snapshot);
      const int stated_sequence = integer("sequence", 0);
      const int stated_tick = integer("tick", 0);
      const int samples = integer("samples", MC_SAMPLES_DEFAULT);
      int seed_source = SEED_SOURCE_SEQUENCE;
      // **The `seed` key means the seed *policy's* name here** (an integer
      // seed under the `pellets` objective): the two are told apart by their
      // type.
      if (const Json* value = args.find("seed"); value != nullptr && value->is_string()) {
        seed_source = seed_source_for(value->as_string());
      }
      // The sample count: an out-of-range count is **ignored**, exactly as the
      // `rage` command ignores it, so the two entry points behave the same.
      // The payload says which count is in force and whether it fits the tick.
      mc_samples_requested = samples;
      mc_samples_in_force = mc_samples_in_range(samples) ? samples : MC_SAMPLES_DEFAULT;
      input->monte_carlo_samples = mc_samples_in_force;
      input->monte_carlo_seed = aim::SeedContext{
          .sequence = stated_sequence > 0 ? static_cast<std::uint32_t>(stated_sequence) : tick,
          .tick = stated_tick > 0 ? static_cast<std::uint32_t>(stated_tick) : tick,
          .source = static_cast<aim::SeedSource>(seed_source)};
      // The weapon's pattern: the core has no default for it, and the designer
      // name is the only thing that tells a revolver from a rifle.
      input->monte_carlo_pattern =
          spread_pattern_for(snapshot.local.has_value() ? std::string_view{snapshot.local->weapon}
                                                        : std::string_view{});
      // The target's own health: the rule's first key is judged against it.
      //
      // **The index comes from the hitboxes**, which the reader stamped with
      // the target it chose: the health list and the hitboxes must speak about
      // **the same** target, or the core's can-kill lookup never matches (the
      // rule is `cs2/control/rage_objective.hpp`).
      input->monte_carlo_targets.clear();
      const int target_index = hitbox_target(input->hitboxes);
      if (target_index >= 0) {
        for (const game::PlayerSnapshot& player : snapshot.players) {
          if (player.index == target_index) {
            input->monte_carlo_targets.push_back(
                health_entry_for(input->hitboxes, static_cast<float>(player.health)));
            break;
          }
        }
      }
    }
  }

  // The spread cone for the hitchance objective (`docs/re/aim.md` §"Inaccuracy"): an
  // explicit `cone` (radians) or the weapon's first-order inaccuracy + spread (standing;
  // the exact state composition `FUN_18080b880` still needs a live cross-check).
  {
    float cone = number("cone", -1.0F);
    if (cone < 0.0F) {
      const float inacc = aim::inaccuracy(input->weapon, 0.0F, true, false);
      cone = std::sqrt(inacc * inacc + input->weapon.spread * input->weapon.spread);
    }
    input->spread_sigma = cone;
  }

  const aim::SolveResult result = aim::solve(*input);
  if (!result.valid) {
    set_error(out, result.occlusion_candidates_complete
                       ? "aim unavailable: no hittable target"
                       : "aim unavailable: per-hitbox occluder cap");
    return;
  }

  constexpr float k_rad_to_deg = 57.295779513082320876F;
  const float yaw = std::atan2(result.direction.y, result.direction.x) * k_rad_to_deg;
  const float pitch = -std::asin(std::clamp(result.direction.z, -1.0F, 1.0F)) * k_rad_to_deg;

  // `write: true` applies the solve to the (silent) sub-tick view angles.
  const Json* write_arg = args.find("write");
  const bool write = write_arg != nullptr && write_arg->is_bool() && write_arg->as_bool();
  if (write) {
    input_.refresh();
    input_.set_view_angles(pitch, yaw, 0.0F);
  }

  Json payload = Json::object();
  payload.set("valid", Json::boolean(true));
  payload.set("yaw", Json::number(yaw));
  payload.set("pitch", Json::number(pitch));
  payload.set("value", Json::number(result.value));
  payload.set("hitchance", Json::number(static_cast<double>(result.hitchance)));
  payload.set("cone", Json::number(static_cast<double>(input->spread_sigma)));
  payload.set("weapon_spread", Json::number(static_cast<double>(input->weapon.spread)));
  payload.set("weapon_inaccuracy", Json::number(static_cast<double>(input->weapon.inaccuracy)));
  payload.set("cells", Json::number(static_cast<double>(result.cells)));
  payload.set("aim_u", Json::number(result.aim.x));
  payload.set("aim_v", Json::number(result.aim.y));
  payload.set("wrote_view", Json::boolean(write));
  // The objective **by name**, and — for the Monte-Carlo one — the numbers it
  // produced and the reason it refused, if it did (`docs/ragebot.md` section
  // 13.7). The two objectives' numbers mean different things, so the name is
  // what tells them apart.
  payload.set("objective", Json::string(std::string(objective_name_of(input->objective))));
  // The sample count in force and what the operator has to know about it: a
  // count outside 1..256 is ignored, and a count above 64 does not fit the
  // tick (`cs2/control/rage_objective.hpp`).
  payload.set("mc_samples", Json::number(static_cast<double>(mc_samples_in_force)));
  payload.set("mc_samples_requested", Json::number(static_cast<double>(mc_samples_requested)));
  payload.set(
      "mc_samples_note",
      Json::string(std::string(mc_samples_note(mc_samples_requested, mc_samples_in_force))));
  if (result.monte_carlo.has_value()) {
    payload.set("mc_samples", Json::number(static_cast<double>(result.monte_carlo->samples)));
    payload.set("mc_hits", Json::number(static_cast<double>(result.monte_carlo->hits)));
    payload.set("mc_hitchance", Json::number(static_cast<double>(result.monte_carlo->probability)));
    payload.set("mc_expected_damage",
                Json::number(static_cast<double>(result.monte_carlo->expected_damage)));
    payload.set("mc_target", Json::number(static_cast<double>(result.monte_carlo->target)));
    payload.set("mc_hitgroup", Json::number(static_cast<double>(result.monte_carlo->hitgroup)));
    payload.set("mc_can_kill", Json::boolean(result.monte_carlo->can_kill));
    payload.set("mc_filtered",
                Json::number(static_cast<double>(result.monte_carlo->filtered_hitboxes)));
    const int seed = static_cast<int>(result.monte_carlo->seed_source);
    payload.set("mc_seed", Json::string(std::string(seed_source_name(seed))));
    // **The seed's disclosure** (`docs/ragebot.md` §13.7): under the `sequence`
    // policy the tick stands in for the command's sequence number, so the
    // caller must be told that the number is a stand-in.
    payload.set("mc_seed_fallback", Json::boolean(seed_source_is_fallback(seed)));
    payload.set("mc_seed_note", Json::string(std::string(seed_source_note(seed))));
  }
  // The refusal is always reported (empty when there was none), so a fallback
  // to another objective is never silent.
  payload.set("mc_refusal",
              Json::string(result.monte_carlo_refusal.has_value()
                               ? std::string(aim::to_string(*result.monte_carlo_refusal))
                               : std::string()));
  payload.set("mc_fallback", Json::boolean(result.monte_carlo_refusal.has_value()));

  // Raw model hitboxes (validation for the exact-hitbox path).
  Json hitboxes = Json::array();
  for (const aim::ModelHitbox& box : aim::read_model_hitboxes(input->pawn)) {
    Json item = Json::object();
    item.set("bone", Json::number(static_cast<double>(box.bone)));
    item.set("mesh", Json::number(static_cast<double>(box.mesh)));
    item.set("group", Json::number(static_cast<double>(box.group)));
    item.set("shape", Json::number(static_cast<double>(box.shape)));
    item.set("name", Json::string(std::string(box.name)));
    item.set("radius", Json::number(static_cast<double>(box.radius)));
    item.set("min_x", Json::number(box.mins.x));
    item.set("min_y", Json::number(box.mins.y));
    item.set("min_z", Json::number(box.mins.z));
    item.set("max_x", Json::number(box.maxs.x));
    item.set("max_y", Json::number(box.maxs.y));
    item.set("max_z", Json::number(box.maxs.z));
    // World capsule endpoints (the bone-transformed min/max) — the exact input
    // to the solver and the renderer, so an external render can be compared.
    item.set("a_x", Json::number(box.world_a.x));
    item.set("a_y", Json::number(box.world_a.y));
    item.set("a_z", Json::number(box.world_a.z));
    item.set("b_x", Json::number(box.world_b.x));
    item.set("b_y", Json::number(box.world_b.y));
    item.set("b_z", Json::number(box.world_b.z));
    // The bone transform used: position + quaternion (x, y, z, w).
    item.set("bone_x", Json::number(box.transform.position.x));
    item.set("bone_y", Json::number(box.transform.position.y));
    item.set("bone_z", Json::number(box.transform.position.z));
    item.set("quat_x", Json::number(box.transform.rotation[0]));
    item.set("quat_y", Json::number(box.transform.rotation[1]));
    item.set("quat_z", Json::number(box.transform.rotation[2]));
    item.set("quat_w", Json::number(box.transform.rotation[3]));
    item.set("world_x", Json::number((box.world_a.x + box.world_b.x) * 0.5));
    item.set("world_y", Json::number((box.world_a.y + box.world_b.y) * 0.5));
    item.set("world_z", Json::number((box.world_a.z + box.world_b.z) * 0.5));
    hitboxes.push_back(std::move(item));
  }
  payload.set("hitboxes", std::move(hitboxes));
  payload.set("hitcount", Json::number(static_cast<double>(input->hitboxes.size())));
  // The weapon damage model actually used (the real VData, or the caller's).
  payload.set("weapon_damage", Json::number(static_cast<double>(input->weapon.damage)));
  payload.set("weapon_headshot",
              Json::number(static_cast<double>(input->weapon.headshot_multiplier)));
  payload.set("weapon_range_modifier",
              Json::number(static_cast<double>(input->weapon.range_modifier)));
  payload.set("weapon_penetration", Json::number(static_cast<double>(input->weapon.penetration)));
  set_ok(out, std::move(payload));
}

void Service::do_rage(const Json& args, Json& out) noexcept {
  bool on = rage_enabled_.load();
  if (const Json* value = args.find("on"); value != nullptr && value->is_bool()) {
    on = value->as_bool();
  }
  int key = rage_key_.load();
  if (const Json* value = args.find("key"); value != nullptr && value->is_number()) {
    key = static_cast<int>(value->as_number());
  }
  bool fire = rage_fire_.load();
  if (const Json* value = args.find("fire"); value != nullptr && value->is_bool()) {
    fire = value->as_bool();
  }
  if (const Json* value = args.find("force"); value != nullptr && value->is_bool()) {
    rage_force_.store(value->as_bool());
  }
  if (const Json* value = args.find("damage"); value != nullptr && value->is_number()) {
    rage_damage_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("headshot"); value != nullptr && value->is_number()) {
    rage_headshot_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("range_modifier"); value != nullptr && value->is_number()) {
    rage_range_modifier_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("sigma"); value != nullptr && value->is_number()) {
    rage_sigma_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("objective"); value != nullptr && value->is_string()) {
    // The name -> value mapping is a **pure rule**
    // (`cs2/control/rage_objective.hpp`). An unknown name selects the documented
    // safe default -- the hitchance -- exactly as it did before.
    rage_objective_.store(objective_value_for(value->as_string()));
  }
  if (const Json* value = args.find("seed"); value != nullptr && value->is_string()) {
    // The seed policy: which of the two accounts of the engine's seed is in
    // force (`sequence` -- the repo's own RE -- or `angle_grid` -- the
    // community's, with the stand-in hash). An unknown name selects the
    // default, the `sequence` policy.
    rage_mc_seed_.store(seed_source_for(value->as_string()));
  }
  if (const Json* value = args.find("samples"); value != nullptr && value->is_number()) {
    // The Monte-Carlo sample count. The **default is 64** — the only count
    // that fits the frame budget at a live-sized hitbox set (`docs/ragebot.md`
    // §13.6). A different count must be an explicit choice, and the status
    // shows which one is in force.
    //
    // **A count outside 1..256 is ignored** — exactly as the `aim` command
    // ignores it, so the two entry points behave the same way. The status says
    // so (`mc_samples_note`).
    const int samples = static_cast<int>(value->as_number());
    rage_mc_samples_requested_.store(samples, std::memory_order_relaxed);
    if (mc_samples_in_range(samples)) {
      rage_mc_samples_.store(samples, std::memory_order_relaxed);
    }
  }
  if (const Json* value = args.find("fov"); value != nullptr && value->is_number()) {
    rage_fov_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("min_damage"); value != nullptr && value->is_number()) {
    rage_min_damage_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("min_hitchance"); value != nullptr && value->is_number()) {
    rage_min_hitchance_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("hitbox"); value != nullptr && value->is_number()) {
    rage_hitbox_.store(static_cast<int>(value->as_number()));
  }
  if (const Json* value = args.find("autostop"); value != nullptr && value->is_number()) {
    rage_autostop_.store(static_cast<int>(value->as_number()));
  }
  if (const Json* value = args.find("autoscope"); value != nullptr && value->is_number()) {
    rage_autoscope_.store(static_cast<int>(value->as_number()));
  }
  if (const Json* value = args.find("autoreload"); value != nullptr && value->is_bool()) {
    rage_autoreload_.store(value->as_bool());
  }
  if (const Json* value = args.find("only_visible"); value != nullptr && value->is_bool()) {
    rage_only_visible_.store(value->as_bool());
  }
  if (const Json* value = args.find("recoil"); value != nullptr && value->is_bool()) {
    rage_recoil_.store(value->as_bool());
  }
  if (const Json* value = args.find("backtrack"); value != nullptr && value->is_bool()) {
    rage_backtrack_.store(value->as_bool());
  }
  if (const Json* value = args.find("stop_speed"); value != nullptr && value->is_number()) {
    rage_stop_speed_.store(static_cast<float>(value->as_number()));
  }
  // The trigger: `mode` 0 hold / 1 always / 2 toggle, or the names `hold`/`always`/
  // `toggle`. `key` may be a number (virtual-key code) in the same call.
  if (const Json* value = args.find("mode"); value != nullptr) {
    if (value->is_number()) {
      rage_mode_.store(static_cast<int>(value->as_number()));
    } else if (value->is_string()) {
      const std::string_view name = value->as_string();
      rage_mode_.store(name == "always" ? 1 : (name == "toggle" ? 2 : 0));
    }
  }
  if (const Json* value = args.find("per_weapon"); value != nullptr && value->is_bool()) {
    rage_per_weapon_.store(value->as_bool());
  }
  char weapon[24] = {};
  {
    const std::scoped_lock lock(rage_config_mutex_);
    if (const Json* value = args.find("weapon"); value != nullptr && value->is_string()) {
      rage_weapon_key(std::string(value->as_string()).c_str(), rage_weapon_, sizeof(rage_weapon_));
    }
    std::snprintf(weapon, sizeof(weapon), "%s", rage_weapon_);
  }
  const auto config_flag = [&](const char* name) -> bool {
    const Json* value = args.find(name);
    return value != nullptr && value->is_bool() && value->as_bool();
  };
  if (config_flag("profile_save")) {
    apply_rage_config_op(1, weapon);
  }
  if (config_flag("profile_load")) {
    apply_rage_config_op(2, weapon);
  }
  if (config_flag("save")) {
    apply_rage_config_op(3, weapon);
  }
  if (config_flag("load")) {
    apply_rage_config_op(4, weapon);
  }
  if (config_flag("profile_delete")) {
    apply_rage_config_op(5, weapon);
  }
  input_.refresh();
  set_rage_features(on, key, fire, rage_objective_.load(), rage_hitbox_.load(), rage_fov_.load(),
                    rage_min_damage_.load(), rage_min_hitchance_.load(), rage_autostop_.load(),
                    rage_autoscope_.load(), rage_autoreload_.load(), rage_only_visible_.load(),
                    rage_mode_.load(), rage_per_weapon_.load(), weapon, 0,
                    rage_config_token_.load(), rage_recoil_.load(), rage_stop_speed_.load(),
                    rage_backtrack_.load());

  Json payload = Json::object();
  payload.set("enabled", Json::boolean(on));
  payload.set("key", Json::number(static_cast<double>(key)));
  payload.set("fire", Json::boolean(fire));
  payload.set("force", Json::boolean(rage_force_.load()));
  payload.set("active", Json::boolean(input_.rage_active()));
  payload.set("target", Json::number(static_cast<double>(input_.rage_target())));
  payload.set("hitbox", Json::number(static_cast<double>(input_.rage_hitbox())));
  payload.set("solves", Json::number(static_cast<double>(rage_solves_.load())));
  payload.set("writes", Json::number(static_cast<double>(input_.rage_writes())));
  payload.set("shots", Json::number(static_cast<double>(input_.rage_shots())));
  payload.set("occluders", Json::number(static_cast<double>(rage_occluders_.load())));
  payload.set("occlusion_query_complete",
              Json::boolean(rage_occlusion_query_complete_.load(std::memory_order_relaxed)));
  payload.set("occlusion_query_issues",
              Json::integer(static_cast<std::int64_t>(
                  rage_occlusion_query_issues_.load(std::memory_order_relaxed))));
  payload.set("backtracks", Json::number(static_cast<double>(rage_backtracks_.load())));
  payload.set("record", Json::number(static_cast<double>(rage_last_record_.load())));
  // Whether the last tick **skipped** the backtracking because the frame had
  // already blown the 12 ms guard: the operator sees the backtrack counter
  // stop incrementing and needs to know why (`docs/ragebot-perf.md` §3.9).
  payload.set("backtrack_over_budget",
              Json::boolean(rage_backtrack_over_budget_.load(std::memory_order_relaxed)));
  // The objective **by name**, not by number: the operator has to be able to
  // tell the Monte-Carlo objective from the others, and the numbers mean
  // different things under each (`docs/ragebot.md` section 13.7). The value is
  // still reported, under its own key.
  //
  // **The effective objective** is the one the solve ran: with a per-weapon
  // profile the profile's objective wins (`effective_rage_tuning`), so naming
  // the global setting would label the numbers with the wrong objective. The
  // global one is reported under its own key.
  const int global_objective = rage_objective_.load();
  const int effective_objective =
      rage_per_weapon_.load() ? rage_effective_objective_.load() : global_objective;
  payload.set("objective", Json::string(std::string(objective_name_for(effective_objective))));
  payload.set("objective_value", Json::number(static_cast<double>(effective_objective)));
  payload.set("objective_global", Json::string(std::string(objective_name_for(global_objective))));
  // The Monte-Carlo objective's own state: the sample count, the seed policy in
  // force, the numbers the last solve produced, and the reason it refused if it
  // did. **A fallback to another objective is never silent**: `mc_refusal` names
  // it and `mc_fallback` says the aim came from the `damage` objective.
  //
  // The verdict is read as **one value** (`read_monte_carlo`): the reader can
  // never see a half-published verdict, so a refusal can never be hidden.
  {
    const int seed = rage_mc_seed_.load();
    const int samples = rage_mc_samples_.load();
    const MonteCarloState state = read_monte_carlo();
    payload.set("mc_samples", Json::number(static_cast<double>(samples)));
    payload.set("mc_samples_note",
                Json::string(std::string(mc_samples_note(
                    rage_mc_samples_requested_.load(std::memory_order_relaxed), samples))));
    payload.set("mc_seed", Json::string(std::string(seed_source_name(seed))));
    payload.set("mc_seed_fallback", Json::boolean(seed_source_is_fallback(seed)));
    payload.set("mc_seed_note", Json::string(std::string(seed_source_note(seed))));
    payload.set("mc_hitchance", Json::number(static_cast<double>(state.probability)));
    payload.set("mc_expected_damage", Json::number(static_cast<double>(state.expected_damage)));
    payload.set("mc_target", Json::number(static_cast<double>(state.target)));
    payload.set("mc_hitgroup", Json::number(static_cast<double>(state.hitgroup)));
    payload.set("mc_hits", Json::number(static_cast<double>(state.hits)));
    payload.set("mc_can_kill", Json::boolean(state.can_kill));
    payload.set("mc_filtered", Json::number(static_cast<double>(state.filtered_hitboxes)));
    const int refusal = state.refusal;
    payload.set(
        "mc_refusal",
        Json::string(refusal >= 0
                         ? std::string(aim::to_string(static_cast<aim::MonteCarloError>(refusal)))
                         : std::string()));
    payload.set("mc_fallback", Json::boolean(refusal >= 0));
    payload.set("mc_valid", Json::boolean(state.valid));
  }
  payload.set("hitbox_mode", Json::number(static_cast<double>(rage_hitbox_.load())));
  payload.set("fov", Json::number(static_cast<double>(rage_fov_.load())));
  payload.set("min_damage", Json::number(static_cast<double>(rage_min_damage_.load())));
  payload.set("min_hitchance", Json::number(static_cast<double>(rage_min_hitchance_.load())));
  payload.set("autostop", Json::number(static_cast<double>(rage_autostop_.load())));
  payload.set("autoscope", Json::number(static_cast<double>(rage_autoscope_.load())));
  payload.set("autoreload", Json::boolean(rage_autoreload_.load()));
  payload.set("only_visible", Json::boolean(rage_only_visible_.load()));
  payload.set("recoil", Json::boolean(rage_recoil_.load()));
  payload.set("backtrack", Json::boolean(rage_backtrack_.load()));
  payload.set("stop_speed", Json::number(static_cast<double>(rage_stop_speed_.load())));
  payload.set("visible", Json::boolean(rage_visible_.load()));
  payload.set("stops", Json::number(static_cast<double>(input_.rage_stops())));
  payload.set("mode", Json::number(static_cast<double>(rage_mode_.load())));
  payload.set("per_weapon", Json::boolean(rage_per_weapon_.load()));
  {
    const std::scoped_lock lock(rage_config_mutex_);
    payload.set("weapon", Json::string(std::string(rage_weapon_)));
    payload.set("profiles", Json::number(static_cast<double>(rage_profiles_.count)));
  }
  // The instrumentation (`docs/ragebot-perf.md`): averages since process start
  // (`*_us`), the last frame's cost, the worst frame, and the last frame's counts. A
  // high `backtrack_us` with `poses` ≫ 1 points at the per-pose solve; a high
  // `argmax`/`solve_us` with large `cells` points at the field's objective search.
  {
    const std::uint64_t frames = perf_.frames.load(std::memory_order_relaxed);
    const std::uint64_t snapshots = perf_.snapshot_frames.load(std::memory_order_relaxed);
    const auto avg_us = [frames](const std::atomic<std::uint64_t>& acc) -> double {
      return frames != 0 ? static_cast<double>(acc.load(std::memory_order_relaxed)) /
                               static_cast<double>(frames)
                         : 0.0;
    };
    const auto avg_snapshot = [snapshots](const std::atomic<std::uint64_t>& acc) -> double {
      return snapshots != 0 ? static_cast<double>(acc.load(std::memory_order_relaxed)) /
                                  static_cast<double>(snapshots)
                            : 0.0;
    };
    Json perf = Json::object();
    perf.set("frames", Json::number(static_cast<double>(frames)));
    perf.set("snapshot_frames", Json::number(static_cast<double>(snapshots)));
    perf.set("total_us", Json::number(avg_us(perf_.total_us)));
    perf.set("last_us", Json::number(static_cast<double>(perf_.last_total_us.load())));
    perf.set("max_us", Json::number(static_cast<double>(perf_.max_total_us.load())));
    perf.set("sample_us", Json::number(avg_snapshot(perf_.sample_us)));
    perf.set("query_us", Json::number(avg_snapshot(perf_.query_us)));
    perf.set("build_us", Json::number(avg_us(perf_.build_us)));
    perf.set("project_us", Json::number(avg_us(perf_.project_us)));
    perf.set("solve_us", Json::number(avg_us(perf_.solve_us)));
    perf.set("backtrack_us", Json::number(avg_us(perf_.backtrack_us)));
    perf.set("solve_project_us", Json::number(avg_us(perf_.solve_project_us)));
    perf.set("solve_occlude_us", Json::number(avg_us(perf_.solve_occlude_us)));
    perf.set("solve_argmax_us", Json::number(avg_us(perf_.solve_argmax_us)));
    perf.set("solve_calls", Json::number(static_cast<double>(perf_.solve_calls.load())));
    perf.set("candidates", Json::number(static_cast<double>(perf_.candidates.load())));
    perf.set("hitboxes", Json::number(static_cast<double>(perf_.hitboxes.load())));
    perf.set("occluders", Json::number(static_cast<double>(perf_.occluders.load())));
    perf.set("poses", Json::number(static_cast<double>(perf_.poses.load())));
    perf.set("cells", Json::number(static_cast<double>(perf_.cells.load())));
    payload.set("perf", std::move(perf));
  }
  set_ok(out, std::move(payload));
}

void Service::do_trigger(const Json& args, Json& out) noexcept {
  // The triggerbot's settings surface (`docs/ragebot.md` §"The trigger"). The
  // trigger's configuration is **not** in the wire settings block (the block is
  // versioned and full), so it travels through the control channel and the
  // environment. With no arguments this is a pure query: it returns the current
  // state and the last decision.
  TriggerConfig config = trigger_.config();
  if (const Json* value = args.find("on"); value != nullptr && value->is_bool()) {
    config.on = value->as_bool();
  }
  if (const Json* value = args.find("hitchance"); value != nullptr && value->is_number()) {
    config.hitchance = std::clamp(static_cast<float>(value->as_number()), 0.0F, 1.0F);
  }
  if (const Json* value = args.find("head_hitchance"); value != nullptr && value->is_number()) {
    config.head_hitchance = std::clamp(static_cast<float>(value->as_number()), 0.0F, 1.0F);
  }
  if (const Json* value = args.find("yaw_tolerance"); value != nullptr && value->is_number()) {
    config.yaw_tolerance = std::clamp(static_cast<float>(value->as_number()), 0.0F, 360.0F);
  }
  if (const Json* value = args.find("pitch_tolerance"); value != nullptr && value->is_number()) {
    config.pitch_tolerance = std::clamp(static_cast<float>(value->as_number()), 0.0F, 360.0F);
  }
  if (const Json* value = args.find("delay_ms"); value != nullptr && value->is_number()) {
    config.delay_ms = static_cast<std::uint32_t>(std::clamp(value->as_number(), 0.0, 60000.0));
  }
  if (const Json* value = args.find("key"); value != nullptr && value->is_number()) {
    config.key = static_cast<int>(value->as_int());
  }
  if (const Json* value = args.find("fire"); value != nullptr && value->is_bool()) {
    config.fire = value->as_bool();
  }
  if (const Json* value = args.find("force"); value != nullptr && value->is_bool()) {
    config.force = value->as_bool();
  }
  if (const Json* value = args.find("solve_interval_ms"); value != nullptr && value->is_number()) {
    config.solve_interval_ms =
        static_cast<std::uint32_t>(std::clamp(value->as_number(), 0.0, 1000.0));
  }
  // The mode: 0 hold / 1 always / 2 toggle, or the names `hold`/`always`/`toggle`.
  if (const Json* value = args.find("mode"); value != nullptr) {
    if (value->is_number()) {
      const auto mode = value->as_int();
      config.mode =
          mode == 1 ? TriggerMode::always : (mode == 2 ? TriggerMode::toggle : TriggerMode::hold);
    } else if (value->is_string()) {
      const std::string_view name = value->as_string();
      config.mode = name == "always" ? TriggerMode::always
                                     : (name == "toggle" ? TriggerMode::toggle : TriggerMode::hold);
    }
  }
  trigger_.configure(config);

  const TriggerDecision decision = trigger_.last_decision();
  Json payload = Json::object();
  payload.set("enabled", Json::boolean(config.on));
  payload.set("hitchance", Json::number(static_cast<double>(config.hitchance)));
  payload.set("head_hitchance", Json::number(static_cast<double>(config.head_hitchance)));
  payload.set("yaw_tolerance", Json::number(static_cast<double>(config.yaw_tolerance)));
  payload.set("pitch_tolerance", Json::number(static_cast<double>(config.pitch_tolerance)));
  payload.set("delay_ms", Json::number(static_cast<double>(config.delay_ms)));
  payload.set("key", Json::number(static_cast<double>(config.key)));
  payload.set("fire", Json::boolean(config.fire));
  payload.set("force", Json::boolean(config.force));
  payload.set("mode", Json::number(static_cast<double>(config.mode)));
  payload.set("mode_name",
              Json::string(config.mode == TriggerMode::always
                               ? "always"
                               : (config.mode == TriggerMode::toggle ? "toggle" : "hold")));
  payload.set("solve_interval_ms", Json::number(static_cast<double>(config.solve_interval_ms)));
  // The last decision: the operator's window. `firing` is what the trigger
  // actually does — with `fire` off it decides but never pulls the trigger.
  payload.set("firing", Json::boolean(config.fire && decision.hold));
  payload.set("reason", Json::string(std::string(to_string(decision.reason))));
  payload.set("delay_remaining_ms", Json::number(static_cast<double>(decision.delay_remaining_ms)));
  payload.set("target", Json::number(static_cast<double>(trigger_.target_index())));
  // The instrumentation: how often the trigger was evaluated, how often the
  // cheap pre-check skipped the solve, and what the last solve cost.
  payload.set("evaluations", Json::number(static_cast<double>(trigger_.evaluations())));
  payload.set("gate_skips", Json::number(static_cast<double>(trigger_.gate_skips())));
  payload.set("solves", Json::number(static_cast<double>(trigger_.solves())));
  payload.set("shots", Json::number(static_cast<double>(trigger_.shots())));
  payload.set("last_solve_us", Json::number(static_cast<double>(trigger_.last_solve_us())));
  // The press goes through the command backend, so the backend decides whether
  // it reaches the command at all (`docs/control-harness.md` §"Input backends").
  payload.set("backend", Json::string(Input::backend_name(input_.backend())));
  set_ok(out, std::move(payload));
}

void Service::do_heatmap(const Json& args, Json& out) noexcept {
  refresh_reader(true);
  if (!reader_.has_value()) {
    set_error(out, "heatmap unavailable: client.dll / entity system not ready");
    return;
  }
  game::Snapshot snapshot = reader_->snapshot();
  refresh_reader(snapshot.valid);
  if (!snapshot.valid || !snapshot.local.has_value()) {
    set_error(out, "heatmap unavailable: no local player");
    return;
  }
  const game::PlayerSnapshot& local = *snapshot.local;

  const auto number = [&](const char* key, float fallback) -> float {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<float>(value->as_number())
                                                  : fallback;
  };
  const auto integer = [&](const char* key, int fallback) -> int {
    const Json* value = args.find(key);
    return value != nullptr && value->is_number() ? static_cast<int>(value->as_number()) : fallback;
  };
  const auto flag = [&](const char* key) -> std::optional<bool> {
    const Json* value = args.find(key);
    if (value == nullptr || !value->is_bool()) {
      return std::nullopt;
    }
    return value->as_bool();
  };

  if (const std::optional<bool> sample = flag("sample"); sample.has_value()) {
    heatmap_sampling_.store(*sample);
  }
  if (const std::optional<bool> clear = flag("clear"); clear.has_value() && *clear) {
    const std::scoped_lock lock(records_mutex_);
    records_.clear();
  }

  aim::WeaponProfile weapon;
  weapon.damage = number("damage", 100.0F);
  weapon.range_modifier = number("range_modifier", 0.98F);
  weapon.penetration = number("penetration", 1.0F);
  weapon.headshot_multiplier = number("headshot", 4.0F);
  const bool explicit_weapon =
      args.find("damage") != nullptr || args.find("range_modifier") != nullptr ||
      args.find("penetration") != nullptr || args.find("headshot") != nullptr;
  if (!explicit_weapon) {
    (void)aim::read_weapon_profile(local.weapon_vdata, weapon);
  }

  // The chart is the **absolute world yaw/pitch** of the eye→direction (docs/ragebot.md):
  // a function of the world geometry only and never the camera, so the projected
  // hitbox/occluder cells (and their values) do not move when the player looks around.
  const game::Vec3 eye = local.eye_origin;
  const aim::Basis basis = aim::Basis::world();

  constexpr float k_deg_to_rad = 0.01745329251994329577F;
  constexpr float k_pi = 3.14159265358979323846F;
  // The **view window** (a render concern, not the computation): centred on the camera
  // direction, `fov` degrees wide. The values are absolute; only the shown window moves
  // with the camera. The camera direction in chart coords is (yaw, -pitch) — Source
  // pitch is positive downwards.
  const float fov = number("fov", 90.0F);
  const float half_span = fov > 0.0F ? fov * 0.5F * k_deg_to_rad : k_pi;
  const float extent = half_span < k_pi ? half_span : k_pi;
  aim::Vec2 view{.x = local.eye_angles.yaw * k_deg_to_rad,
                 .y = -local.eye_angles.pitch * k_deg_to_rad};
  // The optional `target`: the view window is centred on the direction from
  // the eye to that entity instead of on the camera. The **chart** itself
  // stays the absolute world yaw/pitch field (the basis is unchanged), so the
  // cell values do not move — only the window the chart is clipped to, and
  // rendered through, does.
  int view_target = -1;
  if (const Json* value = args.find("target"); value != nullptr && value->is_number()) {
    view_target = static_cast<int>(value->as_number());
    const game::PlayerSnapshot* target = nullptr;
    for (const game::PlayerSnapshot& player : snapshot.players) {
      if (player.index == view_target) {
        target = &player;
        break;
      }
    }
    if (target == nullptr) {
      set_error(out, "heatmap: unknown target entity " + std::to_string(view_target));
      return;
    }
    // The window is centred on the target's **body**, not on its eyes: the
    // chart's cells are the hitboxes (the body), so a window centred on the
    // eye is offset by the view height (~1.6 m) from what it should frame.
    const game::Vec3 centre{(target->origin.x + target->eye_origin.x) * 0.5F,
                            (target->origin.y + target->eye_origin.y) * 0.5F,
                            (target->origin.z + target->eye_origin.z) * 0.5F};
    const game::Vec3 delta = centre - eye;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (length <= 0.0F) {
      set_error(out, "heatmap: target " + std::to_string(view_target) + " is at the eye");
      return;
    }
    // The chart's coordinates: u = atan2(y, x), v = asin(z / |d|).
    view.x = std::atan2(delta.y, delta.x);
    view.y = std::asin(std::clamp(delta.z / length, -1.0F, 1.0F));
  }
  const int history = std::clamp(integer("history", 6), 0, 64);
  const bool include_all = flag("all").value_or(false);
  // The lag-comp window (`docs/re/lagcomp.md`): the rewind floor is measured from
  // the local `m_nTickBase`; records at or below it are outside the window.
  if (const Json* value = args.find("max_unlag"); value != nullptr && value->is_number()) {
    lag_max_unlag_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("interp"); value != nullptr && value->is_number()) {
    lag_interp_.store(static_cast<float>(value->as_number()));
  }
  const float max_unlag = lag_max_unlag_.load();
  const float interp = lag_interp_.load();
  const std::int32_t tick_base = local.tick_base;
  const std::int32_t floor_tick = aim::rewind_tick(tick_base, max_unlag, interp);

  // Every live enemy's current pose (read outside the lock — the memory walk is
  // slow and must not stall the render thread's sampling).
  std::vector<aim::TargetPose> poses;
  std::vector<const game::PlayerSnapshot*> targets;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || player.pawn == 0) {
      continue;
    }
    if (player.team != 2 && player.team != 3) {
      continue;
    }
    if (!include_all && player.team == local.team) {
      continue;  // enemies only unless `all`
    }
    std::vector<aim::Hitbox> hitboxes = aim::read_solver_hitboxes(player.pawn, weapon);
    if (hitboxes.empty()) {
      continue;
    }
    aim::TargetPose pose;
    pose.target = player.index;
    pose.record = -1;
    pose.hitboxes = std::move(hitboxes);
    poses.push_back(std::move(pose));
    targets.push_back(&player);
  }

  // The sampled lag records, newest first, up to `history` per enemy. The store
  // is already trimmed to the window; skip anything at or below the floor as a
  // guard, and age the records against the current tick base.
  std::vector<std::size_t> record_counts(targets.size(), 0);
  {
    const std::scoped_lock lock(records_mutex_);
    const std::int32_t newest = tick_base > 0 ? tick_base : records_.newest_tick();
    for (std::size_t i = 0; i < targets.size(); ++i) {
      const std::vector<aim::Record>& ring = records_.records(targets[i]->pawn);
      int taken = 0;
      for (auto it = ring.rbegin(); it != ring.rend() && taken < history; ++it) {
        if (tick_base > 0 && it->tick <= floor_tick) {
          continue;  // outside the lag-comp window
        }
        aim::TargetPose pose;
        pose.target = targets[i]->index;
        pose.record = it->tick;
        pose.age = static_cast<float>(newest - it->tick);
        pose.hitboxes = it->hitboxes;
        poses.push_back(std::move(pose));
        ++taken;
      }
      record_counts[i] = static_cast<std::size_t>(taken);
    }
  }

  aim::HeatmapOptions options;
  options.weapon = weapon;

  // The penetration model (docs/re/penetration.md): `pen` = "simple" keeps the
  // first-pass approximation, "engine" applies the engine's real per-surface loss.
  const bool engine_pen = [&]() -> bool {
    const Json* value = args.find("pen");
    return value != nullptr && value->is_string() && value->as_string() == "engine";
  }();
  options.penetration = engine_pen ? aim::PenetrationModel::engine : aim::PenetrationModel::simple;
  options.penetration_params.weapon_penetration = weapon.penetration;
  options.penetration_params.max_penetrations = std::clamp(integer("max_pen", 4), 0, 16);
  // The per-surface modifiers (docs/re/penetration.md). The payload now reads each
  // shape's real surface from the game (`occlusion_query.cpp`); these args override
  // it when passed, for a sweep or a fallback when the surface read fails.
  const bool have_pen_mod = args.find("pen_mod") != nullptr;
  const bool have_dmg_mod = args.find("dmg_mod") != nullptr;
  const bool have_material = args.find("material") != nullptr;
  const bool have_grate = args.find("grate") != nullptr;
  const float pen_mod = number("pen_mod", 1.0F);
  const float dmg_mod = number("dmg_mod", 1.0F);
  const int material = integer("material", -1);
  const bool grate = flag("grate").value_or(false);

  // Occlusion (docs/re/occlusion.md): enumerate the world solids between the eye
  // and each target, project them into the chart, and let `build_heatmap` split the
  // hitbox silhouettes and attenuate. No rays. Fault-guarded engine call.
  OcclusionQueryStats walls_stats;
  const bool walls = flag("walls").value_or(false);
  const bool walls_call = flag("walls_call").value_or(false);
  // Diagnostic (`docs/ragebot-perf.md` §2.1): `enum=box` forces the older single-AABB
  // enumeration, so the pyramid's occluder set can be diffed against it at one instant.
  if (const Json* mode = args.find("enum"); mode != nullptr && mode->is_string()) {
    const std::string_view name = mode->as_string();
    occlusion_enum_mode_.store(name == "box" ? 1 : (name == "both" ? 2 : 0));
  }
  occlusion_live_.store(walls);
  occlusion_invoke_.store(walls_call);
  std::vector<aim::Polygon>
      occluder_polygons;  // projected silhouettes, parallel to options.occlusion
  if (walls) {
    input_.refresh();  // install the CreateMove hook (the game-thread query site)
    // The solids were enumerated on the game thread; project the cached hulls.
    const std::scoped_lock lock(occlusion_mutex_);
    walls_stats = occlusion_stats_;
    for (const OcclusionHull& hull : occlusion_hulls_) {
      std::optional<aim::OccluderCell> cell;
      if (hull.kind == aim::SolidKind::sphere) {
        cell = aim::project_sphere_occluder(eye, basis, hull.a, hull.radius, hull.surface_modifier);
      } else if (hull.kind == aim::SolidKind::capsule) {
        cell = aim::project_capsule_occluder(eye, basis, hull.a, hull.b, hull.radius,
                                             hull.surface_modifier);
      } else if (hull.kind == aim::SolidKind::mesh) {
        // A collision mesh (the map's large/merged wall surfaces) is a triangle
        // soup; project its vertices as the silhouette and keep the triangles for
        // the exact ray test (`docs/re/bullet-trace.md`).
        cell = aim::project_mesh_occluder(eye, basis, hull.vertices, hull.triangles,
                                          hull.surface_modifier, hull.mesh_bvh);
      } else {
        cell = aim::project_occluder(eye, basis, hull.vertices, hull.planes, hull.surface_modifier);
      }
      if (cell) {
        // Clip to the view window (centred on the camera direction): near geometry
        // projects far outside it, and the render is a camera-centred view of the
        // absolute chart.
        cell->polygon = aim::clip_to_view(cell->polygon, view, extent);
        if (cell->polygon.empty()) {
          continue;  // entirely outside the view — it cannot occlude a chart cell
        }
        // Keep the projected silhouette; the JSON reports only the solids that end up
        // between the eye and a target (`map.used_occluders`, below), not every solid
        // the enumeration box caught.
        occluder_polygons.push_back(cell->polygon);
        // The per-surface penetration properties (docs/re/penetration.md): the shape's
        // real `CGameSurfaceProperties` read by `occlusion_query.cpp`, or the
        // `pen_mod`/`dmg_mod`/`material`/`grate` args when passed.
        if (have_pen_mod) {
          cell->surface_modifier = pen_mod;
        }
        cell->damage_modifier = have_dmg_mod ? dmg_mod : hull.damage_modifier;
        cell->material = have_material ? material : hull.material;
        cell->grate = have_grate ? grate : hull.grate;
        // Every enumerated solid is offered to the heat map; `build_heatmap` keeps
        // only those the shot to a given hitbox actually reaches *before* the hitbox
        // (`OccluderCell::ray_span`) — that per-cell depth test is what makes
        // "between the eye and the hitbox" exact, with no bounding-box heuristic.
        options.occlusion.push_back(std::move(*cell));
      }
    }
  }

  const aim::Heatmap map = aim::build_heatmap(eye, basis, extent, poses, options, view);

  // Report only the solids that are actually **between the eye and a target** — the
  // ones `build_heatmap` kept as candidates — so the render (and the JSON) do not show
  // solids behind or beside the enemies that the enumeration box merely enclosed.
  Json occluder_polys = Json::array();
  for (const aim::OccluderCell* used : map.used_occluders) {
    const auto index = static_cast<std::size_t>(used - options.occlusion.data());
    if (index >= occluder_polygons.size()) {
      continue;
    }
    Json poly = Json::array();
    for (const aim::Vec2& p : occluder_polygons[index].points) {
      Json point = Json::array();
      point.push_back(Json::number(static_cast<double>(p.x)));
      point.push_back(Json::number(static_cast<double>(p.y)));
      poly.push_back(std::move(point));
    }
    occluder_polys.push_back(std::move(poly));
  }

  const auto vec3_json = [](const game::Vec3& v) -> Json {
    Json item = Json::object();
    item.set("x", Json::number(static_cast<double>(v.x)));
    item.set("y", Json::number(static_cast<double>(v.y)));
    item.set("z", Json::number(static_cast<double>(v.z)));
    return item;
  };

  Json payload = Json::object();
  payload.set("valid", Json::boolean(true));
  payload.set("eye", vec3_json(eye));
  Json basis_json = Json::object();
  basis_json.set("forward", vec3_json(basis.forward));
  basis_json.set("right", vec3_json(basis.right));
  basis_json.set("up", vec3_json(basis.up));
  payload.set("basis", std::move(basis_json));
  payload.set("fov", Json::number(static_cast<double>(fov)));
  payload.set("extent", Json::number(static_cast<double>(extent)));
  // The camera direction in absolute chart coords (radians) — the render is centred on
  // it; the chart data itself is absolute (docs/ragebot.md).
  payload.set("view_yaw", Json::number(static_cast<double>(view.x)));
  payload.set("view_pitch", Json::number(static_cast<double>(view.y)));
  // The entity the view window is centred on (-1 = the camera).
  payload.set("view_target", Json::integer(view_target));
  Json weapon_json = Json::object();
  weapon_json.set("damage", Json::number(static_cast<double>(weapon.damage)));
  weapon_json.set("headshot", Json::number(static_cast<double>(weapon.headshot_multiplier)));
  weapon_json.set("range_modifier", Json::number(static_cast<double>(weapon.range_modifier)));
  weapon_json.set("penetration", Json::number(static_cast<double>(weapon.penetration)));
  payload.set("weapon", std::move(weapon_json));
  payload.set("sampling", Json::boolean(heatmap_sampling_.load()));
  payload.set("history", Json::integer(static_cast<std::int64_t>(history)));
  payload.set("capacity", Json::integer(static_cast<std::int64_t>(k_heatmap_capacity)));
  // The lag-comp window the records were trimmed to (`docs/re/lagcomp.md`).
  payload.set("tick_base", Json::integer(tick_base));
  payload.set("rewind_tick", Json::integer(floor_tick));
  payload.set("max_unlag", Json::number(static_cast<double>(max_unlag)));
  payload.set("interp", Json::number(static_cast<double>(interp)));
  payload.set("window_ticks", Json::integer(tick_base - floor_tick));

  // Occlusion enumeration report (docs/re/occlusion.md): whether the engine query
  // ran, how many solids came back, and how many made it into the chart.
  const auto hex = [](std::uintptr_t value) -> std::string {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
  };

  Json walls_json = Json::object();
  walls_json.set("enabled", Json::boolean(walls));
  walls_json.set("called", Json::boolean(walls_call));
  walls_json.set("ok", Json::boolean(walls_stats.ok));
  walls_json.set("faulted", Json::boolean(walls_stats.faulted));
  walls_json.set("query_complete", Json::boolean(walls_stats.complete));
  Json query_issues = Json::array();
  constexpr struct {
    OcclusionQueryIssue issue;
    const char* name;
  } k_query_issue_names[] = {
      {.issue = OcclusionQueryIssue::not_invoked, .name = "not-invoked"},
      {.issue = OcclusionQueryIssue::query_unavailable, .name = "query-unavailable"},
      {.issue = OcclusionQueryIssue::query_fault, .name = "query-fault"},
      {.issue = OcclusionQueryIssue::record_capacity, .name = "record-capacity"},
      {.issue = OcclusionQueryIssue::record_scan_limit, .name = "record-scan-limit"},
      {.issue = OcclusionQueryIssue::body_capacity, .name = "body-capacity"},
      {.issue = OcclusionQueryIssue::parser_failure, .name = "parser-failure"},
      {.issue = OcclusionQueryIssue::parser_limit, .name = "parser-limit"},
      {.issue = OcclusionQueryIssue::mesh_cluster_limit, .name = "mesh-cluster-limit"},
      {.issue = OcclusionQueryIssue::unsupported_compound, .name = "unsupported-compound"},
      {.issue = OcclusionQueryIssue::occluder_capacity, .name = "occluder-capacity"},
      {.issue = OcclusionQueryIssue::target_capacity, .name = "target-capacity"},
      {.issue = OcclusionQueryIssue::surface_metadata, .name = "surface-metadata"},
  };
  for (const auto& entry : k_query_issue_names) {
    const auto bit = static_cast<std::uint32_t>(entry.issue);
    if ((walls_stats.incomplete_reasons & bit) != 0) {
      query_issues.push_back(Json::string(entry.name));
    }
  }
  walls_json.set("incomplete_reasons", std::move(query_issues));
  walls_json.set("records", Json::integer(walls_stats.records));
  walls_json.set("bodies", Json::integer(walls_stats.bodies));
  walls_json.set("shapes", Json::integer(walls_stats.shapes));
  walls_json.set("hulls", Json::integer(walls_stats.hulls));
  Json type_counts = Json::array();
  for (const int type_count : walls_stats.type_counts) {
    type_counts.push_back(Json::integer(type_count));
  }
  walls_json.set("type_counts", std::move(type_counts));
  walls_json.set("iface", Json::string(hex(walls_stats.iface)));
  walls_json.set("world", Json::string(hex(walls_stats.world)));
  walls_json.set("filter", Json::string(hex(walls_stats.filter)));
  walls_json.set("filter_vtable", Json::string(hex(walls_stats.filter_vtable)));
  // The custom bullet filter (docs/re/bullet-trace.md): the mask actually handed to the
  // world query is `filter+8`, so report it (the engine static default carries ~0).
  walls_json.set("filter_custom", Json::boolean(walls_stats.filter_custom));
  walls_json.set("filter_mask", Json::integer(walls_stats.filter_mask));
  walls_json.set("filter_group", Json::integer(walls_stats.filter_group));
  walls_json.set("filter_flags", Json::integer(walls_stats.filter_flags));
  walls_json.set("mesh_clusters", Json::integer(walls_stats.mesh_clusters));
  // The enum self-check (`enum=both`): the AABB hull count, the pyramid hull count and
  // how many pyramid hulls the AABB missed. `enum_outside_box` must be 0 (the pyramid is
  // a subset of the box volume).
  walls_json.set("enum_box_kept", Json::integer(walls_stats.enum_box_kept));
  walls_json.set("enum_pyramid_kept", Json::integer(walls_stats.enum_pyramid_kept));
  walls_json.set("enum_outside_box", Json::integer(walls_stats.enum_outside_box));
  walls_json.set("enum_box_only_between", Json::integer(walls_stats.enum_box_only_between));
  walls_json.set("enum_targets", Json::integer(walls_stats.enum_targets));
  walls_json.set("enum_target_max_distance",
                 Json::number(static_cast<double>(walls_stats.enum_target_max_distance)));
  walls_json.set("occluders", Json::integer(static_cast<std::int64_t>(options.occlusion.size())));
  // The penetration model this chart was valued with (docs/re/penetration.md).
  walls_json.set("penetration_model", Json::string(engine_pen ? "engine" : "simple"));
  // This flag is only the downstream per-hitbox candidate-cap status; it does not mean
  // the engine query/parser returned the whole scene (see query_complete above).
  walls_json.set("occlusion_candidates_complete", Json::boolean(map.occlusion_candidates_complete));
  walls_json.set("max_penetrations", Json::integer(options.penetration_params.max_penetrations));
  walls_json.set("pen_mod", Json::number(static_cast<double>(pen_mod)));
  walls_json.set("dmg_mod", Json::number(static_cast<double>(dmg_mod)));
  // The per-shape surface read (docs/re/penetration.md): the resolved
  // SurfacePropertiesList array and the first shape's surface, so a live run can
  // confirm the read.
  walls_json.set("surface_array", Json::string(hex(walls_stats.surface_array)));
  walls_json.set("surface_index", Json::integer(walls_stats.surface_index));
  walls_json.set("surface_distance",
                 Json::number(static_cast<double>(walls_stats.surface_distance)));
  walls_json.set("surface_material", Json::integer(walls_stats.surface_material));
  walls_json.set("surface_hits", Json::integer(walls_stats.surface_hits));
  // RE probe (docs/re/occlusion.md): the record's `+0x50` material token, raw and
  // reinterpreted as a float, so a live run can settle whether it is a float modifier
  // or a pointer (a sane 0.2-1.0 float vs a denormal/pointer).
  walls_json.set("material_token", Json::string(hex(walls_stats.material_token)));
  {
    const auto low = static_cast<std::uint32_t>(walls_stats.material_token);
    float as_float = 0.0F;
    std::memcpy(&as_float, &low, sizeof(as_float));
    walls_json.set("material_float", Json::number(static_cast<double>(as_float)));
  }
  walls_json.set("material_records", Json::integer(walls_stats.material_records));
  // Compound probe (docs/re/vphysics2.md): the first type-4 shape's group counts.
  Json compound_json = Json::object();
  compound_json.set("address", Json::string(hex(walls_stats.compound)));
  compound_json.set("instances", Json::integer(walls_stats.compound_instances));
  compound_json.set("meshes", Json::integer(walls_stats.compound_meshes));
  compound_json.set("hulls", Json::integer(walls_stats.compound_hulls));
  compound_json.set("g100", Json::integer(walls_stats.compound_g100));
  compound_json.set("g110", Json::integer(walls_stats.compound_g110));
  compound_json.set("g120", Json::integer(walls_stats.compound_g120));
  walls_json.set("compound", std::move(compound_json));
  // Player-exclusion + world-cache diagnostics.
  walls_json.set("excluded_records", Json::integer(walls_stats.excluded_records));
  walls_json.set("world_cache_body", Json::string(hex(walls_stats.world_cache_body)));
  walls_json.set("world_cache_hulls", Json::integer(walls_stats.world_cache_hulls));
  walls_json.set("world_box_kept", Json::integer(walls_stats.world_box_kept));
  walls_json.set("world_box_rejected", Json::integer(walls_stats.world_box_rejected));
  walls_json.set("hulls_no_planes", Json::integer(walls_stats.hulls_no_planes));
  Json handles = Json::array();
  for (int i = 0; i < walls_stats.record_handle_count; ++i) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%x", walls_stats.record_handles[i]);
    handles.push_back(Json::string(buffer));
  }
  walls_json.set("record_handles", std::move(handles));
  walls_json.set("occluder_polygons", std::move(occluder_polys));
  payload.set("walls", std::move(walls_json));

  Json targets_json = Json::array();
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const game::PlayerSnapshot& player = *targets[i];
    Json item = Json::object();
    item.set("index", Json::integer(player.index));
    item.set("name", Json::string(player.name));
    item.set("team", Json::integer(player.team));
    item.set("health", Json::integer(player.health));
    item.set("distance", Json::number(static_cast<double>(player.distance)));
    item.set("origin", vec3_json(player.origin));
    item.set("records", Json::integer(static_cast<std::int64_t>(record_counts[i])));
    targets_json.push_back(std::move(item));
  }
  payload.set("targets", std::move(targets_json));

  Json cells = Json::array();
  for (const aim::HeatCell& cell : map.cells) {
    Json item = Json::object();
    Json poly = Json::array();
    for (const aim::Vec2& p : cell.polygon.points) {
      Json point = Json::array();
      point.push_back(Json::number(static_cast<double>(p.x)));
      point.push_back(Json::number(static_cast<double>(p.y)));
      poly.push_back(std::move(point));
    }
    item.set("poly", std::move(poly));
    item.set("value", Json::number(static_cast<double>(cell.value)));
    item.set("base_value", Json::number(static_cast<double>(cell.base_value)));
    item.set("target", Json::integer(cell.target));
    item.set("hitgroup", Json::integer(cell.hitgroup));
    item.set("record", Json::integer(cell.record));
    item.set("age", Json::number(static_cast<double>(cell.age)));
    cells.push_back(std::move(item));
  }
  payload.set("cell_count", Json::integer(static_cast<std::int64_t>(map.cells.size())));
  payload.set("cells", std::move(cells));
  set_ok(out, std::move(payload));
}

auto Service::start() noexcept -> bool {
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
  cs2::game::append_log("[cs2-payload] control: listening on " + std::string(name));
  return true;
}

void Service::stop() noexcept {
  running_.store(false, std::memory_order_relaxed);
  // The trigger must not leave the gun firing: release the shot before the
  // service goes away (`docs/ragebot.md` §"The trigger").
  trigger_.reset(input_);
  // Closing the pipe unblocks a thread parked in ConnectNamedPipe/ReadFile.
  channel_.close();
  if (thread_ != nullptr) {
    WaitForSingleObject(static_cast<HANDLE>(thread_), 2000);
    CloseHandle(static_cast<HANDLE>(thread_));
    thread_ = nullptr;
  }
}

#else

auto Service::start() noexcept -> bool {
  return false;
}
void Service::stop() noexcept {}

#endif

}  // namespace cs2::control
