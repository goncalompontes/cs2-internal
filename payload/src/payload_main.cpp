// payload_main.cpp — payload DLL entry.
//
// Boots the payload: interposes the DXGI swapchain (ADR-0015) and drives the
// ImGui debug HUD, which reads local-player/player state via `payload/game`.
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "cs2/control/service.hpp"
#include "cs2/diag/crash_handler.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/features/chams.hpp"
#include "cs2/features/config_store.hpp"
#include "cs2/features/esp.hpp"
#include "cs2/features/game_present_hook.hpp"
#include "cs2/features/menu.hpp"
#include "cs2/features/model_publisher.hpp"
#include "cs2/features/overlay.hpp"
#include "cs2/features/present_hook.hpp"
#include "cs2/features/settings.hpp"
#include "cs2/features/thirdperson.hpp"
#include "cs2/game/log.hpp"
#include "cs2/overlay/wire.hpp"
#include "cs2/render/imgui_backend.hpp"
#include "cs2/tls/bootstrap.hpp"

#if __has_include("interfaces.hpp")
#include "interfaces.hpp"
#define CS2_PAYLOAD_HAVE_INTERFACES 1
#endif

#if __has_include("schemas.hpp")
#include "schemas.hpp"
#define CS2_PAYLOAD_HAVE_SCHEMAS 1
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::payload {
namespace {

// The live feature settings block (ADR-0021) as the control channel sees it.
//
// The block is **owned** elsewhere: the in-process overlay menu owns it, and
// in the Vulkan-layer mode the shared segment does (the layer's menu writes
// it, the payload publishes it). The owner publishes the block every frame
// through the settings sink, so the control thread reads that copy — it never
// touches the shared segment's seqlock (the publisher owns that) and never
// reads the menu's block while the menu is writing it.
struct LiveSettings {
  mutable std::mutex mutex;
  cs2::overlay::wire::Settings block{};
  bool valid = false;

  // The owner published the block it owns.
  void update(const cs2::overlay::wire::Settings& in) noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    block = in;
    valid = true;
  }

  void read(cs2::overlay::wire::Settings& out) const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    out = block;
  }

  [[nodiscard]] bool published() const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    return valid;
  }
};

// Lifetime owner for the payload's subsystems. A namespace-scope global (not a
// function-local static) so it is constructed during CRT startup on the
// injector's entry thread: the static-init guard would otherwise run on the
// hook-install thread, whose static-CRT per-thread state is not initialized for
// a manually mapped module.
struct Runtime {
  render::ImGuiBackend backend;
  features::Overlay overlay;
  features::Menu menu{overlay};
  features::ModelPublisher publisher{overlay};
  features::GamePresentHook game_present{publisher};
  features::PresentHook present{backend, overlay, &menu};
  // The third-person camera: the engine's own camera, driven by the
  // operator's parameters.
  features::ThirdPerson thirdperson;
  // The 3D chams: the player models are drawn with one of the game's own flat
  // materials. The hook is installed lazily (the scene system only exists
  // once the game is up) from the per-frame snapshot sink below.
  features::Chams chams;
  control::Service control;
  // The payload's own copy of the settings, so a config **load** has somewhere to
  // land: the menu owns the live block, the file owns the persisted one, and the
  // op protocol moves between them.
  features::Config config;
  // The one-shot config op's bookkeeping: the last token the payload acted on.
  // It belongs to the runtime, not to a file-scope static (ADR-0010).
  features::ConfigOpState config_op;
  // The live settings block the control channel reads and writes.
  LiveSettings live_settings;
  // Whether the shared segment (the Vulkan layer) owns the settings block, or
  // the in-process menu does. The render mode decides it, and the render mode
  // can still fall back from the in-process renderer to the layer.
  std::atomic<bool> layer_owns_settings{false};
};

Runtime g_runtime;

auto runtime() -> Runtime& {
  return g_runtime;
}

