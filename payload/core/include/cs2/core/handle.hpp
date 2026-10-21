#pragma once

// An owning OS handle: it releases what it holds when it goes out of scope, so
// a function that acquires a handle cannot leak it on an early return.
//
// The wrapper is a template over the handle type and a *policy*: the policy
// says how to recognise a live handle and how to release one. The policy is a
// value, not a type, so a handle kind whose closer is resolved at run time
// (the payload resolves kernel32's entry points by name) can carry the
// resolved entry point with it.
//
// Why a template and not `std::unique_ptr`: its deleter is type-erased through
// a virtual call, which is exactly what the policy parameter replaces, and it
// drags in <memory> (and with it the CRT's allocator) for a payload that is
// manually mapped and avoids the CRT.
//
// The wrapper is deliberately platform-independent and exception-free, so the
// ownership logic itself has a host test (payload/core/tests/test_handle.cpp):
// the Windows-only sources that use it cannot be tested off-Windows.

#include <utility>

namespace cs2::core {

// The policy must provide:
//   [[nodiscard]] bool usable() const noexcept;          // can this policy work at all?
//   [[nodiscard]] bool is_valid(Handle) const noexcept;  // is this a live handle?
//   void close(Handle) const noexcept;                    // release a live handle
//
// `usable()` IS ASKED FIRST, EVERY TIME. It is the one question the wrapper
// asks before it asks anything else, and it is what makes a policy that
// cannot work harmless: a policy whose closer is resolved at run time (the
// payload resolves kernel32's entry points by name) has a *default* state in
// which it cannot answer anything -- a `std::function` closer is empty, and
// calling an empty one throws `std::bad_function_call`, out of a `noexcept`
// function that is `std::terminate`. The wrapper therefore asks the policy
// whether it can work before it asks it to do anything, and a policy that
// says no is never asked again. A policy that says yes and then cannot work
// is a broken policy: the contract is that the policy's three methods never
// throw, because the payload is manually mapped and has no unwinder.
//
// THE POLICY IS ASKED ONLY WHEN A HANDLE IS TAKEN. From then on the wrapper
// knows whether it owns what it holds, and it never asks the policy whether
// the handle is live again.
//
// A MOVE COPIES THE POLICY. The policy is a *value*, so a moved-from wrapper
// keeps a usable one, and there is therefore never a *moved-from* policy for
// the wrapper to call -- neither in the destructor, nor in `reset()`. That
// matters: a policy that carries a `std::function` closer is empty after the
// move, and calling it throws (`std::bad_function_call`) -- out of a
// `noexcept` function, that is `std::terminate`, and a `terminate` in a
// payload that is manually mapped and has no exceptions is the worst outcome
// there is. The price is that the policy must be copyable; the policies in
// the tree are plain old data (a resolved entry point, a comparison), so the
// copy allocates nothing.
//
// THE EMPTY VALUE IS RESERVED. `Handle{}` is the wrapper's "holds nothing",
// and the wrapper never owns it and never releases it. A handle kind whose
// zero value is live (a POSIX file descriptor, where 0 is stdin) must either
// be wrapped in a type whose empty value is distinct, or be given a policy
// whose `is_valid(Handle{})` is false. The wrapper enforces the reservation
// itself, so a policy that breaks the rule leaks the handle instead of
// closing something that is live.
//
// `Handle` must be comparable with `==` (the wrapper compares a handle with
// the empty value), and `Policy` must be default-constructible and
// copyable: the single-argument constructor uses the policy's default, and a
// move copies it. A policy whose *default* cannot work (its `usable()` is
// false) is not an error: the wrapper simply does not adopt the handle, and
// `valid()` says so.
template <typename Handle, typename Policy>
class ScopedHandle {
 public:
  using value_type = Handle;

  ScopedHandle() noexcept = default;
  explicit ScopedHandle(Handle handle) noexcept : handle_(handle), policy_(Policy{}) {
    adopt();
  }
  ScopedHandle(Handle handle, Policy policy) noexcept
      : handle_(handle), policy_(std::move(policy)) {
    adopt();
  }
  ~ScopedHandle() {
    release_owned();
  }

  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle(ScopedHandle&& other) noexcept : policy_(other.policy_) {
    take_from(other);
  }
  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      reset();
      policy_ = other.policy_;
      take_from(other);
    }
    return *this;
  }

  // The raw handle, or `Handle{}` when the wrapper holds nothing.
  [[nodiscard]] auto get() const noexcept -> Handle {
    return handle_;
  }

  // Whether the wrapper holds a live handle.
  [[nodiscard]] auto valid() const noexcept -> bool {
    return owns_;
  }

  // Release the held handle (if the wrapper owns it) and take `handle` in its
  // place. Passing the handle the wrapper already holds is a no-op rather
  // than a double release.
  void reset(Handle handle = Handle{}) noexcept {
    if (owns_ && handle != handle_) {
      policy_.close(handle_);
    }
    handle_ = handle;
    adopt();
  }

  // Give up ownership without releasing the handle: the caller becomes
  // responsible for closing it.
  [[nodiscard]] auto release() noexcept -> Handle {
    const Handle held = handle_;
    handle_ = Handle{};
    owns_ = false;
    return held;
  }

 private:
  Handle handle_{};
  Policy policy_{};
  // Whether the wrapper owns `handle_` and has to release it. The wrapper
  // tracks this itself instead of asking the policy again: a moved-from
  // policy must never be called.
  bool owns_ = false;

  // Take ownership of the held handle if the policy can work and the handle
  // is a live one. The empty value is never owned (see the policy contract
  // above), and a policy that cannot work is never asked.
  auto adopt() noexcept -> void {
    owns_ = policy_.usable() && (handle_ != Handle{}) && policy_.is_valid(handle_);
  }

  // Release the held handle, if the wrapper owns it.
  auto release_owned() noexcept -> void {
    if (owns_) {
      policy_.close(handle_);
    }
  }

  // Take the handle -- and the ownership of it -- from `other`, which keeps
  // neither. `other`'s policy is not touched.
  auto take_from(ScopedHandle& other) noexcept -> void {
    handle_ = other.handle_;
    owns_ = other.owns_;
    other.handle_ = Handle{};
    other.owns_ = false;
  }
};

}  // namespace cs2::core
