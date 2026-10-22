#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include "cs2/aim/ballistics.hpp"
#include "cs2/game/module.hpp"
#include "cs2/game/rage_debug.hpp"
#include "cs2/game/snapshot.hpp"
namespace cs2::control {
enum class ShotBlock {
  ready,
  no_data,
  no_target,
  cooldown,
  reload,
  empty,
  accuracy,
  history,
  recoil,
  duplicate,
  stale
};
const char* shot_block_name(ShotBlock block) noexcept;
struct ShotState {
  std::uintptr_t pawn{}, weapon{};
  int command{}, client_tick{}, tick_base{}, next_primary{}, clip{};
  bool valid{}, reloading{};
  bool history_eye{};
};
class ShotGate {
 public:
  ShotBlock check(const ShotState& state) noexcept;
  void commit(const ShotState& state) noexcept;

 private:
  std::uintptr_t pawn_{}, weapon_{};
  int last_tick_{(std::numeric_limits<int>::min)()}, last_command_{-1};
};
struct ShotTiming {
  int render_tick{};
  float render_fraction{};
  int player_tick{};
  float player_fraction{};
  bool valid{}, player_valid{};
};
// Engine calls are made only from the command thread; returned samples are values.
class NativeShotSource {
  friend struct NativeShotTestAccess;

 public:
  game::RageSourceDebug debug() const noexcept {
    return debug_;
  }
  bool prepare(game::ModuleInfo module) noexcept;
  std::optional<unsigned> shooting_position_slot(std::uintptr_t table) const noexcept;
  std::uintptr_t shooting_position_function() const noexcept {
    return shoot_position_;
  }
  bool schema_bindings_match(std::uint32_t spread, std::uint32_t mode) const noexcept;
  const char* diagnostic() const noexcept {
    return diagnostic_;
  }

  bool sample(const game::PlayerSnapshot& local, void* command, ShotState& state, game::Vec3& eye,
              std::vector<aim::KernelSample>& kernel) noexcept;

 private:
  bool fields() noexcept;
  float (*spread_)(std::uintptr_t){};
  float (*inaccuracy_)(std::uintptr_t, float*, float*){};
  void (*calculate_)(std::int16_t, int, int, std::uint32_t, float, float, float, float*, float*){};
  std::uintptr_t shoot_position_{};
  game::ModuleInfo module_{};
  std::uint32_t clip_{}, reload_{}, next_{}, tick_{}, services_{}, recoil_{}, pellets_{},
      attributes_{}, item_{}, definition_{}, type_{}, accuracy_begin_{}, accuracy_end_{};
  game::RageSourceDebug debug_{};
  char diagnostic_[160]{"not sampled"};
  bool fields_ready_{};
  std::uintptr_t attempted_base_{};
  unsigned long long fields_retry_ms_{};
};
ShotTiming command_timing(void* command, float simulation_time) noexcept;
bool stamp_shot_timing(void* command, const ShotTiming& timing) noexcept;
}  // namespace cs2::control