#if defined(_WIN32)
// Which renderer the payload drives.
enum class RenderMode {
  present,       // in-process D3D11/DXGI Present + the payload's own menu (default)
  layer,         // external Vulkan layer: publish the model on the game's present
  layer_thread,  // external Vulkan layer via a dedicated publish thread (Wine-safe)
};

// `CS2_PAYLOAD_RENDER` picks explicitly. Otherwise the graphics stack decides: a
// Vulkan/DXVK stack (a loaded `vulkan-1.dll`) uses the external layer, and
// everything else (native D3D11) renders in-process. The game inherits its
// environment from Steam, so the env is only a hint — the module check is the
// reliable signal. `CS2_PAYLOAD_PUB_THREAD` selects the Wine-safe layer variant.
auto render_mode() noexcept -> RenderMode {
  if (const char* mode = std::getenv("CS2_PAYLOAD_RENDER"); mode != nullptr) {
    if (std::string(mode) == "present") {
      return RenderMode::present;
    }
    if (std::string(mode) == "layer") {
      return RenderMode::layer;
    }
    if (std::string(mode) == "layer-thread") {
      return RenderMode::layer_thread;
    }
  }
  if (GetModuleHandleW(L"vulkan-1.dll") != nullptr) {
    return std::getenv("CS2_PAYLOAD_PUB_THREAD") != nullptr ? RenderMode::layer_thread
                                                            : RenderMode::layer;
  }
  return RenderMode::present;
}
#endif

// `CS2_PAYLOAD_MOVEMENT` — the payload's own switch for the movement features
// (`docs/re/subtick.md` §"The movement yaw channel"). Comma-separated names:
// `strafe` (silent airborne auto-strafe) and `bhop` (auto-jump while grounded).
// They become the *defaults* published to the overlay menu, so the menu can change
// them later; an empty/unset value leaves both off.
struct MovementConfig {
  bool strafe = false;
  bool bhop = false;
};

auto parse_movement_env() noexcept -> MovementConfig {
  MovementConfig config;
  const char* value = std::getenv("CS2_PAYLOAD_MOVEMENT");
  if (value == nullptr) {
    return config;
  }
  const std::string names(value);
  config.strafe = names.find("strafe") != std::string::npos;
  config.bhop = names.find("bhop") != std::string::npos;
  return config;
}

// Split a comma-separated token list (trimming spaces and empty tokens).
auto split_tokens(const std::string& value) noexcept -> std::vector<std::string> {
  std::vector<std::string> tokens;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find(',', start);
    std::string token =
        value.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
      token.erase(token.begin());
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.pop_back();
    }
    if (!token.empty()) {
      tokens.push_back(std::move(token));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return tokens;
}

// `CS2_PAYLOAD_RAGE` — the payload's own switch for the ragebot keybind
// (`docs/ragebot.md`). Comma-separated tokens: `on` (enable), `fire` (auto-fire),
// `key=<vk>` (a decimal virtual-key code; the default is `X`). These become the
// *defaults* published to the overlay menu, which then owns the feature.
struct RageConfig {
  bool enabled = false;
  bool fire = false;
  int key = 0x58;  // VK_X
};

auto parse_rage_env() noexcept -> RageConfig {
  RageConfig config;
  const char* value = std::getenv("CS2_PAYLOAD_RAGE");
  if (value == nullptr) {
    return config;
  }
  for (const std::string& token : split_tokens(std::string(value))) {
    if (token == "on") {
      config.enabled = true;
    } else if (token == "fire") {
      config.fire = true;
    } else if (token.starts_with("key=")) {
      config.key = std::atoi(token.c_str() + 4);
    } else {
      const int key = std::atoi(token.c_str());
      if (key > 0) {
        config.key = key;
      }
    }
  }
  return config;
}

