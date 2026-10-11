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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cs2/loader/mapping.hpp"
#include "cs2/loader/pe.hpp"

namespace {

using Bytes = std::span<const std::byte>;

std::uint16_t rd16(const std::byte* p) {
  std::uint16_t v = 0;
  std::memcpy(&v, p, 2);
  return v;
}

std::uint32_t rd32(const std::byte* p) {
  std::uint32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

std::uint32_t find_pid(const char* name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }
  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  std::uint32_t pid = 0;
  if (Process32First(snapshot, &entry)) {
    do {
      if (_stricmp(entry.szExeFile, name) == 0) {
        pid = entry.th32ProcessID;
        break;
      }
    } while (Process32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return pid;
}

std::optional<std::uintptr_t> remote_module_base(HANDLE process, const char* name) {
  const DWORD pid = GetProcessId(process);
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  MODULEENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  std::optional<std::uintptr_t> base;
  if (Module32First(snapshot, &entry)) {
    do {
      if (_stricmp(entry.szModule, name) == 0) {
        base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        break;
      }
    } while (Module32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return base;
}

bool read_remote(HANDLE process, std::uintptr_t address, void* buffer, std::size_t size) {
  SIZE_T got = 0;
  return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &got) != 0 &&
         got == size;
}

bool write_remote(HANDLE process, std::uintptr_t address, const void* buffer, std::size_t size) {
  SIZE_T wrote = 0;
  return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size, &wrote) != 0 &&
         wrote == size;
}

std::string read_remote_string(HANDLE process, std::uintptr_t address, std::size_t max) {
  std::vector<char> buffer(max, '\0');
  SIZE_T got = 0;
  if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer.data(), max, &got)) {
    return {};
  }
  return std::string(buffer.data());  // stops at NUL
}

std::uint64_t read_u64(HANDLE process, std::uintptr_t address) {
  std::uint64_t value = 0;
  read_remote(process, address, &value, sizeof(value));
  return value;
}

bool write_u16(HANDLE process, std::uintptr_t address, std::uint16_t value) {
  return write_remote(process, address, &value, sizeof(value));
}

bool write_u32(HANDLE process, std::uintptr_t address, std::uint32_t value) {
  return write_remote(process, address, &value, sizeof(value));
}

bool write_u64(HANDLE process, std::uintptr_t address, std::uint64_t value) {
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

/// Insert an LDR_DATA_TABLE_ENTRY for the mapped image into the target's PEB
/// loader list, so loader-dependent CRT init (RtlPcToFileHeader,
/// GetModuleHandle, ...) can find it.
bool register_module(HANDLE process, std::uintptr_t base, std::uint32_t size_of_image,
                     std::uint32_t entry_rva, const wchar_t* dll_name) {
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

  const std::uintptr_t peb = reinterpret_cast<std::uintptr_t>(pbi.peb_base_address);
  const std::uint64_t ldr = read_u64(process, peb + k_peb_ldr);
  if (ldr == 0) {
    return false;
  }

  void* memory = VirtualAllocEx(process, nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (memory == nullptr) {
    return false;
  }
  const std::uintptr_t entry = reinterpret_cast<std::uintptr_t>(memory);
  const std::size_t name_length = std::wcslen(dll_name);

  // Name buffers, then the UNICODE_STRINGs that point at them.
  write_remote(process, entry + 0x100, dll_name, (name_length + 1) * sizeof(wchar_t));
  write_remote(process, entry + 0x140, dll_name, (name_length + 1) * sizeof(wchar_t));
  const auto write_unicode_string = [&](std::size_t offset, std::uintptr_t buffer) {
    write_u16(process, entry + offset, static_cast<std::uint16_t>(name_length * 2));
    write_u16(process, entry + offset + 2, static_cast<std::uint16_t>((name_length + 1) * 2));
    write_u64(process, entry + offset + 8, buffer);
  };
  write_unicode_string(k_ldr_base_name, entry + 0x100);
  write_unicode_string(k_ldr_full_name, entry + 0x140);

  write_u64(process, entry + k_ldr_dll_base, base);
  write_u64(process, entry + k_ldr_entry_point, base + entry_rva);
  write_u32(process, entry + k_ldr_size_of_image, size_of_image);

  const auto link = [&](std::size_t list_offset, std::size_t links_offset) {
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
std::optional<std::uintptr_t> resolve_export(HANDLE process, std::uintptr_t module_base,
                                             const std::string& name, std::uint16_t ordinal,
                                             bool by_ordinal, int depth = 0) {
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
    if (dll.find(".dll") == std::string::npos && dll.find(".DLL") == std::string::npos) {
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
TlsSlot setup_tls(HANDLE process, std::uintptr_t remote_base,
                  const cs2::loader::pe::ImageInfo& image, std::span<const std::byte> mapped) {
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
  HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                     reinterpret_cast<LPTHREAD_START_ROUTINE>(*tls_alloc), nullptr,
                                     0, nullptr);
  if (thread == nullptr) {
    return slot;
  }
  WaitForSingleObject(thread, 5000);
  DWORD index = TLS_OUT_OF_INDEXES;
  GetExitCodeThread(thread, &index);
  CloseHandle(thread);
  if (index == TLS_OUT_OF_INDEXES) {
    return slot;
  }

  void* block = VirtualAllocEx(process, nullptr, block_size, MEM_COMMIT | MEM_RESERVE,
                               PAGE_READWRITE);
  if (block == nullptr) {
    return slot;
  }
  std::vector<std::byte> local(block_size, std::byte{0});
  const std::size_t raw_offset =
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

std::vector<std::byte> read_file(const char* path) {
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
std::vector<std::byte> make_entry_stub(std::uintptr_t base, std::uintptr_t entry, bool set_tls,
                                       std::uint32_t tls_index, std::uintptr_t tls_block) {
  std::vector<std::byte> code;
  const auto push = [&](std::initializer_list<std::uint8_t> bytes) {
    for (std::uint8_t b : bytes) {
      code.push_back(static_cast<std::byte>(b));
    }
  };
  const auto push_imm64 = [&](std::uintptr_t value) {
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

void print_usage() {
  std::puts("usage: injector.exe <process-name|pid> <payload.dll> [--no-call]");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    print_usage();
    return 2;
  }
  const std::string target = argv[1];
  const std::string dll_path = argv[2];
  const bool call_entry = !(argc > 3 && std::strcmp(argv[3], "--no-call") == 0);

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

  void* remote = VirtualAllocEx(process, nullptr, image.size_of_image,
                                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (remote == nullptr) {
    std::printf("injector: VirtualAllocEx failed: %lu\n", GetLastError());
    return 1;
  }
  const std::uintptr_t remote_base = reinterpret_cast<std::uintptr_t>(remote);
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
    const auto module_base = remote_module_base(process, module.name.c_str());
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
    std::printf("injector: resolved %zu import(s) from %s\n", module.symbols.size(),
                module.name.c_str());
  }

  if (!write_remote(process, remote_base, sink.bytes().data(), image.size_of_image)) {
    std::printf("injector: WriteProcessMemory failed: %lu\n", GetLastError());
    return 1;
  }

  // Register the image in the target's PEB loader list so CRT init can find it.
  {
    const std::size_t slash = dll_path.find_last_of("\\/");
    const std::string base_name = slash == std::string::npos ? dll_path : dll_path.substr(slash + 1);
    const std::wstring wide_name(base_name.begin(), base_name.end());
    if (register_module(process, remote_base, image.size_of_image, image.entry_point_rva,
                        wide_name.c_str())) {
      std::printf("injector: registered %s in the loader list\n", base_name.c_str());
    } else {
      std::puts("injector: warning: could not register module in the loader list");
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

  HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                     reinterpret_cast<LPTHREAD_START_ROUTINE>(stub_mem), nullptr,
                                     0, nullptr);
  if (thread == nullptr) {
    std::printf("injector: CreateRemoteThread failed: %lu\n", GetLastError());
    return 1;
  }
  WaitForSingleObject(thread, 15000);
  DWORD exit_code = 0;
  GetExitCodeThread(thread, &exit_code);
  CloseHandle(thread);
  std::printf("injector: entry returned %lu\n", exit_code);
  return 0;
}
