#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "cs2/game/math.hpp"
namespace cs2::game {
enum class RageDebugGate { not_run, pass, fail, waiting };
enum class RageDebugStage {
  command,
  local,
  bindings,
  schema,
  weapon,
  eye,
  spread,
  world,
  targets,
  proposals,
  damage,
  accuracy,
  history,
  trigger,
  serialization,
  count
};
inline constexpr std::size_t k_rage_debug_stages = static_cast<std::size_t>(RageDebugStage::count);
const char* rage_debug_stage_name(RageDebugStage stage) noexcept;
const char* rage_debug_gate_name(RageDebugGate gate) noexcept;
struct RageSourceDebug {
  bool bindings{}, schema{}, weapon{}, eye{}, spread{}, eye_function_in_client{}, eye_call{},
      history_eye{};
  int type{}, item{}, pellets{}, samples{}, eye_slot{-1};
  float recoil{}, spread_value{}, inaccuracy{}, eye_distance{};
  std::uintptr_t services{}, eye_function{};
  Vec3 local_eye{}, returned_eye{};
};
struct MovementDebug {
  bool strafe{}, bhop{};
  int reason{}, steps{}, hop_mode{}, landed_tick{}, landed_age{};
  float speed{}, goal{}, when{};
  std::uint64_t strafe_writes{}, hop_writes{};
  float counter_wish{};
  bool paired_analog{};
  float terminal_lateral{};
  bool autostop_applied{};
  int autostop_path{};
  bool trace_ready{};
  std::uint64_t trace_calls{},trace_hits{};
};
const char* movement_debug_reason(int reason) noexcept;
struct CommandChokeDebug {
  bool enabled{}, bound{}, deferred{};
  int held{}, command{}, server_executed{}, range_first{}, range_last{};
  int release_first{}, release_last{}, release_command{};
  float penalty{}, horizontal_speed{-1};
  std::uint64_t calls{}, deferred_commands{}, release_attempts{}, sample_ms{};
  std::uintptr_t network{};
};
struct RageDebugSnapshot {
  std::array<RageDebugGate, k_rage_debug_stages> gates{};
  bool enabled{}, auto_fire{}, hook{}, stop_requested{}, aim_ready{}, reload{}, world_known{},
      fallback{};
  std::uint64_t captured_ms{}, observer_ms{}, callbacks{}, unique_commands{}, duplicates{},
      calls5{}, crc_writes{}, crc_failures{}, camera_restores{}, shot_requests{}, ammo_decreases{},
      submitted{}, completed{}, worker_us{};
  int command{}, client_tick{}, tick_base{}, next_primary{}, clip{}, snapshot_clip{}, target{-1},
      group{-1}, players{}, enemies{}, targets{}, proposals{}, examined{}, rejected_immunity{},
      rejected_time{}, rejected_model{}, rejected_changed{};
  float nominal_damage{}, expected_damage{}, probability{}, minimum_damage{},
      required_probability{};
  std::uintptr_t pawn{}, weapon{}, vdata{};
  CommandChokeDebug choke{};
  MovementDebug movement{};
  RageSourceDebug source{};
  char weapon_name[64]{}, detail[192]{"Waiting for command callback"}, trigger[48]{"not reached"};
};
std::string format_rage_debug(const RageDebugSnapshot& snapshot);
}  // namespace cs2::game