// The overlay menu's `Movement`/`Ragebot` tabs write the shared settings; the
// publisher calls this on every published frame, and the payload's features follow (so
// the menu is the single owner).
void on_movement_settings(void* context, const cs2::overlay::wire::Settings& settings) noexcept {
  auto* runtime = static_cast<Runtime*>(context);
  // Work on a copy: in the in-process path `settings` **is** the menu's live
  // block, and the write-back below assigns to it — the `const&` in the sink's
  // contract would be a lie.
  cs2::overlay::wire::Settings live = settings;
  // The owner published the block it owns: keep the control channel's copy in
  // step, so the `settings` command reads the same block the menu owns.
  runtime->live_settings.update(live);
  runtime->control.set_movement_features(live.auto_strafe != 0, live.bhop != 0);
  // The auto-strafe's own knobs: how the goal heading is derived (0 = off,
  // 1 = the camera's forward/back, 2 = the player's W/A/S/D), whether a held
  // `A`/`D` stands the feature down, the analog's wish speed (0 = keep the
  // current one) and the number of sub-tick `yaw_delta` steps (0 = the
  // camera-frame analog).
  runtime->control.set_strafe_features(
      live.movement_strafe_mode, live.movement_strafe_ignore_keys != 0,
      live.movement_strafe_wishspeed, live.movement_strafe_subticks);
  runtime->control.set_rage_features(
      live.rage != 0, live.rage_key, live.rage_fire != 0, live.rage_objective, live.rage_hitbox,
      live.rage_fov, live.rage_min_damage, live.rage_min_hitchance, live.rage_autostop,
      live.rage_autoscope, live.rage_autoreload != 0, live.rage_only_visible != 0, live.rage_mode,
      live.rage_per_weapon != 0, live.rage_weapon, live.rage_config_op, live.rage_config_token,
      live.rage_recoil != 0, live.rage_stop_speed, live.rage_backtrack != 0);

  // Publish where the settings file lives so the menu can show it (a read-only
  // display field). The path is only written when it actually changes, so the
  // publication never disturbs the menu's in-flight edits.
  if (cs2::features::publish_config_path(live)) {
    std::memcpy(runtime->menu.settings().menu_config_path, live.menu_config_path,
                sizeof(live.menu_config_path));
#if defined(_WIN32)
    // The Vulkan layer's menu reads the shared segment.
    runtime->publisher.publish_settings_block(live);
#endif
  }

  // The third-person camera. The engine supports the switch and the distance;
  // the yaw offset is damped by the engine's smoothing, and the pivot, the
  // shoulder and the field of view have no engine counterpart (the client has
  // no shoulder camera and the view setup's FOV field is unconfirmed).
  runtime->thirdperson.configure({
      .enabled = settings.movement_thirdperson != 0,
      .distance = settings.movement_thirdperson_distance,
      .angle = settings.movement_thirdperson_angle,
      .pivot_z = settings.movement_thirdperson_pivot_z,
      .fov = settings.movement_thirdperson_fov,
      .right_shoulder = settings.movement_thirdperson_side != 0,
  });

  // The ESP's visibility toggles. The control service answers the ESP's
  // per-player visibility question, and the world-occlusion query behind that
  // answer only runs while one of the two is on.
  runtime->control.set_esp_features(settings.esp_glow != 0, settings.esp_visible_only != 0);

  // The 3D chams: the switch, the mode and the two per-player switches. The
  // colour is passed on too, but the material system's parameter API is not
  // resolved yet, so the flat material keeps its own colour (the chams'
  // `configure` says so).
  {
    features::Config::Visuals visuals;
    visuals.chams = settings.visuals_chams != 0;
    visuals.chams_mode = settings.visuals_chams_mode > 2
                             ? features::ChamsMode::flat
                             : static_cast<features::ChamsMode>(settings.visuals_chams_mode);
    visuals.chams_visible = settings.visuals_chams_visible != 0;
    visuals.chams_local = settings.visuals_chams_local != 0;
    visuals.chams_color = features::Color::from_wire(settings.visuals_chams_color);
    runtime->chams.configure(visuals);
  }

  // The menu's one-shot config op (save / load / reset the settings file). The op
  // runs **once per token** — the token bookkeeping lives in the store's state,
  // so the two callers of this sink (the menu and the publisher) cannot both
  // fire it.
  //
  // The op runs against the **live** block: the store rebuilds the payload's
  // configuration from it first, so a save persists the settings the user is
  // looking at and not the defaults. A load or a reset changes the
  // configuration, so the result has to reach the owner of the live block: the
  // in-process menu (`runtime->menu`) and the shared segment (the Vulkan
  // layer's menu, ADR-0016). Both get the same block. A save changes nothing,
  // so it does not touch the live block at all.
  const cs2::features::ConfigOpOutcome outcome =
      cs2::features::apply_pending_op(runtime->config, live, runtime->config_op);
  if (outcome.changed) {
    runtime->menu.settings() = outcome.block;
    runtime->overlay.set_esp_config(cs2::features::from_wire_settings(outcome.block));
    runtime->overlay.set_show_hud(outcome.block.show_hud != 0);
#if defined(_WIN32)
    runtime->publisher.publish_settings_block(outcome.block);
#endif
  }
}

