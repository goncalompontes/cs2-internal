// The payload's fault guard (`cs2::diag::call_guarded`).
//
// The guard is the payload's **only** safety net for an engine call that
// faults: the payload is manually mapped, so a C++ `__try/__except` cannot be
// registered, and the guard is a vectored exception handler plus a
// `setjmp`/`longjmp` instead. Two things about it are proven here.
//
// **1. The guard recovers.** A call that deliberately faults (a write through a
// null pointer) is caught, the process survives, and the fault is logged -- ten
// times in a row. This is the behaviour the whole "a wrong engine call logs a
// line instead of killing the game" argument rests on.
//
// **2. The guard is built with the compiler the payload ships with.**
// clang-cl, not MSVC. That is the point of the test: a `noexcept` vectored
// handler is `nounwind`, and the `longjmp` out of a `nounwind` frame runs the
// frame's `std::terminate` cleanup instead of recovering, so the process dies
// with `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409). Under MSVC's `cl.exe` the
// same source recovers, so **a host test built with `cl.exe` cannot see the
// defect at all** -- and the host suite was the only suite there was. The
// test therefore prints the compiler it was built with, and the payload's own
// build (the one that ships) registers it as a test of its own.
//
// The last check is a **source** check over the repository: the tree must hold
// exactly **one** recovery mechanism. The trace probe (T39) wrote a second
// vectored handler with its own `setjmp`/`longjmp` because it could not use
// this one, and two mechanisms -- one of which does not work -- are how this
// defect survived. The check reads the repository's own sources, so a second
// guard cannot come back unnoticed.
//
// Windows-only: the guard is the vectored exception handler, which exists only
// on Windows. On another platform the guard is a stub that refuses the call,
// and the test says so instead of pretending.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cs2/diag/crash_handler.hpp"
#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// The number of deliberate faults the test makes. Ten, because a guard that
// recovers once could still be a guard that recovers once: the guard's state
// (`g_guard_active`, the jump target) has to be re-armed every round.
constexpr int k_fault_rounds = 10;

// The name of the compiler the test was built with. The guard's behaviour
// depends on it, so the test says which one it is.
auto compiler_name() -> const char* {
#if defined(__clang__)
  return "clang-cl";
#elif defined(_MSC_VER)
  return "msvc";
#else
  return "unknown";
#endif
}

// A call that does not fault: the guard must report it as a success.
void call_that_works(void* context) {
  *static_cast<int*>(context) = 42;
}

// A call that faults on purpose: a write through the (null) context pointer.
// The write goes through a `volatile` pointer so the compiler cannot fold it
// away.
void call_that_faults(void* context) {
  auto* target = static_cast<volatile std::uintptr_t*>(context);
  *target = 0xDEADBEEFU;
}

#if defined(_WIN32)

// The payload's log directory, redirected so the test reads **its own** log
// and not the game's. `cs2::game::append_log` reads `CS2_PAYLOAD_DIR`, so the
// test points it at a directory of its own and reads the file back.
auto log_directory() -> std::filesystem::path {
  const auto temp = std::filesystem::temp_directory_path();
  return temp / ("cs2-diag-guard-" + std::to_string(GetCurrentProcessId()));
}

