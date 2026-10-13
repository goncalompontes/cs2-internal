#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::game {

void append_log(std::string_view text) noexcept {
#if defined(_WIN32)
  using CreateFileAFn = HANDLE(__stdcall*)(const char*, unsigned long, unsigned long, void*,
                                           unsigned long, unsigned long, HANDLE);
  static CreateFileAFn create_file = nullptr;
  if (create_file == nullptr) {
    const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) return;
    create_file = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel32, "CreateFileA"));
    if (create_file == nullptr) return;
  }
  constexpr unsigned long k_file_append_data = 0x4;
  constexpr unsigned long k_file_share_read = 0x1;
  constexpr unsigned long k_open_always = 0x4;
  constexpr unsigned long k_file_attribute_normal = 0x80;
  HANDLE file = create_file("C:\\cs2-payload.log", k_file_append_data, k_file_share_read, nullptr,
                            k_open_always, k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) return;
  unsigned long written = 0;
  WriteFile(file, text.data(), static_cast<unsigned long>(text.size()), &written, nullptr);
  WriteFile(file, "\n", 1, &written, nullptr);
  CloseHandle(file);
#else
  (void)text;
#endif
}

}  // namespace cs2::game
