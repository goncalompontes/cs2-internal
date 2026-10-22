#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include "cs2/aim/solver.hpp"
#include "cs2/control/shot_state.hpp"
#include "cs2/control/command_choke.hpp"
#include "cs2/game/state_reader.hpp"
namespace cs2::control {
class Input;
class EngineBallistics;
struct CapturedTarget {
  int index{}, health{};
  float simulation_time{};
  aim::SolveInput geometry;
};
struct AimFrame {
  std::uintptr_t local{}, weapon{};
  unsigned long long captured_ms{}, generation{};
  std::vector<CapturedTarget> targets;
};
struct AimProposal {
  std::uintptr_t pawn{};
  int index{}, group{}, bone{};
  game::Vec3 offset{};
  float score{};
};
struct AimProposals {
  std::uintptr_t local{}, weapon{};
  unsigned long long captured_ms{}, generation{};
  std::vector<AimProposal> entries;
};
// Pure geometry search: no pointers are dereferenced, no engine callbacks allowed.
AimProposals propose_shots(AimFrame frame);
std::optional<game::Vec3> proposal_point(const CapturedTarget& target, const AimProposal& proposal);
struct CheckedProposal {
  game::Vec3 direction{};
  aim::AimMetrics metrics{};
  bool accepted{};
};
CheckedProposal check_proposal(const AimFrame& frame, const CapturedTarget& target,
                               const AimProposal& proposal, const aim::WorldDamageQuery& world,
                               float minimum_damage, float probability);
class LiveRage {
 public:
  ~LiveRage();
  void configure(bool enabled, bool fire) noexcept;
  void policy(float damage, float probability) noexcept;
  bool recovery_choke(bool on) noexcept { return choke_.configure(on); }
  bool recovery_choke_enabled() const noexcept { return choke_.enabled(); }
  game::CommandChokeDebug choke_debug() const noexcept { return choke_.debug(); }
  void tick(Input& input, EngineBallistics& engine, void* command) noexcept;
  void stop() noexcept;
  bool status(char* output, std::size_t size) const noexcept;
  bool debug(game::RageDebugSnapshot& out) const noexcept;

 private:
  void submit(AimFrame frame);
  void diagnose(Input& input, const ShotState& state) noexcept;
  std::atomic<bool> enabled_{false}, fire_{false}, stopping_{false};
  std::atomic<float> minimum_damage_{20}, probability_{.6F};
  std::atomic<unsigned long long> generation_{0};
  std::atomic<ShotBlock> block_{ShotBlock::no_data};
  std::atomic<float> damage_{0}, chance_{0};
  std::atomic<unsigned long long> ammo_decreases_{0};
  RecoveryCommandChoke choke_;
  std::optional<std::uint32_t> penalty_field_, ground_field_, velocity_field_;
  NativeShotSource source_;
  std::optional<game::StateReader> reader_;
  mutable std::mutex debug_mutex_;
  game::RageDebugSnapshot debug_{};
  std::atomic<std::uint64_t> callbacks_{0}, unique_{0}, duplicates_{0}, observer_ms_{0},
      submitted_{0}, completed_count_{0}, worker_us_{0};
  std::mutex mutex_;
  std::condition_variable_any wake_;
  std::optional<AimFrame> pending_;
  std::optional<AimProposals> completed_;
  std::jthread worker_;
  AimProposals current_;
  std::optional<AimProposal> preferred_;
  std::uintptr_t previous_weapon_{};
  int previous_clip_{-1}, last_command_{-1};
  unsigned long long log_ms_{};
  std::size_t cursor_{};
};
}  // namespace cs2::control
