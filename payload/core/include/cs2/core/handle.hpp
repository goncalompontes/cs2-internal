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
//   [[nodiscard]] bool is_valid(Handle) const noexcept;  // is this a live handle?
//   void close(Handle) const noexcept;                    // release a live handle
//
// THE POLICY IS ASKED ONLY WHEN A HANDLE IS TAKEN. From then on the wrapper
// knows whether it owns what it holds, and it never asks the policy again.
// That is what makes the wrapper safe with a policy that cannot survive a
// move: the destructor of a moved-from wrapper must not call a moved-from
// policy, and a policy that carries a `std::function` closer throws
// (`std::bad_function_call`) when it is called empty -- out of a `noexcept`
// destructor, that is `std::terminate`.
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
// the empty value), and `Policy` must be default-constructible: the
// single-argument constructor uses the policy's default, and that default
// must be able to release a handle.
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

  ScopedHandle(ScopedHandle&& other) noexcept : policy_(std::move(other.policy_)) {
    take_from(other);
  }
  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      reset();
      policy_ = std::move(other.policy_);
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

  // Take ownership of the held handle if it is a live one. The empty value
  // is never owned (see the policy contract above).
  auto adopt() noexcept -> void {
    owns_ = (handle_ != Handle{}) && policy_.is_valid(handle_);
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
