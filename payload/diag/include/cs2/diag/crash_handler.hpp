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

}  // namespace cs2::diag
