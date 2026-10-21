// The prediction's copy of the engine's movement services.
//
// The payload is **manually mapped**, so the game's threads have no TLS block
// for the image: a `thread_local` snapshot resolved against a block that was
// never installed, the fault-safe reader answered "false", and the prediction
// was **silently off** — no error anywhere (`docs/re/prediction.md` §"the TLS
// defect").
//
// The snapshot is therefore the **caller's buffer**: a plain local of the
// prediction's step. The test drives the copy against a fake engine object,
// and its last check reads the repository's own sources: the prediction's
// path must not depend on per-thread storage that a manually mapped image
// does not have.
//
// The checks pin three different properties, and each one of them can fail:
//
//   * the copy reads the engine's bytes into the caller's buffer and the
//     restore puts them back (a copy that does not round trip is not a copy);
//   * a failed read leaves the engine's object alone (the restore must not
//     write what was never read);
//   * two steps never share a buffer (a shared buffer would need a lock, and
//     the copy's lifetime is one step), and the module's sources carry no
//     `thread_local` at all (the property that was broken).

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cs2/control/prediction_copy.hpp"

namespace {

using cs2::control::prediction::MOVEMENT_SERVICES_SIZE;
using cs2::control::prediction::MovementServicesSnapshot;

// The fake engine object: the bytes the "engine" holds, and the address the
// reader and the writer resolve it by. A file static: a test fixture, not the
// payload's state (the payload has no global mutable state — ADR-0010).
std::vector<std::byte> g_engine_object(MOVEMENT_SERVICES_SIZE, std::byte{0x11});

// The writes the writer performed, so the test can prove that a failed read
// is never followed by a restore.
int g_writes = 0;

auto engine_address() -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(g_engine_object.data());
}

// The fault-safe reader, as the payload's `cs2::game::read_bytes` behaves: it
// copies out of the engine's object and answers false for an address it does
// not know.
auto fake_read(std::uintptr_t address, void* out, std::size_t count) noexcept -> bool {
  if (address != engine_address() || out == nullptr || count == 0) {
    return false;
  }
  std::memcpy(out, g_engine_object.data(), count);
  return true;
}

// The restore's writer: a plain copy back into the engine's object.
auto fake_write(std::uintptr_t address, const void* data, std::size_t count) noexcept -> void {
  if (address != engine_address() || data == nullptr || count == 0) {
    return;
  }
  std::memcpy(g_engine_object.data(), data, count);
  ++g_writes;
}

// The snapshot is the caller's buffer: the reader is handed its address.
void* g_read_destination = nullptr;

auto recording_read(std::uintptr_t address, void* out, std::size_t count) noexcept -> bool {
  g_read_destination = out;
  return fake_read(address, out, count);
}

// --- the copy ---------------------------------------------------------------

// The prediction's step, as the payload runs it: take the copy, let the
// simulation change the engine's object, put the copy back. A failed copy
// returns immediately — nothing was read, so nothing may be written.
auto run_step(MovementServicesSnapshot& snapshot, std::uintptr_t address) -> bool {
  if (!cs2::control::prediction::snapshot(address, snapshot, &fake_read)) {
    return false;
  }
  // The simulation: the engine's `RunCommand` mutates the movement object.
  std::memset(g_engine_object.data(), 0x5a, MOVEMENT_SERVICES_SIZE);
  cs2::control::prediction::restore(address, snapshot, &fake_write);
  return true;
}

