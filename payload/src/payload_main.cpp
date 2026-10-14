// payload_main.cpp — payload DLL entry.
//
// Boots the payload: interposes the DXGI swapchain (ADR-0015) and drives the
// ImGui debug HUD, which reads local-player/player state via `payload/game`.
#include <cstdio>
#include <cstdlib>

#include "cs2/diag/crash_handler.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/features/game_present_hook.hpp"
#include "cs2/features/model_publisher.hpp"
#include "cs2/features/overlay.hpp"
#include "cs2/features/present_hook.hpp"
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
};

Runtime g_runtime;

auto runtime() -> Runtime& {
  return g_runtime;
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
  if (reason == DLL_PROCESS_ATTACH) cs2::payload::boot();
  return TRUE;
}
#endif
