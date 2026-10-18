#pragma once

// Crash diagnostics.
//
// Installs a first-chance vectored exception handler and an unhandled-exception
// filter that append the exception code, the faulting address (resolved to
// `module+offset`) and a stack trace to the payload log, so a crash inside the
// payload or the hooked Present is identifiable after the process dies.
//
// The handlers allocate nothing and take no locks: `append_log` uses Win32 file
// I/O and the module table is captured up front.

namespace cs2::diag {

void install_crash_handler() noexcept;

// Run `fn(arg)` under a recovery guard. If an access violation (or another
// recoverable fault) occurs **on this thread** while the call is active, the
// process is recovered to just after `call_guarded`, the fault is logged, and it
// returns false. Otherwise it returns true once `fn` returns.
//
// This exists because the payload is manually mapped with no exception tables, so
// a C++ `__try/__except` cannot be registered; the guard uses the vectored
// handler + `longjmp` instead. Use it only for read-only calls whose partial
// side effects are acceptable (e.g. probing a fault-prone engine query).
[[nodiscard]] auto call_guarded(void (*fn)(void*), void* arg) noexcept -> bool;

}  // namespace cs2::diag
