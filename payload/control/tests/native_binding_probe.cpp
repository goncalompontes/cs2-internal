// Maps PE data for signature verification only; no client code is executed.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include "MinHook.h"
#include "cs2/control/movement_trace.hpp"
#include "cs2/control/native_command.hpp"
#include "cs2/control/shot_state.hpp"
#include "cs2/features/camera.hpp"
#include "cs2/loader/mapping.hpp"
#include "cs2/loader/pe.hpp"

// Static schema descriptors carry class names, field tables and declared offsets.
// Resolve them from relocated PE data, without calling SchemaSystem or the client.
std::optional<std::uint32_t> static_field(const std::byte* bytes, std::size_t size,
                                          const char* type, const char* name) {
  const auto begin = reinterpret_cast<std::uintptr_t>(bytes);
  auto contains = [&](std::uintptr_t p, std::size_t n) {
    return p >= begin && p - begin <= size && n <= size - (p - begin);
  };
  std::optional<std::uint32_t> result;
  const auto len = std::strlen(type) + 1;
  for (std::size_t at = 0; at + len <= size; ++at) {
    if (std::memcmp(bytes + at, type, len) != 0) continue;
    const auto address = begin + at;
    for (std::size_t ref = 0; ref + 48 <= size; ref += 8) {
      std::uintptr_t candidate = 0;
      std::memcpy(&candidate, bytes + ref, 8);
      if (candidate != address) continue;
      std::uint16_t count = 0;
      std::uintptr_t fields = 0;
      std::memcpy(&count, bytes + ref + 28, 2);
      std::memcpy(&fields, bytes + ref + 40, 8);
      if (count == 0 || count > 8192 || !contains(fields, static_cast<std::size_t>(count) * 32))
        continue;
      for (unsigned i = 0; i < count; ++i) {
        const auto* entry = reinterpret_cast<const std::byte*>(fields) + i * 32;
        std::uintptr_t field_name = 0;
        std::uint32_t value = 0;
        std::memcpy(&field_name, entry, 8);
        std::memcpy(&value, entry + 16, 4);
        if (!contains(field_name, std::strlen(name) + 1) ||
            std::memcmp(reinterpret_cast<void*>(field_name), name, std::strlen(name) + 1) != 0)
          continue;
        if (value >= 0x100000) return {};
        if (result && *result != value) return {};
        result = value;
      }
    }
  }
  return result;
}

