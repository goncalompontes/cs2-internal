#include "cs2/game/camera.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "offsets.hpp"

namespace cs2::game {
namespace {

using namespace cs2::sdk;

// --- The offsets (client.dll build 25947352, PE timestamp 0x6ad01314) -------
//
// Re-derived by the reverse-engineering pass on the live build. They are
// build-specific: `docs/re/client-dll.md` documents an older build (25815307)
// and none of its addresses may be reused.

// The `CCSGOInput` camera block, relative to the input object (slot 0).
constexpr std::size_t k_switch_offset = 0x229;    // uint8: the third-person switch
constexpr std::size_t k_pitch_offset = 0x230;     // float: the camera pitch
constexpr std::size_t k_yaw_offset = 0x234;       // float: the camera yaw
constexpr std::size_t k_distance_offset = 0x238;  // float: the camera distance
constexpr std::size_t k_mode_offset = 0x6a8;      // int32: the camera mode
// The per-slot stride of the camera block (slot 0 is at the object base).
constexpr std::size_t k_slot_stride = 0x928;

// The published per-slot view state: the origin (12 bytes) then the angles
// (12 bytes), written by the view-setup finaliser (`FUN_180c1a710`).
constexpr std::uintptr_t k_published_view_rva = 0x256af48;
constexpr std::size_t k_published_view_stride = 0xc;

// The `CViewRender` singleton: the pointer global, the per-slot view setup
// (`+0x10 + slot*0x990`) and the origin inside it (`+0x4a0`).
constexpr std::uintptr_t k_view_render_rva = 0x256b9e8;
constexpr std::size_t k_view_setup_offset = 0x10;
constexpr std::size_t k_view_setup_stride = 0x990;
constexpr std::size_t k_view_origin_offset = 0x4a0;

// The view setup's origin and the two FOV candidates.
constexpr std::size_t k_view_fov_scale_offset = 0x4cc;
constexpr std::size_t k_view_fov_candidate_offset = 0x4d0;

// The `cam_idealdist` convar: the 16-byte `ConVarRef` global holds the
// `ConVar*` at +8, and a convar's float value lives at `ConVar* + 0x58`.
constexpr std::uintptr_t k_cam_ideal_dist_rva = 0x255fe88;
constexpr std::size_t k_convar_value_offset = 0x58;

// The client's per-frame camera update (`FUN_180b62a20`).
constexpr std::uintptr_t k_camera_update_rva = 0xb62a20;

// The `CCSGOInput` singleton object — the fallback when the dumper's
// `ccsgo_input` signature does not resolve (the same address the signature
// reproduces on this build).
constexpr std::uintptr_t k_input_object_rva = 0x257b640;

// The client build the offsets above were verified on.
constexpr std::uint32_t k_client_timestamp = 0x6ad01314u;

// The engine's own console/pipe slot (`Cbuf_AddText`).
constexpr int k_console_slot = 5;

using add_text_fn = void (*)(void* input_service, int slot, const char* text, int flags);

// True when `[address, address + size)` is mapped. The first and the last byte
// are probed through the fault-safe reader, so a stale offset fails cleanly
// instead of faulting the game.
[[nodiscard]] auto writable(std::uintptr_t address, std::size_t size) noexcept -> bool {
  if (address == 0 || size == 0) {
    return false;
  }
  unsigned char probe = 0;
  return read_bytes(address, &probe, 1) && read_bytes(address + size - 1, &probe, 1);
}

// The client's image base (0 when the client is not loaded).
[[nodiscard]] auto client_base() noexcept -> std::uintptr_t {
  return find_module("client.dll").base;
}

// The camera block of the input object for `slot` (slot 0 is the local
// player and sits at the object base).
[[nodiscard]] auto camera_block(int slot) noexcept -> std::uintptr_t {
  const std::uintptr_t input = input_object();
  if (input == 0 || slot < 0) {
    return 0;
  }
  return input + static_cast<std::uintptr_t>(slot) * k_slot_stride;
}

}  // namespace

auto input_object() noexcept -> std::uintptr_t {
  const std::uintptr_t base = client_base();
  if (base == 0) {
    return 0;
  }
  const OffsetInfo* info = find_offset("ccsgo_input");
  if (info != nullptr && info->module == "client.dll") {
    // The dumper's signature: it re-resolves per build.
    return base + info->rva;
  }
  // The verified build's object address (the signature reproduces it). On
  // another build the address is unknown, and guessing it would read the
  // wrong object.
  if (!client_build_verified()) {
    return 0;
  }
  return base + k_input_object_rva;
}

auto client_build_timestamp() noexcept -> std::uint32_t {
  const ModuleInfo module = find_module("client.dll");
  if (!module.valid()) {
    return 0;
  }
  // The PE header: `e_lfanew` at +0x3c, then the COFF header, whose
  // `TimeDateStamp` is the first field after the signature.
  std::uint32_t lfanew = 0;
  if (!read(module.base + 0x3c, lfanew) || lfanew == 0) {
    return 0;
  }
  std::uint32_t timestamp = 0;
  if (!read(module.base + lfanew + 8, timestamp)) {
    return 0;
  }
  return timestamp;
}

auto client_build_verified() noexcept -> bool {
  const std::uint32_t timestamp = client_build_timestamp();
  return timestamp != 0 && timestamp == k_client_timestamp;
}

auto read_published_view(int slot) noexcept -> CameraState {
  CameraState state;
  if (slot < 0) {
    return state;
  }
  const std::uintptr_t base = client_base();
  if (base == 0) {
    return state;
  }
  const std::uintptr_t address =
      base + k_published_view_rva + static_cast<std::uintptr_t>(slot) * k_published_view_stride;
  float values[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
  if (!read_bytes(address, values, sizeof(values))) {
    return state;
  }
  state.origin = Vec3{values[0], values[1], values[2]};
  state.angles = Angle{values[3], values[4], values[5]};
  state.valid = true;
  return state;
}

auto read_view_setup_origin(int slot) noexcept -> Vec3 {
  if (slot < 0) {
    return {};
  }
  const std::uintptr_t base = client_base();
  if (base == 0) {
    return {};
  }
  std::uintptr_t view_render = 0;
  if (!read(base + k_view_render_rva, view_render) || view_render == 0) {
    return {};
  }
  const std::uintptr_t setup =
      view_render + k_view_setup_offset + static_cast<std::uintptr_t>(slot) * k_view_setup_stride;
  float values[3] = {0.0F, 0.0F, 0.0F};
  if (!read_bytes(setup + k_view_origin_offset, values, sizeof(values))) {
    return {};
  }
  return Vec3{values[0], values[1], values[2]};
}

auto read_view_setup_fov(int slot) noexcept -> FovCandidates {
  FovCandidates candidates;
  if (slot < 0) {
    return candidates;
  }
  const std::uintptr_t base = client_base();
  if (base == 0) {
    return candidates;
  }
  std::uintptr_t view_render = 0;
  if (!read(base + k_view_render_rva, view_render) || view_render == 0) {
    return candidates;
  }
  const std::uintptr_t setup =
      view_render + k_view_setup_offset + static_cast<std::uintptr_t>(slot) * k_view_setup_stride;
  if (!read(setup + k_view_fov_scale_offset, candidates.scale) ||
      !read(setup + k_view_fov_candidate_offset, candidates.candidate)) {
    return candidates;
  }
  candidates.valid = true;
  return candidates;
}

auto camera_origin(int slot, const Vec3& fallback) noexcept -> Vec3 {
  const CameraState published = read_published_view(slot);
  if (published.valid) {
    return published.origin;
  }
  const Vec3 setup = read_view_setup_origin(slot);
  if (setup.x != 0.0F || setup.y != 0.0F || setup.z != 0.0F) {
    return setup;
  }
  return fallback;
}

auto read_third_person_state() noexcept -> ThirdPersonState {
  ThirdPersonState state;
  const std::uintptr_t block = camera_block(0);
  if (block == 0) {
    return state;
  }
  std::uint8_t switch_on = 0;
  std::int32_t mode = 0;
  if (!read(block + k_switch_offset, switch_on) || !read(block + k_mode_offset, mode)) {
    return state;
  }
  state.switch_on = switch_on != 0;
  state.mode = mode;
  (void)read(block + k_pitch_offset, state.pitch);
  (void)read(block + k_yaw_offset, state.yaw);
  (void)read(block + k_distance_offset, state.distance);
  state.valid = true;
  return state;
}

auto read_third_person_switch() noexcept -> bool {
  return read_third_person_state().switch_on;
}

auto write_third_person_state(const ThirdPersonParams& params, float pitch, float yaw) noexcept
    -> bool {
  // The offsets are build-specific: a write on another build would land in
  // the wrong field of the input object.
  if (!client_build_verified()) {
    return false;
  }
  const std::uintptr_t block = camera_block(0);
  if (block == 0) {
    return false;
  }
  if (!writable(block + k_switch_offset, 1) ||
      !writable(block + k_mode_offset, sizeof(std::int32_t)) ||
      !writable(block + k_pitch_offset, 3 * sizeof(float))) {
    return false;
  }
  // The distance, the camera's angles, the mode (0 = third person) and the
  // switch the camera code gates on. The engine smooths the angles and the
  // distance towards its own targets right after this write, so the switch
  // is set last: the camera goes third person with its state in place.
  *reinterpret_cast<float*>(block + k_distance_offset) = params.distance;
  *reinterpret_cast<float*>(block + k_pitch_offset) = pitch;
  *reinterpret_cast<float*>(block + k_yaw_offset) = yaw;
  *reinterpret_cast<std::int32_t*>(block + k_mode_offset) = 0;
  *reinterpret_cast<std::uint8_t*>(block + k_switch_offset) = 1;
  return true;
}

auto clear_third_person_switch() noexcept -> bool {
  if (!client_build_verified()) {
    return false;
  }
  const std::uintptr_t block = camera_block(0);
  if (block == 0 || !writable(block + k_switch_offset, 1)) {
    return false;
  }
  *reinterpret_cast<std::uint8_t*>(block + k_switch_offset) = 0;
  return true;
}

auto exec_engine_command(std::string_view line) noexcept -> bool {
  if (line.empty()) {
    return false;
  }
  const ModuleInfo module = find_module("engine2.dll");
  if (!module.valid()) {
    return false;
  }
  const OffsetInfo* add_text = find_offset("cbuf_add_text");
  const OffsetInfo* input_service = find_offset("input_service");
  if (add_text == nullptr || input_service == nullptr || add_text->module != "engine2.dll" ||
      input_service->module != "engine2.dll") {
    return false;
  }
  // The engine expects a NUL-terminated line.
  const std::string text(line);
  reinterpret_cast<add_text_fn>(module.base + add_text->rva)(
      reinterpret_cast<void*>(module.base + input_service->rva), k_console_slot, text.c_str(), 0);
  return true;
}

auto read_ideal_distance() noexcept -> float {
  // The convar's address is build-specific like the rest of the camera
  // block, so the read is guarded: a stale address would report a value that
  // belongs to some other convar.
  if (!client_build_verified()) {
    return 0.0F;
  }
  const std::uintptr_t base = client_base();
  if (base == 0) {
    return 0.0F;
  }
  std::uintptr_t convar = 0;
  if (!read(base + k_cam_ideal_dist_rva, convar) || convar == 0) {
    return 0.0F;
  }
  float value = 0.0F;
  if (!read(convar + k_convar_value_offset, value)) {
    return 0.0F;
  }
  return value;
}

auto camera_update_address() noexcept -> std::uintptr_t {
  const ModuleInfo module = find_module("client.dll");
  if (!module.valid()) {
    return 0;
  }
  return module.base + k_camera_update_rva;
}

}  // namespace cs2::game
