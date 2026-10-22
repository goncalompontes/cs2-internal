// A minimal static-CRT DLL to isolate CRT-init behaviour under manual mapping.
#include <windows.h>

#include <cstdio>

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    if (std::FILE* file = std::fopen("C:\\cs2-crt-boot.txt", "a")) {
      std::fputs("crt dll attached\n", file);
      std::fclose(file);
    }
  }
  return TRUE;
}
