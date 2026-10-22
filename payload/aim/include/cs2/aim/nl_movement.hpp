#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "cs2/game/math.hpp"
namespace cs2::aim {
struct NlAirState {
  game::Vec2 velocity{};
  float goal_yaw{}, wish_speed{250}, air_cap{30}, air_acceleration{12}, friction{1},
      tick_interval{1.0F / 64}, begin{}, blend{1};
  int steps{16};
  // Explicit turn side; native gain plans latch their selection for one command.
  int side{};
  bool allow_counter{true};
  bool smooth_yaw{};
  // Native directional steering uses heading feedback and per-command gain-side
  // alternation; the recovered dump fixtures retain their separate behavior.
  bool directional_feedback{};
  bool balanced_lateral{};
  unsigned phase{};
  float stamina_recovery{60};
  float seed_yaw{}, maximum_yaw_step{45}, stamina{};
};
struct NlAirEvent {
  float when{};
  game::Vec2 world_wish{};
};
struct NlAirPlan {
  std::array<NlAirEvent, 32> events{};
  int count{};
  game::Vec2 final_velocity{}, displacement{};
  bool valid{};
};
float nl_grid(float fraction) noexcept;
game::Vec2 nl_air_wish(const NlAirState& state, float delta_time) noexcept;
NlAirPlan nl_air_plan(NlAirState state, std::span<const float> synchronized_times = {}) noexcept;
struct NlHopState {
  bool held{}, grounded{}, record_valid{};
  int command_tick{}, landed_tick{};
  float landed_fraction{};
  bool predicted_contact{};
  float contact_fraction{};
  bool actual_valid{};
  int actual_tick{};
  float actual_fraction{};
};
struct NlHopPlan {
  bool release_held{}, pulse{};
  float when{};
  int mode{};
};
NlHopPlan nl_hop_plan(const NlHopState& state) noexcept;
}  // namespace cs2::aim
