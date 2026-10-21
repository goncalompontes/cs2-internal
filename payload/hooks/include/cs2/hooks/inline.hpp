#pragma once

// Inline (prologue) hooking for x64. Replaces the first whole instructions of
// `target` with a jump to `detour`, and builds a trampoline holding the copied
// prologue + a jump back, so the detour can call the original via `original()`.
//
// Conservative: bails out on relative branches in the prologue and on
// RIP-relative displacements that cannot be represented after relocation.
//
// ## The publication order
//
// The hook's state is read by the **game's** threads and written by the
// payload's: the chams' draw detour runs on the scene system's render
// threads (the tier0 thread pool), and it reads `original()` and
// `installed()`. So the two words are **atomics**, and the order they are
// written in is part of the contract:
//
//   * **install**: the trampoline is published **before** the patch, so a
//     detour that runs in the window between the patch and the publication
//     finds `original() == nullptr` and drops the call rather than calling a
//     half-built trampoline. `installed_` comes **after** the patch, so the
//     hook cannot be uninstalled while it is being written.
//   * **uninstall**: `installed_` is cleared **first** (no new caller
//     proceeds), then `trampoline_` is set to `nullptr` — the null is
//     published **before** the trampoline's memory is released — and only
//     then is the trampoline retired.
//
// The old order freed the trampoline **before** it nulled the pointer: a
// detour that had already read `trampoline_` held a **dangling** pointer and
// called **freed executable memory**.
//
// ## The trampoline's lifetime
//
// Publishing the null before the free closes the window for every detour that
// **enters** after the null — but a detour that **already holds** the pointer
// can still call it. So the trampoline's pages are **not** released when the
// hook is uninstalled: the retired trampoline is kept alive (a **deferred
// free**) and its pages are only unmapped when the `InlineHook` itself is
// destroyed. The cost is a bounded leak — one retired trampoline per
// uninstall — and the bound is the number of times the hook is uninstalled
// (for the chams: the number of times the operator switches the feature
// off).

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

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

  [[nodiscard]] bool installed() const noexcept { return installed_.load(std::memory_order_acquire); }
  [[nodiscard]] void* original() const noexcept { return trampoline_.load(std::memory_order_acquire); }

 private:
  // A trampoline that has been uninstalled but whose pages are still mapped.
  //
  // It is kept because a detour that was already inside the hook when the
  // uninstall happened may still call it: the pages must not be unmapped
  // under it. They are released when the hook is destroyed.
  struct Retired {
    void* address = nullptr;
    std::size_t size = 0;
  };

  void* target_ = nullptr;
  // The published trampoline. `nullptr` when the hook is not installed.
  std::atomic<void*> trampoline_{nullptr};
  std::size_t trampoline_size_ = 0;
  std::size_t patched_size_ = 0;
  unsigned char saved_[16]{};
  // Whether the hook is in place.
  std::atomic<bool> installed_{false};
  // The trampolines that were uninstalled and are still mapped.
  std::vector<Retired> retired_;
};

}  // namespace cs2::hooks
