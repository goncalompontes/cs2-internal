#include "cs2/render/native_menu.hpp"
#include <imgui.h>
#include <windows.h>
#include <cstring>
#include "cs2/game/log.hpp"
#include "cs2/tls/bootstrap.hpp"

namespace cs2::render {
namespace {
namespace wire = ::cs2::overlay::wire;
// C ABI window callback, one native renderer per payload.
NativeMenu* active_menu = nullptr;
LRESULT CALLBACK menu_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  cs2::tls::ensure_thread_tls();
  auto* menu = active_menu;
  const auto previous =
      menu != nullptr ? reinterpret_cast<WNDPROC>(menu->previous_proc()) : nullptr;
  if (menu != nullptr && message == WM_KILLFOCUS) menu->note_focus_lost();
  if (menu != nullptr && menu->open()) {
    if (message == WM_MOUSEWHEEL) {
      menu->add_wheel(GET_WHEEL_DELTA_WPARAM(wparam));
      return 0;
    }
    // The window thread never touches ImGui. Input is polled on the render
    // thread, avoiding concurrent context mutations and cross-thread locks.
    if (message == WM_INPUT) return DefWindowProcW(window, message, wparam, lparam);
    if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
        (message >= WM_KEYFIRST && message <= WM_KEYLAST))
      return 0;
  }
  return CallWindowProcW(previous != nullptr ? previous : DefWindowProcW, window, message, wparam,
                         lparam);
}
void toggle(const char* label, std::uint8_t& value) {
  bool on = value != 0;
  if (ImGui::Checkbox(label, &on)) {
    value = on ? 1 : 0;
  }
}

