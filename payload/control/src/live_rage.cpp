#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "cs2/control/live_rage.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "cs2/aim/reader.hpp"
#include "cs2/control/engine_ballistics.hpp"
#include "cs2/control/input.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/schema.hpp"
#include "cs2/tls/bootstrap.hpp"
namespace cs2::control {
namespace {
game::Vec3 center(const aim::Hitbox& h) {
  return h.is_capsule ? (h.capsule.a + h.capsule.b) * .5F : h.box.center;
}
int region(int group) {
  return group == 1 ? 0 : (group == 2 || group == 3 ? 1 : 2);
}
}  // namespace
AimProposals propose_shots(AimFrame frame) {
  AimProposals output{frame.local, frame.weapon, frame.captured_ms, frame.generation, {}};
  for (auto& target : frame.targets) {
    for (int part = 0; part < 3; ++part) {
      auto model = target.geometry;
      // Captured jobs are values. Reject callbacks instead of invoking live engine state on a
      // worker.
      model.world_query = {};
      model.target_query = {};
      model.world.clear();
      model.world_complete = true;
      model.occluders.clear();
      model.policy.require_known_world = false;
      model.policy.require_known_spread = true;
      model.policy.minimum_damage = 1;
      model.policy.minimum_probability = 0;
      model.policy.maximum_seeds = 16;
      model.policy.refine_starts = 1;
      model.policy.refine_iterations = 2;
      std::erase_if(model.hitboxes, [part](const auto& h) { return region(h.group) != part; });
      if (model.hitboxes.empty()) continue;
      const auto solved = aim::solve(model);
      if (!solved.valid || solved.metrics.nominal_hitbox < 0) continue;
      const auto& hitbox = model.hitboxes[static_cast<std::size_t>(solved.metrics.nominal_hitbox)];
      std::optional<float> distance;
      if (hitbox.is_capsule)
        distance =
            aim::intersect_capsule(model.eye, solved.direction, hitbox.capsule, model.weapon.range);
      else if (const auto hit =
                   aim::intersect_box(model.eye, solved.direction, hitbox.box, model.weapon.range))
        distance = hit->enter;
      if (!distance) continue;
      const auto point = model.eye + solved.direction * (*distance);
      output.entries.push_back(
          {model.pawn, target.index, hitbox.group, hitbox.bone, point - center(hitbox),
           std::min(solved.metrics.expected_damage, static_cast<float>(target.health))});
    }
  }
  std::stable_sort(output.entries.begin(), output.entries.end(),
                   [](const auto& a, const auto& b) { return a.score > b.score; });
  return output;
}
std::optional<game::Vec3> proposal_point(const CapturedTarget& target, const AimProposal& p) {
  if (target.geometry.pawn != p.pawn || target.index != p.index) return {};
  for (const auto& h : target.geometry.hitboxes)
    if (h.group == p.group && h.bone == p.bone) return center(h) + p.offset;
  return {};
}
CheckedProposal check_proposal(const AimFrame& frame, const CapturedTarget& target,
                               const AimProposal& proposal, const aim::WorldDamageQuery& world,
                               float minimum_damage, float probability) {
  CheckedProposal output;
  const auto point = proposal_point(target, proposal);
  if (!point || !world) return output;
  auto model = target.geometry;
  for (const auto& other : frame.targets)
    if (other.geometry.pawn != model.pawn)
      for (auto h : other.geometry.hitboxes) {
        h.multiplier = 0;
        model.hitboxes.push_back(h);
      }
  model.world_query = world;
  model.target_query = {};
  model.world.clear();
  model.world_complete = false;
  model.policy.require_known_world = true;
  model.policy.require_known_spread = true;
  model.policy.minimum_damage = std::min(minimum_damage, std::max(1.0F, target.health - .01F));
  model.policy.minimum_probability = probability;
  output.direction = game::normalized(*point - model.eye);
  const auto chart = model.basis.project(output.direction);
  if (!chart || !model.spread_known || model.kernel.empty()) return output;
  output.metrics = aim::evaluate_aim(model, *chart);
  const auto& metrics = output.metrics;
  output.accepted = metrics.world_known && metrics.nominal_damage >= model.policy.minimum_damage &&
                    metrics.expected_damage >= model.policy.minimum_damage &&
                    metrics.damage_probability >= model.policy.minimum_probability;
  return output;
}
LiveRage::~LiveRage() {
  stop();
}
void LiveRage::configure(bool enabled, bool fire) noexcept {
  if (enabled_.exchange(enabled) != enabled) generation_.fetch_add(1);
  fire_.store(fire);
}
void LiveRage::policy(float damage, float probability) noexcept {
  minimum_damage_.store(std::isfinite(damage) ? std::clamp(damage, 1.0F, 130.0F) : 20);
  probability_.store(std::isfinite(probability) ? std::clamp(probability, 0.0F, 1.0F) : .6F);
}
void LiveRage::stop() noexcept {
  choke_.stop();
  stopping_.store(true);
  enabled_.store(false);
  generation_.fetch_add(1);
  if (worker_.joinable()) {
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
  }
}
void LiveRage::submit(AimFrame frame) {
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return;
  if (stopping_.load()) return;
  pending_ = std::move(frame);
  submitted_.fetch_add(1);
  if (!worker_.joinable())
    worker_ = std::jthread([this](std::stop_token stop) {
      tls::ensure_thread_tls();
      while (!stop.stop_requested()) {
        AimFrame frame;
        {
          std::unique_lock guard(mutex_);
          if (!wake_.wait(guard, stop, [this] { return pending_.has_value(); })) break;
          frame = std::move(*pending_);
          pending_.reset();
        }
        const auto started = std::chrono::steady_clock::now();
        auto result = propose_shots(std::move(frame));
        worker_us_.store(std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count());
        completed_count_.fetch_add(1);
        {
          std::lock_guard guard(mutex_);
          completed_ = std::move(result);
        }
      }
    });
  lock.unlock();
  wake_.notify_one();
}
void LiveRage::diagnose(Input& input, const ShotState& state) noexcept {
  if (source_.debug().weapon && state.clip >= 0 && state.weapon == previous_weapon_ &&
      previous_clip_ >= 0 && state.clip < previous_clip_)
    ammo_decreases_.fetch_add(static_cast<unsigned>(previous_clip_ - state.clip));
  if (source_.debug().weapon && state.clip >= 0) {
    previous_weapon_ = state.weapon;
    previous_clip_ = state.clip;
  }
  const auto now = GetTickCount64();
  if (now - log_ms_ < 2000) return;
  log_ms_ = now;
  char line[512];
  std::snprintf(line, sizeof(line),
                "[rage-cycle] command=%d tick=%d next=%d ammo=%d reason=%s trigger=%s "
                "requests=%llu ammo_decreases=%llu damage=%.1f chance=%.0f%% source=%s",
                state.command, state.client_tick, state.next_primary, state.clip,
                shot_block_name(block_.load()), shot_block_name(input.shot_block()),
                static_cast<unsigned long long>(input.rage_shots()), ammo_decreases_.load(),
                damage_.load(), chance_.load() * 100, source_.diagnostic());
  game::append_log(line);
}
bool LiveRage::status(char* out, std::size_t size) const noexcept {
  if (!enabled_.load() || !out || !size) return false;
  std::snprintf(out, size, "rage: %s | damage %.1f HC %.0f%% | ammo used %llu",
                shot_block_name(block_.load()), damage_.load(), chance_.load() * 100,
                ammo_decreases_.load());
  return true;
}
bool LiveRage::debug(game::RageDebugSnapshot& out) const noexcept {
  std::unique_lock lock(debug_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  out = debug_;
  out.choke = choke_.debug();
  out.enabled = enabled_.load();
  out.auto_fire = fire_.load();
  if (out.minimum_damage <= 0) out.minimum_damage = minimum_damage_.load();
  out.required_probability = probability_.load();
  out.callbacks = callbacks_.load();
  out.unique_commands = unique_.load();
  out.duplicates = duplicates_.load();
  out.observer_ms = observer_ms_.load();
  out.submitted = submitted_.load();
  out.completed = completed_count_.load();
  out.worker_us = worker_us_.load();
  out.ammo_decreases = ammo_decreases_.load();
  return true;
}
void LiveRage::tick(Input& input, EngineBallistics& engine, void* command) noexcept {
  callbacks_.fetch_add(1);
  observer_ms_.store(GetTickCount64());
  game::RageDebugSnapshot diagnostic;
  diagnostic.captured_ms = GetTickCount64();
  using Stage = game::RageDebugStage;
  using Gate = game::RageDebugGate;
  auto gate = [&](Stage stage, Gate mark) {
    diagnostic.gates[static_cast<std::size_t>(stage)] = mark;
  };
  auto detail = [&](const char* text) {
    std::snprintf(diagnostic.detail, sizeof(diagnostic.detail), "%s", text);
  };
  bool publish = true;
  auto finish = [&] {
    if (!publish) return;
    std::unique_lock lock(debug_mutex_, std::try_to_lock);
    if (lock.owns_lock()) debug_ = diagnostic;
  };
  struct PublishOnExit {
    decltype(finish)& action;
    ~PublishOnExit() {
      action();
    }
  } guard{finish};
  gate(Stage::command, Gate::pass);

  if (stopping_.load() || !enabled_.load()) {
    choke_.observe({});
    detail("Ragebot is disabled");
    input.clear_rage_aim();
    input.set_shot_plan(false, false);
    return;
  }
  int number = 0;
  if (!command || !game::read(reinterpret_cast<std::uintptr_t>(command) + 8, number)) {
    gate(Stage::command, Gate::fail);
    choke_.observe({});
    detail("Command pointer/number unavailable");
    return;
  }
  if (number == last_command_) {
    duplicates_.fetch_add(1);
    publish = false;
    return;
  }
  choke_.observe({});
  unique_.fetch_add(1);
  diagnostic.command = number;
  last_command_ = number;
  input.clear_rage_aim();
  input.set_shot_plan(false, false);
  input.set_shot_state({});
  input.set_rage_fire(fire_.load());
  block_.store(ShotBlock::no_data);
  damage_.store(0);
  chance_.store(0);
  if (!reader_) reader_ = game::StateReader::attach();
  if (!reader_) {
    gate(Stage::local, Gate::fail);
    detail("Local state reader could not attach");
    return;
  }
  const auto snapshot = reader_->snapshot(true);
  if (!snapshot.valid || !snapshot.local || !snapshot.local->alive) {
    gate(Stage::local, Gate::fail);
    detail("Local snapshot is missing, invalid, or dead");
    reader_.reset();
    return;
  }
  gate(Stage::local, Gate::pass);
  diagnostic.pawn = snapshot.local->pawn;
  diagnostic.weapon = snapshot.local->weapon_entity;
  diagnostic.vdata = snapshot.local->weapon_vdata;
  diagnostic.snapshot_clip = snapshot.local->clip;
  diagnostic.players = static_cast<int>(snapshot.players.size());
  std::snprintf(diagnostic.weapon_name, sizeof(diagnostic.weapon_name), "%s",
                snapshot.local->weapon.c_str());
  input.note_local_pawn(snapshot.local->pawn);
  const auto module = game::find_module("client.dll");
  ShotState state;
  game::Vec3 eye;
  std::vector<aim::KernelSample> kernel;
  if (!source_.prepare(module)) {
    gate(Stage::bindings, Gate::fail);
    detail("Native accuracy/shoot-position signatures unavailable");
    diagnose(input, state);
    return;
  }
  gate(Stage::bindings, Gate::pass);
  const bool sampled = source_.sample(*snapshot.local, command, state, eye, kernel);
  diagnostic.source = source_.debug();
  diagnostic.clip = state.clip;
  diagnostic.reload = state.reloading;
  diagnostic.client_tick = state.client_tick;
  diagnostic.tick_base = state.tick_base;
  diagnostic.next_primary = state.next_primary;
  const std::pair<Stage, bool> source_gates[] = {{Stage::schema, diagnostic.source.schema},
                                                 {Stage::weapon, diagnostic.source.weapon},
                                                 {Stage::eye, diagnostic.source.eye},
                                                 {Stage::spread, diagnostic.source.spread}};
  for (const auto& [stage, passed] : source_gates) {
    gate(stage, passed ? Gate::pass : Gate::fail);
    if (!passed) break;
  }
  if (!sampled) {
    detail(source_.diagnostic());
    diagnose(input, state);
    return;
  }
  input.set_shot_state(state);
  if (choke_.enabled()) {
    if (!penalty_field_) penalty_field_ = game::schema_field("C_CSWeaponBase", "m_fAccuracyPenalty");
    if (!ground_field_) ground_field_ = game::schema_field("C_BaseEntity", "m_fFlags");
    if (!velocity_field_) velocity_field_ = game::schema_field("C_BaseEntity", "m_vecVelocity");
    float penalty = 0;
    game::Vec3 velocity{};
    std::uint32_t flags = 0;
    const bool valid = penalty_field_ && ground_field_ && velocity_field_ && !state.reloading && state.clip > 0 &&
        game::read(state.weapon + *penalty_field_, penalty) &&
        game::read(state.pawn + *ground_field_, flags) &&
        game::read(state.pawn + *velocity_field_, velocity);
    choke_.observe({valid, (flags & 1u) != 0, false, state.pawn, state.weapon,
                    reinterpret_cast<std::uintptr_t>(command), state.command, state.clip,
                    penalty, GetTickCount64(), std::hypot(velocity.x, velocity.y)});
  }
  if (state.reloading || state.clip <= 0) {
    gate(Stage::weapon, Gate::fail);
    detail(state.reloading ? "Weapon is reloading" : "Magazine is empty");
    block_.store(state.reloading ? ShotBlock::reload : ShotBlock::empty);
    diagnose(input, state);
    return;
  }
  if (!engine.initialize()) {
    gate(Stage::world, Gate::fail);
    detail("Engine bullet-trace backend unavailable");
    diagnose(input, state);
    return;
  }
  gate(Stage::world, Gate::pass);
  if (engine.recoil_ready())
    input.set_recoil_query(
        [](void* p, std::uintptr_t pawn, float* out) {
          return static_cast<EngineBallistics*>(p)->recoil(pawn, out);
        },
        &engine);
  AimFrame frame{state.pawn, state.weapon, GetTickCount64(), generation_.load(), {}};
  const auto simulation = game::schema_field("C_BaseEntity", "m_flSimulationTime");
  const auto immunity = game::schema_field("C_CSPlayerPawn", "m_bGunGameImmunity");
  if (!simulation || !immunity) {
    gate(Stage::targets, Gate::fail);
    detail("Target simulation/immunity schema unavailable");
    diagnose(input, state);
    return;
  }
  for (const auto& player : snapshot.players) {
    if (player.is_local || !player.alive || player.team == snapshot.local->team) continue;
    ++diagnostic.enemies;
    std::uint8_t immune = 1;
    float time = 0;
    if (!game::read(player.pawn + *immunity, immune) || immune ||
        !game::read(player.pawn + *simulation, time) || !std::isfinite(time) || time <= 0) {
      ++diagnostic.rejected_immunity;
      continue;
    }
    const float age = state.client_tick - time * 64;
    if (age < -2 || age > 64) {
      ++diagnostic.rejected_time;
      continue;
    }
    auto geometry = aim::build_input(snapshot, {}, player.index, true, true);
    if (!geometry || !geometry->weapon_known || geometry->hitboxes.empty() ||
        std::none_of(geometry->hitboxes.begin(), geometry->hitboxes.end(),
                     [](const auto& h) { return h.is_capsule; })) {
      ++diagnostic.rejected_model;
      continue;
    }
    geometry->eye = eye;
    geometry->kernel = kernel;
    geometry->spread_known = true;
    // Recenter the chart on the actual weapon shooting origin.
    const auto direction = game::normalized(player.eye_origin - eye);
    constexpr float degrees = 57.295779513F;
    geometry->basis =
        aim::basis_from_angles({-std::asin(std::clamp(direction.z, -1.0F, 1.0F)) * degrees,
                                std::atan2(direction.y, direction.x) * degrees, 0});
    float after = 0;
    if (!game::read(player.pawn + *simulation, after) || after != time) {
      ++diagnostic.rejected_changed;
      continue;
    }
    frame.targets.push_back({player.index, player.health, time, std::move(*geometry)});
  }
  diagnostic.targets = static_cast<int>(frame.targets.size());
  if (frame.targets.empty()) {
    gate(Stage::targets, Gate::fail);
    detail(diagnostic.enemies ? "All enemy geometry rejected; see rejection counts"
                              : "No living enemy players in snapshot");
    block_.store(ShotBlock::no_target);
    diagnose(input, state);
    return;
  }
  gate(Stage::targets, Gate::pass);
  {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() && completed_) {
      current_ = std::move(*completed_);
      completed_.reset();
    }
  }
  const auto now = GetTickCount64();
  if (current_.local != state.pawn || current_.weapon != state.weapon ||
      current_.generation != frame.generation || now - current_.captured_ms > 100) {
    diagnostic.fallback = true;
    // Current center candidates keep the pipeline usable while refinement is pending.
    // They go through the same live damage/spread acceptance as worker proposals.
    current_ = {state.pawn, state.weapon, now, frame.generation, {}};
    preferred_.reset();
    for (const auto& target : frame.targets) {
      bool included[3]{};
      for (const auto& h : target.geometry.hitboxes) {
        const int part = region(h.group);
        if (included[part]) continue;
        included[part] = true;
        current_.entries.push_back({target.geometry.pawn, target.index, h.group, h.bone, {}, 0});
      }
    }
  }
  diagnostic.proposals = static_cast<int>(current_.entries.size());
  gate(Stage::proposals, current_.entries.empty() ? Gate::waiting : Gate::pass);
  if (current_.entries.empty()) detail("Waiting for worker proposals / center candidates");
  bool target_found = false, accepted = false;
  aim::AimMetrics best_metrics;
  game::Vec3 best_direction{};
  int best_index = -1, best_group = -1, best_health = 0;
  float best_time = 0;
  const auto start = std::chrono::steady_clock::now();
  const auto count = current_.entries.size();
  std::size_t examined = 0;
  std::optional<AimProposal> chosen_proposal;
  const auto preferred = preferred_;
  // Rotate a bounded set: blocked heads cannot permanently starve body/other targets.
  for (; examined < count + (preferred ? 1 : 0) && examined < 6; ++examined) {
    const auto& proposal =
        (preferred && examined == 0)
            ? *preferred
            : current_.entries[(cursor_ + examined - (preferred ? 1 : 0)) % count];
    const auto it = std::find_if(frame.targets.begin(), frame.targets.end(), [&](const auto& t) {
      return t.geometry.pawn == proposal.pawn && t.index == proposal.index;
    });
    if (it == frame.targets.end()) continue;
    const auto checked = check_proposal(
        frame, *it, proposal, engine.query(state.pawn, snapshot.local->team, proposal.pawn),
        minimum_damage_.load(), probability_.load());
    const auto& metrics = checked.metrics;
    const auto direction = checked.direction;
    target_found = true;
    const bool passes = checked.accepted;
    diagnostic.world_known = diagnostic.world_known || metrics.world_known;
    diagnostic.nominal_damage = std::max(diagnostic.nominal_damage, metrics.nominal_damage);
    diagnostic.expected_damage = std::max(diagnostic.expected_damage, metrics.expected_damage);
    diagnostic.probability = std::max(diagnostic.probability, metrics.damage_probability);
    if ((passes && !accepted) ||
        (passes == accepted && metrics.expected_damage > best_metrics.expected_damage)) {
      best_metrics = metrics;
      best_direction = direction;
      best_index = it->index;
      best_health = it->health;
      best_group = proposal.group;
      best_time = it->simulation_time;
      accepted = passes;
      chosen_proposal = proposal;
    }
    if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(4)) {
      ++examined;
      break;
    }
  }
  diagnostic.examined = static_cast<int>(examined);
  if (count) cursor_ = (cursor_ + examined - (preferred && examined ? 1 : 0)) % count;
  if (accepted && chosen_proposal) preferred_ = chosen_proposal;
  if (!enabled_.load() || generation_.load() != frame.generation) {
    detail("Configuration generation changed; candidate discarded");
    input.clear_rage_aim();
    input.set_shot_plan(false, false);
    return;
  }
  if (target_found && best_index >= 0) {
    constexpr float degrees = 57.295779513F;
    const float pitch = -std::asin(std::clamp(best_direction.z, -1.0F, 1.0F)) * degrees;
    const float yaw = std::atan2(best_direction.y, best_direction.x) * degrees;
    auto timing = command_timing(command, best_time);
    timing.player_valid = state.history_eye;
    diagnostic.target = best_index;
    diagnostic.group = best_group;
    diagnostic.nominal_damage = best_metrics.nominal_damage;
    diagnostic.expected_damage = best_metrics.expected_damage;
    diagnostic.probability = best_metrics.damage_probability;
    diagnostic.world_known = best_metrics.world_known;
    diagnostic.minimum_damage =
        std::min(minimum_damage_.load(), std::max(1.0F, best_health - .01F));
    const bool damage_ok = best_metrics.world_known &&
                           best_metrics.nominal_damage >= diagnostic.minimum_damage &&
                           best_metrics.expected_damage >= diagnostic.minimum_damage;
    const bool chance_ok = best_metrics.damage_probability >= probability_.load();
    gate(Stage::damage, damage_ok ? Gate::pass : Gate::fail);
    gate(Stage::accuracy, chance_ok ? Gate::pass : Gate::fail);
    gate(Stage::history, timing.valid ? Gate::pass : Gate::fail);
    detail(!damage_ok      ? "Current trace damage below requirement / world result unknown"
           : !chance_ok    ? "Damage probability below configured hit chance"
           : !timing.valid ? "Target record timing invalid"
                           : "Candidate accepted; see trigger / serialization");
    accepted = accepted && timing.valid;
    const auto speed = std::sqrt(snapshot.local->velocity.x * snapshot.local->velocity.x +
                                 snapshot.local->velocity.y * snapshot.local->velocity.y);
    // Plan movement independently of attack. Poor accuracy can request braking without committing a
    // shot.
    const bool stop = !accepted && best_metrics.nominal_damage > 0 && speed > 1 &&
                      state.client_tick >= state.next_primary;
    diagnostic.aim_ready = accepted;
    diagnostic.stop_requested = stop;
    input.set_rage_weapon(snapshot.local->weapon_vdata, snapshot.local->scoped);
    input.set_shot_plan(accepted, stop, timing);
    input.set_rage_aim(pitch, yaw, best_index, best_group);
    damage_.store(best_metrics.expected_damage);
    chance_.store(best_metrics.damage_probability);
    ShotGate readiness;
    const auto reason = readiness.check(state);
    block_.store(reason != ShotBlock::ready ? reason
                                            : (accepted ? ShotBlock::ready : ShotBlock::accuracy));
  } else {
    gate(Stage::damage, Gate::fail);
    detail("No candidate produced a selectable damage result");
    block_.store(ShotBlock::no_target);
  }
  submit(std::move(frame));
  diagnose(input, state);
}
}  // namespace cs2::control
