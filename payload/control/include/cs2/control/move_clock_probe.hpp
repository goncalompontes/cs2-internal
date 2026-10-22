#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include "cs2/game/module.hpp"
#include "cs2/hooks/inline.hpp"
namespace cs2::control {
// Inspected Source 2 command-tick ABI. This is not Source 1 CL_Move(float,bool).
using CommandTickFn = void (*)(void*, int);
struct MoveClockSnapshot {
  bool valid{};
  int command{}, tick_base{}, last_processed{}, acknowledged_command{-1};
  float accuracy_penalty{};
  std::uint64_t time_ms{}, pawn{}, weapon{};
  std::uint32_t flags{};
  int landed_tick{}, clip{};
  float landed_fraction{}, inaccuracy{}, movement_inaccuracy{}, air_inaccuracy{}, spread{};
  float velocity_x{}, velocity_y{}, velocity_z{};
  double network_time{};
  std::uint64_t controller{};
  int health{}, life_state{-1};
  std::uint32_t pawn_handle{}, actor_reads{};
};
using ReadMoveClockFn = MoveClockSnapshot (*)(void*, void*) noexcept;
struct MoveClockReport {
  std::uint64_t calls{}, adjusted{}, reentrant{};
  int original_count{}, forwarded_count{}, applied_extra{};
  MoveClockSnapshot before{}, after{};
};
// Read PE bytes and verify the profile label, integer argument and inner stages.
// The returned address is not executed by resolution or the binding probe.
std::optional<std::uintptr_t> resolve_command_tick(game::ModuleInfo module) noexcept;
class MoveClockProbe {
 public:
  static constexpr int maximum_extra = 8;
  ~MoveClockProbe();
  MoveClockProbe() = default;
  MoveClockProbe(const MoveClockProbe&) = delete;
  MoveClockProbe& operator=(const MoveClockProbe&) = delete;
  // Installation/removal require a quiescent target. Not auto-installed in CS2.
  bool install(CommandTickFn target, ReadMoveClockFn reader, void* context) noexcept;
  bool install_live(CommandTickFn target, ReadMoveClockFn reader, void* context) noexcept;
  // Disable the live hook without freeing its trampoline. The live DLL keeps
  // the probe resident until process exit to accommodate callbacks in flight.
  bool disable_live() noexcept;
  void uninstall() noexcept;
  bool request_extra(int extra, bool next_landing = false) noexcept;
  MoveClockReport report() const noexcept;
  MoveClockReport last_adjustment() const noexcept;
  std::optional<hooks::InlineError> hook_error() const noexcept {
    return error_;
  }

 private:
  static void detour(void* self, int count) noexcept;
  void invoke(void* self, int count) noexcept;
  static std::atomic<MoveClockProbe*> owner_;
  static thread_local bool in_call_;
  hooks::InlineHook hook_;
  std::atomic<CommandTickFn> original_{nullptr};
  void* live_target_{};
  bool live_{};
  std::atomic<bool> enabled_{false};
  std::atomic<unsigned> active_{0};
  ReadMoveClockFn reader_{};
  void* context_{};
  std::atomic<int> pending_{0};
  std::mutex pending_mutex_;
  bool landing_only_{};
  int landing_after_{};
  std::uint64_t landing_pawn_{};
  std::atomic<std::uint64_t> reentrant_{0};
  mutable std::mutex mutex_;
  MoveClockReport report_{}, last_adjustment_{};
  std::optional<hooks::InlineError> error_;
};
}  // namespace cs2::control
