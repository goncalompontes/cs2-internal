#include "cs2/diag/watchdog.hpp"

#include <atomic>
#include <cstdint>

#include "cs2/diag/text.hpp"
#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <tlhelp32.h>

#include "cs2/core/handle.hpp"
#include "cs2/core/win_handle.hpp"
#endif

namespace cs2::diag {
namespace {

#if defined(_WIN32)

// A frame is expected well within this; the report is throttled to the same
// period so a long freeze does not flood the log.
constexpr unsigned long long k_stall_ms = 10000;
constexpr unsigned long long k_poll_ms = 2000;

using LookupFunctionEntryFn = PRUNTIME_FUNCTION(NTAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);
using VirtualUnwindFn = PVOID(NTAPI*)(DWORD, DWORD64, DWORD64, PRUNTIME_FUNCTION, PCONTEXT, PVOID*,
                                      PDWORD64, PKNONVOLATILE_CONTEXT_POINTERS);

std::atomic<unsigned long long> g_heartbeat{0};
std::atomic<unsigned long long> g_last_report{0};
std::atomic<bool> g_installed{false};
LookupFunctionEntryFn g_lookup_function_entry = nullptr;
VirtualUnwindFn g_virtual_unwind = nullptr;

void resolve_unwind() noexcept {
  const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (ntdll == nullptr) {
    return;
  }
  g_lookup_function_entry =
      reinterpret_cast<LookupFunctionEntryFn>(GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
  g_virtual_unwind =
      reinterpret_cast<VirtualUnwindFn>(GetProcAddress(ntdll, "RtlVirtualUnwind"));
}

auto read_memory(std::uintptr_t address, void* out, std::size_t size) noexcept -> bool {
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size,
                           &got) != 0 &&
         got == size;
}

// Walk a suspended thread's stack using the PE unwind info (x64 has no frame
// pointer chain). Falls back to the raw return address for leaf frames.
void unwind(CONTEXT& context, unsigned long thread_id) noexcept {
  for (int frame = 0; frame < 16; ++frame) {
    char line[128];
    TextBuffer text(line, sizeof(line));
    text += "[freeze]   tid=";
    text.dec(thread_id);
    text += " #";
    text.dec(frame);
    text += ' ';
    text.address(static_cast<std::uintptr_t>(context.Rip));
    cs2::game::append_log(text.view());

    if (g_lookup_function_entry == nullptr || g_virtual_unwind == nullptr) {
      break;
    }
    DWORD64 image_base = 0;
    const PRUNTIME_FUNCTION entry = g_lookup_function_entry(context.Rip, &image_base, nullptr);
    if (entry == nullptr) {
      std::uintptr_t return_address = 0;
      if (!read_memory(static_cast<std::uintptr_t>(context.Rsp), &return_address,
                       sizeof(return_address))) {
        break;
      }
      context.Rip = return_address;
      context.Rsp += sizeof(void*);
      continue;
    }
    PVOID handler_data = nullptr;
    DWORD64 establisher = 0;
    KNONVOLATILE_CONTEXT_POINTERS pointers{};
    g_virtual_unwind(0, image_base, context.Rip, entry, &context, &handler_data, &establisher,
                     &pointers);
  }
}

void log_thread(const THREADENTRY32& entry) noexcept {
  // The handle is owned by the wrapper: it is released on every path out of
  // this function. The wrapper allocates nothing, so this stays usable from
  // the crash handler's cold path.
  core::ScopedKernelHandle thread(
      OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                 entry.th32ThreadID));
  if (!thread.valid()) {
    return;
  }
  SuspendThread(static_cast<HANDLE>(thread.get()));
  CONTEXT context{};
  context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  if (GetThreadContext(static_cast<HANDLE>(thread.get()), &context) != 0) {
    unwind(context, entry.th32ThreadID);
  }
  ResumeThread(static_cast<HANDLE>(thread.get()));
}

void report_freeze(unsigned long long stalled_ms) noexcept {
  char line[160];
  TextBuffer text(line, sizeof(line));
  text += "[freeze] no frame for ";
  text.dec(stalled_ms / 1000);
  text += "s (heartbeats=";
  text.dec(g_heartbeat.load());
  text += ')';
  cs2::game::append_log(text.view());

  core::ScopedKernelHandle snapshot(
      CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
  if (!snapshot.valid()) {
    return;
  }
  const DWORD self = GetCurrentProcessId();
  const DWORD current = GetCurrentThreadId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Thread32First(static_cast<HANDLE>(snapshot.get()), &entry) != 0) {
    do {
      if (entry.th32OwnerProcessID == self && entry.th32ThreadID != current) {
        log_thread(entry);
      }
    } while (Thread32Next(static_cast<HANDLE>(snapshot.get()), &entry) != 0);
  }
}

DWORD WINAPI watchdog_thread(void*) noexcept {
  unsigned long long last = 0;
  unsigned long long last_change = GetTickCount64();
  bool reported = false;
  while (true) {
    Sleep(k_poll_ms);
    const unsigned long long now = GetTickCount64();
    const unsigned long long current = g_heartbeat.load();
    if (current != last) {
      last = current;
      last_change = now;
      reported = false;
      continue;
    }
    const unsigned long long stalled = now - last_change;
    // Report a stall once per episode: repeatedly suspending every thread to
    // capture stacks is itself destabilizing under Wine.
    if (stalled >= k_stall_ms && !reported) {
      reported = true;
      g_last_report.store(now);
      report_freeze(stalled);
    }
  }
}

#endif

}  // namespace

void heartbeat() noexcept {
#if defined(_WIN32)
  g_heartbeat.fetch_add(1, std::memory_order_relaxed);
#endif
}

void install_watchdog() noexcept {
#if defined(_WIN32)
  if (g_installed.exchange(true)) {
    return;
  }
  capture_modules();
  resolve_unwind();
  CreateThread(nullptr, 0, &watchdog_thread, nullptr, 0, nullptr);
#endif
}

}  // namespace cs2::diag