// Draws the menu; returns false when the user closed it with the window button.
auto draw_window(wire::Settings& settings, const game::RageDebugSnapshot& debug) -> bool {
  bool open = true;
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("cs2-internal", &open)) {
    if (ImGui::BeginTabBar("##tabs")) {
      if (ImGui::BeginTabItem("ESP")) {
        toggle("Box", settings.box);
        toggle("Health bar", settings.health_bar);
        toggle("Ammo bar", settings.ammo_bar);
        ImGui::Separator();
        toggle("Name", settings.name);
        toggle("Weapon", settings.weapon);
        toggle("Flags", settings.flags);
        ImGui::Separator();
        toggle("Only enemies", settings.only_enemies);
        toggle("Show dead", settings.show_dead);
        ImGui::Separator();
        toggle("Hitboxes (studio)", settings.hitboxes);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Interface")) {
        toggle("Debug HUD window", settings.show_hud);
        ImGui::TextDisabled("Insert or Delete toggles this menu");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Movement")) {
        toggle("Auto-strafe (directional subtick)", settings.auto_strafe);
        ImGui::TextDisabled("Paired analog gain when unquantized; yaw fallback otherwise.");
        toggle("Bunnyhop (NL pulses)", settings.bhop);
        ImGui::Separator();
        ImGui::SliderFloat("Strafe subticks", &settings.movement_subticks, 1, 30, "%.0f");
        ImGui::SliderFloat("Steering strength", &settings.movement_speed_gain, 0, 100, "%.0f%%");
        ImGui::TextDisabled("Hold jump for bhop. Aiming and camera stay independent.");
        ImGui::TextWrapped("State: %s", game::movement_debug_reason(debug.movement.reason));
        ImGui::Text("Steps %d | speed %.1f | strafe writes %llu", debug.movement.steps,
                    debug.movement.speed, debug.movement.strafe_writes);
        ImGui::Text("Mode %s | modeled end sideways speed %.4f",
                    debug.movement.paired_analog ? "paired analog" : "yaw/native",
                    debug.movement.terminal_lateral);
        ImGui::Text("Opposing wish (braking only) %.2f%% | autostop %s (path %d)",
                    debug.movement.counter_wish * 100,
                    debug.movement.autostop_applied ? "applied" : "off",
                    debug.movement.autostop_path);
        ImGui::Text("Landing sweep: %s | calls %llu | contacts %llu",
                    debug.movement.trace_ready ? "bound" : "unavailable",
                    debug.movement.trace_calls, debug.movement.trace_hits);
        ImGui::TextDisabled("Hop mode: 3 ground, 4 record, 5 cooldown, 6 predicted contact");
        ImGui::Text("Hop mode %d | when %.4f | writes %llu", debug.movement.hop_mode,
                    debug.movement.when, debug.movement.hop_writes);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Ragebot")) {
        toggle("Ragebot", settings.rage);
        toggle("Auto fire", settings.rage_fire);
        toggle("Command choke (4, recovery + stationary)", settings.recovery_choke);
        toggle("Ragebot debug indicators", settings.rage_debug);
        ImGui::SliderFloat("Minimum damage", &settings.rage_minimum_damage, 1.0F, 130.0F, "%.0f");
        ImGui::SliderFloat("Hit chance", &settings.rage_hitchance, 0.0F, 100.0F, "%.0f%%");
        ImGui::Separator();
        ImGui::TextDisabled("Ragebot evaluates each command while enabled.");
        ImGui::TextDisabled("Auto fire is controlled by its own toggle.");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Camera")) {
        toggle("Third person", settings.third_person);
        ImGui::SliderFloat("Distance", &settings.third_person_distance, 35.0F, 200.0F, "%.0f");
        ImGui::SliderFloat("Collision hull", &settings.third_person_hull, 0.0F, 20.0F, "%.0f");
        ImGui::Separator();
        toggle("Remove scope overlay", settings.remove_scope);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("About")) {
        ImGui::TextWrapped(
            "cs2-internal overlay. ESP labels are outlined; the player name is not.");
        ImGui::TextDisabled("font: Dear ImGui default, antialiasing off");
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
  return open;
}

}  // namespace

void NativeMenu::configure(wire::Settings defaults, SettingsSink sink, void* context) noexcept {
  settings_ = defaults;
  sink_ = sink;
  sink_context_ = context;
}

bool NativeMenu::initialize(void* window) noexcept {
  if (window == nullptr || IsWindow(static_cast<HWND>(window)) == FALSE) return false;
  if (window_ == window && previous_proc_ != nullptr) return true;
  shutdown();
  window_ = window;
  active_menu = this;
  SetLastError(0);
  previous_proc_ = reinterpret_cast<void*>(SetWindowLongPtrW(
      static_cast<HWND>(window), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&menu_window_proc)));
  if (previous_proc_ == nullptr) {
    active_menu = nullptr;
    window_ = nullptr;
    game::append_log("[native-dx11] window input hook failed");
    return false;
  }
  game::append_log("[native-dx11] window input hook ready; Insert/Delete opens menu");
  return true;
}

