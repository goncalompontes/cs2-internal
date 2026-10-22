#include "cs2/diag/crash_handler.hpp"

#include <csetjmp>

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

// The recovery guard (`call_guarded`). One in-flight guarded call per process is
// enough (the payload's engine probes are serialised on the control thread).
std::jmp_buf g_guard_jmp;
volatile long g_guard_active = 0;
unsigned long g_guard_thread = 0;

[[nodiscard]] auto is_recoverable(DWORD code) noexcept -> bool {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
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
// A `noexcept` function is `nounwind`: clang-cl gives its frame a C++
// exception-handler unwind record whose cleanup action is `std::terminate`
// (an exception must not escape a `noexcept` function). The guard leaves this
// frame with a `longjmp`, and MSVC's `longjmp` unwinds the frames it leaves --
// so leaving a `nounwind` frame runs that frame's terminate action. The
// process then dies with `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) instead of
// returning to the `setjmp`: the guard would turn "a bad engine call logs a
// line" into "a bad engine call kills the game".
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
  if (g_guard_active != 0 && g_guard_thread == GetCurrentThreadId() && is_recoverable(code)) {
    g_guard_active = 0;
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

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
  log_exception("unhandled", info);
  return EXCEPTION_CONTINUE_SEARCH;
}

// The guard's frame must not be a C++ exception frame.
//
// A `noexcept` handler is `nounwind`: clang-cl gives its frame a C++
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
// a `noexcept`; it is spelled the same way anyway, because the payload's
// sources use no C++ exceptions and `noexcept` buys nothing here: it only
// turns the frame into a C++ exception frame.

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
  if (fn == nullptr || g_guard_active != 0) {
    return false;
  }
  g_guard_thread = GetCurrentThreadId();
  if (std::setjmp(g_guard_jmp) != 0) {
    g_guard_active = 0;
    return false;  // recovered from a fault inside `fn`
  }
  g_guard_active = 1;
  fn(arg);
  g_guard_active = 0;
  return true;
#else
  (void)fn;
  (void)arg;
  return false;
#endif
}

}  // namespace cs2::diag
