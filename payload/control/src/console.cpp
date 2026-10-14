#include "cs2/control/console.hpp"

#include <cstdint>
#include <string>

#include "cs2/game/module.hpp"
#include "offsets.hpp"

namespace cs2::control {
namespace {

using namespace cs2::sdk;

constexpr std::string_view k_engine_module = "engine2.dll";

}  // namespace

void Console::refresh() noexcept {
  if (add_text_ != nullptr) {
    return;
  }
  const cs2::game::ModuleInfo module = cs2::game::find_module(k_engine_module);
  if (!module.valid()) {
    return;
  }
  const OffsetInfo* add_text = find_offset("cbuf_add_text");
  const OffsetInfo* input_service = find_offset("input_service");
  if (add_text == nullptr || input_service == nullptr) {
    return;
  }
  if (add_text->module != k_engine_module || input_service->module != k_engine_module) {
    return;
  }
  const std::uintptr_t base = module.base;
  add_text_ = reinterpret_cast<add_text_fn>(base + add_text->rva);
  input_service_ = reinterpret_cast<void*>(base + input_service->rva);
}

bool Console::exec(std::string_view line) noexcept {
  if (add_text_ == nullptr || input_service_ == nullptr) {
    return false;
  }
  // The engine expects a NUL-terminated line.
  const std::string text(line);
  add_text_(input_service_, k_slot, text.c_str(), 0);
  return true;
}

}  // namespace cs2::control
