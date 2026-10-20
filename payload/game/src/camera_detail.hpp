#pragma once

// The camera module's build-specific offsets and the helpers that read
// through the engine access.
//
// This header is private to the game module. The offsets are the ones the
// reverse-engineering pass derived on **one** client build; the reads go
// through the engine access, so the host test can bind a fake one and prove
// that the build guard refuses the read before the address is touched.

#include <cstddef>
#include <cstdint>

#include "cs2/game/camera.hpp"
#include "cs2/game/memory.hpp"

namespace cs2::game::camera_detail {

// --- The offsets (client.dll build 25947352, PE timestamp 0x6ad01314) -------
//
// Re-derived by the reverse-engineering pass on the live build. They are
// build-specific: `docs/re/client-dll.md` documents an older build (25815307)
// and none of its addresses may be reused.

// The `CCSGOInput` camera block, relative to the input object (slot 0).
inline constexpr std::size_t k_switch_offset = 0x229;    // uint8: the third-person switch
inline constexpr std::size_t k_pitch_offset = 0x230;     // float: the camera pitch
inline constexpr std::size_t k_yaw_offset = 0x234;       // float: the camera yaw
inline constexpr std::size_t k_distance_offset = 0x238;  // float: the camera distance
inline constexpr std::size_t k_mode_offset = 0x6a8;      // int32: the camera mode
// The per-slot stride of the camera block (slot 0 is at the object base).
inline constexpr std::size_t k_slot_stride = 0x928;

// The published per-slot view state: the origin (12 bytes) then the angles
// (12 bytes), written by the view-setup finaliser (`FUN_180c1a710`).
inline constexpr std::uintptr_t k_published_view_rva = 0x256af48;
inline constexpr std::size_t k_published_view_stride = 0xc;

// The `CViewRender` singleton: the pointer global, the per-slot view setup
// (`+0x10 + slot*0x990`) and the origin inside it (`+0x4a0`).
inline constexpr std::uintptr_t k_view_render_rva = 0x256b9e8;
inline constexpr std::size_t k_view_setup_offset = 0x10;
inline constexpr std::size_t k_view_setup_stride = 0x990;
inline constexpr std::size_t k_view_origin_offset = 0x4a0;

// The view setup's origin and the two FOV candidates.
inline constexpr std::size_t k_view_fov_scale_offset = 0x4cc;
inline constexpr std::size_t k_view_fov_candidate_offset = 0x4d0;

// The `cam_idealdist` convar: the 16-byte `ConVarRef` global holds the
// `ConVar*` at +8, and a convar's float value lives at `ConVar* + 0x58`.
inline constexpr std::uintptr_t k_cam_ideal_dist_rva = 0x255fe88;
inline constexpr std::size_t k_convar_value_offset = 0x58;

// The client's per-frame camera update (`FUN_180b62a20`).
inline constexpr std::uintptr_t k_camera_update_rva = 0xb62a20;

// The `CCSGOInput` singleton object — the fallback when the dumper's
// `ccsgo_input` signature does not resolve (the same address the signature
// reproduces on this build).
inline constexpr std::uintptr_t k_input_object_rva = 0x257b640;

// The client build the offsets above were verified on.
inline constexpr std::uint32_t k_client_timestamp = 0x6ad01314U;

// --- The reads --------------------------------------------------------------

// Read `count` bytes through the engine access.
[[nodiscard]] inline auto read_at(const CameraEngine& engine, std::uintptr_t address, void* out,
                                  std::size_t count) noexcept -> bool {
  if (engine.read_bytes == nullptr) {
    return false;
  }
  return engine.read_bytes(engine.read_context, address, out, count);
}

template <typename T>
[[nodiscard]] inline auto read_at(const CameraEngine& engine, std::uintptr_t address,
                                  T& out) noexcept -> bool {
  return read_at(engine, address, &out, sizeof(T));
}

// True when `[address, address + size)` is mapped **and readable**. The first
// and the last byte are probed through the fault-safe reader, so a stale
// offset fails cleanly instead of faulting the game.
//
// The name says what it proves: the helper probes with the *read* helper, so
// it says nothing about writability. The stores use the checked write
// (`cs2::game::write_bytes`), which fails on a read-only page instead of
// faulting inside the game.
[[nodiscard]] inline auto readable(const CameraEngine& engine, std::uintptr_t address,
                                   std::size_t size) noexcept -> bool {
  if (address == 0 || size == 0) {
    return false;
  }
  unsigned char probe = 0;
  return read_at(engine, address, &probe, 1) && read_at(engine, address + size - 1, &probe, 1);
}

// The camera block of the input object for `slot` (slot 0 is the local
// player and sits at the object base).
[[nodiscard]] inline auto camera_block(const CameraEngine& engine, int slot) noexcept
    -> std::uintptr_t {
  if (engine.input_object == 0 || slot < 0) {
    return 0;
  }
  return engine.input_object + static_cast<std::uintptr_t>(slot) * k_slot_stride;
}

// The view setup of `slot` (`CViewRender + 0x10 + slot*0x990`), or 0.
[[nodiscard]] inline auto view_setup(const CameraEngine& engine, int slot) noexcept
    -> std::uintptr_t {
  if (slot < 0) {
    return 0;
  }
  std::uintptr_t view_render = 0;
  if (!read_at(engine, engine.client_base + k_view_render_rva, view_render) || view_render == 0) {
    return 0;
  }
  return view_render + k_view_setup_offset +
         static_cast<std::uintptr_t>(slot) * k_view_setup_stride;
}

}  // namespace cs2::game::camera_detail
