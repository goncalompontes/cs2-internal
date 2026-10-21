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
// Publishing the null before the teardown closes the window for every detour
// that **enters** after the null — but a detour that **already holds** the
// pointer can still call it. So the trampoline's pages are **never unmapped**:
// the trampoline is **retired** at the uninstall (the pointer is published as
// null and dropped) and its pages stay mapped — and executable — until the
// process exits.
//
// The old order freed the trampoline **before** it nulled the pointer: a
// detour that had already read `trampoline_` held a **dangling** pointer and
// called **freed executable memory**. The deferred free closed that window
// for the **uninstall**; keeping the pages mapped closes it for the
// **destruction** as well, because there is no point in the hook's life at
// which the pages can be released: the engine cannot know whether a detour
// is still inside the hook — the detour is the **caller's** function, and the
// engine never sees it enter or leave.
//
// The cost is a bounded leak — **one page per uninstall** — and the operating
// system reclaims it when the process exits. For the chams that is one page
// per switch-off of the feature; the other users (the third-person camera,
// the present hook, the animfix) uninstall when the state they hook changes,
// which is a handful of times in a session.
//
// The one sentence: **no thread can execute freed trampoline memory, because
// the trampoline's pages are never unmapped** — the null is published before
// the teardown, and a retired trampoline stays mapped and executable for the
// life of the process.

#include <atomic>
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

  [[nodiscard]] bool installed() const noexcept {
    return installed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] void* original() const noexcept {
    return trampoline_.load(std::memory_order_acquire);
  }

 private:
  void* target_ = nullptr;
  // The published trampoline. `nullptr` when the hook is not installed.
  std::atomic<void*> trampoline_{nullptr};
  std::size_t patched_size_ = 0;
  unsigned char saved_[16]{};
  // Whether the hook is in place.
  std::atomic<bool> installed_{false};
};

}  // namespace cs2::hooks
