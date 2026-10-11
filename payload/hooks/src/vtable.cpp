#include "cs2/hooks/vtable.hpp"

#include "cs2/hooks/memory.hpp"

namespace cs2::hooks {

VtableHook::VtableHook(void** vtable, std::size_t index, void* detour) noexcept {
  if (vtable == nullptr || detour == nullptr) {
    return;
  }
  slot_ = &vtable[index];
  original_ = *slot_;
  if (!make_writable(slot_, sizeof(void*))) {
    slot_ = nullptr;
    return;
  }
  *slot_ = detour;
  installed_ = true;
}

VtableHook::~VtableHook() {
  if (installed_ && slot_ != nullptr) {
    *slot_ = original_;
  }
}

}  // namespace cs2::hooks