void NativeMenu::draw_rage_debug() noexcept {
  if (!settings_.rage_debug || !debug_provider_) return;
  if (!debug_frozen_) {
    game::RageDebugSnapshot next;
    if (debug_provider_(debug_context_, next)) debug_snapshot_ = next;
  }
  const auto& d = debug_snapshot_;
  using Gate = game::RageDebugGate;
  const ImVec4 green{.35F, .95F, .55F, 1}, red{1, .38F, .35F, 1}, amber{1, .78F, .3F, 1},
      grey{.62F, .65F, .7F, 1};
  const auto now = GetTickCount64();
  const auto age = d.observer_ms && now >= d.observer_ms ? now - d.observer_ms : 0;
  const auto snapshot_age = d.captured_ms && now >= d.captured_ms ? now - d.captured_ms : 0;
  ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetIO().DisplaySize.x > 550 ? ImGui::GetIO().DisplaySize.x - 540 : 12, 40),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(525, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(460, 160),
      ImVec2(680, ImGui::GetIO().DisplaySize.y > 260 ? ImGui::GetIO().DisplaySize.y - 60 : 200));
  const auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                     (open_.load() ? 0 : ImGuiWindowFlags_NoInputs);
  if (ImGui::Begin("Ragebot diagnostics", nullptr, flags)) {
    if (!d.enabled)
      ImGui::TextColored(grey, "RAGEBOT OFF");
    else if (!d.hook)
      ImGui::TextColored(red, "INPUT HOOK NOT INSTALLED");
    else if (!d.observer_ms)
      ImGui::TextColored(amber, "WAITING FOR COMMAND CALLBACK");
    else if (!debug_frozen_ && age > 500)
      ImGui::TextColored(red, "COMMAND HEARTBEAT STALE (%llu ms)", age);
    else {
      bool blocked = false;
      for (std::size_t i = 0; i < d.gates.size(); ++i)
        if (d.gates[i] == Gate::fail) {
          ImGui::TextColored(red, "BLOCKED: %s",
                             game::rage_debug_stage_name(static_cast<game::RageDebugStage>(i)));
          blocked = true;
          break;
        }
      if (!blocked)
        ImGui::TextColored(d.auto_fire ? green : amber,
                           d.auto_fire ? "PIPELINE RUNNING" : "AUTO FIRE OFF / MANUAL TRIGGER");
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 490);
    ImGui::TextUnformatted(d.detail);
    ImGui::PopTextWrapPos();
    ImGui::Text("Command %d | heartbeat %llu ms | snapshot %llu ms%s", d.command, age, snapshot_age,
                debug_frozen_ ? " [FROZEN]" : "");
    ImGui::Text("Weapon %s | ammo %d (snapshot %d) | reload %s",
                d.weapon_name[0] ? d.weapon_name : "?", d.clip, d.snapshot_clip,
                d.reload ? "yes" : "no");
    ImGui::Text("Tick: client %d / base %d / next shot %d", d.client_tick, d.tick_base,
                d.next_primary);
    ImGui::Separator();
    if (ImGui::BeginTable("rage-gates", 2,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch, 3);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1);
      for (std::size_t i = 0; i < d.gates.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(game::rage_debug_stage_name(static_cast<game::RageDebugStage>(i)));
        ImGui::TableNextColumn();
        const auto state = d.gates[i];
        ImGui::TextColored(state == Gate::pass      ? green
                           : state == Gate::fail    ? red
                           : state == Gate::waiting ? amber
                                                    : grey,
                           "%s", game::rage_debug_gate_name(state));
      }
      ImGui::EndTable();
    }
    ImGui::Text("Targets %d/%d enemies | proposals %d | checked %d%s", d.targets, d.enemies,
                d.proposals, d.examined, d.fallback ? " [centers]" : "");
    ImGui::Text("Damage: nominal %.1f / expected %.1f / need %.1f", d.nominal_damage,
                d.expected_damage, d.minimum_damage);
    ImGui::Text("Damage probability %.1f%% / need %.1f%% | world %s", d.probability * 100,
                d.required_probability * 100, d.world_known ? "known" : "unknown");
    ImGui::Text("Aim ready %s | stop %s | trigger: %s", d.aim_ready ? "yes" : "no",
                d.stop_requested ? "yes" : "no", d.trigger);
    ImGui::Text("Shot requests %llu | observed ammo used %llu", d.shot_requests, d.ammo_decreases);
    const auto& c = d.choke;
    ImGui::Text("Recovery choke %s | binding %s | held %d/4", c.enabled ? "on" : "off",
                c.bound ? "ready" : "unavailable", c.held);
    ImGui::Text("Deferred commands %llu | release attempts %llu | penalty %.6f",
                c.deferred_commands, c.release_attempts, c.penalty);
    ImGui::Text("Serialized range %d..%d | server executed %d", c.range_first, c.range_last,
                c.server_executed);
    ImGui::Text("Choke horizontal speed %.2f (stationary <= 1.0)", c.horizontal_speed);
    ImGui::Text("Last release cmd %d: serialized %d..%d", c.release_command, c.release_first,
                c.release_last);
    if (ImGui::CollapsingHeader("Source details / rejection counts")) {
      ImGui::Text("Type %d | item %d | pellets %d | kernel %d", d.source.type, d.source.item,
                  d.source.pellets, d.source.samples);
      ImGui::Text("Spread %.6f | inaccuracy %.6f | recoil %.3f", d.source.spread_value,
                  d.source.inaccuracy, d.source.recoil);
      ImGui::Text("Local eye  %.2f %.2f %.2f", d.source.local_eye.x, d.source.local_eye.y,
                  d.source.local_eye.z);
      ImGui::Text("Return eye %.2f %.2f %.2f | delta %.2f", d.source.returned_eye.x,
                  d.source.returned_eye.y, d.source.returned_eye.z, d.source.eye_distance);
      ImGui::Text("Eye slot %d | function 0x%llx | in client %d | call ok %d", d.source.eye_slot,
                  static_cast<unsigned long long>(d.source.eye_function),
                  d.source.eye_function_in_client, d.source.eye_call);
      ImGui::Text("Services 0x%llx | history eye %d",
                  static_cast<unsigned long long>(d.source.services), d.source.history_eye);
      ImGui::Text("Reject: immunity/data %d / age %d / model %d / changed %d", d.rejected_immunity,
                  d.rejected_time, d.rejected_model, d.rejected_changed);
      ImGui::Text("Commands: hook %llu / observer %llu / unique %llu / duplicate %llu", d.calls5,
                  d.callbacks, d.unique_commands, d.duplicates);
      ImGui::Text("Worker submitted %llu / completed %llu / last %.2f ms", d.submitted, d.completed,
                  d.worker_us / 1000.0);
      ImGui::Text("Serialization OK %llu / failed %llu | view restores %llu", d.crc_writes,
                  d.crc_failures, d.camera_restores);
    }
    if (open_.load()) {
      ImGui::Checkbox("Freeze debug snapshot", &debug_frozen_);
      ImGui::SameLine();
      if (ImGui::Button("Copy diagnostics"))
        ImGui::SetClipboardText(game::format_rage_debug(d).c_str());
    } else
      ImGui::TextDisabled("Insert: freeze / copy / move panel");
  }
  ImGui::End();
}

