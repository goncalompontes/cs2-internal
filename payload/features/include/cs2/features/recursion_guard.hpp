#pragma once

// The re-entrancy guard for an inline hook's detour — **without** `thread_local`.
//
// ## Why a `thread_local` cannot guard a detour here
//
// The payload is **manually mapped**: the Windows loader never loads it, so it
// never allocates the per-thread TLS block that a `thread_local` access
// resolves against. `cs2::tls::ensure_thread_tls()` installs that block by
// hand — but only on the threads the payload itself starts (and on the
// injector's entry thread), and only for a TLS index **below** the 64 slots
// the TEB holds inline (`payload/tls/src/bootstrap.cpp:49`). A detour that
// runs on **one of the game's own threads** — the client's per-frame camera
// update, for instance — reads a `thread_local` whose block was never
// installed: the slot holds garbage and the read faults **inside the
// payload** (the crash the operator saw; see `docs/re/thirdperson.md`).
//
// The bootstrap cannot repair that either: on a thread that never had a
// block there is nothing to repair, and the index check refuses the repair
// outright. The only correct fix is to have **no** `thread_local` on that
// path at all.
//
// ## The design
//
// The guard tracks the **owner** of a slot — an opaque token that is unique
// per thread (the OS thread id) — and the owner's **depth**, which is one
// while the owner is inside. A call is refused only when the *same* owner is
// already inside: that is the recursion the guard exists for (the trampoline
// landing back in the detour). Another thread's call is **never** refused:
// the camera update can run on more than one thread, and a shared counter
// would silently drop the other thread's camera update.
//
// The state is one 64-bit word per slot (the owner in the high half, the
// depth in the low half), so a claim and a release are a single atomic
// compare-exchange: no lock, no allocation, no `thread_local`.
//
// ## The decision, in two passes
//
// `try_enter` asks two questions **in this order**:
//
//   1. *am I already inside?* — **every** slot is checked, so the answer does
//      not depend on which slot this owner happens to hold. A single pass
//      that claims the first free slot and only then asks the question gets
//      it wrong: the owner of slot 3 is let back in as soon as slot 0 has
//      been freed, and the recursion runs until the stack overflows.
//   2. *is there a free slot?* — the first free one is claimed with a
//      compare-exchange, so exactly one of two racing threads wins it.
//
// The two passes together mean a thread holds **at most one** slot: the
// second entry of a thread that is already inside is refused by the first
// pass, so `leave` has exactly one slot to release. (A thread that claimed
// two slots would leak one of them: `leave` releases one, and the leftover
// would refuse that thread's legitimate calls **forever**.)
//
// ## The pool
//
// Eight slots for the camera update (which runs on one or two threads), plus
// one reserve slot that is only reached when all eight are taken — so that
// owner is still tracked and its recursion is still broken. When **every**
// slot is held by *other* owners the guard **fails open** (the call is
// allowed, and it is not tracked): refusing a call that is not a recursion
// would drop a camera update — the worse failure. The cost of that window is
// stated in `docs/re/thirdperson.md`.
//
// ## The limits
//
// A slot is leaked when a thread dies **inside** the guarded region (the
// detour's `leave` never runs). The leaked slot is not reclaimed: the guard
// is pure and has no way to ask whether a thread is still alive. The
// consequence is bounded — the pool is nine slots deep, and a thread that
// reuses a dead thread's id is refused (its camera update is dropped, not a
// crash).
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
// The camera update runs on one or two threads. Eight slots leave room for
// the others, and one more slot is kept in reserve for the case where they
// are all taken.
inline constexpr std::size_t SLOT_COUNT = 8;

// The reserve slot: it is only claimed once every other slot is taken.
inline constexpr std::size_t RESERVE_SLOT_COUNT = 1;

// How many slots the guard has in total.
inline constexpr std::size_t TOTAL_SLOT_COUNT = SLOT_COUNT + RESERVE_SLOT_COUNT;

// How many times `leave` re-scans the slots when an exchange fails.
//
// A **strong** compare-exchange does not fail spuriously, and only the owner
// writes its own slot, so one scan is enough; the retries are the belt to
// the braces of a slot that is never abandoned.
inline constexpr std::size_t LEAVE_ATTEMPTS = 4;

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
    // Pass one: am I already inside? Every slot is checked, so the answer
    // does not depend on the order the slots happen to be in.
    for (const auto& slot : slots_) {
      const std::uint64_t state = slot.state.load(std::memory_order_acquire);
      if (depth_of(state) > 0 && owner_of(state) == owner) {
        return false;
      }
    }
    // Pass two: claim a free slot. The exchange is the only way two threads
    // can take the same slot, so exactly one of them wins it.
    for (auto& slot : slots_) {
      std::uint64_t state = slot.state.load(std::memory_order_acquire);
      if (depth_of(state) > 0) {
        continue;  // taken by another owner
      }
      // The failure order is an acquire: the load that failed needs to see
      // the slot's new value.
      if (slot.state.compare_exchange_strong(state, pack(owner, 1), std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        return true;
      }
      // The exchange failed: another thread claimed the slot first. It cannot
      // be our own slot — the owner token is unique per thread and the first
      // pass has just checked every slot — so the next one is tried.
    }
    // Every slot is taken by another owner: **fail open**. Refusing here
    // would drop a camera update on a thread that is not recursing.
    return true;
  }

  // Leave the guarded region. The owner that entered leaves exactly once.
  void leave(OwnerId owner) noexcept {
    for (std::size_t attempt = 0; attempt < LEAVE_ATTEMPTS; ++attempt) {
      for (auto& slot : slots_) {
        std::uint64_t state = slot.state.load(std::memory_order_acquire);
        if (depth_of(state) == 0 || owner_of(state) != owner) {
          continue;
        }
        const std::uint64_t released = pack(owner, depth_of(state) - 1);
        if (slot.state.compare_exchange_strong(state, released, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
          return;
        }
        // The exchange failed: the slot changed under us. Only the owner
        // writes its own slot, so this cannot happen; the slot is re-scanned
        // rather than abandoned.
        break;
      }
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
  // A depth of zero means the slot is free. The depth is one while the owner
  // is inside: a second entry is refused, so it never grows.
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

  // The reserve slot is the last one: it is only reached when every other
  // slot is taken.
  std::array<Slot, TOTAL_SLOT_COUNT> slots_{};
};

}  // namespace cs2::features::guard
