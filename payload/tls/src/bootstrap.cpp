#include "cs2/tls/bootstrap.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <intrin.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cs2::tls {
namespace {

// x64: TEB->ThreadLocalStoragePointer (pointer to the TLS slot array).
constexpr unsigned long k_teb_tls_pointer = 0x58;

// The in-TEB array holds the first 64 slots; higher indices live in expansion
// blocks. TlsAlloc hands out low indices first, so 64 is a safe bound here.
constexpr uint32_t k_inline_tls_slots = 64;

}  // namespace

void ensure_thread_tls() noexcept {
  const auto* base = reinterpret_cast<const std::byte*>(&__ImageBase);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return;

  const IMAGE_DATA_DIRECTORY& directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (directory.VirtualAddress == 0) return;  // image has no TLS
  const auto* tls = reinterpret_cast<const IMAGE_TLS_DIRECTORY*>(base + directory.VirtualAddress);
  if (tls->AddressOfIndex == 0) return;

  const uint32_t index = *reinterpret_cast<const uint32_t*>(tls->AddressOfIndex);
  if (index == 0xFFFFFFFFu || index >= k_inline_tls_slots) return;

  auto* tls_array = reinterpret_cast<std::uint64_t*>(__readgsqword(k_teb_tls_pointer));
  if (tls_array == nullptr || tls_array[index] != 0) return;  // already installed

  const auto raw_size =
      static_cast<std::size_t>(tls->EndAddressOfRawData - tls->StartAddressOfRawData);
  const std::size_t block_size = raw_size + tls->SizeOfZeroFill;
  if (block_size == 0) return;

  void* block = VirtualAlloc(nullptr, block_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (block == nullptr) return;
  if (raw_size != 0) {
    std::memcpy(block, reinterpret_cast<const void*>(tls->StartAddressOfRawData), raw_size);
  }
  std::memset(static_cast<std::byte*>(block) + raw_size, 0, tls->SizeOfZeroFill);
  tls_array[index] = reinterpret_cast<std::uint64_t>(block);
}

namespace {

// THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH: skip LdrpInitializeThread.
constexpr unsigned long k_skip_thread_attach = 0x2;
constexpr unsigned long k_thread_all_access = 0x1FFFFF;

using NtCreateThreadExFn = long(__stdcall*)(void** thread_handle, unsigned long desired_access,
                                            void* object_attributes, void* process_handle,
                                            void* start_routine, void* argument,
                                            unsigned long create_flags, std::size_t zero_bits,
                                            std::size_t stack_size, std::size_t max_stack_size,
                                            void* attribute_list);

}  // namespace

auto create_thread_no_attach(void* (*start)(void*), void* parameter) noexcept -> void* {
  const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (ntdll == nullptr) return nullptr;
  const auto nt_create_thread_ex =
      reinterpret_cast<NtCreateThreadExFn>(GetProcAddress(ntdll, "NtCreateThreadEx"));
  if (nt_create_thread_ex == nullptr) return nullptr;

  void* thread = nullptr;
  const long status =
      nt_create_thread_ex(&thread, k_thread_all_access, nullptr, reinterpret_cast<void*>(-1),
                          reinterpret_cast<void*>(start), parameter, k_skip_thread_attach, 0, 0, 0,
                          nullptr);
  return status >= 0 ? thread : nullptr;
}

}  // namespace cs2::tls

#else  // !_WIN32 (host builds)

namespace cs2::tls {
void ensure_thread_tls() noexcept {}
void* create_thread_no_attach(void* (*)(void*), void*) noexcept { return nullptr; }
}  // namespace cs2::tls

#endif
