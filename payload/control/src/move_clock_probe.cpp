#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2/control/move_clock_probe.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <thread>
#include "MinHook.h"
#include "cs2/game/memory.hpp"
#include "cs2/game/pattern.hpp"
namespace cs2::control {
std::atomic<MoveClockProbe*> MoveClockProbe::owner_{nullptr};
thread_local bool MoveClockProbe::in_call_ = false;
std::optional<std::uintptr_t> resolve_command_tick(game::ModuleInfo module) noexcept {
  const auto at = game::unique_code_match(
      module, "4C8BDC49897320574881ECA00000004883B930010000008BF2488BF90F84????????");
  if (!at || !module.contains(at + 0x1b7)) return {};
  const std::array<std::pair<std::size_t, unsigned>, 3> stages{
      {{0xea, 0x260}, {0xfa, 0x268}, {0x10a, 0x288}}};
  for (const auto& [offset, slot] : stages) {
    std::array<unsigned char, 6> bytes{};
    if (!game::read_bytes(at + offset, bytes.data(), bytes.size()) || bytes[0] != 0xff ||
        bytes[1] != 0x90)
      return {};
    unsigned operand = 0;
    std::memcpy(&operand, bytes.data() + 2, 4);
    if (operand != slot) return {};
  }
  const std::array<unsigned char, 6> count_gate{0x85, 0xf6, 0x78, 0x41, 0xff, 0xc6};
  const std::array<unsigned char, 6> count_loop{0x48, 0x83, 0xee, 0x01, 0x75, 0xca};
  std::array<unsigned char, 6> check{};
  if (!game::read_bytes(at + 0xd1, check.data(), check.size()) || check != count_gate ||
      !game::read_bytes(at + 0x110, check.data(), check.size()) || check != count_loop)
    return {};
  // Profile label LEA at +0x35; resolve its RIP displacement and check the text.
  std::array<unsigned char, 3> op{};
  std::int32_t relative = 0;
  if (!game::read_bytes(at + 0x35, op.data(), op.size()) ||
      op != std::array<unsigned char, 3>{0x48, 0x8d, 0x0d} || !game::read(at + 0x38, relative))
    return {};
  const auto label = at + 0x3c + relative;
  constexpr char expected[] = "ClientCommandTick";
  std::array<char, sizeof(expected)> text{};
  if (!module.contains(label) || !module.contains(label + sizeof(expected) - 1) ||
      !game::read_bytes(label, text.data(), text.size()) ||
      std::memcmp(text.data(), expected, sizeof(expected)) != 0)
    return {};
  return at;
}
MoveClockProbe::~MoveClockProbe() {
  uninstall();
}
bool MoveClockProbe::install(CommandTickFn target, ReadMoveClockFn reader, void* context) noexcept {
  if (!target || !reader || original_.load()) return false;
  MoveClockProbe* empty = nullptr;
  if (!owner_.compare_exchange_strong(empty, this)) return false;
  reader_ = reader;
  context_ = context;
  error_ = hook_.install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&detour));
  if (error_) {
    owner_.store(nullptr);
    reader_ = nullptr;
    context_ = nullptr;
    return false;
  }
  original_.store(reinterpret_cast<CommandTickFn>(hook_.original()));
  enabled_.store(true);
  return true;
}
bool MoveClockProbe::install_live(CommandTickFn target, ReadMoveClockFn reader,
                                  void* context) noexcept {
  if (!target || !reader || original_.load()) return false;
  MoveClockProbe* empty = nullptr;
  if (!owner_.compare_exchange_strong(empty, this)) return false;
  reader_ = reader;
  context_ = context;
  live_target_ = reinterpret_cast<void*>(target);
  const auto init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    owner_.store(nullptr);
    return false;
  }
  void* trampoline = nullptr;
  if (MH_CreateHook(live_target_, reinterpret_cast<void*>(&detour), &trampoline) != MH_OK) {
    owner_.store(nullptr);
    return false;
  }
  // Publish the trampoline before MinHook enables the detour while freezing threads.
  original_.store(reinterpret_cast<CommandTickFn>(trampoline));
  live_ = true;
  if (MH_EnableHook(live_target_) != MH_OK) {
    MH_RemoveHook(live_target_);
    original_.store(nullptr);
    live_ = false;
    owner_.store(nullptr);
    return false;
  }
  enabled_.store(true);
  return true;
}
bool MoveClockProbe::disable_live() noexcept {
  if (!live_) return false;
  enabled_.store(false);
  pending_.store(0);
  const auto status = MH_DisableHook(live_target_);
  return status == MH_OK || status == MH_ERROR_DISABLED;
}
void MoveClockProbe::uninstall() noexcept {
  // Test/owner lifecycle: retire only while no invocation is in flight.
  if (!original_.load()) return;
  enabled_.store(false);
  pending_.store(0);
  if (live_) {
    (void)disable_live();
    while (active_.load()) std::this_thread::yield();
    MH_RemoveHook(live_target_);
    live_ = false;
    live_target_ = nullptr;
  } else
    hook_.uninstall();
  original_.store(nullptr);
  reader_ = nullptr;
  context_ = nullptr;
  owner_.store(nullptr);
}
bool MoveClockProbe::request_extra(int extra, bool next_landing) noexcept {
  if (!enabled_.load() || extra < 0 || extra > maximum_extra) return false;
  std::scoped_lock lock(pending_mutex_);
  if (!extra) {
    pending_.store(0);
    return true;
  }
  if (pending_.load()) return false;
  if (next_landing) {
    const auto last = report().after;
    if (!last.valid || !last.pawn) return false;
    landing_after_ = last.landed_tick;
    landing_pawn_ = last.pawn;
  }
  landing_only_ = next_landing;
  pending_.store(extra);
  return true;
}
MoveClockReport MoveClockProbe::report() const noexcept {
  std::scoped_lock lock(mutex_);
  auto out = report_;
  out.reentrant = reentrant_.load();
  return out;
}
MoveClockReport MoveClockProbe::last_adjustment() const noexcept {
  std::scoped_lock lock(mutex_);
  return last_adjustment_;
}
void MoveClockProbe::detour(void* self, int count) noexcept {
  if (auto* owner = owner_.load()) {
    owner->active_.fetch_add(1);
    owner->invoke(self, count);
    owner->active_.fetch_sub(1);
  }
}
void MoveClockProbe::invoke(void* self, int count) noexcept {
  if (in_call_) {
    reentrant_.fetch_add(1);
    original_.load()(self, count);
    return;
  }
  in_call_ = true;
  struct Reset {
    ~Reset() {
      MoveClockProbe::in_call_ = false;
    }
  } reset;
  MoveClockReport next;
  {
    std::scoped_lock lock(mutex_);
    next = report_;
  }
  ++next.calls;
  next.original_count = count;
  next.applied_extra = 0;
  next.before = reader_(context_, self);
  // Negative counts deliberately run no inner iterations. Preserve them and
  // leave the one-shot request pending for a real command-tick invocation.
  if (next.before.valid && count >= 0 && count <= std::numeric_limits<int>::max() - maximum_extra) {
    std::scoped_lock lock(pending_mutex_);
    const bool eligible =
        !landing_only_ || (next.before.pawn == landing_pawn_ && (next.before.flags & 1) &&
                           next.before.landed_tick > landing_after_);
    if (eligible && enabled_.load()) next.applied_extra = pending_.exchange(0);
    if (next.applied_extra) ++next.adjusted;
  }
  next.forwarded_count = count + next.applied_extra;
  original_.load()(self, next.forwarded_count);
  next.after = reader_(context_, self);
  {
    std::scoped_lock lock(mutex_);
    report_ = next;
    if (next.applied_extra) last_adjustment_ = next;
  }
}
}  // namespace cs2::control
