#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2/control/command_choke.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include "MinHook.h"
#include "cs2/game/memory.hpp"
#include "cs2/game/pattern.hpp"
#include "cs2/tls/bootstrap.hpp"
namespace cs2::control {
bool RecoveryChokePolicy::defer(const ChokeSample& s, std::uint64_t now, bool enabled) noexcept {
  const bool fresh = now >= s.time_ms && now - s.time_ms < 100;
  if (!enabled || !s.valid || !s.grounded || s.attack || !s.pawn || !s.weapon || s.clip <= 0 ||
      s.command <= 0 || !std::isfinite(s.penalty) || s.penalty < 0 ||
      !std::isfinite(s.horizontal_speed) || s.horizontal_speed < 0 || !fresh ||
      (held_ && (now < started_ || now - started_ >= 100))) {
    previous_ = {};
    held_ = 0;
    last_deferred_ = false;
    return false;
  }
  if (s.command == previous_.command && s.pawn == previous_.pawn && s.weapon == previous_.weapon &&
      s.clip == previous_.clip)
    return last_deferred_;
  const bool continuous = previous_.valid && s.pawn == previous_.pawn &&
                          s.weapon == previous_.weapon && s.clip == previous_.clip &&
                          static_cast<std::int64_t>(s.command) - previous_.command == 1;
  const bool recovering = continuous && s.penalty + 0.000001F < previous_.penalty;
  const bool stationary = continuous && s.horizontal_speed <= stationary_speed;
  previous_ = s;
  if ((!recovering && !stationary) || held_ == 4) {
    held_ = 0;
    last_deferred_ = false;
    return false;
  }
  if (!held_) started_ = now;
  ++held_;
  last_deferred_ = true;
  return true;
}
std::optional<CommandSendBinding> resolve_command_send(game::ModuleInfo module) noexcept {
  // Verified current-engine builder and caller. Reject a changed layout rather
  // than borrowing a Source 1 send_packet flag or trusting a bare RVA.
  const auto build = game::unique_code_match(
      module, "4055534156488D6C24F04881EC100100004C8BF1C7442420FFFFFFFF33C04533C9");
  const auto stage = game::unique_code_match(
      module, "48895C2408574883EC50488BD9E8????????488BBBF00000004885FF741C");
  if (!build || !stage || !module.contains(stage + 0x45) || !module.contains(build + 0x479))
    return {};
  std::int32_t relative = 0;
  if (!game::read(stage + 0xe, relative) || stage + 0x12 + relative != build) return {};
  unsigned char call = 0;
  if (!game::read(stage + 0x40, call) || call != 0xe8 || !game::read(stage + 0x41, relative))
    return {};
  const auto send = stage + 0x45 + relative;
  if (!module.contains(send) || !module.contains(send + 0x205)) return {};
  // The sender's prologue, ClientSendMove label and call of transport slot 0x158.
  const auto expected =
      game::unique_code_match(module, "48895C241055565741564157488D6C24C94881ECD0000000");
  std::int32_t label_relative = 0;
  if (send != expected || !game::read(send + 0x1f8, label_relative) ||
      game::read_cstring(send + 0x1fc + label_relative) != "ClientSendMove")
    return {};
  std::array<unsigned char, 6> transport{};
  if (!game::read_bytes(send + 0x1ff, transport.data(), transport.size()) ||
      transport != std::array<unsigned char, 6>{0xff, 0x90, 0x58, 0x01, 0, 0})
    return {};
  std::array<unsigned char, 14> stores{};
  if (!game::read_bytes(build + 0x436, stores.data(), stores.size()) ||
      stores != std::array<unsigned char, 14>{0x41, 0x89, 0xbe, 0x90, 0xcf, 0x2c, 0, 0x41, 0x89,
                                              0xb6, 0x94, 0xcf, 0x2c, 0})
    return {};
  // Builder derives its retained command range from current command minus the
  // engine's tracking member, then records the serialized start/end range.
  const auto range = game::unique_code_match(module, "488B0D????????8BDE8B5530412B9E7C352C00");
  if (range != build + 0x1d9) return {};
  return CommandSendBinding{build, stage, send};
}
struct RecoveryCommandChoke::Runtime {
  using BuildFn = void (*)(void*);
  static std::atomic<Runtime*> owner;
  std::atomic<BuildFn> original{nullptr};
  std::atomic<bool> enabled{false}, installed{false};
  std::mutex lifecycle, mutex;
  ChokeSample sample{};
  RecoveryChokePolicy policy;
  game::CommandChokeDebug diagnostic{};
  CommandSendBinding binding{};
};
std::atomic<RecoveryCommandChoke::Runtime*> RecoveryCommandChoke::Runtime::owner{nullptr};
RecoveryCommandChoke::RecoveryCommandChoke() : runtime_(new Runtime) {}
RecoveryCommandChoke::~RecoveryCommandChoke() {
  stop();
}
bool RecoveryCommandChoke::enabled() const noexcept {
  return runtime_->enabled.load();
}
bool RecoveryCommandChoke::configure(bool enabled) noexcept {
  auto& r = *runtime_;
  std::lock_guard lifecycle(r.lifecycle);
  if (enabled && r.enabled.load() && r.installed.load()) return true;
  r.enabled.store(false);
  if (!enabled) {
    std::lock_guard lock(r.mutex);
    r.sample = {};
    r.policy = {};
    r.diagnostic.held = 0;
    r.diagnostic.deferred = false;
    return true;
  }
  if (!r.installed.load()) {
    const auto binding = resolve_command_send(game::find_module("engine2.dll"));
    if (!binding) return false;
    Runtime* empty = nullptr;
    if (!Runtime::owner.compare_exchange_strong(empty, &r) && empty != &r) return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    r.binding = *binding;
    void* original = nullptr;
    void* target = reinterpret_cast<void*>(binding->build);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&detour), &original) != MH_OK) return false;
    r.original.store(reinterpret_cast<Runtime::BuildFn>(original));
    if (MH_EnableHook(target) != MH_OK) {
      MH_RemoveHook(target);
      r.original.store(nullptr);
      return false;
    }
    r.installed.store(true);
  }
  r.enabled.store(true);
  return true;
}
void RecoveryCommandChoke::observe(ChokeSample sample) noexcept {
  std::lock_guard lock(runtime_->mutex);
  runtime_->sample = sample;
}
game::CommandChokeDebug RecoveryCommandChoke::debug() const noexcept {
  std::lock_guard lock(runtime_->mutex);
  auto out = runtime_->diagnostic;
  out.enabled = runtime_->enabled.load();
  out.bound = runtime_->installed.load();
  return out;
}
void RecoveryCommandChoke::stop() noexcept {
  auto& r = *runtime_;
  std::lock_guard lifecycle(r.lifecycle);
  r.enabled.store(false);
  if (r.installed.load()) {
    const auto status = MH_DisableHook(reinterpret_cast<void*>(r.binding.build));
    if (status == MH_OK || status == MH_ERROR_DISABLED) r.installed.store(false);
  }
  // Runtime and trampoline intentionally remain resident for callbacks in flight.
}
void RecoveryCommandChoke::detour(void* self) noexcept {
  tls::ensure_thread_tls();
  auto* r = Runtime::owner.load();
  if (!r) return;
  const auto original = r->original.load();
  if (!original) return;
  // Do not hold the policy lock while invoking the engine. A recursive call
  // fails open and cannot consume the same command a second time.
  static thread_local bool in_call = false;
  if (in_call) {
    original(self);
    return;
  }
  in_call = true;
  bool defer = false;
  bool releasing = false;
  int release_command = 0;
  {
    std::lock_guard lock(r->mutex);
    auto s = r->sample;
    const auto object = reinterpret_cast<std::uintptr_t>(self);
    std::uintptr_t table = 0, stage = 0, channel = 0;
    int signon = 0, command = 0;
    std::uint64_t buttons = 0;
    const bool connected = game::read(object, table) && game::read(table + 0x288, stage) &&
                           stage == r->binding.stage && game::read(object + 0xf0, channel) &&
                           channel && game::read(object + 0x230, signon) && signon == 6;
    // Re-read after Input applied automatic attack and finalized the CRC.
    if (!game::read(s.command_pointer + 8, command) || command != s.command ||
        !game::read(s.command_pointer + 0x60, buttons))
      s.valid = false;
    s.attack = (buttons & ((1ull << 0) | (1ull << 11))) != 0;
    releasing = r->policy.held() != 0;
    defer = r->policy.defer(s, GetTickCount64(), connected && r->enabled.load());
    auto& d = r->diagnostic;
    ++d.calls;
    if (defer && d.command != s.command) ++d.deferred_commands;
    d.command = s.command;
    d.held = r->policy.held();
    d.deferred = defer;
    d.penalty = s.penalty;
    d.horizontal_speed = s.horizontal_speed;
    d.sample_ms = s.time_ms;
    d.network = object;
    (void)game::read(object + 0x2c3578, d.server_executed);
    if (releasing && !defer) {
      ++d.release_attempts;
      release_command = s.command;
    }
  }
  if (!defer) {
    original(self);
    std::lock_guard lock(r->mutex);
    const auto object = reinterpret_cast<std::uintptr_t>(self);
    (void)game::read(object + 0x2ccf90, r->diagnostic.range_first);
    (void)game::read(object + 0x2ccf94, r->diagnostic.range_last);
    if (releasing) {
      r->diagnostic.release_first = r->diagnostic.range_first;
      r->diagnostic.release_last = r->diagnostic.range_last;
      r->diagnostic.release_command = release_command;
    }
  }
  in_call = false;
}
}  // namespace cs2::control
