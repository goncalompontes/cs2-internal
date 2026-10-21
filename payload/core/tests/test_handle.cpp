// The RAII handle's ownership rules.
//
// The wrapper is platform-independent, so the rules it enforces can be driven
// off-Windows: a fake policy counts the releases, and the test proves that
// every path releases exactly once -- the scope exit, `reset`, the move, the
// move-assignment and the hand-off (`release`).
//
// Two rules need a policy that is not a plain old data type, and they are
// proven with one:
//   * a *moved-from* policy must never be called (a policy that carries a
//     `std::function` closer is empty after the move, and calling it throws);
//   * the *empty value* is reserved: a handle kind whose zero value is live
//     (a POSIX file descriptor, where 0 is stdin) must not be closed by the
//     wrapper, and a handle must not be released twice.

#include <cassert>
#include <cstdio>
#include <functional>
#include <utility>

#include "cs2/core/handle.hpp"

namespace {

// The number of handles the fake policy has released.
int g_closed = 0;

// A fake handle: a positive value is live, zero is "nothing held".
struct FakePolicy {
  // The counter the releases are counted in. It travels with the policy, so
  // a test can prove that a moved wrapper keeps its own closer.
  int* counter = &g_closed;

  [[nodiscard]] bool usable() const noexcept {
    return true;
  }
  [[nodiscard]] bool is_valid(int handle) const noexcept {
    return handle > 0;
  }
  void close(int handle) const noexcept {
    assert(handle > 0);
    ++(*counter);
  }
};

using FakeHandle = cs2::core::ScopedHandle<int, FakePolicy>;

// A policy that cannot survive a move: both of its members are
// `std::function`s, so a moved-from policy is *empty* and calling it throws
// `std::bad_function_call`. The wrapper's destructor is `noexcept`, so such a
// call would end the process.
struct FunctionPolicy {
  std::function<void(int)> close_fn;
  std::function<bool(int)> valid_fn;

  [[nodiscard]] bool usable() const noexcept {
    return static_cast<bool>(close_fn) && static_cast<bool>(valid_fn);
  }
  [[nodiscard]] bool is_valid(int handle) const noexcept {
    return valid_fn(handle);
  }
  void close(int handle) const noexcept {
    close_fn(handle);
  }
};

using FunctionHandle = cs2::core::ScopedHandle<int, FunctionPolicy>;

// A policy whose *default* state cannot work: both of its `std::function`s
// are empty, so calling one throws `std::bad_function_call`. The wrapper
// must ask the policy whether it can work before it asks it anything else,
// so a policy like this is never asked -- and the process survives.
struct PoisonedPolicy {
  std::function<void(int)> close_fn;
  std::function<bool(int)> valid_fn;

  [[nodiscard]] bool usable() const noexcept {
    return static_cast<bool>(close_fn) && static_cast<bool>(valid_fn);
  }
  [[nodiscard]] bool is_valid(int handle) const noexcept {
    return valid_fn(handle);
  }
  void close(int handle) const noexcept {
    close_fn(handle);
  }
};

using PoisonedHandle = cs2::core::ScopedHandle<int, PoisonedPolicy>;

// A handle kind whose zero value is live: a POSIX file descriptor, where 0 is
// stdin. The policy says so; the wrapper reserves the empty value anyway.
struct ZeroIsLivePolicy {
  int* closed = nullptr;

  [[nodiscard]] bool usable() const noexcept {
    return true;
  }
  [[nodiscard]] bool is_valid(int handle) const noexcept {
    return handle >= 0;
  }
  void close(int handle) const noexcept {
    assert(handle >= 0);
    ++(*closed);
  }
};

using ZeroIsLiveHandle = cs2::core::ScopedHandle<int, ZeroIsLivePolicy>;

}  // namespace

