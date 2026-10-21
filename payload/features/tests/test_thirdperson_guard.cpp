// The re-entrancy guard of the camera hook (`cs2/features/recursion_guard.hpp`).
//
// The guard is the whole point of this feature's fix: the detour runs on the
// **game's** camera thread, in a **manually mapped** image, where a
// `thread_local` resolves against a TLS block that was never installed — the
// read faults inside the payload and the game dies (see
// `docs/re/thirdperson.md`). So the guard must:
//
//   * break a **same-thread** recursion (the trampoline landing back in the
//     detour) — without it the chain recurses until the stack overflows;
//   * **not** refuse another thread's call: the camera update can run on more
//     than one thread, and a shared counter would silently drop the other
//     thread's camera update;
//   * hold **no** `thread_local` anywhere on its path.
//
// The last one is checked against the repository's own sources: the test
// reads the guard's header, the detour's source and the files the detour's
// path includes, and fails when the word comes back. A comment would not
// have caught the crash.

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cs2/features/recursion_guard.hpp"

namespace {

using cs2::features::guard::OwnerId;
using cs2::features::guard::RecursionGuard;
using cs2::features::guard::TOTAL_SLOT_COUNT;

// The owner tokens the test uses. The real ones are the OS thread ids; the
// test only needs them to be **distinct per thread**, so it derives them from
// the standard library's thread id.
[[nodiscard]] auto owner_token() noexcept -> OwnerId {
  const std::hash<std::thread::id> hash;
  return static_cast<OwnerId>(hash(std::this_thread::get_id()) & 0x7fffffffU) + 1U;
}

// The guard lets the first call through.
void test_the_first_call_is_not_refused() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  assert(guard.try_enter(owner));
  assert(guard.depth(owner) == 1);
  guard.leave(owner);
  assert(guard.depth(owner) == 0);
}

// The recursion: the same thread comes back (the trampoline landed in the
// detour again) and is refused.
void test_the_same_thread_is_refused_the_second_time() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  assert(guard.try_enter(owner));
  assert(!guard.try_enter(owner));
  assert(!guard.try_enter(owner));
  // The refusal does not change the depth: the outer call still owns it.
  assert(guard.depth(owner) == 1);
  guard.leave(owner);
  assert(guard.depth(owner) == 0);
}

// After leaving, the same thread is let through again: the guard is not a
// one-shot latch.
void test_the_guard_is_reusable_after_leaving() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  for (int round = 0; round < 4; ++round) {
    assert(guard.try_enter(owner));
    assert(!guard.try_enter(owner));
    guard.leave(owner);
  }
  assert(guard.depth(owner) == 0);
}

// Another thread's call is **not** refused: the camera update can run on more
// than one thread, and a shared counter would drop the other thread's camera
// update.
void test_another_thread_is_not_refused() {
  RecursionGuard guard;
  const OwnerId owner = owner_token();
  assert(guard.try_enter(owner));

  bool other_entered = false;
  bool other_recursed = false;
  bool other_entered_as_owner = false;
  std::thread other([&guard, owner, &other_entered, &other_recursed, &other_entered_as_owner]() {
    const OwnerId token = owner_token();
    other_entered = guard.try_enter(token);
    // The other thread's own recursion is refused — the guard is not a
    // per-thread bypass.
    other_recursed = !guard.try_enter(token);
    // The token belongs to the thread that owns it: another thread cannot
    // enter on its behalf.
    other_entered_as_owner = guard.try_enter(owner);
    guard.leave(token);
  });
  other.join();

  assert(other_entered);
  assert(other_recursed);
  assert(!other_entered_as_owner);
  // The first thread is still inside: the other thread did not take its slot.
  assert(guard.depth(owner) == 1);
  guard.leave(owner);
  assert(guard.depth(owner) == 0);
}

// The regression of the scan-order hole: the owner of a **higher** slot is
// refused too, even when a lower slot has been freed in the meantime.
//
// The old guard claimed the first free slot it found and only asked "am I
// already inside?" about the slots it walked past, so an owner of slot 1 was
// let back in as soon as slot 0 was free: the recursion it exists to break
// ran until the stack overflowed (0xc00000fd).
void test_the_recursion_is_refused_when_a_lower_slot_is_free() {
  RecursionGuard guard;
  const OwnerId lower = 1;
  const OwnerId higher = 2;
  assert(guard.try_enter(lower));  // slot 0
  assert(guard.try_enter(higher));  // slot 1
  guard.leave(lower);               // slot 0 is free again
  assert(guard.depth(lower) == 0);

  // The recursion: the higher owner comes back while the lower slot is free.
  assert(!guard.try_enter(higher));
  assert(guard.depth(higher) == 1);

  // The higher owner still holds exactly one slot, so one leave is enough.
  guard.leave(higher);
  assert(guard.depth(higher) == 0);
}