void NativeMenu::frame() noexcept {
  const auto before = settings_;
  auto& io = ImGui::GetIO();
  const bool foreground = window_ != nullptr && GetForegroundWindow() == window_;
  const bool toggle_down = foreground && ((GetAsyncKeyState(VK_INSERT) & 0x8000) ||
                                          (GetAsyncKeyState(VK_DELETE) & 0x8000));
  if (toggle_down && !previous_toggle_) {
    open_.store(!open_.load());
    game::append_log(open_.load() ? "[native-dx11] menu opened" : "[native-dx11] menu closed");
  }
  previous_toggle_ = toggle_down;
  if (!foreground) open_.store(false);
  if (open_.load()) {
    ClipCursor(nullptr);
    if (GetCapture() == window_) ReleaseCapture();
    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(static_cast<HWND>(window_), &cursor))
      io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    const int keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    for (int i = 0; i < 3; ++i) {
      const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
      io.AddMouseButtonEvent(i, down);
      mouse_[i] = down;
    }
    io.AddMouseWheelEvent(0, static_cast<float>(wheel_.exchange(0)) / WHEEL_DELTA);
  } else {
    for (int i = 0; i < 3; ++i) {
      if (mouse_[i]) io.AddMouseButtonEvent(i, false);
      mouse_[i] = false;
    }
    wheel_.store(0);
  }
  io.MouseDrawCursor = open_.load();
  if (open_.load() && !draw_window(settings_, debug_snapshot_)) open_.store(false);
  draw_rage_debug();
  settings_.menu_open = open_.load() ? 1 : 0;
  if (sink_ != nullptr && std::memcmp(&before, &settings_, sizeof(settings_)) != 0)
    sink_(sink_context_, settings_);
}

void NativeMenu::shutdown() noexcept {
  if (window_ != nullptr && previous_proc_ != nullptr && IsWindow(static_cast<HWND>(window_)) &&
      GetWindowLongPtrW(static_cast<HWND>(window_), GWLP_WNDPROC) ==
          reinterpret_cast<LONG_PTR>(&menu_window_proc)) {
    SetWindowLongPtrW(static_cast<HWND>(window_), GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(previous_proc_));
  }
  if (active_menu == this) active_menu = nullptr;
  window_ = nullptr;
  previous_proc_ = nullptr;
  open_.store(false);
}
}  // namespace cs2::render