std::optional<std::uintptr_t> static_vtable(const std::byte* bytes, std::size_t size,
                                            const char* name) {
  const auto base = reinterpret_cast<std::uintptr_t>(bytes);
  const auto len = std::strlen(name) + 1;
  for (std::size_t text = 16; text + len <= size; ++text) {
    if (std::memcmp(bytes + text, name, len)) continue;
    const auto type = static_cast<std::uint32_t>(text - 16);
    for (std::size_t col = 0; col + 24 <= size; col += 4) {
      std::uint32_t record[6];
      std::memcpy(record, bytes + col, 24);
      if (record[0] != 1 || record[1] || record[2] || record[3] != type || record[5] != col)
        continue;
      const auto locator = base + col;
      for (std::size_t ref = 0; ref + 16 <= size; ref += 8) {
        std::uintptr_t p = 0;
        std::memcpy(&p, bytes + ref, 8);
        if (p == locator) return base + ref + 8;
      }
    }
  }
  return {};
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) return 2;
  const auto count = file.tellg();
  file.seekg(0);
  std::vector<std::byte> raw(static_cast<std::size_t>(count));
  file.read(reinterpret_cast<char*>(raw.data()), count);
  auto image = cs2::loader::pe::parse_image(raw);
  if (!image) {
    std::puts("PE parse failed");
    return 1;
  }
  void* memory =
      VirtualAlloc(nullptr, image->size_of_image, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory) return 1;
  cs2::loader::mapping::BufferSink sink(reinterpret_cast<std::uintptr_t>(memory),
                                        image->size_of_image);
  const auto mapped = cs2::loader::mapping::map_image(*image, raw, sink);
  if (!mapped) {
    std::puts("PE mapping failed");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 1;
  }
  std::memcpy(memory, sink.bytes().data(), sink.bytes().size());
  cs2::control::NativeCommandBridge bridge;
  bool bound = bridge.initialize({reinterpret_cast<std::uintptr_t>(memory), image->size_of_image});
  cs2::control::MovementTrace movement;
  bound =
      movement.prepare({reinterpret_cast<std::uintptr_t>(memory), image->size_of_image}) && bound;
  cs2::features::CameraFeatures camera;
  bound = camera.prepare({reinterpret_cast<std::uintptr_t>(memory), image->size_of_image}) && bound;
  cs2::control::NativeShotSource shot;
  bound = shot.prepare({reinterpret_cast<std::uintptr_t>(memory), image->size_of_image}) && bound;

  const auto* bytes = static_cast<const std::byte*>(memory);
  const std::pair<const char*, const char*> required[] = {
      {"C_BasePlayerWeapon", "m_iClip1"},
      {"C_CSWeaponBase", "m_bInReload"},
      {"C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"},
      {"CBasePlayerController", "m_nTickBase"},
      {"C_BasePlayerPawn", "m_pWeaponServices"},
      {"C_BasePlayerPawn", "m_pMovementServices"},
      {"C_BaseEntity", "m_vecAbsVelocity"},
      {"C_BaseEntity", "m_vecVelocity"},
      {"CCSPlayerModernJump", "m_nLastActualJumpPressTick"},
      {"CCSPlayerModernJump", "m_flLastActualJumpPressFrac"},
      {"CCSPlayer_MovementServices", "m_flStamina"},
      {"CCSPlayer_MovementServices", "m_flDuckAmount"},
      {"C_BaseEntity", "m_pGameSceneNode"},
      {"CGameSceneNode", "m_vecAbsOrigin"},
      {"C_BaseModelEntity", "m_Collision"},
      {"CCollisionProperty", "m_vecMins"},
      {"CCollisionProperty", "m_vecMaxs"},
      {"CCollisionProperty", "m_collisionAttribute"},
      {"VPhysicsCollisionAttribute_t", "m_nInteractsAs"},
      {"C_BaseEntity", "m_flGravityScale"},
      {"C_BaseEntity", "m_nActualMoveType"},
      {"CPlayer_MovementServices", "m_nLastCommandNumberProcessed"},
      {"CPlayer_MovementServices", "m_nButtons"},
      {"CPlayer_MovementServices", "m_flMaxspeed"},
      {"CPlayer_MovementServices", "m_flCmdForwardMove"},
      {"CPlayer_MovementServices", "m_flCmdLeftMove"},
      {"CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"},
      {"CCSPlayer_MovementServices", "m_ModernJump"},
      {"CCSPlayerModernJump", "m_nLastLandedTick"},
      {"CCSPlayerModernJump", "m_flLastLandedFrac"},
      {"C_CSWeaponBase", "m_flRecoilIndex"},
      {"CCSWeaponBaseVData", "m_nNumBullets"},
      {"C_EconEntity", "m_AttributeManager"},
      {"C_AttributeContainer", "m_Item"},
      {"C_EconItemView", "m_iItemDefinitionIndex"},
      {"CCSWeaponBaseVData", "m_WeaponType"},
      {"C_CSWeaponBase", "m_flTurningInaccuracyDelta"},
      {"C_BaseEntity", "m_flSimulationTime"},
      {"C_CSPlayerPawn", "m_bGunGameImmunity"}};
  for (const auto& [type, name] : required)
    if (!static_field(bytes, image->size_of_image, type, name)) {
      std::printf("FAIL: required schema field %s::%s missing\n", type, name);
      bound = false;
    }
  const auto spread = static_field(bytes, image->size_of_image, "CCSWeaponBaseVData", "m_flSpread");
  const auto mode = static_field(bytes, image->size_of_image, "C_CSWeaponBase", "m_weaponMode");
  if (!spread || !mode || !shot.schema_bindings_match(spread.value_or(0), mode.value_or(0))) {
    std::puts("FAIL: spread getter does not read the schema-declared weapon spread/mode fields");
    bound = false;
  }
  if (spread && mode && shot.schema_bindings_match(*spread - 8, *mode)) {
    std::puts("FAIL: neighboring VData field falsely accepted as spread");
    bound = false;
  }
  const auto services =
      static_vtable(bytes, image->size_of_image, ".?AVCCSPlayer_WeaponServices@@");
  const auto eye_slot = services ? shot.shooting_position_slot(*services) : std::nullopt;
  if (!eye_slot || *eye_slot == 29) {
    std::puts("FAIL: verified shooting-position getter not found in current weapon-service vtable");
    bound = false;
  } else
    std::printf("PASS: weapon-service vector getter resolved to slot %u (stale slot 29 excluded)\n",
                *eye_slot);
  if (bound) {
    void* target = bridge.view_handler();
    std::array<std::byte, 32> before{};
    std::memcpy(before.data(), target, before.size());
    DWORD prior = 0;
    const bool executable =
        VirtualProtect(memory, image->size_of_image, PAGE_EXECUTE_READWRITE, &prior) != 0;
    const auto initialized = MH_Initialize();
    void* trampoline = nullptr;
    const bool created =
        executable && (initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) &&
        MH_CreateHook(target, reinterpret_cast<void*>(&main), &trampoline) == MH_OK;
    const bool enabled = created && MH_EnableHook(target) == MH_OK;
    const bool restored = enabled && MH_DisableHook(target) == MH_OK;
    if (created) MH_RemoveHook(target);
    const bool valid = restored && std::memcmp(before.data(), target, before.size()) == 0;
    std::printf("%s: inline view handler RVA 0x%llx patch/restore; no client function called\n",
                valid ? "PASS" : "FAIL",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(target) -
                                                reinterpret_cast<std::uintptr_t>(memory)));
    bound = bound && valid;
  }
  std::puts(bound ? "PASS: all native command/view/CRC camera/scope and weapon accuracy/schema "
                    "bindings verified against installed client bytes; no engine code executed"
                  : "FAIL: installed client signatures did not bind");
  VirtualFree(memory, 0, MEM_RELEASE);
  return bound ? 0 : 1;
}
