// The chams' draw hook.
//
// The **guard** is the whole point of this fix: the scene system renders its
// draw list in **jobs on the tier0 thread pool** (`scenesystem.dll` imports
// `g_pThreadPool` and carries `CSceneSystem::Thread_RenderSceneDrawList`,
// `Thread_RenderViewLayer`, `Thread_RenderGpuCullingLayer`, the
// `CParallelLambdaJob` / `CUtlMultiJobProcessor` types and the
// `sc_disableThreading` convar, so the threading is on by default). Two
// threads can be inside the draw detour at the same instant.
//
// The guard the detour used was a **process-wide** depth counter, and its
// early return **dropped** the draw instead of forwarding it: the second
// job's primitive was read as a recursion and never drawn — a hole in the
// model for that frame, silently. The repository's own rule for this class
// of guard (`docs/orchestration/spec-t27-thirdperson-crash.md` §4.1) says so:
//
//   "The guard must **not** refuse another thread's call ... a shared counter
//    that makes one thread's call return early would silently drop that
//    thread's camera update. A **per-owner** design (the thread id that
//    owns the depth, plus an atomic depth) satisfies both."
//
// So the guard is the shared, **per-owner** one
// (`cs2/features/recursion_guard.hpp`), and this test drives it directly:
// the header is pure (no Windows headers, no engine access), so the whole
// decision can be checked on the host.
//
// The test also reads the repository's own sources: the guard's path must
// hold no `thread_local` (the payload is manually mapped and the game's
// threads have no TLS block for it), the tree must hold exactly **one**
// implementation of the guard, the chams' detour must hold the shared one,
// and the status line must not allocate on the render thread — a comment
// would not have caught the crash, and a second copy of the guard would be a
// second implementation of the same rules.
//
// The **wiring** is pinned too, because the per-owner guard exposed three
// gaps that no check covered:
//
//   * **H3** — the switch-off has to **clear** the refusal latch. The rule
//     is pure (`hook_target::refusal_after_switch`), so what was missing was
//     the call site: deleting the clear of `install_failed_` left the suite
//     green.
//   * **H6** — the guard's scope has to be held **across** the trampoline
//     call. The old check only asked whether the source mentioned
//     `guard::Scope` (presence, not ordering), so moving the scope after
//     the call left the suite green.
//   * **H7** — the chams' guard **fails closed** and the camera's does not.
//     The policy is per instance, so the camera's behaviour does not change.
//
// The checks are **explicit**: the test counts its failures instead of using
// `assert`, so it still bites in a release build (Meson's `b_ndebug` default
// is `if-release`, so `NDEBUG` is defined in the configuration
// `scripts/release.sh` builds and every `assert` would be compiled out — a
// test that only bites in the debug build is not a test).

#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cs2/features/guard_scope.hpp"
#include "cs2/features/hook_target.hpp"
#include "cs2/features/recursion_guard.hpp"

namespace {

using cs2::features::guard::OwnerId;
using cs2::features::guard::RecursionGuard;
using cs2::features::guard::Scope;

// The number of failed checks. The test's exit code is the verdict, so a
// release build (where `assert` is compiled out) still reports the failure.
int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// The owner tokens the test uses. The real ones are the OS thread ids; the
// test only needs them to be **distinct per thread**, so it derives them
// from the standard library's thread id.
[[nodiscard]] auto owner_token() noexcept -> OwnerId {
  const std::hash<std::thread::id> hash;
  return static_cast<OwnerId>(hash(std::this_thread::get_id()) & 0x7fffffffU) + 1U;
}

// --- The control: the design the detour used before the fix -----------------

// The **process-wide** depth counter the chams' detour used, reproduced here
// as the control.
//
// The test's central claim is "another thread's call is not refused". That
// claim is only worth anything if the old design really did refuse it, so
// the old design is here, in the test, and it is shown to drop the second
// thread's draw.
std::atomic<int> g_process_wide_depth{0};

// The old detour's guard: a process-wide counter, and an early return that
// does **not** forward the draw.
[[nodiscard]] auto the_old_design_allows_the_draw() -> bool {
  if (g_process_wide_depth.load(std::memory_order_relaxed) > 0) {
    return false;
  }
  g_process_wide_depth.store(1, std::memory_order_relaxed);
  return true;
}

// --- The guard's own rules ---------------------------------------------------

// The guard lets the first call through.
void test_the_first_call_is_not_refused() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  Scope scope(guard, owner);
  expect(scope.entered(), "the first call is not refused");
  expect(guard.depth(owner) == 1, "the first call owns the guard");
}

