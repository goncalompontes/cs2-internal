#pragma once

// The trace probe's fault guard (Windows).
//
// The payload is manually mapped and has **no registered exception tables**, so a
// fault inside an engine call cannot be caught by the C++ machinery: it must be
// caught by a vectored exception handler and turned back into a *failure*. A wrong
// ABI that is not caught is a dead game.
//
// **Why the probe has its own guard and not `cs2::diag::call_guarded`.**
//
// `cs2::diag`'s vectored handler is a **`noexcept`** function. clang-cl marks a
// `noexcept` function `nounwind`, and MSVC's `longjmp` then refuses to unwind
// through that frame: it raises `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) instead
// of returning. Measured on this machine (clang-cl 22.1.8, MSVC 14.51 CRT):
//
// * the shared guard **cannot recover** — the payload's own log shows a
//   "[crash] guarded" line followed by a fresh "[crash] handler installed" nine
//   times over (the process died and was re-injected);
// * the same code built with MSVC's `cl.exe` **does** recover.
//
// The probe's engine call is the one call that must never kill the game, so it
// runs under this guard: the same vectored handler + `setjmp`/`longjmp`, with a
// handler the unwinder will cross.
//
// The repo-wide fix is one word — drop the `noexcept` from the handler in
// `cs2/diag/crash_handler.cpp` — but that file is not this task's to change, so
// the fix is reported instead of applied.

#include <csetjmp>
#include <cstdint>

#if defined(_WIN32)
// `windows.h` defines `min`/`max` as macros, which break `std::min`/`std::max` in
// the including translation unit (the same guard the game module's memory reader
// uses).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cs2::control {

#if defined(_WIN32)

namespace trace_guard {

// The guard's state. One copy for the whole program (an `inline` variable), behind
// a typed accessor — the same shape as the diag module's guard.
struct State {
  std::jmp_buf jump{};        // the jump target
  volatile long active = 0;   // a guarded call is in flight
  volatile unsigned long thread = 0;  // the thread that owns it
  volatile long installed = 0;        // the handler is registered
};

inline State g_state{};

// The fault codes the guard recovers from (the diag module's own list).
[[nodiscard]] inline auto is_recoverable(DWORD code) noexcept -> bool {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000409UL:  // STATUS_STACK_BUFFER_OVERRUN
    case 0x40000015UL:  // STATUS_FATAL_APP_EXIT
      return true;
    default:
      return false;
  }
}

// The vectored handler.
//
// **Not `noexcept`** — that is the whole point of this file (see the header's
// comment): a `noexcept` handler is `nounwind` under clang-cl, and MSVC's
// `longjmp` will not unwind through it.
LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (g_state.active != 0 && g_state.thread == GetCurrentThreadId() &&
      is_recoverable(code)) {
    g_state.active = 0;
    std::longjmp(g_state.jump, 1);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// Register the handler once. The payload's own crash handler registers one too;
// the vectored handlers run from the most recently registered to the least, so
// this one sees the fault first while a guarded call is in flight.
inline void install() noexcept {
  if (g_state.installed != 0) {
    return;
  }
  g_state.installed = 1;
  AddVectoredExceptionHandler(1, &handler);
}

}  // namespace trace_guard

// Run `fn(arg)` under the guard. Returns false when the call faulted: the fault
// was caught, the process is still alive, and the caller reports it as a result.
//
// One guarded call at a time (the probe is serial): a second one is refused
// rather than allowed to overwrite the jump target.
[[nodiscard]] inline auto trace_call_guarded(void (*fn)(void*), void* arg) noexcept -> bool {
  if (fn == nullptr) {
    return false;
  }
  trace_guard::install();
  trace_guard::State& state = trace_guard::g_state;
  if (state.active != 0) {
    return false;
  }
  state.thread = GetCurrentThreadId();
  if (std::setjmp(state.jump) != 0) {
    state.active = 0;
    return false;  // recovered from a fault inside `fn`
  }
  state.active = 1;
  fn(arg);
  state.active = 0;
  return true;
}

#else  // !_WIN32

// The guard is the vectored exception handler, which exists only on Windows. On
// another platform the probe's engine call is refused outright (the payload never
// runs there).
[[nodiscard]] inline auto trace_call_guarded(void (*fn)(void*), void* arg) noexcept -> bool {
  (void)fn;
  (void)arg;
  return false;
}

#endif

}  // namespace cs2::control
