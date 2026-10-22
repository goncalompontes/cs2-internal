// Read PE data and exercise patch/restore on our private mapping. Never call engine code.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "MinHook.h"
#include "cs2/control/command_choke.hpp"
#include "cs2/control/move_clock_probe.hpp"
#include "cs2/loader/mapping.hpp"
#include "cs2/loader/pe.hpp"
namespace {
cs2::control::MoveClockSnapshot read_empty(void*, void*) noexcept {
  return {};
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) return 2;
  const auto size = file.tellg();
  file.seekg(0);
  std::vector<std::byte> raw(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(raw.data()), size);
  const auto image = cs2::loader::pe::parse_image(raw);
  if (!image) return 1;
  void* memory =
      VirtualAlloc(nullptr, image->size_of_image, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory) return 1;
  cs2::loader::mapping::BufferSink sink(reinterpret_cast<std::uintptr_t>(memory),
                                        image->size_of_image);
  if (!cs2::loader::mapping::map_image(*image, raw, sink)) {
    VirtualFree(memory, 0, MEM_RELEASE);
    return 1;
  }
  std::memcpy(memory, sink.bytes().data(), sink.bytes().size());
  const auto binding = cs2::control::resolve_command_tick(
      {reinterpret_cast<std::uintptr_t>(memory), image->size_of_image});
  bool ok = binding.has_value();
  if (binding) {
    std::array<unsigned char, 32> before{};
    std::memcpy(before.data(), reinterpret_cast<void*>(*binding), before.size());
    cs2::control::MoveClockProbe probe;
    ok = probe.install(reinterpret_cast<cs2::control::CommandTickFn>(*binding), &read_empty,
                       nullptr);
    if (!ok && probe.hook_error())
      std::printf("Hook decoder rejected prologue: %s\n",
                  cs2::hooks::to_string(*probe.hook_error()));
    probe.uninstall();
    ok = ok && std::memcmp(before.data(), reinterpret_cast<void*>(*binding), before.size()) == 0;
    auto* gate = reinterpret_cast<unsigned char*>(*binding + 0xd1);
    const auto saved = *gate;
    *gate ^= 1;
    ok = ok && !cs2::control::resolve_command_tick(
                   {reinterpret_cast<std::uintptr_t>(memory), image->size_of_image});
    *gate = saved;
    ok = ok && cs2::control::resolve_command_tick(
                   {reinterpret_cast<std::uintptr_t>(memory), image->size_of_image})
                   .has_value();
    std::printf(
        "Command-tick RVA 0x%llx; object,int ABI; patch/restore %s\n",
        static_cast<unsigned long long>(*binding - reinterpret_cast<std::uintptr_t>(memory)),
        ok ? "PASS" : "FAIL");
  } else
    std::puts("FAIL: no unique verified command-tick binding");
  const cs2::game::ModuleInfo module{reinterpret_cast<std::uintptr_t>(memory),
                                     image->size_of_image};
  const auto send = cs2::control::resolve_command_send(module);
  ok = ok && send.has_value();
  if (send) {
    std::array<unsigned char, 32> bytes{};
    std::memcpy(bytes.data(), reinterpret_cast<void*>(send->build), bytes.size());
    DWORD protection = 0;
    const bool executable =
        VirtualProtect(memory, image->size_of_image, PAGE_EXECUTE_READWRITE, &protection) != 0;
    const auto initialized = MH_Initialize();
    void* trampoline = nullptr;
    void* target = reinterpret_cast<void*>(send->build);
    const bool created =
        executable && (initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) &&
        MH_CreateHook(target, reinterpret_cast<void*>(&read_empty), &trampoline) == MH_OK;
    const bool enabled = created && MH_EnableHook(target) == MH_OK;
    const bool disabled = enabled && MH_DisableHook(target) == MH_OK;
    if (created) MH_RemoveHook(target);
    ok = ok && created && enabled && disabled;
    ok = ok && std::memcmp(bytes.data(), reinterpret_cast<void*>(send->build), bytes.size()) == 0;
    auto* range = reinterpret_cast<unsigned char*>(send->build + 0x1e7);
    const auto saved = *range;
    *range ^= 1;
    ok = ok && !cs2::control::resolve_command_send(module);
    *range = saved;
    ok = ok && cs2::control::resolve_command_send(module).has_value();
    std::printf(
        "Command serializer RVA 0x%llx, stage 0x%llx, sender 0x%llx; patch/restore + rejection "
        "%s\n",
        static_cast<unsigned long long>(send->build - module.base),
        static_cast<unsigned long long>(send->stage - module.base),
        static_cast<unsigned long long>(send->send - module.base), ok ? "PASS" : "FAIL");
  } else
    std::puts("FAIL: no verified command serializer binding");
  VirtualFree(memory, 0, MEM_RELEASE);
  if (ok) std::puts("PASS: engine PE was mapped as data; no engine function was executed");
  return ok ? 0 : 1;
}
