#include "cs2/diag/crash_handler.hpp"

#include <atomic>
#include <csetjmp>
#include <cstdint>

#include "cs2/diag/text.hpp"
#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::diag {
namespace {

#if defined(_WIN32)

using CaptureBackTraceFn = unsigned short(__stdcall*)(unsigned long, unsigned long, void**,
                                                      unsigned long*);

CaptureBackTraceFn g_capture_backtrace = nullptr;
int g_first_chance_logged = 0;
constexpr int k_first_chance_limit = 64;

// Not all of these are in <windows.h>.
constexpr DWORD k_status_stack_buffer_overrun = 0xC0000409UL;
constexpr DWORD k_status_fatal_app_exit = 0x40000015UL;

// The guard's slot states. A Windows thread id is never 0 and never one of
// the two sentinels, so a thread id in `g_guard_owner` always means "armed".
constexpr std::uint32_t k_guard_free = 0;
constexpr std::uint32_t k_guard_claiming = 0xFFFFFFFFUL;
constexpr std::uint32_t k_guard_unwinding = 0xFFFFFFFEUL;

// The recovery guard (`call_guarded`). One in-flight guarded call per process is
// enough (the payload's engine probes are serialised on the control thread).
//
// The jump target is a **single process-wide buffer**, so the guard is owned by
// exactly one thread at a time. `g_guard_owner` says who owns it:
//
//   * `k_guard_free`      - nobody: the slot is available.
//   * `k_guard_claiming`  - a thread has taken the slot and is writing the
//                           jump target; the target is not valid yet.
//   * `k_guard_unwinding` - the owner is unwinding out of the slot (its
//                           `longjmp` is in flight); the slot is still its own.
//   * any other value     - the owning thread's id: the guard is armed.
//
// The arming is a **compare-exchange**, so two threads can never both believe
// they own the slot: the second `setjmp` can never overwrite the first
// thread's jump target. (The plain `volatile long` this replaced was a
// check-then-set, and the check and the set were not one step.)
std::jmp_buf g_guard_jmp;
std::atomic<std::uint32_t> g_guard_owner{k_guard_free};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "the guard's arming must be lock-free: the payload is manually mapped and "
              "has no CRT to provide a lock");

[[nodiscard]] auto is_recoverable(DWORD code) noexcept -> bool {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    // `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) is the code the **broken**
    // guard produced, and a fast fail bypasses the vectored handlers -- so it
    // is unreachable in practice. It stays in the list because the list is the
    // guard's contract, not a measurement of what the hardware delivers.
    case k_status_stack_buffer_overrun:
    case k_status_fatal_app_exit:
      return true;
    default:
      return false;
  }
}

void resolve_backtrace() noexcept {
  const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (ntdll != nullptr) {
    g_capture_backtrace =
        reinterpret_cast<CaptureBackTraceFn>(GetProcAddress(ntdll, "RtlCaptureStackBackTrace"));
  }
}

void log_stack(unsigned long skip) noexcept {
  if (g_capture_backtrace == nullptr) {
    return;
  }
  void* frames[32] = {};
  const unsigned short count = g_capture_backtrace(skip, 32, frames, nullptr);
  for (unsigned short i = 0; i < count; ++i) {
    char line[96];
    TextBuffer text(line, sizeof(line));
    text += "[crash]   #";
    text.dec(i);
    text += ' ';
    text.address(reinterpret_cast<std::uintptr_t>(frames[i]));
    cs2::game::append_log(text.view());
  }
}

// Fallback walk of the RBP chain from the faulting context. `RtlCaptureStackBackTrace`
// yields nothing for a manually-mapped module in some contexts, and this needs no
// imports. Every read is bounds-checked against the faulting stack.
void log_frame_chain(const CONTEXT* context) noexcept {
  if (context == nullptr) {
    return;
  }
  const auto stack = static_cast<std::uintptr_t>(context->Rsp);
  auto frame = static_cast<std::uintptr_t>(context->Rbp);
  const std::uintptr_t limit = stack + 0x40000;
  int emitted = 0;
  while (emitted < 24 && frame >= stack && frame < limit && (frame & 7) == 0) {
    const auto next = *reinterpret_cast<const std::uintptr_t*>(frame);
    const auto ret = *reinterpret_cast<const std::uintptr_t*>(frame + 8);
    if (ret == 0) {
      break;
    }
    char line[96];
    TextBuffer text(line, sizeof(line));
    text += "[crash]   fp#";
    text.dec(emitted);
    text += ' ';
    text.address(ret);
    cs2::game::append_log(text.view());
    ++emitted;
    if (next <= frame) {
      break;
    }
    frame = next;
  }
}

void log_exception(const char* phase, EXCEPTION_POINTERS* info) noexcept {
  char line[256];
  TextBuffer text(line, sizeof(line));
  text += "[crash] ";
  text += phase;
  text += " code=0x";
  text.hex(info->ExceptionRecord->ExceptionCode);
  text += " at ";
  text.address(reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress));
  if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
      info->ExceptionRecord->NumberParameters >= 2) {
    const ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
    text += kind == 0 ? " read" : (kind == 1 ? " write" : " exec");
    text += " addr=";
    text.address(info->ExceptionRecord->ExceptionInformation[1]);
  }
  if (info->ContextRecord != nullptr) {
    text += " rip=";
    text.address(static_cast<std::uintptr_t>(info->ContextRecord->Rip));
  }
  cs2::game::append_log(text.view());
  log_stack(2);  // trim log_stack/log_exception themselves
  log_frame_chain(info->ContextRecord);
}

