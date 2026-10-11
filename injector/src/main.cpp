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

/// Resolve an exported symbol in a loaded module by reading its export table
/// out of the target process.
std::optional<std::uintptr_t> resolve_export(HANDLE process, std::uintptr_t module_base,
                                             const std::string& name, std::uint16_t ordinal,
                                             bool by_ordinal) {
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

  const auto function_at = [&](std::uint32_t index) -> std::optional<std::uintptr_t> {
    std::byte entry[4];
    if (!read_remote(process, module_base + address_of_functions + index * 4, entry, 4)) {
      return std::nullopt;
    }
    return module_base + rd32(entry);
  };

  if (by_ordinal) {
    if (ordinal < ordinal_base) {
      return std::nullopt;
    }
    return function_at(ordinal - ordinal_base);
  }

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
    return function_at(rd16(ord_index));
  }
  return std::nullopt;
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

/// x64 stub that calls `entry(base, DLL_PROCESS_ATTACH, 0)` and returns 0.
std::vector<std::byte> make_entry_stub(std::uintptr_t base, std::uintptr_t entry) {
  std::vector<std::byte> code;
  const auto push = [&](std::initializer_list<std::uint8_t> bytes) {
    for (std::uint8_t b : bytes) {
      code.push_back(static_cast<std::byte>(b));
    }
  };
  push({0x48, 0xB9});  // mov rcx, base
  for (int i = 0; i < 8; ++i) code.push_back(static_cast<std::byte>((base >> (8 * i)) & 0xFF));
  push({0xBA, 0x01, 0x00, 0x00, 0x00});          // mov edx, 1  (DLL_PROCESS_ATTACH)
  push({0x45, 0x33, 0xC0});                       // xor r8d, r8d
  push({0x48, 0xB8});                             // mov rax, entry
  for (int i = 0; i < 8; ++i) code.push_back(static_cast<std::byte>((entry >> (8 * i)) & 0xFF));
  push({0x48, 0x83, 0xEC, 0x28});                 // sub rsp, 0x28
  push({0xFF, 0xD0});                             // call rax
  push({0x48, 0x83, 0xC4, 0x28});                 // add rsp, 0x28
  push({0x33, 0xC0});                             // xor eax, eax
  push({0xC3});                                   // ret
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

  if (!call_entry) {
    std::puts("injector: image written (--no-call; entry not invoked)");
    return 0;
  }

  const std::uintptr_t entry = remote_base + image.entry_point_rva;
  const std::vector<std::byte> stub = make_entry_stub(remote_base, entry);
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
