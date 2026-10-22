#pragma once

namespace cs2::diag {

// Record a frame from the render loop. The watchdog reports a freeze when this
// stops advancing.
void heartbeat() noexcept;

// Start the freeze watchdog thread. When the heartbeat stalls it appends a
// report (with per-thread stacks, unwound via the PE unwind info) to the
// payload log.
void install_watchdog() noexcept;

}  // namespace cs2::diag
