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
// The pool's size is **per instance**, because the two features that share
// this guard need different answers:
//
//   * the **camera** update runs on one or two threads. Eight slots leave
//     room for the others, and one more slot is kept in reserve for the case
//     where they are all taken. When **every** slot is held by *other* owners
//     the guard **fails open** (the call is allowed, and it is not tracked):
//     refusing a call that is not a recursion would drop a camera update — the
//     worse failure. The cost of that window is stated in
//     `docs/re/thirdperson.md`.
//   * the **draw path** holds its slot for the **whole draw** — the call into
//     the original included, because that is the call the recursion happens
//     inside — so its pool has to hold one slot per **concurrent render job**,
//     not one per thread. See `DRAW_PATH_SLOT_COUNT` for the number and for
//     the cost argument.
//
// The default is the camera's pool, so every caller that does not ask for
// anything else keeps the behaviour it has today — the camera included.
//
// ## The policy is per instance, and the draw path's is the other way round
//
// The fail-open rationale above is the **camera's**: a refusal would drop a
// camera update, so the camera fails open. The **draw path** has the
// arithmetic the other way round, and the guard's state is already per
// instance (the chams hold `draw_guard_`, the camera holds `camera_guard_`),
// so the policy can differ without either feature changing.
//
// Under fail-open the calling thread is **not recorded in any slot**. So when
// the trampoline comes back on that thread, the first pass does not find it —
// the recursion is **not broken**, and it runs until the stack overflows
// (0xc00000fd) and takes the renderer with it. A refusal, on the other hand,
// drops **one primitive**: the cost of a refusal is bounded and visible, and
// the cost of a fail-open recursion is the renderer.
//
// So the policy is a **constructor argument** with the camera's value as the
// default: `RecursionGuard guard;` fails open (the camera's behaviour, and
// the behaviour every existing caller has), and the chams' guard is built
// with `Policy::fail_closed`.
//
// ## The limits
//
// A slot is leaked when a thread dies **inside** the guarded region (the
// detour's `leave` never runs). The leaked slot is not reclaimed: the guard
// is pure and has no way to ask whether a thread is still alive. The
// consequence is bounded — the pool is nine slots deep for the camera and
// sixty-four for the draw path — and a thread that reuses a dead thread's id
// is refused (its camera update is dropped, not a crash; its draw is
// dropped, and counted).
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

// The **draw path's** pool.
//
// The chams' guard holds its slot for the **whole draw** — the call into the
// original included, because that is the call the recursion happens inside —
// so the pool has to hold one slot per **concurrent render job**, not one per
// thread. The scene system's job count is **unmeasured** (the live run has
// still not happened: `docs/re/chams.md` §10.7.1), so the pool is sized with
// headroom over any plausible count — the tier0 thread pool has about as
// many threads as the machine has hardware threads, and the scene system
// splits the draw list across them.
//
// The cost of the headroom is the **scan**: the guard's first pass ("am I
// already inside?") walks **every** slot, so the scan is O(pool) per draw.
// A slot is one 64-bit word, so sixty-four slots are 512 bytes (eight cache
// lines, resident in L1) and the scan is sixty-four loads — about ten
// nanoseconds, or roughly twenty microseconds per frame at two thousand
// primitives (a hundredth of a millisecond against a sixteen millisecond
// frame).
//
// Sixty-four is **eight times** the camera's pool, so the camera's behaviour
// and its cost are exactly what they were.
inline constexpr std::size_t DRAW_PATH_SLOT_COUNT = 64;

// How many times `leave` re-scans the slots when an exchange fails.
//
// A **strong** compare-exchange does not fail spuriously, and only the owner
// writes its own slot, so one scan is enough; the retries are the belt to
// the braces of a slot that is never abandoned.
inline constexpr std::size_t LEAVE_ATTEMPTS = 4;

class RecursionGuard {
 public:
  // What the guard does when every slot is held by another owner.
  enum class Policy : std::uint8_t {
    // Let the call in, untracked. The camera's policy: refusing would drop
    // a camera update on a thread that is not recursing.
    fail_open,
    // Refuse the call. The draw path's policy: a refusal drops one
    // primitive, while an untracked recursion runs until the stack
    // overflows and takes the renderer with it.
    fail_closed,
  };

