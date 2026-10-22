#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

#include "cs2/aim/autostop.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* message) {
  if (!value) {
    std::printf("FAIL: %s\n", message);
    ++failures;
  }
}
auto close(float a, float b) -> bool {
  return std::abs(a - b) < 0.0001F;
}
#include "autostop_ground_fixtures.hpp"
}  // namespace

auto main() -> int {
  using namespace cs2::aim;
  StopState state;
  StopCommand cmd;
  expect(solve_autostop(state, cmd).path == StopPath::unchanged, "rest leaves input alone");
  state.vx = 150;
  cmd.axes.up = .25F;
  auto result = solve_autostop(state, cmd);
  expect(result.path == StopPath::ground_reverse, "no XY input falls back to reverse");
  expect(result.command.axes.forward == -1 && result.command.axes.side == 0 &&
             result.command.axes.up == .25F,
         "fallback preserves third axis");
  state.vx = 90;
  state.fractional = false;
  cmd.axes = {1, 0, 0};
  result = solve_autostop(state, cmd);
  expect(result.path == StopPath::ground_scaled, "ordinary ground uses analog scaling");
  expect(close(result.command.axes.forward, .3366F), "ordinary scaling follows threshold");

  cmd.pitch = 70.0F;
  result = solve_autostop(state, cmd);
  expect(result.path == StopPath::ground_scaled && close(result.command.axes.forward, .3366F),
         "horizontal basis normalization preserves speed while looking steeply");
  state.vx = 0.0F;
  state.vy = -90.0F;
  cmd.pitch = 0.0F;
  cmd.axes = {0, 1, 0};
  result = solve_autostop(state, cmd);
  expect(result.path == StopPath::ground_scaled && close(result.command.axes.side, .3366F),
         "side-axis projection follows the project's right-vector convention");

  std::size_t fractional = 0;
  for (const auto& fixture : k_ground_fixtures) {
    StopState s;
    s.speed_limit = std::bit_cast<float>(fixture[0]);
    s.vx = std::bit_cast<float>(fixture[1]);
    s.ground_friction = std::bit_cast<float>(fixture[2]);
    s.ground_acceleration = std::bit_cast<float>(fixture[3]);
    StopCommand input;
    input.axes = {1, 0, .25F};
    const auto out = solve_autostop(s, input);
    if (fixture[5] != 0) {
      ++fractional;
      expect(out.path == StopPath::ground_fractional,
             "dump instruction fixture selects fractional path");
      expect(out.event_count == 2 && std::bit_cast<unsigned>(out.events[1].when) == fixture[4],
             "dump instruction fixture fraction matches bitwise");
      expect(out.events[0].when == 0 && out.events[1].command.axes.forward == -1 &&
                 out.events[1].command.axes.up == -.25F && !out.events[1].angles,
             "ground events reverse all three axes, without angle data");
    } else
      expect(out.path == StopPath::ground_reverse, "dump instruction fixture falls back");
  }
  expect(fractional > 0, "fixtures cover successful fractional branch");

  state = StopState{};
  state.ground = false;
  state.fractional = false;
  state.vx = 100;
  cmd = StopCommand{};
  result = solve_autostop(state, cmd);
  expect(result.path == StopPath::air && result.command.axes.forward < 0 &&
             result.command.axes.forward > -1,
         "air counter-movement uses a partial acceleration budget");
  expect(close(result.command.axes.forward, -((100 - (250 * .34F) * .99F) / (12 * 250 / 64.F))),
         "air budget matches independently calculated impulse");
  state.air_acceleration = 0.0F;
  expect(solve_autostop(state, cmd).command.axes.forward == -1.0F,
         "zero acceleration budget saturates to reverse, as positive infinity did in the dump");
  state.air_acceleration = 12.0F;
  state.vx = 0.5F;
  expect(solve_autostop(state, cmd).path == StopPath::unchanged, "tiny air speed cutoff");
  state.vx = 100;
  state.force_full_air = true;
  expect(solve_autostop(state, cmd).command.axes.forward == -1, "force full air input");
  state.force_full_air = false;
  state.fractional = true;
  state.vz = 2;
  result = solve_autostop(state, cmd);
  expect(result.path == StopPath::air_fractional && result.event_count == 2,
         "positive vertical crossing splits air step");
  expect(result.events[0].angles && result.events[1].angles &&
             close(result.events[1].when, 11.F / 64.F),
         "air split quantizes upward and includes angles");
  state.vz = -2;
  expect(solve_autostop(state, cmd).path == StopPath::air,
         "descending motion uses ordinary air path");
  state.vz = 12.5F;
  expect(solve_autostop(state, cmd).path == StopPath::air, "vertical upper boundary is exclusive");
  state.vz = 12.49F;
  result = solve_autostop(state, cmd);
  expect(result.event_count == 2 && result.events[1].when == 1 &&
             std::isfinite(result.command.axes.forward),
         "zero-length second interval stays finite");

  state = StopState{};
  state.weapon_scaling = true;
  state.weapon_speed = {100, 200};
  state.weapon_selector = 1;
  expect(close(stop_weapon_scale(state), .8F), "weapon secondary scale");
  state.weapon_selector = -1;
  expect(close(stop_weapon_scale(state), .4F), "bad selector falls back to zero");
  state.duck_active = true;
  state.weapon_category = 1;
  expect(stop_weapon_scale(state) == 1, "flagged low category preserves full acceleration");
  state.weapon_category = 2;
  state.weapon_selector = 1;
  expect(close(stop_weapon_scale(state), .8F), "flagged slow high category scales");
  state.weapon_speed[1] = 250;
  expect(stop_weapon_scale(state) == 1, "110 comparison preserves full acceleration");
  state.speed_limit = std::numeric_limits<float>::quiet_NaN();
  expect(solve_autostop(state, cmd).path == StopPath::invalid, "NaN state rejected");
  state = StopState{};
  state.interval = 0;
  expect(solve_autostop(state, cmd).path == StopPath::invalid, "zero time rejected");
  std::printf("autostop: %d failures; %zu dump-loop fixtures (%zu fractional)\n", failures,
              sizeof(k_ground_fixtures) / sizeof(k_ground_fixtures[0]), fractional);
  return failures == 0 ? 0 : 1;
}
