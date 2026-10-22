#pragma once

#include <array>
#include <cstddef>

namespace cs2::aim {

struct StopAxes {
  float forward = 0.0F;
  float side = 0.0F;
  float up = 0.0F;
};

struct StopCommand {
  StopAxes axes{};
  float pitch = 0.0F;
  float yaw = 0.0F;
  float roll = 0.0F;
};

// Explicit inputs replace the source dump's unresolved schema/global accesses.
struct StopState {
  float vx = 0.0F;
  float vy = 0.0F;
  float vz = 0.0F;
  float speed_limit = 250.0F;
  float interval = 1.0F / 64.0F;
  float movement_factor = 1.0F;
  float ground_friction = 5.2F;
  float stop_speed = 80.0F;
  float ground_acceleration = 5.5F;
  float air_acceleration = 12.0F;
  float air_wish_cap = 30.0F;
  float vertical_coefficient = 800.0F;
  float stamina = 0.0F;
  float stamina_recovery = 0.0F;
  float duck_amount = 0.0F;
  bool ground = true;
  bool duck_active = false;
  bool force_full_air = false;
  bool fractional = true;
  // Keep disabled unless the adapter can supply the dump's conditional inputs.
  bool weapon_scaling = false;
  int weapon_selector = 0;
  int weapon_category = 0;
  std::array<float, 2> weapon_speed{250.0F, 250.0F};
};

struct StopEvent {
  float when = 0.0F;
  StopCommand command{};
  bool angles = false;
};

enum class StopPath {
  inactive,
  unchanged,
  ground_scaled,
  ground_reverse,
  ground_fractional,
  air,
  air_fractional,
  invalid
};

struct StopResult {
  StopCommand command{};
  std::array<StopEvent, 2> events{};
  std::size_t event_count = 0;
  StopPath path = StopPath::inactive;
  float threshold = 0.0F;
};

[[nodiscard]] auto stop_weapon_scale(const StopState& state) noexcept -> float;
// Finite, nonnegative physics inputs are required. Bad inputs produce invalid
// rather than putting NaNs into a live user command. No game memory is touched.
[[nodiscard]] auto solve_autostop(const StopState& state, const StopCommand& command) noexcept
    -> StopResult;

}  // namespace cs2::aim