void test_the_snapshot_holds_the_engine_bytes() {
  for (std::size_t i = 0; i < MOVEMENT_SERVICES_SIZE; ++i) {
    g_engine_object[i] = static_cast<std::byte>(i & 0xffU);
  }
  MovementServicesSnapshot snapshot{};
  assert(cs2::control::prediction::snapshot(engine_address(), snapshot, &fake_read));
  assert(std::memcmp(snapshot.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
}

void test_the_restore_puts_the_bytes_back() {
  MovementServicesSnapshot snapshot{};
  assert(cs2::control::prediction::snapshot(engine_address(), snapshot, &fake_read));

  // The simulation mutates the engine's object (the engine's `RunCommand`).
  std::memset(g_engine_object.data(), 0x5a, MOVEMENT_SERVICES_SIZE);
  assert(std::memcmp(snapshot.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) != 0);

  cs2::control::prediction::restore(engine_address(), snapshot, &fake_write);
  assert(std::memcmp(snapshot.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
}

void test_a_failed_read_leaves_the_object_alone() {
  const auto before = g_engine_object;
  const int writes_before = g_writes;
  MovementServicesSnapshot snapshot{};
  // An address the reader does not know: the copy cannot be made, so the step
  // stops before the simulation and writes nothing back.
  assert(!run_step(snapshot, engine_address() + 1));
  assert(g_writes == writes_before);
  assert(std::memcmp(before.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
}

void test_the_step_restores_the_engine_object() {
  for (std::size_t i = 0; i < MOVEMENT_SERVICES_SIZE; ++i) {
    g_engine_object[i] = static_cast<std::byte>(i & 0xffU);
  }
  const auto before = g_engine_object;
  MovementServicesSnapshot snapshot{};
  assert(run_step(snapshot, engine_address()));
  assert(std::memcmp(before.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
}

void test_the_copy_is_the_callers_buffer() {
  MovementServicesSnapshot snapshot{};
  g_read_destination = nullptr;
  assert(cs2::control::prediction::snapshot(engine_address(), snapshot, &recording_read));
  // The helper wrote into the buffer it was handed: it keeps no copy of its
  // own, and in particular no per-thread one.
  assert(g_read_destination == static_cast<void*>(snapshot.data()));
}

void test_two_steps_never_share_a_buffer() {
  MovementServicesSnapshot first{};
  MovementServicesSnapshot second{};
  std::uintptr_t first_address = 0;
  std::uintptr_t second_address = 0;

  std::thread left([&first, &first_address]() {
    assert(cs2::control::prediction::snapshot(engine_address(), first, &fake_read));
    first_address = reinterpret_cast<std::uintptr_t>(first.data());
  });
  std::thread right([&second, &second_address]() {
    assert(cs2::control::prediction::snapshot(engine_address(), second, &fake_read));
    second_address = reinterpret_cast<std::uintptr_t>(second.data());
  });
  left.join();
  right.join();

  // Two steps, two buffers: a shared buffer would have to be locked, and the
  // copy's lifetime is one step.
  assert(first_address != 0 && second_address != 0);
  assert(first_address != second_address);
  assert(std::memcmp(first.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
  assert(std::memcmp(second.data(), g_engine_object.data(), MOVEMENT_SERVICES_SIZE) == 0);
}

// --- the source check over the repository -----------------------------------

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

// The prediction's path must not contain a `thread_local`: the payload is
// manually mapped, and the game's threads have no TLS block for the image.
//
// The check reads the **repository's own sources** — the control module's
// headers and sources — so the word cannot come back unnoticed. (The chams'
// draw detour and the third-person camera's detour carry the same defect;
// they are `payload/features/**` and another task's files.)
void test_the_module_has_no_thread_local(const std::string& root) {
  const std::filesystem::path base = std::filesystem::path(root) / "payload" / "control";
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string extension = entry.path().extension().string();
    if (extension != ".cpp" && extension != ".hpp" && extension != ".h") {
      continue;
    }
    // The tests are host code: they are not mapped into the game, so the
    // payload's rule does not bind them.
    if (entry.path().string().find("tests") != std::string::npos) {
      continue;
    }
    files.push_back(entry.path());
  }
  assert(!files.empty());
  for (const auto& file : files) {
    const std::string source = read_file(file);
    assert(!source.empty());
    assert(strip_comments(source).find("thread_local") == std::string::npos);
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  assert(argc >= 2);
  test_the_snapshot_holds_the_engine_bytes();
  test_the_restore_puts_the_bytes_back();
  test_the_step_restores_the_engine_object();
  test_a_failed_read_leaves_the_object_alone();
  test_the_copy_is_the_callers_buffer();
  test_two_steps_never_share_a_buffer();
  test_the_module_has_no_thread_local(argv[1]);
  std::puts("control-prediction: ok");
  return 0;
}
