// Host test for the vtable hook engine: build a fake vtable in executable
// memory, hook a slot, verify the detour runs, calls through to the original,
// and that the slot is restored on destruction.

#include <cassert>
#include <cstdio>

#include "cs2/hooks/memory.hpp"
#include "cs2/hooks/vtable.hpp"

namespace {

using Fn = int (*)(int);

Fn g_original = nullptr;

int add_one(int value) { return value + 1; }
int detour(int value) { return g_original(value) * 10; }

}  // namespace

int main() {
  auto* table = static_cast<Fn*>(cs2::hooks::allocate_executable(sizeof(Fn) * 2));
  assert(table != nullptr);
  table[0] = add_one;
  table[1] = add_one;

  {
    cs2::hooks::VtableHook hook(reinterpret_cast<void**>(table), 1,
                                reinterpret_cast<void*>(&detour));
    assert(hook.installed());
    g_original = reinterpret_cast<Fn>(hook.original());
    assert(table[1] == &detour);
    assert(table[1](4) == 50);  // (4 + 1) * 10
    assert(table[0](4) == 5);   // untouched slot
    std::printf("hooks: detour ran, original() = %d\n", g_original(4));
  }

  assert(table[1] == add_one);  // restored

  // A null vtable / detour is rejected without crashing.
  cs2::hooks::VtableHook none(nullptr, 0, nullptr);
  assert(!none.installed());

  cs2::hooks::free_executable(table, sizeof(Fn) * 2);
  std::puts("hooks: ok");
  return 0;
}