// The per-frame overlay refresh forwards the local pawn so the movement features
// (strafe/bhop prediction) see it fresh during menu-only play.
void on_local_pawn(void* context, std::uintptr_t pawn) noexcept {
  static_cast<Runtime*>(context)->control.note_local_pawn(pawn);
}

// The control channel's read access to the live settings block (ADR-0021).
//
// The block belongs to its owner (the in-process menu, or the shared segment
// in the Vulkan-layer mode). The owner publishes it every frame through the
// settings sink, so the control thread reads that copy: a consistent snapshot,
// and no seqlock of the control channel's own.
void read_settings(void* context, cs2::overlay::wire::Settings& out) noexcept {
  static_cast<Runtime*>(context)->live_settings.read(out);
}

// The write access: it goes **through the owner**, so the owner's next frame
// does not overwrite the change (ADR-0021).
void write_settings(void* context, const cs2::overlay::wire::Settings& in) noexcept {
  auto* runtime = static_cast<Runtime*>(context);
  if (runtime->layer_owns_settings.load()) {
    // The Vulkan layer's menu owns the block in the shared segment; the
    // publisher writes it (the same seqlock the layer reads it with).
    runtime->publisher.publish_settings_block(in);
  } else {
    // The in-process menu owns the block; it hands it to the features on its
    // next frame.
    runtime->menu.settings() = in;
  }
  runtime->live_settings.update(in);
}

// Whether the owner has published the block yet (the control channel must not
// write a block it has never seen).
auto settings_published(void* context) noexcept -> bool {
  return static_cast<Runtime*>(context)->live_settings.published();
}

// The per-frame overlay refresh also forwards the full snapshot: the ragebot solve
// runs there and stashes the silent aim for the per-tick `Input::apply()`.
void on_snapshot(void* context, const cs2::game::Snapshot& snapshot) noexcept {
  auto* runtime = static_cast<Runtime*>(context);
  runtime->control.on_snapshot(snapshot);
  // The third-person camera's per-frame step: the console route, the distance
  // knob and the view angles the camera's yaw offset is applied to.
  cs2::game::Angle view{};
  cs2::game::Vec3 eye{};
  if (snapshot.local.has_value()) {
    view = snapshot.local->eye_angles;
    eye = snapshot.local->eye_origin;
  }
  runtime->thirdperson.update(view, eye);
  // The 3D chams: the per-frame step. It installs the draw hook (retrying
  // until the scene system is up), keeps the material in step with the mode
  // and refreshes the local player and the visibility filter.
  runtime->chams.update(snapshot);
}

// The overlay HUD asks for the ragebot's one-line status while it is enabled.
auto on_rage_status(void* context, char* out, std::size_t size) noexcept -> bool {
  return static_cast<Runtime*>(context)->control.rage_status(out, size);
}

// The overlay's hitbox visualiser asks the payload for a pawn's real studio
// hitbox capsules (`docs/re/aim.md`).
auto on_hitboxes(void* context, std::uintptr_t pawn, cs2::game::HitboxCapsule* out,
                 int max) noexcept -> int {
  return static_cast<Runtime*>(context)->control.read_hitboxes(pawn, out, max);
}

