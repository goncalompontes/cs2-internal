// Host test for the inline hook engine: hook a real function, verify the detour
// runs, calls through to the trampoline, and that uninstall restores the target.
//
// It also pins the **trampoline's lifetime**: `uninstall()` publishes the
// null pointer *before* the trampoline's pages are released, and a retired
// trampoline's pages stay mapped, so a detour that was already inside the
// hook when the uninstall happened cannot call **freed** executable memory.
// The old order freed the trampoline first and nulled the pointer after, so
// the detour below called memory that had been unmapped.
//
// The **destructor** is the second half of the property, and it is the half
// that used to be false: `~InlineHook` used to release the retired
// trampolines' pages, so destroying the hook while a detour was inside it
// left that detour holding a pointer into unmapped executable memory. The
// pages are never unmapped now, so the detour's call still lands on live
// memory.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so they still bite in a release build (Meson's `b_ndebug` default
// is `if-release`, so `NDEBUG` is defined in the configuration
// `scripts/release.sh` builds and every `assert` would be compiled out — a
// test that only bites in the debug build is not a test, and this one is the
// proof of a memory-safety property).

#include <atomic>
#include <cstdio>
#include <thread>

#include "cs2/hooks/inline.hpp"

namespace {

using Fn = int (*)(int, int);

Fn g_original = nullptr;

// The number of failed checks. The test's exit code is the verdict, so a
// release build (where `assert` is compiled out) still reports the failure.
int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// --- The hook itself ------------------------------------------------------------

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

// --- The lifetime cases -------------------------------------------------------

// The detour **blocks** inside the hook, so the teardown happens while the
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

// The state the two lifetime cases start from.
void reset_the_lifetime_state() {
  g_inside.store(false, std::memory_order_relaxed);
  g_proceed.store(false, std::memory_order_relaxed);
  g_result.store(0, std::memory_order_relaxed);
  g_captured = nullptr;
}

// The trampoline's lifetime: the uninstall happens while the detour is
// inside, and the detour then calls the trampoline it captured.
//
// With the old order (free, then null) the trampoline's pages were unmapped
// under the detour and the call below faulted. With the retired trampoline
// the pages stay mapped, so the call returns the original's value.
void test_the_trampoline_outlives_the_uninstall() {
  reset_the_lifetime_state();

  cs2::hooks::InlineHook hook;
  const auto error = hook.install(reinterpret_cast<void*>(&lifetime_target),
                                  reinterpret_cast<void*>(&lifetime_detour));
  if (error) {
    std::printf("inline: install failed: %s\n", cs2::hooks::to_string(*error));
    expect(false, "the hook installs");
    return;
  }
  g_original = reinterpret_cast<Fn>(hook.original());
  expect(g_original != nullptr, "the trampoline is published");

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
  expect(hook.original() == nullptr, "the null is published before the teardown");
  expect(!hook.installed(), "the hook is no longer installed");

  // Let the detour proceed: it calls the trampoline it captured.
  g_proceed.store(true, std::memory_order_release);
  caller.join();

  // The call returned the original's value: no freed memory was executed.
  expect(g_result.load(std::memory_order_acquire) == 10, "the trampoline outlives the uninstall");
}

// The **destructor**: the hook is destroyed while the detour is inside and
// holds the trampoline, and the detour then calls the trampoline it
// captured.
//
// This is the case the deferred free did not cover: `~InlineHook` released
// the retired trampolines' pages, so the call below landed on **unmapped
// executable memory** and the process died with an access violation
// (0xC0000005). The pages are never unmapped now.
void test_the_trampoline_outlives_the_destruction() {
  reset_the_lifetime_state();

  std::thread caller;
  {
    cs2::hooks::InlineHook hook;
    const auto error = hook.install(reinterpret_cast<void*>(&lifetime_target),
                                    reinterpret_cast<void*>(&lifetime_detour));
    if (error) {
      std::printf("inline: install failed: %s\n", cs2::hooks::to_string(*error));
      expect(false, "the hook installs");
      return;
    }
    g_original = reinterpret_cast<Fn>(hook.original());

    caller = std::thread([]() { (void)lifetime_target(2, 3); });
    // Wait for the detour to be inside the hook.
    while (!g_inside.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    // The hook is **destroyed here**, with the detour inside it and holding
    // the trampoline pointer. The destructor un-patches the target and
    // retires the trampoline; it must not release the pages the detour is
    // about to call.
  }

  // Let the detour proceed: it calls the trampoline it captured.
  g_proceed.store(true, std::memory_order_release);
  caller.join();

  // The call returned the original's value: no freed memory was executed.
  expect(g_result.load(std::memory_order_acquire) == 10, "the trampoline outlives the destruction");
}

}  // namespace

auto main() -> int {
  expect(target(2, 3) == 10, "the target computes its value");

  cs2::hooks::InlineHook hook;
  const auto error =
      hook.install(reinterpret_cast<void*>(&target), reinterpret_cast<void*>(&detour));
  if (error) {
    std::printf("inline: install failed: %s\n", cs2::hooks::to_string(*error));
    return 1;
  }
  g_original = reinterpret_cast<Fn>(hook.original());
  expect(hook.installed(), "the hook is installed");

  expect(target(2, 3) == 1010, "the detour ran");  // detour ran
  expect(g_original(2, 3) == 10, "the trampoline calls the original");

  hook.uninstall();
  expect(target(2, 3) == 10, "the uninstall restores the target");

  test_the_trampoline_outlives_the_uninstall();
  test_the_trampoline_outlives_the_destruction();

  if (failures != 0) {
    std::printf("inline: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("inline: ok");
  return 0;
}
