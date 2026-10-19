// payload_main.cpp — payload DLL entry.
//
// Boots the payload: interposes the DXGI swapchain (ADR-0015) and drives the
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

// The overlay menu's `Movement`/`Ragebot` tabs write the shared settings; the
// publisher calls this on every published frame, and the payload's features follow (so
// the menu is the single owner).
void on_movement_settings(void* context, const cs2::overlay::wire::Settings& settings) noexcept {
  auto* runtime = static_cast<Runtime*>(context);
  runtime->control.set_movement_features(settings.auto_strafe != 0, settings.bhop != 0);
  runtime->control.set_rage_features(settings.rage != 0, settings.rage_key,
                                     settings.rage_fire != 0, settings.rage_objective,
                                     settings.rage_hitbox, settings.rage_fov,
                                     settings.rage_min_damage, settings.rage_min_hitchance,
                                     settings.rage_autostop, settings.rage_autoscope,
                                     settings.rage_autoreload != 0,
                                     settings.rage_only_visible != 0, settings.rage_mode,
                                     settings.rage_per_weapon != 0, settings.rage_weapon,
                                     settings.rage_config_op, settings.rage_config_token);
}

// The per-frame overlay refresh forwards the local pawn so the movement features
// (strafe/bhop prediction) see it fresh during menu-only play.
void on_local_pawn(void* context, std::uintptr_t pawn) noexcept {
  static_cast<Runtime*>(context)->control.note_local_pawn(pawn);
}

// The per-frame overlay refresh also forwards the full snapshot: the ragebot solve
// runs there and stashes the silent aim for the per-tick `Input::apply()`.
void on_snapshot(void* context, const cs2::game::Snapshot& snapshot) noexcept {
  static_cast<Runtime*>(context)->control.on_snapshot(snapshot);
}

// The overlay HUD asks for the ragebot's one-line status while it is enabled.
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
    cs2::payload::runtime().control.set_rage_features(
        rage.enabled, rage.key, rage.fire, 1 /*hitchance*/, 0 /*auto*/, 0.0F, 0.0F, 0.0F, 0, 0,
        false, false, 0 /*hold*/, false, "", 0, 0);
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
