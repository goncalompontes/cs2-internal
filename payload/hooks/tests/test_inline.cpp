// Host test for the inline hook engine: hook a real function, verify the detour
// runs, calls through to the trampoline, and that uninstall restores the target.

#include <cassert>
#include <cstdio>

#include "cs2/hooks/inline.hpp"

namespace {

using Fn = int (*)(int, int);

Fn g_original = nullptr;

#if defined(__clang__)
__attribute__((noinline, optnone))
#elif defined(__GNUC__)
__attribute__((noinline, optimize("O0")))
#endif
int target(int a, int b) {
  volatile int sum = a + b;
  return sum * 2;
}

int detour(int a, int b) { return g_original(a, b) + 1000; }

}  // namespace

int main() {
  assert(target(2, 3) == 10);

  cs2::hooks::InlineHook hook;
  const auto error =
      hook.install(reinterpret_cast<void*>(&target), reinterpret_cast<void*>(&detour));
  if (error) {
    std::printf("inline: install failed: %s\n", cs2::hooks::to_string(*error));
    return 1;
  }
  g_original = reinterpret_cast<Fn>(hook.original());
  assert(hook.installed());

  assert(target(2, 3) == 1010);   // detour ran
  assert(g_original(2, 3) == 10); // trampoline calls the original

  hook.uninstall();
  assert(target(2, 3) == 10);

  std::puts("inline: ok");
  return 0;
}
