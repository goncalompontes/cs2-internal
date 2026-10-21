#pragma once

// The re-entrancy guard for an inline hook's detour — **without** `thread_local`.
//
// ## Why a `thread_local` cannot guard a detour here
//
// The payload is **manually mapped**: the Windows loader never loads it, so it
// never allocates the per-thread TLS block that a `thread_local` access
// resolves against. `cs2::tls::ensure_thread_tls()` installs that block by
// hand — but only on the threads the payload itself starts (and on the
// injector's entry thread). A detour that runs on **one of the game's own
// threads** — the client's per-frame camera update, for instance — reads a
// `thread_local` whose block was never installed: the block pointer is
// garbage and the read faults **inside the payload** (the crash the operator
// saw; see `docs/re/thirdperson.md`).
//
// The bootstrap cannot repair that either: it treats a non-null slot as
// "already installed", and on such a thread the slot holds garbage rather
// than a block. The only correct fix is to have **no** `thread_local` on that
// path at all.
//
// ## The design
//
// The guard tracks the **owner** of a slot — an opaque token that is unique
// per thread (the OS thread id) — and the owner's **depth**. A call is
// refused only when the *same* owner is already inside: that is the
// recursion the guard exists for (the trampoline landing back in the
// detour). Another thread's call is **never** refused: the camera update can
// run on more than one thread, and a shared counter would silently drop the
// other thread's camera update.
//
// The state is one 64-bit word per slot (the owner in the high half, the
// depth in the low half), so a claim and a release are a single atomic
// compare-exchange: no lock, no allocation, no `thread_local`.
//
// The header is **pure** — no Windows headers, no engine access — so the host
// test drives the whole decision.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cs2::features::guard {

// The owner token: an opaque value that is unique per thread. The payload
// uses the OS thread id; the host test uses its own.
using OwnerId = std::uint32_t;

// How many owners the guard tracks at once.
//
// The camera update runs on one or two threads. Eight slots leave room for the
// others, and the guard **fails open** (a call is allowed, not refused) when
// they are all taken: refusing a call that is not a recursion would drop a
// camera update.
inline constexpr std::size_t SLOT_COUNT = 8;

class RecursionGuard {
 public:
  RecursionGuard() = default;
  RecursionGuard(const RecursionGuard&) = delete;
  RecursionGuard& operator=(const RecursionGuard&) = delete;

  // Try to enter the guarded region as `owner`.
  //
  // `true` — the call may proceed.
  // `false` — the call is **refused**: `owner` is already inside, which is
  // the recursion the guard exists to break.
  [[nodiscard]] auto try_enter(OwnerId owner) noexcept -> bool {
    for (auto& slot : slots_) {
      std::uint64_t state = slot.state.load(std::memory_order_acquire);
      if (depth_of(state) > 0) {
        // The slot is taken. It is the recursion when it is *our* slot.
        if (owner_of(state) == owner) {
          return false;
        }
        continue;
      }
      // The slot is free: claim it for this owner.
      if (slot.state.compare_exchange_weak(state, pack(owner, 1), std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        return true;
      }
      // The exchange failed: another thread claimed the slot first and `state`
      // now holds its value. It cannot be our own slot (the owner token is
      // unique per thread), but the check is cheap and keeps the rule local.
      if (depth_of(state) > 0 && owner_of(state) == owner) {
        return false;
      }
    }
    // Every slot is taken by another owner: **fail open**. Refusing here would
    // drop a camera update on a thread that is not recursing.
    return true;
  }

  // Leave the guarded region. The owner that entered leaves exactly once.
  void leave(OwnerId owner) noexcept {
    for (auto& slot : slots_) {
      std::uint64_t state = slot.state.load(std::memory_order_acquire);
      if (depth_of(state) == 0 || owner_of(state) != owner) {
        continue;
      }
      const std::uint64_t released = pack(owner, depth_of(state) - 1);
      if (slot.state.compare_exchange_weak(state, released, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        return;
      }
      // The exchange failed: the slot changed under us. Only the owner writes
      // its own slot, so this cannot happen; the next slot is tried anyway.
    }
  }

  // The depth `owner` is at: 0 when it is not inside, 1 when it is.
  [[nodiscard]] auto depth(OwnerId owner) const noexcept -> std::uint32_t {
    for (const auto& slot : slots_) {
      const std::uint64_t state = slot.state.load(std::memory_order_acquire);
      if (depth_of(state) > 0 && owner_of(state) == owner) {
        return depth_of(state);
      }
    }
    return 0;
  }

 private:
  // The slot's state: the owner in the high half, the depth in the low half.
  // A depth of zero means the slot is free.
  struct Slot {
    std::atomic<std::uint64_t> state{0};
  };

  static constexpr auto pack(OwnerId owner, std::uint32_t depth) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(owner) << 32) | static_cast<std::uint64_t>(depth);
  }

  static constexpr auto owner_of(std::uint64_t state) noexcept -> OwnerId {
    return static_cast<OwnerId>(state >> 32);
  }

  static constexpr auto depth_of(std::uint64_t state) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(state & 0xffffffffU);
  }

  std::array<Slot, SLOT_COUNT> slots_{};
};

}  // namespace cs2::features::guard
