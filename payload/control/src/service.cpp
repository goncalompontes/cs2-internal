#include "cs2/control/service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "cs2/aim/reader.hpp"
#include "cs2/aim/solver.hpp"
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

Service::~Service() {
  stop();
}

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
  } else if (command == "aim") {
    do_aim(arguments, out);
  } else if (command == "rage") {
    do_rage(arguments, out);
  } else if (command == "predict") {
    do_predict(arguments, out);
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
  const auto handle_subscribe = [&](std::string_view name) {
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
  std::int64_t limit = static_cast<std::int64_t>(EventRing::k_capacity);
  if (const Json* arg = args.find("limit"); arg != nullptr && arg->is_number()) {
    limit = arg->as_int();
  }
  if (limit < 0) {
    limit = 0;
  }
  Json subscriptions = Json::object();
  subscriptions.set("accepted", std::move(accepted));
  subscriptions.set("rejected", std::move(rejected));
  Json result = Json::object();
  result.set("count", Json::integer(static_cast<std::int64_t>(events_.count())));
  result.set("subscribe", std::move(subscriptions));
  result.set("events", events_.recent(static_cast<std::size_t>(limit)));
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
    arg_matches.push_back(EventArgMatch{arg_storage[i], arg_storage[i + 1]});
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

auto Service::input_status() noexcept -> Json {
  const auto hex = [](std::uint64_t value) {
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
  result.set("strafe_writes", Json::number(static_cast<double>(input_.strafe_writes())));
  result.set("strafe_raw_writes",
             Json::number(static_cast<double>(input_.strafe_raw_writes())));
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
  result.set("bhop_holding_ticks",
             Json::number(static_cast<double>(input_.bhop_holding_ticks())));
  result.set("bhop_air_ticks", Json::number(static_cast<double>(input_.bhop_air_ticks())));
  result.set("input_local_pawn", Json::number(static_cast<double>(input_.local_pawn())));
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
  const auto move_json = [](const MoveSample& sample) {
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

void Service::set_movement_features(bool auto_strafe, bool bhop) noexcept {
  input_.set_strafe(auto_strafe);
  input_.set_bhop(bhop);
}

void Service::set_rage_features(bool on, int key, bool fire) noexcept {
  rage_enabled_.store(on);
  rage_key_.store(key);
  rage_fire_.store(fire);
}

// The hitgroup / hitbox id of the box whose centre is angularly closest to
// `direction` — a diagnostic (which hitbox the solve effectively picked).
auto rage_hitbox_for(const aim::SolveInput& solve_input, const game::Vec3& direction) -> int {
  int best = -1;
  float best_dot = -2.0F;
  for (const aim::Hitbox& hitbox : solve_input.hitboxes) {
    const game::Vec3 center = hitbox.is_capsule
                                  ? (hitbox.capsule.a + hitbox.capsule.b) * 0.5F
                                  : hitbox.box.center;
    const game::Vec3 to_center = game::normalized(center - solve_input.eye);
    const float d = game::dot(direction, to_center);
    if (d > best_dot) {
      best_dot = d;
      best = hitbox.group;
    }
  }
  return best;
}

void Service::on_snapshot(const game::Snapshot& snapshot) noexcept {
  const bool forced = rage_force_.load();
  if (!rage_enabled_.load() && !forced) {
    input_.clear_rage_aim();
    return;
  }
  // The hook is what runs `apply()` per tick; install it on first use so the
  // ragebot works during menu-only play (no scenario attached).
  input_.refresh();
  const int key = rage_key_.load();
  if (!forced && (key <= 0 || (GetAsyncKeyState(key) & 0x8000) == 0)) {
    input_.clear_rage_aim();
    return;
  }
  if (!snapshot.valid || !snapshot.local.has_value() || !snapshot.local->alive) {
    input_.clear_rage_aim();
    return;
  }

  aim::WeaponProfile weapon;
  weapon.damage = rage_damage_.load();
  weapon.range_modifier = rage_range_modifier_.load();
  weapon.headshot_multiplier = rage_headshot_.load();

  auto solve_input = aim::build_input(snapshot, weapon, -1);
  if (!solve_input.has_value()) {
    input_.clear_rage_aim();
    return;
  }
  solve_input->pose_sigma = rage_sigma_.load();
  switch (rage_objective_.load()) {
    case 0:
      solve_input->objective = aim::AimObjective::damage;
      break;
    case 2:
      solve_input->objective = aim::AimObjective::expected;
      break;
    default:
      solve_input->objective = aim::AimObjective::hitchance;
      break;
  }

  const aim::SolveResult result = aim::solve(*solve_input);
  if (!result.valid) {
    input_.clear_rage_aim();
    return;
  }

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

  input_.set_rage_aim(pitch, yaw, target_index, hitbox);
  // The menu owns `fire` in normal play; the forced (test) path bypasses the key
  // and therefore also fires, since the menu's per-frame push clears `fire`.
  input_.set_rage_fire(rage_fire_.load() || forced);
  rage_last_target_.store(target_index);
  rage_solves_.fetch_add(1);
}

auto Service::read_hitboxes(std::uintptr_t pawn, game::HitboxCapsule* out, int max) const noexcept
    -> int {
  if (out == nullptr || max <= 0 || pawn == 0) {
    return 0;
  }
  int count = 0;
  for (const aim::ModelHitbox& box : aim::read_model_hitboxes(pawn)) {
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
  std::snprintf(out, size, "rage: %s target %d box %d w%llu s%llu fire %d",
                input_.rage_active() ? "AIM" : "hold", target, input_.rage_hitbox(),
                static_cast<unsigned long long>(input_.rage_writes()),
                static_cast<unsigned long long>(input_.rage_shots()),
                input_.rage_fire() ? 1 : 0);
  return true;
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
  const bool explicit_weapon = args.find("damage") != nullptr ||
                               args.find("range_modifier") != nullptr ||
                               args.find("penetration") != nullptr ||
                               args.find("headshot") != nullptr;

  auto input = aim::build_input(snapshot, weapon, target, !explicit_weapon);
  if (!input.has_value()) {
    set_error(out, "aim unavailable: no local player / live enemy");
    return;
  }
  input->pose_sigma = number("sigma", 0.0F);
  if (const Json* objective = args.find("objective");
      objective != nullptr && objective->is_string()) {
    const std::string_view name = objective->as_string();
    if (name == "hitchance") {
      input->objective = aim::AimObjective::hitchance;
    } else if (name == "expected") {
      input->objective = aim::AimObjective::expected;
    } else {
      input->objective = aim::AimObjective::damage;
    }
  }

  const aim::SolveResult result = aim::solve(*input);
  if (!result.valid) {
    set_error(out, "aim unavailable: no hittable target");
    return;
  }

  constexpr float k_rad_to_deg = 57.295779513082320876F;
  const float yaw = std::atan2(result.direction.y, result.direction.x) * k_rad_to_deg;
  const float pitch =
      -std::asin(std::clamp(result.direction.z, -1.0F, 1.0F)) * k_rad_to_deg;

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
  payload.set("cells", Json::number(static_cast<double>(result.cells)));
  payload.set("aim_u", Json::number(result.aim.x));
  payload.set("aim_v", Json::number(result.aim.y));
  payload.set("wrote_view", Json::boolean(write));

  // Raw model hitboxes (validation for the exact-hitbox path).
  Json hitboxes = Json::array();
  for (const aim::ModelHitbox& box : aim::read_model_hitboxes(input->pawn)) {
    Json item = Json::object();
    item.set("bone", Json::number(static_cast<double>(box.bone)));
    item.set("group", Json::number(static_cast<double>(box.group)));
    item.set("name", Json::string(std::string(box.name)));
    item.set("radius", Json::number(static_cast<double>(box.radius)));
    item.set("min_x", Json::number(box.mins.x));
    item.set("min_y", Json::number(box.mins.y));
    item.set("min_z", Json::number(box.mins.z));
    item.set("max_x", Json::number(box.maxs.x));
    item.set("max_y", Json::number(box.maxs.y));
    item.set("max_z", Json::number(box.maxs.z));
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
  payload.set("weapon_penetration",
              Json::number(static_cast<double>(input->weapon.penetration)));
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
  if (const Json* value = args.find("range_modifier");
      value != nullptr && value->is_number()) {
    rage_range_modifier_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("sigma"); value != nullptr && value->is_number()) {
    rage_sigma_.store(static_cast<float>(value->as_number()));
  }
  if (const Json* value = args.find("objective"); value != nullptr && value->is_string()) {
    const std::string_view name = value->as_string();
    rage_objective_.store(name == "damage" ? 0 : (name == "expected" ? 2 : 1));
  }
  input_.refresh();
  set_rage_features(on, key, fire);

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
