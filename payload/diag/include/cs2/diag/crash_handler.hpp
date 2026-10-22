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
// This exists because the payload is manually mapped: the loader does not
// register the image's exception tables, so the payload does not lean on the
// C++ exception machinery. The injector registers the `.pdata` explicitly --
// and the guard's `longjmp` needs it, because it unwinds the frames it leaves.
// The fault itself is caught in a vectored handler and turned back into a
// return. Use it only for read-only calls whose partial side effects are
// acceptable (e.g. probing a fault-prone engine query).
//
// **The limits of the guard** (they are the guard's contract, not accidents):
//
//   * **One guarded call at a time, per process.** A second concurrent guarded
//     call is refused (it returns false without calling `fn`), because the
//     jump target is a single process-wide slot. The arming is a
//     compare-exchange, so the refusal is **enforced**: two threads can never
//     both take the slot, and the second one never overwrites the first
//     thread's jump target.
//   * **The faulting thread only.** The handler recovers only when the fault
//     happens on the thread that entered the guard; a fault on another thread
//     is logged by the first-chance path and kills the process.
//   * **The recoverable codes only**: `EXCEPTION_ACCESS_VIOLATION`,
//     `EXCEPTION_ILLEGAL_INSTRUCTION`, `EXCEPTION_INT_DIVIDE_BY_ZERO`,
//     `EXCEPTION_PRIV_INSTRUCTION`, `EXCEPTION_STACK_OVERFLOW`,
//     `STATUS_STACK_BUFFER_OVERRUN` and `STATUS_FATAL_APP_EXIT`. Anything else
//     is logged and continues the search.
//     (`STATUS_STACK_BUFFER_OVERRUN` -- 0xC0000409 -- is the code the **broken**
//     guard produced, and a fast fail bypasses the vectored handlers, so it
//     is unreachable in practice. It is listed because the list is the
//     contract, not a measurement of what the hardware delivers.)
//   * **The guarded call must not own resources.** The recovery is a `longjmp`,
//     so the guarded call's frame (and every frame between it and the fault)
//     is left without running its cleanups. `fn` must therefore be a read-only
//     probe whose locals need no destruction.
//   * The handler that owns the jump is **not** `noexcept`. A `noexcept` frame
//     is not `nounwind`: clang-cl gives it a C++ exception-handler unwind
//     record whose cleanup action is `std::terminate`, and the `longjmp` out
//     of that frame runs the action -- so the process dies with 0xC0000409
//     instead of recovering. `payload/diag/src/crash_handler.cpp` explains it,
//     and `payload/diag/tests/test_diag_guard.cpp` checks it (behaviourally,
//     built with clang-cl, and textually).
//
// The guard is the tree's **only** recovery mechanism: `cs2::diag` owns it and
// everything that needs a guarded engine call uses it. The tree's test fails
// when a second one appears outside `payload/diag` -- a vectored handler, an
// unhandled filter, a `setjmp`/`longjmp`, or an SEH `__try`/`__except`.
[[nodiscard]] auto call_guarded(void (*fn)(void*), void* arg) noexcept -> bool;

}  // namespace cs2::diag
