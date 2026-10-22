// payload_main.cpp — payload DLL entry.
//
// Native Windows boots the DXGI function-hook renderer (ADR-0023) and drives the
// ImGui debug HUD, which reads local-player/player state via `payload/game`.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "cs2/control/service.hpp"
#include "cs2/diag/crash_handler.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/features/game_present_hook.hpp"
#include "cs2/features/model_publisher.hpp"
#include "cs2/features/overlay.hpp"
#include "cs2/features/present_hook.hpp"
#include "cs2/features/camera.hpp"
#include "cs2/game/log.hpp"
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

// Lifetime owner for the payload's subsystems. A namespace-scope global (not a
// function-local static) so it is constructed during CRT startup on the
// injector's entry thread: the static-init guard would otherwise run on the
// hook-install thread, whose static-CRT per-thread state is not initialized for
// a manually mapped module.
struct Runtime {
  render::ImGuiBackend backend;
  features::Overlay overlay;
  features::ModelPublisher publisher{overlay};
  features::GamePresentHook game_present{publisher};
  features::PresentHook present{backend, overlay};
  features::CameraFeatures camera;
  control::Service control;
};

Runtime g_runtime;

auto runtime() -> Runtime& {
  return g_runtime;
}

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
    std::string token = value.substr(start, end == std::string::npos ? std::string::npos
                                                                    : end - start);
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
    } else if (token.rfind("key=", 0) == 0) {
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

// The overlay menu's `Movement` tab writes the shared settings; the publisher calls
// this when they *change*, and the payload's movement features follow.
void on_movement_settings(void* context, bool auto_strafe, bool bhop, bool rage, int rage_key,
                          bool rage_fire, bool recovery_choke) noexcept {
  auto* runtime = static_cast<Runtime*>(context);
  runtime->control.set_movement_features(auto_strafe, bhop);
  runtime->control.set_rage_features(rage, rage_key, rage_fire);
  (void)runtime->control.set_recovery_choke(recovery_choke);
}

// The native menu edits the same feature configuration directly on the render
// thread; no cross-process settings transport is involved.
void on_native_settings(void* context, const overlay::wire::Settings& ui) noexcept {
  auto* owner = static_cast<Runtime*>(context);
  auto config = owner->overlay.esp_config();
  config.box = ui.box != 0; config.health_bar = ui.health_bar != 0;
  config.ammo_bar = ui.ammo_bar != 0; config.name = ui.name != 0;
  config.weapon = ui.weapon != 0; config.flags = ui.flags != 0;
  config.only_enemies = ui.only_enemies != 0; config.show_dead = ui.show_dead != 0;
  config.hitboxes = ui.hitboxes != 0;
  owner->overlay.set_esp_config(config);
  owner->control.set_movement_features(ui.auto_strafe != 0, ui.bhop != 0);
  owner->control.set_nl_movement_options(static_cast<int>(ui.movement_subticks),ui.movement_speed_gain/100.0F);
  owner->control.set_rage_features(ui.rage != 0, ui.rage_key, ui.rage_fire != 0);
  owner->control.set_rage_policy(ui.rage_minimum_damage, ui.rage_hitchance / 100.0F);
  if (!owner->control.set_recovery_choke(ui.recovery_choke != 0))
    cs2::game::append_log("[command-choke] binding/hook unavailable; commands pass through");
  owner->camera.configure(ui.third_person != 0, ui.remove_scope != 0, ui.third_person_distance, ui.third_person_hull);
}

// The per-frame overlay refresh forwards the local pawn so the movement features
// (strafe/bhop prediction) see it fresh during menu-only play.
void on_local_pawn(void* context, std::uintptr_t pawn) noexcept {
  static_cast<Runtime*>(context)->control.note_local_pawn(pawn);
  static_cast<Runtime*>(context)->camera.note_local_pawn(pawn);
}

// The per-frame overlay refresh also forwards the full snapshot: the ragebot solve
// runs there and stashes the silent aim for the per-tick `Input::apply()`.
void on_snapshot(void* context, const cs2::game::Snapshot& snapshot) noexcept {
  static_cast<Runtime*>(context)->control.on_snapshot(snapshot);
}

// The overlay HUD asks for the ragebot's one-line status while it is enabled.
bool on_rage_debug(void* context, game::RageDebugSnapshot& out) noexcept {
  return static_cast<Runtime*>(context)->control.rage_debug(out);
}
bool on_rage_status(void* context, char* out, std::size_t size) noexcept {
  return static_cast<Runtime*>(context)->control.rage_status(out, size);
}

// The overlay's hitbox visualiser asks the payload for a pawn's real studio
// hitbox capsules (`docs/re/aim.md`).
int on_hitboxes(void* context, std::uintptr_t pawn, cs2::game::HitboxCapsule* out,
                int max) noexcept {
  return static_cast<Runtime*>(context)->control.read_hitboxes(pawn, out, max);
}

// The overlay highlights the ragebot's current target.
int on_rage_target(void* context) noexcept {
  return static_cast<Runtime*>(context)->control.rage_target_index();
}

// The overlay asks for the bhop prediction's position to draw the demo box.
bool on_predicted_origin(void* context, float out[3]) noexcept {
  return static_cast<Runtime*>(context)->control.predicted_origin(out);
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
    const RageConfig rage = parse_rage_env();
    cs2::game::append_log(std::string("[cs2-payload] rage: enabled=") +
                          (rage.enabled ? "1" : "0") + " key=" + std::to_string(rage.key) +
                          " fire=" + (rage.fire ? "1" : "0"));
    cs2::payload::runtime().control.set_rage_features(rage.enabled, rage.key, rage.fire);
    cs2::payload::runtime().publisher.set_rage_defaults(rage.enabled, rage.key, rage.fire);
    cs2::payload::runtime().publisher.set_settings_sink(&on_movement_settings,
                                                        &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_local_pawn_sink(&on_local_pawn,
                                                        &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_snapshot_sink(&on_snapshot, &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_rage_status_provider(&on_rage_status,
                                                             &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_hitbox_provider(&on_hitboxes,
                                                        &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_rage_target_provider(&on_rage_target,
                                                             &cs2::payload::runtime());
    cs2::payload::runtime().overlay.set_predicted_origin_provider(
        &on_predicted_origin, &cs2::payload::runtime());
  }
#if defined(CS2_NATIVE_WINDOWS_BUILD)
  // Native Windows draws directly from the real DXGI present callback. No
  // hard-coded engine RVA, publisher thread, Vulkan layer, or shared file.
  overlay::wire::Settings ui{};
  const auto config = cs2::payload::runtime().overlay.esp_config();
  ui.box = config.box; ui.health_bar = config.health_bar; ui.ammo_bar = config.ammo_bar;
  ui.name = config.name; ui.weapon = config.weapon; ui.flags = config.flags;
  ui.only_enemies = config.only_enemies; ui.show_dead = config.show_dead;
  ui.hitboxes = config.hitboxes;
  const auto movement = parse_movement_env();
  ui.auto_strafe = movement.strafe; ui.bhop = movement.bhop;
  const auto rage = parse_rage_env();
  ui.rage = rage.enabled; ui.rage_key = static_cast<std::uint8_t>(rage.key); ui.rage_fire = rage.fire;
  cs2::payload::runtime().backend.configure_menu(ui, &on_native_settings, &cs2::payload::runtime());
  cs2::payload::runtime().control.set_nl_movement_options(static_cast<int>(ui.movement_subticks),ui.movement_speed_gain/100.0F);
  cs2::payload::runtime().backend.configure_rage_debug(&on_rage_debug, &cs2::payload::runtime());
  if (!cs2::payload::runtime().camera.install())
    cs2::game::append_log("[camera] native camera hooks unavailable");
  bool installed = false;
  for (unsigned attempt = 0; attempt < 30 && !installed; ++attempt) {
    installed = cs2::payload::runtime().present.install();
    if (!installed) Sleep(1000);
  }
  cs2::game::append_log(installed ? "[native-dx11] native overlay ready" :
      "[native-dx11] installation failed; use the Direct3D renderer (remove -vulkan)");
#else
  // Publish the overlay model for the Vulkan present layer (ADR-0016), driven
  // from the game's own present hook on the render thread — no payload thread.
  if (std::getenv("CS2_PAYLOAD_NO_PUBLISH") == nullptr) {
    (void)cs2::payload::runtime().publisher.map();
    if (std::getenv("CS2_PAYLOAD_PUB_THREAD") != nullptr) {
      (void)cs2::payload::runtime().publisher.start();  // legacy dedicated thread
    } else {
      (void)cs2::payload::runtime().game_present.install();
    }
  }
#endif
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
  const auto* controller = cs2::sdk::schemas::client_dll::find_class_by_target("CBasePlayerController");
  const auto local_flag =
      cs2::sdk::schemas::client_dll::field_offset_by_target("CBasePlayerController",
                                                            "m_bIsLocalPlayerController");
  if (controller != nullptr) {
    std::printf("[cs2-payload] CBasePlayerController size=0x%x fields=%zu m_bIsLocalPlayerController=0x%x\n",
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
  cs2::tls::create_thread_no_attach(reinterpret_cast<void* (*)(void*)>(&install_thread), nullptr);
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
