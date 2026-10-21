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
//   bool is_valid(Handle) const noexcept;  // is this a live handle?
//   void close(Handle) const noexcept;      // release a live handle
template <typename Handle, typename Policy>
class ScopedHandle {
 public:
  using value_type = Handle;

  ScopedHandle() noexcept = default;
  explicit ScopedHandle(Handle handle) noexcept : handle_(handle), policy_(Policy{}) {}
  ScopedHandle(Handle handle, Policy policy) noexcept
      : handle_(handle), policy_(std::move(policy)) {}
  ~ScopedHandle() {
    reset();
  }

  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle(ScopedHandle&& other) noexcept
      : handle_(other.release()), policy_(std::move(other.policy_)) {}
  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.release();
      policy_ = std::move(other.policy_);
    }
    return *this;
  }

  // The raw handle, or `Handle{}` when the wrapper holds nothing.
  [[nodiscard]] auto get() const noexcept -> Handle {
    return handle_;
  }

  // Whether the wrapper holds a live handle.
  [[nodiscard]] auto valid() const noexcept -> bool {
    return policy_.is_valid(handle_);
  }

  // Release the held handle (if it is live) and take `handle` in its place.
  void reset(Handle handle = Handle{}) noexcept {
    if (policy_.is_valid(handle_)) {
      policy_.close(handle_);
    }
    handle_ = handle;
  }

  // Give up ownership without releasing the handle: the caller becomes
  // responsible for closing it.
  [[nodiscard]] auto release() noexcept -> Handle {
    const Handle held = handle_;
    handle_ = Handle{};
    return held;
  }

 private:
  Handle handle_{};
  Policy policy_{};
};

}  // namespace cs2::core
