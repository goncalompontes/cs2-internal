#include "cs2/game/rage_debug.hpp"
#include <cstdio>
namespace cs2::game {
const char* rage_debug_stage_name(RageDebugStage s) noexcept {
  constexpr const char* names[] = {
      "Command callback",   "Local player",      "Accuracy bindings",    "Live schema",
      "Weapon fields/type", "Shooting position", "Spread samples",       "Ballistics backend",
      "Target geometry",    "Aim proposals",     "Damage / penetration", "Damage probability",
      "Record timing",      "Shot trigger",      "Command serialization"};
  const auto index = static_cast<std::size_t>(s);
  return index < k_rage_debug_stages ? names[index] : "Unknown";
}
const char* rage_debug_gate_name(RageDebugGate s) noexcept {
  switch (s) {
    case RageDebugGate::pass:
      return "PASS";
    case RageDebugGate::fail:
      return "BLOCKED";
    case RageDebugGate::waiting:
      return "WAIT";
    default:
      return "--";
  }
}
const char* movement_debug_reason(int reason) noexcept {
  constexpr const char* names[] = {"off / not processed",
                                   "ready",
                                   "grounded",
                                   "movement schema unavailable",
                                   "local movement state unavailable",
                                   "invalid physics / velocity",
                                   "not walking (ladder/noclip/etc)",
                                   "jump not held",
                                   "no step slots",
                                   "subtick budget full",
                                   "step allocation failed",
                                   "waiting for landing",
                                   "rage autostop owns movement",
                                   "native control (no input / low speed)",
                                   "manual direction has priority",
                                   "physical event reaches tick end",
                                   "strafe strength is zero",
                                   "command already simulated (no prestate)"};
  return reason >= 0 && reason < 18 ? names[reason] : "unknown";
}
std::string format_rage_debug(const RageDebugSnapshot& d) {
  char b[4096];
  std::snprintf(
      b, sizeof(b),
      "Ragebot debug | enabled=%d auto_fire=%d hook=%d\nDetail: %s\n"
      "Command %d client=%d base=%d next_primary=%d ammo=%d snapshot_ammo=%d reload=%d\n"
      "Callbacks=%llu unique=%llu duplicate=%llu CreateMove=%llu captured_ms=%llu "
      "observer_ms=%llu\n"
      "Weapon=%s type=%d item=%d pellets=%d samples=%d spread=%.6f inaccuracy=%.6f recoil=%.3f\n"
      "pawn=0x%llx weapon=0x%llx vdata=0x%llx services=0x%llx eye_fn=0x%llx slot=%d in_client=%d "
      "call_ok=%d\n"
      "Local eye=(%.2f %.2f %.2f) returned=(%.2f %.2f %.2f) distance=%.2f history_eye=%d\n"
      "Players=%d enemies=%d valid_targets=%d proposals=%d examined=%d centers=%d\n"
      "Rejected: immunity=%d time=%d model=%d changed_pose=%d\n"
      "Target=%d group=%d nominal=%.2f expected=%.2f minimum=%.2f probability=%.1f%% "
      "required=%.1f%% world_known=%d\n"
      "Aim_ready=%d stop_requested=%d trigger=%s shot_requests=%llu observed_ammo_decreases=%llu\n"
      "Worker submitted=%llu completed=%llu last_us=%llu | CRC ok=%llu failed=%llu "
      "camera_restores=%llu\n",
      d.enabled, d.auto_fire, d.hook, d.detail, d.command, d.client_tick, d.tick_base,
      d.next_primary, d.clip, d.snapshot_clip, d.reload, d.callbacks, d.unique_commands,
      d.duplicates, d.calls5, d.captured_ms, d.observer_ms, d.weapon_name, d.source.type,
      d.source.item, d.source.pellets, d.source.samples, d.source.spread_value, d.source.inaccuracy,
      d.source.recoil, static_cast<unsigned long long>(d.pawn),
      static_cast<unsigned long long>(d.weapon), static_cast<unsigned long long>(d.vdata),
      static_cast<unsigned long long>(d.source.services),
      static_cast<unsigned long long>(d.source.eye_function), d.source.eye_slot,
      d.source.eye_function_in_client, d.source.eye_call, d.source.local_eye.x,
      d.source.local_eye.y, d.source.local_eye.z, d.source.returned_eye.x, d.source.returned_eye.y,
      d.source.returned_eye.z, d.source.eye_distance, d.source.history_eye, d.players, d.enemies,
      d.targets, d.proposals, d.examined, d.fallback, d.rejected_immunity, d.rejected_time,
      d.rejected_model, d.rejected_changed, d.target, d.group, d.nominal_damage, d.expected_damage,
      d.minimum_damage, d.probability * 100, d.required_probability * 100, d.world_known,
      d.aim_ready, d.stop_requested, d.trigger, d.shot_requests, d.ammo_decreases, d.submitted,
      d.completed, d.worker_us, d.crc_writes, d.crc_failures, d.camera_restores);
  std::string out = b;
  char motion[512];
  const auto& m = d.movement;
  std::snprintf(motion, sizeof(motion),
                "Movement: strafe=%d bhop=%d state=%s steps=%d speed=%.2f goal=%.2f hop_mode=%d "
                "landed=%d age=%d when=%.6f strafe_writes=%llu hop_writes=%llu counter_wish=%.6f autostop_applied=%d autostop_path=%d trace_ready=%d trace_calls=%llu trace_hits=%llu\n",
                m.strafe, m.bhop, movement_debug_reason(m.reason), m.steps, m.speed, m.goal,
                m.hop_mode, m.landed_tick, m.landed_age, m.when, m.strafe_writes, m.hop_writes,
                m.counter_wish, m.autostop_applied, m.autostop_path, m.trace_ready, m.trace_calls, m.trace_hits);
  out += motion;
  const auto& c = d.choke;
  std::snprintf(motion, sizeof(motion),
      "Recovery choke: enabled=%d bound=%d deferred=%d held=%d/4 command=%d "
      "deferred_commands=%llu release_attempts=%llu penalty=%.6f range=%d..%d "
      "server_executed=%d sample_ms=%llu network=0x%llx\n",
      c.enabled, c.bound, c.deferred, c.held, c.command, c.deferred_commands,
      c.release_attempts, c.penalty, c.range_first, c.range_last, c.server_executed,
      c.sample_ms, static_cast<unsigned long long>(c.network));
  out += motion;
  std::snprintf(motion, sizeof(motion), "Last release: command=%d serialized=%d..%d horizontal_speed=%.3f\n",
                c.release_command, c.release_first, c.release_last, c.horizontal_speed);
  out += motion;

  for (std::size_t i = 0; i < k_rage_debug_stages; ++i) {
    out += rage_debug_stage_name(static_cast<RageDebugStage>(i));
    out += ": ";
    out += rage_debug_gate_name(d.gates[i]);
    out += '\n';
  }
  return out;
}
}  // namespace cs2::game
