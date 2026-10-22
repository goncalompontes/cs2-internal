#include <windows.h>
// No game code, I/O, or background work: a fixture for Windows-loader tests.
__declspec(thread) int native_loader_tls = 73;
extern "C" __declspec(dllexport) int native_loader_tls_value() { return native_loader_tls; }
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