// Read the payload's log and count the lines that report a guarded fault.
auto count_guarded_lines(const std::filesystem::path& path) -> int {
  std::ifstream file(path);
  if (!file.good()) {
    return -1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string content = buffer.str();
  int count = 0;
  for (std::size_t at = content.find("[crash] guarded"); at != std::string::npos;
       at = content.find("[crash] guarded", at + 1)) {
    ++count;
  }
  return count;
}

// The guard recovers from a deliberate fault, ten times in a row, and the
// process is still running afterwards.
void test_the_guard_recovers_from_a_deliberate_fault() {
  const auto directory = log_directory();
  _putenv_s("CS2_PAYLOAD_DIR", directory.string().c_str());

  char log_path[MAX_PATH] = {};
  assert(cs2::game::data_file_path("cs2-payload.log", log_path, sizeof(log_path)));
  const std::filesystem::path log_file(log_path);
  // A previous run under the same process id would have left lines behind.
  std::filesystem::remove(log_file);

  cs2::diag::install_crash_handler();

  // Ten deliberate faults. Each one must be caught, reported as a failure, and
  // leave the process alive.
  for (int round = 0; round < k_fault_rounds; ++round) {
    const bool recovered = cs2::diag::call_guarded(&call_that_faults, nullptr);
    assert(!recovered);
  }

  // The process is still here: it runs its own code, and the guard is still
  // armed (a guarded call that works is reported as a success).
  int value = 0;
  assert(cs2::diag::call_guarded(&call_that_works, &value));
  assert(value == 42);

  // The fault was logged: the log is the only evidence a fault happened, and
  // it must carry one line per deliberate fault.
  const int lines = count_guarded_lines(log_file);
  assert(lines == k_fault_rounds);

  // A null call is refused (never a call through a null pointer).
  assert(!cs2::diag::call_guarded(nullptr, &value));

  std::filesystem::remove(log_file);
  std::filesystem::remove(directory);
}

#endif  // defined(_WIN32)

// --- the source check over the repository ---------------------------------

auto read_file(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file.good()) {
    return {};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// The source with its comments and string literals removed.
//
// The rule is about the **code**: the files document the rule in prose ("the
// guard is the tree's only recovery mechanism"), and a comment that states the
// rule must not trip the check that enforces it.
auto strip_comments(const std::string& source) -> std::string {
  std::string out;
  out.reserve(source.size());
  bool in_block = false;
  bool in_string = false;
  bool in_char = false;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
    const char next = i + 1 < source.size() ? source[i + 1] : '\0';
    if (in_block) {
      if (c == '*' && next == '/') {
        in_block = false;
        ++i;
      }
      continue;
    }
    if (in_string) {
      if (c == '\\') {
        ++i;
        continue;
      }
      if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (in_char) {
      if (c == '\\') {
        ++i;
        continue;
      }
      if (c == '\'') {
        in_char = false;
      }
      continue;
    }
    if (c == '/' && next == '*') {
      in_block = true;
      ++i;
      continue;
    }
    if (c == '/' && next == '/') {
      while (i < source.size() && source[i] != '\n') {
        ++i;
      }
      out.push_back('\n');
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '\'') {
      in_char = true;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

// The handler's declaration must not spell `noexcept`.
//
// The word cannot be caught at compile time: the `noexcept` operator reports
// the *implicit* exception specification, and a function with no throwing
// operations has `noexcept(true)` whether or not it spells `noexcept`, so the
// operator cannot tell the two apart. The spelling is therefore checked
// here. The behavioural half of the same check is the test itself: with the
// `noexcept` the process dies with 0xC0000409 instead of recovering.
void test_the_handler_is_not_noexcept(const std::filesystem::path& file) {
  const std::string code = strip_comments(read_file(file));
  assert(!code.empty());
  for (const char* name : {"vectored_handler", "unhandled_filter"}) {
    const std::size_t at = code.find(name);
    assert(at != std::string::npos);
    const std::size_t brace = code.find('{', at);
    assert(brace != std::string::npos);
    assert(code.substr(at, brace - at).find("noexcept") == std::string::npos);
  }
}

// The tree holds **one** recovery mechanism: `cs2::diag::call_guarded`.
//
// Only `payload/diag/src/crash_handler.cpp` may register a vectored exception
// handler or jump out of one. The probe (T39) wrote its own vectored handler
// with its own `setjmp`/`longjmp` because it could not use the shared one,
// and one of the two did not work -- that is how the defect survived a whole
// wave of work. A second implementation cannot come back unnoticed.
void test_the_tree_has_one_guard(const std::filesystem::path& base) {
  assert(std::filesystem::is_directory(base));
  const std::filesystem::path owner = base / "diag" / "src" / "crash_handler.cpp";

  for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::filesystem::path& path = entry.path();
    const std::string extension = path.extension().string();
    if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".cc") {
      continue;
    }
    // The tests name the mechanism they test; they are not a second one.
    if (path.string().find("tests") != std::string::npos) {
      continue;
    }
    const std::string code = strip_comments(read_file(path));
    if (code.empty()) {
      continue;
    }
    // The tokens a recovery mechanism is made of: a vectored handler, a
    // `setjmp` (the include of `<csetjmp>` counts) and a `longjmp`.
    const bool registers = code.find("AddVectoredExceptionHandler") != std::string::npos;
    const bool jumps = code.find("longjmp") != std::string::npos;
    const bool saves = code.find("setjmp") != std::string::npos;
    if (!registers && !jumps && !saves) {
      continue;
    }
    assert(path == owner);
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  assert(argc >= 2);
  const std::filesystem::path payload_dir(argv[1]);
  test_the_handler_is_not_noexcept(payload_dir / "diag" / "src" / "crash_handler.cpp");
  test_the_tree_has_one_guard(payload_dir);
#if defined(_WIN32)
  test_the_guard_recovers_from_a_deliberate_fault();
  std::printf("diag-guard: ok (%s, %d deliberate faults recovered)\n", compiler_name(),
              k_fault_rounds);
#else
  std::puts("diag-guard: ok (the source check only: the guard is Windows-only)");
#endif
  return 0;
}
