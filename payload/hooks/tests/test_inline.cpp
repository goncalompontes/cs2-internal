// Host test for the inline hook engine: hook a real function, verify the detour
// runs, calls through to the trampoline, and that uninstall restores the target.
//
// It also pins the **trampoline's lifetime**: `uninstall()` publishes the
// null pointer *before* the trampoline's pages are released, and a retired
// trampoline's pages stay mapped, so a detour that was already inside the
// hook when the uninstall happened cannot call **freed** executable memory.
// The old order freed the trampoline first and nulled the pointer after, so
// the detour below called memory that had been unmapped.

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

#include "cs2/hooks/inline.hpp"

namespace {

using Fn = int (*)(int, int);

Fn g_original = nullptr;

#if defined(__clang__)
__attribute__((noinline, optnone))
#elif defined(__GNUC__)
__attribute__((noinline, optimize("O0")))
#endif
auto target(int a, int b) -> int {
  const volatile int sum = a + b;
  return sum * 2;
}

auto detour(int a, int b) -> int {
  return g_original(a, b) + 1000;
}

// --- The lifetime case -------------------------------------------------------
//
// The detour **blocks** inside the hook, so the uninstall happens while the
// detour is running. The detour then calls the trampoline it captured
// **before** the block: that is the call that must not touch freed memory.
std::atomic<bool> g_inside{false};
std::atomic<bool> g_proceed{false};
std::atomic<int> g_result{0};
Fn g_captured = nullptr;

#if defined(__clang__)
__attribute__((noinline, optnone))
#elif defined(__GNUC__)
__attribute__((noinline, optimize("O0")))
#endif
auto lifetime_target(int a, int b) -> int {
  const volatile int sum = a + b;
  return sum * 2;
}

auto lifetime_detour(int a, int b) -> int {
  // The trampoline, captured while the hook is still installed — the pointer
  // a detour holds across the call to the original.
  g_captured = g_original;
  g_inside.store(true, std::memory_order_release);
  while (!g_proceed.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  // The call under test: the trampoline's pages must still be mapped.
  g_result.store(g_captured(a, b), std::memory_order_release);
  return g_result.load(std::memory_order_acquire);
}

// The trampoline's lifetime: the uninstall happens while the detour is
// inside, and the detour then calls the trampoline it captured.
//
// With the old order (free, then null) the trampoline's pages were unmapped
// under the detour and the call below faulted. With the retired trampoline
// the pages stay mapped, so the call returns the original's value.
void test_the_trampoline_outlives_the_uninstall() {
  cs2::hooks::InlineHook hook;
  const auto error = hook.install(reinterpret_cast<void*>(&lifetime_target),
                                  reinterpret_cast<void*>(&lifetime_detour));
  if (error) {
    std::printf("inline: install failed: %s\n", cs2::hooks::to_string(*error));
    assert(false);
    return;
  }
  g_original = reinterpret_cast<Fn>(hook.original());
  assert(g_original != nullptr);

  g_inside.store(false, std::memory_order_relaxed);
  g_proceed.store(false, std::memory_order_relaxed);
  g_result.store(0, std::memory_order_relaxed);

  std::thread caller([]() { (void)lifetime_target(2, 3); });
  // Wait for the detour to be inside the hook.
  while (!g_inside.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // The uninstall, while the detour is inside and holds the trampoline.
  hook.uninstall();

  // The null is published **before** the trampoline's pages are released:
  // a detour that enters from here on finds no trampoline and drops the
  // call.
  assert(hook.original() == nullptr);
  assert(!hook.installed());

  // Let the detour proceed: it calls the trampoline it captured.
  g_proceed.store(true, std::memory_order_release);
  caller.join();

  // The call returned the original's value: no freed memory was executed.
  assert(g_result.load(std::memory_order_acquire) == 10);
}

}  // namespace

auto main() -> int {
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

  test_the_trampoline_outlives_the_uninstall();

  std::puts("inline: ok");
  return 0;
}
