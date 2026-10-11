#pragma once

// Inline (prologue) hooking for x64. Replaces the first whole instructions of
// `target` with a jump to `detour`, and builds a trampoline holding the copied
// prologue + a jump back, so the detour can call the original via `original()`.
//
// Conservative: bails out on relative branches in the prologue and on
// RIP-relative displacements that cannot be represented after relocation.

#include <cstddef>
#include <optional>

namespace cs2::hooks {

enum class InlineError {
  too_short,
  unsupported_instruction,
  allocation_failed,
  protect_failed,
};

[[nodiscard]] const char* to_string(InlineError error) noexcept;

class InlineHook {
 public:
  InlineHook() = default;
  ~InlineHook();

  InlineHook(const InlineHook&) = delete;
  InlineHook& operator=(const InlineHook&) = delete;
  InlineHook(InlineHook&&) = delete;
  InlineHook& operator=(InlineHook&&) = delete;

  // Install the hook. On success `original()` is a callable trampoline.
  [[nodiscard]] std::optional<InlineError> install(void* target, void* detour) noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] void* original() const noexcept { return trampoline_; }

 private:
  void* target_ = nullptr;
  void* trampoline_ = nullptr;
  std::size_t trampoline_size_ = 0;
  std::size_t patched_size_ = 0;
  unsigned char saved_[16]{};
  bool installed_ = false;
};

}  // namespace cs2::hooks
