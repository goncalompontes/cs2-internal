// payload_main.cpp — placeholder entry for the payload DLL.
// Real loader/manual-map entry, hook install and feature wiring land in
// Phases 2-4. For now it just proves the cross-compiled DLL links.
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::payload {

void boot() { std::puts("[cs2-payload] boot (stub)"); }

}  // namespace cs2::payload

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) cs2::payload::boot();
  return TRUE;
}
#endif
