#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "cs2/control/shot_state.hpp"
using namespace cs2::control;
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
template <class T>
void put(void* p, int at, T v) {
  std::memcpy(static_cast<std::byte*>(p) + at, &v, sizeof(v));
}
template <class T>
T get(void* p, int at) {
  T v;
  std::memcpy(&v, static_cast<std::byte*>(p) + at, sizeof(v));
  return v;
}

namespace cs2::control {
struct NativeShotTestAccess {
  static void bind(NativeShotSource& source, float (*spread)(std::uintptr_t),
                   float (*accuracy)(std::uintptr_t, float*, float*),
                   void (*calculate)(std::int16_t, int, int, std::uint32_t, float, float, float,
                                     float*, float*),
                   std::uintptr_t eye) {
    source.spread_ = spread;
    source.inaccuracy_ = accuracy;
    source.calculate_ = calculate;
    source.shoot_position_ = eye;
    source.module_ = {eye - 1, 2};
    source.fields_ready_ = true;
    source.clip_ = 8;
    source.reload_ = 12;
    source.next_ = 16;
    source.tick_ = 8;
    source.services_ = 8;
    source.recoil_ = 0x74;
    source.pellets_ = 8;
    source.type_ = 12;
    source.attributes_ = 0x20;
    source.item_ = 8;
    source.definition_ = 4;
    source.accuracy_begin_ = 0x70;
    source.accuracy_end_ = 0x74;
  }
};
}  // namespace cs2::control
bool invalid_spread = false;
bool invalid_eye = false;
float fake_spread(std::uintptr_t) {
  return invalid_spread ? std::numeric_limits<float>::quiet_NaN() : .01F;
}
float fake_accuracy(std::uintptr_t w, float*, float*) {
  put(reinterpret_cast<void*>(w), 0x70, 99.0F);
  return .02F;
}
void fake_calculate(std::int16_t item, int pellets, int, std::uint32_t, float accuracy,
                    float spread, float, float* x, float* y) {
  check(item == 40 && pellets == 1 && accuracy == .02F && spread == .01F,
        "engine spread ABI inputs");
  *x = .1F;
  *y = .2F;
}
bool external_eye = false;
int wrong_slot_calls = 0;
cs2::game::Vec3* old_slot(std::uintptr_t, cs2::game::Vec3*) {
  ++wrong_slot_calls;
  return nullptr;
}
cs2::game::Vec3* fake_eye(std::uintptr_t, cs2::game::Vec3* eye) {
  static cs2::game::Vec3 returned{3, 4, 64};
  if (external_eye) return &returned;
  *eye = invalid_eye ? cs2::game::Vec3{1000, 1000, 0} : cs2::game::Vec3{1, 2, 64};
  return eye;
}
int main() {
  ShotGate gate;
  ShotState state{11, 22, 1, 100, 100, 100, 5, true, false};
  check(gate.check(state) == ShotBlock::ready, "initial ready weapon");
  gate.commit(state);
  check(gate.check(state) == ShotBlock::duplicate, "committed command deduplicated");
  state.command = 2;
  state.tick_base = 101;
  state.client_tick = 101;
  check(gate.check(state) == ShotBlock::cooldown, "two tick gap");
  state.tick_base = 102;
  state.client_tick = 102;
  state.next_primary = 104;
  check(gate.check(state) == ShotBlock::cooldown, "engine weapon cooldown honored");
  state.next_primary = 102;
  state.reloading = true;
  check(gate.check(state) == ShotBlock::reload, "reload block");
  state.reloading = false;
  state.clip = 0;
  check(gate.check(state) == ShotBlock::empty, "empty block");
  state.clip = 5;
  check(gate.check(state) == ShotBlock::ready, "ready again after reload");
  gate.commit(state);
  state.weapon = 33;
  state.command = 3;
  check(gate.check(state) == ShotBlock::ready, "new weapon has independent state");
  gate.commit(state);
  state.tick_base = 1;
  state.client_tick = 1;
  state.next_primary = 1;
  state.command = 4;
  check(gate.check(state) == ShotBlock::ready, "round clock reset recovers");
  std::array<std::byte, 0x98> cmd{};
  std::array<std::byte, 16> rep{};
  std::array<std::byte, 0x80> entry{};
  put(cmd.data(), 0x30, 1u);
  put(cmd.data(), 0x38, reinterpret_cast<std::uintptr_t>(rep.data()));
  put(rep.data(), 0, 1u);
  put(rep.data(), 8, reinterpret_cast<std::uintptr_t>(entry.data()));
  put(entry.data(), 0x68, 640);
  put(entry.data(), 0x6c, .25F);
  const auto timing = command_timing(cmd.data(), 9.5F);
  check(timing.render_tick == 609 && timing.player_tick == 640,
        "separate target and shooter clocks");
  check(stamp_shot_timing(cmd.data(), timing), "history timing commits");
  check(get<int>(entry.data(), 0x60) == 609 && get<float>(entry.data(), 0x64) == 0 &&
            get<int>(entry.data(), 0x68) == 640 && get<float>(entry.data(), 0x6c) == .25F,
        "target render time and original shooter time retained");
  check((get<unsigned>(entry.data(), 0x10) & 0x1e00) == 0x600,
        "correct generated protobuf timing presence bits");
  auto matched_eye = timing;
  matched_eye.player_valid = true;
  check(stamp_shot_timing(cmd.data(), matched_eye) &&
            (get<unsigned>(entry.data(), 0x10) & 0x1e00) == 0x1e00,
        "explicitly matched eye can stamp player timing");
  auto invalid = timing;
  invalid.render_fraction = std::numeric_limits<float>::quiet_NaN();
  check(!stamp_shot_timing(cmd.data(), invalid) && get<int>(entry.data(), 0x60) == 609,
        "invalid timing rejected before mutation");
  check(!command_timing(cmd.data(), -1).valid, "missing record timing rejected");

  NativeShotSource source;
  NativeShotTestAccess::bind(source, fake_spread, fake_accuracy, fake_calculate,
                             reinterpret_cast<std::uintptr_t>(&fake_eye));
  std::array<std::byte, 0x100> weapon{}, pawn{}, controller{}, data{}, base{};
  std::array<std::uintptr_t, 48> vtable{};
  vtable[29] = reinterpret_cast<std::uintptr_t>(&old_slot);
  vtable[32] = reinterpret_cast<std::uintptr_t>(&fake_eye);
  std::array<std::byte, 0x80> services{};
  put(services.data(), 0, reinterpret_cast<std::uintptr_t>(vtable.data()));
  put(services.data(), 0x38, reinterpret_cast<std::uintptr_t>(pawn.data()));
  put(pawn.data(), 8, reinterpret_cast<std::uintptr_t>(services.data()));
  put(controller.data(), 8, 123);
  put(weapon.data(), 8, 10);
  put(weapon.data(), 16, 122);
  put(weapon.data(), 0x2c, std::uint16_t{40});
  put(weapon.data(), 0x70, 7.0F);
  put(weapon.data(), 0x74, 1.0F);
  put(data.data(), 8, 1);
  put(data.data(), 12, 5);
  put(cmd.data(), 8, 33);
  put(cmd.data(), 0x40, reinterpret_cast<std::uintptr_t>(base.data()));
  put(base.data(), 0x54, 123);
  cs2::game::PlayerSnapshot local;
  local.pawn = reinterpret_cast<std::uintptr_t>(pawn.data());
  local.controller = reinterpret_cast<std::uintptr_t>(controller.data());
  local.weapon_entity = reinterpret_cast<std::uintptr_t>(weapon.data());
  local.weapon_vdata = reinterpret_cast<std::uintptr_t>(data.data());
  local.alive = true;
  local.eye_origin = {1, 2, 64};
  ShotState sampled;
  cs2::game::Vec3 eye;
  std::vector<cs2::aim::KernelSample> kernel;
  check(source.sample(local, cmd.data(), sampled, eye, kernel),
        "command-thread live weapon sample");
  check(wrong_slot_calls == 0 && source.debug().eye_slot == 32,
        "verified getter found independently of stale slot 29");
  external_eye = true;
  check(source.sample(local, cmd.data(), sampled, eye, kernel) && eye.x == 3 && eye.y == 4,
        "shoot-position return pointer is consumed, even when output storage is not filled");
  external_eye = false;
  check(source.sample(local, cmd.data(), sampled, eye, kernel), "normal output getter restored");
  check(sampled.valid && sampled.clip == 10 && sampled.next_primary == 122 &&
            sampled.tick_base == 123 && sampled.client_tick == 123,
        "readiness uses current engine fields");
  check(
      eye.z == 64 && kernel.size() == 32 && kernel[0].offset.x == -.1F && kernel[0].offset.y == .2F,
      "weapon eye and engine samples converted to heatmap basis");
  check(get<float>(weapon.data(), 0x70) == 7.0F, "engine accuracy scratch restored after getter");
  std::array<std::byte, 0x30> historical_position{};
  put(historical_position.data(), 0x18, cs2::game::Vec3{10, 2, 64});
  put(entry.data(), 0x40, reinterpret_cast<std::uintptr_t>(historical_position.data()));
  put(entry.data(), 0x10, 0x820u);
  put(entry.data(), 0x68, 120);
  check(
      source.sample(local, cmd.data(), sampled, eye, kernel) && sampled.history_eye && eye.x == 10,
      "command eye retains its matching player tick source");
  invalid_eye = true;
  check(!source.sample(local, cmd.data(), sampled, eye, kernel),
        "bad shooting-position result rejected");
  const auto debug = source.debug();
  check(debug.schema && debug.weapon && !debug.eye && !debug.spread && debug.eye_call &&
            debug.eye_distance > 128 && debug.returned_eye.x == 1000,
        "debug identifies precise eye stage with raw returned values");
  check(std::strstr(source.diagnostic(), "delta") != nullptr,
        "failed eye check has human-readable detail");
  invalid_eye = false;
  invalid_spread = true;
  check(!source.sample(local, cmd.data(), sampled, eye, kernel) && kernel.empty(),
        "invalid native accuracy cannot become zero spread");
  check(get<float>(weapon.data(), 0x70) == 7.0F, "accuracy scratch restored on failed sample");
  std::puts(
      "PASS: firing cooldown/reload/ammo/identity/reset state; independent record/player history "
      "timing");
}
