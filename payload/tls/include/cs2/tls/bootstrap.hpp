#pragma once

// Per-thread TLS bootstrap for a manually-mapped image.
//
// The Windows loader allocates a per-thread TLS block and runs TLS callbacks /
// `DllMain(DLL_THREAD_ATTACH)` for every loaded module whenever a thread is
// created (`LdrpInitializeThread`). A manually-mapped DLL is bypassed by that
// machinery, so only the injector's entry thread gets a TLS block (set by the
// entry stub). Any *new* thread the payload creates would run with an
// uninitialized per-thread CRT block, which crashes intermittently.
//
// This must be called at the very start of every thread that runs payload code
// (after the thread is created). It is idempotent: if the loader or a previous
// call already installed a block for the current thread it does nothing.

namespace cs2::tls {

// Install a per-thread TLS block for the mapped image on the calling thread.
// Idempotent: if the loader or a previous call already installed a block it
// does nothing. Call at the very start of any thread that runs payload code.
void ensure_thread_tls() noexcept;

// Native Windows uses a normal loader-managed thread. Proton/cross builds
// create a thread that skips `LdrpInitializeThread` (no TLS callbacks and no
// `DllMain(DLL_THREAD_ATTACH)` for any module in the process). This is the
// m417z/SkipThreadAttach approach: `CreateThread` would run every loaded
// module's thread-attach handler (DXVK/game included), which is what wedged the
// game on a mode change. The start routine must call `ensure_thread_tls()`
// itself, because the loader will not have initialized TLS for it.
// Returns the thread handle, or nullptr on failure.
void* create_thread_no_attach(void* (*start)(void*), void* parameter) noexcept;

}  // namespace cs2::tls
