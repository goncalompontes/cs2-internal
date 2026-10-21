// Manual-map injector (Windows x64).
//
// Runs inside the target's Wine/Proton prefix and maps a DLL into another
// process without LoadLibrary: parse the PE, map sections + apply base
// relocations into a local buffer, resolve the target's imports by reading its
// modules' export tables, write the image into the target, then call the entry
// point on a remote thread.
//
// Usage:
//   injector.exe <process-name|pid> <payload.dll> [--no-call]
//
// Build: see injector/meson.build.

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cs2/core/handle.hpp"
#include "cs2/core/win_handle.hpp"
#include "cs2/loader/mapping.hpp"
#include "cs2/loader/pe.hpp"

namespace {

using Bytes = std::span<const std::byte>;

auto rd16(const std::byte* p) -> std::uint16_t {
  std::uint16_t v = 0;
  std::memcpy(&v, p, 2);
  return v;
}

auto rd32(const std::byte* p) -> std::uint32_t {
  std::uint32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

auto find_pid(const char* name) -> std::uint32_t {
  // The snapshot is owned by the wrapper, so it is released on every path.
  const cs2::core::ScopedKernelHandle snapshot(
      CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.valid()) {
    return 0;
  }
  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  std::uint32_t pid = 0;
  if (Process32First(static_cast<HANDLE>(snapshot.get()), &entry) != 0) {
    do {
      if (_stricmp(entry.szExeFile, name) == 0) {
        pid = entry.th32ProcessID;
        break;
      }
    } while (Process32Next(static_cast<HANDLE>(snapshot.get()), &entry) != 0);
  }
  return pid;
}

auto peb_module_base(HANDLE process, const char* name) -> std::optional<std::uintptr_t>;

auto remote_module_base(HANDLE process, const char* name) -> std::optional<std::uintptr_t> {
  const DWORD pid = GetProcessId(process);
  const cs2::core::ScopedKernelHandle snapshot(
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
  if (snapshot.valid()) {
    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    std::optional<std::uintptr_t> base;
    if (Module32First(static_cast<HANDLE>(snapshot.get()), &entry) != 0) {
      do {
        if (_stricmp(entry.szModule, name) == 0) {
          base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
          break;
        }
      } while (Module32Next(static_cast<HANDLE>(snapshot.get()), &entry) != 0);
    }
    if (base.has_value()) {
      return base;
    }
  }
  // Wine's toolhelp module snapshot is unreliable (it can fail outright or
  // return an empty list); walk the target's PEB loader list instead.
  return peb_module_base(process, name);
}

auto read_remote(HANDLE process, std::uintptr_t address, void* buffer, std::size_t size) -> bool {
  SIZE_T got = 0;
  return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &got) != 0 &&
         got == size;
}

auto write_remote(HANDLE process, std::uintptr_t address, const void* buffer, std::size_t size)
    -> bool {
  SIZE_T wrote = 0;
  return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size, &wrote) != 0 &&
         wrote == size;
}

auto read_remote_string(HANDLE process, std::uintptr_t address, std::size_t max) -> std::string {
  std::vector<char> buffer(max, '\0');
  SIZE_T got = 0;
  if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer.data(), max, &got) ==
      0) {
    return {};
  }
  return std::string(buffer.data());  // stops at NUL
}

auto read_u64(HANDLE process, std::uintptr_t address) -> std::uint64_t {
  std::uint64_t value = 0;
  read_remote(process, address, &value, sizeof(value));
  return value;
}

auto write_u16(HANDLE process, std::uintptr_t address, std::uint16_t value) -> bool {
  return write_remote(process, address, &value, sizeof(value));
}

auto write_u32(HANDLE process, std::uintptr_t address, std::uint32_t value) -> bool {
  return write_remote(process, address, &value, sizeof(value));
}

auto write_u64(HANDLE process, std::uintptr_t address, std::uint64_t value) -> bool {
  return write_remote(process, address, &value, sizeof(value));
}

// LDR_DATA_TABLE_ENTRY / PEB offsets (x64).
constexpr std::size_t k_peb_ldr = 0x18;
constexpr std::size_t k_ldr_in_load_order = 0x10;
constexpr std::size_t k_ldr_in_memory_order = 0x20;
constexpr std::size_t k_ldr_dll_base = 0x30;
constexpr std::size_t k_ldr_entry_point = 0x38;
constexpr std::size_t k_ldr_size_of_image = 0x40;
constexpr std::size_t k_ldr_full_name = 0x48;
constexpr std::size_t k_ldr_base_name = 0x58;

