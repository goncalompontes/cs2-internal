#pragma once
// Defer command serialization, leaving command generation/prediction and the
// network send stage running. See docs/re/command-choke-2026-10-22.md.
#include <cstdint>
#include <optional>
#include "cs2/game/module.hpp"
#include "cs2/game/rage_debug.hpp"
namespace cs2::control {
struct ChokeSample {
  bool valid{}, grounded{}, attack{};
  std::uintptr_t pawn{}, weapon{}, command_pointer{};
  int command{}, clip{};
  float penalty{};
  std::uint64_t time_ms{};
  float horizontal_speed{-1};
};
class RecoveryChokePolicy {
 public:
  static constexpr float stationary_speed = 1.0F;
  // Returns true to defer this builder invocation. Counts unique consecutive
  // commands, never frames, repeated callbacks or simulation iterations.
  bool defer(const ChokeSample& sample, std::uint64_t now, bool enabled) noexcept;
  int held() const noexcept {
    return held_;
  }

 private:
  ChokeSample previous_{};
  int held_{};
  std::uint64_t started_{};
  bool last_deferred_{};
};
struct CommandSendBinding {
  std::uintptr_t build{}, stage{}, send{};
};
std::optional<CommandSendBinding> resolve_command_send(game::ModuleInfo module) noexcept;
class RecoveryCommandChoke {
 public:
  RecoveryCommandChoke();
  ~RecoveryCommandChoke();
  RecoveryCommandChoke(const RecoveryCommandChoke&) = delete;
  RecoveryCommandChoke& operator=(const RecoveryCommandChoke&) = delete;
  bool configure(bool enabled) noexcept;
  bool enabled() const noexcept;
  void observe(ChokeSample sample) noexcept;
  game::CommandChokeDebug debug() const noexcept;
  // Disable the hook; retain callback/trampoline storage until process exit.
  void stop() noexcept;

 private:
  struct Runtime;
  Runtime* runtime_;
  static void detour(void* self) noexcept;
};
}  // namespace cs2::control
