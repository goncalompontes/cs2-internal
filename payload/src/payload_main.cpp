// payload_main.cpp — payload DLL entry.
//
// Boots the payload: installs the Present hook (kiero + MinHook) and drives the
// ImGui debug HUD, which reads local-player/player state via `payload/game`.
#include <cstdio>

#include "cs2/features/overlay.hpp"
#include "cs2/features/present_hook.hpp"
#include "cs2/render/imgui_backend.hpp"

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
  features::PresentHook present{backend, overlay};
};

Runtime g_runtime;

Runtime& runtime() {
  return g_runtime;
}

}  // namespace

#if defined(_WIN32)
DWORD WINAPI install_thread(void*) {
  // Install off the loader lock: device creation loads DLLs and would deadlock
  // if done from DllMain.
  cs2::payload::runtime().overlay.initialize();
  cs2::payload::runtime().present.install();
  return 0;
}
#endif

void boot() {
#ifdef CS2_PAYLOAD_HAVE_INTERFACES
  std::printf("[cs2-payload] boot; SDK has %zu interface(s)\n", cs2::sdk::INTERFACES.size());
#else
  std::puts("[cs2-payload] boot (no SDK headers)");
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
  std::FILE* file = nullptr;
  if (fopen_s(&file, "C:\\cs2-payload-boot.txt", "a") == 0 && file != nullptr) {
    std::fprintf(file, "[cs2-payload] booted (pid %lu)\n",
                 static_cast<unsigned long>(GetCurrentProcessId()));
    std::fclose(file);
  }
#endif

  // The Present hook initializes the UI backend once the swapchain is available.
  // Run it on a separate thread so DllMain does not hold the loader lock while
  // device creation loads DLLs.
#if defined(_WIN32)
  CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr);
#else
  runtime().present.install();
#endif
}

}  // namespace cs2::payload

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) cs2::payload::boot();
  return TRUE;
}
#endif
