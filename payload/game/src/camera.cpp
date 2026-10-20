#include "cs2/game/camera.hpp"

#include <cstddef>
#include <cstdint>

#include "camera_detail.hpp"

namespace cs2::game {
namespace {

using camera_detail::camera_block;
using camera_detail::k_distance_offset;
using camera_detail::k_mode_offset;
using camera_detail::k_pitch_offset;
using camera_detail::k_published_view_rva;
using camera_detail::k_published_view_stride;
using camera_detail::k_switch_offset;
using camera_detail::k_view_fov_candidate_offset;
using camera_detail::k_view_fov_scale_offset;
using camera_detail::k_view_origin_offset;
using camera_detail::k_yaw_offset;
using camera_detail::read_at;
using camera_detail::view_setup;

}  // namespace

// The build-specific reads.
//
// Every one of them is gated by `camera_reads_allowed`: the addresses were
// derived on one client build, and on another build they are still inside
// `client.dll`'s `.data`, so the read would **succeed** — with floats that
// are indistinguishable from real ones. The gate is therefore checked before
// the address is even computed, and the engine access is what the host test
// binds a fake to.

auto read_published_view(const CameraEngine& engine, int slot) noexcept -> CameraState {
  CameraState state;
  if (slot < 0 || !camera_reads_allowed(engine.build_verified)) {
    return state;
  }
  const std::uintptr_t address = engine.client_base + k_published_view_rva +
                                 static_cast<std::uintptr_t>(slot) * k_published_view_stride;
  float values[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  if (!read_at(engine, address, values, sizeof(values))) {
    return state;
  }
  state.origin = Vec3{.x = values[0], .y = values[1], .z = values[2]};
  state.angles = Angle{.pitch = values[3], .yaw = values[4], .roll = values[5]};
  state.valid = true;
  return state;
}

auto read_view_setup_origin(const CameraEngine& engine, int slot) noexcept -> Vec3 {
  if (slot < 0 || !camera_reads_allowed(engine.build_verified)) {
    return {};
  }
  const std::uintptr_t setup = view_setup(engine, slot);
  if (setup == 0) {
    return {};
  }
  float values[3] = {0.0F, 0.0F, 0.0F};
  if (!read_at(engine, setup + k_view_origin_offset, values, sizeof(values))) {
    return {};
  }
  return Vec3{.x = values[0], .y = values[1], .z = values[2]};
}

auto read_view_setup_fov(const CameraEngine& engine, int slot) noexcept -> FovCandidates {
  FovCandidates candidates;
  if (slot < 0 || !camera_reads_allowed(engine.build_verified)) {
    return candidates;
  }
  const std::uintptr_t setup = view_setup(engine, slot);
  if (setup == 0) {
    return candidates;
  }
  if (!read_at(engine, setup + k_view_fov_scale_offset, candidates.scale) ||
      !read_at(engine, setup + k_view_fov_candidate_offset, candidates.candidate)) {
    return candidates;
  }
  candidates.valid = true;
  return candidates;
}

auto camera_origin(const CameraEngine& engine, int slot, const Vec3& fallback) noexcept -> Vec3 {
  const CameraState published = read_published_view(engine, slot);
  if (published.valid) {
    return published.origin;
  }
  const Vec3 setup = read_view_setup_origin(engine, slot);
  if (setup.x != 0.0F || setup.y != 0.0F || setup.z != 0.0F) {
    return setup;
  }
  return fallback;
}

auto read_third_person_state(const CameraEngine& engine) noexcept -> ThirdPersonState {
  ThirdPersonState state;
  // The camera block's offsets are build-specific: on another build the
  // switch's byte is arbitrary, and a state read from it would be trusted
  // by the caller.
  if (!camera_reads_allowed(engine.build_verified)) {
    return state;
  }
  const std::uintptr_t block = camera_block(engine, 0);
  if (block == 0) {
    return state;
  }
  std::uint8_t switch_on = 0;
  std::int32_t mode = 0;
  if (!read_at(engine, block + k_switch_offset, switch_on) ||
      !read_at(engine, block + k_mode_offset, mode)) {
    return state;
  }
  state.switch_on = switch_on != 0;
  state.mode = mode;
  (void)read_at(engine, block + k_pitch_offset, state.pitch);
  (void)read_at(engine, block + k_yaw_offset, state.yaw);
  (void)read_at(engine, block + k_distance_offset, state.distance);
  state.valid = true;
  return state;
}

}  // namespace cs2::game
