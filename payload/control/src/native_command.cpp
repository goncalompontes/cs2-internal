#include "cs2/control/native_command.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/pattern.hpp"
#include "cs2/game/log.hpp"
#include "offsets.hpp"
#include <cmath>
#include <cstring>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace cs2::control {
namespace {
bool mutable_range(std::uintptr_t address, std::size_t size) noexcept {
  if (!address || !size || address + size < address) return false;
#if defined(_WIN32)
  for (auto at = address; at < address + size;) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        info.Protect & (PAGE_GUARD | PAGE_NOACCESS) ||
        !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (next <= at) return false;
    at = next;
  }
#endif
  return true;
}
void* arena(std::uintptr_t message) noexcept {
  std::uintptr_t bits = 0;
  if (!game::read(message + 8, bits)) return nullptr;
  const auto pointer = bits & ~std::uintptr_t{3};
  if (bits & 1) {
    std::uintptr_t indirect = 0;
    if (!game::read(pointer, indirect)) return nullptr;
    return reinterpret_cast<void*>(indirect);
  }
  return reinterpret_cast<void*>(pointer);
}
std::uintptr_t relative_call(game::ModuleInfo module, std::uintptr_t site) noexcept {
  std::uint8_t opcode = 0;
  std::int32_t relative = 0;
  if (!site || !game::read(site, opcode) || opcode != 0xe8 || !game::read(site + 1, relative)) return 0;
  const auto target = site + 5 + relative;
  return module.contains(target) ? target : 0;
}
bool assign(const NativeCommandApi* api, std::uintptr_t* field, const EngineStringView* text,
            void* owner) noexcept {
#if defined(_WIN32)
  __try {
#endif
    api->assign_crc(field, text, owner);
    return true;
#if defined(_WIN32)
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#endif
}
}
MoveCrcBytes encode_move_crc(const std::uint64_t buttons[3], const float angles[3]) noexcept {
  MoveCrcBytes out;
  const auto byte = [&out](unsigned value) { out.data[out.size++] = static_cast<std::byte>(value); };
  unsigned count = 0;
  for (int i = 0; i < 3; ++i) if (buttons[i]) count += 9;
  if (count) {
    byte(0x1a); byte(count);
    for (int i = 0; i < 3; ++i) if (buttons[i]) {
      byte(0x09 + 8 * i);
      std::memcpy(out.data.data() + out.size, buttons + i, 8); out.size += 8;
    }
  }
  count = 0;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(angles[i])) return {};
    if (angles[i] != 0) count += 5;
  }
  if (count) {
    byte(0x22); byte(count);
    for (int i = 0; i < 3; ++i) if (angles[i] != 0) {
      byte(0x0d + 8 * i);
      std::memcpy(out.data.data() + out.size, angles + i, 4); out.size += 4;
    }
  }
  return out;
}
bool NativeCommandBridge::initialize(game::ModuleInfo module) noexcept {
  if (ready()) return true;
  if (!module.valid() || attempted_base_ == module.base) return false;
  attempted_base_ = module.base;
  const auto base_site = game::unique_code_match(module, "4883EC28E8????????8B8010590000");
  const auto view_site = game::unique_code_match(module, "8B0D????????8BD3E8????????F20F1000");
  const auto table_site = game::unique_code_match(module, "FFFFFFFF488D05????????48890D????????");
  const auto alloc_site = game::unique_code_match(module, "488B54CA088D4101894708EB16488B0FE8????????488BD0488BCF");
  const auto assign_site = game::unique_code_match(module, "48895C24??5556574883EC30498BC0488BFA488BF1488B09F6C103");
  const auto set_site = game::unique_code_match(module, "85D275??486381");
  const auto* command = sdk::find_offset("get_user_cmd_by_sequence");
  if (!base_site || !view_site || !table_site || !alloc_site || !assign_site || !set_site || !command) {
    game::append_log("[native-input] command bridge signatures unavailable"); return false;
  }
  const auto get_base = relative_call(module, base_site + 4);
  const auto get_view = relative_call(module, view_site + 8);
  const auto step_new = relative_call(module, alloc_site + 16);
  std::int32_t displacement = 0;
  std::uintptr_t handler = 0;
  if (!get_base || !get_view || !step_new || !game::read(table_site + 7, displacement)) return false;
  const auto table = table_site + 11 + displacement;
  if (!module.contains(table + 0x40) || !game::read(table + 0x40, handler) || !module.contains(handler)) return false;
  // Confirm the preceding constructor is specifically the 0x30-byte button PB.
  const auto button_new = step_new - 0x80;
  std::array<unsigned char, 0x80> constructor{};
  bool button_type = false;
  if (module.contains(button_new) && game::read_bytes(button_new, constructor.data(), constructor.size())) {
    const unsigned char prefix[] = {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,
      0x33,0xff,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0,0xb9,0x30,0,0,0};
    bool layout = true;
    for (std::size_t i = 0; i < sizeof(prefix); ++i)
      if (i != 19 && constructor[i] != prefix[i]) layout = false;
    for (std::size_t i = 0; layout && i + 10 <= constructor.size(); ++i) {
      if (constructor[i] != 0x48 || constructor[i+1] != 0x8d || constructor[i+2] != 0x0d ||
          constructor[i+7] != 0x48 || constructor[i+8] != 0x89 || constructor[i+9] != 0x08) continue;
      std::int32_t relative = 0; std::memcpy(&relative, constructor.data() + i + 3, 4);
      const auto vtable = button_new + i + 7 + relative;
      std::uintptr_t locator = 0; std::uint32_t type_rva = 0;
      if (module.contains(vtable) && game::read(vtable - 8, locator) && module.contains(locator) &&
          game::read(locator + 0xc, type_rva) && module.contains(module.base + type_rva) &&
          game::read_cstring(module.base + type_rva + 0x10).find("CInButtonStatePB") != std::string::npos)
        button_type = true;
    }
  }
  if (!button_type) { game::append_log("[native-input] button allocator RTTI/layout not verified"); return false; }
  api_ = {reinterpret_cast<decltype(api_.command_base)>(get_base),
          reinterpret_cast<decltype(api_.command)>(module.base + command->rva),
          reinterpret_cast<decltype(api_.assign_crc)>(assign_site),
          reinterpret_cast<decltype(api_.allocate_buttons)>(button_new),
          reinterpret_cast<decltype(api_.get_view)>(get_view),
          reinterpret_cast<decltype(api_.set_view)>(set_site), reinterpret_cast<void*>(handler)};
  game::append_log("[native-input] Velocity-style command lookup, move_crc and view bridge bound");
  return true;
}
void* NativeCommandBridge::current(void* controller) const noexcept {
  if (!ready() || !controller) return nullptr;
  auto* base = api_.command_base(controller);
  std::uint32_t sequence = 0;
  if (!base || !game::read(reinterpret_cast<std::uintptr_t>(base) + 0x5910, sequence)) return nullptr;
  return api_.command(controller, sequence);
}
bool NativeCommandBridge::finalize(void* command) const noexcept {
  if (!ready() || !command) return false;
  const auto cmd = reinterpret_cast<std::uintptr_t>(command);
  std::uintptr_t base = 0, buttons = 0, angles = 0;
  std::uint64_t words[3]{}; float values[3]{};
  if (!game::read(cmd + 0x40, base) || !mutable_range(base, 0x48) ||
      !game::read_bytes(cmd + 0x60, words, sizeof(words)) ||
      !game::read(base + 0x40, angles) || !angles || !game::read_bytes(angles + 0x18, values, sizeof(values))) return false;
  for (float value : values) if (!std::isfinite(value)) return false;
  void* owner = arena(base);
  if (!game::read(base + 0x38, buttons)) return false;
  if (!mutable_range(buttons, 0x30)) {
    buttons = reinterpret_cast<std::uintptr_t>(api_.allocate_buttons(owner));
    if (!mutable_range(buttons, 0x30)) return false;
    *reinterpret_cast<std::uintptr_t*>(base + 0x38) = buttons;
  }
  std::memcpy(reinterpret_cast<void*>(buttons + 0x18), words, sizeof(words));
  *reinterpret_cast<std::uint32_t*>(buttons + 0x10) |= 0x7u;
  *reinterpret_cast<std::uint32_t*>(base + 0x10) |= 0x3u;
  const auto payload = encode_move_crc(words, values);
  EngineStringView text;
  text.size = payload.size;
  if (payload.size <= 15) std::memcpy(text.storage.data(), payload.data.data(), payload.size);
  else {
    const auto pointer = payload.data.data();
    std::memcpy(text.storage.data(), &pointer, sizeof(pointer));
    text.capacity = payload.size;
  }
  if (!assign(&api_, reinterpret_cast<std::uintptr_t*>(base + 0x30), &text, owner)) return false;
  *reinterpret_cast<std::uint32_t*>(cmd + 0x20) |= 0x1u;
  return true;
}
bool NativeCommandBridge::view(void* input, game::Vec3& out) const noexcept {
  if (!ready()) return false;
  const auto value = api_.get_view(input, 0);
  return value && game::read(reinterpret_cast<std::uintptr_t>(value), out) &&
      std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
void NativeCommandBridge::restore_view(void* input, const game::Vec3& view) const noexcept {
  if (ready()) api_.set_view(input, 0, &view);
}
}