  // The policy is **per instance**: the default is the camera's (fail open),
  // so every caller that does not ask for anything else keeps the behaviour
  // it has today.
  //
  // The pool's size is per instance too: the default is the camera's pool
  // (eight slots and a reserve), and the draw path asks for its own, which
  // has to hold one slot per concurrent render job.
  explicit RecursionGuard(Policy policy = Policy::fail_open,
                          std::size_t slot_count = TOTAL_SLOT_COUNT) noexcept
      : slot_count_(slot_count < MAX_SLOT_COUNT ? slot_count : MAX_SLOT_COUNT), policy_(policy) {
    if (slot_count_ == 0) {
      slot_count_ = 1;
    }
  }
  RecursionGuard(const RecursionGuard&) = delete;
  RecursionGuard& operator=(const RecursionGuard&) = delete;

  // How many slots this guard has.
  [[nodiscard]] auto slot_count() const noexcept -> std::size_t {
    return slot_count_;
  }

  // Try to enter the guarded region as `owner`.
  //
  // `true` — the call may proceed.
  // `false` — the call is **refused**: `owner` is already inside, which is
  // the recursion the guard exists to break.
  [[nodiscard]] auto try_enter(OwnerId owner) noexcept -> bool {
    // Pass one: am I already inside? Every slot is checked, so the answer
    // does not depend on the order the slots happen to be in.
    for (std::size_t index = 0; index < slot_count_; ++index) {
      const std::uint64_t state = slots_[index].state.load(std::memory_order_acquire);
      if (depth_of(state) > 0 && owner_of(state) == owner) {
        return false;
      }
    }
    // Pass two: claim a free slot. The exchange is the only way two threads
    // can take the same slot, so exactly one of them wins it.
    for (std::size_t index = 0; index < slot_count_; ++index) {
      Slot& slot = slots_[index];
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
    // Every slot is taken by another owner. The policy decides what happens:
    // the camera lets the call in (refusing it would drop a camera update),
    // the draw path refuses it (an untracked recursion is not broken, and it
    // runs until the stack overflows).
    if (policy_ == Policy::fail_closed) {
      return false;
    }
    return true;
  }

  // Leave the guarded region. The owner that entered leaves exactly once.
  void leave(OwnerId owner) noexcept {
    for (std::size_t attempt = 0; attempt < LEAVE_ATTEMPTS; ++attempt) {
      for (std::size_t index = 0; index < slot_count_; ++index) {
        std::uint64_t state = slots_[index].state.load(std::memory_order_acquire);
        if (depth_of(state) == 0 || owner_of(state) != owner) {
          continue;
        }
        const std::uint64_t released = pack(owner, depth_of(state) - 1);
        if (slots_[index].state.compare_exchange_strong(state, released, std::memory_order_acq_rel,
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
    for (std::size_t index = 0; index < slot_count_; ++index) {
      const std::uint64_t state = slots_[index].state.load(std::memory_order_acquire);
      if (depth_of(state) > 0 && owner_of(state) == owner) {
        return depth_of(state);
      }
    }
    return 0;
  }

  // Is `owner` inside the guarded region right now?
  //
  // The draw path asks this when the guard **refused** a call: the refusal
  // is the **recursion** (the owner is already inside) or the **pool** being
  // exhausted (it is not), and the two need different counters and different
  // responses from the operator.
  [[nodiscard]] auto is_inside(OwnerId owner) const noexcept -> bool {
    return depth(owner) > 0;
  }

 private:
  // The slot's state: the owner in the high half, the depth in the low half.
  // A depth of zero means the slot is free. The depth is one while the owner
  // is inside: a second entry is refused, so it never grows.
  struct Slot {
    std::atomic<std::uint64_t> state{0};
  };

  // The largest pool a guard can be given: the draw path's. The array is
  // fixed (no allocation, no vector), and a guard uses the first
  // `slot_count_` of its slots.
  static constexpr std::size_t MAX_SLOT_COUNT = DRAW_PATH_SLOT_COUNT;

  static constexpr auto pack(OwnerId owner, std::uint32_t depth) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(owner) << 32) | static_cast<std::uint64_t>(depth);
  }

  static constexpr auto owner_of(std::uint64_t state) noexcept -> OwnerId {
    return static_cast<OwnerId>(state >> 32);
  }

  static constexpr auto depth_of(std::uint64_t state) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(state & 0xffffffffU);
  }

  // The slots. The last one is the reserve: it is only reached when every
  // other slot is taken.
  std::array<Slot, MAX_SLOT_COUNT> slots_{};
  // How many of the slots this guard uses.
  std::size_t slot_count_{TOTAL_SLOT_COUNT};
  // What the guard does when the pool is exhausted. It is fixed when the
  // guard is built: the policy is a property of the feature that owns the
  // guard, not of the moment.
  Policy policy_{Policy::fail_open};
};

}  // namespace cs2::features::guard
