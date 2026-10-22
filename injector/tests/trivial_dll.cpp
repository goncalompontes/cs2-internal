// A trivial, CRT-less logging DLL used to validate the manual-map injector.
//
// No CRT means no loader TLS / CRT init, so the image can be manual-mapped and
// its entry point called directly. It writes a marker file next to the drive
// root so injection is observable from outside the process.
#include <windows.h>

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    HANDLE file = CreateFileA("C:\\cs2-trivial-boot.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      const char message[] = "trivial dll attached\n";
      DWORD written = 0;
      WriteFile(file, message, sizeof(message) - 1, &written, nullptr);
      CloseHandle(file);
    }
  }
  return TRUE;
}
