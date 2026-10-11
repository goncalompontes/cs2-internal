// payload_main.cpp — placeholder entry for the payload DLL.
//
// Real loader/manual-map entry, hook install and feature wiring land in
// Phases 2-4. For now it proves the cross-compiled DLL links, and that the
// generated SDK (payload/sdk) is consumable from the payload.
#include <cstdio>

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

  // Marker file so injection is observable from outside the target process
  // (printf may not be visible under the game's stdout redirection).
#if defined(_WIN32)
  if (std::FILE* file = std::fopen("C:\\cs2-payload-boot.txt", "a")) {
    std::fprintf(file, "[cs2-payload] booted (pid %lu)\n",
                 static_cast<unsigned long>(GetCurrentProcessId()));
    std::fclose(file);
  }
#endif
}

}  // namespace cs2::payload

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) cs2::payload::boot();
  return TRUE;
}
#endif