auto remote_peb(HANDLE process) -> std::uintptr_t {
  using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto nt_query = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(ntdll, "NtQueryInformationProcess"));
  if (nt_query == nullptr) {
    return 0;
  }
  struct ProcessBasicInformation {
    PVOID reserved1;
    PVOID peb_base_address;
    PVOID reserved2[2];
    ULONG_PTR unique_process_id;
    PVOID reserved3;
  } pbi{};
  if (nt_query(process, 0 /* ProcessBasicInformation */, &pbi, sizeof(pbi), nullptr) != 0) {
    return 0;
  }
  return reinterpret_cast<std::uintptr_t>(pbi.peb_base_address);
}

/// Find a loaded module by walking the target's PEB `InLoadOrderModuleList`.
auto peb_module_base(HANDLE process, const char* name) -> std::optional<std::uintptr_t> {
  const std::uintptr_t peb = remote_peb(process);
  if (peb == 0) {
    return std::nullopt;
  }
  const std::uint64_t ldr = read_u64(process, peb + k_peb_ldr);
  if (ldr == 0) {
    return std::nullopt;
  }
  const std::uintptr_t head = ldr + k_ldr_in_load_order;
  std::uintptr_t node = read_u64(process, head);
  for (int guard = 0; node != 0 && node != head && guard < 4096; ++guard) {
    const std::uintptr_t entry = node - k_ldr_in_load_order;
    const auto length =
        static_cast<std::uint16_t>(read_u64(process, entry + k_ldr_base_name) & 0xffff);
    const std::uintptr_t buffer = read_u64(process, entry + k_ldr_base_name + 8);
    if (buffer != 0 && length >= 2 && length < 512) {
      std::wstring wide(static_cast<std::size_t>(length / 2), L'\0');
      if (read_remote(process, buffer, wide.data(), length)) {
        std::string narrow;
        narrow.reserve(wide.size());
        for (const wchar_t c : wide) {
          narrow.push_back(static_cast<char>(c));
        }
        if (_stricmp(narrow.c_str(), name) == 0) {
          return static_cast<std::uintptr_t>(read_u64(process, entry + k_ldr_dll_base));
        }
      }
    }
    node = read_u64(process, node);
  }
  return std::nullopt;
}

/// Diagnostic: print the target's modules via toolhelp and the PEB walk.
void dump_target_modules(HANDLE process) {
  const DWORD pid = GetProcessId(process);
  std::printf("injector: --- toolhelp ---\n");
  {
    // The snapshot is released at the end of this block -- the same point it
    // was released at before the wrapper took it over.
    const cs2::core::ScopedKernelHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot.valid()) {
      std::printf("  snapshot FAILED (err %lu)\n", GetLastError());
    } else {
      MODULEENTRY32 entry{};
      entry.dwSize = sizeof(entry);
      if (Module32First(static_cast<HANDLE>(snapshot.get()), &entry) != 0) {
        int count = 0;
        do {
          std::printf("  %-32s 0x%llx\n", entry.szModule,
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(entry.modBaseAddr)));
        } while ((Module32Next(static_cast<HANDLE>(snapshot.get()), &entry) != 0) && ++count < 80);
      } else {
        std::printf("  Module32First FAILED (err %lu)\n", GetLastError());
      }
    }
  }
  std::printf("injector: --- peb ---\n");
  const std::uintptr_t peb = remote_peb(process);
  std::printf("  peb = 0x%llx\n", static_cast<unsigned long long>(peb));
  if (peb == 0) {
    return;
  }
  const std::uint64_t ldr = read_u64(process, peb + k_peb_ldr);
  std::printf("  ldr = 0x%llx\n", static_cast<unsigned long long>(ldr));
  if (ldr == 0) {
    return;
  }
  const std::uintptr_t head = ldr + k_ldr_in_load_order;
  std::uintptr_t node = read_u64(process, head);
  for (int guard = 0; node != 0 && node != head && guard < 80; ++guard) {
    const std::uintptr_t entry = node - k_ldr_in_load_order;
    const auto length =
        static_cast<std::uint16_t>(read_u64(process, entry + k_ldr_base_name) & 0xffff);
    const std::uintptr_t buffer = read_u64(process, entry + k_ldr_base_name + 8);
    std::string narrow;
    if (buffer != 0 && length >= 2 && length < 512) {
      std::wstring wide(static_cast<std::size_t>(length / 2), L'\0');
      if (read_remote(process, buffer, wide.data(), length)) {
        for (const wchar_t c : wide) {
          narrow.push_back(static_cast<char>(c));
        }
      }
    }
    std::printf("  %-32s 0x%llx\n", narrow.c_str(),
                static_cast<unsigned long long>(read_u64(process, entry + k_ldr_dll_base)));
    node = read_u64(process, node);
  }
}