// The overlay highlights the ragebot's current target.
auto on_rage_target(void* context) noexcept -> int {
  return static_cast<Runtime*>(context)->control.rage_target_index();
}

// The overlay asks for the bhop prediction's position to draw the demo box.
auto on_predicted_origin(void* context, float out[3]) noexcept -> bool {
  return static_cast<Runtime*>(context)->control.predicted_origin(out);
}

// The ESP's sound provider: the control module's sound events, mapped onto
// the ESP's own event type.
//
// The control module cannot name the features module's provider types (its
// Meson build declares no dependency on the features module — and the
// features module cannot depend on the control module either, which is the
// cycle the provider registry exists to break). The two shapes are therefore
// adapted here, where both modules are visible.
auto on_sounds(void* context, cs2::features::SoundEvent* out, int max) -> int {
  if (out == nullptr || max <= 0) {
    return 0;
  }
  auto* service = static_cast<cs2::control::Service*>(context);
  cs2::control::SoundEvent events[32];
  const int count = service->recent_sounds(events, max < 32 ? max : 32);
  for (int i = 0; i < count; ++i) {
    out[i] = cs2::features::SoundEvent{.player_index = events[i].player_index,
                                       .radius = events[i].radius,
                                       .sequence = events[i].sequence};
  }
  return count;
}

// The ESP's visibility provider: the control service's cached per-player
// state, mapped onto the ESP's own enum. The state is `unknown` until the
// game thread has published one (and whenever it is stale), and the ESP then
// leaves the player alone.
auto on_visibility(void* context, int player_index) -> cs2::features::Visibility {
  const cs2::control::Visibility state =
      static_cast<cs2::control::Service*>(context)->visibility(player_index);
  switch (state) {
    case cs2::control::Visibility::visible:
      return cs2::features::Visibility::visible;
    case cs2::control::Visibility::hidden:
      return cs2::features::Visibility::hidden;
    case cs2::control::Visibility::unknown:
      break;
  }
  return cs2::features::Visibility::unknown;
}

}  // namespace

