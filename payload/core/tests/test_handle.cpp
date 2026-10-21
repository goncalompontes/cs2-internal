// The RAII handle's ownership rules.
//
// The wrapper is platform-independent, so the rules it enforces can be driven
// off-Windows: a fake policy counts the releases, and the test proves that
// every path releases exactly once -- the scope exit, `reset`, the move, the
// move-assignment and the hand-off (`release`).

#include <cassert>
#include <cstdio>
#include <utility>

#include "cs2/core/handle.hpp"

namespace {

// The number of handles the fake policy has released.
int g_closed = 0;

// A fake handle: a positive value is live, zero is "nothing held".
struct FakePolicy {
  static bool is_valid(int handle) noexcept {
    return handle > 0;
  }
  static void close(int handle) noexcept {
    assert(handle > 0);
    ++g_closed;
  }
};

using FakeHandle = cs2::core::ScopedHandle<int, FakePolicy>;

}  // namespace

auto main() -> int {
  // A default-constructed wrapper holds nothing and releases nothing.
  FakeHandle empty;
  assert(!empty.valid());
  assert(empty.get() == 0);
  assert(g_closed == 0);

  // A live handle is released when the wrapper goes out of scope.
  {
    FakeHandle file(7);
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
    assert(file.get() ==  0);
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
    FakeHandle moved(std::move(file));
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

  std::puts("core-handle: ok");
  return 0;
}