/// Insert an LDR_DATA_TABLE_ENTRY for the mapped image into the target's PEB
/// loader list, so loader-dependent CRT init (RtlPcToFileHeader,
/// GetModuleHandle, ...) can find it.
auto register_module(HANDLE process, std::uintptr_t base, std::uint32_t size_of_image,
                     std::uint32_t entry_rva, const wchar_t* dll_name) -> bool {
  using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto nt_query = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(ntdll, "NtQueryInformationProcess"));
  if (nt_query == nullptr) {
    return false;
  }

  struct ProcessBasicInformation {
    PVOID reserved1;
    PVOID peb_base_address;
    PVOID reserved2[2];
    ULONG_PTR unique_process_id;
    PVOID reserved3;
  } pbi{};
  if (nt_query(process, 0 /* ProcessBasicInformation */, &pbi, sizeof(pbi), nullptr) != 0) {
    return false;
  }

  const auto peb = reinterpret_cast<std::uintptr_t>(pbi.peb_base_address);
  const std::uint64_t ldr = read_u64(process, peb + k_peb_ldr);
  if (ldr == 0) {
    return false;
  }

  const void* memory =
      VirtualAllocEx(process, nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (memory == nullptr) {
    return false;
  }
  const auto entry = reinterpret_cast<std::uintptr_t>(memory);
  const std::size_t name_length = std::wcslen(dll_name);

  // Name buffers, then the UNICODE_STRINGs that point at them.
  write_remote(process, entry + 0x100, dll_name, (name_length + 1) * sizeof(wchar_t));
  write_remote(process, entry + 0x140, dll_name, (name_length + 1) * sizeof(wchar_t));
  const auto write_unicode_string = [&](std::size_t offset, std::uintptr_t buffer) -> void {
    write_u16(process, entry + offset, static_cast<std::uint16_t>(name_length * 2));
    write_u16(process, entry + offset + 2, static_cast<std::uint16_t>((name_length + 1) * 2));
    write_u64(process, entry + offset + 8, buffer);
  };
  write_unicode_string(k_ldr_base_name, entry + 0x100);
  write_unicode_string(k_ldr_full_name, entry + 0x140);

  write_u64(process, entry + k_ldr_dll_base, base);
  write_u64(process, entry + k_ldr_entry_point, base + entry_rva);
  write_u32(process, entry + k_ldr_size_of_image, size_of_image);

  const auto link = [&](std::size_t list_offset, std::size_t links_offset) -> void {
    const std::uintptr_t head = ldr + list_offset;
    const std::uint64_t first = read_u64(process, head);
    write_u64(process, entry + links_offset, first);
    write_u64(process, entry + links_offset + 8, head);
    write_u64(process, first + 8, entry);
    write_u64(process, head, entry);
  };
  link(k_ldr_in_load_order, 0x00);
  link(k_ldr_in_memory_order, 0x10);
  return true;
}

