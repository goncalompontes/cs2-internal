// payload_main.cpp — payload DLL entry.
//
// Boots the payload: logs SDK info, then installs the Present hook driving the
// UI backend. Real overlay + features land in later phases.
#include <cstdio>

#include "cs2/features/present_hook.hpp"
#include "cs2/render/frame_log_backend.hpp"

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

// Lifetime owner for the payload's subsystems (constructed on first boot).
struct Runtime {
  render::FrameLogBackend backend;
  features::PresentHook present{backend};
};

Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace

void boot() {
#ifdef CS2_PAYLOAD_HAVE_INTERFACES
  std::printf("[cs2-payload] boot; SDK has %zu interface(s)\n", cs2::sdk::INTERFACES.size());
#else
  std::puts("[cs2-payload] boot (no SDK headers)");
#endif

#if defined(CS2_PAYLOAD_HAVE_SCHEMAS) && __has_include("schemas/client_dll.hpp")
  const auto* entity = cs2::sdk::schemas::client_dll::find_class_by_target("C_BaseEntity");
  if (entity != nullptr) {
    const auto health =
        cs2::sdk::schemas::client_dll::field_offset_by_target("C_BaseEntity", "m_iHealth");
    std::printf("[cs2-payload] C_BaseEntity size=0x%x fields=%zu m_iHealth=0x%x\n",
                entity->size, entity->fields.size(), health);
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

  if (!runtime().backend.initialize()) {
    std::puts("[cs2-payload] UI backend failed to initialize");
  }
  runtime().present.install();
}

}  // namespace cs2::payload

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) cs2::payload::boot();
  return TRUE;
}
#endif
