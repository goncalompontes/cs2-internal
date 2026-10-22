// The fixture of the differential test.
//
// The protection pass rewrites the calls of the selected function into
// indirect dispatches through an encrypted descriptor. A mistake does not
// crash: it calls the *wrong* function, so the fixture has to compute
// something that a wrong call changes.
//
// The fixture is built twice -- once with the pass and once without -- and
// the two builds have to agree on what the probe returns.
//
// It is a **DLL**, like the payload: the loader calls DllMain, and the
// probe is reached through the loader's own path.
//
// The struct-returning callees are the cases this test exists for: the
// call carries an `sret` parameter, the pass routes it through a **thunk**,
// and the hidden pointer has to reach the callee unchanged. A mistake there
// is a **wrong value**, not a crash.

#include <cstdint>

#include "diff_shared.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

// The counter makes every call observable: the compiler cannot fold a call
// that writes to it, so the calls survive the optimiser.
extern "C" volatile int cs2_fixture_counter = 0;

extern "C" __attribute__((noinline)) int cs2_fixture_add(int a, int b) noexcept {
  cs2_fixture_counter += 1;
  return a + b;
}

extern "C" __attribute__((noinline)) int cs2_fixture_mul(int a, int b) noexcept {
  cs2_fixture_counter += 2;
  return a * b;
}

// A variadic callee: the pass must skip it (the indirect call cannot
// reproduce the variadic ABI), and it still has to work.
extern "C" __attribute__((noinline)) int cs2_fixture_variadic(int count, ...) {
  cs2_fixture_counter += 3;
  return count;
}

// A callee that returns a **big** aggregate: the call carries an `sret`
// parameter, so the pass routes it through a thunk.
extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_big(int value) {
  cs2_fixture_counter += 4;
  return FixtureBig{value, value + 1, value + 2};
}

// A callee that returns a **small** aggregate: the ABI returns it in
// registers, and the narrowing of the `sret` rule must not break that path.
extern "C" __attribute__((noinline)) FixtureSmall cs2_fixture_small(int value) {
  cs2_fixture_counter += 5;
  return FixtureSmall{value, static_cast<float>(value) * 0.5F};
}

// A callee that returns an aggregate with **mixed members** and takes
// arguments: the hidden pointer is the first argument of the call, and the
// rest follow it in the call's order.
extern "C" __attribute__((noinline)) FixtureMixed cs2_fixture_mixed(int value, double scale) {
  cs2_fixture_counter += 6;
  return FixtureMixed{value, static_cast<float>(scale), const_cast<int*>(&cs2_fixture_counter),
                       value * 3};
}

// A callee that returns an aggregate **and** throws: the call is an
// `invoke` in the caller, and the exception has to reach the caller's
// handler through the thunk.
//
// The exception is conditional: the compiler has to keep the code after the
// call, or it would delete the calls that follow it.
extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_throwing(int value) noexcept(false) {
  cs2_fixture_counter += 7;
  if (value < 0) {
    throw value;
  }
  return FixtureBig{value, value + 1, value + 2};
}

// The callee of another translation unit: the pass routes it through the
// caller's own table, as a relocatable pointer to the symbol. The
// declaration is all this file has: the definition lives in
// diff_helper.cpp.
extern "C" __attribute__((noinline)) int cs2_fixture_helper(int value) noexcept;

// The callee of another translation unit that returns an aggregate: the
// cross-TU `sret` edge. The definition lives in diff_helper.cpp.
extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_cross(int value);

// The callee of a tail call: the pass must skip a `musttail` call.
extern "C" __attribute__((noinline)) int cs2_fixture_tail(int value) {
  cs2_fixture_counter += 8;
  return value;
}

// A call that may throw: it becomes an `invoke` in the caller, and the
// pass must skip it.
extern "C" void cs2_fixture_may_throw() {
  cs2_fixture_counter += 9;
}

// The selected function. The pass rewrites the calls to cs2_fixture_add and
// cs2_fixture_mul (both are defined in this file) and the call to
// cs2_fixture_helper (which is defined in another translation unit, and the
// policy's `cross_module` list names it). It routes the struct-returning
// calls through a thunk, and it leaves the variadic, the musttail and the
// invoke alone.
extern "C" __attribute__((noinline)) int cs2_fixture_caller(int seed) {
  int total = seed;
  try {
    for (int i = 0; i < 8; ++i) {
      total = cs2_fixture_add(total, seed + i);
      total = cs2_fixture_mul(total, 3);
    }
    cs2_fixture_may_throw();
  } catch (...) {
    return -1;
  }
  // The struct-returning calls. The results are read back after the call:
  // the values the callee wrote through the hidden pointer have to be the
  // ones the callee wrote.
  const FixtureBig big = cs2_fixture_big(total);
  const FixtureSmall little = cs2_fixture_small(total);
  const FixtureMixed mixed = cs2_fixture_mixed(total, 2.5);
  const FixtureBig cross = cs2_fixture_cross(total);
  // The result of this one is discarded: the callee still writes through
  // the hidden pointer.
  (void)cs2_fixture_big(total + 1);
  total = cs2_fixture_variadic(total, 1, 2, 3);
  total += static_cast<int>(big.a + big.b + big.c);
  total += little.a + static_cast<int>(little.b);
  total += static_cast<int>(mixed.a + mixed.b + mixed.d);
  total += static_cast<int>(cross.a + cross.b + cross.c);
  total += mixed.c == &cs2_fixture_counter ? 1 : 0;
  // The exception of a struct-returning callee has to reach this handler
  // through the thunk. The value is negative for a positive total, so the
  // callee throws.
  try {
    const FixtureBig thrown = cs2_fixture_throwing(-total);
    total += static_cast<int>(thrown.a);
  } catch (int) {
    total += 11;
  }
  total = cs2_fixture_helper(total);
  [[clang::musttail]] return cs2_fixture_tail(total);
}

// The probe: the export the test calls. The value carries the result **and**
// the counter, so a call that reached the wrong function changes it.
extern "C" __declspec(dllexport) unsigned long long cs2_fixture_probe(int seed) {
  const int value = cs2_fixture_caller(seed);
  return (static_cast<unsigned long long>(cs2_fixture_counter) << 32) |
         (static_cast<unsigned long long>(value) & 0xFFFFFFFFULL);
}

#if defined(_WIN32)
// The DllMain of the payload: the loader calls it with the real handle.
extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                                                      LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
#endif