// The vectored handler is deliberately **not** `noexcept`.
//
// A `noexcept` function is not `nounwind`: clang-cl gives its frame a **C++
// exception-handler unwind record** (flags `ExceptionHandler (0x1) |
// TerminateHandler (0x2)`, handler `__CxxFrameHandler3`) whose cleanup action
// is `std::terminate` -- an exception must not escape a `noexcept` function.
// The guard leaves this frame with a `longjmp`, and MSVC's `longjmp` unwinds
// the frames it leaves -- so leaving that frame runs its terminate action.
// The process then dies with `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409)
// instead of returning to the `setjmp`: the guard would turn "a bad engine
// call logs a line" into "a bad engine call kills the game". The plain
// handler's record has no handler at all (flags `0x0`), measured with
// `llvm-readobj --unwind`.
//
// Measured with clang-cl 22.1.8 (the compiler the payload ships with): with
// the `noexcept` the guarded call never returns and the process exits with
// 0xC0000409; without it the guard recovers. The regression is pinned by
// `payload/diag/tests/test_diag_guard.cpp`, which is built with clang-cl.
LONG CALLBACK vectored_handler(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  // A guarded call is in flight on this thread and the fault is recoverable:
  // unwind to `call_guarded`. The `longjmp` unwinds the frames it leaves, so
  // the payload's unwind tables have to be registered -- the injector does
  // that (`RtlAddFunctionTable`), see `docs/architecture.md`.
  //
  // The owner is the armed thread's id, so the fault has to be on **that**
  // thread: a fault on another thread is not recovered (the first-chance path
  // below logs it and the search continues).
  const std::uint32_t owner = g_guard_owner.load(std::memory_order_acquire);
  if (owner != k_guard_claiming && owner != k_guard_unwinding &&
      owner == static_cast<std::uint32_t>(GetCurrentThreadId()) && is_recoverable(code)) {
    // The slot stays **owned** while the unwind is in flight -- so no other
    // thread can take it and overwrite the jump target -- but it is no longer
    // armed: a second fault must not enter the `longjmp` again.
    g_guard_owner.store(k_guard_unwinding, std::memory_order_release);
    log_exception("guarded", info);
    std::longjmp(g_guard_jmp, 1);
  }
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case k_status_stack_buffer_overrun:
    case k_status_fatal_app_exit:
      if (g_first_chance_logged < k_first_chance_limit) {
        ++g_first_chance_logged;
        log_exception("first-chance", info);
      }
      break;
    default:
      break;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) noexcept {
  log_exception("unhandled", info);
  return EXCEPTION_CONTINUE_SEARCH;
}

// The guard's frame must not be a C++ exception frame.
//
// A `noexcept` handler is not `nounwind`: clang-cl gives its frame a C++
// exception-handler unwind record whose cleanup action is `std::terminate`,
// and the guard leaves that frame by a `longjmp`, which unwinds it -- so the
// frame's cleanup runs and the process dies with 0xC0000409 instead of
// recovering.
//
// There is **no compile-time check** for this, and that is worth knowing: the
// `noexcept` operator reports the *implicit* exception specification, and a
// function with no throwing operations has `noexcept(true)` whether or not it
// spells `noexcept` -- so `noexcept(handler)` cannot tell the two apart. The
// checks are therefore behavioural and textual: `payload/diag/tests/
// test_diag_guard.cpp` is built with clang-cl (the `noexcept` version of the
// handler makes the process die with 0xC0000409) and it reads this file and
// fails when the spelling comes back.
//
// The unhandled filter never leaves its frame by a jump, so it would survive
// a `noexcept` -- and it keeps it: the filter is not part of the recovery
// mechanism, so its frame does not need to be a C++ exception frame, and the
// ordinary spelling is the one every other function in the payload uses.

#endif

}  // namespace

void install_crash_handler() noexcept {
#if defined(_WIN32)
  capture_modules();
  resolve_backtrace();
  // Prime the log now, so the handler never resolves CreateFileA on a cold path.
  cs2::game::append_log("[crash] handler installed");
  AddVectoredExceptionHandler(1, &vectored_handler);
  SetUnhandledExceptionFilter(&unhandled_filter);
#endif
}

auto call_guarded(void (*fn)(void*), void* arg) noexcept -> bool {
#if defined(_WIN32)
  if (fn == nullptr) {
    return false;
  }
  // Take the slot. The compare-exchange is what makes "one guarded call at a
  // time" true: a second thread does not get past it, and it never writes
  // the jump target, so it can never overwrite the first thread's.
  std::uint32_t expected = k_guard_free;
  if (!g_guard_owner.compare_exchange_strong(expected, k_guard_claiming,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
    return false;  // a guarded call is already in flight: this one is refused
  }
  // The jump target is written while the slot is ours and nobody else's.
  if (std::setjmp(g_guard_jmp) != 0) {
    g_guard_owner.store(k_guard_free, std::memory_order_release);
    return false;  // recovered from a fault inside `fn`
  }
  // The owner is published only now: from here on the handler knows which
  // thread may jump to the target, and the target is this thread's.
  g_guard_owner.store(static_cast<std::uint32_t>(GetCurrentThreadId()),
                      std::memory_order_release);
  fn(arg);
  g_guard_owner.store(k_guard_free, std::memory_order_release);
  return true;
#else
  (void)fn;
  (void)arg;
  return false;
#endif
}

}  // namespace cs2::diag
