// The fault guard the probe's engine call runs under (`docs/re/bullet-trace.md`).
//
// The payload is manually mapped and has **no registered exception tables**, so
// `diag::call_guarded` (the vectored handler + `setjmp`/`longjmp`) is the only
// thing between a wrong engine ABI and a dead game. The probe's engine call is
// guarded by the probe's own guard (`cs2/control/trace_guard.hpp`), and this test
// proves it: a call that **deliberately faults** is caught and reported, and the
// process keeps running.
//
// Windows-only: the guard is the vectored exception handler, which exists only on
// Windows (`cs2/control/trace_guard.hpp`). On another platform the guard is a
// stub that refuses the call, and the test says so instead of pretending.

#include <cstdint>
#include <cstdio>

#include "cs2/control/trace_guard.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

// A call that does not fault: the guard must report it as a success.
void call_that_works(void* context) {
  *static_cast<int*>(context) = 42;
}

// A call that faults on purpose: a write through the (null) context pointer. The
// write goes through a `volatile` pointer so the compiler cannot fold it away.
void call_that_faults(void* context) {
  auto* target = static_cast<volatile std::uintptr_t*>(context);
  *target = 0xDEADBEEFU;
}

}  // namespace

auto main() -> int {
#if defined(_WIN32)
  // A call that works: the guard reports the success and the side effect happened.
  int value = 0;
  expect(cs2::control::trace_call_guarded(&call_that_works, &value),
         "a call that works is reported as a success");
  expect(value == 42, "the guarded call's side effect happened");

  // A call that faults: the guard catches it and reports the failure. The
  // context is a null pointer, so the call writes through it.
  const bool recovered = cs2::control::trace_call_guarded(&call_that_faults, nullptr);
  expect(!recovered, "a faulting call is reported as a failure, not a crash");

  // The process is still here — the guard recovered, so the game would live.
  expect(cs2::control::trace_call_guarded(&call_that_works, &value),
         "the process survived the fault and can still make guarded calls");
  expect(value == 42, "the process is still running its own code");

  // A null function is refused (never a call through a null pointer).
  expect(!cs2::control::trace_call_guarded(nullptr, &value),
         "a null call is refused");

  if (failures == 0) {
    std::printf("control-trace-probe-guard: ok\n");
    return 0;
  }
  return 1;
#else
  std::printf("control-trace-probe-guard: skipped (the guard is Windows-only)\n");
  return 0;
#endif
}