// The recursion: the same thread comes back (the trampoline landed in the
// detour again) and is refused.
void test_the_same_thread_is_refused_the_second_time() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  Scope scope(guard, owner);
  expect(scope.entered(), "the first call enters");
  Scope recursion(guard, owner);
  expect(!recursion.entered(), "the recursion is refused");
  // The refusal does not change the depth: the outer call still owns it.
  expect(guard.depth(owner) == 1, "the refusal does not change the depth");
}

// After leaving, the same thread is let through again: the guard is not a
// one-shot latch.
void test_the_guard_is_reusable_after_leaving() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  for (int round = 0; round < 4; ++round) {
    Scope scope(guard, owner);
    expect(scope.entered(), "the guard is reusable after leaving");
  }
  expect(guard.depth(owner) == 0, "the guard is released");
}

// The scope releases the guard on **every** path, including an early return:
// a guard that is not released is a permanent refusal for that thread.
[[nodiscard]] auto the_call_that_returns_early(RecursionGuard& guard, OwnerId owner) -> bool {
  Scope scope(guard, owner);
  if (!scope.entered()) {
    return false;
  }
  // The early return: the scope has to release the guard all the same.
  return false;
}

void test_the_scope_releases_the_guard_on_every_path() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  (void)the_call_that_returns_early(guard, owner);
  expect(guard.depth(owner) == 0, "an early return still releases the guard");
  // The thread is not permanently refused: it can enter again.
  Scope scope(guard, owner);
  expect(scope.entered(), "the thread is not permanently refused");
}

// The owner is refused even when a **lower** slot has been freed while it was
// inside.
//
// The guard asks "am I already inside?" over **every** slot before it claims
// one. A single-pass guard that claims the first free slot would let the
// owner back in through that lower slot, and the recursion would run until
// the stack overflowed (0xc00000fd) and took the renderer with it.
//
// The case is driven here: another owner takes the first slot, this owner
// takes the next one, the other owner leaves (the first slot is free again)
// and this owner is still inside.
void test_the_owner_is_refused_through_a_freed_slot() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  const OwnerId other = owner_token() ^ 0x5a5aU;
  expect(other != owner, "the two owners are different");
  expect(guard.try_enter(other), "the other owner enters");
  {
    Scope mine(guard, owner);
    expect(mine.entered(), "the owner enters while the other owner holds a slot");
    // The other owner leaves: its slot is free, and this owner is still
    // inside.
    guard.leave(other);
    Scope recursion(guard, owner);
    expect(!recursion.entered(), "the owner is refused through the freed slot");
    expect(guard.depth(owner) == 1, "the refusal does not change the depth");
  }
  expect(guard.depth(owner) == 0, "the owner is released");
}

// **The major finding.** Another thread's call is **not** refused: the scene
// system draws the scene's primitives in jobs on the tier0 thread pool, and
// a shared counter would drop the other job's draw.
void test_another_thread_is_not_refused() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  Scope scope(guard, owner);
  expect(scope.entered(), "the first thread enters");

  bool other_entered = false;
  bool other_recursed = false;
  bool other_entered_as_owner = false;
  std::thread other([&guard, owner, &other_entered, &other_recursed, &other_entered_as_owner]() {
    const OwnerId token = owner_token();
    Scope other_scope(guard, token);
    other_entered = other_scope.entered();
    // The other thread's own recursion is refused — the guard is not a
    // per-thread bypass.
    Scope recursion(guard, token);
    other_recursed = !recursion.entered();
    // The token belongs to the thread that owns it: another thread cannot
    // enter on its behalf.
    Scope as_owner(guard, owner);
    other_entered_as_owner = as_owner.entered();
  });
  other.join();

  expect(other_entered, "another thread's call is not refused");
  expect(other_recursed, "the other thread's own recursion is refused");
  expect(!other_entered_as_owner, "another thread cannot enter on the owner's behalf");
  // The first thread is still inside: the other thread did not take its slot.
  expect(guard.depth(owner) == 1, "the other thread did not take the owner's slot");
}

