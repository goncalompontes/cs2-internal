// Diagnostic DLL: only the opt-in command-count experiment changes game behavior.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <new>
#include <vector>
#include "cs2/control/json.hpp"
#include "cs2/control/move_clock_probe.hpp"
#include "cs2/game/entity_system.hpp"
#include "cs2/game/math.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "cs2/game/pattern.hpp"
#include "cs2/game/schema.hpp"
#include "offsets.hpp"
namespace {
using namespace cs2;
using control::Json;
struct Fields {
  bool ready{};
  std::uint32_t tick{}, pawn_handle{}, controlled_pawn{}, is_local{}, health{}, life{}, flags{},
      velocity{}, movement{}, processed{}, modern{}, land_tick{}, land_frac{}, weapon_services{},
      weapon_handle{}, penalty{}, clip{}, mode{}, vdata{}, spread{};
  bool bind() {
    auto get = [](const char* type, const char* name, std::uint32_t& value) {
      const auto f = game::schema_field(type, name);
      if (!f) return false;
      value = *f;
      return true;
    };
    ready = get("CBasePlayerController", "m_nTickBase", tick) &&
            get("CCSPlayerController", "m_hPlayerPawn", pawn_handle) &&
            get("CBasePlayerController", "m_hPawn", controlled_pawn) &&
            get("CBasePlayerController", "m_bIsLocalPlayerController", is_local) &&
            get("C_BaseEntity", "m_iHealth", health) && get("C_BaseEntity", "m_lifeState", life) &&
            get("C_BaseEntity", "m_fFlags", flags) &&
            get("C_BaseEntity", "m_vecVelocity", velocity) &&
            get("C_BasePlayerPawn", "m_pMovementServices", movement) &&
            get("CPlayer_MovementServices", "m_nLastCommandNumberProcessed", processed) &&
            get("CCSPlayer_MovementServices", "m_ModernJump", modern) &&
            get("CCSPlayerModernJump", "m_nLastLandedTick", land_tick) &&
            get("CCSPlayerModernJump", "m_flLastLandedFrac", land_frac) &&
            get("C_BasePlayerPawn", "m_pWeaponServices", weapon_services) &&
            get("CPlayer_WeaponServices", "m_hActiveWeapon", weapon_handle) &&
            get("C_CSWeaponBase", "m_fAccuracyPenalty", penalty) &&
            get("C_BasePlayerWeapon", "m_iClip1", clip) &&
            get("C_CSWeaponBase", "m_weaponMode", mode) &&
            get("C_BaseEntity", "m_nSubclassID", vdata) &&
            get("CCSWeaponBaseVData", "m_flSpread", spread);
    vdata += 8;
    return ready;
  }
};
using SequenceFn = unsigned (*)(void*);
using AccuracyFn = float (*)(std::uintptr_t, float*, float*);
struct Runtime {
  control::MoveClockProbe probe;
  game::ModuleInfo client{}, engine{};
  Fields fields;
  SequenceFn sequence{};
  AccuracyFn accuracy{};
  std::uintptr_t controller_global{}, entities_global{};
  std::uint32_t ack_offset{};
  std::atomic<int> sample_error{0};
  unsigned long long retry_fields{};
  std::uintptr_t controller_cache{};
};
bool call_sequence(SequenceFn function, std::uintptr_t controller, unsigned* out) noexcept {
  __try {
    *out = function(reinterpret_cast<void*>(controller));
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool call_accuracy(AccuracyFn function, std::uintptr_t weapon, float* total, float* move,
                   float* air) noexcept {
  __try {
    *total = function(weapon, move, air);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
std::uint32_t ack_binding(game::ModuleInfo engine) {
  const auto site =
      game::unique_code_match(engine, "448B8B78352C004C8D05????????8B0D????????BA02000000");
  std::uint32_t offset = 0;
  std::int32_t relative = 0;
  if (!site || !game::read(site + 3, offset) || !game::read(site + 10, relative)) return 0;
  const auto text = site + 14 + relative;
  if (!engine.contains(text) || offset > 0x400000 ||
      !game::read_cstring(text).starts_with(
          ":    processpacketentities: server executed through command"))
    return 0;
  return offset;
}
control::MoveClockSnapshot sample(void* context, void* owner) noexcept {
  auto& r = *static_cast<Runtime*>(context);
  control::MoveClockSnapshot out;
  out.time_ms = GetTickCount64();
  auto fail = [&r, &out](int code) {
    r.sample_error.store(code);
    return out;
  };
  if (!r.fields.ready) {
    if (out.time_ms < r.retry_fields) return fail(1);
    r.retry_fields = out.time_ms + 1000;
    if (!r.fields.bind()) return fail(1);
  }
  const auto& f = r.fields;
  std::uintptr_t controller = 0, blocks = 0, services = 0, weapons = 0, data = 0;
  std::uint32_t pawn_handle = 0, weapon_handle = 0;
  int health = 0, mode = 0;
  std::uint8_t life = 1;
  if (!game::read(r.entities_global, blocks) || !blocks) return fail(2);
  (void)game::read(r.controller_global, controller);
  if (r.controller_cache) controller = r.controller_cache;
  const game::EntitySystem entities(blocks);
  std::uint8_t is_local = 0;
  if (controller) (void)game::read(controller + f.is_local, is_local);
  if (!is_local) {
    r.controller_cache = 0;
    for (int index = 1; index <= 64; ++index) {
      if (entities.designer_name(index) != "cs_player_controller") continue;
      const auto candidate = entities.instance(index);
      if (candidate && game::read(candidate + f.is_local, is_local) && is_local) {
        controller = candidate;
        break;
      }
    }
  }
  if (!controller || !is_local || !game::read(controller + f.pawn_handle, pawn_handle)) {
    out.controller = controller;
    return fail(2);
  }
  r.controller_cache = controller;
  out.controller = controller;
  auto inspect_actor = [&](std::uint32_t handle) {
    out.pawn_handle = handle;
    out.pawn = entities.resolve(handle);
    out.actor_reads = 0;
    out.health = 0;
    out.life_state = -1;
    if (!out.pawn) return false;
    if (game::read(out.pawn + f.health, health)) {
      out.health = health;
      out.actor_reads |= 1;
    }
    if (game::read(out.pawn + f.life, life)) {
      out.life_state = life;
      out.actor_reads |= 2;
    }
    return out.actor_reads == 3 && health > 0 && life == 0;
  };
  if (!inspect_actor(pawn_handle)) {
    std::uint32_t controlled = 0;
    if (!game::read(controller + f.controlled_pawn, controlled) || controlled == pawn_handle ||
        !inspect_actor(controlled))
      return fail(3);
  }
  if (!game::read(controller + f.tick, out.tick_base) ||
      !game::read(out.pawn + f.movement, services) || !services ||
      !game::read(services + f.processed, out.last_processed) ||
      !game::read(services + f.modern + f.land_tick, out.landed_tick) ||
      !game::read(services + f.modern + f.land_frac, out.landed_fraction) ||
      !game::read(out.pawn + f.flags, out.flags))
    return fail(4);
  game::Vec3 velocity{};
  if (!game::read(out.pawn + f.velocity, velocity)) return fail(4);
  out.velocity_x = velocity.x;
  out.velocity_y = velocity.y;
  out.velocity_z = velocity.z;
  if (!game::read(out.pawn + f.weapon_services, weapons) || !weapons ||
      !game::read(weapons + f.weapon_handle, weapon_handle))
    return fail(5);
  out.weapon = entities.resolve(weapon_handle);
  if (!out.weapon || !game::read(out.weapon + f.penalty, out.accuracy_penalty) ||
      !game::read(out.weapon + f.clip, out.clip) || !game::read(out.weapon + f.mode, mode) ||
      mode < 0 || mode > 1 || !game::read(out.weapon + f.vdata, data) || !data ||
      !game::read(data + f.spread + 4 * mode, out.spread))
    return fail(5);
  unsigned sequence = 0;
  if (!call_sequence(r.sequence, controller, &sequence)) return fail(6);
  out.command = static_cast<int>(sequence);
  if (!call_accuracy(r.accuracy, out.weapon, &out.inaccuracy, &out.movement_inaccuracy,
                     &out.air_inaccuracy) ||
      !std::isfinite(out.inaccuracy) || !std::isfinite(out.accuracy_penalty) ||
      !std::isfinite(out.spread) || !std::isfinite(out.movement_inaccuracy) ||
      !std::isfinite(out.air_inaccuracy))
    return fail(7);
  std::uintptr_t network = 0, table = 0, col = 0;
  std::uint32_t type_rva = 0;
  if (!game::read(reinterpret_cast<std::uintptr_t>(owner) + 0x130, network) || !network ||
      !game::read(network, table) || !r.engine.contains(table) || !game::read(table - 8, col) ||
      !r.engine.contains(col) || !game::read(col + 12, type_rva) ||
      !r.engine.contains(r.engine.base + type_rva + 16) ||
      game::read_cstring(r.engine.base + type_rva + 16) != ".?AVCNetworkGameClient@@" ||
      !game::read(network + r.ack_offset, out.acknowledged_command) ||
      !game::read(network + 0xb0, out.network_time))
    return fail(8);
  out.valid = out.command > 0 && out.tick_base > 0 && out.acknowledged_command >= 0 && out.clip > 0;
  r.sample_error.store(out.valid ? 0 : 9);
  return out;
}
std::string runtime_type(std::uintptr_t object, game::ModuleInfo module) {
  std::uintptr_t table = 0, col = 0;
  std::uint32_t type = 0;
  if (!object || !game::read(object, table) || !module.contains(table) ||
      !game::read(table - 8, col) || !module.contains(col) || !game::read(col + 12, type) ||
      !module.contains(module.base + type + 16))
    return "unavailable";
  return game::read_cstring(module.base + type + 16, 128);
}
Json snapshot(const control::MoveClockSnapshot& s) {
  Json j = Json::object();
  j.set("controller", Json::number(static_cast<double>(s.controller)));
  j.set("health", Json::number(s.health));
  j.set("life_state", Json::number(s.life_state));
  j.set("pawn_handle", Json::number(s.pawn_handle));
  j.set("actor_reads", Json::number(s.actor_reads));
  j.set("valid", Json::boolean(s.valid));
  j.set("time_ms", Json::number(static_cast<double>(s.time_ms)));
  j.set("command", Json::number(s.command));
  j.set("tickbase", Json::number(s.tick_base));
  j.set("processed", Json::number(s.last_processed));
  j.set("server_executed", Json::number(s.acknowledged_command));
  j.set("pawn", Json::number(static_cast<double>(s.pawn)));
  j.set("weapon", Json::number(static_cast<double>(s.weapon)));
  j.set("flags", Json::number(s.flags));
  j.set("landed_tick", Json::number(s.landed_tick));
  j.set("landed_fraction", Json::number(s.landed_fraction));
  j.set("penalty", Json::number(s.accuracy_penalty));
  j.set("inaccuracy", Json::number(s.inaccuracy));
  j.set("move_component", Json::number(s.movement_inaccuracy));
  j.set("air_component", Json::number(s.air_inaccuracy));
  j.set("spread", Json::number(s.spread));
  j.set("clip", Json::number(s.clip));
  j.set("vx", Json::number(s.velocity_x));
  j.set("vy", Json::number(s.velocity_y));
  j.set("vz", Json::number(s.velocity_z));
  j.set("network_time", Json::number(s.network_time));
  return j;
}
Json record(const control::MoveClockReport& r) {
  Json j = Json::object();
  j.set("calls", Json::number(static_cast<double>(r.calls)));
  j.set("adjusted", Json::number(static_cast<double>(r.adjusted)));
  j.set("extra", Json::number(r.applied_extra));
  j.set("count_before", Json::number(r.original_count));
  j.set("count_after", Json::number(r.forwarded_count));
  j.set("before", snapshot(r.before));
  j.set("after", snapshot(r.after));
  return j;
}
void write_json(const std::filesystem::path& path, const Json& value) {
  auto temporary = path;
  temporary += L".tmp";
  {
    std::ofstream stream(temporary);
    stream << value.dump() << '\n';
  }
  MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
unsigned long WINAPI worker(void* module) {
  wchar_t module_path[32768]{};
  if (!GetModuleFileNameW(static_cast<HMODULE>(module), module_path, 32768)) return 0;
  const auto folder = std::filesystem::path(module_path).parent_path();
  const auto status_path = folder / L"move-clock-live-status.json";
  Json config = Json::object();
  {
    std::ifstream stream(folder / L"move-clock-live-config.json");
    std::string text((std::istreambuf_iterator<char>(stream)), {});
    const auto parsed = Json::parse(text);
    if (parsed && parsed->is_object()) config = *parsed;
  }
  std::string nonce;
  if (const auto* n = config.find("nonce"); n && n->is_string())
    nonce = std::string(n->as_string());
  Runtime* r = nullptr;
  auto status = [&](const char* state, const char* detail, int sample_error = 0) {
    Json value = Json::object();
    value.set("state", Json::string(state));
    value.set("detail", Json::string(detail));
    value.set("nonce", Json::string(nonce));
    value.set("pid", Json::number(GetCurrentProcessId()));
    value.set("sample_error", Json::number(sample_error));
    if (r) {
      const auto actor = r->probe.report().after;
      value.set("health", Json::number(actor.health));
      value.set("life_state", Json::number(actor.life_state));
      value.set("actor_reads", Json::number(actor.actor_reads));
      value.set("pawn_type", Json::string(runtime_type(actor.pawn, r->client)));
    }
    write_json(status_path, value);
  };
  if (nonce.empty()) {
    status("error", "missing explicit probe configuration");
    return 0;
  }
  status("preparing", "resolving verified command-tick, accuracy and server-executed bindings");
  // Retain runtime/trampoline storage until CS2 exits; only the hook is disabled.
  r = new (std::nothrow) Runtime;
  if (!r) {
    status("error", "runtime allocation failed");
    return 0;
  }
  r->client = game::find_module("client.dll");
  r->engine = game::find_module("engine2.dll");
  const auto target = control::resolve_command_tick(r->engine);
  r->ack_offset = ack_binding(r->engine);
  const auto seq = game::unique_code_match(r->client, "4883EC28E8????????8B8010590000");
  const auto accuracy =
      game::unique_code_match(r->client, "48895C24??5556574881EC????????440F298424");
  if (!target || !r->ack_offset || !seq || !accuracy) {
    status("error", "binding not uniquely verified");
    return 0;
  }
  r->sequence = reinterpret_cast<SequenceFn>(seq);
  r->accuracy = reinterpret_cast<AccuracyFn>(accuracy);
  r->controller_global = r->client.base + sdk::LOCAL_PLAYER_CONTROLLERS_RVA;
  r->entities_global = r->client.base + sdk::ENTITY_LIST_RVA;
  if (!r->probe.install_live(reinterpret_cast<control::CommandTickFn>(*target), &sample, r)) {
    status("error", "live MinHook installation failed");
    return 0;
  }
  status("observing", "collecting normal calls; no extra iterations armed");
  Json samples = Json::array();
  auto begin = GetTickCount64(), armed_at = 0ULL, applied_at = 0ULL;
  std::uint64_t seen = 0;
  while (GetTickCount64() - begin < 90000) {
    const auto now = GetTickCount64();
    const auto last = r->probe.report();
    if (last.calls != seen) {
      seen = last.calls;
      samples.push_back(record(last));
    }
    if (!armed_at && now - begin >= 2000 && !last.after.valid && last.calls && now - begin < 2100)
      status("waiting_for_player", "equip a loaded gun while controlling a living player",
             r->sample_error.load());
    if (!armed_at && now - begin >= 2000 && last.after.valid) {
      if (r->probe.request_extra(1, true)) {
        armed_at = now;
        status("armed", "one extra iteration on the next new grounded landing");
      }
    }
    const auto experiment = r->probe.last_adjustment();
    if (!applied_at && experiment.applied_extra) {
      applied_at = now;
      status("applied", "one-shot completed; observing processed commands and server execution");
    }
    if (applied_at && now - applied_at >= 5000) break;
    if (!armed_at && now - begin >= 45000) break;
    if (armed_at && !applied_at && now - armed_at >= 30000) break;
    Sleep(10);
  }
  const bool disabled = r->probe.disable_live();
  Sleep(50);
  Json result = Json::object();
  result.set("nonce", Json::string(nonce));
  result.set("pid", Json::number(GetCurrentProcessId()));
  result.set("synthetic", Json::boolean(false));
  result.set("extra_requested", Json::number(1));
  result.set("hook_disabled", Json::boolean(disabled));
  result.set("sample_error", Json::number(r->sample_error.load()));
  result.set("ack_member_offset", Json::number(r->ack_offset));
  Json field_offsets = Json::object();
  field_offsets.set("health", Json::number(r->fields.health));
  field_offsets.set("life", Json::number(r->fields.life));
  field_offsets.set("player_pawn", Json::number(r->fields.pawn_handle));
  field_offsets.set("controlled_pawn", Json::number(r->fields.controlled_pawn));
  field_offsets.set("is_local", Json::number(r->fields.is_local));
  result.set("field_offsets", std::move(field_offsets));
  const auto last_state = r->probe.report().after;
  result.set("controller_type", Json::string(runtime_type(last_state.controller, r->client)));
  result.set("pawn_type", Json::string(runtime_type(last_state.pawn, r->client)));
  result.set("experiment", record(r->probe.last_adjustment()));
  result.set("final", record(r->probe.report()));
  result.set("samples", std::move(samples));
  write_json(folder / L"move-clock-live-result.json", result);
  if (applied_at)
    status("completed",
           disabled ? "live result saved; hook disabled" : "result saved; hook-disable failed");
  else
    status("incomplete",
           armed_at ? "no new grounded landing before timeout"
                    : "valid living-player/weapon snapshot unavailable",
           r->sample_error.load());
  return 0;
}
}  // namespace
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    if (auto thread = CreateThread(nullptr, 0, &worker, module, 0, nullptr)) CloseHandle(thread);
  }
  return TRUE;
}