// The regression of the double claim: a thread that owned two slots would
// release only one of them, and the leftover would refuse that thread's
// **legitimate** calls forever — in the detour, a refused entry drops the
// frame's camera update.
void test_a_thread_owns_at_most_one_slot() {
  RecursionGuard guard;
  const OwnerId first = 1;
  const OwnerId second = 2;
  const OwnerId third = 3;

  assert(guard.try_enter(first));   // slot 0
  assert(guard.try_enter(second));  // slot 1
  guard.leave(first);               // slot 0 is free, the second keeps slot 1

  // The recursion is refused: the second owner does not claim a second slot.
  assert(!guard.try_enter(second));
  assert(guard.depth(second) == 1);

  // One leave releases the one slot the second owner holds.
  guard.leave(second);
  assert(guard.depth(second) == 0);

  // The slot the second owner leaked (if it had claimed two) would refuse it
  // here. It does not: the second owner is let in again.
  assert(guard.try_enter(third));
  assert(guard.try_enter(second));
  guard.leave(second);
  guard.leave(third);
  assert(guard.depth(second) == 0);
  assert(guard.depth(third) == 0);
}

// The pool: every slot can be held by a different owner at once, and every
// one of them has its recursion broken.
//
// The last slot is the reserve: it is only reached when the others are
// taken, and it keeps that owner's recursion broken too.
void test_every_slot_is_tracked_and_breaks_its_own_recursion() {
  RecursionGuard guard;
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    assert(guard.try_enter(owner));
  }
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    assert(guard.depth(owner) == 1);
    // The recursion of every owner is broken, including the reserve slot's.
    assert(!guard.try_enter(owner));
  }
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    guard.leave(owner);
  }
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    assert(guard.depth(owner) == 0);
  }
}

// The fail-open window: one more owner than the pool has slots is let in
// (refusing it would drop a camera update on a thread that is not
// recursing), and it is not tracked — the documented cost of the policy.
//
// The camera update runs on one or two threads, so the pool is never close
// to exhaustion in practice; the test pins the behaviour down so that it
// cannot change silently.
void test_the_guard_fails_open_when_the_pool_is_exhausted() {
  RecursionGuard guard;
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    assert(guard.try_enter(owner));
  }
  const OwnerId overflow = TOTAL_SLOT_COUNT + 1;
  assert(guard.try_enter(overflow));  // allowed: not a recursion
  assert(guard.depth(overflow) == 0);  // not tracked: the pool is full
  guard.leave(overflow);               // a no-op: it owns nothing
  for (OwnerId owner = 1; owner <= TOTAL_SLOT_COUNT; ++owner) {
    guard.leave(owner);
  }
  // The pool is free again: the overflow owner is tracked now.
  assert(guard.try_enter(overflow));
  assert(guard.depth(overflow) == 1);
  guard.leave(overflow);
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
        if (guard.try_enter(token)) {
          entered.fetch_add(1);
          if (guard.try_enter(token)) {
            // A second entry must never succeed: that is the recursion the
            // guard exists to break.
            refused.fetch_add(1);
          }
          guard.leave(token);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  assert(entered.load() == 8 * 200);
  assert(refused.load() == 0);
}

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
// The rule is about the **code**: the files document the rule in prose ("no
// `thread_local` on this path"), and a comment that states the rule must not
// trip the check that enforces it.
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
//
// The check reads the **repository's own sources** — the pure header, the
// feature's header, the detour's source and the files the detour's path
// includes — so the word cannot come back unnoticed. (The chams' draw
// detour has the same defect; it is another task's file and is reported in
// `docs/re/thirdperson.md`.)
void test_the_guard_path_has_no_thread_local(const std::string& root) {
  const std::filesystem::path base = std::filesystem::path(root) / "payload";
  const std::vector<std::filesystem::path> files = {
      base / "features" / "include" / "cs2" / "features" / "recursion_guard.hpp",
      base / "features" / "include" / "cs2" / "features" / "thirdperson.hpp",
      base / "features" / "src" / "thirdperson.cpp",
      // The detour's path: the feature's write goes through the game module.
      base / "game" / "src" / "camera_live.cpp",
      base / "game" / "src" / "camera.cpp",
      base / "game" / "src" / "camera_detail.hpp",
      base / "game" / "src" / "module.cpp",
      base / "game" / "src" / "memory.cpp",
  };
  for (const auto& file : files) {
    const std::string source = read_file(file);
    assert(!source.empty());
    assert(strip_comments(source).find("thread_local") == std::string::npos);
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  assert(argc >= 2);
  test_the_first_call_is_not_refused();
  test_the_same_thread_is_refused_the_second_time();
  test_the_guard_is_reusable_after_leaving();
  test_another_thread_is_not_refused();
  test_the_recursion_is_refused_when_a_lower_slot_is_free();
  test_a_thread_owns_at_most_one_slot();
  test_every_slot_is_tracked_and_breaks_its_own_recursion();
  test_the_guard_fails_open_when_the_pool_is_exhausted();
  test_many_threads_at_once();
  test_the_guard_path_has_no_thread_local(argv[1]);
  std::puts("features-thirdperson-guard: ok");
  return 0;
}
