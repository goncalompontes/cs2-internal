#pragma once

// The **scope holder** for the shared re-entrancy guard.
//
// The guard has to be held **across** the call it guards: the recursion the
// guard exists to break happens *inside* the call to the trampoline, so a
// guard that is released before the trampoline runs protects nothing. And it
// has to be released on **every** path: a guard that is not released is a
// permanent refusal for that thread, so a plain "enter here, leave there"
// pair is one early `return` away from a dead hook.
//
// The holder does both by construction: it enters the guard in the
// constructor and leaves it in the destructor.
//
// **No rules live here.** Who may enter and who is refused is decided in
// `cs2/features/recursion_guard.hpp` alone — this header only holds the
// guard for the length of a scope. The chams' draw detour and the
// third-person camera's update detour use the same guard, so the tree holds
// exactly one implementation of it.

#include "cs2/features/recursion_guard.hpp"

namespace cs2::features::guard {

// The guard, held for the length of a scope.
//
// The scope enters the guard in the constructor and leaves it in the
// destructor, so the guard is released on every path out of the scope — an
// early return included.
class Scope {
 public:
  Scope(RecursionGuard& guard, OwnerId owner) noexcept : guard_(&guard), owner_(owner) {
    entered_ = guard_->try_enter(owner_);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  ~Scope() {
    if (entered_) {
      guard_->leave(owner_);
    }
  }

  // Did the scope get in? `false` means the owner was already inside: the
  // recursion the guard exists to break.
  [[nodiscard]] auto entered() const noexcept -> bool {
    return entered_;
  }

 private:
  RecursionGuard* guard_{nullptr};
  OwnerId owner_{0};
  bool entered_ = false;
};

}  // namespace cs2::features::guard