/// Resolve an exported symbol in a loaded module by reading its export table
/// out of the target process. Handles forwarders (e.g. kernel32 -> kernelbase).
auto resolve_export(HANDLE process, std::uintptr_t module_base, const std::string& name,
                    std::uint16_t ordinal, bool by_ordinal, int depth = 0)
    -> std::optional<std::uintptr_t> {
  if (depth > 4) {
    return std::nullopt;
  }
  std::byte dos[0x40];
  if (!read_remote(process, module_base, dos, sizeof(dos))) {
    return std::nullopt;
  }
  const std::uint32_t e_lfanew = rd32(dos + 0x3C);

  std::byte pe[0x120];
  if (!read_remote(process, module_base + e_lfanew, pe, sizeof(pe))) {
    return std::nullopt;
  }
  const std::uint16_t magic = rd16(pe + 24);
  const std::size_t dirs = magic == 0x020B ? 24 + 112 : 24 + 96;
  const std::uint32_t export_rva = rd32(pe + dirs);
  const std::uint32_t export_size = rd32(pe + dirs + 4);
  if (export_rva == 0) {
    return std::nullopt;
  }

  std::byte dir[40];
  if (!read_remote(process, module_base + export_rva, dir, sizeof(dir))) {
    return std::nullopt;
  }
  const std::uint32_t ordinal_base = rd32(dir + 16);
  const std::uint32_t number_of_names = rd32(dir + 24);
  const std::uint32_t address_of_functions = rd32(dir + 28);
  const std::uint32_t address_of_names = rd32(dir + 32);
  const std::uint32_t address_of_name_ordinals = rd32(dir + 36);

  const auto function_rva = [&](std::uint32_t index) -> std::optional<std::uint32_t> {
    std::byte entry[4];
    if (!read_remote(process, module_base + address_of_functions + index * 4, entry, 4)) {
      return std::nullopt;
    }
    return rd32(entry);
  };

  std::optional<std::uint32_t> rva;
  if (by_ordinal) {
    if (ordinal >= ordinal_base) {
      rva = function_rva(ordinal - ordinal_base);
    }
  } else {
    for (std::uint32_t i = 0; i < number_of_names && i < 65536; ++i) {
      std::byte name_rva[4];
      if (!read_remote(process, module_base + address_of_names + i * 4, name_rva, 4)) {
        break;
      }
      const std::string candidate = read_remote_string(process, module_base + rd32(name_rva), 256);
      if (candidate != name) {
        continue;
      }
      std::byte ord_index[2];
      if (!read_remote(process, module_base + address_of_name_ordinals + i * 2, ord_index, 2)) {
        return std::nullopt;
      }
      rva = function_rva(rd16(ord_index));
      break;
    }
  }
  if (!rva) {
    return std::nullopt;
  }

  // A forwarder's "RVA" points inside the export directory at a "Dll.Symbol"
  // string; resolve it against the named module instead of treating it as code.
  if (*rva >= export_rva && *rva < export_rva + export_size) {
    const std::string forwarder = read_remote_string(process, module_base + *rva, 256);
    const std::size_t dot = forwarder.find('.');
    if (dot == std::string::npos) {
      return std::nullopt;
    }
    std::string dll = forwarder.substr(0, dot);
    const std::string symbol = forwarder.substr(dot + 1);
    if (!dll.contains(".dll") && !dll.contains(".DLL")) {
      dll += ".dll";
    }
    const auto forward_base = remote_module_base(process, dll.c_str());
    if (!forward_base) {
      return std::nullopt;
    }
    const bool forward_ordinal = !symbol.empty() && symbol[0] == '#';
    const std::uint16_t forward_ord =
        forward_ordinal ? static_cast<std::uint16_t>(std::stoi(symbol.substr(1))) : 0;
    return resolve_export(process, *forward_base, forward_ordinal ? "" : symbol, forward_ord,
                          forward_ordinal, depth + 1);
  }

  return module_base + *rva;
}

/// A TLS slot allocated in the target for a manually mapped image.
struct TlsSlot {
  bool ok = false;
  std::uint32_t index = 0;
  std::uintptr_t block = 0;
};