// The control: the **old** design really did drop the second thread's draw.
// Without this, the claim above would be vacuous.
void test_the_old_process_wide_design_would_have_dropped_the_draw() {
  g_process_wide_depth.store(0, std::memory_order_relaxed);
  expect(the_old_design_allows_the_draw(), "the old design lets the first draw through");

  bool other_allowed = true;
  std::thread other([&other_allowed]() { other_allowed = the_old_design_allows_the_draw(); });
  other.join();

  expect(!other_allowed, "the old design dropped the other thread's draw");
  g_process_wide_depth.store(0, std::memory_order_relaxed);
}

// Many threads at once: every one of them is let in, and every one of them
// breaks its own recursion. The guard is lock-free, so this is a smoke test
// for the atomic bookkeeping, not a stress test.
void test_many_threads_at_once() {
  RecursionGuard guard;
  std::atomic<int> entered{0};
  std::atomic<int> refused{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&guard, &entered, &refused]() {
      const OwnerId token = owner_token();
      for (int round = 0; round < 200; ++round) {
        Scope scope(guard, token);
        if (!scope.entered()) {
          continue;
        }
        entered.fetch_add(1);
        Scope recursion(guard, token);
        if (recursion.entered()) {
          // A second entry must never succeed: that is the recursion the
          // guard exists to break.
          refused.fetch_add(1);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  expect(entered.load() == 8 * 200, "every thread is let in");
  expect(refused.load() == 0, "no thread's own recursion is let through");
}

// --- The checks over the repository's own sources ----------------------------

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
// The rules are about the **code**: the files document the rules in prose
// ("no `thread_local` on this path"), and a comment that states the rule
// must not trip the check that enforces it.
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

// The guard's code path must not contain a `thread_local`: the payload is
// manually mapped, and the game's threads have no TLS block for it.
void test_the_guard_path_has_no_thread_local(const std::filesystem::path& base) {
  const std::vector<std::filesystem::path> files = {
      base / "include" / "cs2" / "features" / "recursion_guard.hpp",
      base / "include" / "cs2" / "features" / "guard_scope.hpp",
      base / "include" / "cs2" / "features" / "chams.hpp",
      base / "src" / "chams.cpp",
  };
  for (const auto& file : files) {
    const std::string source = read_file(file);
    expect(!source.empty(), "the guard's source is there");
    expect(strip_comments(source).find("thread_local") == std::string::npos,
           "the guard's path has no thread_local");
  }
}

// Does the source mention `word` as a whole identifier (not as a part of a
// longer one)? The material path `materials/.../nodepth.vmt` carries the
// letters "depth" inside a longer name, and it is not a counter.
[[nodiscard]] auto contains_word(const std::string& source, const std::string& word) -> bool {
  std::size_t at = source.find(word);
  while (at != std::string::npos) {
    const bool left = at == 0 || !(std::isalnum(static_cast<unsigned char>(source[at - 1])) != 0 ||
                                   source[at - 1] == '_');
    const std::size_t end = at + word.size();
    const bool right =
        end >= source.size() ||
        !(std::isalnum(static_cast<unsigned char>(source[end])) != 0 || source[end] == '_');
    if (left && right) {
      return true;
    }
    at = source.find(word, at + 1);
  }
  return false;
}

// Every identifier in the source that mentions a depth.
//
// The game's own material is called `k_nodepth_material_path` (the
// no-depth-test variant), and that name is not a counter.
void test_the_chams_hold_no_depth_counter(const std::string& source) {
  std::string token;
  std::vector<std::string> counters;
  for (const char c : source) {
    const bool word = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    if (word) {
      token.push_back(c);
      continue;
    }
    if (token.find("depth") != std::string::npos && token.find("nodepth") == std::string::npos) {
      bool known = false;
      for (const std::string& seen : counters) {
        known = known || seen == token;
      }
      if (!known) {
        counters.push_back(token);
      }
    }
    token.clear();
  }
  for (const std::string& counter : counters) {
    std::printf("FAIL: the chams hold a depth counter of their own (%s)\n", counter.c_str());
  }
  expect(counters.empty(), "the chams hold no depth counter of their own");
}

// The chams' detour must hold the **shared** guard, and it must not keep a
// depth counter of its own: the depth lives in the shared guard, and a
// second counter is the defect this fix removed.
void test_the_chams_hold_the_shared_guard(const std::filesystem::path& base) {
  const std::string source = strip_comments(read_file(base / "src" / "chams.cpp"));
  expect(!source.empty(), "the chams' source is there");
  expect(source.find("guard::Scope") != std::string::npos,
         "the draw detour holds the guard in a scope");
  expect(source.find("draw_guard()") != std::string::npos,
         "the draw detour holds the shared guard");
  expect(source.find("current_owner()") != std::string::npos,
         "the draw detour's guard is per owner (the thread id)");
  test_the_chams_hold_no_depth_counter(source);

  const std::string scope =
      strip_comments(read_file(base / "include" / "cs2" / "features" / "guard_scope.hpp"));
  expect(scope.find("try_enter") != std::string::npos, "the scope enters the guard");
  expect(scope.find("leave") != std::string::npos, "the scope releases the guard");
}

// The tree must hold **exactly one** implementation of the guard: a second
// copy is a second set of rules, and the two would drift.
void test_the_tree_holds_one_guard(const std::filesystem::path& features) {
  int implementations = 0;
  std::string holder;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(features)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::filesystem::path& path = entry.path();
    if (path.extension() != ".hpp" && path.extension() != ".cpp") {
      continue;
    }
    if (strip_comments(read_file(path)).find("class RecursionGuard") == std::string::npos) {
      continue;
    }
    ++implementations;
    holder = path.filename().string();
  }
  expect(implementations == 1, "the tree holds exactly one implementation of the guard");
  expect(holder == "recursion_guard.hpp", "the one implementation is the shared header");
}

// The status line is built on the game's render thread, once per frame: it
// must be written **into the caller's buffer** and must not allocate.
//
// The old signature returned a `std::string`, so the payload copied it into
// the caller's buffer — a heap allocation on the render thread, every frame.
// The sibling providers (`Service::rage_status`, `movement_status`) write
// straight into the caller's fixed buffer, and so does this one now.
void test_the_status_line_does_not_allocate(const std::filesystem::path& base) {
  const std::string header =
      strip_comments(read_file(base / "include" / "cs2" / "features" / "chams.hpp"));
  expect(header.find("status_line(char* out, std::size_t size)") != std::string::npos,
         "the status line writes into the caller's buffer");
  expect(header.find("auto status_line() const -> std::string") == std::string::npos,
         "the status line does not return a string");

  const std::string source = strip_comments(read_file(base / "src" / "chams.cpp"));
  const std::size_t at = source.find("Chams::status_line");
  expect(at != std::string::npos, "the status line is there");
  if (at == std::string::npos) {
    return;
  }
  const std::string body = source.substr(at, source.size() - at);
  expect(body.find("snprintf") != std::string::npos, "the status line is written with snprintf");
  expect(!contains_word(body, "std::string"), "the status line allocates nothing");
  // The counters the line shows are read through the accessors, so none of
  // them is dead code.
  expect(body.find("seen()") != std::string::npos, "the status line uses the seen counter");
  expect(body.find("drawn()") != std::string::npos, "the status line uses the drawn counter");
  expect(body.find("skipped()") != std::string::npos, "the status line uses the skipped counter");
  expect(body.find("hook_status()") != std::string::npos, "the status line uses the hook state");
}

// The body of the function that starts at `marker`, braces matched.
//
// The rules below are about **one function's** body (the switch-off clears
// the latch in `configure`, the guard is held across the trampoline call in
// the detour), so the check has to look at that function and not at the
// whole file: a rule that is satisfied somewhere else in the file is not
// satisfied at all.
auto function_body(const std::string& source, const std::string& marker) -> std::string {
  const std::size_t at = source.find(marker);
  if (at == std::string::npos) {
    return {};
  }
  const std::size_t open = source.find('{', at);
  if (open == std::string::npos) {
    return {};
  }
  int depth = 0;
  for (std::size_t index = open; index < source.size(); ++index) {
    if (source[index] == '{') {
      ++depth;
    } else if (source[index] == '}') {
      --depth;
      if (depth == 0) {
        return source.substr(open, index - open + 1);
      }
    }
  }
  return {};
}

// --- H3: the switch-off clears the refusal latch -----------------------------
//
// The refusal latch (`install_failed_`) stops a refused install from being
// retried and re-reported on every frame. It has to be **cleared** when the
// operator switches the feature off: a latch that survived the switch-off
// would make the next switch-on never retry the install, so the refusal
// would never be reported again and the HUD would say "installing" forever
// — a lie, because no install is being attempted.
//
// The rule is pure (`hook_target::refusal_after_switch`), so what is missing
// is the **wiring**: the `configure()` call site. Deleting the clear of
// `install_failed_` used to leave the whole suite green.
void test_the_switch_off_clears_the_refusal_latch(const std::filesystem::path& base) {
  const std::string source = strip_comments(read_file(base / "src" / "chams.cpp"));
  const std::string body = function_body(source, "void Chams::configure(");
  expect(!body.empty(), "the chams' configure() is there");

  // The latch is stored ...
  const std::size_t store = body.find("install_failed_.store(");
  expect(store != std::string::npos, "configure() stores the refusal latch");
  if (store == std::string::npos) {
    return;
  }
  // ... and the value it stores is the **switch's** answer, not the old
  // latch: `refusal_after_switch(enabled, refused)` is false when the
  // feature is off, and that is what clears the latch.
  const std::size_t rule = body.find("refusal_after_switch", store);
  expect(rule != std::string::npos && rule - store < 200,
         "the latch that is stored is the switch's answer (refusal_after_switch)");

  // The rule itself: the switch-off clears it.
  expect(!cs2::features::hook_target::refusal_after_switch(false, true),
         "the rule clears the latch when the feature is switched off");
  expect(cs2::features::hook_target::refusal_after_switch(true, true),
         "the rule keeps the latch while the feature is on");
}

// --- H6: the guard is held across the trampoline call -------------------------
//
// The guard exists to break the recursion that happens **inside** the call to
// the original: the trampoline lands back in the detour, and the guard is
// what refuses that call. A guard that is released before the call protects
// nothing.
//
// The old check only asked whether the source *mentioned* `guard::Scope` —
// presence, not ordering — so moving the scope after the trampoline call
// left the suite green.
void test_the_guard_is_held_across_the_trampoline_call(const std::filesystem::path& base) {
  const std::string source = strip_comments(read_file(base / "src" / "chams.cpp"));
  const std::string body = function_body(source, "draw_primitive_detour");
  expect(!body.empty(), "the draw detour is there");

  // The guard's scope.
  const std::size_t scope = body.find("guard::Scope");
  expect(scope != std::string::npos, "the detour holds the guard in a scope");
  // The call that reaches the trampoline: `on_draw` is what calls the
  // original, so the scope has to be declared **before** it.
  const std::size_t call = body.find("on_draw(");
  expect(call != std::string::npos, "the detour calls the draw path");
  if (scope == std::string::npos || call == std::string::npos) {
    return;
  }
  expect(scope < call, "the guard is held across the trampoline call");
}

// --- H7: the chams' guard fails closed, the camera's does not ----------------
//
// The shared guard's fail-open rationale is the **camera's**: refusing a call
// that is not a recursion would drop a camera update. For the draw path the
// arithmetic is the other way round: a refusal drops **one primitive**,
// while failing open lets the recursion run until the stack overflows
// (0xc00000fd) and takes the renderer with it.
//
// The policy is **per instance**, so the camera's behaviour does not change:
// the camera's guard is built with the default (fail open) and the chams'
// with `fail_closed`.
void test_the_chams_fail_closed_and_the_camera_does_not(const std::filesystem::path& base) {
  const std::string header =
      strip_comments(read_file(base / "include" / "cs2" / "features" / "chams.hpp"));
  expect(header.find("RecursionGuard::Policy::fail_closed") != std::string::npos,
         "the chams' guard fails closed");

  // The shared header's default is the camera's policy.
  const std::string guard_header =
      strip_comments(read_file(base / "include" / "cs2" / "features" / "recursion_guard.hpp"));
  expect(guard_header.find("Policy policy = Policy::fail_open") != std::string::npos,
         "the guard's default policy is the camera's (fail open)");

  // The camera's guard is built with the default, so it keeps failing open.
  const std::filesystem::path camera = base / "include" / "cs2" / "features" / "thirdperson.hpp";
  const std::string camera_header = strip_comments(read_file(camera));
  expect(!camera_header.empty(), "the camera's header is there");
  expect(camera_header.find("camera_guard_{}") != std::string::npos,
         "the camera's guard is built with the default policy");
  expect(camera_header.find("fail_closed") == std::string::npos,
         "the camera's guard does not fail closed");
}

// --- The pool: the draw path's is sized for the render jobs ----------------
//
// The chams' guard holds its slot for the **whole draw** — the call into the
// original included, because that is the call the recursion happens inside —
// so the pool has to hold one slot per **concurrent render job**, not one per
// thread. The scene system's job count is **unmeasured** (the live run has
// still not happened), so the pool is sized with headroom: the draw path's
// pool is eight times the camera's.
//
// The pool is **per instance**, so the camera keeps its pool — and its cost —
// exactly as it was.
//
// The header is pure, so the pool's size is observable: a fail-closed guard
// lets exactly `slot_count` owners in and refuses the next one. That is what
// the old, fixed pool of nine did to the tenth render job: its draw was
// refused, and the primitive was **not drawn at all**, every frame.
void test_the_pool_holds_the_concurrent_jobs() {
  // The camera's pool: the default, unchanged.
  RecursionGuard camera;
  expect(camera.slot_count() == cs2::features::guard::TOTAL_SLOT_COUNT,
         "the default pool is the camera's");
  RecursionGuard camera_closed{RecursionGuard::Policy::fail_closed};
  expect(camera_closed.slot_count() == camera.slot_count(),
         "the policy does not change the pool");
  for (std::size_t owner = 1; owner <= camera_closed.slot_count(); ++owner) {
    expect(camera_closed.try_enter(static_cast<OwnerId>(owner)),
           "the camera's pool takes its owners");
  }
  expect(!camera_closed.try_enter(static_cast<OwnerId>(camera_closed.slot_count() + 1)),
         "the camera's pool refuses the owner that does not fit");

  // The draw path's pool: eight times the camera's, so a tenth concurrent
  // render job — the case the old pool refused — is let in.
  RecursionGuard draw{RecursionGuard::Policy::fail_closed,
                       cs2::features::guard::DRAW_PATH_SLOT_COUNT};
  expect(draw.slot_count() == cs2::features::guard::DRAW_PATH_SLOT_COUNT,
         "the draw path's pool is the one that was asked for");
  expect(draw.slot_count() > camera.slot_count(),
         "the draw path's pool is bigger than the camera's");
  for (std::size_t owner = 1; owner <= draw.slot_count(); ++owner) {
    expect(draw.try_enter(static_cast<OwnerId>(owner)), "the draw pool takes its owners");
  }
  expect(!draw.try_enter(static_cast<OwnerId>(draw.slot_count() + 1)),
         "the draw pool refuses the owner that does not fit");
}

// The chams' guard must be built with the draw path's pool, and the camera's
// must not be: the camera's behaviour — and its cost — has to stay exactly as
// it is.
void test_the_chams_use_the_draw_pool(const std::filesystem::path& base) {
  const std::string header =
      strip_comments(read_file(base / "include" / "cs2" / "features" / "chams.hpp"));
  const std::size_t at = header.find("draw_guard_{");
  expect(at != std::string::npos, "the chams hold a draw guard");
  if (at == std::string::npos) {
    return;
  }
  const std::size_t pool = header.find("DRAW_PATH_SLOT_COUNT", at);
  expect(pool != std::string::npos && pool - at < 200,
         "the chams' guard is built with the draw path's pool");

  const std::filesystem::path camera = base / "include" / "cs2" / "features" / "thirdperson.hpp";
  const std::string camera_header = strip_comments(read_file(camera));
  expect(!camera_header.empty(), "the camera's header is there");
  expect(camera_header.find("DRAW_PATH_SLOT_COUNT") == std::string::npos,
         "the camera's guard keeps the default pool");
}

// The refusal has to be **counted**: a refused draw is a dropped primitive,
// and a drop that is not counted is a silent hole in the model.
void test_the_refusal_is_counted(const std::filesystem::path& base) {
  const std::string source = strip_comments(read_file(base / "src" / "chams.cpp"));
  const std::string body = function_body(source, "draw_primitive_detour");
  expect(!body.empty(), "the draw detour is there");
  expect(body.find("count_refusal()") != std::string::npos,
         "the detour counts the draw it refused");

  // The counter is reported: the status line shows it. The format string is
  // a **string literal**, so this check reads the source as it is (the
  // stripped source has the literals removed).
  const std::string raw = read_file(base / "src" / "chams.cpp");
  const std::size_t at = raw.find("Chams::status_line");
  expect(at != std::string::npos, "the status line is there");
  if (at != std::string::npos) {
    const std::string line = raw.substr(at, raw.size() - at);
    expect(line.find("refused=") != std::string::npos, "the status line shows the refusals");
    const std::string stripped = strip_comments(line);
    expect(stripped.find("refused()") != std::string::npos, "the status line reads the counter");
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::printf("features-chams-guard: the repository root is missing\n");
    return 1;
  }
  const std::filesystem::path root = std::filesystem::path(argv[1]);
  const std::filesystem::path features = root / "payload" / "features";

  test_the_first_call_is_not_refused();
  test_the_same_thread_is_refused_the_second_time();
  test_the_guard_is_reusable_after_leaving();
  test_the_scope_releases_the_guard_on_every_path();
  test_the_owner_is_refused_through_a_freed_slot();
  test_another_thread_is_not_refused();
  test_the_old_process_wide_design_would_have_dropped_the_draw();
  test_many_threads_at_once();

  test_the_guard_path_has_no_thread_local(features);
  test_the_chams_hold_the_shared_guard(features);
  test_the_tree_holds_one_guard(features);
  test_the_status_line_does_not_allocate(features);
  test_the_switch_off_clears_the_refusal_latch(features);
  test_the_guard_is_held_across_the_trampoline_call(features);
  test_the_chams_fail_closed_and_the_camera_does_not(features);
  test_the_pool_holds_the_concurrent_jobs();
  test_the_chams_use_the_draw_pool(features);
  test_the_refusal_is_counted(features);

  if (failures != 0) {
    std::printf("features-chams-draw-hook: %d failed check(s)\n", failures);
    return 1;
  }
  std::puts("features-chams-draw-hook: ok");
  return 0;
}