auto main() -> int {
  // A default-constructed wrapper holds nothing and releases nothing.
  const FakeHandle empty;
  assert(!empty.valid());
  assert(empty.get() == 0);
  assert(g_closed == 0);

  // A live handle is released when the wrapper goes out of scope.
  {
    const FakeHandle file(7);
    assert(file.valid());
    assert(file.get() == 7);
    assert(g_closed == 0);  // not released yet
  }
  assert(g_closed == 1);

  // `reset` releases the held handle and takes the new one.
  {
    FakeHandle file(1);
    file.reset(2);
    assert(g_closed == 2);  // handle 1 released
    assert(file.valid());
    assert(file.get() == 2);
  }
  assert(g_closed == 3);  // handle 2 released on the way out

  // `reset` with no argument releases the handle and holds nothing.
  {
    FakeHandle file(3);
    file.reset();
    assert(g_closed == 4);
    assert(!file.valid());
    assert(file.get() == 0);
  }
  assert(g_closed == 4);  // nothing left to release

  // `release` hands the ownership over: the wrapper must not close it.
  {
    FakeHandle file(4);
    assert(file.release() == 4);
    assert(!file.valid());
    assert(file.get() == 0);
  }
  assert(g_closed == 4);

  // Moving transfers the ownership; the source holds nothing.
  {
    FakeHandle file(5);
    const FakeHandle moved(std::move(file));
    assert(moved.valid());
    assert(moved.get() == 5);
    assert(!file.valid());
    assert(file.get() == 0);
  }
  assert(g_closed == 5);  // the moved-to wrapper released it

  // Moving over a live handle releases the overwritten one.
  {
    FakeHandle file(6);
    FakeHandle moved(7);
    moved = std::move(file);
    assert(g_closed == 6);  // handle 7 released by the assignment
    assert(moved.valid());
    assert(moved.get() == 6);
    assert(!file.valid());
  }
  assert(g_closed == 7);  // handle 6 released on the way out

  // The closer travels with the handle: a wrapper built with an explicit
  // policy keeps that policy across a move, and the release is counted in
  // *its* counter.
  int other_closed = 0;
  {
    FakeHandle file(8, FakePolicy{&other_closed});
    const FakeHandle moved(std::move(file));
    assert(moved.get() == 8);
    assert(other_closed == 0);
  }
  assert(other_closed == 1);
  assert(g_closed == 7);  // the global counter was not touched

  // A moved-from wrapper holds nothing, so its destructor must not release
  // anything -- and the policy it carries must never be one that was moved
  // out of: the policy here is a `std::function`, and an empty one throws
  // `std::bad_function_call` out of the wrapper's `noexcept` destructor, so
  // the test would end the process instead of failing an assertion.
  int function_closed = 0;
  {
    FunctionHandle file(3, FunctionPolicy{[&function_closed](int) { ++function_closed; },
                                         [](int handle) { return handle > 0; }});
    const FunctionHandle moved(std::move(file));
    assert(moved.valid());
    assert(moved.get() == 3);
    // The moved-from wrapper holds nothing, and it does not ask its policy
    // about it.
    assert(!file.valid());
    assert(file.get() == 0);
  }  // the moved-from wrapper is destroyed here: it must not release anything
  assert(function_closed == 1);  // the moved-to wrapper released it, once

  // A moved-from wrapper is still a *usable* wrapper: the move copies the
  // policy, so the source has a usable one and can take a new handle. If the
  // wrapper moved the policy instead, the source's policy would be empty and
  // `reset` would throw `std::bad_function_call` out of a `noexcept`
  // function -- the test would end the process (0xC0000409).
  int reset_closed = 0;
  {
    FunctionHandle file(3, FunctionPolicy{[&reset_closed](int) { ++reset_closed; },
                                         [](int handle) { return handle > 0; }});
    const FunctionHandle moved(std::move(file));
    assert(moved.get() == 3);
    // The source holds nothing, and it still has a usable policy.
    assert(!file.valid());
    assert(file.get() == 0);
    file.reset(7);
    assert(file.valid());
    assert(file.get() == 7);
    assert(reset_closed == 0);
  }  // the moved-to wrapper releases 3, the source releases 7
  assert(reset_closed == 2);

  // A policy that cannot work must never be asked. The wrapper asks the
  // policy whether it can work before it asks it anything else, so a policy
  // whose default state cannot answer (here: two empty `std::function`s,
  // and calling one throws `std::bad_function_call`) is left alone: the
  // wrapper takes the handle unowned and the process survives.
  int poisoned_closed = 0;
  {
    // The default policy: it cannot work.
    const PoisonedHandle handle(5);
    assert(!handle.valid());  // the wrapper did not adopt it
    assert(handle.get() == 5);
  }  // the destructor: nothing owned, so nothing is asked -- the process
     // survives
  {
    // The same for a default-constructed wrapper that is reset: the default
    // policy cannot work, so the handle is not adopted either.
    PoisonedHandle handle;
    handle.reset(7);
    assert(!handle.valid());
    assert(handle.get() == 7);
  }  // nothing owned, nothing asked
  {
    // A policy that *can* work is asked, and the handle is adopted.
    const PoisonedHandle handle(5, PoisonedPolicy{[&poisoned_closed](int) { ++poisoned_closed; },
                                                   [](int handle) { return handle > 0; }});
    assert(handle.valid());
    assert(handle.get() == 5);
    assert(poisoned_closed == 0);
  }  // the handle is released here
  assert(poisoned_closed == 1);

  // The empty value is reserved: the wrapper never owns it, so a policy that
  // says the zero value is live does not get it closed (stdin!).
  int zero_closed = 0;
  {
    // (`stdin` is a macro in <cstdio>, so the handle is called `descriptor`.)
    const ZeroIsLiveHandle descriptor(0, ZeroIsLivePolicy{&zero_closed});
    assert(descriptor.get() == 0);
    assert(!descriptor.valid());
  }
  assert(zero_closed == 0);

  // A handle is released exactly once, even when `reset` is given the handle
  // the wrapper already holds.
  {
    ZeroIsLiveHandle file(5, ZeroIsLivePolicy{&zero_closed});
    file.reset(5);
    assert(file.valid());
    assert(zero_closed == 0);
  }
  assert(zero_closed == 1);

  std::puts("core-handle: ok");
  return 0;
}
