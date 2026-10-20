#include "cs2/game/camera.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "camera_detail.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "offsets.hpp"

namespace cs2::game {
namespace {

using namespace cs2::sdk;

using camera_detail::k_cam_ideal_dist_rva;
using camera_detail::k_camera_update_rva;
using camera_detail::k_client_timestamp;
using camera_detail::k_convar_value_offset;
using camera_detail::k_distance_offset;
using camera_detail::k_input_object_rva;
using camera_detail::k_mode_offset;
using camera_detail::k_pitch_offset;
using camera_detail::k_switch_offset;
using camera_detail::k_yaw_offset;
using camera_detail::readable;

// The engine's own console/pipe slot (`Cbuf_AddText`).
constexpr int k_console_slot = 5;

using add_text_fn = void (*)(void* input_service, int slot, const char* text, int flags);

// The fault-safe reader, bound to the live process.
[[nodiscard]] auto live_read_bytes(void* context, std::uintptr_t address, void* out,
                                   std::size_t count) noexcept -> bool {
  (void)context;
  return read_bytes(address, out, count);
}

// The client's PE timestamp, read from the module's headers.
[[nodiscard]] auto timestamp_of(const ModuleInfo& module) noexcept -> std::uint32_t {
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

// The `CCSGOInput` singleton object, resolved against a client image.
//
// The dumper's signature re-resolves per build, so it is preferred; the
// verified build's own address is the fallback, and it is only used when the
// build is the verified one — on another build the address is unknown, and
// guessing it would read the wrong object.
[[nodiscard]] auto input_object_at(std::uintptr_t base, bool build_verified) noexcept
    -> std::uintptr_t {
  if (base == 0) {
    return 0;
  }
  const OffsetInfo* info = find_offset("ccsgo_input");
  if (info != nullptr && info->module == "client.dll") {
    // The dumper's signature: it re-resolves per build.
    return base + info->rva;
  }
  if (!build_verified) {
    return 0;
  }
  return base + k_input_object_rva;
}

}  // namespace

// The live bindings: the parts that need the running client (and the
// generated SDK header for the dumper's signature table). The reads
// themselves live in `camera.cpp`, parameterised by the engine access, so
// the host test can drive them against a fake client.

auto input_object() noexcept -> std::uintptr_t {
  const ModuleInfo module = find_module("client.dll");
  return input_object_at(module.base, timestamp_of(module) == k_client_timestamp);
}

auto client_build_timestamp() noexcept -> std::uint32_t {
  return timestamp_of(find_module("client.dll"));
}

auto client_build_verified() noexcept -> bool {
  const std::uint32_t timestamp = client_build_timestamp();
  return timestamp != 0 && timestamp == k_client_timestamp;
}

auto camera_engine() noexcept -> CameraEngine {
  CameraEngine engine;
  const ModuleInfo module = find_module("client.dll");
  if (!module.valid()) {
    return engine;
  }
  engine.client_base = module.base;
  engine.build_verified = timestamp_of(module) == k_client_timestamp;
  engine.read_bytes = &live_read_bytes;
  engine.read_context = nullptr;
  engine.input_object = input_object_at(module.base, engine.build_verified);
  return engine;
}

auto read_published_view(int slot) noexcept -> CameraState {
  return read_published_view(camera_engine(), slot);
}

auto read_view_setup_origin(int slot) noexcept -> Vec3 {
  return read_view_setup_origin(camera_engine(), slot);
}

auto read_view_setup_fov(int slot) noexcept -> FovCandidates {
  return read_view_setup_fov(camera_engine(), slot);
}

auto camera_origin(int slot, const Vec3& fallback) noexcept -> Vec3 {
  return camera_origin(camera_engine(), slot, fallback);
}

auto read_third_person_state() noexcept -> ThirdPersonState {
  return read_third_person_state(camera_engine());
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
  const CameraEngine engine = camera_engine();
  const std::uintptr_t block = engine.input_object;
  if (block == 0) {
    return false;
  }
  if (!readable(engine, block + k_switch_offset, 1) ||
      !readable(engine, block + k_mode_offset, sizeof(std::int32_t)) ||
      !readable(engine, block + k_pitch_offset, 3 * sizeof(float))) {
    return false;
  }
  // The distance, the camera's angles, the mode (0 = third person) and the
  // switch the camera code gates on. The engine smooths the angles and the
  // distance towards its own targets right after this write, so the switch
  // is set last: the camera goes third person with its state in place.
  //
  // The stores go through the checked write: the payload is mapped without
  // exception tables, so a fault inside the game would be fatal. A read-only
  // page fails the write instead of faulting.
  const std::int32_t mode = 0;
  const std::uint8_t switch_on = 1;
  return !(!write_bytes(block + k_distance_offset, &params.distance, sizeof(float)) ||
           !write_bytes(block + k_pitch_offset, &pitch, sizeof(float)) ||
           !write_bytes(block + k_yaw_offset, &yaw, sizeof(float)) ||
           !write_bytes(block + k_mode_offset, &mode, sizeof(std::int32_t)) ||
           !write_bytes(block + k_switch_offset, &switch_on, sizeof(std::uint8_t)));
}

auto clear_third_person_switch() noexcept -> bool {
  if (!client_build_verified()) {
    return false;
  }
  const CameraEngine engine = camera_engine();
  const std::uintptr_t block = engine.input_object;
  if (block == 0 || !readable(engine, block + k_switch_offset, 1)) {
    return false;
  }
  const std::uint8_t switch_off = 0;
  return write_bytes(block + k_switch_offset, &switch_off, sizeof(std::uint8_t));
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
  const std::uintptr_t base = find_module("client.dll").base;
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