#if defined(_WIN32)
DWORD WINAPI install_thread(void*) {
  // Install off the loader lock: device creation loads DLLs and would deadlock
  // if done from DllMain. A new thread has no TLS block for our mapped image
  // until we install one (the loader bypasses manually-mapped modules).
  cs2::tls::ensure_thread_tls();
  if (std::getenv("CS2_PAYLOAD_NO_DIAG") == nullptr) {
    cs2::diag::install_watchdog();
  }
  cs2::payload::runtime().overlay.initialize();
  // Control channel (ADR-0020): the named-pipe server the scenario runner
  // connects to. Runs on its own thread and never touches the render path.
  if (std::getenv("CS2_PAYLOAD_NO_CONTROL") == nullptr) {
    (void)cs2::payload::runtime().control.start();
  }
  // Movement features: the payload's env sets the initial state and the defaults the
  // overlay menu starts from; the menu then owns them (ADR-0021 settings block).
  {
    const MovementConfig movement = parse_movement_env();
    const auto env_value = [](const char* name) -> std::string {
      const char* value = std::getenv(name);
      return value == nullptr ? std::string("<unset>") : std::string(value);
    };
    cs2::game::append_log(std::string("[cs2-payload] movement: strafe=") +
                          (movement.strafe ? "1" : "0") + " bhop=" + (movement.bhop ? "1" : "0") +
                          " | env: MOVEMENT=" + env_value("CS2_PAYLOAD_MOVEMENT") +
                          " PUB_THREAD=" + env_value("CS2_PAYLOAD_PUB_THREAD") +
                          " OVERLAY=" + env_value("CS2_OVERLAY") +
                          " OVERLAY_LOG=" + env_value("CS2_OVERLAY_LOG"));
    cs2::payload::runtime().control.set_movement_features(movement.strafe, movement.bhop);
    cs2::payload::runtime().publisher.set_movement_defaults(movement.strafe, movement.bhop);
    cs2::payload::runtime().menu.set_movement_defaults(movement.strafe, movement.bhop);
    const RageConfig rage = parse_rage_env();
    cs2::game::append_log(std::string("[cs2-payload] rage: enabled=") + (rage.enabled ? "1" : "0") +
                          " key=" + std::to_string(rage.key) + " fire=" + (rage.fire ? "1" : "0"));
    cs2::payload::runtime().control.set_rage_features(
        rage.enabled, rage.key, rage.fire, 1 /*hitchance*/, 0 /*auto*/, 0.0F, 0.0F, 0.0F, 0, 0,
        false, false, 0 /*hold*/, false, "", 0, 0, true /*recoil*/, 0.0F,
        false /*backtrack: off by default*/);
    cs2::payload::runtime().publisher.set_rage_defaults(rage.enabled, rage.key, rage.fire);
    cs2::payload::runtime().menu.set_rage_defaults(rage.enabled, rage.key, rage.fire);
    cs2::payload::runtime().publisher.set_settings_sink(&on_movement_settings,
                                                        &cs2::payload::runtime());
    cs2::payload::runtime().menu.set_settings_sink(&on_movement_settings,
                                                   &cs2::payload::runtime());
    // The control channel's access to the live settings block (ADR-0021): the
    // same block the menu owns, read and written through its owner.
    cs2::payload::runtime().control.set_settings_access(
        &read_settings, &write_settings, &settings_published, &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_local_pawn_sink(&on_local_pawn,
                                                        &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_snapshot_sink(&on_snapshot, &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_rage_status_provider(&on_rage_status,
                                                             &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_hitbox_provider(&on_hitboxes, &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_rage_target_provider(&on_rage_target,
                                                             &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_predicted_origin_provider(&on_predicted_origin,
                                                                  &cs2::payload::runtime());
  }
  // The ESP's sound and visibility providers: the data lives in the control
  // module, which the features module cannot depend on, so the features
  // module asks for it through these callbacks.
  cs2::features::set_esp_providers({
      .sounds = &on_sounds,
      .sounds_context = &cs2::payload::runtime().control,
      .visibility = &on_visibility,
      .visibility_context = &cs2::payload::runtime().control,
  });
  // The third-person camera: the game-thread hook (the client's per-frame
  // camera update) that re-asserts the switch every frame. It is refused on a
  // client build the offsets were not verified on; the console route then
  // carries the feature on its own.
  (void)cs2::payload::runtime().thirdperson.install();
  // Render path (workstream A): the ESP + menu render in-process from the game's
  // D3D11/DXGI Present by default; the external Vulkan layer (the shm publisher +
  // `game_present`) is the fallback for a Vulkan/DXVK stack.
  if (std::getenv("CS2_PAYLOAD_NO_PUBLISH") == nullptr) {
    RenderMode mode = render_mode();
    if (mode == RenderMode::present && !cs2::payload::runtime().present.install()) {
      mode = RenderMode::layer;  // dxgi/d3d11 unavailable: fall back
    }
    switch (mode) {
      case RenderMode::present:
        // In-process renderer: the ESP + menu render from the game's D3D11/DXGI
        // Present; no shared memory, no Vulkan layer.
        cs2::game::append_log("[cs2-payload] render: in-process Present (DXGI) + menu");
        break;
      case RenderMode::layer_thread:
        (void)cs2::payload::runtime().publisher.map();
        (void)cs2::payload::runtime().publisher.start();  // dedicated publish thread
        cs2::game::append_log("[cs2-payload] render: external Vulkan layer (publish thread)");
        break;
      case RenderMode::layer:
      default:
        // Publish the model to shared memory and let the external Vulkan layer
        // draw it, driven from the game's own present hook.
        (void)cs2::payload::runtime().publisher.map();
        (void)cs2::payload::runtime().game_present.install();
        cs2::game::append_log("[cs2-payload] render: external Vulkan layer (present hook)");
        break;
    }
    // The settings block's owner follows the render mode (ADR-0021): the
    // in-process menu owns it, and in the layer modes the shared segment does.
    cs2::payload::runtime().layer_owns_settings.store(mode != RenderMode::present);
  }
  return 0;
}
#endif

void boot() {
  // Ensure this (entry) thread has a TLS block for the mapped image even if the
  // injector's stub did not set one.
  cs2::tls::ensure_thread_tls();
  // Diagnostics first, so everything below is covered by the crash handler.
  // CS2_PAYLOAD_NO_DIAG disables the crash handler + watchdog (useful when
  // bisecting a freeze: the vectored handler does file I/O on the exception
  // path, which can itself deadlock).
  if (std::getenv("CS2_PAYLOAD_NO_DIAG") == nullptr) {
    cs2::diag::install_crash_handler();
  }

#ifdef CS2_PAYLOAD_HAVE_INTERFACES
  std::printf("[cs2-payload] boot; SDK has %zu interface(s)\n", cs2::sdk::INTERFACES.size());
#else
  std::puts("[cs2-payload] boot (no SDK headers)");
#endif

  // Native Windows builds are distinguishable in the log from the msvc-wine
  // cross build (both target Windows).
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  std::puts("[cs2-payload] native Windows build");
#elif defined(CS2_CROSS_WINDOWS_BUILD)
  std::puts("[cs2-payload] cross-built Windows payload (msvc-wine)");
#endif

#if defined(CS2_PAYLOAD_HAVE_SCHEMAS) && __has_include("schemas/client_dll.hpp")
  const auto* controller =
      cs2::sdk::schemas::client_dll::find_class_by_target("CBasePlayerController");
  const auto local_flag = cs2::sdk::schemas::client_dll::field_offset_by_target(
      "CBasePlayerController", "m_bIsLocalPlayerController");
  if (controller != nullptr) {
    std::printf(
        "[cs2-payload] CBasePlayerController size=0x%x fields=%zu "
        "m_bIsLocalPlayerController=0x%x\n",
        controller->size, controller->fields.size(), local_flag);
  }
#endif

  // Marker file so injection is observable from outside the target process.
#if defined(_WIN32)
  char marker_path[MAX_PATH];
  std::FILE* file = nullptr;
  if (cs2::game::data_file_path("cs2-payload-boot.txt", marker_path, sizeof(marker_path)) &&
      fopen_s(&file, marker_path, "a") == 0 && file != nullptr) {
#if defined(CS2_NATIVE_WINDOWS_BUILD)
    const char* build_kind = "native-windows";
#elif defined(CS2_CROSS_WINDOWS_BUILD)
    const char* build_kind = "cross-windows";
#else
    const char* build_kind = "windows";
#endif
    std::fprintf(file, "[cs2-payload] booted (pid %lu, build=%s)\n",
                 static_cast<unsigned long>(GetCurrentProcessId()), build_kind);
    std::fclose(file);
  }
#endif

  // The Present hook initializes the UI backend once the swapchain is available.
  // Run it on a separate thread so DllMain does not hold the loader lock while
  // device creation loads DLLs.
#if defined(_WIN32)
  // A normal CreateThread would run LdrpInitializeThread for every loaded
  // module (the game's TLS callbacks + DllMain(DLL_THREAD_ATTACH)), which wedges
  // CS2/DXVK; create it skipping thread attach instead (the routine bootstraps
  // its own TLS).
  //
  // The in-process renderer is the exception: its DXGI/D3D11 probe (method-table
  // discovery) faults inside dxgi when called from a thread that never ran module
  // thread-attach, so that path uses a normal thread (safe on native Windows
  // D3D11). The layer paths keep the TLS-skip thread (the Wine/DXVK wedge in
  // ADR-0017).
  if (render_mode() == RenderMode::present) {
    (void)CreateThread(nullptr, 0, &install_thread, nullptr, 0, nullptr);
  } else {
    (void)cs2::tls::create_thread_no_attach(reinterpret_cast<void* (*)(void*)>(&install_thread),
                                            nullptr);
  }
#else
  runtime().publisher.start();
#endif
}

}  // namespace cs2::payload

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    cs2::payload::boot();
  }
  return TRUE;
}
#endif
