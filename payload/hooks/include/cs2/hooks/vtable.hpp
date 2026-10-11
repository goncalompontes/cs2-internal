#pragma once

// Virtual-table hooking: replace a slot in an object's vtable with a detour,
// remembering the original so the detour can call through. RAII — restores the
// original slot on destruction.

#include <cstddef>

namespace cs2::hooks {

class VtableHook {
 public:
  // `vtable` points at the first entry (i.e. `*object`); `index` selects the
  // slot; `detour` is the replacement.
  VtableHook(void** vtable, std::size_t index, void* detour) noexcept;
  ~VtableHook();

  VtableHook(const VtableHook&) = delete;
  VtableHook& operator=(const VtableHook&) = delete;
  VtableHook(VtableHook&&) = delete;
  VtableHook& operator=(VtableHook&&) = delete;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] void* original() const noexcept { return original_; }

 private:
  void** slot_ = nullptr;
  void* original_ = nullptr;
  bool installed_ = false;
};

}  // namespace cs2::hooks