/// Initialize TLS for the mapped image: allocate a slot in the target, point
/// `_tls_index` at it, and build a per-thread block from the image's raw data.
/// The block is set on the entry thread by the stub before `DllMain` runs.
auto setup_tls(HANDLE process, std::uintptr_t remote_base, const cs2::loader::pe::ImageInfo& image,
               std::span<const std::byte> mapped) -> TlsSlot {
  TlsSlot slot;
  if (!image.tls.present) {
    return slot;
  }
  const std::uint64_t raw_size =
      image.tls.end_address_of_raw_data - image.tls.start_address_of_raw_data;
  const std::size_t block_size =
      static_cast<std::size_t>(raw_size) + image.tls.size_of_zero_fill;
  if (block_size == 0) {
    return slot;
  }

  const auto kernel32_base = remote_module_base(process, "kernel32.dll");
  if (!kernel32_base) {
    return slot;
  }
  const auto tls_alloc = resolve_export(process, *kernel32_base, "TlsAlloc", 0, false);
  if (!tls_alloc) {
    return slot;
  }
  DWORD index = TLS_OUT_OF_INDEXES;
  {
    const cs2::core::ScopedKernelHandle thread(
        CreateRemoteThread(process, nullptr, 0,
                           reinterpret_cast<LPTHREAD_START_ROUTINE>(*tls_alloc), nullptr, 0,
                           nullptr));
    if (!thread.valid()) {
      return slot;
    }
    WaitForSingleObject(static_cast<HANDLE>(thread.get()), 5000);
    GetExitCodeThread(static_cast<HANDLE>(thread.get()), &index);
  }  // the thread handle is released here, as before
  if (index == TLS_OUT_OF_INDEXES) {
    return slot;
  }

  const void* block =
      VirtualAllocEx(process, nullptr, block_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (block == nullptr) {
    return slot;
  }
  std::vector<std::byte> local(block_size, std::byte{0});
  const auto raw_offset =
      static_cast<std::size_t>(image.tls.start_address_of_raw_data - image.image_base);
  if (raw_size != 0 && raw_offset + raw_size <= mapped.size()) {
    std::memcpy(local.data(), mapped.data() + raw_offset, static_cast<std::size_t>(raw_size));
  }
  if (!write_remote(process, reinterpret_cast<std::uintptr_t>(block), local.data(), block_size)) {
    return slot;
  }

  const std::uint32_t index_value = index;
  write_u32(process, remote_base + (image.tls.address_of_index - image.image_base), index_value);

  slot.ok = true;
  slot.index = index;
  slot.block = reinterpret_cast<std::uintptr_t>(block);
  return slot;
}

/// Return the target's base for `name`, loading it into the target if needed
/// (via a remote `LoadLibraryW` call).
auto ensure_module(HANDLE process, const std::string& name) -> std::optional<std::uintptr_t> {
  if (const auto base = remote_module_base(process, name.c_str())) {
    return base;
  }
  const auto kernel32_base = remote_module_base(process, "kernel32.dll");
  if (!kernel32_base) {
    return std::nullopt;
  }
  const auto load_library = resolve_export(process, *kernel32_base, "LoadLibraryW", 0, false);
  if (!load_library) {
    return std::nullopt;
  }
  const std::wstring wide_name(name.begin(), name.end());
  const std::size_t bytes = (wide_name.size() + 1) * sizeof(wchar_t);
  void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (memory == nullptr) {
    return std::nullopt;
  }
  if (!write_remote(process, reinterpret_cast<std::uintptr_t>(memory), wide_name.c_str(), bytes)) {
    return std::nullopt;
  }
  {
    const cs2::core::ScopedKernelHandle thread(
        CreateRemoteThread(process, nullptr, 0,
                           reinterpret_cast<LPTHREAD_START_ROUTINE>(*load_library), memory, 0,
                           nullptr));
    if (!thread.valid()) {
      return std::nullopt;
    }
    WaitForSingleObject(static_cast<HANDLE>(thread.get()), 10000);
  }  // the thread handle is released here, as before
  // Re-enumerate: the returned HMODULE may exceed the 32-bit thread exit code.
  return remote_module_base(process, name.c_str());
}

/// Map a PE import module name onto the DLL that actually backs its exports.
///
/// User-mode code compiled with the UCRT links against the **API-set** stubs
/// `api-ms-win-crt-*.dll`. Those are virtual: the loader resolves them to a
/// host DLL (the UCRT lives in `ucrtbase.dll`), but they never appear in the
/// target's module list, so a plain module lookup fails. Likewise the
/// `api-ms-win-core-*` sets are backed by `kernelbase.dll`. Rewrite the name to
/// the host module so import resolution walks a real export table.
auto resolve_import_module(const std::string& name) -> std::string {
  std::string lower = name;
  std::ranges::transform(lower, lower.begin(), [](unsigned char c) -> char {
    return static_cast<char>(std::tolower(c));
  });
  if (lower.starts_with("api-ms-win-crt-") || lower.starts_with("ext-ms-win-crt-")) {
    return "ucrtbase.dll";
  }
  if (lower.starts_with("api-ms-win-core-") || lower.starts_with("ext-ms-win-core-") ||
      lower.starts_with("api-ms-win-service-")) {
    return "kernelbase.dll";
  }
  return name;
}

/// True when running under Wine/Proton. Native Windows has no `wine_get_version`
/// export in ntdll; the synthetic loader-list entry (register_module) is needed
/// there under Wine but faults `LdrpInitializeThread` on native Windows.
auto running_under_wine() -> bool {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

auto read_file(const char* path) -> std::vector<std::byte> {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::vector<char> chars((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::memcpy(out.data(), chars.data(), chars.size());
  return out;
}

/// x64 stub that (optionally) sets the thread's TLS slot directly via
/// `gs:[0x58]`, then calls `entry(base, DLL_PROCESS_ATTACH, 0)` and returns 0.
auto make_entry_stub(std::uintptr_t base, std::uintptr_t entry, bool set_tls,
                     std::uint32_t tls_index, std::uintptr_t tls_block) -> std::vector<std::byte> {
  std::vector<std::byte> code;
  const auto push = [&](std::initializer_list<std::uint8_t> bytes) -> void {
    for (std::uint8_t b : bytes) {
      code.push_back(static_cast<std::byte>(b));
    }
  };
  const auto push_imm64 = [&](std::uintptr_t value) -> void {
    for (int i = 0; i < 8; ++i) {
      code.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
  };

  if (set_tls) {
    push({0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00});  // mov rax, gs:[0x58]
    push({0x48, 0xB9});                                            // mov rcx, tls_index
    push_imm64(tls_index);
    push({0x48, 0xBA});  // mov rdx, tls_block
    push_imm64(tls_block);
    push({0x48, 0x89, 0x14, 0xC8});  // mov [rax+rcx*8], rdx
  }

  push({0x48, 0xB9});  // mov rcx, base
  push_imm64(base);
  push({0xBA, 0x01, 0x00, 0x00, 0x00});  // mov edx, 1  (DLL_PROCESS_ATTACH)
  push({0x45, 0x33, 0xC0});              // xor r8d, r8d
  push({0x48, 0xB8});                    // mov rax, entry
  push_imm64(entry);
  push({0x48, 0x83, 0xEC, 0x28});  // sub rsp, 0x28
  push({0xFF, 0xD0});              // call rax
  push({0x48, 0x83, 0xC4, 0x28});  // add rsp, 0x28
  push({0x33, 0xC0});              // xor eax, eax
  push({0xC3});                    // ret
  return code;
}

/// x64 stub that calls `target(a0, a1, a2)` (SysV->Win64: rcx/rdx/r8) and
/// returns its result in eax (used for RtlAddFunctionTable).
auto make_call3_stub(std::uintptr_t target, std::uintptr_t a0, std::uint32_t a1, std::uintptr_t a2)
    -> std::vector<std::byte> {
  std::vector<std::byte> code;
  const auto push = [&](std::initializer_list<std::uint8_t> bytes) -> void {
    for (std::uint8_t b : bytes) {
      code.push_back(static_cast<std::byte>(b));
    }
  };
  const auto push_imm64 = [&](std::uintptr_t value) -> void {
    for (int i = 0; i < 8; ++i) {
      code.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
  };
  push({0x48, 0xB9});  // mov rcx, a0
  push_imm64(a0);
  push({0xBA});  // mov edx, a1
  for (int i = 0; i < 4; ++i) {
    code.push_back(static_cast<std::byte>((a1 >> (8 * i)) & 0xFF));
  }
  push({0x49, 0xB8});  // mov r8, a2
  push_imm64(a2);
  push({0x48, 0xB8});  // mov rax, target
  push_imm64(target);
  push({0x48, 0x83, 0xEC, 0x28});  // sub rsp, 0x28
  push({0xFF, 0xD0});              // call rax
  push({0x48, 0x83, 0xC4, 0x28});  // add rsp, 0x28
  push({0xC3});                    // ret  (result in eax)
  return code;
}

/// Register the image's `.pdata` unwind tables in the target via
/// `RtlAddFunctionTable`, so exceptions/SEH inside the mapped payload unwind
/// correctly. Returns true if the target reported success.
auto register_exception_table(HANDLE process, std::uintptr_t remote_base,
                              const cs2::loader::pe::ImageInfo& image) -> bool {
  if (image.exception_directory_rva == 0 || image.exception_directory_size < 12) {
    return false;
  }
  const auto ntdll = remote_module_base(process, "ntdll.dll");
  if (!ntdll) {
    return false;
  }
  const auto add_function_table = resolve_export(process, *ntdll, "RtlAddFunctionTable", 0, false);
  if (!add_function_table) {
    return false;
  }

  const std::uint32_t count = image.exception_directory_size / 12;  // sizeof(RUNTIME_FUNCTION)
  const std::vector<std::byte> stub =
      make_call3_stub(*add_function_table, remote_base + image.exception_directory_rva, count,
                      remote_base);
  void* memory =
      VirtualAllocEx(process, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (memory == nullptr ||
      !write_remote(process, reinterpret_cast<std::uintptr_t>(memory), stub.data(), stub.size())) {
    return false;
  }
  DWORD exit_code = 0;
  {
    const cs2::core::ScopedKernelHandle thread(
        CreateRemoteThread(process, nullptr, 0,
                           reinterpret_cast<LPTHREAD_START_ROUTINE>(memory), nullptr, 0, nullptr));
    if (!thread.valid()) {
      return false;
    }
    WaitForSingleObject(static_cast<HANDLE>(thread.get()), 5000);
    GetExitCodeThread(static_cast<HANDLE>(thread.get()), &exit_code);
  }  // the thread handle is released here, as before
  return exit_code != 0;
}

void print_usage() {
  std::puts("usage: injector.exe <process-name|pid> <payload.dll> [--no-call] [--load-missing] [--register|--no-register]");
  std::puts("       injector.exe <process-name|pid> <ignored> --modules   # list target modules");
  std::puts("       injector.exe --procs                                   # list processes");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc > 1 && std::strcmp(argv[1], "--procs") == 0) {
    const cs2::core::ScopedKernelHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
      std::printf("injector: process snapshot FAILED (err %lu)\n", GetLastError());
      return 1;
    }
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Process32First(static_cast<HANDLE>(snapshot.get()), &entry) != 0) {
      do {
        std::printf("  pid %-6u ppid %-6u %s\n", entry.th32ProcessID, entry.th32ParentProcessID,
                    entry.szExeFile);
      } while (Process32Next(static_cast<HANDLE>(snapshot.get()), &entry) != 0);
    }
    return 0;
  }
  if (argc < 3) {
    print_usage();
    return 2;
  }
  const std::string target = argv[1];
  const std::string dll_path = argv[2];
  bool call_entry = true;
  bool load_missing = false;
  bool register_loader = running_under_wine();
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--no-call") == 0) {
      call_entry = false;
    }
    if (std::strcmp(argv[i], "--load-missing") == 0) {
      load_missing = true;
    }
    if (std::strcmp(argv[i], "--register") == 0) {
      register_loader = true;
    }
    if (std::strcmp(argv[i], "--no-register") == 0) {
      register_loader = false;
    }
  }

  std::uint32_t pid = 0;
  if (target.find_first_not_of("0123456789") == std::string::npos) {
    pid = static_cast<std::uint32_t>(std::stoul(target));
  } else {
    pid = find_pid(target.c_str());
  }
  if (pid == 0) {
    std::printf("injector: process %s not found\n", target.c_str());
    return 1;
  }

  HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                                   PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
                               FALSE, pid);
  if (process == nullptr) {
    std::printf("injector: OpenProcess(%u) failed: %lu\n", pid, GetLastError());
    return 1;
  }
  std::printf("injector: target pid %u\n", pid);

  if (argc > 3 && std::strcmp(argv[3], "--modules") == 0) {
    dump_target_modules(process);
    return 0;
  }

  const std::vector<std::byte> raw = read_file(dll_path.c_str());
  if (raw.empty()) {
    std::printf("injector: cannot read %s\n", dll_path.c_str());
    return 1;
  }

  auto parsed = cs2::loader::pe::parse_image(raw);
  if (!parsed) {
    std::printf("injector: PE parse failed: %s\n", cs2::loader::pe::to_string(parsed.error()));
    return 1;
  }
  const cs2::loader::pe::ImageInfo& image = *parsed;

  const void* remote = VirtualAllocEx(process, nullptr, image.size_of_image,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (remote == nullptr) {
    std::printf("injector: VirtualAllocEx failed: %lu\n", GetLastError());
    return 1;
  }
  const auto remote_base = reinterpret_cast<std::uintptr_t>(remote);
  std::printf("injector: mapped image at 0x%llx (size 0x%x)\n",
              static_cast<unsigned long long>(remote_base), image.size_of_image);

  // Map sections + relocations locally, then resolve imports and write once.
  cs2::loader::mapping::BufferSink sink(remote_base, image.size_of_image);
  if (auto result = cs2::loader::mapping::map_image(image, raw, sink); !result) {
    std::printf("injector: mapping failed: %s\n",
                cs2::loader::mapping::to_string(result.error()));
    return 1;
  }

  auto* bytes = sink.view().data();
  for (const auto& module : image.imports) {
    // API-set stubs (api-ms-win-crt-*) are virtual; resolve them against the
    // host DLL that backs the exports (see resolve_import_module).
    const std::string host_name = resolve_import_module(module.name);
    std::optional<std::uintptr_t> module_base = remote_module_base(process, host_name.c_str());
    if (!module_base && load_missing) {
      module_base = ensure_module(process, host_name);
    }
    if (!module_base) {
      std::printf("injector: import module %s not found in target\n", module.name.c_str());
      return 1;
    }
    for (const auto& symbol : module.symbols) {
      const auto resolved = resolve_export(process, *module_base, symbol.name, symbol.ordinal,
                                           symbol.by_ordinal);
      if (!resolved) {
        std::printf("injector: unresolved import %s!%s\n", module.name.c_str(),
                    symbol.by_ordinal ? "(ordinal)" : symbol.name.c_str());
        return 1;
      }
      const std::uint64_t value = *resolved;
      std::memcpy(bytes + symbol.iat_rva, &value, sizeof(value));
    }
    if (host_name != module.name) {
      std::printf("injector: resolved %zu import(s) from %s (via %s)\n", module.symbols.size(),
                  module.name.c_str(), host_name.c_str());
    } else {
      std::printf("injector: resolved %zu import(s) from %s\n", module.symbols.size(),
                  module.name.c_str());
    }
  }

  if (!write_remote(process, remote_base, sink.bytes().data(), image.size_of_image)) {
    std::printf("injector: WriteProcessMemory failed: %lu\n", GetLastError());
    return 1;
  }

  // Register the image in the target's PEB loader list so loader-dependent CRT
  // init can find it. Under Wine/Proton this is required; on native Windows the
  // synthetic (partially populated) LDR_DATA_TABLE_ENTRY faults
  // LdrpInitializeThread when a thread attaches to the process, so it is skipped
  // by default there. Override with --register / --no-register.
  if (register_loader) {
    const std::size_t slash = dll_path.find_last_of("\\/");
    const std::string base_name = slash == std::string::npos ? dll_path : dll_path.substr(slash + 1);
    const std::wstring wide_name(base_name.begin(), base_name.end());
    if (register_module(process, remote_base, image.size_of_image, image.entry_point_rva,
                        wide_name.c_str())) {
      std::printf("injector: registered %s in the loader list\n", base_name.c_str());
    } else {
      std::puts("injector: warning: could not register module in the loader list");
    }
  } else {
    std::puts("injector: skipping loader-list registration (--no-register)");
  }

  // Register the image's exception (.pdata) tables so exceptions/SEH inside the
  // manually-mapped payload unwind instead of crashing the process.
  if (image.exception_directory_size >= 12) {
    if (register_exception_table(process, remote_base, image)) {
      std::printf("injector: registered .pdata (%u function entries)\n",
                  image.exception_directory_size / 12);
    } else {
      std::puts("injector: warning: could not register .pdata");
    }
  }

  // TLS: allocate a slot in the target and initialize the image's TLS block.
  const TlsSlot tls = setup_tls(process, remote_base, image, sink.bytes());
  if (image.tls.present) {
    if (tls.ok) {
      std::printf("injector: TLS slot %u initialized\n", tls.index);
    } else {
      std::puts("injector: warning: TLS setup failed");
    }
  }

  if (!call_entry) {
    std::puts("injector: image written (--no-call; entry not invoked)");
    return 0;
  }

  const std::uintptr_t entry = remote_base + image.entry_point_rva;
  const std::vector<std::byte> stub =
      make_entry_stub(remote_base, entry, tls.ok, tls.index, tls.block);
  void* stub_mem = VirtualAllocEx(process, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
  if (stub_mem == nullptr || !write_remote(process, reinterpret_cast<std::uintptr_t>(stub_mem),
                                           stub.data(), stub.size())) {
    std::printf("injector: stub setup failed: %lu\n", GetLastError());
    return 1;
  }

  DWORD exit_code = 0;
  {
    const cs2::core::ScopedKernelHandle thread(
        CreateRemoteThread(process, nullptr, 0,
                           reinterpret_cast<LPTHREAD_START_ROUTINE>(stub_mem), nullptr, 0, nullptr));
    if (!thread.valid()) {
      std::printf("injector: CreateRemoteThread failed: %lu\n", GetLastError());
      return 1;
    }
    WaitForSingleObject(static_cast<HANDLE>(thread.get()), 15000);
    GetExitCodeThread(static_cast<HANDLE>(thread.get()), &exit_code);
  }  // the thread handle is released here, as before
  std::printf("injector: entry returned %lu\n", exit_code);
  return 0;
}
